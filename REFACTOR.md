# PIUU_CORE — estructura y contratos actuales

## Estado

La captura I2S está eliminada; I2S solo reproduce hacia el DAC. CORE ahora recibe
PCM!/EVT! de EARS y envía CMD! START/STOP por UART2 a 921600 baudios. El inicio
puede venir de touch o WAKE; el cierre de entrada, de touch o STOP_WORD.

EARS continúa transmitiendo durante la respuesta; CORE drena ese audio sin
agregarlo al turno cerrado. La cancelación efectiva durante reproducción todavía
requiere identidad de respuesta y cambios del servidor. Bluetooth y animaciones
siguen pendientes. EARS conserva su fuente FILE_REPLAY actual; no se implementó
captura de micrófono en esta etapa. No se cargó firmware ni se validó el enlace
físico en placa.

Las responsabilidades y configuración de la etapa anterior se mantienen.
Contrato, cableado, límites y pruebas UART: [docs/INTEGRACION_EARS_UART.md](docs/INTEGRACION_EARS_UART.md).

## Organización y responsabilidades

```text
src/
├── main.cpp                 # setup/loop y coordinación periódica
├── app/
│   ├── app_services.*       # Conectividad y arranque UDP/audio, sin UI
│   ├── ears_session.*       # Ruteo de entrada UART hacia conversación/uplink
│   ├── conversation_input.h # Puerto de inicio/stop de la fuente de audio
│   ├── conversation.*       # Coordinador de turnos y cola de eventos playback
│   └── conversation_state.h # Transiciones puras y generación local del turno
├── protocol/
│   ├── server_protocol.*    # PCM/Opus UDP v1 y PING/PONG/COMMIT, sin Arduino
│   └── ears_protocol.*      # Parser incremental UART PCM!/EVT! y encoder CMD!
├── network/
│   ├── server_link.*        # Adaptador del servidor y heartbeat
│   ├── network_udp.*        # Transporte opaco protegido por mutex privado
│   ├── wifi_service.*       # Un worker, solicitudes/resultados acotados
│   └── wifi_types.h         # Resultados de scan por valor
├── audio/
│   ├── audio_uplink.*       # Secuencia por turno, END y COMMIT, abort local
│   ├── audio_downlink.*     # RX, jitter, Opus, playback y eventos etiquetados
│   ├── playback_event.h     # Contrato de eventos sin dependencias UI/Arduino
│   └── audio_output.*       # PCM al DAC; frontera para Bluetooth
├── config/
│   ├── ears_config.h       # UART2 RX16/TX17, límites y tiempos del enlace
│   ├── settings.*          # Configuración activa y API de cambios persistidos
│   ├── audio_format.h      # PCM16 mono 16 kHz, 320 muestras/20 ms
│   └── app_config.h        # Defaults, tiempos y ajuste de buffering
├── hardware/
│   ├── app_log.h           # Logs USB UART0; UART1 opcional
│   ├── display_hal.*       # TFT y aserción de ownership
│   ├── touch_hal.*         # XPT2046 y calibración de coordenadas
│   └── pins.h
├── input/
│   ├── touch_input.*       # Captura de bordes Down/Up; no conoce pantallas
│   └── ears_link.*         # Worker UART, colas acotadas y liveness/estado remoto
├── storage/storage_prefs.* # Único acceso a Preferences/NVS
└── ui/
    ├── ui_controller.*     # Acciones, resultados WiFi y cambios de configuración
    ├── ui_model.*          # Pantalla, resultados visuales, selecciones y borradores
    ├── touch_gesture.h     # Press/Release/Cancel y descarte de gestos atrasados
    ├── ui_status.*         # Presentación del estado de servicios/conversación
    ├── app_navigation.*    # Transiciones con revisión de pantalla
    ├── screens/
    └── widgets/
```

Includes desde src; los encabezados permanecen junto a sus implementaciones.
PlatformIO compila src recursivamente.

## Tareas y ownership

- `setup()/loop()` poseen lifecycle, heartbeat, conversación, uplink, configuración,
  modelo UI, teclado y TODOS los accesos al TFT. `display()` y las agrupaciones
  `screenLock/screenUnlock` comprueban el task propietario con `configASSERT`;
  estos últimos ya no toman un mutex. No existe `screenMutex`.
- Touch solo consulta XPT2046 y publica Down/Up en una cola de 16 eventos con
  coordenadas y timestamp. Si se llena, espera únicamente el worker touch para
  no perder silenciosamente un release. No lee pantalla/configuración ni dibuja.
- UI procesa un borde por loop: Press dibuja feedback; Release confirma; Cancel
  restaura sin acción si el dedo terminó a más de 30 px. Una revisión invalida
  gestos sostenidos durante navegación/redibujado; timestamps descartan presses
  anteriores a la vista actual. No quedan `waitTouchRelease` ni sleeps en handlers.
- WiFi tiene un worker permanente con stack solicitado de 6144 bytes. Conexión
  inicial, conexión manual y scan comparten una cola de un trabajo y otra de un
  resultado. Se rechazan nuevos trabajos hasta consumir el resultado anterior.
  Las colas copian datos POD: SSID de hasta 32 bytes, password de hasta 64 y scan
  de hasta 10 redes. Ningún `String` o puntero al modelo se comparte con el worker.
- Los bloqueos de scan/conexión y el timeout de conexión de 15 s ocurren dentro
  del worker. El scan conserva el comportamiento anterior de desconectar y
  restaurar la última conexión exitosa, incluso si el scan falla. No se inicia
  audio ni se navega desde ese worker. Salir de la pantalla no cancela el trabajo.
- Resultados siempre se consumen en loop. Una conexión manual solo fuerza volver
  a inicio si continúa abierta la misma vista de password; no arrastra al usuario
  desde otra pantalla. Durante el trabajo WiFi no se permiten nuevos turnos.
- RX UDP pertenece a su worker; jitter está protegido por mutex. Decoder y salida
  pertenecen a playback. Reset de turno se solicita por atomic y se aplica dentro
  de playback; el controlador ya no resetea el decoder concurrentemente.
- Playback envía Started/Finished por una cola de ocho eventos; si está llena,
  reintenta sin bloquear. Estadísticas compartidas usan atomics; estado interno
  de jitter usa su mutex. No se agregó otro buffer persistente de audio.
- Servicios se inician de forma idempotente; fallos se reintentan al refrescar
  conectividad. El arranque parcial de tareas se revierte y un gate evita uso
  prematuro del socket/decoder. Pérdida WiFi conserva los servicios inicializados,
  aborta el turno local y reinicia heartbeat al detectar la transición.
- UART usa un worker con stack solicitado de 4096 bytes en Core 0, prioridad 2,
  y colas independientes para seis frames PCM y ocho eventos. Se conserva el
  orden con un ordinal común y mutex breve. No llama uplink, settings ni UI.
  `ears_session` consume desde loop y `conversation_input` controla START/STOP.
- UART2 RX16/TX17 está reservado para EARS; el LED RGB fue retirado físicamente.
  UART0 conserva logs USB a 115200. `PIUU_DEBUG_TX_PIN` permite alternativamente
  logs por UART1 en otro GPIO libre.
  El worker de estadísticas de playback solo existe con diagnósticos habilitados.

## Conversación

```text
Idle -> Listening -> ClosingInput -> WaitingResponse -> Playing -> Idle
                                         └── Finished sin audio ──> Idle
```

El cierre envía END repetidos y luego COMMIT sin sleeps. ClosingInput se conserva
hasta completar esa secuencia. Started/Finished solo se aceptan para la generación
local vigente y desde WaitingResponse/Playing. Abort invalida la generación,
cancela END/COMMIT pendientes, solicita reset de playback y vuelve a Idle.
Los eventos encolados de un turno anterior no pueden reactivar su estado.

El downlink ignora audio mientras no se espera respuesta o está pendiente aplicar
el reset. La generación es LOCAL: no agrega identidad al protocolo UDP. No puede
identificar paquetes de respuestas antiguas que lleguen durante una nueva espera.
Abort tampoco cancela la generación en Python/xAI ni vacía el audio ya entregado
al DMA. La interrupción real durante respuesta queda para CORE/servidor con IDs
y cancelación explícitos. No se añadió timeout nuevo para la primera respuesta.

La UI deriva LISTENING/THINKING/SPEAKING de esta máquina y no escribe flags de
audio. Solo repinta al cambiar/invalidarse; conectividad visual se refresca a 1 s.
Errores explícitos permanecen cinco segundos en inicio; el editor/lista de
servidores también muestra errores de guardado sin perder el borrador.

## Configuración y preferencias

`settings()` expone una vista const de `DeviceSettings`. Solo su API modifica el
modelo activo, después de comprobar las escrituras. UI conserva `wifiDraft` y
`serverDraft` separados: escribir o salir con Back no cambia la configuración.
NVS solo se abre desde storage y cada operación cierra su handle.

Se mantienen el namespace `handwalle` y las claves legacy:

| Datos | Claves |
| --- | --- |
| WiFi activo | `wifi_ssid`, `wifi_pass` |
| Servidor activo | `server_ip` |
| Catálogo de servidores | `server_count`, `server_0` … `server_7` |
| Credenciales guardadas por SSID | `wifi_count`, `ssid_N`, `pass_N` |

- No se borra NVS ni se exige reconfigurar. Defaults vienen de app_config.
- Catálogo de hasta ocho servidores; carga elimina duplicados/vacíos y conserva
  el default cuando hay espacio. Agregar no activa automáticamente una dirección.
- Editar reemplaza la entrada, actualizando también la activa si corresponde;
  no crea otra copia. Seleccionar confirma la dirección activa. Borrar la activa
  escoge la primera restante; si no queda ninguna, repone el default.
- Solo una conexión manual exitosa persiste credenciales/configuración WiFi.
  Password vacío funciona para redes abiertas. SSID existente se actualiza;
  otros SSID legacy se conservan, incluidos catálogos de más de ocho redes.
  Se consultan bajo demanda para evitar una colección adicional de Strings en RAM.
- Las APIs legacy de olvidar un SSID y borrar todas las credenciales también
  están migradas. Olvidar uno no cambia el WiFi activo; borrar todas limpia las
  credenciales activas/guardadas y conserva servidores. No agregan controles UI
  nuevos ni desconectan hardware por su cuenta.
- Cambiar servidor activo aborta el turno local y reinicia heartbeat.
- Los fallos de escritura se informan y no publican el modelo propuesto en RAM.
  Preferences realiza escrituras por clave: esta etapa conserva el esquema legacy,
  sin agregar una transacción de múltiples claves ante corte de energía/fallo
  intermedio. Una escritura parcialmente persistida puede aparecer al reiniciar.

## Contrato del servidor

El transporte actual usa `protocol/session_protocol` (sobre UDP v2),
`network/session_client` (sesión, controles fiables y caché PCM), y `server_link`
(adaptador UDP/colas entre controlador y worker RX). `server_protocol` conserva
exclusivamente el cuerpo PCM/Opus de 18 bytes; no habilita datagramas v1 sin sobre.

El lifecycle recibe un `ConversationTransportPort` inyectado, sin depender de
UDP ni del display. Start/cancel/finish y errores de transporte avanzan la máquina;
la UI y EARS siguen pasando intenciones al controlador. Los IDs se publican al
worker mediante atomics de 32 bits; los controles vuelven por una cola acotada.

La cancelación invalida audio anterior, renueva la época local y limpia DMA desde
el worker propietario. El backend cancela y recorta historial antes de confirmar.
Los huecos PCM se recuperan por NACK o se rellenan con silencio limitado a 200 ms.

Ver [contrato y límites UDP v2](docs/PROTOCOLO_UDP_V2.md). Se reemplazan los controles
UDP desnudos legacy. El TX UDP ya no espera 50 ms por mutex: falla inmediatamente
si está ocupado; los controles se reintentan y audio puede recuperarse por NACK.

## Verificación

Desde Projects:

```sh
bash PIUU_CORE/test/run_host_tests.sh
bash PIUU_CORE/test/run_host_tests.sh '/home/denis/Documents/Wall-e Project/Server Wall-e-PCM-OPUS/src/main.py'
pio run -d PIUU_CORE -e cyd -e cyd2usb
```

Pruebas host ejercitan código de producción con fakes de Arduino/FreeRTOS/WiFi/NVS:
protocolo y bytes reales del bridge Python; END/COMMIT y abort; retry/idempotencia
de servicios; heartbeat/reconexión/rollover; conversación/eventos viejos;
presentación sin repaint redundante; gestos; claves NVS legacy, 12 SSID guardados,
olvidar/limpiar credenciales sin tocar servidores,
password vacío, fallos y catálogo de servidores. WiFi se prueba con un worker real
host bloqueado deliberadamente para comprobar que submission/consumo no esperan,
serialización, snapshots y restauración tras scan fallido/exitoso.

Referencia de la etapa anterior al UART (valores actuales en el documento UART):
compilación exitosa en ambos entornos: RAM estática 108856 bytes
(33,2 %); flash cyd 1084508 / cyd2usb 1084416 bytes. Frente a la etapa
anterior, RAM estática aumenta 288 bytes. Esto no expresa el
costo dinámico del worker/colas ni una mejora medida de rendimiento.

No son pruebas de drivers, radio, timing o rendimiento en ESP32. No se cargó
firmware. La plataforma local resuelta es Espressif32 55.3.312 / Arduino 3.3.12;
`platform = espressif32` continúa sin versión fija. El nombre `cyd2usb` identifica
la variante de pantalla existente, no una salida de audio USB.

Los valores de RAM estática del compilador no incluyen stacks/colas/decoder/DMA
asignados dinámicamente. El worker WiFi ahora permanece vivo: deben medirse heap
mínimo y high-water de stacks en placa antes de ajustar su tamaño.

## Validación UDP v2

Suite host completa y contrato C++/Python pasan. Compilan `cyd` y `cyd2usb`:
RAM estática 117880 bytes (36,0 %), flash 1059960 / 1059872 bytes respectivamente.
La caché PCM está incluida en RAM estática; stacks, colas, decoder y DMA dinámicos
no lo están. Sin carga de firmware ni pruebas de radio o xAI real.

## Próximas etapas

1. Validar UART en placa con EARS FILE_REPLAY, cableado y aislamiento USB-UART.
2. Integrar la fuente de micrófono en EARS cuando corresponda.
3. Validar IDs, cancelación, recorte de historial y recuperación UDP v2 en placa/xAI.
4. Optimización de jitter y reparto CPU con mediciones en hardware.
5. Bluetooth detrás de audio_output; después animaciones con ownership del display.
