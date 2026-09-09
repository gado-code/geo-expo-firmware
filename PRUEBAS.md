# PRUEBAS · GEO-EXPO ALERT (firmware etapa 1)

Placa: ESP32 DevKit V1 · Firmware: `src/main.cpp` · Fecha de la sesión de prueba: __________

Marca cada casilla: `[x]` OK · `[!]` falla (describe en "Observaciones").
Monitor serie a **115200 bps**. App móvil: **nRF Connect for Mobile**.

---

## 0. Puesta en marcha

| # | Caso | Procedimiento | Resultado esperado | OK | Observaciones |
|---|---|---|---|:--:|---|
| 0.1 | Entorno Fedora | `id \| grep dialout` tras logout/login | El usuario está en `dialout` | ☐ | |
| 0.2 | Puerto serie | Conectar placa, `pio device list` | Aparece `/dev/ttyUSB0` (o `ACM0`) | ☐ | |
| 0.3 | Compila | `pio run -e devkit_v1` | `[SUCCESS]` | ☐ | |
| 0.4 | Flashea | `pio run -e devkit_v1 -t upload` | `[SUCCESS]`, la placa reinicia | ☐ | |
| 0.5 | Banner | Abrir monitor | Se imprime el banner + `Estado inicial: IDLE` | ☐ | |

---

## 1. Máquina de estados del botón (LED + serie, sin BLE)

| # | Caso | Procedimiento | Resultado esperado (serie) | Resultado esperado (LED) | OK | Observaciones |
|---|---|---|---|---|:--:|---|
| 1.1 | Pulsación corta | Pulsar BOOT y soltar en < 1 s | `IDLE→DEBOUNCE→PRESSED`, `>>> TX ALERT:1`, `PRESSED→CANCEL_WINDOW` | 1 parpadeo largo (~0,8 s), luego respiración lenta | ☐ | |
| 1.2 | Ventana expira (alerta confirmada) | Tras 1.1, esperar 10 s sin tocar | `CANCEL_WINDOW→IDLE (ventana expirada: alerta CONFIRMADA)` | Respiración durante 10 s, luego apagado | ☐ | |
| 1.3 | Pulsación prolongada | Mantener BOOT pulsado ≥ 3 s | A los **3 s exactos y sin soltar**: `>>> TX ALERT:2`, `PRESSED→CANCEL_WINDOW (ALERT:2 ...)` | 2 parpadeos, luego respiración | ☐ | |
| 1.4 | Soltar tras pulsación larga | Soltar BOOT después de 1.3 | `CANCEL_WINDOW: botón liberado -> cancelación ARMADA` | Sigue la respiración | ☐ | |
| 1.5 | Cancelar (desde corta) | Hacer 1.1 y, antes de 10 s, pulsar BOOT otra vez | `>>> TX CANCEL`, `CANCEL_WINDOW→IDLE (CANCEL ...)` | 3 parpadeos rápidos, luego apagado | ☐ | |
| 1.6 | Cancelar (desde larga) | Hacer 1.3, soltar, y antes de 10 s pulsar BOOT | `>>> TX CANCEL`, vuelta a `IDLE` | 3 parpadeos rápidos, apagado | ☐ | |
| 1.7 | Antirrebote flanco de bajada | Rozar BOOT con contacto muy breve (< 50 ms) | `DEBOUNCE→IDLE (rebote/ruido...)`, **sin** `ALERT` | Sin cambios visibles | ☐ | |
| 1.8 | Antirrebote flanco de subida | Pulsación corta normal | **Un solo** `ALERT:1` (no varios por rebote al soltar) | 1 parpadeo largo | ☐ | |
| 1.9 | Umbral just-in-time | Soltar BOOT ~2,8 s (justo antes de 3 s) | Se emite `ALERT:1`, **no** `ALERT:2` | 1 parpadeo largo | ☐ | |
| 1.10 | Reingreso tras confirmar | Tras 1.2, pulsar BOOT de nuevo | Nuevo ciclo `ALERT:1` normal | Igual que 1.1 | ☐ | |

---

## 2. Baliza simulada por comando serie (sin móvil)

| # | Caso | Procedimiento | Resultado esperado | OK | Observaciones |
|---|---|---|---|:--:|---|
| 2.1 | `BEACON:ON` | Escribir `BEACON:ON` + Enter en el monitor | `RX BEACON:ON -> baliza ACTIVADA`; LED parpadea a **2 Hz** (250 ms on / 250 ms off) | ☐ | |
| 2.2 | Prioridad de la baliza | Con baliza ON, pulsar BOOT | Llega `>>> TX ALERT:1` por serie, pero el LED **sigue** a 2 Hz | ☐ | |
| 2.3 | `BEACON:OFF` | Escribir `BEACON:OFF` + Enter | `baliza DESACTIVADA`; el LED vuelve a reposo o respiración según el estado | ☐ | |
| 2.4 | Comando inválido | Escribir `HOLA` + Enter | `RX comando no reconocido: "HOLA"` | ☐ | |
| 2.5 | Ayuda | Escribir `?` + Enter | Se imprime la lista de comandos | ☐ | |

---

## 3. BLE con nRF Connect

| # | Caso | Procedimiento | Resultado esperado | OK | Observaciones |
|---|---|---|---|:--:|---|
| 3.1 | Visible en escaneo | nRF Connect → Scan | Aparece **`GEOEXPO-ALERT`** | ☐ | |
| 3.2 | Conexión | CONNECT en nRF Connect | Serie: `BLE: cliente CONECTADO` | ☐ | |
| 3.3 | Servicio y características | Desplegar servicio `6E400001-...` | `6E400003-...` (Notify) y `6E400002-...` (Write) | ☐ | |
| 3.4 | Suscripción a TX | Activar *notifications* en `6E400003-...` | Sin error; icono de suscripción activo | ☐ | |
| 3.5 | Recibir `ALERT:1` | Pulsación corta de BOOT | En `6E400003-...` llega `ALERT:1\n` (ver en UTF-8) | ☐ | |
| 3.6 | Recibir `ALERT:2` | Pulsación prolongada de BOOT | Llega `ALERT:2\n` a los 3 s | ☐ | |
| 3.7 | Recibir `CANCEL` | Cancelar dentro de la ventana | Llega `CANCEL\n` | ☐ | |
| 3.8 | Enviar `BEACON:ON` | Write UTF-8 `BEACON:ON` en `6E400002-...` | Serie: `<<< RX (BLE) "BEACON:ON"`; LED a 2 Hz | ☐ | |
| 3.9 | Enviar `BEACON:OFF` | Write UTF-8 `BEACON:OFF` | LED deja de parpadear | ☐ | |
| 3.10 | Desconexión y re-advertising | DISCONNECT en nRF Connect | Serie: `BLE: cliente DESCONECTADO -> se reanuda el advertising`; vuelve a ser visible en Scan | ☐ | |
| 3.11 | Funciona sin cliente | Desconectado, pulsar BOOT | Serie: `... registrado solo en serie`; la FSM avanza igual | ☐ | |
| 3.12 | Reconexión | CONNECT otra vez, repetir 3.5 | Todo vuelve a funcionar | ☐ | |

---

## 3.bis. Módulo GPS sin receptor físico

Las tramas se inyectan por el monitor serie (o por BLE con un WRITE en RX: es
el mismo manejador). Copia las tramas **enteras**, con `$` y `*HH`.

| # | Qué escribes | Qué debe salir | OK |
|---|---|---|:--:|
| 3b.1 | `POS` (recién arrancada) | `GPS  tramas OK=0  descartadas=0` y `sin posicion valida` | ☐ |
| 3b.2 | `NMEA $GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47` | `trama ACEPTADA` · `lat=48.117300  lon=11.516667  sats=8` | ☐ |
| 3b.3 | `POS` otra vez | La misma posición, con la antigüedad del fix creciendo | ☐ |
| 3b.4 | `NMEA $GNRMC,041515.00,A,0412.3456,N,07405.6789,W,0.06,,090926,,,A*4F` | `lat=4.205760  lon=-74.094648` (longitud **negativa**: oeste) | ☐ |
| 3b.5 | La trama de 3b.2 cambiando `*47` por `*40` | `trama DESCARTADA` · `descartadas=1` · la posición anterior **no cambia** | ☐ |
| 3b.6 | `NMEA $GPGGA,123519,,,,,0,00,,,M,,M,,*6B` | Trama aceptada (`OK` sube) pero `sin posicion valida`: calidad 0 | ☐ |
| 3b.7 | `pio test -e devkit_v1` | 16 pruebas, todas `PASSED` | ☐ |

> 3b.5 es la prueba importante: un checksum malo **nunca** debe corromper la
> última posición buena. Es lo que separa una posición fiable de una inventada.

---

## 4. Verificación final (la checklist del enunciado)

| # | Requisito | Casos que lo cubren | OK |
|---|---|---|:--:|
| V1 | El LED responde al botón BOOT según lo especificado | 1.1–1.10 | ☐ |
| V2 | El dispositivo aparece como `GEOEXPO-ALERT` al escanear con nRF Connect | 3.1 | ☐ |
| V3 | Al pulsar llegan los mensajes correctos a nRF Connect | 3.5, 3.6, 3.7 | ☐ |
| V4 | Al escribir `BEACON:ON` desde nRF Connect el LED empieza a parpadear | 3.8 | ☐ |
| V5 | El firmware sabe convertir tramas NMEA en una posición | 3b.2–3b.7 | ☐ |

---

## Registro de incidencias

| Caso | Qué vi exactamente (texto del monitor + LED) | Hipótesis | Estado |
|---|---|---|---|
| | | | |
| | | | |
| | | | |
