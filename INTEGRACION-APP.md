# Integración app ↔ dispositivo · Ivy (antes GEO-EXPO ALERT)

Documento para el equipo que desarrolla la **app Android**. Describe cómo hablar
con el dispositivo por Bluetooth Low Energy.

- **Firmware:** https://github.com/IVY-Alert/ivy-firmware (antes `gado-code/geo-expo-firmware`;
  la dirección vieja redirige sola)
- **App (iPhone y Android):** https://github.com/IVY-Alert/ivy-app (privado)
- **Estado:** el contrato de esta página está **congelado y verificado en hardware**.
  Podéis empezar a programar contra él sin esperar a nada más.
- **Reparto:** vosotros hacéis la app **Android**; la de **iPhone** la hacemos nosotros.
  Ambas hablan con el mismo firmware y el mismo contrato.

---

## 1. Qué es el dispositivo

Un botón de pánico portátil basado en ESP32. El usuario lo pulsa, el dispositivo
emite una alerta por BLE, y la app es quien la convierte en algo útil (aviso a
contactos, ubicación, lo que decidáis).

El dispositivo **no tiene pantalla ni conectividad propia**: la app es su única
salida al mundo. Si no hay app conectada, la alerta se registra en el
dispositivo pero **no llega a ningún sitio**.

---

## 2. Descubrimiento

| Dato | Valor |
|---|---|
| Nombre anunciado | `GEOEXPO-ALERT` |
| UUID del servicio en el anuncio | `6E400001-B5A3-F393-E0A9-E50E24DCCA9E` |
| Nombre | viaja en el **scan response**, no en el anuncio principal |

Filtrad por **UUID de servicio**, no por nombre: es más fiable y más rápido.

> ⚠️ **La dirección BLE no es la MAC impresa en la placa.** El ESP32 usa la MAC
> base **+2** para BLE. Nuestra unidad de pruebas tiene MAC `3C:8A:1F:A7:31:D8`
> y aparece por BLE como `3C:8A:1F:A7:31:DA`. No useis direcciones fijas en el
> código: hay varias unidades y cada una tiene la suya.

**No hay emparejamiento ni PIN.** Es una conexión BLE directa. Si el usuario
empareja el dispositivo desde los ajustes del sistema, sólo conseguirá
problemas: hay que decirle que no lo haga.

---

## 3. El perfil: Nordic UART Service (NUS)

Es el perfil estándar de "puerto serie sobre BLE". Hay librerías hechas para
casi todo.

| Elemento | UUID | Dirección | Propiedades |
|---|---|---|---|
| Servicio | `6E400001-B5A3-F393-E0A9-E50E24DCCA9E` | — | — |
| **TX** | `6E400003-B5A3-F393-E0A9-E50E24DCCA9E` | dispositivo → app | `NOTIFY` |
| **RX** | `6E400002-B5A3-F393-E0A9-E50E24DCCA9E` | app → dispositivo | `WRITE`, `WRITE_NO_RESPONSE` |

Los nombres son desde el punto de vista **del dispositivo**: en TX escribe él y
leéis vosotros.

> ⚠️ Para recibir nada hay que **suscribirse a las notificaciones de TX**, es
> decir, escribir `0x0100` en su descriptor CCCD (`0x2902`). En Android eso es
> `setCharacteristicNotification()` **y además** escribir el descriptor: sólo con
> la primera llamada no llega nada. Es el error nº 1 con este perfil.

---

## 4. Mensajes

Todos son **texto ASCII terminado en `\n`**.

### 4.1. Del dispositivo a la app (TX, notificaciones)

| Mensaje | Cuándo se emite | Qué significa |
|---|---|---|
| `ALERT:1` | pulsación corta del botón | Alerta normal |
| `ALERT:2` | botón mantenido 3 segundos | Alerta prolongada / situación más grave |
| `CANCEL` | el usuario cancela dentro de la ventana | **Falsa alarma**: anula la alerta anterior |

### 4.2. De la app al dispositivo (RX, escrituras)

| Mensaje | Efecto |
|---|---|
| `BEACON:ON` | Activa la baliza: el dispositivo se hace localizable (hoy, LED parpadeando a 2 Hz; en la versión final, zumbador) |
| `BEACON:OFF` | La desactiva |

Se aceptan en mayúsculas o minúsculas. El `\n` final es opcional en las
escrituras. Un comando desconocido se ignora sin romper nada.

---

## 5. La ventana de cancelación — lo más importante

Esta es la parte que **condiciona el diseño de vuestra app**, así que leedla
con calma.

Cuando el usuario dispara una alerta, el dispositivo abre una **ventana de 10
segundos** durante la cual el usuario puede arrepentirse. La secuencia es:

```
  t=0 s      el usuario pulsa      -> el dispositivo envía ALERT:1
  t=0..10 s  ventana abierta
             · si vuelve a pulsar  -> el dispositivo envía CANCEL
             · si no hace nada     -> no se envía nada más
  t=10 s     la alerta queda CONFIRMADA (silenciosamente)
```

Consecuencias para la app:

1. **`ALERT:1` no significa "avisa a todo el mundo ya".** Significa "empieza la
   cuenta atrás". Lo razonable es que la app muestre su propia cuenta atrás y
   dispare el aviso real al terminar.
2. **`CANCEL` cancela la última alerta.** Si os llega, abortad lo que estuvierais
   a punto de hacer.
3. **La confirmación no genera mensaje.** El dispositivo no envía nada al
   expirar los 10 segundos: el silencio *es* la confirmación. Si a los 10 s no
   ha llegado `CANCEL`, la alerta va en serio. Esto es deliberado —
   un mensaje de confirmación podría perderse y dejar la alerta colgada.
4. La cuenta de la app debe ser **de al menos 10 s** desde `ALERT:x`, o
   avisaréis antes de que el usuario haya tenido ocasión de cancelar.

---

## 6. Cosas que os van a morder

**Las alertas no se guardan.** Si no hay app conectada, el mensaje se pierde: no
hay cola ni reenvío. La app debería mantener la conexión abierta mientras esté
en segundo plano y reconectar en cuanto pueda.

**Reconexión.** Al desconectarse un cliente, el dispositivo reanuda el anuncio
solo, de inmediato. No hace falta resetear nada.

**Un cliente a la vez.** El firmware está pensado para una sola app conectada;
seguid contando con eso. (Aviso interno: hasta el 10-sep, si llegaba a haber
dos y una se desconectaba, el firmware dejaba de notificar también a la otra.
Ya está corregido, pero el diseño sigue siendo de un cliente.)

**Un mensaje por notificación, pero no lo deis por hecho.** Hoy cada notificación
lleva un mensaje entero. Aun así, **acumulad en un buffer y partid por `\n`** en
vez de tratar cada notificación como un mensaje completo: es lo correcto con un
perfil serie y os ahorra un bug raro más adelante.

**Ignorad lo que no conozcáis.** Si llega un mensaje que no está en esta tabla,
descartadlo en silencio. Así el firmware puede crecer sin romperos la app.

**MTU.** Por defecto BLE da 23 bytes de MTU, o sea **20 bytes útiles**. Los
mensajes de hoy caben de sobra, pero si se aprueba lo de la sección 7 **no
caben** y hay que negociar MTU (`requestMtu(185)` nada más conectar). Pedidlo
igualmente desde ya: no cuesta nada y os evita un truncamiento silencioso.

### Permisos en Android

Esto nos costó media hora de pruebas, así que va avisado:

- **Android 12+ (API 31+):** `BLUETOOTH_SCAN` y `BLUETOOTH_CONNECT` son permisos
  **distintos y en tiempo de ejecución**. Con sólo el de escanear, la app **ve el
  dispositivo pero al conectar no pasa absolutamente nada**: sin error, sin
  excepción, sin callback. Silencio total.
- **Android 11 y anteriores:** hace falta `ACCESS_FINE_LOCATION` **y la ubicación
  encendida**, o el escaneo devuelve una lista vacía.
- El *error 133* (`GATT_ERROR`) al conectar es endémico en Android. Implementad
  reintento con espera creciente; suele funcionar al segundo o tercer intento.
- Llamad a `close()` sobre el `BluetoothGatt` al desconectar. Si no, se agotan
  los slots internos y a partir de la séptima conexión deja de conectar.

---

## 7. Pendiente de acordar: la posición GPS

**Esto hay que decidirlo entre los dos equipos, y por eso está en un apartado
propio.**

El módulo GPS ya está programado y probado en el firmware, pero **la posición
todavía no se envía**, porque añadirla cambiaría los mensajes y os rompería el
parser. Está implementado y desactivado, a la espera de vuestro visto bueno.

Nuestra propuesta:

```
ALERT:1;<lat>;<lon>      con posición  (grados decimales, 6 decimales, punto como separador)
ALERT:1                  sin posición  (todavía sin cobertura GPS) — exactamente igual que hoy
CANCEL                   nunca lleva posición
```

Ejemplo real: `ALERT:1;4.205760;-74.094648`

Es **compatible hacia atrás**: si vuestro parser corta por el primer `;` y se
queda con la primera parte, funciona igual antes y después del cambio. Si lo
hacéis así desde el principio, el día que lo activemos no tendréis que tocar
nada.

Dos avisos:

- La latitud/longitud pueden ser **negativas** (sur y oeste). Colombia es
  longitud negativa, así que esto os va a pasar siempre.
- El mensaje con posición son ~28 bytes: **no cabe en los 20 bytes del MTU por
  defecto**. Por eso lo del `requestMtu()` de la sección 6.

Decidnos si os sirve tal cual o preferís otro formato — por ejemplo un mensaje
`POS` aparte. Mientras no haya acuerdo, el firmware sigue enviando exactamente
lo de la sección 4.

---

## 8. Cómo probar sin tener el dispositivo delante

Mientras no tengáis una unidad, cualquiera del equipo puede levantar un
periférico NUS falso y probar la app contra él:

- **nRF Connect for Mobile** (Nordic Semiconductor, gratis) tiene un modo
  *Advertiser* que permite simular un periférico desde otro móvil.
- Con el dispositivo real: conectáis con nRF Connect, activáis las
  notificaciones en `...0003` y veis los mensajes tal cual llegan. Sirve para
  comprobar si un problema es de la app o del dispositivo.

Lo que ya está **verificado en hardware** por nuestra parte: el anuncio, la
conexión, las notificaciones de `ALERT:1` / `ALERT:2` / `CANCEL`, y las
escrituras `BEACON:ON` / `BEACON:OFF`. Si algo de eso no os funciona, es la app.

---

## 9. Resumen para empezar mañana

1. Escanear filtrando por el servicio `6E400001-B5A3-F393-E0A9-E50E24DCCA9E`.
2. Pedir los permisos de la sección 6 **antes** de escanear.
3. Conectar, `requestMtu(185)`, descubrir servicios.
4. Suscribirse a `...0003` (característica + descriptor CCCD).
5. Acumular lo recibido y partir por `\n`.
6. `ALERT:1` / `ALERT:2` → cuenta atrás de 10 s en la app. `CANCEL` → abortar.
7. Escribir `BEACON:ON` / `BEACON:OFF` en `...0002` cuando haga falta.

Cualquier duda o cambio que necesitéis en el firmware, decídnoslo antes de
programar alrededor del problema.
