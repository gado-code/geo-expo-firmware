# Ivy LoRa · Diseño de la red (E1 punto a punto → E2 multi-salto)

> **Estado:** diseño, 18-sep-2026. Todavía no hay código en `lora/`.
> **Placas:** 2 × Heltec WiFi LoRa 32 **V3** (ESP32-S3 + SX1262).
> **Alcance de este documento:** subproyecto PlatformIO **independiente** en
> `lora/`, con su propio `platformio.ini`. No toca nada del firmware del
> llavero (`src/`, `lib/`, `test/` y `platformio.ini` de la raíz), así que no
> cambia el binario de `devkit_v1` ni reabre la incidencia I12.
>
> El README raíz (§7) dice que la migración a Heltec se canceló el 8-sep. Eso
> sigue siendo cierto **para el llavero**: este subproyecto no migra nada, es
> una red LoRa aparte que en el futuro podrá transportar las alertas del
> llavero (ver §14).

---

## 0. Resumen y decisiones que se apartan del encargo

El diseño en una frase: **un único firmware** para todos los nodos, con un
formato de paquete que ya incluye `ttl`/`hops` desde E1, de modo que pasar de
punto a punto a multi-salto es **cambiar `TTL 0` + `RELAY OFF` por `TTL 3` +
`RELAY ON`**, sin reescribir código ni cambiar el formato.

Durante la investigación salieron cuatro puntos que **contradicen el encargo**.
Van aquí arriba para que la sesión principal los decida antes de implementar:

| # | Encargo | Propuesta | Motivo (detalle en) |
|---|---|---|---|
| D1 | Frecuencia por defecto **915.0 MHz** | **921.5 MHz** (configurable 915.25–927.75) | En Colombia **sólo 915–928 MHz** es de uso libre. 902–915 MHz **no**, y la Res. ANE 28/2026 lo asigna al enlace ascendente móvil (896–915 MHz). Un canal centrado en 915.0 MHz se sale **por debajo** del borde de la banda. 921.5 MHz evita además la frecuencia por defecto de Meshtastic ANZ y las sub-bandas AU915 que más se usan (§3, §4.1). |
| D2 | SF/BW sin especificar (lo típico es BW 125 kHz) | Perfil por defecto **SF11 / BW 500 kHz / CR 4/5** | El Anexo 1 de la Res. ANE 105/2020 exige un **ancho de banda a 6 dB ≥ 500 kHz** para la categoría de "modulación digital" (1 W). Un canal único de 125 kHz no encaja en esa categoría. SF11/BW500 tiene **el mismo tiempo de símbolo** que SF9/BW125, una sensibilidad casi igual (−128.5 frente a −129.5 dBm) y **menos** tiempo en el aire (§3, §4). |
| D3 | USB CDC nativo (`ARDUINO_USB_CDC_ON_BOOT=1`) | **`ARDUINO_USB_CDC_ON_BOOT=0`**: `Serial` = UART0 | La V3 lleva un puente **CP2102** entre el USB-C y la UART0. Con `CDC_ON_BOOT=1`, `Serial` se va al USB nativo (GPIO19/20), que en la V3 **no llega al conector**, y la consola se queda muda. El `board` de PlatformIO ya pone `ARDUINO_USB_MODE=1`, y es inocuo. Ojo: el entorno `heltec_v3` del `platformio.ini` raíz tiene este mismo fallo, aunque es material muerto (§2.3). |
| D4 | "sync word privado" | **0x12**, el sync word *privado* estándar de LoRa (el 0x34 es el público de LoRaWAN) | Los valores "inventados" tienen riesgos de sensibilidad y de falsas detecciones. Nuestros paquetes se distinguen de otras redes privadas por el `magic`+versión y el CRC16 de aplicación (§5). El sync word sigue siendo configurable. |

---

## 1. Objetivo y alcance por etapas

**Objetivo:** mandar mensajes cortos (texto de prueba y **alertas GEO-EXPO**:
`ALERT:1`, `ALERT:2` y `CANCEL`, con lat/lon opcional) entre placas Heltec V3
por LoRa. Tienen que llegar con **confirmación de entrega** y hay que poder
medir el alcance real en campo **sin PC**.

| Etapa | Qué | Nodos | Criterio de "hecho" |
|---|---|---|---|
| **E1 · P2P con ACK** | Enlace directo A↔B. DATA/ALERT con ACK, reintentos con *backoff*, duplicados filtrados, PING/PONG con RSSI/SNR/RTT, prueba de alcance `RANGE`, OLED y botón PRG para el campo. | 2 | Pruebas nativas en verde. En banco: 100 SEND seguidos con 0 duplicados entregados y 100 % confirmados. En campo: planilla de alcance (§13.3) rellena para 2 perfiles. |
| **E2 · Multi-salto** | Inundación controlada: TTL, caché de vistos, retardo aleatorio ponderado por SNR, supresión, ACK de vuelta por la malla, ACK implícito en broadcast. **Mismo firmware y mismo formato**, se activa por configuración. | ≥ 3 (con 2 sólo se prueba en simulación, §13.1) | Simulación nativa de topologías (línea, anillo, malla) en verde. En campo, A→C a través de B con B fuera del alcance directo de A–C. |
| E3 · (fuera de alcance, prevista) | Autenticación (flag `AUTH`), pasarela GEO-EXPO (BLE/Internet), GPS, bajo consumo / nodos solares, encaminamiento *next-hop*. | — | — |

**No objetivos de E1/E2:** cifrado, pasarela, OTA, LoRaWAN, salto de
frecuencia y bajo consumo.

---

## 2. Hardware: Heltec WiFi LoRa 32 V3

### 2.1. Pines (verificados)

Fuentes: `variants/heltec_wifi_lora_32_V3/pins_arduino.h` de arduino-esp32, el
`variant.h` de Meshtastic para la V3 y la librería de ropg (§17).

| Función | GPIO | Nota |
|---|---|---|
| SX1262 NSS (CS) | 8 | `SS` en `pins_arduino.h` |
| SX1262 SCK / MOSI / MISO | 9 / 10 / 11 | Son también el `SPI` por defecto de la variante |
| SX1262 RESET | 12 | `RST_LoRa` |
| SX1262 BUSY | 13 | `BUSY_LoRa` (Meshtastic lo llama `LORA_DIO2`, pero es BUSY) |
| SX1262 **DIO1** (IRQ) | 14 | **Ojo:** `pins_arduino.h` lo llama `DIO0 = 14`. El SX1262 **no tiene** DIO0: es DIO1. |
| SX1262 DIO2 | — | Interno, maneja el **conmutador RF** → `setDio2AsRfSwitch(true)` |
| SX1262 DIO3 | — | Interno, alimenta el **TCXO a 1.8 V** → `tcxoVoltage = 1.8` en `begin()` |
| `Vext` | 36 | **LOW = encendido**. Alimenta el OLED. Meshtastic dice que también el "antenna boost" (sin confirmar en el esquemático), así que por prudencia Vext va **siempre ON** mientras la radio funciona. |
| OLED SDA / SCL / RST | 17 / 18 / 21 | SSD1306 128×64, I²C 0x3C. **No** son los `SDA=41`/`SCL=42` por defecto de `Wire`. |
| LED blanco | 35 | `LED_BUILTIN` |
| Botón PRG | 0 | Activo a nivel bajo. Es **pin de arranque**: no pulsarlo durante un reset. |
| Batería (ADC) / control del divisor | 1 / 37 | Opcional (E1.x). Meshtastic usa `ADC_CTRL` activo LOW y un multiplicador ×4.9·1.045. La polaridad cambia entre revisiones: verificar en la placa. |

### 2.2. Si fuera una V4 (diferencias a vigilar)

La V4 mantiene los mismos pines del SX1262 (8–14), `Vext` 36, el OLED y el
botón. Añade un **front-end de RF (FEM) con PA externo** de hasta 28 ± 1 dBm:

- V4.2 (GC1109): `PA_EN` (CSD) en **GPIO2** y `PA_TX_EN` (CPS) en **GPIO46**.
- V4.3 (KCT8103L): `CSD` en **GPIO2** y `CTX` en **GPIO5**.
- Alimentación del FEM (`VFEM_Ctrl`) en **GPIO7**.

Sin esos pines bien manejados la V4 **transmite pero no se oye** (hay un caso
documentado en las discusiones de RadioLib). Además, `PWR` significa otra cosa
en la V4: la potencia a la salida del PA ≠ la potencia del SX1262. Hay que
limitarla en firmware por la norma (§3). La V4 trae conector GNSS (L76K,
GPIO 38/39 y otros), útil para E3. El firmware aísla todo esto en `src/placa.h`
detrás de `#if defined(PLACA_HELTEC_V4)`, pero **sólo se implementa y se prueba
la V3**.

### 2.3. USB y consola

La V3 lleva un puente **CP2102**: en Windows aparece como *"Silicon Labs CP210x
USB to UART Bridge (COMx)"*, igual que la DevKit. Por eso:

- `Serial` tiene que ser la UART0 → `-D ARDUINO_USB_CDC_ON_BOOT=0` (decisión D3).
- El truco de `monitor_dtr = 0` / `monitor_rts = 0` de la raíz también aplica
  aquí: el auto-reset DTR→GPIO0 simularía el botón PRG pulsado.
- `setup()` **no** espera a `Serial` (`while(!Serial)` prohibido): en campo no
  hay PC.

### 2.4. Reglas físicas

- **Nunca transmitir sin la antena puesta** (se puede dañar el PA). El firmware
  arranca **en recepción**, y el primer TX sólo sale por orden explícita
  (consola o botón).
- En banco, a menos de 1 m y a +22 dBm se satura el receptor (RSSI > −30 dBm y
  pérdidas raras). Para probar en mesa: `PWR 0` o separar las placas ≥ 2 m.

---

## 3. Normativa en Colombia (nota prudente)

**Esto no es asesoría legal.** Es lo que dicen los textos consultados el
18-sep-2026.

1. **Banda:** según el Anexo 1 (Tabla 1.2) de la **Res. ANE 105 de 2020**, en
   Colombia la banda de uso libre para "dispositivos de salto en frecuencia,
   modulación digital o híbridos" es **915–928 MHz**. El Análisis de Impacto
   Normativo de la ANE lo dice explícitamente: de 902–928 MHz, en Colombia
   *"solo está permitido el uso"* de 915–928 MHz. La **Res. ANE 28 de 2026**
   reorganiza la banda de 900 MHz (enlace ascendente móvil 896–915 MHz) y
   mantiene la convivencia con los dispositivos de uso libre en 915–928 MHz.
   → **Todo el canal (centro ± BW/2) debe caer dentro de 915–928 MHz.** El
   firmware lo valida (§9) y rechaza, por ejemplo, 915.0 MHz con BW 500.
   Meshtastic asigna a Colombia la región **ANZ** (915–928 MHz), lo que
   coincide.
2. **Potencia (§3.6.1.1 del Anexo 1):**
   - **1 W conducido** para "modulación digital", con **ancho de banda mínimo
     de 500 kHz a 6 dB**.
   - Con salto de frecuencia y canales de menos de 250 kHz hacen falta **≥ 50
     frecuencias de salto** y límites de ocupación.
   - Si la antena supera 6 dBi, hay que reducir la potencia en lo que exceda.
3. **Qué implica para nosotros:**
   - Un enlace LoRa de **canal único a 125/250 kHz** no es ni "modulación
     digital" (< 500 kHz) ni salto de frecuencia. Estrictamente caería en
     "RCA no específicas": 50 mV/m a 3 m, del orden de **1 mW PIRE**.
   - Por eso el perfil por defecto usa **BW 500 kHz** (D2). Aun así, un
     transceptor LoRa de 500 kHz queda **justo en el límite** de los 500 kHz a
     6 dB: si algún día se certifica, hay que **medirlo**.
   - La potencia por defecto es **moderada: +14 dBm (25 mW)**. El máximo del
     SX1262 es +22 dBm (≈ 158 mW), muy por debajo de 1 W.
   - Los perfiles de 125 kHz existen **sólo para pruebas comparativas cortas**.
4. **Para un producto** (no para estas pruebas) haría falta la homologación del
   equipo terminal ante la CRC y una revisión formal con la ANE. Queda como
   tarea abierta (§16).

Autoimpuesto (no lo exige la norma consultada, pero es de buen vecino): un
**presupuesto de ocupación ≤ 10 % en cualquier ventana de 60 s** para el
tráfico que no es ALERT (§7.6).

---

## 4. Parámetros de radio por defecto

### 4.1. Valores

| Parámetro | Defecto | Rango aceptado | Comentario |
|---|---|---|---|
| Frecuencia | **921.5 MHz** | f ± BW/2 ⊂ [915, 928] | Evita 919.875 MHz (Meshtastic ANZ *LongFast*), la FSB2 de AU915 (916.8–918.2, la que usan TTN/Helium) y los descendentes AU915 (923.3+). |
| SF | **11** | 7–12 | |
| BW | **500 kHz** | 125 / 250 / 500 | 125/250 sólo para laboratorio (§3) |
| CR | **4/5** | 4/5–4/8 | En RadioLib se pasa el denominador (5–8) |
| Sync word | **0x12** (privado) | 1 byte | RadioLib lo convierte al formato de 2 bytes del SX126x |
| Preámbulo | **16 símbolos** | 8–64 | Más largo que el 8 típico para que el CAD (escucha antes de hablar) lo detecte con fiabilidad en E2. Es el mismo valor que usa Meshtastic. |
| Potencia | **+14 dBm** | −9…+22 dBm | `PWR 22` para las pruebas de alcance. Con +22, límite de corriente (OCP) a 140 mA: verificarlo en RadioLib. |
| CRC de la PHY | ON | — | Además del CRC16 de aplicación |
| Cabecera LoRa | explícita | — | |
| LDRO | automático | — | Se activa cuando T_sím > 16 ms (SF11/SF12 a BW125) |
| Ganancia RX | *boosted* | — | `setRxBoostedGainMode(true)`: ≈ +2 dB de sensibilidad por ≈ 2 mA |
| TCXO / regulador | 1.8 V / DC-DC | — | Valor por defecto de RadioLib (`useRegulatorLDO=false`) |

**Presets** (comando `RADIO PRESET <nombre>`, y la pulsación larga del botón en
el campo si se habilita, §10):

| Preset | SF/BW/CR | Sensibilidad aprox.* | Bitrate | Uso |
|---|---|---|---|---|
| `RAPIDO` | 9 / 500 / 4-5 | −123.5 dBm | 7.0 kbps | Banco, enlaces cortos, E2 denso |
| **`NORMAL`** | **11 / 500 / 4-5** | **−128.5 dBm** | 2.1 kbps | Defecto |
| `LARGO` | 12 / 500 / 4-5 | −131 dBm | 1.2 kbps | Máximo alcance dentro de la categoría de 500 kHz |
| `LAB125` | 9 / 125 / 4-5 | −129.5 dBm | 1.8 kbps | Sólo para comparar con la configuración "típica" de la comunidad; pruebas cortas (§3) |

\* Estimada con −174 + 10·log(BW) + NF 6 dB + SNR mínimo por SF (−7.5 dB en
SF7 … −20 dB en SF12). La sensibilidad real se mide en §13.3.

### 4.2. Tiempo en el aire (airtime)

Fórmula de Semtech (AN1200.13), con cabecera explícita, CRC ON y preámbulo
N_pre = 16:

```
T_sím   = 2^SF / BW
T_pre   = (N_pre + 4.25) · T_sím
N_carga = 8 + max( ceil( (8·PL − 4·SF + 28 + 16·CRC − 20·IH) / (4·(SF − 2·DE)) ) · (CR + 4), 0 )
T_aire  = T_pre + N_carga · T_sím          (CR = 1..4 ↔ 4/5..4/8, DE = LDRO)
```

Tamaños de trama según el §5 (cabecera 12 B + CRC 2 B = **14 B de
sobrecarga**):

| Trama (bytes PHY) | RAPIDO SF9/500 | **NORMAL SF11/500** | LARGO SF12/500 | LAB125 SF9/125 |
|---|---|---|---|---|
| PING 18 · ACK 19 · PONG 21 | 54.5 ms | **197.6 ms** | 354–395 ms | 218.1 ms |
| ALERT sin posición, 17 | 49.4 ms | **197.6 ms** | 354.3 ms | 197.6 ms |
| ALERT con posición, 25 | 59.6 ms | **218.1 ms** | 436.2 ms | 238.6 ms |
| DATA con 32 caracteres, 46 | 85.2 ms | **300.0 ms** | 559.1 ms | 341.0 ms |
| DATA máxima, 214 (200 de carga) | 274.7 ms | **914.4 ms** | 1706 ms | 1098.8 ms |

Por comparar: SF12/BW125 tarda **1.58 s** en un PING. Por eso no está entre los
presets. La función `airtimeMs()` de la librería pura reproduce esta tabla (es
un test, §13.1), y en la placa se contrasta con `radio.getTimeOnAir()` de
RadioLib (que devuelve µs).

### 4.3. Presupuesto de enlace (orientativo)

Con +14 dBm, antenas de serie de 0–2 dBi y el preset NORMAL (−128.5 dBm)
salen ≈ 143–147 dB de pérdida de trayecto admisible. Con 10 dB de margen de
desvanecimiento quedan ≈ 135 dB. En espacio libre eso son decenas de km, pero
en Bogotá urbana y sin línea de vista lo esperable es del orden de **1–3 km**.
Pasar a `PWR 22` da +8 dB (≈ ×2.5 en distancia con línea de vista). Son
**hipótesis a medir**, no promesas: para eso está el `RANGE`.

---

## 5. Formato de paquete binario (versión 1)

Todos los enteros multibyte van en **little-endian**, serializados byte a byte
(nunca con `memcpy` de un `struct`, para no depender del empaquetado del
compilador).

### 5.1. Cabecera (12 bytes) + carga + CRC

| Off | Campo | Tam | Descripción |
|---|---|---|---|
| 0 | `magic` | 1 | **0x47** (`'G'`). Descarta de entrada las tramas de otras redes con sync word 0x12. |
| 1 | `ver_tipo` | 1 | Nibble alto = **versión** (1). Nibble bajo = **tipo** (§5.2). |
| 2 | `flags` | 1 | §5.3 |
| 3–4 | `src` | 2 | ID del nodo **origen** (§6). **No** cambia al reenviar. |
| 5–6 | `dst` | 2 | Destino final. **0xFFFF = broadcast**, **0xFFFE = cualquier pasarela** (prevista). |
| 7–8 | `msgId` | 2 | Contador del origen. **Se conserva en los reintentos y en los reenvíos.** |
| 9 | `ttl` | 1 | Saltos de reenvío que **quedan**. 0 = nadie lo repite (E1). |
| 10 | `hops` | 1 | Reenvíos que ya ha sufrido (0 = viene directo del origen). |
| 11 | `len` | 1 | Bytes de carga. Debe cumplir `len_PHY == 14 + len` y `len ≤ LEN_MAX` (200). |
| 12… | `carga` | `len` | Según el tipo (§5.4) |
| 12+len | `crc` | 2 | **CRC-16/CCITT-FALSE** (poli 0x1021, init 0xFFFF, sin reflejar, xorout 0) sobre los bytes 0…11+len. Vector de prueba: `"123456789"` → **0x29B1**. |

El CRC cubre `ttl`/`hops`, así que **el repetidor lo recalcula** al reenviar.
El LoRa ya tiene CRC de PHY. El de aplicación protege además frente a tramas
válidas de otras redes y frente a errores entre la radio y el búfer, y deja
probar la integridad en nativo.

**Receptor, validación en este orden** (cada rechazo incrementa un contador
visible en `STATUS`): longitud mínima 14 → `magic` → versión → `len`
coherente → CRC → tipo conocido → `src` no reservado. Los tipos desconocidos
**con versión conocida** se descartan pero **se reenvían** en E2, así un nodo
antiguo no rompe la malla de nodos nuevos.

### 5.2. Tipos

| Valor | Tipo | ¿ACK_REQ? | Carga |
|---|---|---|---|
| 0x1 | `DATA` | sí en unicast, no en broadcast | Bytes/texto libre (consola `SEND`/`BCAST`) |
| 0x2 | `ACK` | nunca | `ackMsgId u16` · `rssiNeg u8` · `snrQ4 i8` · `hopsRx u8` = **5 B** |
| 0x3 | `PING` | no (la respuesta es el PONG) | `t_ms u32` (millis del emisor) [+ relleno opcional para pruebas de tamaño] |
| 0x4 | `PONG` | no | `t_eco u32` · `rssiNeg u8` · `snrQ4 i8` · `hopsRx u8` = **7 B** |
| 0x5 | `ALERT` | **siempre** | §5.4 |
| 0x6 | `ORDEN` | — | **Reservado** (E3: bajada hacia el llavero, p. ej. BEACON:ON/OFF) |
| 0x7–0xF | — | — | Reservados |

- `rssiNeg` = −RSSI en dBm, saturado a 0…255 (−87 dBm → 87).
- `snrQ4` = SNR en cuartos de dB (el mismo formato que da el SX126x: −10.25 dB
  → −41).
- `hopsRx` = el campo `hops` de la trama a la que se responde, con lo que el
  origen aprende la longitud del camino.

Así el ACK y el PONG llevan la **calidad de la ida**, medida en el otro
extremo, y el receptor mide la de la **vuelta**. Con dos placas tenemos los
dos sentidos del enlace sin PC en el otro lado.

### 5.3. Flags

| Bit | Nombre | Significado |
|---|---|---|
| 0 | `ACK_REQ` | El destino debe contestar con un ACK |
| 1 | `AUTH` | **Reservado E3.** Detrás de la carga irá una etiqueta de autenticación. En v1 debe valer 0; si llega a 1, se descarta. |
| 2 | `PRUEBA` | Tráfico de prueba: una ALERT con este bit se muestra, pero una futura pasarela **no** debe escalarla |
| 3–7 | — | Reservados. El emisor los pone a 0 y el receptor los ignora. |

Un cambio incompatible sube la **versión**, no los flags.

### 5.4. Carga de ALERT (transporte de los tokens GEO-EXPO)

| Campo | Tam | Descripción |
|---|---|---|
| `codigo` | 1 | **1 = `ALERT:1`**, **2 = `ALERT:2`**, **3 = `CANCEL`**. 0 y 4–255 reservados. |
| `seqAlerta` | 1 | Número de alerta del origen. Un `CANCEL` lleva el **mismo** `seqAlerta` que la alerta que anula, para que la pasarela los empareje. |
| `aflags` | 1 | bit0 `POS` (sigue lat/lon), bit1 `LLAVERO` (reservado E3: id del llavero), resto a 0 |
| `lat`, `lon` | 4 + 4 | Sólo si `POS`. `int32` en **1e-7 grados** (con signo: Colombia tiene longitud negativa). |

**Correspondencia con el contrato BLE congelado:** la carga lleva un **código**
de 1 byte, no el texto, por airtime. Quien la traduzca de vuelta (consola,
OLED, futura pasarela) debe producir **exactamente** los tokens `ALERT:1`,
`ALERT:2` y `CANCEL` del README raíz §1. Si llega a acordarse la extensión con
posición del README §8.4, la pasarela añade `;<lat>;<lon>` con 6 decimales.
`BEACON:ON`/`OFF` son órdenes **hacia** el llavero, no alertas: no viajan en
ALERT (tipo `ORDEN`, reservado).

### 5.5. Ejemplos de trama (vectores de test)

PING de `1A2B` a `3C4D`, `msgId` 7, `ttl` 0, `t_ms` = 1000 (18 bytes):

```
47 13 00 2B 1A 4D 3C 07 00 00 00 04 | E8 03 00 00 | F3 A5
```

ALERT:2 de `1A2B` a "cualquier pasarela" (0xFFFE), `ACK_REQ`, `msgId` 8,
`ttl` 3, `seqAlerta` 5, con posición 4.6097100, −74.0817500 (Bogotá)
(25 bytes):

```
47 15 01 2B 1A FE FF 08 00 03 00 0B | 02 05 01 CC 62 BF 02 A4 05 D8 D3 | BE 16
```

La misma trama reenviada una vez (`ttl` 2, `hops` 1) termina en
`… 02 01 0B … D3 | BC 28`: el CRC cambia porque cubre `ttl`/`hops`.

---

## 6. Identidad de nodo

- **ID por defecto:** los **2 últimos bytes de la MAC base** (`ESP.getEfuseMac()`).
  Una MAC `…:1A:2B` da el ID `1A2B`, que se puede comprobar a ojo con la
  etiqueta o con `ID`. Las placas del mismo lote tienen MAC consecutivas, así
  que difieren en esos bytes.
- **IDs reservados:** `0x0000` (sin asignar/inválido) y **`0xFFF0`–`0xFFFF`**
  (`FFFF` broadcast, `FFFE` pasarela, el resto para el futuro). Si la MAC da un
  ID reservado, se usa `crc16(MAC)`; si también lo es, `crc16 ^ 0x0101`.
- **Override:** `ID <hex4>` lo guarda en NVS (Preferences, espacio `geolora`,
  clave `id`), valida que no sea reservado y se aplica al instante. `ID AUTO`
  borra el override.
- **Detección de ID duplicado:** llega una trama con `src == miId` y un `msgId`
  que **no** está en mi lista de enviados recientes (en E2 los ecos de mis
  propias tramas sí están) → evento `EV ID_DUPLICADO` en la consola y aviso en
  el OLED.
- **`msgId`:** `u16` por nodo, que arranca en `esp_random()` en cada arranque.
  Así un reinicio no reusa IDs que siguen en las cachés de los vecinos. Se
  incrementa por mensaje nuevo, **no** por reintento.

La derivación es una función pura, `derivarId(const uint8_t mac[6])`, con
tests.

---

## 7. Fiabilidad

### 7.1. ACK

- El destino de una trama con `ACK_REQ` contesta con un `ACK` (`dst` = `src`
  original, `msgId` **propio**, `ttl` = TTL configurado) tras un **retardo de
  giro** de 30 ms. Ese retardo da tiempo al emisor, que es semidúplex, a volver
  a recepción.
- **El ACK nunca se confirma**, y si se pierde lo cubre el reintento del emisor.
- **Un duplicado dirigido a mí con `ACK_REQ` se vuelve a confirmar** (con un
  límite: 1 ACK por `(src,msgId)` cada 1 s), pero **no** se vuelve a entregar.
  Sin esto, un ACK perdido acabaría en "fallo de entrega" aunque el mensaje
  hubiera llegado.

### 7.2. Temporizador de ACK

```
E1:  T_ack = T_aire(trama) + T_giro + T_aire(ACK) + margen          (margen 100 ms)
E2:  T_ack = (ttl+1)·(T_aire(trama) + 2·CW) + (ttl+1)·(T_aire(ACK) + 2·CW) + margen
```

`CW` es la ventana de contención de reenvío (§8.2). Con NORMAL: DATA de 32
caracteres en E1 → ≈ **630 ms**. ALERT con `ttl` 3 en E2 → ≈ **8.7 s**. Cuando
llega el primer ACK de un destino, su `hopsRx` se guarda en la tabla de vecinos
y los siguientes timeouts usan `hopsRx+1` en lugar de `ttl+1` (timeout
adaptativo).

### 7.3. Reintentos: *backoff* exponencial + jitter

Entre el intento *k* y el *k+1* (*k* = 1, 2, …):

```
espera_k = T_ack + min(B0 · 2^(k−1), B_max) + U(0, B0)      B0 = T_aire(trama) + 100 ms
```

| Clase | Intentos | B_max | Al agotarse |
|---|---|---|---|
| DATA unicast | 1 + 3 reintentos | 8 s | `EV FALLO msgId=…` |
| PING (de `RANGE`) | 1, sin reintento | — | Cuenta como pérdida |
| **ALERT** | 1 + 7 rápidos, luego **1 cada 30 s** | 30 s | Sigue **hasta recibir ACK, un CANCEL local o 10 min**. Nunca se rinde en silencio: `EV ALERTA_SIN_ACK` en cada ciclo lento. |

- Los reintentos **conservan el `msgId`**.
- El `U(0,B0)` descorrelaciona a dos nodos que colisionaron.
- La fuente de azar se **inyecta** (en la placa `esp_random`, en los tests un
  generador determinista con semilla).

### 7.4. Detección de duplicados: caché de vistos `(src, msgId)`

- Anillo de **128 entradas** `{src u16, msgId u16, t_ms u32}`, con una vida de
  **15 min**. Tiene que ser mayor que la ventana de reintentos de ALERT (10
  min), porque si no un reintento tardío se entregaría dos veces.
- Si la caché se llena se reemplaza la entrada **más antigua**.
- La búsqueda es lineal (128 × 8 B = 1 KiB, que es de sobra en un S3).
- Usa aritmética *unsigned* con `millis()`, con lo que sobrevive al
  desbordamiento de los 49 días (hay test).
- Guarda **todos** los tipos, ACK y PONG incluidos. En E2 es lo que corta los
  bucles.

### 7.5. Acceso al canal: escucha antes de hablar (CAD)

- Antes de cada TX, `scanChannel()` (CAD del SX1262).
- Si hay preámbulo en el aire, se espera `U(1, 4) · T_aire(PING)` y se
  reintenta, hasta 5 veces. Después se transmite de todos modos (en ALERT) o se
  devuelve a la cola (resto).
- Limitación conocida: el CAD detecta **preámbulos**, no cargas a medias. El
  preámbulo de 16 símbolos sube la probabilidad de cazarlos.

### 7.6. Presupuesto de ocupación

- Ventana deslizante de 60 s sumando `T_aire` de lo transmitido.
- El tráfico no-ALERT se **retrasa** si superaría el 10 %; ALERT, ACK y
  reenvíos de ALERT están exentos.
- `RANGE` ajusta su intervalo mínimo a esto: en NORMAL, PING + PONG ≈ 400 ms,
  así que el intervalo mínimo es ≈ 4 s.
- `STATUS` muestra el % del último minuto.

---

## 8. Multi-salto (E2): inundación controlada

### 8.1. Reglas de recepción y reenvío (en este orden)

```
al recibir trama válida T (con rssi, snr):
 1. si T.src == miId:
        si T.msgId ∈ mis_enviados: es mi eco → ACK IMPLÍCITO si era broadcast; FIN
        si no: EV ID_DUPLICADO; FIN
 2. si (T.src, T.msgId) ∈ vistos:
        si tengo un reenvío pendiente de T → CANCELARLO (supresión: otro ya lo repitió)
        si T.dst == miId y ACK_REQ → re-ACK (limitado, §7.1)
        FIN
 3. vistos.añadir(T.src, T.msgId)
 4. si T.hops == 0 → tabla_vecinos[T.src] = {rssi, snr, ahora}    (vecino directo)
 5. entregar si T.dst == miId  o  T.dst == 0xFFFF  o  (T.dst == 0xFFFE y rol == PASARELA)
 6. si T.dst == miId: si ACK_REQ → encolar ACK; FIN            (el destino no reenvía)
 7. reenviar si  RELAY == ON  y  T.ttl > 0  y  T.hops < HOPS_MAX(7):
        T' = T con ttl−1, hops+1, CRC recalculado
        retardo = D_snr(snr) + U(0, CW)
        encolar T' no-antes-de ahora+retardo, con CAD (§7.5)
```

- **No reenviar lo propio** (regla 1) ni lo ya visto (regla 2): esto garantiza
  que **cada nodo transmite cada mensaje como mucho una vez**. El coste de una
  inundación es ≤ N transmisiones.
- **Nunca se cambian** `src`, `dst`, `msgId` ni la carga, de forma que la
  deduplicación funciona extremo a extremo.
- El `ACK de vuelta` es una trama normal (`dst` = origen) que **se inunda
  igual** con su propio `msgId`. No hace falta recordar caminos.
- **Broadcast (`BCAST`, `PING *`):** sin `ACK_REQ`. Si el origen oye a un
  vecino reenviarlo (regla 1), lo da por **confirmado implícitamente**. Si no
  lo oye, lo reintenta una vez. Es lo mismo que hace Meshtastic.
- **`PING *`** (a 0xFFFF): todos los que lo oyen contestan PONG tras
  `U(0, 2 s)`. Sirve de **descubrimiento de vecinos** sin un tipo HELLO aparte.

### 8.2. Retardo de reenvío ponderado por SNR

```
CW     = 2 · T_aire(T)                                       (NORMAL, ALERT: ≈ 440 ms)
D_snr  = CW · clamp((snr + 20) / 30, 0, 1)                   (snr −20 dB → 0 ; +10 dB → CW)
```

El que oyó **peor** (más lejos) repite **antes** y llega más lejos. Los
cercanos, que oyen peor la señal débil pero sí oyen ese reenvío, **se callan**
(regla 2). Es la idea de la "inundación gestionada" de Meshtastic.

- Rol `REPETIDOR` (nodo fijo en altura): `D_snr = 0`, siempre repite primero.
- Cola de reenvíos pendientes de **8 entradas**. Si se llena, se descarta el
  más antiguo que no sea ALERT.

### 8.3. De E1 a E2 sólo con configuración

| | E1 (defecto inicial) | E2 |
|---|---|---|
| `TTL` | 0 | 3 (máx. 7) |
| `RELAY` | OFF | ON |
| Formato / firmware | igual | igual |
| Timeouts | fórmula E1 | fórmula E2 (se elige sola según el `ttl`) |

Con `TTL 0` las tramas salen con `ttl = 0`, así que aunque un nodo tenga
`RELAY ON` nadie las repite: los dos modos conviven en la misma red.

### 8.4. Límites conocidos de la inundación

- Escala bien a **decenas** de nodos con poco tráfico. Cada mensaje cuesta
  hasta N transmisiones y, con NORMAL, una ALERT ocupa ≈ 220 ms × N.
- Más allá hacen falta rutas *next-hop* (E3: aprender el siguiente salto de los
  ACK, como Meshtastic 2.6) o pasarse a Meshtastic (§15).
- No hay protección frente a nodos maliciosos hasta que llegue el flag `AUTH`
  (§16).

---

## 9. Consola serie

115200 baudios, 8N1, por la **UART0 (CP2102)**. Una orden por línea
(terminador `\n` o `\r\n`), **sin distinguir mayúsculas y minúsculas**. Los IDs
van en hexadecimal de 4 dígitos, y `*` significa broadcast.

- Respuestas **deterministas**, pensadas para scripts: `OK …` o `ERR <motivo>`.
- Sucesos asíncronos con el prefijo `EV …`.
- Las medidas de `RANGE` salen como `R,…` (CSV).

| Orden | Efecto |
|---|---|
| `HELP` | Lista las órdenes |
| `ID` · `ID <hex4>` · `ID AUTO` | Muestra ID, MAC y origen (MAC/NVS) · fija el override · lo borra (§6) |
| `PING <dst>` · `PING *` · `PING <dst> <bytes>` | Un PING: imprime RTT, RSSI/SNR de ida (del PONG) y de vuelta (local) y `hops` · descubrimiento de vecinos · con relleno de carga |
| `SEND <dst> <texto>` | DATA con `ACK_REQ`, y luego `EV ACK msgId=… rtt=… intentos=…` o `EV FALLO …` |
| `BCAST <texto>` | DATA a 0xFFFF sin ACK (en E2 con ACK implícito) |
| `ALERT <1\|2\|C> [lat lon] [PRUEBA]` | Envía una ALERT (§5.4). Es para probar el camino de las alertas antes de que exista la pasarela. |
| `STATUS` | Uptime, ID, radio, TTL/RELAY, contadores (tx, rx, ack, reintentos, fallos, duplicados, reenviados, suprimidos, rechazos por motivo), % de airtime en el último minuto, tabla de vecinos (ID, RSSI, SNR, antigüedad, `hops`) |
| `RADIO` | Muestra freq/SF/BW/CR/sync/preámbulo/potencia y el airtime de una ALERT |
| `RADIO FREQ <MHz>` · `RADIO BW <kHz>` · `RADIO CR <5-8>` · `RADIO SYNC <hex2>` · `RADIO PRE <n>` | Cambia un parámetro. Valida la banda (§3) y rechaza, por ejemplo, 915.0 con BW 500. |
| `RADIO PRESET <RAPIDO\|NORMAL\|LARGO\|LAB125>` | Aplica un preset (§4.1) |
| `RADIO SAVE` · `RADIO LOAD` · `RADIO DEFECTO` | Guarda en NVS · recarga · vuelve a los valores de fábrica |
| `SF <7-12>` · `PWR <-9..22>` | Atajos. `PWR` avisa si supera +14 dBm. |
| `TTL <0-7>` · `RELAY ON\|OFF` | Modo E1/E2 (§8.3) |
| `RANGE <dst> [n] [ms]` | Prueba de alcance: *n* PINGs (por defecto infinitos) cada *ms* (mínimo automático, §7.6). Por cada uno imprime `R,punto,seq,ok,rtt_ms,rssi_ida,snr_ida,rssi_vuelta,snr_vuelta,hops`. |
| `RANGE STOP` · `RANGE INFORME` | Para · resumen **por punto**: enviados, recibidos, pérdidas %, RSSI/SNR mín/media/máx en los dos sentidos, RTT medio |
| `PERDIDA <0-100>` | **Sólo depuración:** descarta en RX ese % de tramas para ejercitar los reintentos en el banco. Vuelve a 0 al reiniciar. |

**Aviso importante:** un cambio de SF/BW/CR/freq/sync en **un solo** nodo lo
deja **sordo** para el resto. El OLED y `RADIO` muestran siempre el preset
activo para poder comparar las dos placas de un vistazo.

---

## 10. OLED, botón PRG y LED (pruebas de campo sin PC)

**OLED** (U8g2, `U8G2_SSD1306_128X64_NONAME_F_HW_I2C` con reset 21, SCL 18 y
SDA 17; se compila sólo con `-D GEOLORA_OLED=1`):

- Antes de iniciarlo: `Vext` LOW y 50 ms de espera.
- Se refresca a ≤ 4 Hz y **nunca** justo antes de un TX pendiente: el RX va por
  interrupción al búfer del SX1262, así que un `sendBuffer()` de ≈ 25 ms no
  hace perder tramas.
- Páginas:
  1. **Estado:** ID, preset, freq, potencia, TTL/RELAY, % de airtime.
  2. **Enlace:** RSSI/SNR de ida y de vuelta del último PONG/ACK, RTT, pérdidas
     % de la racha, **número de punto** y cuenta `recibidos/enviados`.
  3. **Tráfico:** contadores de `STATUS` y vecinos.
  4. **Alertas:** última ALERT recibida (origen, token, lat/lon, antigüedad, si
     es `PRUEBA`). Al llegar una ALERT se salta a esta página y se queda ahí
     10 s.

**Botón PRG (GPIO0)**, con detector de gestos puro (antirrebote de 50 ms, igual
que `lib/panic` del llavero, pero **reimplementado** dentro de `lora/`):

| Gesto | Fuera de RANGE | Dentro de RANGE |
|---|---|---|
| Pulsación corta | Un `PING` al último vecino (o `PING *` si no hay ninguno) | **Marca el punto siguiente** (waypoint), que queda en el CSV y en el OLED |
| Doble pulsación | Página siguiente del OLED | Página siguiente del OLED |
| Larga (≥ 1.5 s) | **Inicia `RANGE`** hacia el último vecino | **Para `RANGE`** |

Los resultados de `RANGE` se guardan en un **anillo en RAM de 512 medidas**. De
vuelta en casa, `RANGE INFORME` imprime el resumen por punto. La RAM se pierde
al reiniciar: guardarlo en LittleFS se deja para E1.x.

**LED (GPIO35):** destello de 20 ms en cada TX y doble destello en cada RX
válido. Con una ALERT recibida parpadea a 4 Hz durante 10 s.

**Procedimiento típico sin PC:** B se queda fija y encendida (contesta PONG
sola). A va en la mano: pulsación larga → `RANGE`, y en cada sitio, pulsación
corta → punto nuevo. Se apunta en papel o en el móvil qué lugar es cada número
de punto.

---

## 11. Configuración persistente (NVS)

Espacio `geolora` de `Preferences`, con las claves `id`, `freq`, `sf`, `bw`,
`cr`, `sync`, `pre`, `pwr`, `ttl`, `relay` y `rol`.

- Al arrancar se leen y se validan con las mismas funciones puras que la
  consola. Un valor corrupto → **defecto** + `EV NVS_INVALIDA`.
- Los cambios de la consola se aplican al instante pero **sólo** se guardan con
  `RADIO SAVE` (o con `ID <hex4>`, que se guarda directamente). Así un
  experimento no sobrevive a un reinicio por accidente.

---

## 12. Arquitectura de código

### 12.1. Árbol

```
lora/
├── platformio.ini          envs: heltec_v3 (placa) y native (tests en el PC)
├── DISENO.md               este documento
├── README.md               cómo compilar/flashear/usar (se escribe al implementar E1)
├── lib/geolora/            C++17 PURO: sin <Arduino.h>, sin millis(), sin radio
│   ├── crc16.h/.cpp          CRC-16/CCITT-FALSE
│   ├── paquete.h/.cpp        codificar/decodificar/validar la cabecera (§5), tipos, flags, direcciones
│   ├── carga.h/.cpp          cargas PING/PONG/ACK/ALERT; código ↔ token GEO-EXPO
│   ├── identidad.h/.cpp      derivarId(mac), reservados, parseo de hex4
│   ├── radio_params.h/.cpp   presets, validación de banda, airtimeMs(), presupuesto de ocupación
│   ├── vistos.h/.cpp         caché (src, msgId)
│   ├── motor.h/.cpp          Nodo: ACK, reintentos, supresión/reenvío, colas, vecinos, eventos
│   ├── gestos.h/.cpp         botón: corta/doble/larga con antirrebote
│   └── consola.h/.cpp        línea de texto → struct Orden (sólo parsea, no imprime)
├── src/                    capa Arduino/ESP32 (fina)
│   ├── placa.h               pines V3 (§2.1) y, tras #if, V4
│   ├── radio.h/.cpp          RadioLib SX1262: begin, RX por IRQ DIO1, CAD, TX, RSSI/SNR, aplicar params
│   ├── ajustes.h/.cpp        NVS (Preferences)
│   ├── pantalla.h/.cpp       OLED U8g2 (#if GEOLORA_OLED)
│   └── main.cpp              setup/loop: pega radio ↔ motor ↔ consola ↔ OLED/botón/LED
└── test/                   Unity, `pio test -e native`
    ├── test_paquete/  test_carga/  test_identidad/  test_radio_params/
    ├── test_vistos/   test_motor_p2p/  test_motor_malla/
    └── test_consola/  test_gestos/
```

### 12.2. El motor, puro y con el tiempo y el azar inyectados

```cpp
namespace geolora {
class Motor {
 public:
  Motor(const ConfigNodo& cfg, uint32_t (*azar)(void*), void* ctxAzar);
  // --- entradas ---
  uint16_t enviar(uint16_t dst, Tipo t, const uint8_t* carga, uint8_t len,
                  bool ackReq, uint8_t flagsExtra, uint32_t ahoraMs);   // devuelve msgId
  void alRecibir(const uint8_t* trama, uint8_t len, int16_t rssiDbm,
                 int8_t snrQ4, uint32_t ahoraMs);
  void tick(uint32_t ahoraMs);                         // timeouts, reintentos, reenvíos
  // --- salidas ---
  bool siguienteTx(uint32_t ahoraMs, TramaSalida& out); // la capa radio pregunta
  void txHecha(uint32_t ahoraMs, uint32_t airtimeMs);   // alimenta el presupuesto
  bool siguienteEvento(Evento& ev);                     // ENTREGADO, ACK, FALLO, PONG, ALERTA, ...
};
}
```

Sin memoria dinámica: colas y cachés son arrays de tamaño fijo, lo que en
tiempo de compilación deja ver cuánta RAM se gasta.

El mismo `Motor` se instancia **N veces** en `test_motor_malla`, sobre un canal
simulado con una matriz de alcance y pérdidas, así que toda la lógica de E2 se
prueba en el PC con cualquier número de nodos.

### 12.3. Capa radio (`src/radio.cpp`)

- `SX1262 radio = new Module(8, 14, 12, 13)` sobre el `SPI` de la variante
  (SCK 9, MISO 11, MOSI 10).
- `begin(freq, bw, sf, cr, sync, pwr, pre, 1.8f /*TCXO*/, false /*DC-DC*/)`,
  luego `setDio2AsRfSwitch(true)`, `setCRC(2)` y `setRxBoostedGainMode(true)`.
- Si `begin()` falla: `ERR RADIO <código>` por consola y en el OLED, y el
  firmware sigue vivo (consola útil) **sin** transmitir. Causas típicas: TCXO
  o pin BUSY mal puestos.
- **RX:** `setPacketReceivedAction(isr)`. La ISR sólo levanta una bandera (nada
  de SPI dentro de la ISR). El `loop()` llama a `readData()`, `getRSSI()` y
  `getSNR()`, se lo pasa al motor y vuelve a `startReceive()`.
- **TX:** `scanChannel()` y después `transmit()` bloqueante (≤ 1.7 s en el
  peor caso de LARGO), y luego `startReceive()`. Bloquear es aceptable en E1.
  Si en E2 molesta, se pasa a `startTransmit()` más la bandera de fin.

### 12.4. `loop()`

Sin `delay()`. Cada vuelta hace:

1. bandera RX → motor;
2. `motor.tick()`;
3. `siguienteTx` → CAD → TX;
4. eventos → consola/OLED/LED;
5. bytes de `Serial` → parser;
6. gestos del botón;
7. refresco del OLED si toca.

### 12.5. `platformio.ini` (esbozo)

```ini
[platformio]
default_envs = heltec_v3

[env:heltec_v3]
platform        = espressif32@6.9.0          ; arduino-esp32 2.0.17, igual que el llavero
board           = heltec_wifi_lora_32_V3     ; ya define ARDUINO_USB_MODE=1
framework       = arduino
monitor_speed   = 115200
monitor_dtr     = 0                          ; §2.3: el auto-reset tocaría GPIO0 (PRG)
monitor_rts     = 0
lib_deps =
    jgromes/RadioLib @ 7.7.1                 ; última 7.x (31-may-2026), fijada exacta
    olikraus/U8g2 @ 2.36.18                  ; sólo se usa con GEOLORA_OLED=1
build_flags =
    -D CORE_DEBUG_LEVEL=1
    -D ARDUINO_USB_CDC_ON_BOOT=0             ; D3: Serial = UART0 → CP2102
    -D GEOLORA_OLED=1

[env:native]
platform        = native
test_framework  = unity
build_flags     = -std=gnu++17 -Wall -Wextra
```

Hay que **comprobar al implementar** que RadioLib 7.7.1 compila con
arduino-esp32 2.0.17. RadioLib soporta los núcleos 2.x y 3.x, pero no está
probado en este repo. En `native` no se declaran `lib_deps`, así que ni
RadioLib ni U8g2 entran en los tests.

---

## 13. Plan de pruebas

### 13.1. Nativas (`cd lora && pio test -e native`), sin placa

| Suite | Qué comprueba |
|---|---|
| `test_paquete` | CRC `"123456789"` → 0x29B1. Codificar/decodificar ida y vuelta. **Tramas de oro del §5.5 byte a byte.** Rechazos: magic, versión, `len` incoherente, CRC, `len` > 200, `AUTH`=1, `src` reservado. LE de todos los campos. |
| `test_carga` | Código ↔ token (`ALERT:1`/`ALERT:2`/`CANCEL` exactos). Lat/lon negativos (Bogotá) ida y vuelta sin pérdida a 1e-7. Saturación de `rssiNeg`/`snrQ4`. |
| `test_identidad` | MAC → ID. Caída a `crc16` en reservados. Parseo/validación de `hex4`. |
| `test_radio_params` | `airtimeMs()` contra la tabla del §4.2 (±0.1 ms), LDRO automático, validación de banda (915.0/BW500 → error, 921.5 → ok, 927.75/BW500 → ok, 927.8/BW500 → error), presupuesto de ocupación. |
| `test_vistos` | Insertar/buscar, caducidad a los 15 min, reemplazo del más antiguo, desbordamiento de `millis()` y de `msgId`. |
| `test_motor_p2p` | ACK a la primera. ACK perdido → reintento con el mismo `msgId` → re-ACK sin segunda entrega. Calendario de *backoff* con semilla fija (tiempos exactos). Fallo tras N. ALERT: ciclo lento y parada por CANCEL. PING→PONG con RTT. |
| `test_motor_malla` | Canal simulado. **Línea A–B–C:** con `TTL 0`, A→C falla; con `TTL 2`, entrega y ACK de vuelta. **Anillo/malla de 6:** cada nodo transmite cada `(src,msgId)` ≤ 1 vez, 0 entregas duplicadas, supresión efectiva (menos de N transmisiones con vecinos densos). Broadcast con ACK implícito. Eco propio descartado. ID duplicado detectado. Mezcla de nodos E1 y E2. |
| `test_consola` | Cada orden del §9: válidas, fuera de rango, IDs, `*`, mayúsculas/minúsculas, espacios y `\r\n`. |
| `test_gestos` | Corta, doble, larga y rebotes, con tiempos simulados (igual que `test_panic`). |

### 13.2. Banco (2 placas a ≥ 2 m, `PWR 0`, preset NORMAL)

1. **Arranque:** 10 reinicios por placa. La consola responde y la radio da
   `OK` en `RADIO`. Buena práctica heredada de I12, aunque aquí no hay BLE.
2. **ID:** las dos placas tienen IDs distintos y coinciden con sus MAC.
   `ID 00AA` / `ID AUTO` persisten tras reiniciar.
3. **PING:** 20 PINGs. RTT ≈ T_aire(PING) + T_aire(PONG) + giro ≈ 430 ms en
   NORMAL. Anotar la desviación.
4. **SEND con ACK:** 100 mensajes: 100 % confirmados y **0 duplicados**
   entregados en B (contador de `STATUS`).
5. **Reintentos:** en B, `PERDIDA 50`. En A, 50 SEND. Se ven los reintentos
   con *backoff* creciente, en B hay duplicados **descartados y re-ACKeados**,
   y en A hay ≥ 90 % de éxito.
6. **Fallo:** apagar B. `SEND` → 4 intentos con los tiempos del §7.3 →
   `EV FALLO`.
7. **ALERT:** `ALERT 2 4.60971 -74.08175 PRUEBA` → B muestra el token
   `ALERT:2` y la posición en el OLED y confirma. Con B apagada, A entra en el
   ciclo lento; `ALERT C` lo corta.
8. **Coherencia de radio:** `SF 9` sólo en A → deja de haber enlace. `SF 11` →
   vuelve. Así se comprueba el aviso del §9.
9. **E2 con 2 placas** (lo único posible sin un tercer nodo): las dos con
   `RELAY ON` y `TTL 3`; `BCAST` desde A. B lo entrega y lo repite **una vez**;
   A oye su eco → ACK implícito y **no** lo repite. No hay tormenta.

### 13.3. Campo: alcance (sin PC en el lado móvil)

- **Montaje:** B fija en altura (ventana o terraza), con batería o powerbank y
  una ubicación conocida. A, móvil, con powerbank. Mismo preset en las dos,
  comprobado en el OLED.
- **Recorrido:** puntos a ≈ 100 m, 250 m, 500 m, 1 km, 2 km… más allá
  mientras haya respuestas. En cada punto, 20 PINGs (`RANGE` con marca de
  punto) con **NORMAL a +14 dBm**, y repetir con **LARGO a +22 dBm** en los
  puntos límite.
- **Planilla** por punto: n.º de punto, lugar, distancia (medida en el mapa),
  línea de vista (sí/parcial/no), altura aproximada, preset y potencia,
  pérdidas %, RSSI/SNR medios de ida y de vuelta, RTT medio. Al volver:
  `RANGE INFORME` > planilla.
- **Criterio de "enlace usable":** pérdidas ≤ 10 % con SNR medio ≥ (límite del
  SF + 5 dB). Para NORMAL (SF11, límite −17.5 dB): SNR ≥ −12.5 dB.
- **Resultado esperado:** la distancia máxima usable por preset y potencia en
  el entorno real, que es lo que decide si hace falta E2 (repetidores) para el
  caso de uso.

### 13.4. Campo: E2 (cuando haya un 3.er nodo)

A y C fuera de alcance directo (comprobado con `PING`, 0/20), y B en medio con
`RELAY ON`. Pruebas:

- A→C `SEND` y ALERT con ACK, esperando `hops` = 1 en el ACK;
- `PING *` desde A: se descubre C a 1 salto;
- `STATUS` en B: reenviados y suprimidos coherentes.

---

## 14. Relación con GEO-EXPO y pasarela prevista (fuera de alcance)

- **Hoy:** el llavero (DevKit V1, BLE, contrato congelado) no sabe nada de
  LoRa, y esta red tampoco depende del llavero.
- **Previsto:** nodo con **rol `PASARELA`** (una Heltec con BLE, Wi-Fi y
  LoRa):
  - (a) **Llavero → malla:** hace de *central* BLE, se conecta a
    `GEOEXPO-ALERT` (servicio `6E400001-…`), se suscribe a TX (`6E400003-…`)
    y convierte cada token en una ALERT LoRa hacia `0xFFFE`.
  - (b) **Malla → exterior:** recibe las ALERT dirigidas a `0xFFFE` y las
    entrega a la app o a Internet con **los mismos tokens** y el mismo
    terminador de línea.
  - Si hay varias pasarelas, todas confirman; el origen se queda con el primer
    ACK y los demás son inocuos.
- **Riesgo a tener en cuenta:** meter NimBLE en el S3 junto a RadioLib cambia
  el binario. I12 era de la DevKit, pero la lección se mantiene: probar 10
  arranques en cada cambio.
- **Seguridad antes de producción:** sin `AUTH`, **cualquiera con una radio
  LoRa puede falsificar una ALERT**. E3 tiene que añadir una etiqueta de
  autenticación en la trama (p. ej. AES-CMAC o SipHash truncado a 4 B, con
  clave de red provisionada por consola) y un anti-repetición basado en
  `(src,msgId)` + tiempo.

---

## 15. Alternativa madura: Meshtastic

[Meshtastic](https://meshtastic.org) es firmware abierto (GPLv3) de mensajería
en malla LoRa. Soporta oficialmente la Heltec V3 (y la V4). Se flashea desde el
navegador y tiene apps para Android/iOS.

| | **Meshtastic** | **Firmware propio (este diseño)** |
|---|---|---|
| Madurez | Años de uso y miles de nodos. Inundación gestionada por SNR, límite de saltos 0–7, ACK implícito en broadcast, rutas *next-hop* para mensajes directos desde la 2.6 | Por construir y probar |
| Seguridad | Canal cifrado con PSK (AES) y PKI en mensajes directos | Ninguna hasta E3 |
| Apps/integración | Apps móviles, API serie/BT/Wi-Fi, CLI Python, MQTT. El **módulo Detection Sensor** manda un mensaje cuando cambia un GPIO: **un botón de pánico sin escribir firmware**. | Consola propia. Integración con GEO-EXPO a medida (tokens exactos) |
| Región Colombia | **ANZ (915–928 MHz)**, que coincide con la ANE. Sus presets por defecto (*LongFast*, 250 kHz) tienen el mismo matiz normativo que nuestro `LAB125`; hay presets de 500 kHz (*ShortTurbo*). | BW 500 kHz por defecto (§3) |
| Control del protocolo | Ninguno salvo *fork* (C++ grande, protobuf). Cabecera de 16 B y carga máx. ≈ 237 B | Total: 14 B de sobrecarga y ALERT de 25 B |
| Fiabilidad de ALERT | Mensaje directo: **máx. 3 retransmisiones** y después NAK. Broadcast: *best effort* + ACK implícito. No hay política de "insistir hasta confirmación". | Política específica (§7.3): insistir hasta 10 min o CANCEL |
| Airtime | *LongFast* (SF11/BW250) ≈ 0.5–1 s por mensaje corto. Canal compartido con extraños si se usa el canal por defecto. | ALERT ≈ 220 ms en NORMAL, canal y sync propios |
| Coste de adopción | Horas | Semanas (E1 + E2) |

**Recomendación:**

1. Seguir con el **firmware propio para E1**. Es pequeño, se prueba en nativo
   como el resto del repo, transporta los tokens GEO-EXPO tal cual, tiene una
   política de ALERT a medida y enseña cómo se comporta el enlace (las medidas
   de `RANGE` sirven para cualquier opción).
2. **Evaluar Meshtastic en paralelo** como referencia para la "red de más
   puntos": flashear las dos V3 durante una tarde (región ANZ, canal privado
   con PSK propio y un preset de 500 kHz), repetir el recorrido del §13.3 y
   comparar.
3. Si E2 llega a necesitar más de ~10–20 nodos, cifrado y apps a corto plazo,
   **Meshtastic es probablemente la mejor base**. Nuestro papel sería entonces
   una pasarela que traduzca los tokens a mensajes de texto de un canal
   privado (`ALERT:1;<lat>;<lon>`) mediante su API serie o MQTT.
4. Otra opción a mirar en ese momento: **MeshCore** (enrutado con repetidores
   dedicados; Heltec también la anuncia como compatible).

---

## 16. Riesgos y preguntas abiertas

1. **Normativa:** confirmar con la ANE la lectura del §3, sobre todo si LoRa a
   500 kHz cuenta como "modulación digital". Para un producto, homologación
   CRC.
2. **D1–D4** (§0) necesitan el visto bueno de la sesión principal.
3. **RadioLib 7.7.1 + arduino-esp32 2.0.17:** compilarlo como primer paso de
   E1.
4. **Potencia real de la V3:** hay informes de que da menos de lo nominal a
   +22 dBm. Se medirá de forma indirecta (RSSI a distancia fija, `PWR 14` frente
   a `PWR 22`).
5. **Seguridad:** sin `AUTH` no hay protección contra la suplantación. Es
   **obligatorio** antes de cualquier uso real de las alertas.
6. **E2 con 2 placas** no demuestra el multi-salto real: hace falta un tercer
   nodo con SX1262 (otra V3/V4 o cualquier placa compatible con RadioLib).
7. **Consumo:** E1/E2 escuchan siempre (el S3 activo más el SX1262 en RX, del
   orden de decenas de mA). Los nodos solares o con batería de larga duración
   son de E3.

---

## 17. Fuentes consultadas (18-sep-2026)

- ANE, **Res. 105 de 2020**, Anexo 1 (Tabla 1.2 y §3.6.1.1, banda 915–928 MHz):
  [PDF en ane.gov.co](https://www.ane.gov.co/Documentos%20compartidos/ArchivosDescargables/noticias/RESOLUCI%C3%93N%20No%20000105%20DE%2027-03-2020(1).pdf)
  · [compilación MinTIC](https://normograma.mintic.gov.co/mintic/compilacion/docs/resolucion_ane_0105_2020.htm)
- ANE, *Análisis de impacto normativo sobre espectro de uso libre para
  medidores inteligentes* (915–928 MHz como única porción de uso libre):
  [PDF](https://www.ane.gov.co/Documentos%20compartidos/ArchivosDescargables/noticias/An%C3%A1lisis%20de%20impacto%20normativo%20sobre%20espectro%20de%20uso%20libre%20para%20medidores%20inteligentes%20de%20consumo.pdf)
- ANE, **Res. 28 de 2026** (banda de 900 MHz):
  [compilación MinTIC](https://normograma.mintic.gov.co/mintic/compilacion/docs/resolucion_ane_0028_2026.htm)
  · [resumen Eleos Compliance](https://www.eleoscompliance.com/en/article/colombia-colombia-updates-900-mhz-band-for-flexible-use)
- FCC 47 CFR 15.247 (el modelo del texto colombiano) y LoRa a 500 kHz:
  [eCFR 15.247](https://www.ecfr.gov/current/title-47/chapter-I/subchapter-A/part-15/subpart-C/subject-group-ECFR2f2e5828339709e/section-15.247)
  · [NodakMesh: el mínimo de 500 kHz y las mallas LoRa](https://nodakmesh.org/blog/fcc-15-247-500khz-lora)
- Pines de la V3:
  [arduino-esp32 `pins_arduino.h`](https://github.com/espressif/arduino-esp32/blob/master/variants/heltec_wifi_lora_32_V3/pins_arduino.h)
  · [Meshtastic `variant.h` V3](https://github.com/meshtastic/firmware/blob/master/variants/esp32s3/heltec_v3/variant.h)
  · [ropg/heltec_esp32_lora_v3](https://github.com/ropg/heltec_esp32_lora_v3)
  (CP2102, Vext, batería)
  · [Heltec, página de la V3](https://heltec.org/project/wifi-lora-32-v3/)
- V4:
  [Meshtastic `variant.h` V4](https://github.com/meshtastic/firmware/blob/master/variants/esp32s3/heltec_v4/variant.h)
  · [OpenELAB, V3 frente a V4](https://openelab.io/blogs/learn/heltec-wifi-lora-32-v3-vs-v4-differences-and-buying-guide)
  · [RadioLib, discusión #1665 (V4 que transmite y nadie oye)](https://github.com/jgromes/RadioLib/discussions/1665)
- Placa PlatformIO `heltec_wifi_lora_32_V3` (platform-espressif32 v6.9.0,
  `ARDUINO_USB_MODE=1`, sin `CDC_ON_BOOT`):
  [JSON](https://github.com/platformio/platform-espressif32/blob/v6.9.0/boards/heltec_wifi_lora_32_V3.json)
- RadioLib (7.7.1, 31-may-2026): [releases](https://github.com/jgromes/RadioLib/releases)
  · U8g2 2.36.18 (registro de PlatformIO)
- Meshtastic:
  [algoritmo de malla](https://meshtastic.org/docs/overview/mesh-algo/)
  · [región por país (Colombia = ANZ)](https://meshtastic.org/docs/configuration/region-by-country/)
  · [configuración LoRa](https://meshtastic.org/docs/configuration/radio/lora/)
