# PRUEBAS · GEO-EXPO ALERT (firmware etapa 1)

Placa: ESP32 DevKit V1 · Firmware: `src/main.cpp` · Última sesión con la placa: **18-sep-2026** · Última revisión del código: **19-sep-2026**

> ✅ **Los cambios del 10-sep ya están probados en la placa.** Se revalidaron
> el 18-sep desde un PC con Windows: resultados en la **§0-ter**, incluidos los
> 10 arranques seguidos que pedía la incidencia I12. La tabla de las §1 y §2
> conserva las observaciones del 9-sep; lo medido el 18-sep está en la §0-ter
> para no borrar el histórico.
>
> ✅ **22-sep: el botón físico está probado.** Pulsador cableado en GPIO4 y
> GND, firmware flasheado con `-D BOOT_BUTTON_ENABLED=0`: la §3-ter está
> entera en verde, incluidos el antirrebote con un pulsador mecánico de verdad
> y la I13 en hardware. Lo único que sigue sin comprobarse a ojo es la **2.2**
> (mirar el LED) y toda la §3 de BLE, que necesita el móvil.

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

Resultado del 10-sep: **34 de 34 pasan**.

Las pruebas de `test_panic` simulan el botón **milisegundo a milisegundo**, así
que cubren cosas que a mano son casi imposibles de reproducir: soltar a 2999 ms
exactos, un rebote de 1 ms, o lo que pasa a los 49,7 días cuando `millis()`
vuelve a cero. Los casos 1.6, 1.7, 1.9 y 1.10 de la tabla de abajo tienen ahí
su equivalente automático, así que una regresión sale a la luz sin tener que
volver a pulsar el botón con un cronómetro.

---

## 0-ter. Revalidación en la placa — 18-sep-2026 (desde un PC con Windows)

Por fin se probó en el ESP32 todo lo que el 10-sep se había hecho a ciegas.
Tres diferencias de entorno respecto a las sesiones anteriores:

- El puerto ya no es `/dev/ttyUSB0` sino **`COM3`**. Windows dejó el CP2102 en
  **código 28** («no hay driver») hasta instalar a mano el *CP210x Universal
  Windows Driver* de Silicon Labs.
- `pio device monitor` sigue sin funcionar desde Claude Code (incidencia I9),
  así que se usó **`tools/hw.py`**, un arnés de pyserial que además **pulsa
  BOOT por software** (DTR → GPIO0, el mismo truco del caso 1.6) y **resetea
  por RTS** (→ EN). Gracias a eso los tiempos se miden al milisegundo en vez
  de a ojo con un cronómetro.
- Los tres cables micro-USB de repuesto volvieron a fallar: son de **solo
  carga** (incidencia I1). El LED rojo encendía pero Windows no enumeraba
  absolutamente nada, ni un dispositivo con error.

Binario probado: commit `4f4b050`, RAM **11,2 %**, Flash **45,9 %**.

| # | Caso | Resultado medido | OK |
|---|---|---|:--:|
| 0.2 | Puerto serie | `COM3`, `Silicon Labs CP210x`, tras instalar el driver | **x** |
| 0.4 | Flashea | `[SUCCESS]` a 115200. Misma unidad: MAC `3c:8a:1f:a7:31:d8`, ESP32-D0WD-V3 rev 3.1 | **x** |
| 0.5 | Banner | Completo, con `Estado inicial: IDLE` a los **418 ms** | **x** |
| **I12** | **10 arranques seguidos** | **10 de 10 limpios**: `init() completado` y `IDLE` en los diez, `rst:0x1 (POWERON_RESET)` siempre, ni un `TG1WDT_SYS_RESET` | **x** |
| 1.1 | Pulsación corta | `ALERT:1` a t=34655 ms | **x** |
| 1.2 | La ventana expira | **10 000 ms exactos** (34655 → 44655) | **x** |
| 1.3 | Pulsación prolongada | `ALERT:2` a los **3000 ms exactos** de `PRESSED` (56965 → 59965) | **x** |
| 1.4 | Soltar tras la larga | `cancelación ARMADA` ~500 ms después de soltar | **x** |
| 1.5 | Cancelar desde corta | `CANCEL` y **ningún** `ALERT:1` fantasma detrás | **x** |
| 1.6 | Cancelar desde larga | `ALERT:2` → `ARMADA` al soltar → `CANCEL` | **x** |
| 1.7 | Antirrebote de bajada | Pulso de 20 ms → `DEBOUNCE → IDLE (rebote/ruido)`, sin alerta | **x** |
| 1.8 | Antirrebote de subida | Un solo `ALERT:1` por pulsación | **x** |
| 1.9 | Umbral just-in-time | 2800 ms → `ALERT:1`; **2970 ms → `ALERT:1`** (dentro de la franja del fallo I11); 3100 ms → `ALERT:2` | **x** |
| 1.10 | Reingreso tras confirmar | Dos ciclos completos encadenados, sin residuos de estado | **x** |
| **I13** | Pulsación justo tras un rebote | Rebote de 20 ms + pulsación de 400 ms → `ALERT:1`. **Primera vez comprobado en hardware**: la pulsación ya no se pierde | **x** |
| 2.1 | `BEACON:ON` | `baliza ACTIVADA` | **x** |
| 2.2 | Prioridad de la baliza | Con la baliza activa, `ALERT:1` sale igual por serie | *?* |
| 2.3 | `BEACON:OFF` | `baliza DESACTIVADA` | **x** |
| 2.4 | Comando inválido | `HOLA` → `comando no reconocido` | **x** |
| 2.5 | Ayuda | `?` imprime la ayuda | **x** |
| — | Comandos en minúsculas | `beacon:on` se acepta igual que `BEACON:ON` | **x** |

> La **2.2 queda a medias a propósito**: el log está comprobado, pero lo que
> hay que juzgar es **el LED**, y eso no aparece en ningún registro. Falta que
> una persona mire la placa y confirme que sigue a 2 Hz mientras se pulsa.
>
> Las tildes salían corruptas en la consola de Windows, pero **no es un fallo
> del firmware**: leyendo los bytes crudos del puerto, la placa manda UTF-8
> correcto (`bot\xc3\xb3n`). Era la consola, no el ESP32.

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
| 2.2 | Prioridad de la baliza | Con baliza ON, pulsar el botón | Llega `>>> TX ALERT:1`, pero el LED **sigue** a 2 Hz | *?* | 22-sep: la parte del log está comprobada (la alerta sale igual con la baliza activa). Se repitió de forma controlada, encendiendo la baliza desde el puerto y separando 14 s la pulsación; el usuario lo dio por bueno pero **sin una observación firme del LED**. El código respalda la prioridad: `led::update()` sale por la rama de la baliza antes de mirar el patrón. |
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
| 3.6 | Recibir `ALERT:2` | Pulsación prolongada | Llega `ALERT:2\n` a los 3 s | **x** | 22-sep, **por primera vez por BLE**, con el pulsador externo: `ALERT:2` a los 3000 ms clavados y visto en el iPhone. |
| 3.7 | Recibir `CANCEL` | Cancelar dentro de la ventana | Llega `CANCEL\n` | **x** | 22-sep, **por primera vez por BLE**: tres `CANCEL` enviados y vistos en el móvil. |
| 3.8 | Enviar `BEACON:ON` | Write UTF-8 `BEACON:ON` en `6E400002-...` | Serie: `<<< RX (BLE) "BEACON:ON"`; LED a 2 Hz | **x** | 22-sep **con log**: `<<< RX (BLE) "BEACON:ON"` → `baliza ACTIVADA`. Ya no es de palabra. |
| 3.9 | Enviar `BEACON:OFF` | Write UTF-8 `BEACON:OFF` | LED deja de parpadear | **x** | 22-sep con log. Además aceptó **`BEACON:off` en minúsculas**, escrito tal cual desde el móvil. |
| 3.10 | Desconexión y re-advertising | DISCONNECT en nRF Connect | Serie: `BLE: cliente DESCONECTADO -> se reanuda el advertising`; vuelve a ser visible en Scan | **x** | Tras desconectar el PC, el móvil volvió a encontrarlo sin resetear la placa. |
| 3.11 | Funciona sin cliente | Desconectado, pulsar BOOT | Serie: `... registrado solo en serie`; la FSM avanza igual | **x** | Todo el log previo al móvil: `registrado solo en serie`, con la FSM avanzando igual. |
| 3.12 | Reconexión | CONNECT otra vez, repetir 3.5 | Todo vuelve a funcionar | **x** | PC → desconexión → móvil, sin tocar la placa. |

---

## 3-quater. Sesión BLE del 22-sep (iPhone + pulsador externo)

Primera sesión BLE con el **botón físico** y con el firmware del 19-sep. El
móvil es un **iPhone**, así que dos avisos de la tabla de arriba no aplican:
iOS **no muestra la MAC** (enseña un UUID propio, así que lo de la dirección
`:DA` es cosa de Android) y **no pide el permiso de «Dispositivos cercanos»**
de la incidencia I4, solo el de Bluetooth.

| # | Caso | Resultado medido | OK |
|---|---|---|:--:|
| 3.1 / 3.2 | Escaneo y conexión | `BLE: cliente CONECTADO (1 en total)` | **x** |
| 3.3 | Servicio y características | Se desplegó `6E400001-...` y se usaron las dos: suscripción en `...0003` y escritura en `...0002` | **x** |
| 3.4 | Suscripción a TX | Tras activarla, el log pasa a `(enviado por BLE)` | **x** |
| 3.5 / 3.6 / 3.7 | `ALERT:1`, `ALERT:2` y `CANCEL` | Los tres vistos en el móvil; `ALERT:2` a los **3000 ms exactos** | **x** |
| 3.8 / 3.9 | `BEACON:ON` / `BEACON:OFF` | Con log esta vez; `BEACON:off` en minúsculas también se aceptó | **x** |
| 3.10 | Desconexión y re-advertising | `cliente DESCONECTADO (quedan 0) -> se reanuda el advertising` | **x** |
| 3.11 | Funciona sin cliente | Pulsación con el móvil desconectado: `(sin cliente BLE: registrado solo en serie)` | **x** |
| 3.12 | Reconexión | Reconectar, suscribirse y pulsar: `ALERT:1 (enviado por BLE)`, sin tocar la placa | **x** |
| 3c.10 | **Log honesto de TX** | Pulsando **antes** de suscribirse: `(cliente conectado pero SIN suscribirse a TX: NO lo recibe)`. Con el firmware viejo decía «enviado por BLE» y la alerta se perdía en silencio | **x** |
| 3c.11 | **I16: ¿sigue visible con un cliente conectado?** | Falta confirmar si `GEOEXPO-ALERT` seguía apareciendo en el escaneo estando conectado | ☐ |

> **Regalo de esta sesión: la I11 quedó verificada con un pulsador mecánico.**
> Buscando una pulsación larga, dos intentos se quedaron cortos y uno soltó a
> **2980 ms**, justo dentro de la franja 2950–3000 ms donde el firmware viejo
> emitía `ALERT:2` en vez de `ALERT:1`. Emitió `ALERT:1`, que es lo correcto.

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

## 3-ter. Botón externo y buzzer — cambios del 19-sep

El pulsador externo ya está soportado y **activo por defecto** en `GPIO4`, con
el BOOT funcionando en paralelo (`pulsado = BOOT || externo`). El buzzer está
implementado pero **apagado** (`-D BUZZER_ENABLED=1` para encenderlo). Detalle
y cableado en el README §2-bis.

| # | Caso | Procedimiento | Resultado esperado | OK | Observaciones |
|---|---|---|---|:--:|---|
| 3c.1 | El pin no dispara nada solo | Placa quieta 60 s | Ni una transición de la FSM | **x** | 60 s sin un solo evento. La placa respondía a `POS` justo después, así que el silencio es real y no un cuelgue. |
| 3c.2 | Banner | Arrancar y leer el banner | Dice `Pulsador externo : GPIO4` y `Buzzer : desactivado` | **x** | Con `-D BOOT_BUTTON_ENABLED=0` sale `Boton BOOT en GPIO0 (DESACTIVADO)` y la ayuda dice que el botón es el externo. |
| 3c.3 | El pulsador externo alerta | Cablear GPIO4 → pulsador → GND y pulsar corto | `ALERT:1` exactamente igual que con BOOT | **x** | 22-sep con el pulsador real: flanco → antirrebote de 50 ms → `ALERT:1`, **uno solo**, y la ventana expiró a los 10 000 ms exactos. | |
| 3c.4 | Pulsación larga por el externo | Mantener el pulsador 3 s | `ALERT:2` a los 3000 ms | **x** | `PRESSED` 94600 → `ALERT:2` 97600 = **3000 ms clavados**, sin ningún `ALERT:1` por el camino (I11). `ARMADA` 316 ms después, al soltar. | |
| 3c.5 | Guarda de pines peligrosos | `PLATFORMIO_BUILD_FLAGS="-D EXT_BUTTON_PIN=5" pio run -e devkit_v1` | **No compila**: `#error ... es un strapping pin (0/2/5/12/15)` | **x** | |
| 3c.6 | BOOT desactivable (I10) | Compilar con `-D BOOT_BUTTON_ENABLED=0` y activar DTR | El DTR ya no genera alertas fantasma | **x** | DTR activo **6 s** (el doble del umbral de 3 s): ni `ALERT:2` ni una sola transición. Antes esto disparaba la alerta fantasma. |
| 3c.7 | Buzzer suena | Con `-D BUZZER_ENABLED=1` y el piezo en GPIO25, mandar `BEACON:ON` | Pita a 2 Hz a la vez que el LED | ☐ | |
| 3c.8 | El buzzer no estropea el LED | Con el buzzer activo, mirar el parpadeo del LED | El LED sigue igual: el buzzer usa el canal LEDC **0** y el LED el **15**, que están en timers distintos | ☐ | |
| 3c.9 | Trama NMEA con minúsculas | Inyectar una trama con minúsculas y checksum correcto | **ACEPTADA** (antes se descartaba: el comando pasaba la trama a mayúsculas y eso cambia el XOR) | ☐ | |
| 3c.10 | Log honesto de TX | Conectar un móvil **sin** suscribirse a TX y pulsar | `(cliente conectado pero SIN suscribirse a TX: NO lo recibe)` en vez del viejo `(enviado por BLE)` | ☐ | |
| 3c.11 | Sigue visible con un cliente (I16) | Con un móvil conectado, escanear desde otro | El llavero **sigue apareciendo**: ahora el advertising se relanza al conectar | ☐ | |
| 3c.12 | 10 arranques con el binario nuevo | `tools/hw.py` con 10 resets | 10 de 10 con `init() completado` | **x** | **Dos tandas de 10 de 10 limpias** el 22-sep (la segunda ya con el pulsador cableado). `rst:0x1` siempre, ningún `TG1WDT`, `init()` entre **424 y 437 ms**. Cero transiciones espontáneas durante los arranques. |
| 3c.13 | **Antirrebote con un pulsador mecánico** (el 1.7, pero con hardware real) | Rozar el pulsador lo más brevemente posible | `DEBOUNCE → IDLE (rebote/ruido)` y **ninguna** alerta | **x** | **6 rebotes reales filtrados**, con contactos de **15, 20, 23, 23, 27 y 35 ms**. Ninguno generó alerta. Hasta hoy el antirrebote solo se había probado con el táctil de BOOT y con pulsaciones por software. |
| 3c.14 | **I13 en hardware real** | Rebote seguido de una pulsación buena | La pulsación siguiente **no** se pierde | **x** | Verificado **4 veces**, incluso con **dos rebotes seguidos** antes de la pulsación buena (t=340918 y 341231 → `ALERT:1` en 341760). Era el fallo que dejaba el botón muerto hasta soltarlo del todo. |
| 3c.15 | Toques rápidos seguidos | Varios toques separados por menos de 10 s | Alternan `ALERT:1` y `CANCEL` | **x** | **Comportamiento por diseño, pero es el riesgo del punto 5c**: toques separados por apenas **300-500 ms** cancelaron la alerta anterior. En un llavero de bolsillo un doble toque sin querer anula la alerta. Pendiente de decidir con el equipo (¿cancelar con pulsación larga?). |

---

## 4. Verificación final (la checklist del enunciado)

| # | Requisito | Casos que lo cubren | OK | Estado |
|---|---|---|:--:|---|
| V1 | El LED responde al botón BOOT según lo especificado | 1.1–1.10 | **x** | La lógica está verificada por serie con marcas de tiempo. Faltan 1.6, 1.7 y 1.9. |
| V2 | El dispositivo aparece como `GEOEXPO-ALERT` al escanear con nRF Connect | 3.1 | **x** | Confirmado desde el móvil y desde el PC. |
| V3 | Al pulsar llegan los mensajes correctos a nRF Connect | 3.5, 3.6, 3.7 | **x** | `ALERT:1` llegó al móvil. `ALERT:2` y `CANCEL` sólo verificados por serie. |
| V4 | Al escribir `BEACON:ON` desde nRF Connect el LED empieza a parpadear | 3.8 | **x** | 22-sep con log, desde un iPhone. |
| V5 | El firmware sabe convertir tramas NMEA en una posición | 3b.2–3b.7 | **x** | 16/16 pruebas automáticas + inyección manual en la placa. |

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
| I12 | **SIN RESOLVER.** Ciertos binarios se quedan colgados dentro de `NimBLEDevice::init()` y la placa entra en bucle de reinicio (`rst:0x8 TG1WDT_SYS_RESET`), **sin ningún mensaje de panic**. | Desconocida. Es determinista por binario y depende del *layout* del código: el mismo fuente con una línea de más arranca. Descartados: NVS corrupta (falla igual tras `pio run -t erase`), desbordamiento de pila de `loopTask` (sólo usa 1952 B de 8192) y la versión de NimBLE (sin cambios desde el 8-sep). **Acotado el 10-sep:** el punto exacto es la espera `while(!m_synced) taskYIELD();` del final de `NimBLEDevice::init()` (`NimBLEDevice.cpp:910`), que gira esperando un `sync` del controlador BT que nunca llega. Se descartó también que el compilador estuviera sacando la lectura de `m_synced` fuera del bucle: `taskYIELD()` es una llamada real a `vPortYield()`, así que la variable se relee en cada vuelta. | ⚠️ Mitigado, no resuelto. Añadidos dos marcadores en el log (`entrando en NimBLEDevice::init()` / `init() completado`) para reconocerla de un vistazo. **Ojo:** los cambios del 10-sep mueven el *layout*, así que hay que volver a comprobar **10 arranques seguidos** al flashear. |

> Recordatorio: sólo un proceso puede tener abierto `/dev/ttyUSB0`. Si dos lo
> leen a la vez se reparten los bytes y ninguno ve la salida entera.
> Comprobar con `fuser -v /dev/ttyUSB0`.

---

## Qué falta por probar

- **Toda la §3-ter**: es lo del 19-sep (botón externo, buzzer, log honesto de
  TX, advertising con cliente conectado) y **no se ha probado en la placa**.
  Lo más importante de ahí son los **10 arranques** del caso 3c.12, porque el
  binario cambió y la I12 depende del layout.
- **2.2** prioridad de la baliza sobre el patrón de alerta (hay que **mirar el LED**, no se puede
  comprobar desde el log).
- **I14** en la placa: conectar **dos** móviles, desconectar uno y comprobar que
  el que queda sigue recibiendo `ALERT:1`.
- **I12**: encontrar la causa real del cuelgue en `NimBLEDevice::init()`.
- **3.6 / 3.7** recibir `ALERT:2` y `CANCEL` **por BLE** (por serie ya están).
- **3.8 / 3.9** `BEACON:ON` / `BEACON:OFF` desde el móvil, dejándolo en el log.
