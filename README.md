# GEO-EXPO ALERT · Firmware etapa 1

Dispositivo de alerta personal para el proyecto de física. Esta etapa corre en
una **ESP32 DevKit V1 (ESP32-WROOM-32)** usando **solo los periféricos
integrados de la placa**: el botón **BOOT (GPIO0)** y el **LED integrado
(GPIO2)**. No hace falta protoboard, buzzer, pulsadores ni batería.

El firmware:

1. Expone un servidor **BLE** con perfil **Nordic UART Service (NUS)** y el
   contrato de interfaz acordado con el equipo de la app.
2. Implementa la **máquina de estados del botón de pánico** con ventana de
   cancelación, **100 % no bloqueante** (todo por `millis()`, nunca `delay()`
   en la lógica).
3. Registra **cada transición de estado por el monitor serie**, haya o no un
   cliente BLE conectado.

Como todavía no hay buzzer, la **baliza se simula** parpadeando el LED de GPIO2
a 2 Hz e imprimiendo en serie.

---

## 0. Punto de continuación — 9-sep-2026

**Estado: etapa 1 verificada en la placa + módulo GPS escrito y probado.**

### Hecho hasta ahora

- Placa detectada en **`/dev/ttyUSB0`**, chip **CP2102** (`10c4:ea60`),
  ESP32-D0WD-V3 rev 3.1, MAC `3c:8a:1f:a7:31:d8`.
- Firmware flasheado y probado en la placa: antirrebote, `ALERT:1`, `ALERT:2`
  a los 3000 ms exactos, `CANCEL` dentro de la ventana, confirmación al
  expirar los 10 s, comandos por serie y BLE anunciándose como `GEOEXPO-ALERT`.
- **Corregido el re-disparo tras `CANCEL`** (`ST_IDLE` disparaba por nivel en
  vez de por flanco, así que cada `CANCEL` generaba un `ALERT:1` fantasma).
- **`platformio.ini`**: `upload_speed` bajado a **115200** (460800 y 921600
  fallan con este cable: `Unable to verify flash chip connection`) y quitado
  el filtro `colorize`, que la pyserial del sistema no conoce.
- **Módulo GPS nuevo** (`lib/nmea/`) con **16 pruebas automáticas que pasan en
  la placa** y comandos `POS` / `NMEA <trama>` para probarlo sin GPS físico.
  Ver la §8.

### Lo único que falta comprobar a mano

⚠️ El arreglo del `CANCEL` está flasheado pero **no probado con el dedo**.
Secuencia: pulsación corta → esperar a `ALERT:1` → dentro de los 10 s pulsar
otra vez **manteniendo el dedo un par de segundos**. Debe salir **solo**
`CANCEL`, sin ningún `ALERT:1` posterior.

Y la §5.C (BLE con nRF Connect desde el móvil) sigue sin hacerse: en todas las
pruebas salió `sin cliente BLE: registrado solo en serie`.

### Decisiones que siguen abiertas

1. **Formato de la posición en los mensajes BLE.** Está implementado pero
   **desactivado** (`ALERT_WITH_POSITION`, `src/main.cpp` §6), porque el
   contrato de la §1 está congelado con el equipo de la app. La propuesta a
   acordar con ellos es `ALERT:1;<lat>;<lon>`. Detalle en la §8.3.
2. **Botón definitivo.** El pin es la constante `PIN_BUTTON` (`src/main.cpp`
   §1), así que sólo hay que cambiar el número. Pero **no uses GPIO0, 2, 12 ni
   15**: son *strapping pins* y pueden impedir el arranque. Libres y seguros:
   GPIO4, 5, 18, 19, 21. El código asume `INPUT_PULLUP` con el botón a masa
   (LOW = pulsado); si se cablea al revés hay que invertir la lógica.
3. **Buzzer.** Llegan 20 buzzers piezo pasivos el 19-sep; hoy la baliza se
   simula con el LED a 2 Hz.

### Notas de entorno

- **No hay compilador de C/C++ en el sistema**, así que `pio test -e native`
  (las pruebas en el PC) no puede correr todavía. Se arregla con:
  ```bash
  sudo dnf install gcc-c++
  ```
  Mientras tanto las mismas pruebas se ejecutan **en la placa**, que es igual
  de válido: `pio test -e devkit_v1`.
- `pio device monitor` **no funciona desde Claude Code**: necesita una TTY real
  y falla con `termios.error: Inappropriate ioctl for device`. Desde ahí hay
  que leer el puerto con un script de pyserial. En una terminal normal va bien.
- Sólo un proceso a la vez debe tener abierto `/dev/ttyUSB0`; si no, se
  reparten los bytes y ambos ven la salida a trozos. Comprobar con
  `fuser -v /dev/ttyUSB0`.

---

## 1. Contrato de interfaz BLE (definitivo)

| Elemento | Valor |
|---|---|
| Nombre del dispositivo | `GEOEXPO-ALERT` |
| Servicio | `6E400001-B5A3-F393-E0A9-E50E24DCCA9E` |
| Característica **TX** (ESP32 → app, `NOTIFY`) | `6E400003-B5A3-F393-E0A9-E50E24DCCA9E` |
| Característica **RX** (app → ESP32, `WRITE`) | `6E400002-B5A3-F393-E0A9-E50E24DCCA9E` |

**Emite por TX** (ASCII, terminado en `\n`):

| Mensaje | Cuándo |
|---|---|
| `ALERT:1` | Pulsación corta |
| `ALERT:2` | Pulsación prolongada (≥ 3 s), en el instante en que se cumplen los 3 s |
| `CANCEL` | Cancelación dentro de la ventana de 10 s |

**Recibe por RX:**

| Mensaje | Efecto |
|---|---|
| `BEACON:ON` | Activa la baliza (LED a 2 Hz) |
| `BEACON:OFF` | La desactiva |

---

## 2. Máquina de estados del botón

Estados: `IDLE → DEBOUNCE → PRESSED → CANCEL_WINDOW → IDLE`.

Parámetros (constantes al inicio de `src/main.cpp`, sin números mágicos):

```c
DEBOUNCE_MS        = 50
LONG_PRESS_MS      = 3000
CANCEL_WINDOW_MS   = 10000
```

| Acción del usuario | Resultado |
|---|---|
| Pulsar y soltar antes de 3 s | `ALERT:1` + ventana de cancelación de 10 s |
| Mantener pulsado 3 s | `ALERT:2` **en el instante de los 3 s** (sin esperar a soltar) + ventana |
| Pulsar de nuevo durante la ventana | `CANCEL` y vuelta a `IDLE` |
| Dejar que la ventana expire | Vuelta a `IDLE`, alerta **confirmada** |

El antirrebote de 50 ms se aplica **al flanco de bajada y al de subida**.

### Realimentación visual del LED

| Situación | LED |
|---|---|
| `ALERT:1` | 1 parpadeo largo (800 ms) |
| `ALERT:2` | 2 parpadeos |
| `CANCEL` | 3 parpadeos rápidos |
| Ventana de cancelación activa | respiración lenta (fade en ~3 s) |
| Baliza (`BEACON:ON`) | parpadeo a 2 Hz — **tiene prioridad sobre lo anterior** |
| Reposo | apagado |

> Mientras la baliza está encendida, el LED muestra el parpadeo de 2 Hz y
> enmascara la realimentación del botón. La FSM sigue funcionando y los
> mensajes BLE/serie se emiten igual. Cuando llegue el buzzer, la baliza pasará
> a él y el LED recuperará la realimentación del botón.

---

## 3. Preparación del entorno (Fedora)

> Estado de **tu** equipo (usuario `dj4do`, Fedora 44) — comprobado el 9-sep:
>
> - ✅ **Ya estás en el grupo `dialout`**, así que el paso 3.1 está hecho.
>   Se deja documentado por si hay que repetirlo en otro equipo.
> - ✅ **Placa conectada en `/dev/ttyUSB0`** (CP2102). El cable de solo carga
>   era el problema; con el cable de datos enumera bien.
> - ✅ PlatformIO instalado (`pio`, Core 6.1.19 del paquete de Fedora).
> - ❌ **Falta el compilador del sistema** si quieres correr las pruebas en el
>   PC: `sudo dnf install gcc-c++` (ver §8.4).

### 3.1. Añadir tu usuario al grupo `dialout`

```bash
sudo usermod -aG dialout "$USER"
```

⚠️ **El cambio de grupo NO aplica hasta que cierres sesión y vuelvas a
entrar** (o reinicies). Un `su - $USER` en la terminal no basta para el acceso
al dispositivo desde una app gráfica; lo más fiable es cerrar sesión.

Comprobar después de volver a entrar:

```bash
id | tr ',' '\n' | grep -q dialout && echo "OK: ya estás en dialout" || echo "FALTA dialout"
```

### 3.2. Reglas udev de PlatformIO (recomendado)

Permiten que las herramientas identifiquen la placa sin privilegios y evitan
tener que pulsar BOOT al flashear:

```bash
curl -fsSL https://raw.githubusercontent.com/platformio/platformio-core/master/platformio/assets/system/99-platformio-udev.rules \
  | sudo tee /etc/udev/rules.d/99-platformio-udev.rules
sudo udevadm control --reload-rules
sudo udevadm trigger
```

(Desconecta y reconecta la placa tras esto.)

### 3.3. Instalar PlatformIO Core

**Opción A — paquete de Fedora (más simple):**

```bash
sudo dnf install platformio
```

Esto da el comando `pio` (y `platformio`) a nivel de sistema.

**Opción B — aislado con pipx (versión más nueva, sin tocar el sistema):**

```bash
sudo dnf install pipx
pipx ensurepath
pipx install platformio
exec $SHELL           # recarga el PATH
```

**Opción C — entorno virtual dedicado (cero privilegios de root):**

```bash
python3 -m venv ~/.pio-venv
~/.pio-venv/bin/pip install -U platformio
# usar siempre:  ~/.pio-venv/bin/pio ...
# (o añadir  export PATH="$HOME/.pio-venv/bin:$PATH"  a ~/.bashrc)
```

Verificar:

```bash
pio --version
```

> **Toolchain:** la primera compilación descarga automáticamente el
> compilador Xtensa y el framework arduino-esp32 (~1 min de descarga la
> primera vez). No hay que instalar nada más con `dnf` para compilar.
>
> Paquetes de sistema que sí conviene tener (suelen venir ya): `python3`,
> `git`. Si algo falla al compilar por falta de headers:
> `sudo dnf install python3 git`.

### 3.4. ¿Qué puerto aparece al conectar la placa?

Enchufa la ESP32 con el cable USB-C de datos y ejecuta:

```bash
pio device list
# o, sin PlatformIO:
ls -l /dev/serial/by-id/  ;  dmesg | tail -20
```

| Chip USB-serie de la placa | Dispositivo que aparece | Driver (ya en el kernel de Fedora) |
|---|---|---|
| **CP2102 / CP2104** (Silicon Labs) | `/dev/ttyUSB0` | `cp210x` |
| **CH340 / CH9102** (WCH) | `/dev/ttyUSB0` | `ch341` / `ch34x` |
| **ESP32-S3 USB nativo** (la Heltec V3) | `/dev/ttyACM0` | `cdc_acm` |

La DevKit V1 casi siempre es CP2102 o CH340 → **`/dev/ttyUSB0`**.
PlatformIO autodetecta el puerto; si tuvieras varios, fija
`upload_port` / `monitor_port` en `platformio.ini`.

Si al abrir el puerto sale `permission denied` y **ya** hiciste logout/login
tras el paso 3.1:

```bash
ls -l /dev/ttyUSB0          # debe figurar grupo "dialout"
id                          # debe listar "dialout"
```

---

## 4. Compilar y flashear

```bash
# Compilar
pio run -e devkit_v1

# Compilar + flashear (autodetecta el puerto)
pio run -e devkit_v1 -t upload

# Monitor serie (115200 bps, con marca de tiempo)
pio device monitor -e devkit_v1

# Todo junto:
pio run -e devkit_v1 -t upload -t monitor
```

> ⚠️ **Velocidad de flasheo.** `upload_speed` está en **115200** a propósito.
> Con el cable actual, 460800 y 921600 fallan con
> `A fatal error occurred: Unable to verify flash chip connection (No serial
> data received.)`. Si algún día cambias de cable puedes probar a subirla; si
> falla, vuelve a 115200 en vez de pelearte con el error.

**Si el flasheo falla con `Failed to connect ... Wrong boot mode detected`:**
mantén pulsado **BOOT**, pulsa y suelta **EN/RST**, suelta **BOOT**, y repite
`-t upload`. Con las reglas udev del paso 3.2 esto no suele hacer falta.

> ⚠️ El botón de la aplicación es **BOOT (GPIO0)**. Si lo tienes pulsado
> mientras la placa arranca/resetea, entra en modo descarga en lugar de
> ejecutar el firmware. Para las pruebas, deja que arranque sola y luego
> pulsa.

Salida esperada del monitor al arrancar:

```
============================================================
  GEO-EXPO ALERT  -  Firmware etapa 1
  Placa            : ESP32 DevKit V1 (ESP32-WROOM-32)
  LED en GPIO2      Boton BOOT en GPIO0
------------------------------------------------------------
  BLE  Nordic UART Service
    Nombre   : GEOEXPO-ALERT
    ...
------------------------------------------------------------
  GPS              : sin modulo (inyeccion manual)   posicion en las alertas: NO (contrato congelado)
------------------------------------------------------------
  DEBOUNCE_MS=50  LONG_PRESS_MS=3000  CANCEL_WINDOW_MS=10000
============================================================
[t=     ... ms] GPS: sin modulo fisico (usa el comando NMEA para inyectar tramas)
[t=     ... ms] BLE: advertising iniciado como "GEOEXPO-ALERT"
[t=     ... ms] Sistema listo. Estado inicial: IDLE
```

---

## 5. Procedimiento de prueba paso a paso

Necesitas: la placa por USB, el monitor serie abierto y un móvil con la app
**nRF Connect for Mobile** (nordic Semiconductor, gratis en Android/iOS).

La tabla de casos con casillas para marcar está en **[`PRUEBAS.md`](PRUEBAS.md)**.

### A. LED + botón BOOT (sin BLE)

1. Abre el monitor: `pio device monitor -e devkit_v1`.
2. **Pulsación corta:** pulsa BOOT y suéltalo rápido (< 1 s).
   - Serie: `FSM IDLE -> DEBOUNCE -> PRESSED`, luego
     `>>> TX  ALERT:1` y `PRESSED -> CANCEL_WINDOW`.
   - LED: 1 parpadeo largo y después "respiración" lenta durante 10 s.
3. Espera 10 s sin tocar nada.
   - Serie: `CANCEL_WINDOW -> IDLE (ventana expirada: alerta CONFIRMADA)`.
   - LED: se apaga.
4. **Pulsación prolongada:** mantén BOOT pulsado y cuenta 3 s.
   - **A los 3 exactos**, sin soltar: `>>> TX  ALERT:2`, 2 parpadeos,
     `PRESSED -> CANCEL_WINDOW`.
   - Suelta: `CANCEL_WINDOW: botón liberado -> cancelación ARMADA`.
5. **Cancelación:** repite el paso 2 y, **antes de 10 s**, vuelve a pulsar BOOT.
   - Serie: `>>> TX  CANCEL`, `CANCEL_WINDOW -> IDLE (CANCEL ...)`.
   - LED: 3 parpadeos rápidos y se apaga.
6. **Rebote:** toca BOOT muy levemente. Si el contacto dura < 50 ms verás
   `DEBOUNCE -> IDLE (rebote/ruido...)` y **no** se emite alerta.

### B. Baliza sin móvil (comando por serie)

En el monitor, escribe y pulsa Enter:

- `BEACON:ON`  → `RX BEACON:ON -> baliza ACTIVADA`; el LED parpadea a 2 Hz.
- `BEACON:OFF` → el LED vuelve a su estado anterior (reposo o respiración).
- `?` → muestra la ayuda.

### C. BLE con nRF Connect

1. **Escaneo:** abre nRF Connect → *Scanner* → *Scan*. Debe aparecer
   **`GEOEXPO-ALERT`**. Pulsa **CONNECT**.
   - Serie: `BLE: cliente CONECTADO`.
2. Despliega el servicio **`6E400001-...`**. Verás dos características:
   - `6E400003-...` con propiedad **Notify**.
   - `6E400002-...` con propiedad **Write**.
3. **Suscríbete a TX:** en `6E400003-...` pulsa el icono de flechas hacia abajo
   (*Enable notifications / subscribe*).
4. **Recibir alertas:** pulsa BOOT en la placa.
   - En `6E400003-...` debe llegar el valor. nRF Connect lo muestra en HEX;
     cambia a **UTF-8** con el icono de la característica para leer
     `ALERT:1` (o `ALERT:2`, `CANCEL`).
5. **Enviar comando:** en `6E400002-...` pulsa la flecha hacia arriba (*Write*),
   elige tipo **Text (UTF-8)**, escribe `BEACON:ON` y envía.
   - Serie: `<<< RX (BLE) "BEACON:ON"` y `baliza ACTIVADA`.
   - El LED empieza a parpadear a 2 Hz.
   - Envía `BEACON:OFF` para apagarla.
6. **Sin conexión:** desconecta en nRF Connect (`BLE: cliente DESCONECTADO ->
   se reanuda el advertising`). Pulsa BOOT: la alerta se sigue registrando en
   serie (`... registrado solo en serie`). Vuelve a conectar cuando quieras.

---

## 6. Verificación (checklist rápida en la placa física)

| # | Qué comprobar | Cómo | OK |
|---|---|---|---|
| 1 | El LED responde al botón BOOT según la especificación | Sección 5.A | ☐ |
| 2 | El dispositivo aparece como `GEOEXPO-ALERT` al escanear | Sección 5.C.1 | ☐ |
| 3 | Al pulsar llegan `ALERT:1` / `ALERT:2` / `CANCEL` a nRF Connect | Sección 5.C.4 | ☐ |
| 4 | Al escribir `BEACON:ON` desde nRF Connect el LED parpadea | Sección 5.C.5 | ☐ |

Si algo no coincide, anota en `PRUEBAS.md` **qué viste exactamente** (texto del
monitor + comportamiento del LED) y lo depuramos.

---

## 7. Migración a Heltec WiFi LoRa 32 V3 (ESP32-S3) — ❌ CANCELADA

> **Esta migración NO se va a hacer.** El pedido de la Heltec se canceló el
> 8-sep, así que **todo el proyecto se queda en la ESP32 DevKit V1** y no hay
> LoRa en ninguna etapa.
>
> El entorno `heltec_v3` de `platformio.ini` y los `#if defined(BOARD_HELTEC_V3)`
> de `src/main.cpp` se conservan porque siguen compilando y no estorban, pero
> son **material muerto**: no los tomes como trabajo pendiente. El resto de
> esta sección queda sólo como referencia, por si algún día se retoma otra
> placa.

Entorno preparado (sin uso): `pio run -e heltec_v3`.

### Qué cambia automáticamente (ya resuelto en el código)

| Punto | Cómo está resuelto |
|---|---|
| **Pines** | `src/main.cpp` §1: bloque `#if defined(BOARD_HELTEC_V3)`. LED `GPIO2 → GPIO35`, botón sigue en `GPIO0`. |
| **Pila BLE** | Se usa **NimBLE-Arduino**, la misma API en ESP32 y ESP32-S3. No hay `#ifdef` de BLE. |
| **PWM del LED** | Se usa `analogWrite()`, portable entre arduino-esp32 2.x y 3.x (no se usa `ledcSetup`/`ledcAttachPin`, que cambian de firma). |
| **USB / puerto serie** | El S3 usa USB nativo (CDC). El entorno `heltec_v3` ya añade `-D ARDUINO_USB_MODE=1 -D ARDUINO_USB_CDC_ON_BOOT=1`. El puerto será **`/dev/ttyACM0`**. |

### Qué habrá que revisar / decidir a mano

1. **Versión de NimBLE + core.** Este proyecto fija
   `platform = espressif32@6.9.0` (arduino-esp32 **2.0.17**) y
   `NimBLE-Arduino @ ^1.4.3`. Si al migrar se sube a arduino-esp32 **3.x**
   (platform 51.x), conviene pasar a **NimBLE-Arduino 2.x**, que cambia
   algunas firmas de callbacks:
   - `onConnect(NimBLEServer*, NimBLEConnInfo&)` (antes `NimBLEServer*` solo).
   - `onWrite(NimBLECharacteristic*, NimBLEConnInfo&)` (antes `NimBLECharacteristic*` solo).
   - `NimBLEDevice::setPower()` recibe `int` en dBm en vez de `esp_power_level_t`.
   Son 3 sitios en `src/main.cpp` (clases `ServerCB`, `RxCB` y `bleBegin`).
2. **LED blanco de la Heltec V3 y `Vext`.** El LED de GPIO35 es directo, pero la
   placa tiene control de alimentación de periféricos (`Vext`, GPIO36) y el
   OLED. No afecta a este firmware, pero tenlo presente al añadir pantalla.
3. **El botón PRG (GPIO0) es también strapping de arranque**, igual que en la
   DevKit: no lo pulses mientras resetea.
4. **Radio LoRa (SX1262).** Este firmware **no** toca LoRa. La mensajería larga
   distancia entre las dos Heltec es trabajo de la siguiente etapa
   (librería `RadioLib`, pines SPI dedicados del módulo).
5. **Bluetooth clásico.** La DevKit (WROOM-32) tiene BT clásico + BLE; el S3
   solo **BLE**. Este firmware usa solo BLE, así que no hay pérdida de
   funcionalidad.

---

## 8. Módulo GPS (`lib/nmea`)

El receptor GPS todavía no está en la placa, pero **todo el software que lo
usará ya está escrito y probado**. Es la parte que se podía adelantar sin
hardware, y así cuando llegue el módulo sólo hay que cablearlo.

### 8.1. Qué hace

`lib/nmea/` convierte las tramas **NMEA 0183** que escupe cualquier GPS en una
posición utilizable:

| Aspecto | Cómo está resuelto |
|---|---|
| **Sentencias** | `GGA` (posición, calidad, satélites, altitud) y `RMC` (posición, validez, fecha/hora, velocidad). Cualquier otra se valida y se ignora. |
| **Talkers** | `GP`, `GN`, `GL`, `GA`, `BD`… — se mira sólo el tipo, no el prefijo. |
| **Checksum** | Se comprueba siempre. Una trama corrupta se descarta y no toca la posición conocida. |
| **Coordenadas** | Se convierten de `ddmm.mmmm` + hemisferio a **grados decimales con signo** (+ norte, + este), en `double` para no perder precisión. |
| **Robustez** | Entrada carácter a carácter, sin `malloc`, con buffer acotado; un `$` resincroniza el parser tras una trama cortada. |

El módulo **no incluye `<Arduino.h>`** a propósito: así se puede compilar y
probar fuera de la placa.

### 8.2. Cómo probarlo HOY, sin GPS

Dos formas, y las dos funcionan ya:

**a) Pruebas automáticas** (16 casos: hemisferios negativos, checksum roto,
trama sin fix, desbordamiento, flujo con ruido…):

```bash
pio test -e devkit_v1      # se ejecutan EN LA PLACA
pio test -e native         # en el PC; necesita gcc-c++ (§8.4)
```

**b) Inyectando tramas a mano** por el monitor serie (o por BLE, es el mismo
manejador de comandos):

```
POS
NMEA $GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47
POS
```

Respuesta esperada:

```
[t=    4438 ms] RX  NMEA        -> trama ACEPTADA (checksum correcto)
[t=    4437 ms] GPS  tramas OK=1  descartadas=0
             lat=48.117300  lon=11.516667  sats=8  hdop=0.9  alt=545.4 m (ultima GGA)
             UTC 12:35:19  antiguedad del fix: 0 ms
```

Si le cambias un dígito al checksum, la trama debe salir **DESCARTADA** y la
posición anterior debe quedar intacta.

### 8.3. Cuando llegue el módulo GPS

1. Cablear (el GPS sólo necesita que le escuchemos, no le mandamos nada):

   | GPS | ESP32 DevKit V1 |
   |---|---|
   | VCC | 3V3 |
   | GND | GND |
   | TX  | **GPIO16** (RX2) |
   | RX  | *sin conectar* |

2. Poner `GPS_UART_ENABLED` a `1` en `src/main.cpp` §5 (o compilar con
   `-D GPS_UART_ENABLED=1`). El baudio por defecto es 9600, el de fábrica de
   los NEO-6M/7M.
3. `POS` debe empezar a dar posición al cabo de 1-2 minutos **con el módulo a
   la vista del cielo**: en interiores un GPS no engancha.

### 8.4. Posición dentro de los mensajes BLE — decisión pendiente

El contrato BLE de la §1 está **congelado** con el equipo de la app: hoy sólo
espera `ALERT:1`, `ALERT:2` y `CANCEL`. Mandarles la posición sin avisar les
rompería el parser, así que la extensión está **implementada pero apagada**
(`ALERT_WITH_POSITION`, `src/main.cpp` §6).

Propuesta a acordar con ellos:

```
ALERT:1;<lat>;<lon>      lat/lon en grados decimales, 6 decimales, punto decimal
ALERT:1                  igual que hoy cuando todavía no hay fix
CANCEL                   nunca lleva posición
```

Es compatible hacia atrás si su parser corta por el primer `;`. Para activarla
una vez acordado: `ALERT_WITH_POSITION` a `1` (o `-D ALERT_WITH_POSITION=1`).
Ya está comprobado que compila.

---

## 9. Estructura del proyecto

```
Expofisica/
├── platformio.ini           # entornos: devkit_v1 (el bueno), heltec_v3 (muerto), native (pruebas PC)
├── src/
│   └── main.cpp             # firmware completo (FSM + BLE + LED + integración GPS)
├── lib/
│   └── nmea/                # módulo GPS: parseo NMEA, sin dependencias de Arduino
│       ├── nmea.h
│       └── nmea.cpp
├── test/
│   └── test_nmea/           # 16 pruebas automáticas del módulo GPS
│       └── test_nmea.cpp
├── README.md                # este archivo
├── PRUEBAS.md               # tabla de casos de prueba
└── .gitignore
```

---

## 10. Solución de problemas

| Síntoma | Causa probable / arreglo |
|---|---|
| `Unable to verify flash chip connection (No serial data received.)` | `upload_speed` demasiado alto para el cable. Deja **115200** (ya está así en `platformio.ini`). |
| `sh: gcc: orden no encontrada` al hacer `pio test -e native` | No hay compilador del sistema: `sudo dnf install gcc-c++`. Mientras tanto, `pio test -e devkit_v1` corre las mismas pruebas en la placa. |
| `POS` dice siempre "sin posicion valida" | Con `GPS_UART_ENABLED=0` es lo normal: no hay GPS, alimenta la posición con `NMEA <trama>`. Con el módulo puesto, sácalo a la vista del cielo y espera 1-2 min. |
| Una trama NMEA sale siempre "DESCARTADA" | Checksum mal copiado, o la línea se cortó: el buffer de comandos son 128 caracteres. Comprueba que copiaste la trama entera, `$` y `*HH` incluidos. |
| `could not open port ... Permission denied` | Falta `dialout` (§3.1) y **cerrar sesión**. Verifica con `id`. |
| `pio: command not found` | PATH: usa `pipx ensurepath && exec $SHELL`, o la ruta completa del venv (§3.3-C). |
| No aparece ningún puerto en `pio device list` | Cable USB **sin datos** (solo carga), o falta el driver: mira `dmesg | tail`. Prueba otro cable/puerto. |
| `Failed to connect to ESP32: Wrong boot mode` | Secuencia BOOT/RST manual (§4), o instala las reglas udev (§3.2). |
| nRF Connect no ve `GEOEXPO-ALERT` | Bluetooth y **permiso de ubicación** activados en el móvil (Android exige ubicación para escanear BLE). Reinicia el advertising reseteando la placa. La primera vez, "olvida" dispositivos emparejados antiguos. |
| Llegan las notificaciones pero se ven como HEX | En nRF Connect, cambia el formato de valor de la característica a **UTF-8**. |
| El monitor muestra caracteres basura | Velocidad incorrecta: debe ser **115200**. |
| `Task watchdog got triggered ... IDLE` | No debería ocurrir: el `loop()` cede con `delay(1)`. Si aparece tras modificar el código, asegúrate de no haber metido un bucle bloqueante. |
| El LED del paso "respiración" no se ve tenue sino fijo | Algunas placas clónicas invierten el LED. Cambia `LED_ACTIVE_HIGH` a `false` en `src/main.cpp` §1. |
