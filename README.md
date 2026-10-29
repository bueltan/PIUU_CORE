# PIUU_CORE

## Diagnóstico de bloqueo de audio (2026-10-29)

Con `PIUU_PLAYBACK_DIAGNOSTICS=1` (ya activado en `platformio.ini`), un fallo de
reproducción conserva evidencia adicional sin habilitar los logs detallados de
red. No cambia los timeouts, el buffer, la admisión de audio ni la recuperación.
Requiere recompilar/cargar CORE; EARS y PIUU_SERVER no necesitan cambios para
capturar estos registros.

| Registro | Datos e interpretación |
| --- | --- |
| `[PLAYBACK] stalled` | Epoch, secuencia esperada, mayor secuencia admitida, paquetes en cola, mínimo necesario para iniciar/reanudar, estado de reproducción/rebuffer y END. Permite distinguir falta de prebuffer y un bloqueo después de comenzar. |
| `AUDIO_QUEUE_FAULT` | Epoch solicitado/procesado y descartes por respuesta no esperada (`off`), cambio de epoch (`epoch_bad`), buffer lleno, datos malformados, duplicados, paquetes tardíos/adelantados y END rechazado. |
| `MEDIA_PATH` | Turno/respuesta y contadores desde el último `ResponseStarted`: datagramas completos leídos (`wire`), encabezado inválido, cliente incorrecto, snapshot de identidad no disponible, medios aceptados y rechazos por flags, sesión, turno, respuesta, capacidad o payload. `accepted` incluye audio y END válidos a nivel de transporte; no demuestra entrada al jitter ni reproducción. |
| `MEDIA_LAST` | Última secuencia aceptada, su antigüedad y muestra coherente del último rechazo: motivo, secuencia, turno/respuesta recibidos y esperados. Ignorar los campos del rechazo si `reject_valid=0`. |
| `UDP_FAULT` | Totales de datagramas/bytes leídos, mutex ocupado al parsear/leer, datagramas pendientes, lecturas vacías, sobredimensionadas y parciales. Ayuda a separar pérdida de paquetes y problemas de servicio del socket. |

`MEDIA_PATH` y `MEDIA_LAST` se capturan en el controlador **antes del reset de la
conversación**. Junto con `UDP_FAULT`, se imprimen una línea por vez, separadas por
al menos 100 ms; comparten un número `fault`. `t` en los dos primeros es la hora
local de captura; en UDP es la de impresión. Los contadores de UDP son acumulados
desde el arranque. En `AUDIO_QUEUE_FAULT`, `off`, `epoch_bad`, `full`, `malformed`
y `end_reject` son acumulados desde el arranque; duplicados, tardíos y adelantados
corresponden al stream actual. Las lecturas atómicas son observaciones cercanas,
no una transacción que congele todos los workers.

Ejemplo de interpretación para el fallo observado: si vuelve a quedar `seq=0`,
`queued=4` y `dec=0`, comparar `needed`, `MEDIA_PATH` y `AUDIO_QUEUE_FAULT`. Si
crecen `response_bad` o `session_bad`, hay rechazos por identidad; si los medios
pasan el transporte pero crecen los descartes de epoch o buffer, investigar su
admisión al jitter. Si no pasan más medios y tampoco crecen esos rechazos, revisar
recepción UDP y los contadores/avance del RX worker. Ninguna de esas señales por
sí sola confirma la causa del fallo.

Los contadores usan memoria fija y operaciones atómicas; no guardan audio, correos,
credenciales ni otros payloads. El RX worker solo cuenta y copia metadatos: no
formatea logs por paquete. La salida depende del transporte de diagnóstico elegido;
puede perderse si este se satura o CORE reinicia antes de terminar el informe.

Capturar por EARS, con su monitor serie cerrado:

```bash
/home/denis/.platformio/penv/bin/python tools/capture_serial.py --only ears
```

Validación: suite host completa aprobada, razones de rechazo y muestras concurrentes,
contadores UDP con diagnóstico de red/playback/apagado, captura previa al reset y
salida espaciada con playback on/off, y callback de fallo ignorando eventos viejos.
`cyd-uart0` compiló correctamente. No se cargó firmware; falta reproducir el fallo
en hardware con esta instrumentación.

## Espera sin audio y cierre por inactividad (2026-10-29)

Después de **20 segundos** sin habla en `Listening`, CORE entra en **Standby**:
detiene el streaming PCM de EARS y deja de enviar audio al backend, conservando
la conversación y los mensajes de control/heartbeat. La detección local de la
palabra de activación permanece habilitada; no se envía audio ambiental durante
la espera. No se emite CANCEL ni se reinician el turno, el epoch o la secuencia
PCM. Los frames UART pendientes de la pausa se descartan.

La **palabra de activación o el botón MIC** reactivan el micrófono en esa misma
conversación. La pantalla muestra `Standby / Wake word or tap MIC`. No se envía un
INTERRUPT al reanudar; eso evita que Gemini cierre por una interrupción manual
que no soporta. Una respuesta tardía de una herramienta puede reproducirse, pero
no reactiva por sí sola el micrófono de una conversación en Standby.

Después de **15 minutos en Standby sin activación del usuario**, CORE cierra la
sesión con motivo `standby_timeout`. La siguiente activación abre una nueva.
Reanudar antes del vencimiento cancela ese plazo; al volver a Standby empieza
otro completo. Los valores se configuran al compilar:

```ini
-DCONVERSATION_IDLE_TIMEOUT_MS=20000u
-DCONVERSATION_STANDBY_TIMEOUT_MS=900000u
```

Agregar esas opciones a los `build_flags` del entorno. El límite de Standby debe ser
positivo y menor que la mitad del rango de `millis()`.

Ambos contadores usan un reloj de actividad que se **pausa tras 4 segundos sin
tráfico válido del servidor** y continúa con el tiempo restante cuando vuelve.
HELLO/PING se envían cada 3 s; se permite 1 s de margen. Los paquetes inválidos o
de otra sesión no renuevan el reloj. Un backend ocupado que responde no equivale
a una desconexión.

La pausa no elimina el timeout definitivo del enlace de **6 segundos**, los
fallos de controles/ACK, la pérdida real de WiFi, los fallos de audio ni el límite
inicial de 20 s de los recordatorios. Ante esos errores se cierra y la próxima
activación abre una conversación nueva. El reloj de silencio no se evalúa en
`WaitingResponse` (Thinking) ni durante reproducción. Se mantiene un watchdog de
120 s para una detección de habla atascada con energía local continua.

Cada cierre activo conserva un resumen fijo en RAM: motivo, uptime, estado previo,
epoch, silencio local/del proveedor y estado del micrófono. Las limpiezas en Idle
no sobrescriben la causa. Se pierde al reiniciar y no se escribe en flash.
`conversationLastClose()` está disponible con los logs apagados. La pantalla
inicial muestra `Conversation ended` y el motivo durante cinco segundos; los
cierres manuales y por cambios de ajustes no generan ese aviso.

Con una salida de logs habilitada se emiten registros compactos, independientes
de los diagnósticos detallados:

```text
[MIC_GATE t=...] standby=silence session_preserved=1 provider_quiet_ms=... local_quiet_ms=...
[MIC_GATE] resumed=user_activation session_preserved=1
[CONV_TIMER t=...] silence=paused reason=server_traffic
[CONV_TIMER t=...] silence=resumed reason=server_traffic
[CONV_CLOSE t=...] reason=standby_timeout state=Listening epoch=... provider_quiet_ms=... local_quiet_ms=... speaking=0 muted=1
```

En `cyd-uart0` los logs salen por EARS hacia su USB con `PIUU_EARS_LOG=1`. Con
todas las salidas apagadas, el resumen RAM y el aviso de pantalla siguen funcionando;
no se envían registros retrospectivos al backend.

**Límites del proveedor:** 15 minutos es la política local máxima de espera, no
una garantía de duración de Gemini. Google limita la conexión WebSocket a unos
10 minutos y envía `GoAway`; nuestro backend todavía cierra ante ese evento.
La compresión de contexto se habilitó en PIUU_SERVER para reducir el crecimiento
del historial, pero la reanudación de Gemini a través de otra conexión sigue
pendiente. Los detalles antiguos pueden salir de la ventana de contexto comprimida.
Ver [gestión de sesiones de Google](https://ai.google.dev/gemini-api/docs/live-api/session-management).

El backend actual ya mantiene la conversación con PING aunque no reciba PCM.
Los avisos proactivos de Calendar pueden usar la conversación conservada si ambos
extremos negocian `CAP_STANDBY_NOTIFICATIONS=0x0020` y CORE confirma que sigue en
Standby. Hablar, esperar una respuesta, reproducir audio o tener herramientas
pendientes aplaza el aviso. No se cierra Gemini ni se reinicia la secuencia PCM.

Validación: suite host completa con controlador en diagnósticos on/off; pausa,
reactivación por wake/botón sin CANCEL ni reinicio PCM, respuesta tardía con micrófono
pausado, cierre exacto tras 15 minutos, reinicio del plazo, reloj congelado sin
enlace, UI y rollover. Backend: 216 pruebas offline, incluyendo 30 minutos
simulados de PING sin audio y reanudación del mismo turno. `cyd-uart0` compiló.
Falta cargar firmware y validar en hardware; no requiere cambiar EARS. Actualizar
PIUU_SERVER para incorporar la compresión de contexto.

## Avisos de Calendar desde el backend (2026-10-29)

Nueva capacidad opcional UDP v3 `CAP_NOTIFICATIONS=0x0010` y paquete `NOTICE=28`.
CORE puede aceptar una solicitud del backend cuando está inactivo y abrir una
conversación con START correlacionado. El micrófono se silencia durante la apertura
del recordatorio y retoma la escucha después de reproducirlo. Una conversación
activa fuera de Standby tiene prioridad y no es interrumpida. Los avisos duplicados, de otra sesión
o sin capacidad negociada se ignoran; la espera de respuesta inicial tiene un
límite de 20 segundos.

Para una sesión en Standby, `NOTICE` lleva el turno actual y CORE responde con
`NOTICE_ACK=29` (cuerpo vacío; flags=1 acepta, 0 difiere). La aceptación reserva
la sesión con el micrófono pausado; los duplicados sólo repiten el ACK. El servidor
revalida estado/vigencia, persiste el intento y envía texto a Gemini ya conectado.
Una activación manual anterior a la aceptación tiene prioridad. Después del aviso
CORE reactiva EARS con la guarda de eco habitual; una respuesta tardía de una
herramienta sin esta admisión conserva el micrófono en Standby. Si no llega una
respuesta inicial en 20 segundos, libera la reserva conservando la sesión.

**Actualizar primero PIUU_SERVER y después CORE**: los servidores anteriores
rechazan el nuevo bit de HELLO. El servidor actualizado mantiene los avisos desde
Idle para firmware anterior. Mantener `PIUU_CALENDAR_REMINDERS_ENABLED=1` en el
servicio. No requiere cambios en EARS/KEYWORD ni carga automática de firmware.

Requiere el backend Calendar con avisos habilitados y cargar este firmware en CORE.
La capacidad es opcional y no cambia las sesiones manuales. La planificación,
consulta de Google y persistencia de avisos corresponden al backend. Ver
[protocolo UDP v3](docs/PROTOCOLO_UDP_V3.md#avisos-proactivos-opcionales-2026-10-29)
y [Calendar MCP del backend](../PIUU_SERVER/CALENDAR_MCP.md).
Pasaron los tests host y la interoperabilidad Python/C++; `cyd-uart0` compiló
correctamente. No se cargó firmware; falta la prueba de aviso real.

## Coordinación WiFi y recuperación del servidor (2026-10-28)

La conectividad refleja el estado real del driver (`WL_CONNECTED`). Un trabajo
WiFi pendiente no equivale a una desconexión: escanear estando conectado ya no
aborta la conversación ni cambia la identidad UDP. Al terminar un escaneo tampoco
se reinicia el enlace con el servidor. Una pérdida real de conectividad conserva
su tratamiento de cierre y nueva negociación.

El worker serializa las operaciones explícitas, pausa la reconexión automática
de Arduino mientras las ejecuta y restaura la política anterior al terminar.
Como prueba diagnóstica, espera hasta 60 segundos por intento; si falla la cancelación, no inicia
otra conexión ni un escaneo que compita con el intento pendiente. Los errores de
conexión muestran `Connection not established`, sin atribuirlos automáticamente
a la contraseña. El framework, los núcleos y las prioridades RTOS no cambiaron.

El HELLO anuncia la MAC WiFi STA como identidad estable del dispositivo mediante
`CAP_DEVICE_MAC=0x0008`, además del nonce variable de sesión. CORE exige que el
backend confirme esa extensión. El mismo dispositivo puede recuperar el enlace
cuando cambien la IP, el puerto NAT o la identidad de sesión; el backend cierra
primero la conversación anterior. Recuperar el transporte no reanuda el historial:
hace falta abrir otra conversación mediante wake/touch. Incluso estando inactivo,
CORE renueva su nonce después de seis segundos sin tráfico válido del servidor.

**Actualizar primero PIUU_SERVER y después CORE.** El backend asocia la primera
MAC declarada durante la vida del proceso y rechaza otra MAC; no hay una clave
compartida ni verificación criptográfica todavía. Ver el [contrato de identidad
MAC](docs/PROTOCOLO_UDP_V3.md#identidad-estable-de-core-y-reconexión-udp).

Validación realizada en este chat: pruebas host de CORE, interoperabilidad con
PIUU_SERVER (incluido el HELLO real de C++ decodificado en Python) y compilación
de `cyd-uart0`. Para repetir desde este directorio:

```bash
bash test/run_host_tests.sh ../PIUU_SERVER
pio run -e cyd-uart0
```

La recepción UDP también corrige el bloqueo por contención descrito abajo.
Falta validar estas correcciones con los microcortes WiFi reales: las pruebas
host y la compilación no confirman la causa de la pérdida inicial del servidor.
Esta revisión no carga automáticamente firmware ni despliega el backend.

## Recepción UDP después de un fallo temporal del mutex

El socket de audio/control comparte un mutex entre TX y un único consumidor RX.
`networkUdpParsePacket()` comprueba primero `udp.available()`: si la lectura
anterior no consiguió el mutex o devolvió cero bytes, el mismo datagrama sigue
pendiente y vuelve a anunciarse al consumidor. Esto evita el bloqueo de la
biblioteca Arduino, cuyo `parsePacket()` devuelve cero mientras queda un buffer
RX sin consumir. Los paquetes válidos se entregan completos y sin cambios.

Un datagrama que supera el buffer del consumidor, o una lectura parcial que ya
consumió bytes, se descarta entero con `udp.clear()` bajo el mismo mutex. Su cola
no se interpreta como otro mensaje. Se conservan los límites actuales: TX no
espera el mutex y cada operación RX espera como máximo 20 ms. No se añaden tareas,
colas ni buffers; el formato UDP v3 y el backend no requieren cambios.

Con `PIUU_NETWORK_DIAGNOSTICS=1`, `UDP_IO` incluye `rx_pending` (consultas que
reutilizan un datagrama pendiente), `rx_oversized` y `rx_short` (descartes). Los
contadores no son cantidad de frames de audio recuperados. Con diagnósticos
apagados no se reservan esos contadores. El test host simula el comportamiento
real de `NetworkUDP` y un TX concurrente que retiene el mutex entre parse y read;
comprueba reintento, conservación de bytes/orden, lecturas cero y descarte de
colas, con diagnósticos habilitados y deshabilitados.

## Logs de CORE por EARS → USB (predeterminado en UART0)

`cyd-uart0` y `cyd2usb-uart0` activan `PIUU_EARS_LOG=1`, con UDP y SD
log desactivados. Actualizar **EARS primero**, usando `esp32-c3-ears`, y luego
CORE. Se usa el cableado UART existente a 921600 baud; EARS expone los logs
por su USB CDC nativo (`/dev/esp32-ears`, GPIO18/19), independiente de UART0
GPIO6/7. No hace falta conectar el USB de CORE ni que funcione su WiFi.

Desde PIUU_CORE, con el monitor serie de EARS cerrado:

```bash
/home/denis/.platformio/penv/bin/python tools/capture_serial.py --only ears
```

La captura imprime el directorio de salida y guarda `ears.log` con los mensajes
de ambas placas y hora UTC de recepción. Ctrl+C cierra la captura. `--only ears`
evita abrir CORE y KEYWORD; no envía comandos de mantenimiento ni secuencias de
reset. Se conservan `--ears`, `--output` y la reconexión automática del puerto.
Ejemplo del puente:

```text
[CORE boot=1234abcd seq=12 t=15000 reset=1 lost_bytes=0 usb_drop=0 bad_crc=0] [WIFI_HISTORY t=14980] event=disconnected reason=204 ...
```

Cada línea representa un fragmento de hasta 192 bytes, no necesariamente una
línea completa original; saltos y controles se convierten en espacios. `boot`
cambia en cada arranque, `seq` permite detectar huecos, `t` es el tiempo del
registro en CORE, y `reset` es el valor numérico de `esp_reset_reason()` de ese
arranque (por ejemplo 1=power-on, 3=software, 4=panic, 6=task watchdog,
9=brownout). `lost_bytes` cuenta bytes descartados en CORE; `usb_drop` cuenta
fragmentos descartados en EARS; `bad_crc` cuenta paquetes de diagnóstico
inválidos. La primera secuencia recibida solo establece una referencia.

El historial WiFi de 64 eventos se drena sin depender de la red, antes de los
logs generales. Se mantienen callbacks sin formato ni I/O. `[WIFI_JOB]` registra
inicio/fin de Scan/Connect, estado, resultado, memoria interna, mayor bloque
libre y pila disponible del worker. `[CORE_LOG]`, cada cinco segundos, informa
build, heap, memoria interna, mayor bloque y pila del dueño UART. Estado e IP
se observan en los eventos WiFi; el transporte UART no consulta APIs de red.
Los registros no incluyen contraseña ni SSID.

CORE utiliza cuatro fragmentos estáticos (768 bytes de texto) y su tarea UART
existente: sin tarea, socket ni cola dinámica adicionales. El dueño UART atiende
comandos y RX antes de logs, reserva espacio para un comando y transmite como
máximo 25 paquetes/s (4.8 kB/s de texto). EARS conserva ocho fragmentos, valida
CRC y escribe solo si USB tiene espacio; el timeout USB de diagnóstico es cero.
Las colas llenas descartan los fragmentos antiguos y cuentan pérdidas. Durante
mantenimiento se suspende este puente y se restablece el timeout del transporte
de programación; al salir vuelve a cero. No se cambia el formato PCM/EVT.

Los logs ya guardados en la computadora sobreviven a un reinicio de CORE.
Los pendientes en su RAM se pierden; EARS no persiste logs en flash. El canal
transporta logs de aplicación y el motivo del nuevo arranque, **no** salida ROM
ni el volcado completo de un panic. Si CORE se bloquea antes de transmitir,
los últimos registros pueden perderse. No hay ACK ni retransmisión.

Validación: `bash test/run_ears_log_tests.sh` prueba salida real del logger CORE
contra el receptor EARS, fragmentación, CRC, colas llenas, productores concurrentes,
USB sin espacio y continuidad de comandos. Compilaciones y pruebas host no
sustituyen validar audio, corte WiFi, reconexión y reinicio en las placas.

## Prueba de espera de IP: 60 segundos (2026-10-29)

El worker de WiFi deja hasta **60000 ms desde que retorna `WiFi.begin`** para que
el mismo intento obtenga IP, en lugar de 15000 ms. Esto aplica tanto al arranque
como a Connect manual. No añade otro `WiFi.begin`, reconexiones automáticas de la
aplicación ni reinicios de DHCP/radio. Al vencer mantiene la cancelación existente
y restaura la política previa de auto-reconnect. La UI sigue ocupada durante la
espera; no encadenar otro Scan/Connect hasta que termine. El plazo no incluye
el tiempo de preparación/cancelación o bloqueo interno de llamadas al SDK.

Con `PIUU_WIFI_DIAGNOSTICS=1`, `[WIFI_WAIT]` se emite desde el worker cada cinco
segundos mientras espera y una vez al terminar (`final=1`), antes de cancelar si
vence el plazo. Incluye:

- `elapsed_ms`, `status` de Arduino y `assoc` obtenido del driver WiFi.
- RSSI en dBm si está asociado (`rssi=0` cuando `assoc=0` no es una medición).
- `dhcp=init|started|stopped|unknown` a partir de ESP-NETIF, más `dhcp_err`.
- IP observada y `ip_err`; una consulta fallida se muestra como 0.0.0.0 y se
  distingue de una IP aún no obtenida mediante el código de error.

`dhcp=started` indica que el cliente está iniciado; **no** demuestra que haya
recibido OFFER/ACK ni identifica por sí solo el paso exacto del intercambio DHCP.
No se imprimen SSID ni contraseña. El transporte de logs sigue independiente del
WiFi mediante EARS; no se agregan tareas ni colas a esta prueba.

Cargar únicamente CORE (`cyd-uart0`), mantener la captura EARS, hacer
Scan → misma red → Connect una vez y esperar su resultado hasta un minuto.
Si obtiene IP después de 15 s sin otro intento, el plazo anterior era insuficiente
para ese caso. Si a los 60 s sigue asociado sin IP, conservar la captura antes de
reintentar y revisar el estado DHCP/router. Esta prueba no da por corregida la causa.

Pruebas host: conexión a los 20 s sin segundo begin, cancelación a los 60 s,
política de reconexión restaurada, observaciones DHCP y rollover de `millis()`.

## Diagnóstico mínimo de reproducción (2026-10-29)

`PIUU_PLAYBACK_DIAGNOSTICS=1` en `platformio.ini` añade cuatro líneas cada cinco
segundos, separadas al menos 100 ms, por el sink de logs elegido. Es independiente
de `PIUU_DIAGNOSTICS`, `PIUU_NETWORK_DIAGNOSTICS` y `PIUU_DAC_DIAGNOSTICS`; estos
últimos pueden permanecer en cero. Solo requiere actualizar CORE; el puente EARS
ya instalado reconoce estos fragmentos de texto. Capturar con `--only ears`.

- `AUDIO_RX`: epochs solicitado/procesado, espera de respuesta, paquetes aceptados,
  decodificados, errores, descartes y PLC. `rx_age_ms` viene del snapshot del buffer.
- `AUDIO_BUFFER`: snapshot de secuencia esperada, mayor secuencia recibida, cola,
  END aceptado y límite final, y rebuffer. `locked=0` indica que no se pudo obtener
  el mutex inmediatamente: los campos del snapshot son inválidos, no ceros reales.
  `snap_t` y `epoch` identifican cuándo y para qué respuesta se capturaron.
- `AUDIO_WORKER`: fase (`idle`, `poll`, `flush`, `reset`, `jitter`, `pop`, `decode`,
  `output`, `advance`, `finished`), antigüedad de fase/última vuelta/último avance
  de secuencia y estimación de reproducción. `flags` contiene bits: 1=inicio
  notificado, 2=reproducción terminada, 4=evento final notificado.
- `AUDIO_WRITE`: llamadas **completadas** a `audioOutputWrite`, duración última/máxima
  en microsegundos, último error y bytes aceptados frente a solicitados. Incluye
  escalado de volumen y las escrituras I2S de esa llamada completa. END tiene
  contadores `end_ok`/`end_reject`, última secuencia y epoch examinados por
  `markEndPacket`; un END rechazado por filtros anteriores no llega a ese contador.

Si `phase=output`, la edad y las vueltas dejan de avanzar y `calls` no aumenta,
la llamada de salida aún no devolvió. `last_us`, `err` y `bytes` describen la última
llamada terminada; `wanted` ya corresponde a la llamada en curso. `phase=decode`
separa ese caso del decodificador. `end=1` confirma que el END entró en el buffer;
`flags` permite distinguir terminar el audio de entregar el evento al controlador.
Edades sin actividad previa se miden desde el arranque, no prueban un bloqueo.

Los contadores RX/decodificación se reinician por respuesta; END, escrituras y
máximo de duración se acumulan desde el arranque. Los campos atómicos se observan
sin bloquear y pueden cambiar entre líneas; correlacionar por tiempo/epoch.
El snapshot del buffer usa mutex sin espera y se libera antes de formatear.
No se añaden tareas, colas, muestras PCM ni buffers de trazas. No imprime registros
nuevos desde reproducción/RX ni cambia los plazos de I2S o la lógica de recuperación.
Desactivar con `PIUU_PLAYBACK_DIAGNOSTICS=0` y recompilar.

Prueba inicial: una conversación sin Scan/Connect; si queda en Playing, mantener
la captura activa y revisar estos cuatro grupos. Después probar WiFi por separado.
`test/diagnostics/test_audio_progress.cpp` verifica observación concurrente de una
salida bloqueada, escrituras parciales/error y rollover de reloj.

## CORE logs over LAN UDP (optional)


For an optional UDP build, set `PIUU_EARS_LOG=0`, `PIUU_UDP_LOG=1` and
`PIUU_SD_LOG=0` in the desired environment. The three sinks are mutually
exclusive. Logs go directly to a PC on CORE's WiFi subnet, not to the voice
backend. UART0 remains a binary EARS link.

After loading CORE, find its WiFi IPv4 address in the router's client list or
network UI. On a PC on the same subnet, run from the PIUU_CORE directory:

```bash
python3 tools/capture_udp.py --host 192.168.1.123
# Optional fixed output and duration (seconds):
python3 tools/capture_udp.py --host 192.168.1.123 --duration 120 --output logs/integrated/core-udp-test.log
```

Replace the example IP with CORE's actual IP. Python uses only its standard
library. Default output is `logs/integrated/core-udp-YYYYMMDD-HHMMSS.log`.
Ctrl+C finishes partial lines and closes the file; no serial port is opened and
neither board is reset. Use `tail -f` on the printed output path for live viewing.
The serial capture script can run alongside it to capture EARS and KEYWORD.

The PC subscribes to CORE UDP port 12001 every 3 seconds from one ephemeral UDP
port. CORE grants one collector a 10-second renewable lease, restricted to its
local subnet; after stopping capture, allow 10 seconds before another collector
takes over. Allow outgoing UDP to CORE:12001 and replies in the PC firewall;
WiFi client isolation must be off. No packets for 12 seconds produces a console
warning. This unauthenticated diagnostic channel is intended for a trusted LAN;
do not forward its port to the Internet.

A priority-1 worker on core 0 owns a separate socket. Producers never wait for
queue space or perform network operations. Four queue items retain 768 payload
bytes (800 bytes of item storage plus FreeRTOS metadata); worker stack is 4096
bytes, with additional network/library allocations. Overflow evicts old chunks;
each producer call accepts at most 1024 bytes. Maximum transmission is 25 UDP
datagrams/s, each at most 216 bytes including the diagnostic header (5.4 kB/s,
excluding IP/WiFi overhead). General log chunks are not retried. Without a
subscriber, only a bounded recent backlog is retained. SD allocates no resources
when disabled. This reduces logger allocation versus SD but is not a hardware
latency or memory guarantee.

Every five seconds a `[UDP_LOG]` heartbeat reports heap, internal free memory,
worker stack headroom and cumulative dropped bytes. Packets carry `PLG1`, a boot
identifier, sequence, dropped-byte counter, CORE uptime and payload length in a
24-byte big-endian header. The receiver rejects malformed packets and unexpected
sources, discards duplicate/reordered packets, reports sequence gaps and boot
changes, and preserves UTF-8 across chunks. Log lines may be incomplete when
bytes are dropped; timestamps on lines are PC receipt time in UTC. The first
received packet establishes a baseline; earlier losses cannot be inferred.
Only application logs are captured, not ROM messages or panic dumps. WiFi must
connect; startup messages may already have been evicted before subscription.

Validation: `python3 test/udp_log/test_capture_udp.py` checks malformed packets,
loss/reorder/reboot handling, C++ encoder compatibility and real localhost UDP
subscription/capture. After flashing, verify I2S/Opus startup, several audio
replies, `[UART_ERR]`, `[UDP_LOG]` memory/headroom and drops; stop/restart the
collector and disconnect/reconnect WiFi. Hardware coexistence remains to be
verified. Disable with `PIUU_UDP_LOG=0`; SD and UDP cannot be enabled together.

## Historial de conexión Wi-Fi

En `platformio.ini`, `-DPIUU_WIFI_DIAGNOSTICS=1` activa un historial independiente
de `PIUU_NETWORK_DIAGNOSTICS` y de la cola general de logs. Cambiarlo a `0` y
recompilar elimina el buffer y el registro de eventos Wi-Fi. Para recibirlo por
EARS se usa `PIUU_EARS_LOG=1` (predeterminado en los entornos UART0). Para UDP
se necesita `PIUU_UDP_LOG=1` y desactivar el sink EARS.

El historial conserva los últimos 64 eventos en RAM (1024 bytes de registros,
más mutex y contador), sin guardar SSID ni contraseñas. Registra intentos,
estado inmediato de `WiFi.begin`, asociación, motivo numérico de desconexión,
obtención/pérdida de IP, plazo de espera y escaneos. Se pierde al reiniciar;
si se llena reemplaza los eventos más antiguos. El callback solo copia un
registro bajo un mutex breve: no escribe logs ni realiza operaciones de red.

Para el sink UDP opcional, después de que CORE conecte, ejecutar el colector:

```bash
python3 tools/capture_udp.py --host 192.168.1.123
```

La salida incluye líneas `[WIFI_HISTORY t=...]`, donde `t` es el tiempo del
evento en milisegundos desde el arranque, aunque se reciba mucho después.
Cada suscripción nueva reproduce el historial retenido y luego sigue los eventos
nuevos; renovar la suscripción cada 3 segundos no repite lo anterior. Reiniciar
el colector permite repetirlo después de que venza la sesión anterior (10 s).
Se mantiene el límite global de 25 datagramas/s y el heartbeat UDP. El historial
se envía directamente, antes que la cola general; los eventos permanecen en RAM
tras enviarse. Un error local de envío vuelve a intentar el registro. UDP no
confirma recepción: si un paquete se pierde en la red, hay que volver a
suscribirse para reproducir el historial. `skipped` informa eventos reemplazados
si el colector se atrasa; al comenzar se parte del más antiguo aún retenido.

Interpretación de campos:

- `associated channel=... authmode=...`: logró asociarse al router; todavía no
  confirma que tenga IP.
- `got_ip ip=...`: obtuvo IP. Si aparece después de `timeout`, la conexión
  terminó después del plazo que espera la aplicación.
- `disconnected reason=...`: motivo del driver ESP32. Códigos habituales:
  `8` desconexión voluntaria (también la provoca el cambio de red),
  `200` pérdida de beacons, `201` AP no encontrado, `202` fallo de autenticación,
  `204` timeout del handshake, `210` sin AP con seguridad compatible,
  `211` sin AP que cumpla el umbral de autenticación. El código orienta el
  diagnóstico; por sí solo no demuestra que la contraseña sea incorrecta.
- `ready` / `timeout`: `status` es el estado Arduino y `elapsed_ms` el tiempo
  desde que retornó `WiFi.begin`.
- `setup_failed step=...`: `1` modo STA, `2` desactivar ahorro de energía,
  `3` desconexión previa. No son códigos de motivo de desconexión.
- `scan_end networks=...`: cantidad de resultados; un valor negativo indica
  que el escaneo no terminó con resultados válidos.

El estado de conectividad depende del driver, no de que haya un trabajo WiFi
pendiente. Un escaneo conectado conserva la asociación, la conversación y la
identidad del enlace UDP. El worker pausa la reconexión automática de Arduino
durante cada operación explícita y restaura la política anterior antes de
publicar el resultado. Antes de escanear sin conexión cancela cualquier intento
pendiente; una cancelación fallida impide iniciar otro escaneo o conexión.
El intento espera ahora hasta 60 segundos (prueba diagnóstica) y, al vencer, cancela la conexión
pendiente para que no compita con el siguiente trabajo. No hay reintentos
propios del worker. El ahorro de energía se desactiva antes de conectar.
Un fallo genérico muestra `Connection not established`; usar el historial para
distinguir asociación, autenticación y DHCP. La validación de esos pasos y la
convivencia con audio requiere una prueba en la placa.

## Volumen de reproducción (PCM5102)

Abrir `SETUP → VOLUME`. Los botones `+` y `-` cambian el nivel de 0 a 100 en
pasos de 5; `MUTE` lo lleva a cero y `UNMUTE` recupera el nivel anterior de la
sesión. El ajuste se aplica a la reproducción que esté en curso. `BACK` guarda
el valor en NVS (`volume_pct`) y vuelve a SETUP; si falla, muestra `Save failed`
y permite reintentar. No se escribe flash en cada pulsación. El silencio también
queda guardado al salir y se restaura tras reiniciar.

Sin un valor guardado, se usa 20. Los porcentajes representan la posición del
control, con una curva de ganancia en dB; no son un porcentaje lineal de amplitud:

| Nivel | Ganancia aproximada | Atenuación |
| --- | --- | --- |
| 0 | 0 | Silencio digital |
| 20 (inicial) | 0,00398 | −48 dB |
| 50 | 0,03162 | −30 dB |
| 100 | 1 | 0 dB, sin amplificación digital |

El antiguo `volumeScale = 0.02f` equivalía a −34 dB. La multiplicación sí atenuaba
el PCM, y no se encontró otra ruta que la omitiera. El nuevo valor inicial reduce
la amplitud unas cinco veces más. No se puede atribuir todavía el volumen físico
alto a una causa concreta: las pruebas digitales no miden la salida del DAC ni
la amplificación posterior.

La conversión de PCM firmado se aplica justo antes de `i2s_write`, con ganancia
Q16 y una transición de 5 ms para reducir saltos al cambiar el nivel. No modifica
el buffer del decodificador, no cambia el formato I2S ni los pines y no amplifica
por encima de la señal original. El audio previamente encolado en DMA puede
seguirse oyendo brevemente después del ajuste. La tarea principal publica la
ganancia con un atómico y la tarea de reproducción mantiene la transición.

El formato I2S de 16 bits coincide con los formatos admitidos por la familia
[PCM510xA según Texas Instruments](https://www.ti.com/lit/ds/symlink/pcm5102a.pdf).
Esa familia entrega una señal de línea (hasta 2,1 V RMS a escala completa); la
etapa conectada después también influye en el volumen audible. La compatibilidad
del cableado y la salida analógica deben verificarse en el módulo físico.

Los cambios generan `[VOLUME] percent=... gain_q16=...` por el canal de logs
habitual. Para inspeccionar las muestras enviadas al DAC, agregar temporalmente
`-DPIUU_DAC_DIAGNOSTICS=1` a `build_flags` de `platformio.ini`. `[VOLUME_PCM]`
registra tres bloques al comenzar y después de cada cambio observado: pico de
entrada y de salida, ganancia, bytes aceptados y error I2S. Tras la transición,
con volumen 0 el pico de salida debe ser 0. `DAC_WRITE` sigue mostrando las
muestras anteriores al volumen; usar `VOLUME_PCM` para comprobar la atenuación.
Desactivar los diagnósticos DAC elimina esos cálculos y mensajes por bloque.

Las pruebas de host interceptan `i2s_write` para comprobar lo que recibe el
driver: signos PCM16, mínimo negativo, extremos de volumen, silencio exacto,
transiciones, entrada sin modificar y escrituras parciales/fallidas. También se
verifican botones dentro de 320×240 y persistencia, valores inválidos y fallos de
NVS. Falta escuchar y medir en el PCM5102 real.

### Control remoto por voz mediante Device MCP

El backend puede consultar, fijar, incrementar o silenciar este mismo volumen
mediante **MCP stdio → WebSocket local → UDP v3**. Activar
`PIUU_DEVICE_MCP_ENABLED=1` en PIUU_SERVER; ver
[configuración y ejemplos](../PIUU_SERVER/README.md#control-de-volumen-de-core-mediante-device-mcp-2026-10-29).
«Hablá más despacio» baja 20 puntos y «hablá más fuerte» sube 20. Una cantidad
explícita reemplaza ese paso; «poné al 30 %» fija 30. En esta integración
«despacio» controla volumen y no velocidad. Una frase con dos referencias a la
misma bajada debe producir un solo ajuste. La interpretación depende de Gemini;
las pruebas offline verifican sus instrucciones, sin probar reconocimiento real.

CORE anuncia `CAP_VOLUME_CONTROL=0x0040` y sólo acepta `VOLUME=30` si el servidor
negocia esa capacidad. Procesa los tres bytes (operación, valor firmado,
persistencia) en la tarea principal mediante la cola de controles; el worker RX
no modifica el volumen ni escribe NVS. `VOLUME_ACK=31` confirma estado, nivel
aplicado, nivel de restauración y nivel guardado. La identidad de sesión/turno,
longitud, flags e identificador se validan antes de ejecutar. Los reintentos del
último identificador repiten la respuesta sin aplicar otra vez el delta; comandos
anteriores se descartan y la deduplicación se reinicia con la sesión.

Pantalla y red comparten el último nivel no nulo para mute/unmute. Si VOLUME
está abierta, refleja el cambio remoto en el siguiente ciclo de UI y BACK guarda
el nivel vigente. Los botones siguen usando pasos de 5. Los pedidos remotos no
escriben flash salvo `set_volume(..., persist=true)`; si el guardado falla, el
ajuste sigue aplicado temporalmente y el ACK informa ese fallo.

Actualizar primero PIUU_SERVER y después CORE: el servidor actualizado admite
CORE antiguo sin volumen remoto; el servidor antiguo rechaza la nueva capacidad
HELLO. Las pruebas host incluyen duplicados, límites, mute compartido, fallos de
persistencia, cola RX/controlador y comandos/ACK Python↔C++. La validación audible
y por voz en la placa queda pendiente de despliegue.

Los logs de este grupo tienen un interruptor propio en `platformio.ini`:

```ini
-DPIUU_VOLUME_DIAGNOSTICS=1
```

Cambiarlo a `0` elimina los mensajes `[VOLUME]` y `[VOLUME_LINK]`, incluidos los
del arranque y de la pantalla, sin desactivar el control de volumen. No depende
de `PIUU_DIAGNOSTICS`, `PIUU_NETWORK_DIAGNOSTICS`, `PIUU_PLAYBACK_DIAGNOSTICS` ni
`PIUU_DAC_DIAGNOSTICS`; usa el destino habitual configurado (EARS, UDP, SD o
consola). El proyecto lo habilita por defecto; el fallback del header es 0.

`[VOLUME] source=remote` informa operación, valor, persistencia, estado y niveles
vivo/restaurable/guardado. Estado 0 confirma aplicación, 1 indica argumentos
inválidos y 2 fallo de guardado con nivel temporal aplicado. `[VOLUME_LINK]`
correlaciona sesión, turno e identificador con `VOLUME_DUPLICATE`,
`VOLUME_ACK_SENT`, `VOLUME_ACK_FAILED` o `VOLUME_REJECTED`. En rechazo, `detail`
vale 1 para capacidad no habilitada, 2 flags inválidos, 3 identidad/turno,
4 longitud/cuerpo, 5 secuencia antigua/cero y 6 identificador reutilizado con
otro comando. Los filtros previos del RX pueden descartar paquetes ajenos antes
de estos eventos. Un ACK enviado no garantiza que haya llegado al servidor.
El análisis por bloque `[VOLUME_PCM]` conserva su interruptor separado
`PIUU_DAC_DIAGNOSTICS`.

## Conversation inactivity

CORE closes a listening conversation after `CONVERSATION_IDLE_TIMEOUT_MS=10000`
without provider-detected speech. The provider inactivity clock restarts on server
conversation events and after reply playback/echo guard; local microphone energy
cannot reset it. This prevents ambient noise from keeping the session open.
Response waiting and playback are excluded from this timeout.

While provider speech is active, qualified local activity keeps the session alive;
10 seconds without it closes a stale speech event. A separate configurable
`CONVERSATION_SPEECH_WATCHDOG_MS=120000` bounds a speech event without an end even
if background energy persists. This also limits a single uninterrupted utterance
to two minutes; increase it if longer monologues are required. If the provider
misses a real speech-start event, CORE may close after 10 seconds despite local
voice. Backend VAD event delivery is therefore required for this policy.

`CONVERSATION_ACTIVITY_LEVEL=600` measures mean absolute PCM16 amplitude, not RMS.
It is an initial fixed setting above the 225–499 ambient means observed in the
2026-10-28 capture, not an automatic calibration or speech classifier. Activity
must stay at/above the threshold for `CONVERSATION_ACTIVITY_HOLD_MS=200`; a lower
frame or an observation gap above 100 ms breaks qualification. Short spikes do
not reset the local activity counter. This local counter is used only to recover
from a stale provider speech event; quiet voices below this threshold can be missed. Tune using
real microphone trials; KEYWORD's RMS threshold is not interchangeable.

`PIUU_CONVERSATION_DIAGNOSTICS=1` independently enables one `[CONV_STATS]` line
every five seconds while a conversation is active, even with general diagnostics
disabled. It includes state, local level, elapsed quiet time, threshold, timeout,
provider speech flag, qualification duration, microphone mute flag and
`provider_quiet_ms`. Closure reports `source=provider_idle` or `speech_watchdog`. A
`[CONV] silence_close` line identifies the actual timeout. These changes add no
queue, task, dynamic allocation or audio processing buffer.

## Microphone guard and SD logs (UART0 builds)

CORE now runs half-duplex: from `ResponseStarted` until playback completes, it
continues draining EARS UART but discards microphone PCM instead of sending it to
the backend. It also ignores WAKE events during that interval, so the speaker
cannot trigger WALLE or interrupt its own response. Touch cancellation remains
available; keyword STOP is accepted outside playback and its echo guard. After playback completes, `CONVERSATION_ECHO_GUARD_MS` adds
250 ms before microphone forwarding resumes. This intentionally disables spoken
barge-in while PIUU talks; it is not acoustic echo cancellation. Muted frames do
not advance UDP PCM sequence/PTS and are never buffered for later transmission.
EARS continues reading its microphone. KEYWORD routing is controlled separately,
as described below.

### KEYWORD durante la conversación

**Cargar CORE y EARS actualizados juntos.** No cambia el modelo ni el firmware de
C3_KEYWORD. EARS deja de elegir un único destino: envía el mismo frame a ambos
UART cuando KEYWORD está habilitado durante una sesión.

| Estado | PCM a CORE | PCM a KEYWORD |
| --- | --- | --- |
| Sin sesión | No | Sí |
| Conectando / escuchando al usuario | Sí | Sí |
| Esperando respuesta de la IA | Sí | Sí |
| Reproduciendo voz de la IA | Sí | No |
| Guarda de eco de 250 ms tras reproducir | Sí | No |
| Escucha reanudada | Sí | Sí |

La pausa de KEYWORD empieza al procesar el evento de inicio de reproducción del
DAC, no al recibir `ResponseStarted`. El descarte de PCM hacia el backend conserva
su política anterior: se activa antes, desde `ResponseStarted`. Ambos controles
son independientes. El cierre/aborto de una sesión vuelve a la ruta KEYWORD.

CORE publica la intención por UART y la reafirma periódicamente, como el estado
de streaming. Los comandos v1 sin payload son `0x01` CORE solo (KEYWORD pausado),
`0x02` KEYWORD solo (CORE apagado), y el nuevo `0x03` CORE + KEYWORD. El ACK
`CORE_STREAM_STATE=0x80` conserva su byte 0/1 y confirma solo streaming a CORE.
Un EARS anterior no conoce `0x03`; no usar CORE actualizado con ese firmware.
EARS actualizado sí acepta los comandos anteriores.

`[KEYWORD_GATE] enabled=0/1 state=...` registra los cambios de intención en CORE;
`[EARS] CMD=... core=... keywords=...` muestra el enrutamiento aplicado por EARS.
El cambio tiene la latencia del loop, UART y frame de audio; no es una señal
eléctrica sincronizada con la primera muestra del DAC. KEYWORD puede terminar
inferencia ya encolada al pausarse el PCM. CORE descarta eventos de detección
recibidos durante reproducción/guarda; un WAKE repetido tampoco puede volver a
habilitar la ruta pausada. Al reanudarse, el salto de secuencia permite al receptor
KEYWORD detectar la discontinuidad con su mecanismo existente.

Esto prepara el camino para una palabra de detención mientras la IA escucha o
espera respuesta: el evento UART `STOP_WORD=0x03` ya cierra la sesión en CORE.
No se agregó una palabra al modelo. Durante la voz de la IA se mantiene pausado
el detector, por lo que no se ofrece interrupción por voz en ese intervalo.

Pruebas: `bash test/run_host_tests.sh` en CORE y
`bash test/run_audio_routing_tests.sh` en C3_EARS verifican gates, guarda de eco,
STOP, eventos repetidos, comandos reales entre placas y envío de las mismas
secuencias por ambas rutas. Falta medir en placa el tiempo de ambos envíos UART y
la continuidad de inferencia/audio usando los diagnósticos existentes.

SD logging is optional and disabled in both UART0 environments. To test it,
set `PIUU_SD_LOG=1` and `PIUU_UDP_LOG=0`. Insert a FAT-formatted
microSD before powering up. CORE app logs go to `/piuu-core.log`; at approximately
1 MiB it rotates to `/piuu-core.previous.log`, retaining two files. Reboots append
a boot marker to the current file. Read the files on a PC after powering CORE off.

The SD uses a dedicated VSPI instance at 4 MHz with configurable build flags:
`PIUU_SD_SCK=18`, `PIUU_SD_MISO=19`, `PIUU_SD_MOSI=23`, `PIUU_SD_CS=5`.
These pins must match the board's microSD socket. TFT remains on HSPI and touch
uses its existing separate bit-banged pins. Do not connect other devices to the
SD pins. Standard builds keep their serial logging route.

SD storage starts only after the UDP cache, I2S, Opus decoder and audio tasks
have initialized successfully. Before that, only the bounded log queue exists;
early messages can overflow and are counted as dropped bytes. No SD file is
written if WiFi/audio startup never succeeds. The worker attempts mounting once
and holds an 8192-byte internal RAM reserve through mount/open, releasing it
afterward. If the reserve, task or mount cannot be allocated, SD logging stays
disabled until reboot; audio services remain active. This headroom does not
guarantee sufficient RAM for every later workload. A new boot marker includes
`audio_ready=1`; if mounting fails, the card may contain only an older session.

A low-priority worker owns all SD operations. Producers enqueue without waiting;
8 chunks of 192 bytes hold up to 1536 payload bytes. The worker stack is 4096
bytes, in addition to queue metadata and SD/filesystem allocations. The mount
allows only one open file (instead of the SD library default of five); rotation
closes the old file before opening the next one. Overflow
drops log bytes and later records `dropped_bytes`; it never waits for card writes
in the audio or UART path. Files flush every second. Abrupt power loss can lose
recent logs or damage the filesystem. A missing card or mount/write failure
silences SD logging until restart, without falling back to the binary UART0.
Set `PIUU_SD_LOG=0` in the build environment to disable it.

Only `appLog()` messages are captured, including `[MIC_GATE]`, `[CONV_EVENT]`,
`[EARS]`, resource and network diagnostics. ROM boot output, panic dumps and
ESP-IDF logs are not redirected to SD. Hardware validation of the card, sustained
audio playback and log recovery is still required after flashing.

The initial SD capture showed `driver_install failed: 257` (`ESP_ERR_NO_MEM`).
The reduced queue saves 1552 bytes of queue item storage; limiting the filesystem
to one open file also reduces mount allocations, whose actual saving depends on
the framework. The 4096-byte worker stack and audio DMA configuration are retained.
SD avoids network traffic, but still consumes RAM and CPU; it is not inherently
cheaper than UDP in those resources.

After flashing, check `[MEM] stage=sd_before_mount`, `sd_after_open`,
`i2s_before_install` and `i2s_ready` for internal/DMA free bytes and largest blocks.
`i2s_install_failed` includes a post-failure snapshot and the I2S error has its
symbolic name. Every ten seconds `[SD_LOG]` reports worker stack headroom in bytes,
maximum chunk-write/flush times in microseconds, and queued chunks. Long SD writes
run in the logger task; queue overflow may truncate log lines. A missing SD cannot
report its own mount failure through this sink.

Validation on hardware: cold boot with the SD inserted, confirm `[SERVICES]
UDP/audio ready` and `i2s_ready`, then test several spoken replies while EARS
continues streaming. Inspect UART/audio counters, `dropped_bytes`, stack headroom
and memory across replies. Check rotation past 1 MiB and a boot without the card.
Build/host checks do not establish that SD/audio coexist reliably on the board.

Once mounted, SD continues capturing through WiFi/backend disconnects.
Forwarding logs via EARS would require a new framed message and rate limits on
both firmware sides; raw text must never be added to the existing binary link.


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
conversación; el proveedor detecta los turnos. El envío del micrófono se suspende
durante respuestas y se reanuda al terminar la reproducción y la guarda de eco. Touch cierra; wake durante respuesta se ignora para evitar activaciones por eco. CORE cierra
tras 10 segundos de silencio esperando una nueva intervención; el backend
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
rotación 3, invertida 180° respecto del prototipo anterior). La emoción inicial
es `neutral`, con blink y glance automáticos. Ojos y UI comparten orientación.
Mantener touch **más de 5000 ms continuos** abre la UI con ajustes WiFi/servidor
y botón MIC. Las listas, menús y teclado están adaptados a 320×240. El botón
**EYES**, junto a ajustes en la pantalla principal, vuelve a los ojos sin reiniciar.
Desde ajustes, usar BACK para regresar a la pantalla principal.
Un toque corto en los ojos no acciona MIC; soltar el toque largo no pulsa
controles de la UI. La calibración original se conserva y sus coordenadas
verticales se transforman a horizontal: `x = 319 - portrait_y`, `y = portrait_x`.
La orientación común está en `src/hardware/display_geometry.h`.
Para validar en hardware: mantener touch más de cinco segundos, comprobar MIC,
SETUP y las teclas de ambos extremos del teclado, y regresar con BACK → EYES.
Repetir durante reproducción para comprobar que la navegación no corta el audio.

Los comandos y temporizadores visuales siguen funcionando mientras la
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

### Comandos UDP inmediatos (kind 25)

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

### Planes de emociones sincronizados con audio (kind 26/27)

La integración del backend `Server Wall-e-PCM-OPUS` y CORE ya implementa la
extensión de planes. CORE anuncia `CAP_EYES_PLAN=0x0004` en HELLO; el backend
la acepta devolviendo ese bit en HELLO_ACK cuando `expression_udp_enabled=True`.
Si la respuesta de handshake no tiene el bit, CORE mantiene audio pero no admite
planes. Un backend antiguo que rechaza HELLO flags=4 necesita actualizarse:
este firmware no hace downgrade automático. `expression_tools_enabled=True`
habilita la herramienta de IA que genera planes; una respuesta breve puede no
usarla, sin que eso sea un error de transporte.

Se conserva el sobre PIU3 v3 de 38 bytes:

| Kind | Dirección | Contenido |
| --- | --- | --- |
| 26 `EyesPlan` | Backend → CORE | Plan binario completo, 12/20/28 bytes |
| 27 `EyesPlanAck` | CORE → backend | Cuerpo vacío; flags=0, identidad y sequence iguales |

Cada respuesta admite **un plan inmutable, sequence=1**. La secuencia es
independiente de los comandos inmediatos kind 25. Sesión, turno y respuesta deben
coincidir con la respuesta actual no cero. El backend espera el ACK de
`ResponseStarted` y envía el plan antes del primer Opus, sin esperar el ACK visual
para continuar audio. Un ACK confirma recepción, no reproducción física.

Cuerpo big endian, sin JSON:

```
version:u8=1 | count:u8=1..3 | reserved:u16=0
por paso: emotion:u8 | gesture:u8 | duration_ms:u16 | offset_ms:u32
```

Duraciones 1200..4000 ms; primer offset=0 y siguientes iguales a la suma de las
duraciones anteriores. Máximo 12000 ms y **66 bytes por datagrama** con cabecera.
Emociones ID 0..6: neutral, happy, curious, calm, concerned, sad, excited.
Gestos ID 0..8: none, emphasis, question, acknowledge, glance, blink, doubleblink,
soft_emphasis, sway. **soft_emphasis=7**; no usar el orden del schema de la IA como
numeración. Todo el plan se valida antes de admitirlo.

Ejemplo: happy/soft_emphasis por 2400 ms, curious/question por 1800 ms:

```
01 02 00 00 01 07 09 60 00 00 00 00 02 02 07 08 00 00 09 60
```

- El plan espera almacenado hasta que se escriben muestras al DAC. Emoción y gesto
  se aplican juntos; las transiciones están incluidas en el intervalo del paso.
- El reloj visual cuenta muestras aceptadas por I2S, incluidas PLC/silencio de
  recuperación. No usa la secuencia de paquetes, que puede saltar. Interpola a
  16 kHz hasta agotar las muestras encoladas y luego se congela durante rebuffer.
  Las animaciones del plan también se congelan con ese reloj.
- Es una **estimación de reproducción**, no un contador DMA ni una medición
  acústica. La escritura I2S y sus buffers pueden introducir desfase. No se cambió
  el estimador usado para truncar historial de voz. Validar sincronía real en CYD.
- Un duplicado exacto se confirma otra vez sin reiniciar pose, gesto ni reloj.
  Contenido conflictivo con la misma identidad/sequence se rechaza. Se conserva
  la marca de deduplicación hasta cambiar la identidad, incluso tras cancelar o
  terminar el plan.
- Un plan tardío selecciona el paso vigente y omite gestos históricos. Saltos del
  reloj que pierden pasos no producen ráfagas de gestos. Los cruces normales de
  frontera toleran hasta 100 ms de retraso del loop; después se omite el gesto.
- El plan reserva prioridad UDP desde su admisión hasta el último paso o la
  cancelación. EARS puede seguir cambiando configuración; sus emociones/gestos
  se descartan mientras dura la reserva. Los gestos explícitos del plan reemplazan
  los automáticos. Un comando visual UDP inmediato cancela el plan; sus reintentos
  posteriores no lo restauran. Los ajustes UDP no cancelan el plan.
- Interrupción local/VAD, nueva respuesta, desconexión o fin de reproducción
  invalidan el plan y su prioridad. Una respuesta corta corta los pasos restantes.
  Al terminar la secuencia se vuelve a neutral; blink/glance y habla automática
  siguen las reglas existentes. No se envía ni espera un comando remoto neutral.

El backend reintenta como máximo dos veces más, a intervalos de al menos 100 ms,
y descarta el pendiente a los 500 ms. Un fallo de ACK/cola visual nunca aborta audio.
Plan y comandos comparten la cola visual UDP acotada; no usan la cola crítica de
conversación. El receptor, scheduler y ACK se atienden en loop; RX sólo copia.

`[EYES_PLAN]` muestra `negotiated`, `active`, `step` (0..2), disponibilidad del reloj,
`played_est_ms`, planes aceptados, duplicados, rechazados y ACK enviados/fallidos.
Correlacionar con `EYES_PLAN_SENT/ACK` del backend y con `[EARS]`, `[AUDIO]` y
`[RESOURCE]`. `active=0` es normal cuando la IA no genera plan o éste terminó.

Prueba integrada sin hardware (también conserva las regresiones de audio):

```bash
bash test/run_host_tests.sh '/home/denis/Documents/Wall-e Project/Server Wall-e-PCM-OPUS'
```

La prueba cruza el emisor Python real, el decoder/scheduler C++ y el ACK de
SessionClient, y devuelve ese ACK al backend. Para validar en placa, cargar el
firmware correspondiente y ejecutar el backend actualizado; comprobar handshake,
respuesta con plan, interrupción a mitad de gesto, respuesta más corta que el plan,
pausas de audio y configuración UART concurrente. Verificar ausencia de pérdidas
adicionales de audio y medir heap mínimo. No se cargó firmware desde estas pruebas.

### Renderizado, extensión y recursos

`src/eyes/engine` contiene una copia del motor de PIUU_EYES, sin su `main.cpp`:
`eyes` define poses y transiciones, `gestures` compone variaciones, `contour`
calcula intersecciones y `renderer` envía regiones sucias. Las diferencias de
integración son `Renderer::invalidate()` para volver desde la UI y
`Gestures::cancel()` para reemplazar un gesto remoto. Mantener estas extensiones
al actualizar la copia; CORE compila sin depender de la carpeta PIUU_EYES.

`control` valida comandos y administra emoción, temporizador y prioridad.
`admission` valida identidad/secuencia de comandos; `plan` valida y reproduce
secuencias acotadas; `runtime` copia mensajes desde los
workers y aplica/renderiza sólo desde loop. La pantalla conserva un único dueño.
No se crea una tarea visual, no se usa LVGL/Bluetooth ni se asigna memoria por
comando o frame. El buffer de píxeles es **320×4×2 = 2560 bytes**. Las dos colas
contienen ocho mensajes de 72 bytes en esta compilación ESP32, más sus estructuras
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


Compilación con planes: 117472 bytes de RAM estática, **264 bytes adicionales**
respecto de la integración de comandos inmediatos (117208). El buffer de píxeles
sigue siendo de 2560 bytes. El estado del plan tiene un límite de compilación de
256 bytes; el cuerpo decodificado ocupa 28. Estos números no sustituyen el heap
libre medido con WiFi, Opus y reproducción en funcionamiento.


## UART0 y carga a través de EARS

Para el nuevo cableado usar `cyd-uart0` (ILI9341) o `cyd2usb-uart0` (ST7789).
EARS GPIO6 TX → CORE GPIO3 RX0; EARS GPIO7 RX ← CORE GPIO1 TX0, a 921600
para audio/eventos. Las variantes originales `cyd`/`cyd2usb` conservan UART2.
Las variantes UART0 envían `appLog()` a microSD mediante una cola sin espera;
no inicializan una consola sobre ese puerto. El mensaje de la ROM/bootloader
al resetear es independiente. Ver la sección de logs SD al inicio de este README.

La carga usa el modo mantenimiento integrado en `esp32-c3-ears` y el script
`../C3_EARS/tools/core_bridge.py`, con EARS GPIO10 → EN y GPIO2 → BOOT/GPIO0
de CORE. Seguir el procedimiento de `../C3_EARS/README`: primero instalar el
firmware actualizado en EARS una vez, luego ejecutar `info` y `flash`. El script
restaura EARS normal automáticamente tras éxito; usar `run` tras un fallo.
El puente programa a 115200 y requiere USB nativo de EARS. El botón Upload
normal de estos entornos no invoca el puente: usar el script documentado.
