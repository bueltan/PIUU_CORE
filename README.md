# PIUU_CORE

CORE recibe audio PCM16LE mono a 16 kHz y eventos desde C3_EARS por UART,
y envía comandos START/STOP en sentido inverso. Inicia mostrando PIUU_EYES.
Wake sigue disponible; para usar el micrófono de la UI, mantener touch más de
cinco segundos y utilizar el botón MIC de la interfaz anterior.

## Cableado CORE ↔ EARS

El LED RGB de CORE fue retirado físicamente para reutilizar sus GPIO.
El enlace usa **UART2 a 921600 baudios, 8N1 y lógica de 3,3 V**:

| Dirección | CORE CYD | EARS C3 |
| --- | --- | --- |
| CORE → EARS | TX GPIO17 (antiguo azul) | RX GPIO7 |
| EARS → CORE | RX GPIO16 (antiguo verde) | TX GPIO6 |
| Referencia | GND | GND |

UART0 (GPIO1/GPIO3) queda para programación y debug por USB.
El monitor serial usa **115200 baudios**. No conectar EARS al conector P1.
El DAC conserva BCK=26, WS=27 y DATA=22; GPIO21 controla la retroiluminación.
Las asignaciones EARS se centralizan en `src/config/ears_config.h`.

La configuración está aplicada en el firmware; falta cargar y validar en hardware.
Bluetooth queda pendiente. `null-signal-cyd` se integrará en código y no reserva UART.

Validación del cambio: suite host y compilaciones `cyd`/`cyd2usb` exitosas.

Ver [integración UART](docs/INTEGRACION_EARS_UART.md) para protocolo, pruebas y límites,
y [refactor](REFACTOR.md) para la separación de responsabilidades.

## Conversación continua y UDP v3

Actualizar CORE y backend juntos: **PIU3 / versión 3**. Wake o touch abre una
conversación; xAI `server_vad` detecta los turnos. El audio sigue transmitiéndose
durante respuestas. Touch cierra; wake durante respuesta interrumpe. CORE cierra
tras 30 segundos de silencio esperando una nueva intervención; el backend
cierra xAI inmediatamente, sin conservar una conexión idle cinco minutos.

El estado Connecting permite distinguir la apertura de xAI de la escucha normal.
El backend conserva audio de arranque en un búfer limitado; cada respuesta tiene
un ID distinto y requiere confirmación antes de comenzar la salida. Se mantienen
NACK acotado, silencio limitado ante pérdidas y recorte de historial al interrumpir.

Ver [contrato y responsabilidades v3](docs/PROTOCOLO_UDP_V3.md), límites y
pruebas. El documento v2 queda como referencia histórica.

## Logs del test integrado

Diagnóstico por USB a 115200: eventos de conversación/controles con IDs y
resúmenes EARS, UDP, audio y recursos cada cinco segundos, escalonados.
Ver [captura e interpretación de logs](docs/DIAGNOSTICOS_TEST_INTEGRADO.md).

Para guardar los logs de las tres placas automáticamente, iniciar **antes de
conectarlas** (desde esta carpeta):

```sh
~/.platformio/penv/bin/python tools/capture_serial.py
```

Espera los puertos USB, reconecta y guarda `core.log`, `keyword.log`, `ears.log`
y `capture.log` en una carpeta nueva dentro de `logs/integrated/` (ignorada por Git).
Terminar con Ctrl+C. Cerrar otros monitores antes de capturar y detener la captura
antes de subir firmware. No requiere modificar ni cargar las placas.

Para el test con archivo en EARS: backend disponible → encender CORE y KEYWORD →
esperar a que CORE conecte con el servidor y KEYWORD indique `[BOOT] listo` →
encender EARS. Así no se pierde el inicio del archivo por arrancar KEYWORD tarde.

## Verificación

Desde la carpeta Projects:

```sh
bash PIUU_CORE/test/run_host_tests.sh
pio run -d PIUU_CORE -e cyd -e cyd2usb
```

### Medir recursos de CORE

Las métricas de rendimiento están **desactivadas** para aislar las pérdidas UART:
`PIUU_PERFORMANCE_DIAGNOSTICS=0`. Esto retira las capturas de tareas/CPU, tiempos
de Opus/I2S y lecturas periódicas de stack. `loop_hwm_bytes=0` indica que esa
medición está desactivada. Se conservan los diagnósticos de conexión, heap y UART.
Para volver a medir recursos, cambiar el flag a `1` y recompilar.
Consultar [métricas de rendimiento](docs/DIAGNOSTICOS_TEST_INTEGRADO.md#rendimiento-de-core)
para interpretar CPU, stack, tiempos de audio y buffers.

### Recepción UART por bloques

CORE lee EARS sin espera en bloques de hasta 256 bytes y procesa un máximo de
1024 bytes por ciclo antes de ceder CPU. El parser admite cabeceras/payloads
fragmentados, eventos intercalados y varios paquetes en la misma lectura.
El scratch de 256 bytes es estático; no se reserva por frame ni añade una cola.
Se conservan baudrate, pines, buffers de driver/audio, prioridades y protocolo.
`UART_RX` permite verificar el tamaño medio/máximo real de las lecturas.

El umbral de interrupción RX se configura en `EarsConfig::RX_FIFO_FULL_THRESHOLD`
a **64 bytes**. En la FIFO de 128 bytes deja unos 694 us de margen a 921600
baudios, frente a los 87 us del valor predeterminado de 120. Esto aumenta la
frecuencia de interrupciones; la próxima captura debe verificar `fifo_threshold=64`
en `BOOT` y los incrementos de `UART_ERR fifo`/`rx_missing` durante el envío.
`UART_RX` y CPU se pueden comparar al volver a habilitar rendimiento.
El margen no garantiza ausencia de desbordamientos.

Si persisten, comparar una segunda prueba con `PIUU_PERFORMANCE_DIAGNOSTICS=0`
y `PIUU_UART_DIAGNOSTICS=1` para aislar el efecto de las mediciones de rendimiento.
Las capturas de tareas alcanzaron 8,4 ms en la prueba del 11 de octubre; ese tiempo
no demuestra por sí solo que las interrupciones estuvieran bloqueadas ni que
causaran las pérdidas. El buffer de driver no se llenó (`ring_full=0`); aumentar
su capacidad no resuelve directamente un desbordamiento previo en la FIFO.

## Optional UART investigation

`PIUU_UART_DIAGNOSTICS=1` in `platformio.ini` enables the new EARS link probes.
Set it to `0` and rebuild to remove their counters, sample ring, driver callback,
Arduino UART event task and logs. Existing diagnostics use `PIUU_DIAGNOSTICS`
and `PIUU_PERFORMANCE_DIAGNOSTICS` separately.

- `UART_ERR`: cumulative FIFO overflow, driver ring-full, framing, parity and
  break events; RX sequence gaps/missing frames, sequence resets and omitted samples.
- `UART_GAP`: exact expected/received sequence, arrival order and device timestamp.
  `stage=rx` observes the parser before application queues; `stage=app` observes
  the conversation consumer. Matching samples are the same gap, not two losses.
  A gap only in the application suggests application queue/admission handling.
  Stream acknowledgements that change state reset the RX baseline; repeated START
  acknowledgements do not. Application counters retain their own conversation boundary.

A fixed eight-sample ring bounds storage. Logs drain at most eight samples per
five seconds, spaced by at least 80 ms. `sample_drop` makes omitted detail visible.
The RX worker/event callback never formats these logs; loop() prints them.
The error callback creates an additional Arduino UART event task while enabled;
RAM/CPU overhead must be measured on hardware. Driver events are observations,
not byte-integrity guarantees: PCM! still has no checksum.

For correlated tests upload CORE and EARS with their diagnostics enabled. Keep
KEYWORD firmware and the same test audio: existing KEYWORD discontinuity/queue
logs plus EARS's `wake_*` timings cover that branch without changing its model.

## Optional UDP and connection diagnostics

`PIUU_NETWORK_DIAGNOSTICS=1` enables lightweight counters and bounded samples,
independently of performance measurements. Set it to `0` and rebuild to remove
the new UDP/downlink probes and connection log formatting. General periodic
`UDP_IO` output also requires `PIUU_DIAGNOSTICS=1`.

- `WIFI_CONNECT`: start/end of each connection attempt, status and elapsed time;
  no SSID or password is logged. `HELLO_SEND` reports request ID and `local_ok`;
  `HELLO_CONNECTED` reports elapsed time and attempts until a valid HELLO_ACK.
  HELLO sends are logged only while establishing the link, not healthy keepalives.
- `UDP_IO`: cumulative local send successes and failures (socket uninitialized,
  mutex busy, beginPacket failure, short write, endPacket failure), RX lock misses,
  empty reads and datagrams/bytes read. These counters do not prove network delivery
  or expose Wi-Fi/lwIP losses before the socket. Failed beginPacket and short writes
  are now reported as send failures instead of relying only on endPacket.
- `OPUS_RX`: per-epoch validated audio arrivals before jitter admission,
  forward sequence gaps, reorder/duplicate observations, largest arrival interval,
  intervals >=100 ms, arrival age, skipped frames and rebuffer durations.
- `OPUS_GAP`: timestamped `arrival`, `initial` and `resume` samples with expected/
  received sequence. `arrival` gaps can subsequently be filled by reordered packets;
  `forward_frames` is an observation count, not a final loss total. `skipped_frames`
  counts sequence positions omitted when playback starts/resumes and is separate
  from `AUDIO miss` recovery attempts. Do not add these overlapping counters.

An eight-entry fixed ring bounds samples; `sample_drop` reports omitted detail.
Summaries repeat every five seconds; at most eight detail samples drain per period,
spaced at least 80 ms apart. RX/playback workers update counters under the existing
jitter mutex; only loop() formats downlink logs, outside that mutex. Epoch changes
reset the observations and samples. Arrival intervals include transport/socket
and RX scheduling effects; they do not identify a network fault by themselves.

For the next comparison upload only CORE, keep EARS/KEYWORD/audio unchanged and
confirm BOOT reports `perf_diag=0 uart_diag=1 net_diag=1 fifo_threshold=64`.

### PCM consumer scheduling headroom

The EARS PCM queue defaults to 24 frames (480 ms) and can be set at compile time
with `PIUU_EARS_AUDIO_QUEUE_FRAMES`. Frames are consumed immediately; the queue
is not a mandatory playback/capture delay. This adds 11664 bytes of queue payload
versus the previous six frames. The loop services EARS both before UI work and
again before its bounded eye-rendering step and 1 ms yield, preserving a single owner for conversation state.

With `PIUU_UART_DIAGNOSTICS=1`, `EARS` includes `queue`, cumulative `peak` and
`poll_gap_max_ms`. These inexpensive counters remain available with performance
sampling disabled. They distinguish consumer scheduling stalls from UART FIFO
or driver overflow. Larger or sustained stalls can still fill this bounded queue;
measure these values and heap headroom in the next integrated test.

### Locating downlink UDP loss

`PIUU_NETWORK_DIAGNOSTICS=1` enables `UDP_RX_SERVICE` every five seconds. No
allocation or printing occurs in the RX task. The values are cumulative maxima
in microseconds since task startup, and include any preemption during measurement:

- `idle_max_us`: interval between RX iterations, including the deliberate 1 ms
  yield, scheduling delays and time when this task did not execute.
- `work_max_us`: complete active iteration, excluding its deliberate yield.
- `socket_max_us`: socket polling, including UDP mutex acquisition.
- `dispatch_max_us`: socket read and envelope/control dispatch.
- `queue_max_us`: Opus parsing and jitter insertion, including its mutex wait.

Compare these with `OPUS_RX` arrival gaps and `UDP_IO` lock failures. A long
arrival gap with short RX service times suggests loss before application insertion;
it alone does not distinguish the radio/network from lwIP socket drops. A long
service time identifies a local delay worth investigating. Timings do not directly
measure CPU utilization or socket overflow, and a successful send only confirms
local acceptance.

For the same integrated run, capture traffic in a second SSH terminal on the VPS:

```bash
sudo timeout 240s tcpdump -i any -nn -s 0 -w backend-udp-test.pcap 'udp port 9999'
```

Start capture before the devices. Keep the capture and matching backend/serial
logs. The capture contains PCM and Opus traffic. Inspect the final tcpdump capture
drop count before treating missing packets as evidence. Compare response IDs and
sequence numbers at server egress with those observed by CORE. VPS capture cannot
prove delivery to the ESP32; isolating radio loss from socket loss may require an
AP/router-side capture. This investigation does not change retransmission,
transport choice, task affinity, priorities or audio buffer sizes.

### Recuperación PCM uplink

CORE conserva 32 frames / 640 ms desde la captura en `SessionClient`, en un pool
reservado una sola vez al iniciar, sin asignaciones durante streaming (unos
13,1 KiB adicionales respecto de 12 frames). El backend espera hasta 400 ms por
NACK, con un intento de retransmisión por frame. PCM contiguo no agrega espera.
Actualizar CORE y backend para evaluar estos límites juntos; EARS y KEYWORD no
requieren cambios. El transporte sigue usando UDP v3 y el downlink no cambia.

`V3_STATS` diferencia `retx` exitosos, `retx_fail`, `cache_miss`, `cache_expired`
(subconjunto de misses) y `nack_age_max_ms` para frames encontrados en caché.
Comparar sus deltas con `INPUT_STATS.nack_recovered`, `missing` y `silence_frames`
del backend. Un frame solicitado que llega puede ser también un original tardío.
La instrumentación existente permite desactivar estos logs. Validar heap mínimo
bajo reproducción y conversación; las ventanas no cubren cortes de varios segundos.

### Diagnóstico del inicio de salida DAC

`PIUU_DAC_DIAGNOSTICS` está desactivado por defecto (`0`). Definirlo como `1`
activa temporalmente el muestreo y los logs. `DAC_WRITE` conserva los primeros cinco
frames escritos de cada epoch y escrituras fallidas/incompletas, en una cola fija.
El loop imprime las muestras; la tarea de reproducción no imprime estos logs.
`t` es el instante anterior a `i2s_write`; `seq`, `epoch`, `write_us`, `err`,
`bytes/expected`, `peak` y `mean_abs` permiten correlacionar el PCM decodificado
con lo aceptado por I2S. Los niveles son valores absolutos PCM16 (0–32768), no
una detección de voz. `errors`, `incomplete` y `trace_drop` son acumulados.
Un envío aceptado por I2S no confirma que el DAC lo haya emitido por su salida
analógica. No se modifica el canal, DMA ni la política de reproducción.

### Recuperación Opus downlink y fin confirmado

Actualizar CORE y backend juntos. HELLO_ACK anuncia la capacidad downlink con
el bit `0x02`; CORE rechaza un servidor sin esa capacidad (`INCOMPATIBLE_SERVER`).
El sobre UDP v3 añade `DOWNLINK_NACK=22`, `AUDIO_END=23`, `AUDIO_END_ACK=24`.
NACK lleva base uint32 + máscara uint16 big endian (ocho bits válidos), ligado
al cliente, servidor, conversación y respuesta. END conserva el cuerpo Opus END
con secuencia final exclusiva; su ACK solo se envía tras admitir esa frontera.
`PLAYED/PLAYED_ACK` sigue confirmando la reproducción y el drenaje estimado de DMA.

CORE conserva el jitter buffer actual. La recuperación usa 128 entradas de
metadatos (sin copiar audio), espera 40 ms de reordenamiento y genera como máximo
un NACK de ocho frames cada 20 ms, una vez por secuencia. Durante reproducción
normal no solicita frames con menos de ocho frames (160 ms) de margen. El
backend guarda 128 frames Opus por hasta tres segundos, retransmite una vez por
frame y sirve como máximo dos por ciclo de 20 ms. Si llegan tarde se descartan;
FEC/PLC continúa sin esperar indefinidamente.

El backend repite END cada 150 ms hasta recibir ACK; tras cinco segundos sin
confirmación falla explícitamente. CORE da hasta 400 ms al tramo final antes de
completar huecos con FEC/PLC. Sin END no declara una reproducción completa.
Cancelación, nueva respuesta y reconexión invalidan solicitudes/ACK antiguos.
Los límites CORE están en `src/audio/downlink_recovery.h`, los del backend en
`BridgeConfig`. No se cambian los umbrales de heartbeat.

`DOWNLINK_RECOVERY` muestra solicitudes, frames solicitados y llegadas posteriores
aceptadas. `recovered` también puede incluir un original retrasado; no garantiza
que cada llegada corresponda a una retransmisión. Comparar con `OUTPUT_RECOVERY_STATS`,
`OUTPUT_END_ACK`, `OUTPUT_END_TIMEOUT` y `PLAYBACK_FINISHED` del backend.


## PIUU_EYES integrado

Al arrancar se muestran ojos azules sobre fondo negro en horizontal (320×240,
rotación 1). La emoción inicial es `neutral`, con blink y glance automáticos.
Mantener touch **más de 5000 ms continuos** abre la UI anterior, conservada en
vertical (240×320, rotación 0), con sus ajustes WiFi/servidor y botón MIC. Un toque
corto en los ojos no acciona MIC. La liberación del toque largo no pulsa un botón
al cambiar de pantalla. La calibración de touch existente no cambia: para salir
de los ojos se mide el tiempo, sin depender de coordenadas.

La UI permanece abierta hasta reiniciar; `showEyesScreen()` permite volver desde
código. Los comandos y temporizadores visuales siguen funcionando mientras la
UI está abierta, pero no dibujan sobre ella. Al mostrar ojos nuevamente se limpia
la pantalla completa mediante franjas, incluso si el fondo no cambió.

`ConversationState::Playing` activa las variaciones automáticas de habla; al
terminar o interrumpir audio se desactivan. No se analiza PCM ni se modifica el
estado de conversación desde un comando visual. El estado de habla no es un
comando remoto y la secuencia automática es decorativa: no sincroniza palabras.
Las emociones base y gestos explícitos se controlan con los mensajes siguientes.

### Comandos compartidos por UDP y UART

Cada mensaje contiene **un comando ASCII de 1 a 31 bytes**, sin NUL, CR ni LF.
Los nombres son en inglés y minúsculas. Los valores inválidos y argumentos extra
se rechazan sin cambios parciales. Los ajustes son volátiles, no se guardan en NVS.

| Comando | Valores / efecto |
| --- | --- |
| `emotion NAME [hold_ms]` | neutral, happy, curious, calm, concerned, sad, excited |
| `gesture NAME` | none, soft_emphasis, emphasis, question, acknowledge, glance, blink, doubleblink, sway |
| `expr NAME [hold_ms]` | rest, connecting, listening, thinking, speaking, error, happy, curious, surprised, sad, sleepy, blink; también 0..11 |
| `size 25..200` | Porcentaje del tamaño de cada ojo |
| `x -160..160`, `y -120..120` | Offset horizontal/vertical en píxeles |
| `gap 0..260` | Separación entre centros en píxeles |
| `color NAME`, `background NAME` | blue, red, green, cyan, white, yellow, orange, purple, pink, black; también RRGGBB hexadecimal |
| `roundness 0..100` | 0 reproduce el primer prototipo; 1..100 usa contornos con suavizado geométrico |
| `duration 0..3000` | Duración de transición, en ms; inicialmente 260 |

`hold_ms` predeterminado es 5000; rango 0..600000. Se cuenta después de la
transición y luego se vuelve a neutral. Cero mantiene la pose hasta otro comando
o, para una pose UDP, hasta terminar/cancelar su respuesta. Los gestos se suman a
la emoción base, uno a la vez. Un nuevo gesto aceptado reemplaza al anterior;
`gesture none` cancela el actual, sin desactivar blink/glance o habla automáticos.
La orden `expr speaking` selecciona una pose; la reproducción real controla las
variaciones de habla. No se incluye el demo del proyecto independiente.

### Prioridad y caducidad

- UDP tiene prioridad sobre emociones, expresiones y gestos de EARS. La reserva
  dura `hold_ms + duration` para emoción/expresión y al menos 1000 ms para gesto.
  Un gesto no acorta una reserva UDP ya existente. Una pose persistente (`hold_ms=0`)
  reserva prioridad por 5000 ms; después EARS puede volver a controlar los ojos.
- Los comandos visuales UART recibidos durante esa reserva se descartan al
  procesarlos. No se guardan para ejecutarlos tarde. Los ajustes de color, fondo,
  tamaño, posición, separación, suavizado y transición de EARS siguen aceptándose.
  Para ajustes rige el último comando aplicado; se procesan UDP y después UART.
- Cada transporte tiene una cola estática independiente de cuatro mensajes.
  Cola llena descarta el nuevo mensaje; no espera al audio ni aborta la conversación.
  Un comando que lleva más de 1000 ms en cola se descarta.
- UDP exige la identidad de sesión, turno y respuesta vigente. Al terminar,
  interrumpir, cambiar de respuesta o reconectar se liberan sus gestos/emociones
  y su prioridad. Los ajustes permanecen. La secuencia UDP se valida contra
  duplicados y desorden, con rollover de uint32.

### UART desde EARS

Se conserva UART2 y el framing binario existente. Se agrega **EVT code 0x05**:

```
"EVT!" | version=1 (u8) | code=5 (u8) | length (u16 LE) | ASCII command
```

Ejemplo `x 1`: `45 56 54 21 01 05 03 00 78 20 31`.
EARS debe enviar estos bytes entre paquetes completos PCM!/EVT!, utilizando el
mismo escritor serial; nunca intercalar el comando dentro de un frame PCM.
No enviar líneas de texto sueltas ni comandos en UART0/USB: allí siguen los logs.
El receptor extrae EVT 0x05 antes de las colas críticas WAKE/STOP, de modo que una
ráfaga de gestos no consume su capacidad. No se agrega ACK ni reintento visual.
El firmware de EARS todavía debe invocar este envío; sus eventos existentes siguen
compatibles. Se puede construir un paquete sin enviarlo con:

```bash
python3 tools/eyes_command.py uart 'emotion happy 3000'
python3 tools/eyes_command.py uart 'background blue'
```

### UDP desde el backend

Se usa el socket existente de CORE, puerto **12000**, con el sobre PIU3 versión 3.
La extensión opcional es **kind=25 (Eyes)**, flags=0. El cuerpo es el mismo comando
ASCII. Cabecera de 38 bytes, todos los enteros big endian:

```
struct.pack('!4sBBHQQIIIH', b'PIU3', 3, 25, 0,
            client_session, server_session, turn_id, response_id,
            visual_sequence, len(command)) + command
```

`visual_sequence` es un contador uint32 independiente del de audio/eventos,
creciente para la identidad completa. Usar la identidad vigente obtenida por el
backend en la sesión, no constantes. Emoción/gesto/expr requieren turno y respuesta
no cero; ajustes también se permiten cuando la identidad vigente tiene ceros.
Esperar el ACK de `ResponseStarted` antes de enviar comandos de esa respuesta,
pues CORE descarta identidades futuras. Repetir el mismo número no vuelve a aplicar
un comando. Esta extensión es **best effort**, sin ACK ni garantía de entrega;
no se reintentan comandos como controles críticos y el audio no espera por ellos.

Ejemplo de construcción local (estos IDs de ejemplo no controlan una sesión real):

```bash
python3 tools/eyes_command.py udp 'emotion curious 2500' \
  --client 1 --server 2 --turn 3 --response 4 --sequence 1
```

Agregar `--host IP_DE_CORE` envía el datagrama; hacerlo con IDs reales. El script
por defecto sólo imprime bytes hexadecimales y no abre un puerto serial.

Esta entrega prepara **el receptor de CORE**. El backend actual sigue siendo
compatible para audio, pero su emisor debe añadir kind 25 y enviar cada paso de
`set_expression_sequence` según su cronología. CORE no recibe JSON ni planes
completos, ni reserva un framebuffer por paso. Los tiempos de estos comandos
empiezan al aplicarlos: aún no hay planificación por PTS ni sincronía exacta con
el DAC. No se modificó el backend ni el firmware de EARS en esta integración.

### Renderizado, extensión y recursos

`src/eyes/engine` contiene una copia del motor de PIUU_EYES, sin su `main.cpp`:
`eyes` define poses y transiciones, `gestures` compone variaciones, `contour`
calcula intersecciones y `renderer` envía regiones sucias. Las diferencias de
integración son `Renderer::invalidate()` para volver desde la UI y
`Gestures::cancel()` para reemplazar un gesto remoto. Mantener estas extensiones
al actualizar la copia; CORE compila sin depender de la carpeta PIUU_EYES.

`control` valida comandos y administra emoción, temporizador y prioridad.
`admission` valida identidad/secuencia UDP; `runtime` copia mensajes desde los
workers y aplica/renderiza sólo desde loop. La pantalla conserva un único dueño.
No se crea una tarea visual, no se usa LVGL/Bluetooth ni se asigna memoria por
comando o frame. El buffer de píxeles es **320×4×2 = 2560 bytes**. Las dos colas
contienen ocho mensajes de 64 bytes en esta compilación ESP32, más sus estructuras
FreeRTOS. El renderer completo ocupa 3232 bytes en esta compilación.

Para añadir una emoción, extender `Emotion`, su nombre y `emotionPose()`, y luego
el vocabulario del emisor/prompt. Para una expresión legacy, extender `Expression`,
`presets` y `names` en `eyes.cpp`. Para un gesto, extender `Gesture`, las tablas de
nombres/duración y `Gestures::apply()`, usando una envolvente que empiece y termine
en cero. Mantener un solo gesto activo y valores acotados. Los diseños se cambian
mediante geometría, sin guardar secuencias de imágenes en RAM. Cambiar las curvas
redondeadas en `contour.cpp`; preservar el camino legacy si `roundness 0` debe
seguir mostrando el prototipo original. Ejecutar la suite host tras actualizar.

Se presenta como máximo una franja de cuatro filas por vuelta de loop, con cesión
de 1 ms entre vueltas; los consumidores de audio/UART se atienden entre franjas.
La cadencia objetivo es 33 ms, pero una presentación en progreso termina antes
de comenzar otra. Una pose/configuración sin cambios no se retransmite. Cambiar
fondo o volver de la UI repinta toda la pantalla por franjas. Los tiempos no son
un límite de tiempo real bajo preempción.

`[EYES]` registra comandos aceptados/invalid/stale/suppressed/drops acumulados,
prioridad activa y métricas por ventana: frames terminados, peor paso, peor frame
y tiempo de render. `max_step_us` incluye preparación y transferencia;
`frame_max_us` incluye cesiones entre franjas. Comparar junto con `[RESOURCE]`,
`[EARS]` y pérdidas/rebuffer de audio, especialmente con `size 200`, cambios de
fondo y habla continua. La RAM estática compilada no equivale al heap libre con
WiFi/Opus/tareas activos; medir éste en la placa antes de concluir el margen real.

Validación local: suite host (prioridad, replay, caducidad, rollover, touch,
renderizado incremental y ráfagas aisladas de UART/UDP), compilaciones `cyd` y
`cyd2usb`. Queda pendiente cargar y verificar en CYD: orientación, toque largo,
colores, heap mínimo y audio continuo con comandos concurrentes.
