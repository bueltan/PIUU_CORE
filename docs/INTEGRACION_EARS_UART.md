> Actualización UDP v2: el tramo UART descrito aquí sigue vigente. Las referencias
> históricas a controles de servidor sin IDs e interrupción pendiente quedan
> reemplazadas por [PROTOCOLO_UDP_V2.md](PROTOCOLO_UDP_V2.md): touch/wake cancela
> y abre otro turno durante espera/reproducción.

# Integración CORE ↔ EARS por UART

Fecha: 2026-10-10. Firmware integrado y pruebas host; validación física pendiente.
No se modificaron EARS, KEYWORD ni el servidor Python en esta etapa.

## Cableado confirmado

| Señal | CORE CYD | EARS C3 |
| --- | --- | --- |
| CORE → EARS | TX GPIO17 | RX GPIO7 |
| EARS → CORE | RX GPIO16 | TX GPIO6 |
| Referencia | GND | GND |

UART2 de CORE, 921600 baudios, 8N1, niveles lógicos 3,3 V. No conectar niveles
RS-232. Pines y tamaños se centralizan en `src/config/ears_config.h`.

GPIO16/17 quedan disponibles porque se retiró físicamente el LED RGB. Se usan
los pads correspondientes a verde (16) y azul (17). No conectar EARS al conector
P1 GPIO1/3: UART0 queda para programación y logs por USB a 115200 baudios.
El DAC conserva BCK=26, WS=27 y DATA=22; GPIO21 sigue siendo la retroiluminación.
No se realizó carga de firmware ni validación eléctrica/física en esta etapa.

## Contratos de bytes

Todos los enteros multibyte de UART son little-endian. No confundir UART con
el protocolo UDP del servidor, que usa un encabezado de 18 bytes big-endian.

| Paquete | Encabezado | Payload |
| --- | --- | --- |
| PCM! | magic[4], sequence u32, payloadBytes u16 | exactamente 640 bytes PCM16LE mono, 320 muestras a 16 kHz |
| EVT! | magic[4], versión u8=1, evento u8, payloadBytes u16 | 0…32 bytes |
| CMD! | magic[4], versión u8=1, comando u8, payloadBytes u16=0 | ninguno |

CMD START_AUDIO=0x01; STOP_AUDIO=0x02. EARS confirma con EVT
CORE_STREAM_STATE=0x80 y un byte 0/1. El ACK es estado del transporte;
**no termina un turno**, no identifica una sesión ni correlaciona un comando.

El parser incremental acepta paquetes fragmentados, consecutivos y mezclados.
Consume el payload declarado sin buscar magic en su interior. Rechaza tamaños
PCM distintos de 640, versiones EVT distintas de 1 y eventos mayores de 32 bytes,
consumiendo el payload descartado. Una pausa de 100 ms abandona un paquete parcial.
No hay CRC: pérdida/corrupción de bytes puede dañar un frame antes de resincronizar;
el framing actual no permite garantizar detección de toda corrupción.

## Responsabilidades y memoria

- `protocol/ears_protocol`: parser y encoder puros, sin heap ni Arduino.
- `input/ears_link`: único worker que lee/escribe UART2. Core 0, prioridad 2,
  stack solicitado 4096 bytes, RX driver 4096 bytes y TX driver 256 bytes.
- Colas de seis frames PCM y ocho eventos, por valor y sin punteros prestados.
  Copiar PCM aquí mueve unos 32 kB/s; no se añadió malloc/free por frame.
- Un ordinal común y un mutex breve conservan el orden entre ambas colas.
  Loop intenta tomar ese mutex sin espera; el worker solo lo usa para publicar.
- PCM no ocupa capacidad de la cola de control. Si se satura audio, se descarta
  el frame entrante y se contabiliza. Si se pierden eventos, se aborta el turno
  local y se ignoran los mensajes hasta el ordinal observado al detectar la pérdida.
- Loop consume hasta ocho mensajes por pasada. Ninguna tarea UART toca TFT,
  conversación, configuración, NVS, uplink o UDP directamente.
- `app/ears_session` interpreta eventos y entrega PCM a uplink desde loop.
  Devuelve avisos semánticos; `ui_status` decide su presentación.
- `conversation_input` permite al coordinador pedir inicio/stop sin conocer
  HardwareSerial. La fuente se mantiene activa hasta terminar la respuesta/abortar,
  no solamente hasta cerrar la entrada.

UART, colas y stack solicitan aproximadamente 12,4 KiB dinámicos, sin metadata
RTOS/driver. Además existe el buffer fijo del parser. No son medidas de heap
mínimo ni de uso real del stack. Al desactivar logs por defecto se elimina el
worker de estadísticas de playback (stack solicitado anteriormente: 2048 bytes).

## Inicio, cierre y respuesta

### Inicio touch

1. Se exige WiFi, servicios, heartbeat del servidor y estado Idle.
2. EARS debe haber enviado un paquete válido dentro de los últimos 2500 ms.
3. Se toma un ordinal de inicio y se solicita CMD START de forma no bloqueante.
4. Los frames anteriores a ese ordinal se descartan: no pertenecen al turno nuevo.
5. Los frames aceptados se envían por UDP usando el contrato existente del servidor.

No se espera un ACK dentro de la tarea UI. El worker emite el nuevo estado
solicitado y lo reafirma cada 500 ms, incluido STOP estando Idle. Esto permite
reintentar comandos perdidos y obtener estado/liveness mientras EARS está apagado
hacia CORE. La recepción de ACK no prueba que el origen esté produciendo audio.

### Inicio wake

EVT WAKE_WORD=0x01 inicia desde Idle con las mismas condiciones de servicios.
EARS ya envía primero el evento y después habilita PCM; CORE usa el ordinal del
wake para conservar los frames posteriores que hayan llegado antes de consumirlo.
Un wake rechazado pide STOP, porque EARS pudo haber comenzado a transmitir.
PCM sin evento no inicia una conversación. Un wake repetido durante Listening
no resetea secuencia, buffers ni turno.

### Fin de entrada y reproducción

Touch o EVT STOP_WORD=0x03 pasan Listening a ClosingInput. STOP_WORD se adopta
aquí como cierre de entrada; no presupone un detector EARS de fin de turno nuevo.
END/COMMIT continúan siendo los existentes del servidor, sin sleeps en loop.

EARS **continúa enviando** durante ClosingInput, WaitingResponse y Playing.
CORE consume ese PCM sin agregarlo a la entrada cerrada y mantiene disponibles
los eventos. Al finalizar la respuesta solicita CMD STOP. Abort por WiFi, enlace
EARS perdido o cambio de configuración también pide STOP y cancela END/COMMIT
pendientes localmente.

Wake durante ClosingInput/WaitingResponse/Playing se reconoce y contabiliza,
pero todavía no reinicia/cancela al asistente. La interrupción real sigue pendiente
hasta disponer de IDs y cancelación en CORE/servidor. Reiniciar solo el cliente
podría mezclar respuestas antiguas con una nueva sesión.

### Otros eventos

LIGHTS_WORD, MUTE_WORD y códigos futuros no ejecutan acciones nuevas. Conservan
hasta 32 bytes y se entregan al hook opcional `earsSessionSetExtraEventSink`,
si se instala. El hook se ejecuta en loop, debe retornar pronto y no conservar
el puntero al payload. WAKE y STOP pueden llevar payload, aunque actualmente
sus acciones se resuelven solo por el código.

## Secuencia y recuperación

La secuencia EARS es global y no empieza en cero por turno. CORE toma el primer
frame aceptado como referencia y mantiene la secuencia UDP local del turno.
Duplicados EARS se descartan; saltos de hasta 250 frames perdidos se contabilizan
y avanzan secuencia/PTS UDP sin fabricar datos ni generar una ráfaga de silencios.
Retrocesos o saltos mayores abortan como discontinuidad. Se admite rollover u32.

**Límite del servidor actual:** el bridge concatena payloads y no reconstruye
silencio a partir de seq/PTS. Preservar esos metadatos permite diagnosticar pérdidas,
pero no recupera voz ni conserva automáticamente la duración de un hueco allí.
No se cambió el servidor para ocultar o rellenar esas pérdidas.

El peer se considera ausente tras 2500 ms sin paquetes válidos, incluidos ACK.
Eso aborta el turno local. No garantiza detección inmediata del reinicio de EARS
si sucede mientras no llegan frames; los ACK v1 carecen de identificador de boot.

## Diagnósticos y pruebas

Los logs de aplicación salen por UART0/USB a 115200 baudios. El enlace EARS
usa UART2 a 921600 y no comparte la consola. `PIUU_DEBUG_TX_PIN` sigue siendo
una alternativa opcional para logs por UART1 en otro GPIO libre; no usar los
pines de EARS. Los contadores periódicos EARS y el worker de estadísticas de
playback conservan su activación opcional mediante esa definición.

`earsLinkStats()` expone RX, drops de audio/eventos, errores de framing/ACK,
comandos enviados y último estado remoto. `earsSessionStats()` expone frames
enviados, gaps, duplicados, eventos extra y wakes ocupados. Son contadores locales;
no sustituyen mediciones de overrun del driver físico o de latencia extremo a extremo.

Desde Projects:

```sh
bash PIUU_CORE/test/run_host_tests.sh '/home/denis/Documents/Wall-e Project/Server Wall-e-PCM-OPUS/src/main.py'
pio run -d PIUU_CORE -e cyd -e cyd2usb
```

Las pruebas nuevas compilan el encoder PCM, handler CMD y relay EVT reales de
C3_EARS junto al parser/encoder CORE. Cubren fragmentación, payload con magic,
versiones/tamaños inválidos, timeout, touch/wake, STOP, secuencias, pérdida de
control, audio anterior al turno y transmisión continua durante reproducción.
Un worker UART host adicional verifica GPIO/baud, ACK, reenvío de comandos,
orden, saturación separada de colas y liveness. Los drivers son fakes host.

Validación previa al cambio a UART2: suite host completa, incluida compatibilidad con el bridge
Python indicado, y builds `cyd` y `cyd2usb` exitosos. Ambos reportan 109456 bytes
de RAM estática; flash: 1054068 bytes en `cyd` y 1053980 en `cyd2usb`.
Antes del cambio de pines también compiló una variante temporal `cyd_uart_debug` con
`PIUU_DEBUG_TX_PIN=17`; esto valida compilación, no disponibilidad física de ese
pin. Estas cifras de RAM no incluyen todas las asignaciones runtime ni prueban
latencia o margen de CPU.

Prueba física pendiente: EARS FILE_REPLAY actual (`test.raw`, PCM16LE mono 16 kHz),
inicio touch y wake con servidor disponible, cierre touch/STOP, respuesta por DAC,
reinicio/desconexión de EARS y WiFi, sesiones largas y medición de memoria/colas.
No afirmar funcionamiento de micrófono mientras EARS siga usando FILE_REPLAY.
