# Logs para el test integrado

## Conversación continua v3 (actual)

Ver PROTOCOLO_UDP_V3.md. El flujo esperado es START_ACK → PROVIDER_READY / READY
→ SPEECH_STARTED → SPEECH_STOPPED → RESPONSE_STARTED → OUTPUT_END → PLAYED_ACK.
La conversación sigue abierta después de la respuesta. PCM continúa durante Playing.
`CONV_STATS` muestra nivel PCM y quiet_ms. Tras 30 segundos de silencio esperando
entrada: `silence_close` → CANCEL_ACK → `CONVERSATION_CLOSED provider_closed=True`.
No debe aparecer una nueva conexión xAI hasta el siguiente START.
`PROVIDER_READY startup_frames/startup_dropped` permite evaluar la apertura.
`XAI_USAGE` registra el contador de facturación del proveedor cuando está presente.
Los ejemplos posteriores de COMMIT o V2 corresponden al flujo anterior.


Instrumentación habilitada en CORE (`PIUU_DIAGNOSTICS=1`) y backend (nivel INFO).
No cambia el protocolo de audio ni requiere cambios en EARS. No se imprimen
frames PCM, claves, credenciales ni transcripciones desde la instrumentación nueva.
Las grabaciones WAV configuradas en el backend siguen siendo independientes.

## Capturar

En tres terminales, desde cada proyecto correspondiente. Los dispositivos deben
existir; adaptar el puerto si no están disponibles los alias configurados.

```sh
# CORE, dentro de PIUU_CORE
pio device monitor -p /dev/esp32-core -b 115200 | tee /tmp/core-test.log

# EARS, dentro de C3_EARS
pio device monitor -p /dev/esp32-ears -b 115200 | tee /tmp/ears-test.log

# Backend, con XAI_API_KEY ya presente en el entorno
.venv/bin/python -u src/main.py 2>&1 | tee /tmp/backend-test.log
```

CORE usa `t` en milisegundos desde boot; el backend fecha UTC con milisegundos.
**Los relojes no están sincronizados:** correlacionar con `c`, `s`, `turn`, `resp`
y `request`, no restar directamente los timestamps de equipos distintos.
`epoch` identifica el ciclo local de reproducción y aparece en `[CONV]`/`[AUDIO]`.

## CORE

| Prefijo / evento | Qué permite verificar |
| --- | --- |
| BOOT | UDP v2, pines/baud EARS, diagnóstico habilitado |
| CONV | Idle → Listening → ClosingInput → WaitingResponse → Playing → Idle; interrupción/abort y epoch |
| EARS_EVT | Wake/STOP y orden UART; código 1=wake, 3=STOP |
| V2 CONTROL_QUEUE | Control encolado, tipo legible e ID; todavía no es confirmación |
| V2 CONTROL_ACK | Confirmación recibida del backend, con identidad del control original |
| V2 CONTROL_RETRY / ACK_TIMEOUT / ACK_REJECTED | Reintento, límite agotado o rechazo explícito |
| V2 CANCEL | Posición conservadora enviada para recorte; `a`=played_ms |
| V2 HELLO_READY / PONG_READY | Cambio de disponibilidad xAI; `a`=0/1 |
| V2 RESET / REMOTE_ERROR / HEARTBEAT_TIMEOUT | Reinicio de transporte y pérdida de sesión |
| EARS | Peer, streaming, frames, descartes, errores de parsing, comandos y gaps UART |
| V2_STATS | PCM transmitido, fallos TX, NACK, retransmisiones, caché ausente, reintentos, descarte de arranque y RX inválido/viejo |
| AUDIO | Decode, pérdidas, FEC/PLC, descartes, jitter, rebuffer y reproducción estimada |
| STACK / RESOURCE | Margen mínimo de stacks UDP/playback/loop, heap libre/mínimo y mayor bloque libre |

Los grupos periódicos se escalonan, uno por segundo: **cada grupo se repite cada
cinco segundos**, evitando una gran ráfaga serial. No se agrega tarea de estadísticas.
AUDIO también intenta una instantánea al terminar/abortar, antes del reset.
La instantánea usa try-lock y se omite si jitter está ocupado; no imprime con el
mutex tomado. No son snapshots atómicos de todas las tareas.

EARS y V2_STATS son acumulativos desde arranque; AUDIO se reinicia por epoch.
`startup_drop` cuenta frames retenidos que fueron sobrescritos antes de START_ACK.
`pcm_tx` cuenta transmisiones locales exitosas, incluyendo reintentos; no prueba
recepción remota. `cache_miss` cuenta solicitudes sin copia disponible.
`fec` indica decode FEC intentado exitosamente, no prueba que hubiera FEC útil.

Stack HWM se expresa en bytes en este port ESP32. Es el mínimo margen observado,
no consumo actual. Se muestran los workers de audio y loop, no todos los stacks.
Heap mínimo es histórico desde boot. Estos valores requieren medición en placa.

Para quitar resúmenes y trazas detalladas, cambiar `-DPIUU_DIAGNOSTICS=1` a `0`
en `platformio.ini` y recompilar. Los mensajes básicos de error permanecen.
`PIUU_DEBUG_TX_PIN` solo cambia el destino UART del log; no hace falta para USB.

## Backend

| Evento | Qué permite verificar |
| --- | --- |
| BOOT / PEER_BOUND / PEER_REPLACED | Configuración y propietario de la sesión |
| CONTROL_RX / CONTROL_ACK | Petición, ID, ACK exitoso/rechazado |
| CONTROL_DUPLICATE | ACK reenviado; no repetir efecto de COMMIT/CANCEL |
| TURN_START / START_REJECTED / INPUT_CLOSE | Apertura explícita y límite final de entrada |
| INPUT_FIRST_APPEND / XAI_COMMIT_BEGIN / XAI_RESPONSE_REQUESTED | Primer append y solicitud de generación |
| XAI_READY / XAI_RESPONSE_CREATED | Configuración confirmada y asociación del response_id remoto |
| XAI_FIRST_AUDIO | Primer delta y tiempo desde solicitud en ese backend |
| XAI_AUDIO_DONE / XAI_RESPONSE_DONE | Bytes/duración generados y finalización remota |
| OUTPUT_FIRST_PCM / OUTPUT_END | Inicio y final del envío UDP de respuesta |
| XAI_CANCEL_BEGIN / XAI_RESPONSE_CANCEL | Posición estimada y cancelación solicitada |
| XAI_OPERATION / XAI_CONFIRMED | truncate/delete/clear, item, audio_end_ms y tiempo de confirmación |
| XAI_CANCEL_COMPLETE / XAI_CANCEL_FAILED | Limpieza confirmada o fallo que fuerza cierre remoto |
| SESSION_STATS / INPUT_STATS / INPUT_CLOSE_SUMMARY | Colas, reorder, descartes, NACK, caché ausente, silencio y errores |

SESSION_STATS/INPUT_STATS aparecen cada cinco segundos y en final/cancelación.
Los contadores de sesión son acumulativos durante la vida de ManagedSession;
los de entrada se reinician por turno. `silence_frames × 20` da ms de relleno.
`missing` registra todos los frames no recuperados, incluso si se superó el límite
de 200 ms de silencio por hueco. `nack_frames` son frames solicitados, no recuperados;
comparar con missing, cache_misses y retransmisiones de CORE.

`OUTPUT_END` significa END enviado localmente; no confirma reproducción física.
`played_ms`/`audio_end_ms` son estimaciones conservadoras, no una medida acústica.
`XAI_FIRST_AUDIO elapsed_ms` parte de la solicitud local de commit/respuesta,
no del inicio de habla. La duración de respuesta generada no equivale a escuchada.

Configurar `PIUU_LOG_LEVEL=WARNING` silencia los nuevos logs INFO sin tocar el
protocolo. Se conservan algunos prints legacy de arranque/WAV/errores. No se
habilita log por frame a ningún nivel.

## Orden de evaluación

1. BOOT EARS/CORE, LittleFS y disponibilidad XAI_READY/HELLO_READY.
2. Touch, START_ACK, frames UART/PCM, cierre touch y COMMIT_ACK.
3. response_id, primer audio, Playing, END y regreso a Idle; repetir otro turno.
4. Interrumpir con touch: CANCEL anterior, confirmaciones de recorte/clear,
   CANCEL_ACK y START_ACK del turno nuevo. No debe volver audio del anterior.
5. Repetir inicio/interrupción por wake y cierre por STOP con KEYWORD/emisor.
6. Reinicio/desconexión y pérdidas UDP controladas; evaluar reintentos, silencio,
   latencia percibida, heap mínimo y HWM de stacks.

No se agregaron aún inyectores de pérdida ni se cargó firmware. Logs correctos
permiten observar los casos, pero no reemplazan la prueba con radio, DAC y xAI real.

## Comprobación de heartbeat y reproducción (2026-10-11)

- Cierre normal: silence_close o touch → CANCEL_ACK; si el backend anuncia busy,
  mostrar Server busy y recuperar ready, sin reset de identidad por ese motivo.
- PONG atrasado con ACK/eventos/audio válidos: LINK_STATS muestra pong_age_ms
  creciente pero rx_age_ms reciente; no debe aparecer HEARTBEAT_TIMEOUT.
- Pausa de audio sin END de unos 2 segundos: conservar respuesta y reanudar.
  No debe aparecer PLAYED ni Listening por esa pausa.
- Sin datos durante 10 segundos con búfer vacío: playback_stalled y CANCEL,
  sin PLAYED. El plazo RESPONSE_STALL_TIMEOUT_MS es configurable.
- Fin normal: END y búfer drenado → PLAYED_ACK → Listening.
- Cortar energía al finalizar captura se analiza como interrupción externa.

## Rendimiento de CORE

Activo con `PIUU_PERFORMANCE_DIAGNOSTICS=1` en `platformio.ini`. Cambiar a `0` y
recompilar elimina los snapshots, temporizadores de Opus/I2S y contadores de picos
nuevos. Los logs anteriores de conexión/errores se conservan. `PIUU_DIAGNOSTICS`
controla los resúmenes anteriores; los eventos de error existentes no dependen todos
de esa opción. No hace falta modificar EARS ni backend.

El módulo `src/diagnostics/` toma una muestra cada cinco segundos desde `loop`,
sin crear tareas ni reservar memoria dinámica. Usa arrays estáticos para hasta 48
tareas y reparte las líneas posteriores (como máximo una cada 80 ms). Si la tabla
no alcanza, informa que no pudo obtener el snapshot y reinicia la referencia de CPU.

| Prefijo | Interpretación |
| --- | --- |
| `CPU` | Ocupación estimada por núcleo en la ventana anterior, calculada como 100% menos el tiempo de su tarea IDLE. El primer informe establece la referencia. `snapshot_us` mide la consulta a FreeRTOS. |
| `TASK` | CPU de cada tarea como porcentaje de un núcleo, afinidad y mínimo histórico de stack libre en **bytes**. `valid=0` indica una tarea sin muestra previa; ese 0% todavía no es una medición. `affinity=-1` indica que no tiene un núcleo fijo. |
| `PERF op=opus` | Cantidad, promedio y máximo en microsegundos de llamadas al decodificador, incluyendo FEC/PLC. `over20ms` cuenta llamadas que superaron el tiempo de un frame. |
| `PERF op=i2s_wait_write` | Duración de escritura de PCM, **incluyendo espera** por espacio en DMA; no representa consumo de CPU. `n=0` es normal en reposo. |
| `BUFFERS` | Ocupación actual/capacidad y picos de las colas PCM/eventos de EARS y del jitter buffer. `valid=0` significa que no se pudo leer el jitter sin esperar; sus ceros no deben interpretarse como mediciones. |
| `BUFFERS rebuf_*` | Cantidad, duración acumulada y máximo de pausas terminadas para rellenar audio, y duración de la pausa activa. Excluye el llenado inicial. No es una medida acústica de cortes. |
| `RESOURCE` | Heap libre, mínimo histórico y mayor bloque libre, como antes. |

Los tiempos de `PERF` se reinician por ventana de cinco segundos. Los picos de
EARS y los mínimos de stack corresponden a todo el arranque. El pico de jitter y
las pausas se reinician con el estado de reproducción de cada respuesta/epoch;
`total_ms`/`max_ms` incluyen pausas terminadas y `active_ms` muestra la aún abierta.
Los valores de cola se leen mientras los workers continúan: son snapshots, no un
registro completo de cada frame. Los contadores usan resta sin signo para tolerar
el rollover del reloj; los tiempos de CPU son estimaciones del scheduler y no un
perfil exacto de instrucciones/interrupciones. Las CPU de las tareas se expresan
respecto de **un** núcleo; la suma en el equipo puede acercarse a 200%.

Para comparar: guardar 20–30 s con conversación cerrada, abrir con wake/touch,
hablar y dejar terminar una respuesta, y guardar otros 20–30 s en reposo. Correlacionar
`CPU`, `PERF`, `BUFFERS` y `RESOURCE` con `CONV_STATS`/eventos de reproducción.
Un `max_us` alto en Opus, poco margen de stack o repetidos `rebuf_n` ayudan a separar
presión de procesamiento de irregularidad de llegada de paquetes. La instrumentación
consume recursos propios; comparar también una compilación desactivada si afecta al audio.

Validación local: suite host (incluida interoperabilidad CORE/backend), compilación
`cyd` y `cyd2usb` activa, y compilación CYD con métricas desactivadas. El firmware
sin métricas no contiene sus prefijos. En esta configuración, activarlas añade
4216 bytes de RAM estática (122752 bytes totales frente a 118536 desactivadas).
Los porcentajes reales de CPU y el efecto sobre el audio requieren el test en placa.
