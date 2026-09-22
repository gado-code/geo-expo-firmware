# GEO-EXPO ALERT · Enlace LoRa punto a punto (Heltec WiFi LoRa 32 V3)

Dos placas hablando entre ellas por radio, **sin internet, sin móvil y sin
nube**. La idea es la del AirTag —"caliente, caliente… frío"— pero con LoRa, y
con una vuelta de tuerca: aquí el llavero **también puede gritar**.

| Placa | Papel | Qué hace |
|---|---|---|
| `heltec_tag` | **LLAVERO** | Lo lleva la persona. Botón de pánico, zumbador, GPS opcional y una baliza periódica por radio. |
| `heltec_finder` | **BUSCADOR** | Se queda con quien vigila. Oye al llavero, dice si está cerca o lejos, confirma las alertas y puede hacerle pitar. |

> **Estado: escrito y revisado, SIN probar en placa.** El protocolo, la
> estimación de cercanía y los patrones de sonido están cubiertos por 53
> pruebas automáticas que corren en el PC (`pio test -e native`), y los dos
> firmwares pasan la comprobación de sintaxis (`tools/comprobar-sintaxis.sh`).
> Pero **nada de esto ha visto una Heltec todavía**: la §7 es la lista de lo
> que hay que comprobar cuando lleguen las placas.

---

## 1. Qué hace cada cosa

### El llavero (TAG)

- **Baliza**: cada 15 s manda una trama diciendo "sigo aquí", con su posición
  si tiene GPS y su batería si está cableada. En modo rápido, cada 3 s.
- **Botón de pánico**: exactamente la misma máquina de estados que el firmware
  de la DevKit (`lib/panic`), con la misma ventana de cancelación de 10 s:
  - pulsación corta → alerta `ALERT:1`
  - mantener 3 s → alerta `ALERT:2` **en el instante de los 3 s**
  - otra pulsación dentro de los 10 s → **CANCELA** la alerta
- **La alerta se reintenta** hasta 6 veces, cada 4 s, **hasta que el buscador
  la confirma con un ACK**. Una alerta que se pierde en el aire y nadie
  reintenta no es una alerta, es un deseo.
- **Obedece al buscador**: ponerse a pitar (`FIND_ON`), callarse (`FIND_OFF`),
  mandar posición ahora (`WHERE`), cambiar la cadencia (`FAST_ON`/`FAST_OFF`).
- **La cancelación también se reintenta.** Si se mandara una sola vez y la
  radio estuviera ocupada en ese instante, el buscador se quedaría con la
  alarma puesta por algo que el usuario ya había anulado.

### Quién cierra una alerta

Conviene tenerlo claro porque son dos cosas distintas:

| Mensaje | Significa | Efecto en el llavero |
|---|---|---|
| `ACK` | "me ha llegado" | Deja de reintentar (se comprueba que confirma *ese* envío, no uno anterior) |
| `FAST_OFF` (o `STOP` en el buscador) | "ya está atendida" | Cierra la alerta y vuelve a la baliza lenta |

Un `ACK` **no** cierra la alerta: mientras nadie diga que está atendida, el
llavero sigue marcando alerta y balizando rápido, que es lo que se quiere en
una emergencia de verdad. El `STOP` del buscador manda el `FAST_OFF` solo.

### El buscador (FINDER)

- Imprime **una línea por trama recibida**: zona, barras, distancia estimada,
  RSSI crudo y suavizado, SNR, posición (con enlace a Google Maps) y batería.
- **Confirma las alertas con un ACK** en cuanto llegan, para que el llavero
  deje de reintentar, y pita hasta que alguien lo silencia.
- **Modo búsqueda** (`FIND` o el botón PRG): le dice al llavero que pite y
  **pita él también, cada vez más seguido según te acercas**:

  | Zona | RSSI suavizado | Pitido del buscador |
  |---|---|---|
  | `AQUI` | ≥ −55 dBm | cada 150 ms (casi continuo) |
  | `CERCA` | −55 … −85 dBm | cada 500 ms |
  | `LEJOS` | −85 … −110 dBm | cada 1,2 s |
  | `MUY LEJOS` | < −110 dBm | cada 2,5 s |
  | `SIN SENAL` | 20 s sin oír nada | no pita (mentir sería peor) |

- Además dice si **te estás acercando o alejando**, comparando el RSSI con el
  de las últimas tramas.

---

## 2. Qué NO hace (y por qué)

- **No mide la distancia de verdad.** El número en metros sale de un modelo
  log-distancia sobre el RSSI: depende de la antena, del bolsillo en el que
  vaya el llavero, de las paredes y de cómo sujetes la placa. La **zona** es
  bastante fiable; el número es orientativo y hay que presentarlo así. Con la
  calibración de la §6 mejora mucho, pero sigue siendo una estimación.
- **No cifra el contenido.** Las tramas van firmadas (nadie puede inventarse
  una alerta ni apagar el pitido sin la clave), pero la posición viaja a la
  vista de quien escuche esa frecuencia. Para el proyecto es una decisión
  consciente, no un olvido: es lo que hay que contar si lo preguntan.
- **No hay red mallada ni repetidores.** Es punto a punto entre dos placas.

---

## 3. Montaje

### Lo mínimo

Dos Heltec WiFi LoRa 32 V3 **con su antena puesta** y un cable USB-C de datos.
Sin nada más ya funciona: el botón PRG integrado hace de botón de pánico y las
trazas salen por el monitor serie.

> ⚠️ **Nunca enciendas la radio sin la antena.** Sin ella, la potencia rebota
> hacia el amplificador y se puede cargar el módulo.

### Lo recomendable

| Pieza | Dónde | Notas |
|---|---|---|
| Zumbador piezo **pasivo** | `GPIO6` (o `GPIO7`) y GND | Descomenta `-D PIN_BUZZER_CFG=6` en `[heltec_base]`. **No uses GPIO25**: en el ESP32-S3 ese pin no existe (no hay 22-25, y 26-32 son flash/PSRAM); hay un `#error` que lo impide. |
| Botón externo | `GPIO4`, `5`, `6`, `7` … a GND | `-D PIN_BUTTON_CFG=<pin>`. El PRG (GPIO0) vale para probar, pero es pin de arranque. |
| Batería | divisor de fábrica en `GPIO1`, habilitado por `GPIO37` | `-D PIN_VBAT_CFG=1`. El porcentaje viaja en cada trama y el buscador lo enseña. |
| GPS (NEO-6M/7M) | TX del GPS → `GPIO4` | `-D GPS_UART_ENABLED=1`. Al GPS no le mandamos nada: su RX se queda sin conectar. |

Pines de la radio (van soldados en la placa, no se tocan): `NSS 8`, `SCK 9`,
`MOSI 10`, `MISO 11`, `RST 12`, `BUSY 13`, `DIO1 14`.

---

## 4. Compilar y flashear

```bash
pio run -e heltec_tag    -t upload     # la placa del llavero
pio run -e heltec_finder -t upload     # la placa del buscador
pio device monitor -e heltec_finder    # ver qué oye
```

El USB-C de la Heltec V3 **no** es el USB nativo del S3: va a un puente
**CP2102**, así que el puerto es **`/dev/ttyUSB0`**, igual que la DevKit (por
eso `platformio.ini` no lleva los flags `ARDUINO_USB_CDC_ON_BOOT`; con ellos
el monitor serie se queda mudo). Si la placa no aparece, mantén **PRG**
pulsado, pulsa **RST**, suelta **RST** y luego **PRG** para entrar en modo
descarga.

### Antes de la expo, cambia dos cosas en `platformio.ini`

```ini
-D LORA_FREQ_MHZ=915.0                      ; 915 América · 868 Europa
-D GEO_LINK_KEY='"la-clave-que-quieras"'    ; ¡la de ejemplo es pública!
```

⚠️ **La frecuencia tiene que ser la de tu placa y la de tu país.** Las Heltec
V3 vienen en versión US915 o EU868 y **no son intercambiables**: emitir fuera
de banda no sólo es ilegal, además la antena no está hecha para esa frecuencia
y el módulo lo sufre.

Las dos placas deben llevar **la misma clave y la misma frecuencia**, o no se
oirán. Si cambias la clave sólo en una, verás en el monitor
`RX descartada: firma invalida`, que es exactamente lo que tiene que pasar.

---

## 5. Comandos del monitor serie

Comunes a las dos placas:

| Comando | Qué hace |
|---|---|
| `STATUS` / `POS` | Foto completa: radio, contadores, última trama oída, posición |
| `MUTE:ON` / `MUTE:OFF` | Silencia o reactiva el zumbador (el resto sigue igual) |
| `BEEP` | Prueba de zumbador |
| `NMEA <trama>` | Inyecta una sentencia NMEA a mano, para probar sin GPS |
| `?` | Ayuda |

Sólo en el **buscador**:

| Comando | Qué hace |
|---|---|
| `FIND` | Entra en modo búsqueda: el llavero pita y el buscador hace caliente/frío |
| `STOP` | Sale del modo búsqueda y calla la alarma |
| `WHERE` | Pide posición al llavero ahora mismo |
| `FAST` / `SLOW` | Cambia la cadencia de la baliza del llavero |
| `CAL <dBm>` | Calibra el RSSI de referencia a 1 m (ver §6) |

El **botón PRG** del buscador entra y sale del modo búsqueda; si hay una alarma
sonando, el primer toque la silencia.

Sólo en el **llavero**:

| Comando | Qué hace |
|---|---|
| `FIND` / `STOP` | Hace que pite o que se calle, sin pasar por el buscador |
| `TEST` | Dispara una alerta sin tocar el botón (para probar el enlace solo) |

---

## 6. Calibrar la distancia

Vale la pena hacerlo una vez, con las antenas que se vayan a usar de verdad:

1. Pon las dos placas **a un metro exacto**, a la altura a la que vayan a ir.
2. Mira en el monitor del buscador el `RSSI` de varias tramas seguidas y
   quédate con el valor típico (por ejemplo `-52`).
3. Escribe `CAL -52` en el buscador.

A partir de ahí la distancia estimada es mucho menos fantasiosa. Si además
todo pasa en interiores con paredes, el exponente de pérdidas se puede subir
recompilando con un `ranging::Config` distinto (2,0 es campo abierto; 2,7 el
valor por defecto; 3,5 un edificio con tabiques).

> La calibración **no se guarda**: al reiniciar vuelve al valor por defecto.
> Si acaba siendo siempre el mismo número, lo suyo es fijarlo en el código.

---

## 7. Qué hay que probar cuando lleguen las placas

Nada de esto se ha podido hacer todavía. En orden, porque cada paso depende
del anterior:

| # | Prueba | Qué tiene que pasar |
|---|---|---|
| L1 | Flashear las dos y abrir los dos monitores | Banner con el papel correcto y `RADIO: escuchando` |
| L2 | Dejarlas quietas 1 min | El buscador imprime una línea por baliza, cada 15 s |
| L3 | Alejarse andando con el llavero | La zona baja de `AQUI` a `CERCA` y a `LEJOS`, y la flecha dice "te estás alejando" |
| L4 | Pulsación corta del botón del llavero | `*** ALERTA CORTA ***` en el buscador, ACK de vuelta y `ALERTA CONFIRMADA` en el llavero |
| L5 | Cancelar dentro de los 10 s | Llega la cancelación y el buscador deja de sonar |
| L6 | Apagar el buscador y pulsar el botón | El llavero reintenta 6 veces y luego avisa de que nadie contestó |
| L7 | `FIND` en el buscador | El llavero pita; el buscador pita cada vez más seguido al acercarse |
| L8 | Cambiar la clave en una sola placa | Todas las tramas salen como `firma invalida`: la firma hace su trabajo |
| L9 | Alcance real | Medir hasta dónde llega en el patio y **anotarlo**: es el dato que va a preguntar todo el mundo |
| L10 | Consumo | Cuánto dura con la batería que se vaya a usar, con baliza lenta y con rápida |

---

## 8. El protocolo, por dentro

Trama fija de **28 bytes**, little-endian:

| Offset | Tam. | Campo |
|---:|---:|---|
| 0 | 1 | magic `'G'` |
| 1 | 1 | versión (4 bits) + tipo (4 bits) |
| 2 | 2 | id del emisor |
| 4 | 2 | id del destinatario (`0xFFFF` = a todos) |
| 6 | 2 | contador `seq` |
| 8 | 1 | flags |
| 9 | 1 | batería (0-100 %, 255 = desconocida) |
| 10 | 4 | latitud × 10⁷ |
| 14 | 4 | longitud × 10⁷ |
| 18 | 1 | `aux0`: código de alerta u orden |
| 19 | 1 | `aux1`: reservado / eco del `seq` en los ACK |
| 20 | 8 | firma SipHash-2-4 de los 20 bytes anteriores |

**Tipos**: `BEACON` (1), `ALERT` (2), `ACK` (3), `CMD` (4), `STATUS` (5).
**Órdenes** (`aux0` de un `CMD`): `FIND_ON` (1), `FIND_OFF` (2), `WHERE` (3),
`FAST_ON` (4), `FAST_OFF` (5). En un `ALERT`, `aux0` es `1` (corta), `2`
(prolongada) o `0` (cancelación).

**Por qué lleva firma.** LoRa es radio abierta: cualquiera con otra placa podría
inventarse una alerta o mandar "deja de pitar". Los 8 bytes de SipHash-2-4 con
clave compartida hacen que sólo quien conoce la clave pueda fabricar una trama
válida, y el contador `seq` con su ventana impide repetir una trama grabada del
aire. No es cifrado —el contenido viaja a la vista— pero corta en seco las dos
travesuras evidentes. Hay pruebas automáticas para las dos cosas:
`test_orden_falsificada_no_apaga_la_alerta` y
`test_guardia_acepta_lo_nuevo_y_rechaza_lo_repetido`.

**Parámetros de radio por defecto**: SF9, BW 125 kHz, CR 4/5, 14 dBm, sync word
`0x34` (red privada). Una trama tarda unos 100 ms en el aire. Subir a SF12
multiplica el alcance y también el tiempo en el aire (medio segundo por trama),
que es justo lo que no conviene en una alerta que se reintenta.

---

## 9. Si algo no va

| Síntoma | Causa probable |
|---|---|
| `RADIO: fallo al arrancar (codigo -2)` | La placa no es una V3 (o los pines SPI no son los de la tabla). El `-2` de RadioLib es "no encuentro el chip". |
| `RADIO: fallo al arrancar (codigo -11)` | Frecuencia fuera de la banda del módulo: revisa `LORA_FREQ_MHZ`. |
| Todo parece transmitir pero el otro no oye nada | Falta `setDio2AsRfSwitch(true)` (ya está en el código) o, más probable, **la antena**. También: claves distintas, frecuencias distintas o `sync word` distinta. |
| `RX descartada: firma invalida` | Las dos placas no llevan la misma `GEO_LINK_KEY`. |
| `RX descartada: repetida` | Pasa si una placa arranca en frío y su contador vuelve a cero. **Se arregla solo**: tras cinco tramas rechazadas seguidas del mismo emisor, el otro lado da por hecho que se reinició y resincroniza (lo dice en el log). El contador vive en memoria RTC, así que un reset normal —o del perro guardián— no lo pierde; sólo un apagón. |
| La distancia dice disparates | Sin calibrar (§6). Y recuerda que el RSSI cambia mucho si la placa va en un bolsillo. |
| El buscador no pita | ¿Hay zumbador cableado y `-D PIN_BUZZER_CFG` descomentado? ¿`MUTE:ON` activo? ¿Zona `SIN SENAL`? |
