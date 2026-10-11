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
