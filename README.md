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

## 0. Punto de continuación — 22-sep-2026

**Estado: el llavero de la DevKit queda listo para cablear (botón externo,
zumbador y batería), y el enlace LoRa punto a punto entre dos Heltec V3 está
escrito y probado en el PC, a falta de placas.**

### Sesión del 22-sep (otra vez sin placa delante)

- **Las cosas del botón, terminadas.** Los pines ya no están escritos a mano en
  el código: salen de `platformio.ini`, con comprobaciones que saltan **al
  compilar** si alguien pone el botón y el LED en el mismo pin o el zumbador en
  un GPIO que sólo sabe leer. Entorno nuevo `devkit_llavero` con el cableado de
  verdad: **botón en GPIO4, zumbador piezo en GPIO25 y batería en GPIO34**.
- **Zumbador piezo** (`lib/buzzer`): patrones propios para `ALERT:1`, `ALERT:2`,
  `CANCEL`, alerta confirmada, baliza y búsqueda, con prioridades —una alerta no
  la tapa un bip de interfaz— y **modo mudo** (`MUTE:ON`) para la expo.
- **Batería** por ADC, opcional, con aviso de batería baja y comando `STATUS`.
- **Arranque a prueba de fallos para la I12**: si tres arranques seguidos se
  cuelgan dentro de `NimBLEDevice::init()`, el cuarto arranca **sin BLE**. Lo
  grave de esa incidencia no era quedarse sin Bluetooth, era que el bucle de
  reinicio se llevaba por delante **el botón de pánico**. Ahora el botón, el
  LED, el zumbador y el GPS siguen funcionando y el log lo dice con todas las
  letras. Se rearma con `BLE:RETRY`.
- **Enlace LoRa punto a punto** entre dos Heltec V3, estilo AirTag: un llavero
  que avisa y un buscador que dice "caliente/frío". Todo el detalle en
  **`LORA-P2P.md`**. Es el archivo que hay que leer antes de tocar las Heltec.
- **47 pruebas nuevas** (protocolo, cercanía y zumbador). Con las 34 que ya
  había son **81, todas pasando en el PC** en un par de segundos.
- **`tools/comprobar-sintaxis.sh`**: pasa el `g++` del sistema por los dos
  firmwares con cabeceras de mentira. No sustituye a compilar de verdad, pero
  caza erratas en segundos y **sin descargar el toolchain de ESP32**.

> ⚠️ **Nada de esta sesión se ha probado en placa**, ni la DevKit ni las Heltec
> (que todavía no están). Lo que hay que comprobar cuando haya hardware está en
> `PRUEBAS.md` §5 (LoRa) y en la lista de "qué falta por probar".

---

## 0-bis. Punto de continuación anterior — 10-sep-2026

**Estado: etapa 1 verificada en la placa, módulo GPS probado y la lógica del
botón ahora cubierta por pruebas automáticas que corren en el PC.**

### Sesión del 10-sep (sin placa delante)

Toda esta sesión se hizo **sin el ESP32 a mano**, así que el trabajo fue
revisar el firmware entero a fondo, arreglar lo que estaba mal y —sobre todo—
dejar montada una red de seguridad que no dependa de tener la placa:

- **La máquina de estados del botón se sacó a `lib/panic/`**, igual que se hizo
  con el GPS. Ya no depende de Arduino: recibe `(tiempo, ¿pulsado?)` y devuelve
  qué ha pasado. `src/main.cpp` sólo lee el pin y traduce el evento a NOTIFY,
  LED y traza. **El comportamiento y los textos del log son los de siempre.**
- **18 pruebas nuevas** (`test/test_panic/`) que simulan el botón milisegundo a
  milisegundo: el umbral de los 3 s, rebotes al pulsar y al soltar, cancelar
  desde pulsación corta y larga, la ventana que expira, arrancar con el botón
  pulsado y hasta el desbordamiento de `millis()` a los 49,7 días. Con las 16
  del GPS son **34 pruebas, todas pasando en el PC** (hoy son 81):
  ```bash
  pio test -e native
  ```
- **Tres fallos corregidos** (detalle en `PRUEBAS.md`): **I11** el umbral de
  los 3 s, **I13** una pulsación que se perdía después de un rebote, y **I14**
  las alertas dejaban de salir por BLE si se desconectaba un segundo móvil.
- Sigue **sin resolver la I12** (cuelgue en `NimBLEDevice::init()`). Lo que sí
  se hizo fue acotarla: se localizó la línea exacta en la que se queda colgada
  y se añadieron marcadores en el log para reconocerla al vuelo. Ver §10.

> ⚠️ **Nada de esta sesión se ha probado en la placa.** Antes de dar por bueno
> el firmware hay que flashear y repetir la §6, incluyendo los 10 reinicios
> seguidos que pide la incidencia I12.

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

- **`pio test -e native` ya funciona** (el `gcc-c++` que faltaba está
  instalado). Es la forma rápida de comprobar la lógica **sin placa y en
  segundo y medio**. Las mismas pruebas corren también en la placa con
  `pio test -e devkit_v1`.
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

> **Dónde está el código.** La lógica vive en **`lib/panic/`** y no depende de
> Arduino (no llama a `millis()` ni a `digitalRead()`, no toca el LED y no
> imprime nada): recibe `(tiempo, ¿pulsado?)` y devuelve qué ha pasado.
> `src/main.cpp` §9 se limita a leer el pin y traducir el evento a NOTIFY, LED
> y traza. Gracias a eso la FSM se prueba entera en el PC con
> `pio test -e native`, que es la única forma de cubrir los bordes de
> temporización sin estar pulsando el botón a ojo con un cronómetro.

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
> mensajes BLE/serie se emiten igual.

### Realimentación sonora del zumbador (`lib/buzzer`)

Con `-D PIN_BUZZER_CFG=<pin>` (el entorno `devkit_llavero` ya lo trae en
GPIO25) cada evento tiene además su sonido:

| Situación | Zumbador |
|---|---|
| Arranque | dos notas cortas ascendentes |
| `ALERT:1` | un pitido de 250 ms |
| `ALERT:2` | dos pitidos, el segundo más agudo y más largo |
| `CANCEL` | tres pitidos descendentes |
| Ventana expirada (alerta **confirmada**) | dos pitidos secos |
| Baliza (`BEACON:ON`) | sirena lenta, sin parar |

Los patrones tienen **prioridad**: la baliza no la tapa una alerta, y una
alerta no la tapa el bip de una confirmación. `MUTE:ON` silencia el zumbador
sin tocar nada más (útil si en la expo hay que bajar el volumen un rato);
`BEEP` hace una prueba de sonido.

El zumbador es **pasivo**: no suena solo, hay que darle una onda cuadrada. Se
genera con LEDC en un canal propio, para no pelearse con el `analogWrite()` que
mueve el LED.

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

## 7. Heltec WiFi LoRa 32 V3 (ESP32-S3) — enlace LoRa punto a punto

> **Esto cambió el 22-sep.** Lo que en septiembre estaba cancelado vuelve al
> plan, pero **no como migración**: la DevKit sigue siendo el llavero con BLE
> y el equipo de la app no tiene que cambiar nada. Lo de las Heltec es un
> **segundo aparato** que funciona por su cuenta, de placa a placa, sin móvil.
>
> **Todo está en `LORA-P2P.md`**: qué hace cada papel, cómo se monta, cómo se
> flashea, los comandos, el formato de trama y la lista de pruebas pendientes.
> Aquí sólo queda el resumen y las notas de portabilidad que ya estaban.

```bash
pio run -e heltec_tag    -t upload     # el llavero que avisa
pio run -e heltec_finder -t upload     # el buscador que lo encuentra
```

El firmware de las Heltec es `src/lora/main_lora.cpp` y **no** comparte
`main.cpp` con la DevKit: son dos aparatos distintos que sí comparten las
librerías de `lib/` (botón, GPS, zumbador). Reaprovechar `lib/panic` significa
que el botón de pánico se comporta **exactamente igual** en las dos placas.

### Notas de portabilidad que siguen valiendo

| Punto | Cómo está resuelto |
|---|---|
| **Pines** | Todos salen de `platformio.ini` con `-D`. El LED integrado de la V3 es `GPIO35`; el botón PRG, `GPIO0`. Los de la radio (NSS 8, SCK 9, MOSI 10, MISO 11, RST 12, BUSY 13, DIO1 14) van soldados en la placa. |
| **PWM del LED** | En la DevKit se usa `analogWrite()`, portable entre arduino-esp32 2.x y 3.x. El zumbador sí usa LEDC directamente, con `#if ESP_ARDUINO_VERSION_MAJOR >= 3` para las dos firmas de la API. |
| **USB / puerto serie** | El S3 usa USB nativo (CDC): los entornos `heltec_*` ya añaden `-D ARDUINO_USB_MODE=1 -D ARDUINO_USB_CDC_ON_BOOT=1`. El puerto será **`/dev/ttyACM0`**. |
| **Radio** | `RadioLib` con el `SX1262`, en `src/lora/main_lora.cpp`. Transmisión **no bloqueante** (interrupción de DIO1), como todo lo demás del proyecto. |

### Cosas a tener en cuenta con las Heltec

1. **Antena obligatoria.** No enciendas la radio sin ella: la potencia rebota
   hacia el amplificador y se puede cargar el módulo.
2. **Banda de frecuencia.** Las V3 vienen en versión US915 o EU868 y no son
   intercambiables. `-D LORA_FREQ_MHZ` tiene que corresponder a tu placa **y a
   la ley de tu país** (915 en América, 868 en Europa).
3. **El botón PRG (GPIO0) es strapping de arranque**, igual que en la DevKit:
   vale para probar, pero el botón definitivo va en otro pin.
4. **`Vext` (GPIO36)** alimenta los periféricos de la placa y el OLED. Este
   firmware no usa pantalla, pero tenlo presente si se añade.
5. **Versión de NimBLE + core**, si algún día se mete BLE en la Heltec: con
   arduino-esp32 **3.x** hay que pasar a **NimBLE-Arduino 2.x**, que cambia
   tres firmas en `src/main.cpp` (`ServerCB`, `RxCB` y `bleBegin`):
   `onConnect(NimBLEServer*, NimBLEConnInfo&)`,
   `onWrite(NimBLECharacteristic*, NimBLEConnInfo&)` y `setPower(int dBm)`.
6. **Bluetooth clásico.** La DevKit (WROOM-32) tiene BT clásico + BLE; el S3
   solo **BLE**. Este proyecto usa solo BLE, así que no se pierde nada.

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
├── platformio.ini           # entornos: devkit_v1, devkit_llavero, heltec_tag,
│                            #           heltec_finder y native (pruebas en el PC)
├── src/
│   ├── main.cpp             # firmware DevKit: BLE, botón, LED, zumbador, GPS
│   └── lora/
│       └── main_lora.cpp    # firmware Heltec V3: enlace LoRa punto a punto
│                            # (el mismo archivo hace de llavero o de buscador)
├── lib/                     # lógica pura, SIN Arduino -> se prueba en el PC
│   ├── nmea/                # GPS: parseo de tramas NMEA
│   ├── panic/               # máquina de estados del botón de pánico
│   ├── buzzer/              # patrones del zumbador piezo
│   ├── link/                # protocolo GEO-LINK: trama, firma, antirrepetición
│   └── ranging/             # RSSI -> zona -> distancia -> cadencia de pitido
├── test/                    # 81 pruebas automáticas (pio test -e native)
│   ├── test_nmea/     (16)
│   ├── test_panic/    (18)
│   ├── test_link/     (20)
│   ├── test_ranging/  (14)
│   └── test_buzzer/   (13)
├── tools/
│   ├── comprobar-sintaxis.sh   # g++ sobre los dos firmwares, sin toolchain ESP32
│   └── stubs/                  # cabeceras de mentira SÓLO para ese script
├── README.md                # este archivo
├── LORA-P2P.md              # el enlace LoRa entre las dos Heltec, al detalle
├── INTEGRACION-APP.md       # contrato BLE para el equipo de la app Android
├── PRUEBAS.md               # tabla de casos de prueba
└── .gitignore
```

### Qué entorno uso para qué

| Quiero… | Entorno |
|---|---|
| Probar la lógica sin ninguna placa (2 s) | `pio test -e native` |
| Probar el botón con la DevKit tal cual, sin cablear nada | `pio run -e devkit_v1 -t upload` |
| El llavero cableado: botón externo, zumbador y batería | `pio run -e devkit_llavero -t upload` |
| El llavero LoRa | `pio run -e heltec_tag -t upload` |
| El buscador LoRa | `pio run -e heltec_finder -t upload` |
| Saber si un cambio compila, sin descargar nada | `./tools/comprobar-sintaxis.sh` |

---

## 10. Solución de problemas

| Síntoma | Causa probable / arreglo |
|---|---|
| `Unable to verify flash chip connection (No serial data received.)` | `upload_speed` demasiado alto para el cable. Deja **115200** (ya está así en `platformio.ini`). |
| `sh: gcc: orden no encontrada` al hacer `pio test -e native` | Ya resuelto (hay `gcc-c++`). Si reaparece en otra máquina: `sudo dnf install gcc-c++`. |
| **Al abrir el monitor salen solas transiciones de la FSM y hasta un `ALERT:2`** | El terminal activa **DTR**, que en esta placa está cableado a **GPIO0** — el mismo pin del botón. Ya está corregido con `monitor_dtr = 0` y `monitor_rts = 0` en `platformio.ini`. Si usas otro terminal (screen, Arduino IDE, minicom), **desactiva DTR/RTS ahí también** o volverá a pasar. |
| **La placa arranca diciendo "BLE: DESACTIVADO tras 3 arranques colgados"** | Es la red de seguridad de la **I12** haciendo su trabajo: tres arranques seguidos se colgaron dentro de `NimBLEDevice::init()` y el firmware ha arrancado **sin BLE** para que el botón de pánico siga funcionando. El aparato sirve igual salvo por el Bluetooth. Para reintentar: comando **`BLE:RETRY`** (pone el contador a cero y reinicia) o quitar y poner la alimentación. Si vuelve a pasar siempre, es la I12 de verdad y hay que mirar el punto siguiente. |
| **El zumbador no suena** | ¿Está compilado con `-D PIN_BUZZER_CFG=<pin>` (el entorno `devkit_v1` NO lo trae, `devkit_llavero` sí)? ¿Hay un `MUTE:ON` activo? Prueba con el comando **`BEEP`**. Y comprueba que el piezo sea **pasivo**: uno activo suena a su propia frecuencia y con estas señales apenas se le oye. |
| **La placa no arranca desde que se cableó el botón** | El botón está en un *strapping pin* (GPIO0, 2, 12 o 15) y estaba pulsado al resetear. Cámbialo a GPIO4, 5, 18, 19, 21, 22, 23, 25, 26 o 27 con `-D PIN_BUTTON_CFG=<pin>`. |
| **El botón se comporta al revés (parece pulsado siempre)** | Está cableado a 3V3 en vez de a masa. Compila con `-D BUTTON_ACTIVE_HIGH=1` o cámbialo a masa. |
| **`STATUS` dice "Bateria: no medida"** | No hay divisor cableado o falta `-D PIN_VBAT_CFG=<pin>`. El pin tiene que ser del ADC1 (GPIO32-39): los del ADC2 dejan de leerse en cuanto el WiFi está activo, así que mejor no jugársela. |
| **Bucle de reinicio: `rst:0x8 (TG1WDT_SYS_RESET)` justo tras el banner** | Incidencia **I12**, sin causa conocida. **Cómo confirmar que es ella:** en el log sale `BLE: entrando en NimBLEDevice::init() ...` y **nunca** llega el `init() completado` de la línea siguiente. Se queda dentro de la espera `while(!m_synced) taskYIELD();` del final de `NimBLEDevice::init()` (`NimBLEDevice.cpp:910`), esperando un `sync` del controlador BT que no llega. **Qué hacer:** vuelve al último binario que arrancaba (`git stash` de tus cambios + `pio run -t upload`) y comprueba con **10 reinicios seguidos** antes de dar nada por bueno. |
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
