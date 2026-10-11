# Análisis de captura integrada 20261011T185528.320755Z

Fuente: `logs/integrated/20261011T185528.320755Z/` (archivos locales ignorados por Git).
Solo análisis; no se modificó firmware ni configuración.

## Funcionamiento

- Tres aperturas por wake y tres respuestas terminadas con END, retorno a Listening y PLAYED_ACK.
- Dos cierres automáticos después de 30 s sin voz tras reproducir.
- CORE conecta al Wi-Fi a los ~30 s de arrancar. Después de HELLO_READY, los snapshots no muestran reachable=0.
- admission=0 durante cierre indica indisponibilidad para abrir conversación, no pérdida de conectividad.
- EARS desconectado a 18:59:46.747 UTC; peer_timeout a 18:59:49.251 UTC. KEYWORD desconectado después, CORE al final. No atribuir este cierre a una caída de red.
- No aparecen panic ni reinicios inesperados durante el ensayo. La captura contiene los arranques.

## Recursos

- CPU0: máximo 98,6%; earsUart llega a 78,2%. CPU1: máximo 49,0%.
- Reposo conectado: muestras cercanas a CPU0 6% / CPU1 16%; touch consume ~11% de un núcleo incluso sin conversación.
- Opus: 3701 llamadas (normal/FEC/PLC), promedio ponderado de promedios por ventana ~2454 us; máximo 8683 us; ninguna supera 20 ms.
- Escritura I2S incluyendo espera: promedio ~86 us; máximo 4832 us; ninguna supera 20 ms.
- playbackTask: CPU máxima 14,6%, mínimo histórico de stack libre 688 bytes. Margen reducido, sin evidencia de overflow.
- Heap mínimo histórico 70944 bytes; termina con 84656 libres y mayor bloque 42996. No se observa descenso continuo en esta captura; no demuestra ausencia de fugas a largo plazo.
- Snapshot de FreeRTOS: ~4,6 ms promedio, máximo 10,547 ms. Es carga de la propia instrumentación.

## Audio recibido

Los valores se toman del último AUDIO registrado antes del reset de cada respuesta.
END es el límite exclusivo de secuencia; todos los streams comienzan en 0.

| Respuesta / epoch | END | rx | Secuencias ausentes al recibir | Pausas de rebuffer |
| --- | ---: | ---: | ---: | --- |
| 1 / 3 | 1567 | 1562 | 5 | 0 |
| 2 / 7 | 1423 | 1322 | 101 | 1 de 1819 ms |
| 3 / 12 | 871 | 785 | 86 | 1 de 1540 ms |

En las dos últimas hay tanto reconstrucción FEC/PLC como salto de secuencia al
reanudar desde el mínimo disponible. `miss` no contabiliza todas las secuencias
saltadas: no interpretar 15/14 como el total de audio ausente. Los contadores de
recepción no distinguen pérdida en origen, red, Wi-Fi/socket o descarte previo al
jitter; hace falta correlacionar con backend antes de atribuir la causa.

## Uplink y UART

- Colas CORE EARS: pico PCM 4/6 y eventos 1/8; drop=0 y evt_drop=0.
- Aun así, CORE registra 42 frames de huecos de secuencia UART y 1 rechazo del parser.
- KEYWORD recibe 4250 frames al último resumen, sin drops de su cola. EARS registra 6 eventos válidos y relay_errors=0.
- Backend solicita 37 NACK; CORE registra 44 cache_miss y 0 retransmisiones. Parte podría corresponder a frames ausentes ya en UART: un frame nunca recibido por CORE no puede recuperarse desde su caché UDP. No atribuir automáticamente todos los misses a un TTL corto.

## Prioridad recomendada

1. Optimizar lectura UART de CORE por bloques. El código actual hace available/read/millis por byte; el driver Arduino instalado toma locks tanto en available como en read. Es un candidato concreto al alto consumo de earsUart, todavía requiere comparación en hardware.
2. Repetir la misma prueba y correlacionar pérdidas/pausas con logs backend de esta sesión. Revisar pacing y envío real antes de culpar a Internet o cambiar buffers.
3. Revisar margen de stack de playback y posteriormente optimizar el polling del touch.
4. Mantener buffers actuales durante ese diagnóstico. Bajar latencia después de eliminar las pausas; aumentarlos ahora puede ocultar la causa y añade retraso.

Pendiente: log backend de esta misma prueba, experiencia auditiva del usuario y
prueba comparativa tras optimizar UART. No se ha desplegado ningún cambio.

## Segunda captura: después de optimizar UART

Fuente: `logs/integrated/20261011T192215.062978Z/`. El usuario informa un problema
de conexión audífonos/DAC al principio; después lo corrigió. Primer, tercer y
cuarto intentos tuvieron audio audible; el segundo no completó una respuesta.

- earsUart máximo 5,4% (antes 78,2%); CPU0 máximo 26,4% (antes 98,6%). CPU1 máximo 46,3%.
- UART_RX: media acumulada 94 bytes/lectura, máximo 256. Se confirma lectura por bloques.
- Tres respuestas completadas: END/rx 995/995, 1515/1507 y 1419/1419. La respuesta del tercer intento registra 8 secuencias ausentes y una pausa final de rebuffer de 20 ms; las otras dos no tienen pausas.
- Opus máximo 7288 us y escritura I2S máximo 1523 us; ninguna llamada supera 20 ms.
- Heap mínimo histórico 77880 bytes; termina con 84364 libres. Playback conserva solo 692 bytes de margen mínimo de stack.
- Persisten 58 frames de huecos de secuencia UART y 1 rechazo del parser. Son cuatro intentos y una captura más larga: no comparar directamente el total 58 con los 42 anteriores. No afirmar que UART ya sea totalmente libre de pérdidas.
- 63 NACK, 91 cache_miss y 0 retransmisiones; sigue pendiente explicar qué huecos estaban ya antes de entrar en CORE y cuáles ocurren después.

### Segundo intento sin respuesta

A las 19:24:02 UTC el wake abre turn=2. START_ACK y evento de proveedor listo
llegan correctamente. El proveedor anuncia una primera respuesta, luego nuevo
inicio de voz la interrumpe antes de reproducirse. A **19:24:05.375 UTC** CORE
recibe `REMOTE_ERROR` (ERROR del protocolo desde el backend), ejecuta RESET y
retorna a Idle. A 19:24:07.618 UTC negocia otra sesión. No hay una transición a
Playing en este intento ni una caída de Wi-Fi registrada que explique el error.

El log CORE no transporta la causa detallada de ERROR. Solicitar log del backend
entre 19:24:02 y 19:24:08 UTC (16:24:02–16:24:08 Argentina), especialmente eventos
`CONVERSATION_FAILED`, errores del proveedor y cierres WebSocket. No atribuirlo
al DAC ni a Internet sin esa evidencia. Tampoco afirmar que reinició el proceso
del servidor solo porque cambió el identificador de sesión.

Mantener buffers actuales. Prioridad inmediata: diagnóstico del error remoto y
luego investigar los huecos UART residuales. No se modificó firmware al analizar
esta captura.

Nota de segmentación: el capturador anterior permaneció esperando y registró un
arranque adicional de CORE a 19:20:37 UTC en la carpeta antigua. Para comparación,
la captura base es el tramo anterior a la desconexión de 19:00:01 UTC; no mezclar
ese arranque posterior con el ensayo inicial.

## Correlación con backend: segundo intento explicado

Fuente: `logs/integrated/backend-test-oct11.log`, mismo proceso Python 1057955.
Los IDs de conversación y respuesta coinciden con CORE. El servidor no reinició
su proceso en el fallo; cerró la conversación y luego aceptó otra sesión.

| Hora UTC del backend | Evidencia |
| --- | --- |
| 19:24:02.965 | XAI_READY / PROVIDER_READY; proveedor conectado |
| 19:24:03.387 | SPEECH_STARTED, utterance=1 |
| 19:24:03.517 | SPEECH_STOPPED |
| 19:24:03.665 | RESPONSE_STARTED y XAI_RESPONSE_CREATED, turn=2 resp=1 |
| 19:24:03.665 | SPEECH_STARTED, utterance=2, interrupted_response=1 |
| 19:24:04.675 | INPUT_SEND_FAILED, TimeoutError |
| 19:24:05.265 | HISTORY_REPAIR_FAILED, TimeoutError |
| 19:24:05.375 (PC) | CORE recibe REMOTE_ERROR |
| 19:24:07.267 | XAI_CLOSE_FORCED |
| 19:24:07.269 | CONVERSATION_FAILED cause=history_uncertain |

La causa inmediata del cierre está confirmada: falla la reparación de historial
al interrumpir la respuesta. No se registra XAI_ERROR ni RESPONSE_DONE de esa
respuesta antes del cierre, ni XAI_OPERATION de truncate/delete. Comparado con
el código local, esto apunta al plazo de espera de `done.wait()` (1 s) anterior a
editar historial, aunque el log actual solo registra TimeoutError sin etapa.
No permite asegurar por qué faltó esa confirmación del proveedor.

`VadXai.pause_input()` bloquea nuevos appends mediante history_ready. El timeout
de envío (1 s) envuelve también esa espera: INPUT_SEND_FAILED **no prueba** que
haya fallado un envío por la red. Durante la reparación fallida hay input_queue=16
y dropped_full=94. Separar una pausa coordinada de un timeout real de transporte
es una mejora necesaria de lifecycle/backpressure.

El siguiente cambio recomendado es robustecer la interrupción temprana y su
confirmación/cancelación por ID, conservar la reparación del historial y el
fallback de cierre cuando no pueda verificarse, distinguir los estados de espera
y añadir logs de etapa. Probar respuesta creada seguida inmediatamente de nueva
voz, confirmación tardía/ausente y cola de entrada acotada. No basta con agrandar
buffers o suprimir el error.

Pacing: primera y cuarta respuestas finalizan a 50 fps, sin rebases. Tercera
registra un atraso inicial de ~2093 ms y un rebase; los incrementos posteriores
son ~250 frames/5 s. Su promedio global 46,76 fps incluye ese atraso inicial;
no indica por sí solo un envío sostenido a 46,76 fps. Hace falta más detalle de
etapa para atribuir ese retraso inicial a proveedor, puerta de salida o servidor.

No se modificó código del backend ni firmware durante este análisis.
