# Evaluación de propuestas para audio y Bluetooth

Fecha: 2026-10-10. Documento de análisis; no introduce cambios de firmware.
Bluetooth sigue siendo una etapa posterior. Las recomendaciones son candidatas
para pruebas en placa, no una distribución definitiva de tareas.

## Opinión general

La preocupación por tiempo real, buffering y memoria es válida. Sin embargo,
no está demostrado que Opus sea nuestro mayor cuello de botella ni que separar
el trabajo exactamente como se propone sea óptimo. Un retraso de algunos
milisegundos no provoca necesariamente un corte: depende del audio disponible
en los buffers y del plazo de la salida. Tampoco basta con que el promedio de
CPU sea bajo si existen pausas largas o ráfagas que vacían esos buffers.

CORE también deberá recibir UART de EARS, enviar PCM al servidor y atender UI.
La reproducción SD → Bluetooth probada por separado no valida todavía esa carga
conjunta con WiFi, Opus y futura recepción continua de EARS.

## 1. Distribución de núcleos: válida como experimento, corregir los supuestos

Separar trabajo puede ayudar, pero una tarea de aplicación fijada a un núcleo
no mueve el controlador Bluetooth ni su host. La afinidad depende de la
configuración del SDK. No decidiría todavía que Opus debe ejecutarse en Core 0:
puede competir allí con red; DSP y UI también necesitan presupuesto.
[Documentación de prioridades y afinidad](https://docs.espressif.com/projects/esp-idf/en/v5.5.1/esp32/api-guides/performance/speed.html).

Hallazgos locales:

- `audio_downlink.cpp` fija RX UDP, playback/Opus y estadísticas en Core 1,
  con prioridades 3, 2 y 1 respectivamente.
- El `sdkconfig.h` instalado para ESP32/dio_qspi tiene
  `CONFIG_BT_BLUEDROID_PINNED_TO_CORE=0` y
  `CONFIG_BTDM_CTRL_PINNED_TO_CORE=0`. No corresponde asumir Core 1 por defecto.
- Esta observación identifica el SDK instalado de CORE; hay que verificar
  nuevamente la configuración efectiva al integrar Bluetooth. No basta agregar
  un `#define` de aplicación para reconfigurar bibliotecas precompiladas.

WiFi y Bluetooth comparten una radio de 2,4 GHz en el ESP32 clásico. Distribuir
CPU no elimina esa competencia. Espressif contempla separar afinidades de los
stacks como ajuste de coexistencia, no como garantía de audio continuo.
[Coexistencia RF](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-guides/coexist.html).

**Anotar:** comparar afinidades con carga real. Separar responsabilidades primero;
crear una tarea por cada módulo solo cuando resuelva un bloqueo o plazo medido.
Un filtro sencillo puede ejecutarse junto al decoder sin otra cola/tarea.

## 2. Colas de punteros: sí a un pool fijo; evitar malloc/free por frame

Para PCM entre etapas independientes, usar buffers preasignados y transferir
punteros o índices es razonable. Preferir un pool acotado, reservado al iniciar,
con devolución de bloques. No asignar y liberar memoria por cada paquete.

Contrato necesario:

- Un único propietario de cada bloque en cada momento.
- Si falla el envío a la cola, el productor conserva y devuelve el bloque.
- El consumidor devuelve el bloque al pool; no llama a `free()` sobre un pool.
- Definir comportamiento al llenarse, abortar, cambiar salida o desconectarse.
- No encolar punteros a variables de stack, buffers RX reutilizados ni slots de
  jitter que puedan sobrescribirse mientras otro consumidor los usa.

No aplicaría esta regla a todos los mensajes: copiar eventos pequeños y
credenciales acotadas sigue siendo apropiado. Las colas de control actuales no
justifican una migración a punteros.

Nuestro PCM16 mono a 16 kHz representa **32 000 bytes/s**, con **640 bytes por
frame de 20 ms**. Una copia de esos bloques no es automáticamente el cuello de
botella. El jitter actual ya usa slots estáticos; considerar su reutilización
antes de añadir otro pool de paquetes y duplicar RAM. Esto es una recomendación
basada en el código local y en el volumen calculado, pendiente de medición.

## 3. Decoder Opus: evaluar alternativas, sin prometer aceleración

ESP-Skainet se orienta a wake word, reconocimiento de comandos y frontend de
voz; no es el nombre de un decoder Opus optimizado que debamos incorporar.
[Repositorio oficial ESP-Skainet](https://github.com/espressif/esp-skainet).

`esp_audio_codec`, del ecosistema de audio Espressif, sí ofrece decoder Opus y
merece comparación. No hace falta adoptar todo ESP-ADF para plantear ese ensayo.
Los resultados publicados de su README se midieron en ESP32-S3R8: no prueban una
ganancia equivalente en nuestro ESP32 LX6. La disponibilidad de optimizaciones
assembly para otros chips tampoco demuestra que se apliquen a este target.
[Componente oficial y condiciones de benchmark](https://github.com/espressif/esp-adf-libs/blob/master/esp_audio_codec/README.md).

La dependencia actual `esp32_opus` tiene `FIXED_POINT` definido en `src/config.h`.
No la descartaría únicamente por utilizarse desde Arduino. Comparar ambos con
los mismos frames reales a 16 kHz mono: tiempo de decode, peor caso, memoria,
stack, calidad y soporte equivalente de FEC/PLC. No registrar como hecho una
reducción a «una fracción de los ciclos» sin esa comparación.

## 4. SRAM y PSRAM: criterio correcto; malloc no garantiza RAM interna

Priorizar memoria interna para buffers inmediatos y estructuras sensibles a
latencia. Cuando se necesite garantizarlo, reservar con
`heap_caps_malloc(size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)` y comprobar el
resultado. Añadir capacidad DMA cuando el consumidor realmente la requiera;
un buffer PCM de aplicación no necesita DMA por definición.

`malloc()` depende de la política de asignación del SDK y puede usar PSRAM
cuando está incorporada al heap. El SDK local tiene `CONFIG_SPIRAM_USE_MALLOC=1`;
eso no prueba que la placa tenga PSRAM instalada o habilitada en ejecución.
No asumir PSRAM disponible por usar `board=esp32dev`.
[Asignación por capacidades](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/system/mem_alloc.html).

PSRAM puede servir para almacenamiento menos urgente, si existe, pero no la
limitaría exclusivamente a «segundos de audio»: depende del patrón de acceso y
las restricciones del consumidor. Mantener reserva interna para stacks, radio
y drivers; medir heap mínimo y mayor bloque libre, además del heap total.
[Restricciones de RAM externa](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-guides/external-ram.html).

## 5. Prioridades: favorecer audio, sin prioridades máximas indiscriminadas

Las tareas de aplicación deben respetar las tareas internas de WiFi/Bluetooth,
bloquearse al no tener trabajo y mantener secciones críticas cortas. Elevar RX
UDP o un productor PCM al máximo puede perjudicar al stack que necesita atender.
El callback de salida debe consumir datos preparados, con trabajo acotado;
no decodificar, dibujar, guardar NVS ni esperar indefinidamente allí.
[Planificación y tareas internas de ESP-IDF](https://docs.espressif.com/projects/esp-idf/en/v5.5.1/esp32/api-guides/performance/speed.html).

**Anotar:** prioridades relativas y afinidad pendientes de medición. Proteger el
suministro de PCM, pero conservar CPU para red, controlador, UART, UI e idle.
Mover NVS o logs a otro núcleo no garantiza que desaparezcan sus pausas.

## Aspectos que faltan en las propuestas

### A2DP y BLE Audio son caminos diferentes

La prueba usa `BluetoothA2DPSource`: nuestro candidato actual es Bluetooth
clásico/A2DP. Bluedroid soporta Classic; NimBLE es BLE y no reemplaza ese stack
para A2DP. BLE Audio requiere una evaluación independiente de hardware y stack;
no asumirlo disponible por disponer de BLE en el ESP32 clásico.
[Stacks Bluetooth](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/bluetooth/index.html),
[API A2DP](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/bluetooth/esp_a2dp.html),
[Capacidades del ESP32](https://documentation.espressif.com/esp32_datasheet_en.html).

### La salida puede necesitar otro formato

El formato interno y del servidor se mantiene en **16 kHz, mono, PCM16**.
`Bluetooth-Devices/src/main.cpp` prepara su prueba MP3 para **48 kHz estéreo**;
esa expectativa del código no acredita por sí sola el formato negociado con el
parlante. Verificarlo al integrar.

Si el backend A2DP necesita 48 kHz estéreo, adaptar en la frontera de salida:
resamplear y duplicar canales, sin cambiar EARS ni el servidor por esa razón.
Ese PCM pasa de 32 000 a **192 000 bytes/s**. Incluir el costo de la codificación
SBC y sus buffers; no se envía Opus directamente a un parlante A2DP convencional
por reutilizar nuestro decoder.

### Buffering y relojes

La salida Bluetooth consume según su propio ritmo; no asumir que pedirá siempre
640 bytes cada 20 ms. Preparar PCM en una cola/ring acotado, conservar sobrantes
y definir la política de underrun según la API elegida. Observar deriva entre
producción y consumo durante sesiones largas; el buffer no debe crecer sin límite.

La latencia almacenada también afecta la interrupción: una cola grande evita
algunos cortes, pero demora el silencio y puede reproducir audio cancelado.
Definir flush/generación local para nuestros buffers; no prometer retirar audio
que ya recibió el parlante.

## Criterios anotados para la etapa Bluetooth

1. Mantener 16 kHz mono como contrato interno y del servidor.
2. Conservar `audio_output` como frontera de DAC/Bluetooth y adaptación de formato.
3. Usar memoria acotada; pools por valor/índice cuando aporte beneficios medidos.
4. Mantener ligero el callback Bluetooth y sacar de él decode/DSP/bloqueos.
5. Medir antes de cambiar decoder, afinidades o prioridades.
6. Validar Opus+WiFi+Bluetooth, UART bidireccional, touch/UI y sesiones largas juntos.
7. Registrar percentiles y máximo de decode, underruns/overruns, ocupación de
   buffers, latencia, heap interno mínimo y margen de stack.

No se aprueban como decisiones definitivas: Opus obligatoriamente en Core 0,
Bluetooth por defecto en Core 1, prioridad máxima para RX/PCM, malloc/free por
frame, equivalencia NimBLE/A2DP o aceleración garantizada por ESP-Skainet.
