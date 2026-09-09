# PRUEBAS · GEO-EXPO ALERT (firmware etapa 1)

Placa: ESP32 DevKit V1 · Firmware: `src/main.cpp` · Fecha de la sesión de prueba: **9-sep-2026**

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

## 1. Máquina de estados del botón (LED + serie, sin BLE)

| # | Caso | Procedimiento | Resultado esperado (serie) | Resultado esperado (LED) | OK | Observaciones |
|---|---|---|---|---|:--:|---|
| 1.1 | Pulsación corta | Pulsar BOOT y soltar en < 1 s | `IDLE→DEBOUNCE→PRESSED`, `>>> TX ALERT:1`, `PRESSED→CANCEL_WINDOW` | 1 parpadeo largo (~0,8 s), luego respiración lenta | **x** | 4 veces, todas limpias (t=261458, 274018, 286711, 344839, 436866 ms). |
| 1.2 | Ventana expira (alerta confirmada) | Tras 1.1, esperar 10 s sin tocar | `CANCEL_WINDOW→IDLE (ventana expirada: alerta CONFIRMADA)` | Respiración durante 10 s, luego apagado | **x** | **10000 ms exactos** en las 5 alertas medidas. |
| 1.3 | Pulsación prolongada | Mantener BOOT pulsado ≥ 3 s | A los **3 s exactos y sin soltar**: `>>> TX ALERT:2`, `PRESSED→CANCEL_WINDOW (ALERT:2 ...)` | 2 parpadeos, luego respiración | **x** | `ALERT:2` a los **3000 ms clavados** (PRESSED 481993 → TX 484993). |
| 1.4 | Soltar tras pulsación larga | Soltar BOOT después de 1.3 | `CANCEL_WINDOW: botón liberado -> cancelación ARMADA` | Sigue la respiración | **x** | `cancelación ARMADA` a los 466 ms de soltar. |
| 1.5 | Cancelar (desde corta) | Hacer 1.1 y, antes de 10 s, pulsar BOOT otra vez | `>>> TX CANCEL`, `CANCEL_WINDOW→IDLE (CANCEL ...)` | 3 parpadeos rápidos, luego apagado | **x** | **El fallo del re-disparo queda corregido**: tras el `CANCEL` (t=350176) no hay nada hasta la siguiente pulsación 86 s después. |
| 1.6 | Cancelar (desde larga) | Hacer 1.3, soltar, y antes de 10 s pulsar BOOT | `>>> TX CANCEL`, vuelta a `IDLE` | 3 parpadeos rápidos, apagado | ☐ | Sin probar: el `CANCEL` registrado venía de una pulsación corta. |
| 1.7 | Antirrebote flanco de bajada | Rozar BOOT con contacto muy breve (< 50 ms) | `DEBOUNCE→IDLE (rebote/ruido...)`, **sin** `ALERT` | Sin cambios visibles | ☐ | Sin probar en esta sesión. |
| 1.8 | Antirrebote flanco de subida | Pulsación corta normal | **Un solo** `ALERT:1` (no varios por rebote al soltar) | 1 parpadeo largo | **x** | Un solo `ALERT:1` por pulsación en las 5 registradas. |
| 1.9 | Umbral just-in-time | Soltar BOOT ~2,8 s (justo antes de 3 s) | Se emite `ALERT:1`, **no** `ALERT:2` | 1 parpadeo largo | ☐ | Sin probar. |
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
| I7 | `pio test -e native` no arranca: `sh: gcc: orden no encontrada`. | No hay compilador de C/C++ en el sistema. | ⏳ Pendiente: `sudo dnf install gcc-c++`. Mientras tanto las pruebas corren en la placa. |
| I8 | La MAC que sale al flashear (`...31:D8`) no coincide con la que ve el móvil (`...31:DA`). | No es un fallo: el ESP32 usa la **MAC base +2** para BLE. | ✅ Documentado en `INTEGRACION-APP.md`. |
| I9 | `pio device monitor` falla desde Claude Code con `termios.error: Inappropriate ioctl for device`. | Necesita una TTY real. | ✅ Se lee el puerto con un script de pyserial. En terminal normal funciona. |

> Recordatorio: sólo un proceso puede tener abierto `/dev/ttyUSB0`. Si dos lo
> leen a la vez se reparten los bytes y ninguno ve la salida entera.
> Comprobar con `fuser -v /dev/ttyUSB0`.

---

## Qué falta por probar

- **1.6** cancelar desde una pulsación **larga** (el `CANCEL` registrado vino de una corta).
- **1.7** antirrebote del flanco de bajada (roce muy breve del botón).
- **1.9** soltar a ~2,8 s: debe salir `ALERT:1`, **no** `ALERT:2`.
- **2.2** prioridad de la baliza sobre el patrón de alerta.
- **3.6 / 3.7** recibir `ALERT:2` y `CANCEL` **por BLE** (por serie ya están).
- **3.8 / 3.9** `BEACON:ON` / `BEACON:OFF` desde el móvil, dejándolo en el log.
