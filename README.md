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

> Diagnóstico de **tu** equipo (usuario `dj4do`, Fedora 44):
>
> - **NO estás en el grupo `dialout`.** Es la causa nº 1 de
>   *"permission denied"* al abrir el puerto serie en Fedora. **Hay que
>   arreglarlo** (paso 3.1).
> - No hay ninguna placa conectada ahora, así que el puerto (`/dev/ttyUSB0` o
>   `/dev/ttyACM0`) se confirmará al enchufarla (paso 3.4).
> - No tienes instalado PlatformIO ni arduino-cli. Fedora empaqueta
>   `platformio 6.1.19` (paso 3.3).

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
============================================================
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

## 7. Migración a Heltec WiFi LoRa 32 V3 (ESP32-S3) — 19-sep

Ya hay un entorno preparado: `pio run -e heltec_v3`.

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

## 8. Estructura del proyecto

```
Expofisica/
├── platformio.ini      # 2 entornos: devkit_v1 (hoy) y heltec_v3 (19-sep)
├── src/
│   └── main.cpp         # firmware completo (FSM + BLE + LED)
├── README.md            # este archivo
├── PRUEBAS.md           # tabla de casos de prueba
└── .gitignore
```

---

## 9. Solución de problemas

| Síntoma | Causa probable / arreglo |
|---|---|
| `could not open port ... Permission denied` | Falta `dialout` (§3.1) y **cerrar sesión**. Verifica con `id`. |
| `pio: command not found` | PATH: usa `pipx ensurepath && exec $SHELL`, o la ruta completa del venv (§3.3-C). |
| No aparece ningún puerto en `pio device list` | Cable USB **sin datos** (solo carga), o falta el driver: mira `dmesg | tail`. Prueba otro cable/puerto. |
| `Failed to connect to ESP32: Wrong boot mode` | Secuencia BOOT/RST manual (§4), o instala las reglas udev (§3.2). |
| nRF Connect no ve `GEOEXPO-ALERT` | Bluetooth y **permiso de ubicación** activados en el móvil (Android exige ubicación para escanear BLE). Reinicia el advertising reseteando la placa. La primera vez, "olvida" dispositivos emparejados antiguos. |
| Llegan las notificaciones pero se ven como HEX | En nRF Connect, cambia el formato de valor de la característica a **UTF-8**. |
| El monitor muestra caracteres basura | Velocidad incorrecta: debe ser **115200**. |
| `Task watchdog got triggered ... IDLE` | No debería ocurrir: el `loop()` cede con `delay(1)`. Si aparece tras modificar el código, asegúrate de no haber metido un bucle bloqueante. |
| El LED del paso "respiración" no se ve tenue sino fijo | Algunas placas clónicas invierten el LED. Cambia `LED_ACTIVE_HIGH` a `false` en `src/main.cpp` §1. |
