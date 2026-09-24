# PRUEBAS · Ivy LoRa (nodo Heltec V3)

Diseño: [`DISENO.md`](DISENO.md) · Manual: [`README.md`](README.md) · Última
actualización: **19-sep-2026**

Marca: **x** = pasado con registro · *?* = dado por bueno sin log · ☐ = sin probar.

> ⚠️ **Nada de esto se ha probado en hardware todavía**: no hay ninguna Heltec
> conectada al PC. Lo único verificado es la lógica pura (§1) y que el firmware
> compila (§2).

---

## 1. Pruebas automáticas en el PC (sin placa)

```bash
cd lora
pio test -e native
```

| # | Suite | Qué cubre | Casos | OK |
|---|---|---|:--:|:--:|
| 1.1 | `test_paquete` | Cabecera v1, ida y vuelta, CRC malo, trama corta, `len` incoherente, magic y versión, flag AUTH, src reservado, tipo desconocido, `prepararReenvio` | — | **x** |
| 1.2 | `test_carga` | Cargas de ALERT (con y sin posición), ACK, PONG y PING; token ↔ código; grados a texto sin coma flotante | — | **x** |
| 1.3 | `test_identidad` | `derivarId` con MACs inventadas, colisiones con IDs reservados, parseo de hex | — | **x** |
| 1.4 | `test_radio_params` | Presets, validación de banda (915,0 con BW 500 se rechaza), airtime al microsegundo, LDRO, presupuesto de ocupación | — | **x** |
| 1.5 | `test_vistos` | Caché `(src, msgId)`, expulsión, vuelta de `millis()` y de `msgId` | — | **x** |
| 1.6 | `test_motor_p2p` | ACK, reintentos con backoff y jitter, timeouts, duplicados, CAD, PING/PONG, ALERT con sus ciclos rápido y lento | — | **x** |
| 1.7 | `test_motor_malla` | Varios nodos sobre un canal simulado: inundación, supresión, TTL, hops, ecos, tormentas | — | **x** |
| 1.8 | `test_consola` | Todas las órdenes, sus rangos y sus errores | — | **x** |
| 1.9 | `test_gestos` | Botón: corta, doble, larga, rebotes en los dos flancos, arranque con el botón pulsado | — | **x** |

**Resultado del 19-sep: 197 de 197 casos pasan**, en unos 15 s.

---

## 2. Compilación

| # | Caso | Comando | Resultado | OK |
|---|---|---|---|:--:|
| 2.1 | Nodo completo | `pio run -e heltec_v3` | `[SUCCESS]`, RAM **13,4 %**, Flash **11,1 %**, sin warnings | **x** |
| 2.2 | Librerías | — | RadioLib 7.7.1, U8g2 2.36.18 y `geolora` en el grafo de dependencias | **x** |
| 2.3 | Sin pantalla | `-D GEOLORA_OLED=0` | Compila y no enlaza U8g2 | ☐ |

---

## 3. Banco: dos placas sobre la mesa (DISENO §13.2)

**Antes de empezar:** las dos a **2 m o más** y `PWR 0`. Pegadas se saturan y
las medidas no sirven.

| # | Caso | Procedimiento | Resultado esperado | OK |
|---|---|---|---|:--:|
| 3.1 | Arranque | Flashear las dos y abrir los monitores | Banner con ID distinto en cada una, `OK RADIO lista, escuchando` | ☐ |
| 3.2 | ID de la MAC | `ID` en las dos | El ID son los 2 últimos bytes de la MAC, y coincide con la etiqueta | ☐ |
| 3.3 | Descubrimiento | `PING *` en A | En A: `EV PONG de=<B>` con RTT, RSSI/SNR de ida y de vuelta | ☐ |
| 3.4 | PING unicast | `PING <B>` | Igual que 3.3, y `hops=0` | ☐ |
| 3.5 | Datos con ACK | `SEND <B> hola` | En B `EV DATOS ... texto="hola"`; en A `EV ACK ... intentos=1` | ☐ |
| 3.6 | Broadcast | `BCAST hola` en A | En B `EV DATOS`; en A ningún ACK (en E1 no lo lleva) | ☐ |
| 3.7 | Reintentos | `PERDIDA 50` en B y `SEND` desde A varias veces | `EV REINTENTO` con esperas crecientes y, aun así, la mayoría acaban en `EV ACK` | ☐ |
| 3.8 | Se agotan los intentos | Apagar B y `SEND <B> x` | 4 intentos y `EV FALLO ... (sin ACK)` | ☐ |
| 3.9 | Duplicados | `PERDIDA 0`, mandar y provocar un reenvío | El contador `dup` de `STATUS` sube y el texto **no** se entrega dos veces | ☐ |
| 3.10 | Alerta | `ALERT 2 4.60971 -74.08175 PRUEBA` en A | En B `EV ALERTA de=<A> ... ALERT:2;4.609710;-74.081750  PRUEBA`, salta a la página de alertas y el LED parpadea 10 s | ☐ |
| 3.11 | Cancelar una alerta | `ALERT 1` y luego `ALERT C` | El CANCEL lleva el **mismo seq** y detiene los reintentos de la alerta | ☐ |
| 3.12 | Parámetro incompatible | `SF 12` **sólo en A** | Dejan de oírse (sirve para reconocer el síntoma); al igualarlo, vuelven | ☐ |
| 3.13 | Banda | `RADIO FREQ 915.0` con BW 500 | `ERR FREQ`: el canal no cabe en 915–928 | ☐ |
| 3.14 | Guardar y recargar | `SF 10`, `RADIO SAVE`, reiniciar | Arranca con SF10; `RADIO DEFECTO` vuelve a fábrica sin guardar | ☐ |
| 3.15 | NVS inválida | Guardar y corromper a mano | `EV NVS_INVALIDA` y arranca con los valores de fábrica | ☐ |
| 3.16 | Ocupación del aire | `RANGE` un par de minutos | El % de aire de `STATUS` se queda por debajo del 10 % | ☐ |
| 3.17 | ID duplicado | `ID 1111` en las dos | `EV ID_DUPLICADO` | ☐ |
| 3.18 | Radio ausente | Arrancar sin antena/con el SX1262 sin responder | `ERR RADIO <n>` y la consola sigue viva | ☐ |

---

## 4. Botón, OLED y LED (DISENO §10)

| # | Caso | Procedimiento | Resultado esperado | OK |
|---|---|---|---|:--:|
| 4.1 | Corta fuera de RANGE | Pulsación corta | `EV BOTON corta -> PING ...` | ☐ |
| 4.2 | Doble | Dos pulsaciones en menos de 400 ms | Cambia la página del OLED | ☐ |
| 4.3 | Larga | Mantener ≥ 1,5 s | Arranca `RANGE` hacia el último vecino; otra larga lo para | ☐ |
| 4.4 | Corta en RANGE | Pulsación corta con RANGE en marcha | `EV BOTON corta -> punto N`, y el CSV siguiente lleva ese punto | ☐ |
| 4.5 | Rebotes | Pulsaciones muy breves | El antirrebote de 50 ms las filtra: ningún gesto | ☐ |
| 4.6 | Arranque con el botón pulsado | Resetear con PRG pulsado | No cuenta como gesto hasta soltarlo | ☐ |
| 4.7 | Páginas del OLED | Recorrer las cuatro | Estado, Enlace, Tráfico y Alertas, con datos coherentes | ☐ |
| 4.8 | LED | Mandar y recibir | Un destello al transmitir, doble al recibir, 4 Hz con una ALERT | ☐ |

---

## 5. Campo: alcance (DISENO §13.3)

| # | Caso | Procedimiento | Resultado esperado | OK |
|---|---|---|---|:--:|
| 5.1 | Sin PC | B fija encendida; A en la mano con pulsación larga | A mide sola y va guardando; B contesta sin que nadie la toque | ☐ |
| 5.2 | Puntos | Pulsación corta en cada sitio donde te paras | Cada tramo queda con su número de punto | ☐ |
| 5.3 | Informe | De vuelta, `RANGE INFORME` | Resumen por punto: enviados, recibidos, pérdidas %, RSSI/SNR mín/media/máx de los dos lados y RTT medio | ☐ |
| 5.4 | Alcance máximo | Alejarse hasta perder el enlace | Anotar la distancia y el RSSI/SNR del último punto bueno | ☐ |
| 5.5 | Comparar presets | Repetir un tramo con `RADIO PRESET LARGO` | Más alcance a cambio de más airtime (ojo al intervalo mínimo) | ☐ |

---

## 6. Multi-salto (E2), cuando haya un tercer nodo (DISENO §13.4)

| # | Caso | Procedimiento | Resultado esperado | OK |
|---|---|---|---|:--:|
| 6.1 | Paso a E2 | `TTL 3` y `RELAY ON` en los tres | `STATUS` dice E2 | ☐ |
| 6.2 | Dos saltos | A y C sin verse, B en medio | Lo de A llega a C con `hops=1` | ☐ |
| 6.3 | Supresión | Dos nodos que podrían reenviar lo mismo | Uno reenvía y el otro lo **suprime** (contador `suprimidos`) | ☐ |
| 6.4 | Sin tormenta | Un broadcast en E2 | Cada nodo lo repite **una sola vez**; los duplicados se descartan | ☐ |
| 6.5 | E1 y E2 juntos | Un nodo con `TTL 0` | Sus tramas no las repite nadie; las de los demás sí | ☐ |
| 6.6 | Eco de broadcast | `BCAST` en E2 | `EV ECO_BCAST` al oír a otro repetirlo | ☐ |
