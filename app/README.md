# Ivy · App móvil (iPhone y Android)

La app del llavero **Ivy**. Un solo código (Expo / React Native, SDK 57) que se compila
como app **nativa** para iPhone y para Android. Habla con el firmware de este
repositorio siguiendo **[`../INTEGRACION-APP.md`](../INTEGRACION-APP.md)** al pie
de la letra.

## Lo que hace

| Pantalla | Qué hay |
|---|---|
| **Llavero** (inicio) | Estado de la conexión en vivo, señal, botón **Localizar** (`BEACON:ON/OFF`), guía del botón y última alerta |
| **Alertas** | Historial agrupado por día: confirmada o cancelada (desde el llavero o la app), nivel, y mapa si hubo ubicación |
| **Contactos** | Contactos de emergencia que reciben el SMS |
| **Ajustes** | Llavero vinculado, cuenta atrás (10/15/20/30 s), ubicación, número de emergencias (123), mensaje, vibración, **simulador de alertas** y registro BLE |
| **Alerta** (pantalla completa) | Cuenta atrás grande → *cancelar* / *avisar ya* → SMS con ubicación y llamada al 123 |
| **Vincular** (hoja) | Llaveros cercanos ordenados por cercanía; se toca uno y listo |

### El flujo de una alerta (§5 del contrato)

1. Llega `ALERT:1` o `ALERT:2` → vibración, pantalla de alerta y cuenta atrás de
   **al menos 10 s** (nunca menos que la ventana del firmware).
2. Llega `CANCEL` dentro de la ventana → alerta cancelada, no se avisa a nadie.
3. Pasa el plazo sin `CANCEL` (el silencio *es* la confirmación) → se abre el SMS
   a los contactos con la hora y un enlace de Google Maps.

Las notificaciones de la alerta se **programan en el sistema**, no con
temporizadores de JavaScript: si el teléfono está bloqueado o la app en segundo
plano, el aviso de «Alerta confirmada» sale igual, y se anula si llega `CANCEL`.

### Diseño

- **iPhone (iOS 26+): Liquid Glass de verdad.** La barra de pestañas es la
  nativa del sistema (`NativeTabs`), flotante y de vidrio, que se encoge al hacer
  scroll. Encima lleva un **accesorio de vidrio con el logo** y el estado del
  llavero (como el mini reproductor de Música); al encogerse queda en línea con
  la pestaña activa. Las tarjetas y botones usan `GlassView` (`UIGlassEffect`),
  las cabeceras son de título grande transparentes, los iconos son SF Symbols y
  las hojas (vincular, contacto) son *sheets* nativas.
- **iOS 18 y anteriores:** mismo diseño con superficies translúcidas.
- **Android:** barra de navegación Material 3, iconos Material Symbols, ripple,
  canal de notificaciones de máxima prioridad.
- **Modo claro y oscuro** automáticos.

## Branding

**Ivy**. El logo es una hoja: [`assets/brand/logo.svg`](assets/brand/logo.svg)
es el original y [`src/components/BrandMark.tsx`](src/components/BrandMark.tsx)
lo dibuja en la app (barra inferior, Inicio, Vincular y alerta) con los mismos
trazos. Los iconos de `assets/` (iOS, adaptativo de Android, splash, favicon)
salen de ese mismo SVG, sobre un fondo crema → amarillo pastel.

Colores, todos en [`src/theme/brand.ts`](src/theme/brand.ts):

| | Color | Uso |
|---|---|---|
| Ámbar | `#FFA928` | la hoja, botones principales, selección |
| Tinta | `#2E2437` | contorno del logo, texto principal y texto sobre el ámbar (el blanco sobre ámbar no se lee) |
| Crema | `#FFF6E6` | nervaduras del logo, fondos |
| Amarillo pastel | `#FFE9A3` | superficies suaves, fondo del icono |
| Ámbar oscuro / claro | `#A86200` / `#FFC247` | texto e iconos de acento en modo claro / oscuro |

- El rojo de las alertas se mantiene a propósito: una alerta tiene que asustar.
- Identificador de la app: `com.ivy.app` (en `app.json`). Hay que confirmarlo
  antes de publicar: en las tiendas tiene que ser único.
- El llavero sigue anunciándose como `GEOEXPO-ALERT` porque ese nombre lo pone el
  firmware; la app no filtra por nombre, así que da igual para conectar.

## Instalarla en Android (gratis)

Cada cambio en `app/` compila solo un APK en GitHub
([`.github/workflows/android-apk.yml`](../.github/workflows/android-apk.yml)).

1. En el móvil Android, entra con tu cuenta de GitHub al repositorio →
   **Releases** → **android-latest** → descarga **`ivy.apk`**.
2. Ábrelo. Android pedirá permitir «instalar apps de fuentes desconocidas» para
   el navegador: acéptalo. Si Play Protect avisa, «Instalar de todos modos».
3. Abre Ivy y acepta los permisos (dispositivos cercanos, notificaciones,
   ubicación).

Para actualizar, descarga el nuevo `ivy.apk` e instálalo encima: se conservan
los contactos y el historial. Es un APK firmado con la clave de pruebas de Expo:
vale para probar, no para Google Play. Incluye `arm64-v8a` y `armeabi-v7a`: la
segunda hace falta para los Samsung de gama baja (Galaxy A03, A04, A13...), que
llevan un Android de 32 bits aunque el procesador sea de 64.


La app usa Bluetooth nativo (`react-native-ble-plx`), así que **no funciona en
Expo Go**: hace falta un *development build*. Sin llavero, todo se puede probar
en **modo demo** (Ajustes → Pruebas, o «Probar una alerta» en Inicio).

```bash
cd app
npm install

# En la nube, sin Mac ni Android Studio (recomendado):
npx eas-cli@latest build --profile development --platform ios      # o android
npm start                      # servidor de desarrollo para el build instalado

# En local:
npm run ios                    # necesita Xcode (Mac)
npm run android                # necesita Android Studio / SDK
```

Liquid Glass sólo se ve en un iPhone (o simulador) con **iOS 26 o posterior** y
compilando con Xcode 26.

### Comprobaciones

```bash
npm test          # 22 pruebas del protocolo: parser, buffer, base64, máquina de alerta, SMS
npm run typecheck
npm run lint
```

## Estructura

```
src/
  app/                    rutas (expo-router): cada archivo es una pantalla
    _layout.tsx           raíz: proveedor + modales (alerta, vincular, contacto)
    (tabs)/_layout.tsx    barra de pestañas nativa + accesorio del logo
    (tabs)/(inicio)/      Llavero        → /
    (tabs)/alertas/       Historial      → /alertas
    (tabs)/contactos/     Contactos      → /contactos
    (tabs)/ajustes/       Ajustes        → /ajustes
    alerta.tsx            pantalla completa de alerta
  protocol/               contrato BLE puro (sin React Native) + pruebas
  ble/KeychainLink.ts     escaneo, conexión, MTU, notificaciones, reconexión
  state/                  estado global y almacenamiento local
  services/               notificaciones, ubicación, SMS, llamada
  components/ theme/      piezas de interfaz y colores
```

## Decisiones y límites conocidos

- **Escaneo filtrado por el UUID del servicio**, nunca por nombre ni MAC (§2).
  Se vincula un llavero concreto (hay varias unidades) y sólo se conecta a ese.
- **MTU 185** al conectar en Android; iOS lo negocia solo. El parser ya acepta
  el formato con GPS propuesto en §7 (`ALERT:1;lat;lon`): cuando el firmware lo
  active, la app usará esa posición en lugar de la del teléfono sin tocar nada.
- **Reconexión** con espera creciente (1 s → 30 s), que cubre el error 133 de
  Android. La baliza sobrevive a la desconexión en el firmware, así que la app
  reenvía el estado deseado al reconectar.
- **Segundo plano:** en iPhone está activado `bluetooth-central` y la
  restauración de estado de CoreBluetooth, así que las alertas llegan con la app
  cerrada. En Android, si el sistema mata la app en segundo plano la conexión se
  pierde; para garantizarlo hará falta un *foreground service* (siguiente paso).
- **SMS:** iOS no deja a ninguna app enviar SMS sin que el usuario pulse
  «Enviar», así que se abre el mensaje ya redactado. Para avisar sin
  intervención hace falta un servidor con un proveedor de SMS.
- **Doble toque:** el firmware cancela con una segunda pulsación en 10 s
  (PRUEBAS.md, 3c.15). La app lo respeta tal cual; si se cambia en el
  firmware, la app no necesita cambios.
