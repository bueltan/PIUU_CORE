# PIUU_CORE

CORE recibe audio PCM16LE mono a 16 kHz y eventos desde C3_EARS por UART,
y envía comandos START/STOP en sentido inverso. El inicio puede ser touch o wake.

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
again before its 20 ms yield, preserving a single owner for conversation state.

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

`PIUU_DAC_DIAGNOSTICS` está activo por defecto con `PIUU_DIAGNOSTICS`; definirlo
como `0` elimina el muestreo y los logs. `DAC_WRITE` conserva los primeros cinco
frames escritos de cada epoch y escrituras fallidas/incompletas, en una cola fija.
El loop imprime las muestras; la tarea de reproducción no imprime estos logs.
`t` es el instante anterior a `i2s_write`; `seq`, `epoch`, `write_us`, `err`,
`bytes/expected`, `peak` y `mean_abs` permiten correlacionar el PCM decodificado
con lo aceptado por I2S. Los niveles son valores absolutos PCM16 (0–32768), no
una detección de voz. `errors`, `incomplete` y `trace_drop` son acumulados.
Un envío aceptado por I2S no confirma que el DAC lo haya emitido por su salida
analógica. No se modifica el canal, DMA ni la política de reproducción.
