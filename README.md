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

Las métricas de rendimiento están activas para las pruebas actuales. En
`platformio.ini`, cambiar `-DPIUU_PERFORMANCE_DIAGNOSTICS=1` a
`-DPIUU_PERFORMANCE_DIAGNOSTICS=0` y recompilar para retirar sus mediciones y logs,
conservando los diagnósticos habituales de conexión y errores.
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
en `UART_RX`, los incrementos de `UART_ERR fifo`/`rx_missing` durante el envío y
el consumo de CPU. El margen no garantiza ausencia de desbordamientos.

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
