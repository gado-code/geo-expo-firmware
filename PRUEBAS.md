# PRUEBAS · GEO-EXPO ALERT (firmware etapa 1)

Placas: ESP32 DevKit V1 (`src/main.cpp`) y Heltec WiFi LoRa 32 V3 (`src/lora/main_lora.cpp`) · Última sesión con placa: **9-sep-2026** · Última revisión del código: **22-sep-2026**

> ⚠️ **Los cambios del 10-sep y del 22-sep NO están probados en placa** (las dos
> sesiones se hicieron sin ella). Están cubiertos por las pruebas automáticas de
> la §0-bis, pero las tablas de abajo siguen reflejando la sesión del 9-sep. Al
> volver a tener el ESP32: flashear y repetir **0.4, 0.5, 1.1-1.10 y 3.x**, más
> lo nuevo del zumbador y el botón externo (**§4-bis**). Las pruebas del enlace
> LoRa (**§5**) esperan a que haya dos Heltec V3.

Marca cada casilla: **x** = pasado con registro · *?* = dado por bueno sin log · ☐ = sin probar.
Monitor serie a **115200 bps**. App móvil: **nRF Connect for Mobile**.

---

## 0. Puesta en marcha

| # | Caso | Procedimiento | Resultado esperado | OK | Observaciones |
|---|---|---|---|:--:|---|
| 0.1 | Entorno Fedora | `id \| grep dialout` tras logout/login | El usuario está en `dialout` | **x** | Ya estaba en `dialout`; no hizo falta el logout. |
| 0.2 | Puerto serie | Conectar placa, `pio device list` | Aparece `/dev/ttyUSB0` (o `ACM0`) | **x** | `/dev/ttyUSB0`, chip CP2102. El fallo anterior era un cable de solo carga. |
| 0.3 | Compila | `pio run -e devkit_v1` | `[SUCCESS]` | **x** | RAM 11,1 % · Flash 46,8 %. |
| 0.4 | Flashea | `pio run -e devkit_v1 -t upload` | `[SUCCESS]`, la placa reinicia | **x** | Sólo a **115200**; 460800 y 921600 fallan con este cable. |
| 0.5 | Banner | Abrir monitor | Se imprime el banner + `Estado inicial: IDLE` | **x** | Banner completo + `Estado inicial: IDLE` a los 418 ms. |

---

## 0-bis. Pruebas automáticas (sin placa)

Toda la lógica que no necesita hardware vive en `lib/` y se prueba en el PC.
Es lo primero que hay que correr antes de flashear nada:

```bash
pio test -e native      # ~1,5 s, no hace falta la placa
pio test -e devkit_v1   # las mismas pruebas, ejecutadas en el ESP32
```

| # | Suite | Qué cubre | Casos | OK |
|---|---|---|:--:|:--:|
| 0.6 | `test/test_nmea/` | Parseo NMEA: GGA, RMC, hemisferios con signo, checksum corrupto, trama sin fix, desbordamiento, flujo carácter a carácter | 16 | **x** |
| 0.7 | `test/test_panic/` | FSM del botón: umbral de los 3 s, rebotes al pulsar y al soltar, cancelar desde corta y desde larga, ventana que expira, arranque con el botón pulsado, ciclos encadenados, desbordamiento de `millis()` | 18 | **x** |
| 0.8 | `test/test_link/` | Protocolo GEO-LINK: ida y vuelta de todos los campos, coordenadas negativas, tamaño/magic/versión/tipo malos, **un bit cambiado en cualquier byte firmado**, clave distinta, orden falsificada, contador circular y antirrepetición | 22 | **x** |
| 0.9 | `test/test_ranging/` | Cercanía: umbrales de zona, distancia a 1 m y su crecimiento, configuración absurda sin dividir por cero, suavizado que amortigua un pico, pérdida de señal por silencio, recuperación sin arrastrar lo viejo, tendencia acercarse/alejarse | 15 | **x** |
| 0.10 | `test/test_buzzer/` | Zumbador: un patrón suena y termina, repetición finita e infinita, un `loop()` atascado no estira el patrón, prioridades (la baliza no la pisa una alerta), `stopIf`, modo mudo, patrón vacío | 16 | **x** |

Resultado del 22-sep: **87 de 87 pasan** (34 del 10-sep + 53 nuevas).

> Si `pio` no puede descargar el toolchain (sin red, o el registro bloqueado),
> `./tools/comprobar-sintaxis.sh` al menos comprueba que los dos firmwares
> compilan. No sustituye a las pruebas, pero caza erratas en segundos.

Las pruebas de `test_panic` simulan el botón **milisegundo a milisegundo**, así
que cubren cosas que a mano son casi imposibles de reproducir: soltar a 2999 ms
exactos, un rebote de 1 ms, o lo que pasa a los 49,7 días cuando `millis()`
vuelve a cero. Los casos 1.6, 1.7, 1.9 y 1.10 de la tabla de abajo tienen ahí
su equivalente automático, así que una regresión sale a la luz sin tener que
volver a pulsar el botón con un cronómetro.

---

## 1. Máquina de estados del botón (LED + serie, sin BLE)

| # | Caso | Procedimiento | Resultado esperado (serie) | Resultado esperado (LED) | OK | Observaciones |
|---|---|---|---|---|:--:|---|
| 1.1 | Pulsación corta | Pulsar BOOT y soltar en < 1 s | `IDLE→DEBOUNCE→PRESSED`, `>>> TX ALERT:1`, `PRESSED→CANCEL_WINDOW` | 1 parpadeo largo (~0,8 s), luego respiración lenta | **x** | 4 veces, todas limpias (t=261458, 274018, 286711, 344839, 436866 ms). |
| 1.2 | Ventana expira (alerta confirmada) | Tras 1.1, esperar 10 s sin tocar | `CANCEL_WINDOW→IDLE (ventana expirada: alerta CONFIRMADA)` | Respiración durante 10 s, luego apagado | **x** | **10000 ms exactos** en las 5 alertas medidas. |
| 1.3 | Pulsación prolongada | Mantener BOOT pulsado ≥ 3 s | A los **3 s exactos y sin soltar**: `>>> TX ALERT:2`, `PRESSED→CANCEL_WINDOW (ALERT:2 ...)` | 2 parpadeos, luego respiración | **x** | `ALERT:2` a los **3000 ms clavados** (PRESSED 481993 → TX 484993). |
| 1.4 | Soltar tras pulsación larga | Soltar BOOT después de 1.3 | `CANCEL_WINDOW: botón liberado -> cancelación ARMADA` | Sigue la respiración | **x** | `cancelación ARMADA` a los 466 ms de soltar. |
| 1.5 | Cancelar (desde corta) | Hacer 1.1 y, antes de 10 s, pulsar BOOT otra vez | `>>> TX CANCEL`, `CANCEL_WINDOW→IDLE (CANCEL ...)` | 3 parpadeos rápidos, luego apagado | **x** | **El fallo del re-disparo queda corregido**: tras el `CANCEL` (t=350176) no hay nada hasta la siguiente pulsación 86 s después. |
| 1.6 | Cancelar (desde larga) | Hacer 1.3, soltar, y antes de 10 s pulsar BOOT | `>>> TX CANCEL`, vuelta a `IDLE` | 3 parpadeos rápidos, apagado | **x** | Verificado pulsando GPIO0 por software (DTR): `ALERT:2` → `cancelación ARMADA` al soltar → `CANCEL` a los 0,5 s. |
| 1.7 | Antirrebote flanco de bajada | Rozar BOOT con contacto muy breve (< 50 ms) | `DEBOUNCE→IDLE (rebote/ruido...)`, **sin** `ALERT` | Sin cambios visibles | **x** | Pulso de 20 ms: `DEBOUNCE → IDLE (rebote/ruido)` y ninguna alerta. |
| 1.8 | Antirrebote flanco de subida | Pulsación corta normal | **Un solo** `ALERT:1` (no varios por rebote al soltar) | 1 parpadeo largo | **x** | Un solo `ALERT:1` por pulsación en las 5 registradas. |
| 1.9 | Umbral just-in-time | Soltar BOOT ~2,8 s (justo antes de 3 s) | Se emite `ALERT:1`, **no** `ALERT:2` | 1 parpadeo largo | **x** | **Destapó el fallo I11.** Barrido de 2900/2980/2960/3010/3060 ms: antes fallaba entre 2950 y 3000 ms; tras el arreglo el umbral cae limpio en 3000 ms. |
| 1.10 | Reingreso tras confirmar | Tras 1.2, pulsar BOOT de nuevo | Nuevo ciclo `ALERT:1` normal | Igual que 1.1 | **x** | Tres ciclos completos encadenados sin residuos de estado. |

---

## 2. Baliza simulada por comando serie (sin móvil)

| # | Caso | Procedimiento | Resultado esperado | OK | Observaciones |
|---|---|---|---|:--:|---|
| 2.1 | `BEACON:ON` | Escribir `BEACON:ON` + Enter en el monitor | `RX BEACON:ON -> baliza ACTIVADA`; LED parpadea a **2 Hz** (250 ms on / 250 ms off) | **x** | |
| 2.2 | Prioridad de la baliza | Con baliza ON, pulsar BOOT | Llega `>>> TX ALERT:1` por serie, pero el LED **sigue** a 2 Hz | ☐ | Sin probar. |
| 2.3 | `BEACON:OFF` | Escribir `BEACON:OFF` + Enter | `baliza DESACTIVADA`; el LED vuelve a reposo o respiración según el estado | **x** | |
| 2.4 | Comando inválido | Escribir `HOLA` + Enter | `RX comando no reconocido: "HOLA"` | **x** | `XYZ` → `comando no reconocido`. |
| 2.5 | Ayuda | Escribir `?` + Enter | Se imprime la lista de comandos | **x** | |

---

## 3. BLE con nRF Connect

| # | Caso | Procedimiento | Resultado esperado | OK | Observaciones |
|---|---|---|---|:--:|---|
| 3.1 | Visible en escaneo | nRF Connect → Scan | Aparece **`GEOEXPO-ALERT`** | **x** | Visto desde el móvil y también desde el Bluetooth del PC (`3C:8A:1F:A7:31:DA`). |
| 3.2 | Conexión | CONNECT en nRF Connect | Serie: `BLE: cliente CONECTADO` | **x** | Conexión verificada por partida doble: móvil y `bluetoothctl` del PC. |
| 3.3 | Servicio y características | Desplegar servicio `6E400001-...` | `6E400003-...` (Notify) y `6E400002-...` (Write) | **x** | |
| 3.4 | Suscripción a TX | Activar *notifications* en `6E400003-...` | Sin error; icono de suscripción activo | **x** | Ojo: el icono es un interruptor. Fiarse del texto `Notifications enabled`, no del icono. |
| 3.5 | Recibir `ALERT:1` | Pulsación corta de BOOT | En `6E400003-...` llega `ALERT:1\n` (ver en UTF-8) | **x** | `Value: ALERT:1` en el móvil. |
| 3.6 | Recibir `ALERT:2` | Pulsación prolongada de BOOT | Llega `ALERT:2\n` a los 3 s | ☐ | Sin probar por BLE (sí por serie, caso 1.3). |
| 3.7 | Recibir `CANCEL` | Cancelar dentro de la ventana | Llega `CANCEL\n` | ☐ | Sin probar por BLE (sí por serie, caso 1.5). |
| 3.8 | Enviar `BEACON:ON` | Write UTF-8 `BEACON:ON` en `6E400002-...` | Serie: `<<< RX (BLE) "BEACON:ON"`; LED a 2 Hz | *?* | El usuario lo dio por bueno, pero **no quedó registrado en el log**. Repetir para dejar constancia. |
| 3.9 | Enviar `BEACON:OFF` | Write UTF-8 `BEACON:OFF` | LED deja de parpadear | *?* | Igual que 3.8. |
| 3.10 | Desconexión y re-advertising | DISCONNECT en nRF Connect | Serie: `BLE: cliente DESCONECTADO -> se reanuda el advertising`; vuelve a ser visible en Scan | **x** | Tras desconectar el PC, el móvil volvió a encontrarlo sin resetear la placa. |
| 3.11 | Funciona sin cliente | Desconectado, pulsar BOOT | Serie: `... registrado solo en serie`; la FSM avanza igual | **x** | Todo el log previo al móvil: `registrado solo en serie`, con la FSM avanzando igual. |
| 3.12 | Reconexión | CONNECT otra vez, repetir 3.5 | Todo vuelve a funcionar | **x** | PC → desconexión → móvil, sin tocar la placa. |

---

## 3.bis. Módulo GPS sin receptor físico

Las tramas se inyectan por el monitor serie (o por BLE con un WRITE en RX: es
el mismo manejador). Copia las tramas **enteras**, con `$` y `*HH`.

| # | Qué escribes | Qué debe salir | OK | Observaciones |
|---|---|---|:--:|---|
| 3b.1 | `POS` (recién arrancada) | `GPS  tramas OK=0  descartadas=0` y `sin posicion valida` | **x** | `OK=0  descartadas=0`, `sin posicion valida`. |
| 3b.2 | `NMEA $GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47` | `trama ACEPTADA` · `lat=48.117300  lon=11.516667  sats=8` | **x** | `lat=48.117300  lon=11.516667  sats=8`. |
| 3b.3 | `POS` otra vez | La misma posición, con la antigüedad del fix creciendo | **x** | Antigüedad del fix creciendo correctamente. |
| 3b.4 | `NMEA $GNRMC,041515.00,A,0412.3456,N,07405.6789,W,0.06,,090926,,,A*4F` | `lat=4.205760  lon=-74.094648` (longitud **negativa**: oeste) | **x** | `lat=4.205760  lon=-74.094648` (longitud negativa, oeste). |
| 3b.5 | La trama de 3b.2 cambiando `*47` por `*40` | `trama DESCARTADA` · `descartadas=1` · la posición anterior **no cambia** | **x** | `trama DESCARTADA`, `descartadas=1`, **la posición anterior intacta**. |
| 3b.6 | `NMEA $GPGGA,123519,,,,,0,00,,,M,,M,,*6B` | Trama aceptada (`OK` sube) pero `sin posicion valida`: calidad 0 | **x** | Trama válida (`OK` sube) pero `sin posicion valida`: calidad 0. |
| 3b.7 | `pio test -e devkit_v1` | 16 pruebas, todas `PASSED` | **x** | **16/16 PASSED** en la placa, 23,6 s. |

> 3b.5 es la prueba importante: un checksum malo **nunca** debe corromper la
> última posición buena. Es lo que separa una posición fiable de una inventada.

---

## 4. Verificación final (la checklist del enunciado)

| # | Requisito | Casos que lo cubren | OK | Estado |
|---|---|---|:--:|---|
| V1 | El LED responde al botón BOOT según lo especificado | 1.1–1.10 | **x** | La lógica está verificada por serie con marcas de tiempo. Faltan 1.6, 1.7 y 1.9. |
| V2 | El dispositivo aparece como `GEOEXPO-ALERT` al escanear con nRF Connect | 3.1 | **x** | Confirmado desde el móvil y desde el PC. |
| V3 | Al pulsar llegan los mensajes correctos a nRF Connect | 3.5, 3.6, 3.7 | **x** | `ALERT:1` llegó al móvil. `ALERT:2` y `CANCEL` sólo verificados por serie. |
| V4 | Al escribir `BEACON:ON` desde nRF Connect el LED empieza a parpadear | 3.8 | *?* | Dado por bueno de palabra, sin log. Repetir para dejar constancia. |
| V5 | El firmware sabe convertir tramas NMEA en una posición | 3b.2–3b.7 | **x** | 16/16 pruebas automáticas + inyección manual en la placa. |

---

## 4-bis. Llavero cableado: botón externo, zumbador y batería (`devkit_llavero`)

Todo esto es **del 22-sep y está sin probar en placa**: hace falta soldar el
botón, el piezo y el divisor de batería. Compilar con
`pio run -e devkit_llavero -t upload`.

| # | Caso | Procedimiento | Resultado esperado | OK |
|---|---|---|---|:--:|
| 4b.1 | Arranca con los pines nuevos | Flashear y abrir el monitor | El banner dice `Boton GPIO4`, `Zumbador piezo: GPIO25` y `Medida de bateria: GPIO34` | ☐ |
| 4b.2 | Botón externo | Pulsación corta en el botón nuevo | Igual que el caso 1.1 (BOOT ya no se usa) | ☐ |
| 4b.3 | El arranque no depende del botón | Resetear **con el botón pulsado** | La placa arranca normal y **no** dispara alerta al soltarlo | ☐ |
| 4b.4 | Prueba de zumbador | Escribir `BEEP` | Dos notas cortas ascendentes | ☐ |
| 4b.5 | Sonido de cada evento | Repetir 1.1, 1.3, 1.5 y 1.2 | Un pitido / dos pitidos / tres descendentes / dos secos al confirmar | ☐ |
| 4b.6 | Prioridad del sonido | Con `BEACON:ON`, pulsar el botón | La sirena de la baliza **no** se interrumpe; la alerta sale igual por BLE y serie | ☐ |
| 4b.7 | Modo mudo | `MUTE:ON`, pulsar el botón, `MUTE:OFF` | Con mudo no suena nada pero el LED y el BLE siguen; al quitarlo vuelve el sonido | ☐ |
| 4b.8 | Batería | `STATUS` con la batería puesta | Tensión creíble (3,3-4,2 V) y porcentaje acorde | ☐ |
| 4b.9 | Aviso de batería baja | Bajar la batería (o simular con el divisor) por debajo del 20 % | `STATUS` marca `<< BAJA` | ☐ |
| 4b.10 | Arranque a prueba de fallos (I12) | Provocar tres cuelgues en `init()` (o flashear un binario de los que fallan) | Al cuarto arranque: `BLE: DESACTIVADO tras 3 arranques colgados`, y **el botón sigue funcionando**. `BLE:RETRY` lo rearma | ☐ |

> 4b.10 es el caso importante: lo grave de la I12 nunca fue quedarse sin
> Bluetooth, sino que el bucle de reinicio dejaba el **botón de pánico**
> inservible.

---

## 5. Enlace LoRa punto a punto (dos Heltec WiFi LoRa 32 V3)

**Sin placas todavía.** El detalle de cada prueba, con lo que tiene que salir
en el monitor, está en **`LORA-P2P.md` §7**; aquí queda el resumen para no
perder la cuenta.

| # | Caso | Resultado esperado | OK |
|---|---|---|:--:|
| L1 | Flashear las dos (`heltec_tag`, `heltec_finder`) | Banner con el papel correcto y `RADIO: escuchando` | ☐ |
| L2 | Balizas en reposo | El buscador imprime una línea cada 15 s | ☐ |
| L3 | Alejarse con el llavero | La zona baja de `AQUI` a `CERCA` y a `LEJOS`; la flecha dice que te alejas | ☐ |
| L4 | Alerta corta | `*** ALERTA CORTA ***` en el buscador, ACK de vuelta, `ALERTA CONFIRMADA` en el llavero | ☐ |
| L5 | Cancelar dentro de los 10 s | Llega la cancelación y el buscador se calla | ☐ |
| L6 | Buscador apagado | El llavero reintenta 6 veces y avisa de que nadie contestó | ☐ |
| L7 | Modo búsqueda (`FIND`) | El llavero pita; el buscador pita cada vez más seguido al acercarse | ☐ |
| L8 | Clave distinta en una placa | Todas las tramas salen como `firma invalida` | ☐ |
| L9 | Alcance real en el patio | **Anotar los metros**: es el dato que va a preguntar todo el mundo | ☐ |
| L10 | Consumo con batería | Cuánto dura con baliza lenta y con baliza rápida | ☐ |

---

## Registro de incidencias

Lo que costó tiempo de verdad en la sesión del 9-sep. Casi nada fue el código.

| # | Qué pasaba | Causa real | Estado |
|---|---|---|---|
| I1 | La placa no enumeraba por USB con ninguno de los 3 cables disponibles. | **Cables micro-USB de solo carga**, sin las líneas de datos. El puerto y la placa estaban bien (el pendrive enumeraba y el LED rojo encendía). | ✅ Resuelto con un cable de datos. |
| I2 | `A fatal error occurred: Unable to verify flash chip connection (No serial data received.)` al flashear. | `upload_speed` demasiado alto para este cable: fallan **921600 y también 460800**. | ✅ Fijado a **115200** en `platformio.ini`. |
| I3 | Cada `CANCEL` generaba un `ALERT:1` fantasma 12 ms después, que se confirmaba solo a los 10 s. | `ST_IDLE` disparaba por **nivel** (`raw == LOW`) en vez de por **flanco**, y se entraba a `IDLE` con el botón aún pulsado. | ✅ Corregido: se exige flanco HIGH→LOW con `g_idlePrevRaw`. Verificado en la placa (caso 1.5). |
| I4 | En nRF Connect la placa aparecía en el escaneo, pero al pulsar CONNECT **no pasaba absolutamente nada**: sin error, sin aviso, sin cambio en pantalla. | Android 12+ separa `BLUETOOTH_SCAN` y `BLUETOOTH_CONNECT`. Con sólo el de escanear, conectar falla **en silencio**. | ✅ Concediendo «Dispositivos cercanos» a la app. Se descartó antes que fuera del firmware conectando desde el PC con `bluetoothctl`. |
| I5 | `Value: Notifications and indications disabled` y no llegaba nada al pulsar el botón. | El icono de notify es un **interruptor**: un toque de más las apaga. | ✅ Fiarse del texto (`Notifications enabled`), no del icono. |
| I6 | Cuatro comandos pegados de golpe → `comando no reconocido`, con el texto cortado. | Llegaron **en una sola línea**, sin Enter entre ellos, y se truncaron a los 128 caracteres del buffer. | ✅ Un comando por línea. |
| I7 | `pio test -e native` no arranca: `sh: gcc: orden no encontrada`. | No hay compilador de C/C++ en el sistema. | ✅ Resuelto: ya hay `gcc-c++`. `pio test -e native` pasa **16/16** casos de `lib/nmea`. |
| I8 | La MAC que sale al flashear (`...31:D8`) no coincide con la que ve el móvil (`...31:DA`). | No es un fallo: el ESP32 usa la **MAC base +2** para BLE. | ✅ Documentado en `INTEGRACION-APP.md`. |
| I9 | `pio device monitor` falla desde Claude Code con `termios.error: Inappropriate ioctl for device`. | Necesita una TTY real. | ✅ Se lee el puerto con un script de pyserial. En terminal normal funciona. |
| I10 | **Al abrir el monitor la placa suelta sola una cascada de transiciones de la FSM y un `ALERT:2` fantasma** (pasó delante del técnico del laboratorio). | El circuito de auto-reset lleva **DTR → GPIO0** y **RTS → EN**. El terminal activa DTR al abrir el puerto y eso pone GPIO0 a BAJO: el firmware lo lee como el botón BOOT mantenido, y a los 3 s dispara `ALERT:2`. Comprobado con las 4 combinaciones DTR/RTS: sólo falla DTR=1, RTS=0. | ✅ `monitor_dtr = 0` y `monitor_rts = 0` en `platformio.ini`. Verificado: el arranque sale limpio y no aparece ninguna transición sola. |
| I11 | Soltar el botón **entre 2,95 s y 3,00 s** emitía `ALERT:2` (emergencia grave) en vez de `ALERT:1`. | La rama de pulsación larga de `fsmUpdate()` no comprobaba si el botón seguía pulsado, así que se adelantaba al antirrebote del flanco de subida, que tarda `DEBOUNCE_MS` en confirmar el soltado. | ✅ Corregido: la rama exige `raw == LOW`. Verificado con pulsaciones por software de 2900/3010/3030/3060 ms. |
| I13 | **Una pulsación normal se PERDÍA si venía justo después de un rebote**: el botón parecía muerto hasta soltarlo del todo y volver a pulsar. | Al volver de `DEBOUNCE` a `IDLE` por rebote, el nivel previo se quedaba en «pulsado» (lo había dejado así la detección del flanco). Cuando el contacto se asentaba de verdad ya no había transición suelto→pulsado que detectar. Nunca salió en las pruebas a mano porque el caso 1.7 se probó con un pulso limpio de 20 ms, sin lo que viene detrás. | ✅ Corregido: ahora se resincroniza el nivel **siempre** que se entra en `IDLE` (`lib/panic/panic.cpp`, `enterIdle`). Cubierto por `test_i13_pulsacion_tras_rebote_no_se_pierde`, que **falla** si se quita el arreglo. |
| I14 | **Las alertas dejaban de salir por BLE si un segundo móvil se desconectaba**, aunque el primero siguiera conectado. El log decía «sin cliente BLE». | `g_bleConnected` era un `bool` que **cualquier** `onDisconnect` ponía a `false`, y NimBLE admite hasta 3 clientes a la vez. En un botón de pánico es el fallo más grave posible: la alerta se registra en el serie y nadie la recibe. | ✅ Corregido: se le pregunta al servidor con `getConnectedCount()` en vez de llevar un booleano propio (`src/main.cpp` §6). |
| I15 | Dos comandos BLE seguidos y rápidos: el primero se perdía sin dejar rastro. | La cola RX tenía **un solo hueco**; el segundo `WRITE` pisaba al primero antes de que `loop()` lo leyera. | ✅ Corregido: anillo de 4 huecos y aviso explícito en el log si aun así se llena. |
| I12 | **SIN RESOLVER.** Ciertos binarios se quedan colgados dentro de `NimBLEDevice::init()` y la placa entra en bucle de reinicio (`rst:0x8 TG1WDT_SYS_RESET`), **sin ningún mensaje de panic**. | Desconocida. Es determinista por binario y depende del *layout* del código: el mismo fuente con una línea de más arranca. Descartados: NVS corrupta (falla igual tras `pio run -t erase`), desbordamiento de pila de `loopTask` (sólo usa 1952 B de 8192) y la versión de NimBLE (sin cambios desde el 8-sep). **Acotado el 10-sep:** el punto exacto es la espera `while(!m_synced) taskYIELD();` del final de `NimBLEDevice::init()` (`NimBLEDevice.cpp:910`), que gira esperando un `sync` del controlador BT que nunca llega. Se descartó también que el compilador estuviera sacando la lectura de `m_synced` fuera del bucle: `taskYIELD()` es una llamada real a `vPortYield()`, así que la variable se relee en cada vuelta. | ⚠️ **Acotada y contenida, no resuelta.** El 22-sep se añadió el **arranque a prueba de fallos**: un contador en memoria RTC cuenta los arranques que entran en `init()` y no salen; al cuarto, el firmware arranca **sin BLE** y el botón de pánico, el LED, el zumbador y el GPS siguen funcionando (se rearma con `BLE:RETRY`). Eso quita lo grave —la placa en bucle con el botón muerto— pero **no arregla la causa**. Antes ya se habían añadido dos marcadores en el log (`entrando en NimBLEDevice::init()` / `init() completado`) para reconocerla de un vistazo. **Ojo:** los cambios del 10-sep mueven el *layout*, así que hay que volver a comprobar **10 arranques seguidos** al flashear. |

> Recordatorio: sólo un proceso puede tener abierto `/dev/ttyUSB0`. Si dos lo
> leen a la vez se reparten los bytes y ninguno ve la salida entera.
> Comprobar con `fuser -v /dev/ttyUSB0`.

---

## Qué falta por probar

**De la sesión del 22-sep (todo sin placa):**

- **§4-bis entera**: botón externo, zumbador, batería y el arranque a prueba de
  fallos de la I12.
- **§5 entera**: el enlace LoRa, en cuanto haya dos Heltec V3.
- Comprobar que el entorno `devkit_v1` de siempre **sigue igual** tras los
  cambios: mismos textos de log, mismos tiempos, mismo comportamiento del LED.
  Los pines por defecto no han cambiado, pero eso hay que verlo, no suponerlo.

**De antes:**

- **Reflashear y revalidar todo lo del 10-sep** (§0-bis dice qué está cubierto
  por pruebas automáticas y qué no). En especial **10 arranques seguidos** por
  la incidencia I12.
- **2.2** prioridad de la baliza sobre el patrón de alerta (hay que **mirar el LED**, no se puede
  comprobar desde el log).
- **I14** en la placa: conectar **dos** móviles, desconectar uno y comprobar que
  el que queda sigue recibiendo `ALERT:1`.
- **I12**: encontrar la causa real del cuelgue en `NimBLEDevice::init()`.
- **3.6 / 3.7** recibir `ALERT:2` y `CANCEL` **por BLE** (por serie ya están).
- **3.8 / 3.9** `BEACON:ON` / `BEACON:OFF` desde el móvil, dejándolo en el log.

---

## Revisión cruzada del 22-sep (segunda pasada sobre el código nuevo)

Antes de dar la sesión por buena se hizo una revisión completa del firmware
nuevo buscando fallos de corrección, no de estilo. Salieron **ocho** que
merecían arreglo; ninguno se habría visto sin las placas delante, y tres de
ellos dejaban el aparato sin hacer justo lo que tiene que hacer:

| # | Qué pasaba | Arreglo |
|---|---|---|
| R1 | **Las Heltec se habrían quedado sin monitor serie.** `platformio.ini` traía `ARDUINO_USB_CDC_ON_BOOT=1` dando por hecho que la V3 usa el USB nativo del S3, pero su USB-C va a un **CP2102**: `Serial` habría salido por un puerto no cableado. Sin banner, sin trazas y sin comandos, o sea sin demo y sin poder depurar nada. | Fuera los flags `ARDUINO_USB_*`; el puerto es `/dev/ttyUSB0`. |
| R2 | **Con la baliza encendida, el botón de pánico era MUDO**: la baliza tenía más prioridad que la alerta, así que el usuario no tenía confirmación de que su alerta había salido. | Manda la alerta (`Prio::ALERT` es ahora la más alta) y la baliza pasa a ser **patrón de fondo**: la alerta la interrumpe y la baliza vuelve sola al terminar. |
| R3 | **Tras cambiar la pila del buscador, sus primeras órdenes se perdían** sin aviso: su contador volvía a cero y el llavero las tiraba por repetidas. | El contador se guarda también en la flash (NVS) reservando bloques de 256, y la trama que dispara la resincronización ya se atiende en vez de descartarse. |
| R4 | **"SIN SEÑAL" cada dos por tres**: el plazo de silencio (20 s) era menor que dos balizas (2 × 15 s), así que perder una sola trama bastaba para apagar el caliente/frío. | El plazo se ata a la cadencia de la baliza (× 3) y hay una prueba que cruza los dos valores. |
| R5 | La antirrepetición se caía sola pasadas ~32768 tramas: a partir de ahí las grabaciones viejas volvían a parecer nuevas. | Tope de salto hacia adelante (`MAX_JUMP = 4096`), con dos pruebas nuevas. |
| R6 | El llavero daba por bueno **cualquier** ACK, aunque confirmara un envío anterior. | Se comprueba el eco del `seq` y el código; si no casa, se dice en el log. |
| R7 | El aviso sonoro de "BLE desactivado" no llegaba a oírse: la melodía de arranque, de la misma prioridad, sonaba justo después y lo tapaba. | Si el BLE arranca desactivado, no suena la melodía normal. |
| R8 | En la Heltec, un `-D PIN_BUZZER_CFG=25` copiado del firmware de la DevKit compilaba y no sonaba nunca: **en el ESP32-S3 no existe el GPIO25**. | `#error` para los GPIO 22-25 y 26-32, y la documentación corregida (GPIO6/7). |

Además se endureció la radio: la bandera de la interrupción se lee y se borra
de golpe, el rescate por tiempo no deja avisos fantasma, y si `startReceive()`
falla se dice en el log y se reintenta cada 2 s en vez de quedarse sorda
creyendo que escucha.

> Lo que la revisión **no** encontró: ninguna llamada inexistente o con otra
> firma en RadioLib 6.x, NimBLE 1.4.x ni arduino-esp32 2.0.17; ningún
> desbordamiento de buffer; ningún problema con el desbordamiento de
> `millis()`. Eso no quiere decir que no los haya: quiere decir que hay que
> probarlo en placa igual.
