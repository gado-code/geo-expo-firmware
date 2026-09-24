# Ivy LoRa · nodo (E1 punto a punto → E2 multi-salto)

Subproyecto **independiente** del firmware del llavero. Dos placas **Heltec
WiFi LoRa 32 V3** (ESP32-S3 + SX1262) se hablan por LoRa a 915–928 MHz, primero
punto a punto (**E1**) y, cambiando dos parámetros, en malla por inundación
controlada (**E2**).

- El diseño completo, con las decisiones y sus motivos, está en **[`DISENO.md`](DISENO.md)**.
- La tabla de casos de prueba, en **[`PRUEBAS.md`](PRUEBAS.md)**.

> **No comparte binario ni `platformio.ini` con el llavero.** Se hizo así a
> propósito: el firmware del llavero arrastra la incidencia **I12**, que
> depende del *layout* del binario, y nada de lo que se compile aquí puede
> reabrirla.

---

## 0. Estado

| Parte | Estado |
|---|---|
| Lógica de red (`lib/geolora`) | ✅ **197 pruebas automáticas pasando en el PC** |
| Firmware del nodo (`src/`) | ✅ **compila** (RAM 13,4 %, Flash 11,1 %) |
| Probado en placa | ❌ **nada todavía**: no hay Heltec conectada |

Lo que falta es enchufar las dos placas y hacer la §13.2 del diseño (banco) y
la §13.3 (alcance). Ver `PRUEBAS.md`.

---

## 1. Qué hace un nodo

- Manda y recibe **DATA** (texto), **PING/PONG** (medida de enlace con RTT,
  RSSI y SNR de los dos lados) y **ALERT**, que transporta los tokens del
  llavero (`ALERT:1`, `ALERT:2`, `CANCEL`) con posición opcional.
- **Fiabilidad**: ACK, reintentos con *backoff* exponencial y jitter, detección
  de duplicados y escucha antes de hablar (CAD).
- **Presupuesto de aire autoimpuesto**: como mucho el 10 % en cualquier ventana
  de 60 s para todo lo que no sea una alerta.
- **Prueba de alcance de campo sin PC**, con el botón PRG y el OLED, guardando
  hasta 512 medidas en RAM y con resumen por punto.
- **Las dos placas llevan el mismo binario**: las distingue el ID, que sale de
  los dos últimos bytes de la MAC.

---

## 2. Compilar, probar y flashear

Todo se hace **desde esta carpeta**:

```bash
cd lora
```

Pruebas de la lógica, en el PC y sin placa (unos 15 s):

```bash
pio test -e native
```

Compilar el firmware del nodo:

```bash
pio run -e heltec_v3
```

Flashear. Las dos placas llevan lo mismo, así que es el mismo comando cambiando
el puerto (en Windows `COMx`, en Linux `/dev/ttyUSBx`):

```bash
pio run -e heltec_v3 -t upload --upload-port COM5
pio device monitor -e heltec_v3 --port COM5
```

> El puente USB-serie de la V3 es un **CP2102**, el mismo chip que la DevKit del
> llavero, así que en Windows necesita el mismo driver de Silicon Labs.
>
> Y el mismo aviso del llavero: **`monitor_dtr = 0` y `monitor_rts = 0`** ya
> están puestos, porque el DTR baja GPIO0, que aquí es el botón **PRG**.
>
> Desde Claude Code `pio device monitor` no funciona (necesita una TTY real):
> usa `../tools/hw.py`, que también sirve para estas placas.

Sin pantalla, si no la quieres o estorba: `-D GEOLORA_OLED=0`.

---

## 3. Primer arranque: que se vean las dos placas

1. Flashea las dos y abre un monitor en cada una.
2. Apunta el **ID** que dice el banner de cada una (4 cifras hexadecimales,
   sacadas de la MAC). Digamos que son `1A2B` y `3C4D`.
3. Comprueba que las dos tienen **los mismos parámetros de radio**:

   ```
   RADIO
   ```

   > ⚠️ Cambiar SF, BW, CR, frecuencia o sync en **una sola** placa la deja
   > **sorda** para la otra. Si dejan de oírse, es lo primero que hay que mirar.

4. Desde `1A2B`, descubre quién hay cerca:

   ```
   PING *
   ```

   Debe contestar `EV PONG de=3C4D ... rtt=... rssi_ida=... snr_ida=...`.
   *ida* es lo que midió la **otra** placa; *vuelta*, lo que mide esta.

5. Manda un texto con confirmación:

   ```
   SEND 3C4D hola
   ```

   En `3C4D` sale `EV DATOS de=1A2B ... texto="hola"`, y en `1A2B`
   `EV ACK de=3C4D msgId=... rtt=... intentos=1`.

**Pon las placas a 2 metros o más y baja la potencia** (`PWR 0`) para las
pruebas de banco: dos SX1262 pegados se saturan y las medidas no valen nada.

---

## 4. Prueba de alcance (lo que se hace en la calle)

Una placa se queda quieta (contesta los PONG sola, sin PC) y la otra va contigo.

**Con PC:**

```
RANGE 3C4D            # PINGs sin fin, al intervalo mínimo que permite el aire
RANGE 3C4D 20 5000    # 20 PINGs, uno cada 5 s
RANGE STOP
RANGE INFORME         # resumen por punto
```

Cada medida sale en CSV, lista para pegar en una hoja de cálculo:

```
R,punto,seq,ok,rtt_ms,rssi_ida,snr_ida,rssi_vuelta,snr_vuelta,hops
R,1,1,1,412,-97,8.5,-95,9.2,0
R,1,2,0,,,,,,
```

**Sin PC**, sólo con el botón PRG y el OLED:

| Gesto | Fuera de RANGE | En RANGE |
|---|---|---|
| Corta | `PING` al último vecino (o a todos) | **Marca el punto siguiente** |
| Doble | Página siguiente del OLED | Página siguiente del OLED |
| Larga (≥ 1,5 s) | **Arranca RANGE** hacia el último vecino | **Para RANGE** |

Procedimiento: pulsación larga para empezar y, en cada sitio donde te pares,
pulsación corta para que las medidas siguientes vayan a un punto nuevo. Apunta
en papel qué lugar es cada número. De vuelta en casa, `RANGE INFORME`.

> Las medidas viven en RAM: **un reinicio las borra**. Guardarlas en la flash
> se dejó para más adelante.

---

## 5. De punto a punto a red de más puntos

No hay que reescribir nada, son dos parámetros (DISENO §8.3):

| | TTL | RELAY |
|---|---|---|
| **E1** punto a punto | `0` | `OFF` |
| **E2** multi-salto | `3` | `ON` |

```
TTL 3
RELAY ON
RADIO SAVE      # para que sobreviva al reinicio
```

Con `TTL 0` tus tramas salen con ttl 0 y **nadie las repite**, aunque el otro
nodo tenga `RELAY ON`: los dos modos conviven en la misma red.

Un nodo fijo en alto que sólo hace de repetidor va mejor con `ROL REPETIDOR`:
reenvía sin el retardo que se aplica por SNR.

---

## 6. Órdenes de la consola

`HELP` las lista todas. Las respuestas son deterministas para poder leerlas con
un script: `OK …`, `ERR <motivo>`, `EV …` para sucesos y `R,…` para las medidas.

Las más usadas:

| Orden | Para qué |
|---|---|
| `ID` · `ID <hex4>` · `ID AUTO` | Ver el ID, fijarlo a mano (se guarda) o volver al de la MAC |
| `PING <dst\|*> [bytes]` | Medir el enlace o descubrir vecinos |
| `SEND <dst> <texto>` · `BCAST <texto>` | Mandar datos, con ACK o a todos |
| `ALERT <1\|2\|C> [lat lon] [PRUEBA]` | Probar el camino de las alertas del llavero |
| `STATUS` | Contadores, ocupación del aire y tabla de vecinos |
| `RADIO …` | Ver y cambiar parámetros; `RADIO SAVE` los guarda |
| `SF <7-12>` · `PWR <-9..22>` | Atajos de los dos que más se tocan |
| `TTL <0-7>` · `RELAY ON\|OFF` · `ROL …` | E1 / E2 |
| `RANGE …` | Prueba de alcance |
| `PERDIDA <0-100>` | **Depuración**: tira ese % de lo que llega, para ejercitar los reintentos |

Los parámetros que se cambian **se aplican al instante pero no se guardan**:
hace falta `RADIO SAVE`. Así un experimento no sobrevive a un reinicio por
accidente. La excepción es `ID <hex4>`, que se guarda solo.

---

## 7. Normativa y potencia (nota prudente, no asesoría legal)

En Colombia sólo **915–928 MHz** es de uso libre (Res. ANE 105/2020); por eso
la frecuencia por defecto es **921,5 MHz** y no 915,0, que con BW 500 kHz se
saldría de la banda por abajo. El firmware **rechaza** un canal que no quepa
entero. Por defecto va a **+14 dBm**, y por encima la consola avisa. El detalle
está en `DISENO.md` §3.

---

## 8. Si algo no funciona

| Síntoma | Causa probable |
|---|---|
| `ERR RADIO <n>` al arrancar | El SX1262 no contesta: TCXO o pin BUSY mal, o la placa no es una V3. El nodo sigue vivo para la consola, pero no transmite |
| Las dos placas no se oyen | Parámetros distintos. `RADIO` en las dos y compara, sobre todo SF, BW y sync |
| `EV FALLO … (sin ACK)` siempre | Demasiado lejos, antena mal puesta, o la otra placa está en otro canal |
| RSSI cercano a 0 y medidas raras | Placas demasiado juntas: sepáralas y baja con `PWR 0` |
| Consola muda | `ARDUINO_USB_CDC_ON_BOOT=0` tiene que estar (ya está): en la V3 el USB pasa por el CP2102, no por el USB nativo |
| `EV ID_DUPLICADO` | Las dos placas tienen el mismo ID. Fija uno con `ID <hex4>` |
| Nada en el OLED | Se compiló con `-D GEOLORA_OLED=0`, o `Vext` no encendió |
| `EV DESCARTE_COLA` a menudo | Demasiado tráfico para el preset: sube el BW, baja el SF o manda menos |
