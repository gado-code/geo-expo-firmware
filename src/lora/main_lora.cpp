/* ============================================================================
 *  GEO-EXPO ALERT  ·  Enlace LoRa punto a punto  (Heltec WiFi LoRa 32 V3)
 * ============================================================================
 *  Dos placas, sin internet, sin móvil y sin nube:
 *
 *      LLAVERO  (TAG)     -> lo lleva la persona. Botón de pánico, zumbador,
 *                            GPS opcional y una baliza periódica por radio.
 *      BUSCADOR (FINDER)  -> se queda con quien vigila. Oye al llavero, dice
 *                            si está cerca o lejos y puede hacerle pitar.
 *
 *  La idea es la del AirTag —"caliente, caliente... frío"— pero con LoRa en
 *  vez de Bluetooth, así que el alcance se mide en cientos de metros o
 *  kilómetros en campo abierto, no en metros. Y al revés que el AirTag, aquí
 *  el llavero también puede GRITAR: el botón de pánico manda una alerta que
 *  el buscador recibe aunque esté lejísimos.
 *
 *  --------------------------------------------------------------------------
 *  QUÉ FIRMWARE ES ESTE
 *
 *      pio run -e heltec_tag    -t upload     -> la placa que va en el llavero
 *      pio run -e heltec_finder -t upload     -> la placa que busca
 *
 *  Es EL MISMO archivo: lo que cambia es el papel, elegido con -D LORA_ROLE.
 *  Las dos placas tienen que llevar la MISMA clave (GEO_LINK_KEY) y la MISMA
 *  frecuencia, o no se oirán.
 *
 *  --------------------------------------------------------------------------
 *  QUÉ HAY AQUÍ Y QUÉ HAY EN lib/
 *
 *  Toda la lógica que se puede probar sin radio está fuera, en librerías con
 *  sus pruebas (`pio test -e native`):
 *
 *      lib/link    -> formato de trama, firma y antirrepetición
 *      lib/ranging -> RSSI -> zona -> distancia -> cadencia de pitido
 *      lib/buzzer  -> patrones del zumbador
 *      lib/panic   -> máquina de estados del botón (la misma de la DevKit)
 *      lib/nmea    -> parseo del GPS (la misma de la DevKit)
 *
 *  Aquí queda sólo lo que necesita hardware: SPI, la radio, los pines y el
 *  pegamento entre todo eso.
 *
 *  --------------------------------------------------------------------------
 *  ⚠️ FRECUENCIA Y LEY
 *
 *  La banda por defecto es 915 MHz, que es la que se usa en América (y la de
 *  las Heltec V3 versión US915). En Europa hay que compilar con
 *  -D LORA_FREQ_MHZ=868.0 y usar una placa EU868. Emitir fuera de la banda
 *  que corresponde a tu placa y a tu país no es sólo ilegal: con una antena
 *  que no está hecha para esa frecuencia se puede dañar el módulo.
 *
 *  ⚠️ NUNCA enciendas la radio sin la antena puesta.
 * ==========================================================================*/

#include <Arduino.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <SPI.h>
#include <Preferences.h>
#include <RadioLib.h>

#include "link.h"
#include "ranging.h"
#include "buzzer.h"
#include "panic.h"
#include "nmea.h"

/* ==========================================================================
 *  1. PAPEL DE ESTA PLACA
 * ==========================================================================*/
#define ROLE_TAG    1
#define ROLE_FINDER 2

#ifndef LORA_ROLE
  #define LORA_ROLE ROLE_TAG
#endif
#if (LORA_ROLE != ROLE_TAG) && (LORA_ROLE != ROLE_FINDER)
  #error "LORA_ROLE tiene que ser ROLE_TAG (1) o ROLE_FINDER (2)"
#endif
#define IS_TAG    (LORA_ROLE == ROLE_TAG)
#define IS_FINDER (LORA_ROLE == ROLE_FINDER)

/* Identificadores de las dos placas. Si algún día hay varios llaveros, cada
 * uno lleva su ID y el buscador los distingue solo. */
#ifndef TAG_ID
  #define TAG_ID 0x0001
#endif
#ifndef FINDER_ID
  #define FINDER_ID 0x0002
#endif

#if IS_TAG
  static const uint16_t MY_ID   = TAG_ID;
  static const uint16_t PEER_ID = FINDER_ID;
  static const char*    ROLE_LABEL = "LLAVERO (TAG)";
#else
  static const uint16_t MY_ID   = FINDER_ID;
  static const uint16_t PEER_ID = TAG_ID;
  static const char*    ROLE_LABEL = "BUSCADOR (FINDER)";
#endif

/* Clave compartida. CÁMBIALA en platformio.ini antes de la expo: la que viene
 * por defecto está escrita en un repositorio público, así que no protege de
 * nadie que sepa leer. */
#ifndef GEO_LINK_KEY
  #define GEO_LINK_KEY "geo-expo-2026-cambiame"
#endif

/* ==========================================================================
 *  2. PINES DE LA HELTEC WiFi LoRa 32 V3  (ESP32-S3 + SX1262)
 * --------------------------------------------------------------------------
 *  Los de la radio van soldados en la propia placa: no se tocan salvo que se
 *  use otro modelo. Los demás sí se pueden cambiar desde platformio.ini.
 * ==========================================================================*/
#ifndef PIN_LORA_NSS
  #define PIN_LORA_NSS  8
#endif
#ifndef PIN_LORA_SCK
  #define PIN_LORA_SCK  9
#endif
#ifndef PIN_LORA_MOSI
  #define PIN_LORA_MOSI 10
#endif
#ifndef PIN_LORA_MISO
  #define PIN_LORA_MISO 11
#endif
#ifndef PIN_LORA_RST
  #define PIN_LORA_RST  12
#endif
#ifndef PIN_LORA_BUSY
  #define PIN_LORA_BUSY 13
#endif
#ifndef PIN_LORA_DIO1
  #define PIN_LORA_DIO1 14
#endif

#ifndef PIN_LED_CFG
  #define PIN_LED_CFG 35          // LED blanco integrado de la V3
#endif
#ifndef PIN_BUTTON_CFG
  #define PIN_BUTTON_CFG 0        // botón PRG integrado (sirve para probar)
#endif
#ifndef PIN_BUZZER_CFG
  #define PIN_BUZZER_CFG -1       // -1 = sin zumbador cableado
#endif
#ifndef BUTTON_ACTIVE_HIGH
  #define BUTTON_ACTIVE_HIGH 0
#endif

/* Medida de batería de la Heltec V3: el divisor va a GPIO1 (ADC1_CH0) y sólo
 * se habilita poniendo a nivel BAJO el "ADC_Ctrl" de GPIO37. Queda apagado
 * por defecto (-1) porque no todas las unidades llevan batería. */
#ifndef PIN_VBAT_CFG
  #define PIN_VBAT_CFG -1
#endif
#ifndef PIN_VBAT_CTRL_CFG
  #define PIN_VBAT_CTRL_CFG 37
#endif

static const int PIN_LED    = PIN_LED_CFG;
static const int PIN_BUTTON = PIN_BUTTON_CFG;
static const int PIN_BUZZER = PIN_BUZZER_CFG;
static const int PIN_VBAT   = PIN_VBAT_CFG;

#if (PIN_BUZZER_CFG >= 0) && (PIN_BUZZER_CFG == PIN_BUTTON_CFG || PIN_BUZZER_CFG == PIN_LED_CFG)
  #error "El zumbador no puede compartir pin con el boton ni con el LED"
#endif
/* El ESP32-S3 NO tiene los GPIO 22 a 25, y del 26 al 32 están ocupados por la
 * flash y la PSRAM. Un -D PIN_BUZZER_CFG=25 copiado del firmware de la DevKit
 * compilaría sin rechistar y el zumbador no sonaría jamás. */
#if (PIN_BUZZER_CFG >= 22) && (PIN_BUZZER_CFG <= 25)
  #error "En el ESP32-S3 no existen los GPIO22-25 (ese pin es de la DevKit, no de la Heltec V3)"
#endif
#if (PIN_BUZZER_CFG >= 26) && (PIN_BUZZER_CFG <= 32)
  #error "En el ESP32-S3 los GPIO26-32 son de la flash/PSRAM: no se pueden usar"
#endif
#if (PIN_BUZZER_CFG >= 33)
  #error "GPIO33+ en la Heltec V3: comprueba en el esquema que ese pin esta libre antes de forzarlo"
#endif

/* ==========================================================================
 *  3. PARÁMETROS DE RADIO
 * --------------------------------------------------------------------------
 *  SF9 / BW125 es el punto medio razonable para la expo: una trama de 28
 *  bytes tarda unos 100 ms en el aire y el alcance ya es de sobra para un
 *  colegio entero. Subir a SF12 multiplica el alcance y también el tiempo en
 *  el aire (medio segundo por trama), que es exactamente lo que no queremos
 *  en una alerta que se reintenta.
 * ==========================================================================*/
#ifndef LORA_FREQ_MHZ
  #define LORA_FREQ_MHZ 915.0
#endif
#ifndef LORA_BW_KHZ
  #define LORA_BW_KHZ 125.0
#endif
#ifndef LORA_SF
  #define LORA_SF 9
#endif
#ifndef LORA_CR
  #define LORA_CR 5               // 4/5
#endif
#ifndef LORA_POWER_DBM
  #define LORA_POWER_DBM 14       // 14 dBm: legal casi en todas partes
#endif
#ifndef LORA_SYNC_WORD
  #define LORA_SYNC_WORD 0x34     // red privada (0x34 = "no soy LoRaWAN")
#endif
#ifndef LORA_TCXO_V
  #define LORA_TCXO_V 1.8         // la V3 lleva TCXO alimentado desde DIO3
#endif
#ifndef LORA_PREAMBLE
  #define LORA_PREAMBLE 8
#endif

/* Cadencias del llavero. La lenta es la de andar por casa; la rápida se
 * activa sola cuando hay alerta o cuando el buscador pide FAST_ON, porque es
 * justo cuando hace falta saber dónde está. */
#ifndef BEACON_SLOW_MS
  #define BEACON_SLOW_MS 15000
#endif
#ifndef BEACON_FAST_MS
  #define BEACON_FAST_MS 3000
#endif

/* Reintentos de una alerta hasta que el buscador la confirma con un ACK. */
static const uint8_t  ALERT_MAX_RETRIES = 6;
static const uint32_t ALERT_RETRY_MS    = 4000;

/* Sin oír nada del otro lado durante esto, se avisa de enlace perdido. */
static const uint32_t LINK_LOST_MS = 45000;

/* ==========================================================================
 *  4. OBJETOS GLOBALES
 * ==========================================================================*/
/* trace() se define en la §5; aquí basta con declararla. */
static void trace(const char* msg);

static SPIClass    g_spi(HSPI);
static SX1262      g_radio = new Module(PIN_LORA_NSS, PIN_LORA_DIO1, PIN_LORA_RST,
                                        PIN_LORA_BUSY, g_spi);

static link::Key         g_key;
static link::ReplayGuard g_guard;

/* El contador de secuencia vive en memoria RTC para que un reinicio (o el
 * perro guardián) no lo devuelva a cero: si volviera atrás, el otro lado
 * tomaría nuestras tramas por repeticiones y las tiraría todas. Un apagón sí
 * lo pierde, y para eso está la resincronización de más abajo. */
/* ¿`a` es igual o más nuevo que `b`? (aritmética circular, como en lib/link) */
static inline bool seqNewerOrEqual(uint16_t a, uint16_t b) {
  return a == b || link::seqNewer(a, b);
}

RTC_NOINIT_ATTR static uint32_t s_seqMagic;
RTC_NOINIT_ATTR static uint16_t s_seq;
static const uint32_t SEQ_MAGIC = 0x6E4C4B01;   // "nLK" + versión

/* La memoria RTC no sobrevive a QUITAR LA CORRIENTE, y ahí el contador volvía
 * a cero: tras un cambio de pila del buscador, sus primeras órdenes (FIND,
 * STOP...) las tiraba el llavero por "repetidas" y el operador veía que el
 * botón no hacía nada, sin ningún aviso. Así que el contador también se
 * guarda en la flash (NVS).
 *
 * No se escribe en cada trama —la flash tiene sus ciclos contados—: se
 * reserva un BLOQUE por adelantado. Al arrancar se lee el techo reservado, se
 * empieza desde ahí y se guarda el techo siguiente. Lo peor que pasa es que
 * un apagón "gaste" hasta 256 números de secuencia, que no le importan a
 * nadie: lo que importa es que nunca vayan hacia atrás. */
static const uint16_t SEQ_BLOCK = 256;
static Preferences    g_prefs;
static uint16_t       g_seqCeiling = 0;

static void seqReserveBlock(uint16_t desde) {
  g_seqCeiling = (uint16_t)(desde + SEQ_BLOCK);
  g_prefs.putUShort("seq", g_seqCeiling);
}

static void seqBegin() {
  if (!g_prefs.begin("geolink", /*readOnly=*/false)) {
    trace("NVS: no se pudo abrir; el contador solo vivira en memoria RTC");
    return;
  }
  const uint16_t guardado = g_prefs.getUShort("seq", 0);
  // Se toma el mayor de los dos: la RTC manda tras un reinicio normal, la
  // flash manda tras un apagón.
  if (seqNewerOrEqual(guardado, s_seq)) s_seq = guardado;
  seqReserveBlock(s_seq);
}

static void seqTick() {
  // Al llegar al techo reservado se reserva el bloque siguiente.
  if (s_seq >= g_seqCeiling) seqReserveBlock(s_seq);
}

/* Si el otro lado se reinicia en frío, su contador empieza de cero y nosotros
 * lo tomamos por repeticiones. Tras unas cuantas seguidas se da por hecho que
 * se reinició y se le perdona el contador.
 *
 * Esto abre una rendija: quien pudiera grabar y repetir varias tramas
 * seguidas lograría que le aceptáramos una repetición. Con la alternativa
 * —no resincronizar nunca— el enlace se quedaría muerto en cuanto alguien
 * cambiara una pila, que para un botón de pánico es bastante peor. Queda
 * anotado a propósito, no es un descuido. */
static const uint8_t RESYNC_AFTER = 5;
static uint8_t  g_replayStreak = 0;
static uint16_t g_replaySrc    = 0;

static buzzer::Player    g_buzzer;

static bool     g_radioUp     = false;
static uint32_t g_rxOk        = 0;   // tramas válidas
static uint32_t g_rxBad       = 0;   // tramas descartadas (ruido, firma, repetidas)
static uint32_t g_txCount     = 0;
static uint32_t g_tLastHeard  = 0;   // millis() de la última trama del otro lado
static bool     g_everHeard   = false;
static bool     g_linkLostSaid = false;

/* ==========================================================================
 *  5. TRAZAS
 * ==========================================================================*/
static void trace(const char* msg) {
  Serial.printf("[t=%8lu ms] %s\n", (unsigned long)millis(), msg);
}

/* ==========================================================================
 *  6. LED Y ZUMBADOR
 * --------------------------------------------------------------------------
 *  Mismo criterio que en el firmware de la DevKit: nada de delay(), todo por
 *  millis(), y el secuenciador del zumbador vive en lib/buzzer (probado).
 * ==========================================================================*/
namespace led {

  static bool     s_blinkOn   = false;
  static uint32_t s_blinkT0   = 0;
  static uint16_t s_blinkMs   = 0;
  static bool     s_pulseOn   = false;
  static uint32_t s_pulseUntil = 0;

  void begin() { pinMode(PIN_LED, OUTPUT); digitalWrite(PIN_LED, LOW); }

  /* Parpadeo continuo (0 = apagarlo).
   *
   * OJO con el early return: el buscador llama a esto en CADA vuelta de
   * loop() con el periodo de la zona actual. Si se reiniciara el cronómetro
   * en cada llamada, el LED no llegaría nunca a cambiar de estado y se
   * quedaría fijo. Sólo se reinicia cuando el periodo cambia de verdad. */
  void blink(uint16_t periodMs) {
    if (periodMs == s_blinkMs) return;
    s_blinkMs = periodMs;
    s_blinkT0 = millis();
  }

  /* Destello puntual: "acabo de recibir/mandar algo". */
  void pulse(uint16_t ms) { s_pulseOn = true; s_pulseUntil = millis() + ms; }

  void update(uint32_t now) {
    if (s_pulseOn) {
      if ((int32_t)(now - s_pulseUntil) < 0) { digitalWrite(PIN_LED, HIGH); return; }
      s_pulseOn = false;
    }
    if (s_blinkMs == 0) { digitalWrite(PIN_LED, LOW); s_blinkOn = false; return; }
    if (now - s_blinkT0 >= (uint32_t)(s_blinkMs / 2)) {
      s_blinkT0 = now;
      s_blinkOn = !s_blinkOn;
      digitalWrite(PIN_LED, s_blinkOn ? HIGH : LOW);
    }
  }

} // namespace led

namespace snd {

  static bool s_ready = false;
  static const uint8_t  LEDC_CHANNEL = 0;
  static const uint8_t  LEDC_BITS    = 10;
  static const uint32_t LEDC_DUTY    = 512;

  void begin() {
    if (PIN_BUZZER < 0) { trace("BUZZER: no hay zumbador cableado"); return; }
#if ESP_ARDUINO_VERSION_MAJOR >= 3
    ledcAttach(PIN_BUZZER, 2000, LEDC_BITS);
#else
    ledcSetup(LEDC_CHANNEL, 2000, LEDC_BITS);
    ledcAttachPin(PIN_BUZZER, LEDC_CHANNEL);
#endif
    s_ready = true;
    trace("BUZZER: listo");
  }

  static void output(uint16_t freqHz) {
    if (!s_ready) return;
#if ESP_ARDUINO_VERSION_MAJOR >= 3
    if (freqHz == 0) { ledcWrite(PIN_BUZZER, 0); return; }
    ledcWriteTone(PIN_BUZZER, freqHz);
#else
    if (freqHz == 0) { ledcWrite(LEDC_CHANNEL, 0); return; }
    ledcWriteTone(LEDC_CHANNEL, freqHz);
    ledcWrite(LEDC_CHANNEL, LEDC_DUTY);
#endif
  }

  void play(const buzzer::Pattern& p, bool force = false) { g_buzzer.play(p, millis(), force); }
  void stopIf(const buzzer::Pattern& p) { g_buzzer.stopIf(p); }
  void stop() { g_buzzer.stop(); }
  void mute(bool on) { g_buzzer.mute(on); }
  bool muted() { return g_buzzer.muted(); }

  void update(uint32_t now) {
    const buzzer::Out o = g_buzzer.update(now);
    if (o.changed) output(o.freqHz);
  }

} // namespace snd

/* ==========================================================================
 *  7. RADIO  (RadioLib, sin bloquear el loop)
 * --------------------------------------------------------------------------
 *  La radio pasa la vida ESCUCHANDO. Cuando hay algo que mandar se sale de
 *  escucha, se manda con startTransmit() (que vuelve enseguida) y se espera a
 *  que la interrupción de DIO1 avise de que terminó. Nada de transmit() a
 *  secas: eso bloquea medio segundo y con el botón de pánico de por medio no
 *  es aceptable.
 * ==========================================================================*/
namespace radio {

  enum class State : uint8_t { IDLE, RX, TX };

  static volatile bool s_dioFired = false;
  static State         s_state    = State::IDLE;
  static uint32_t      s_txStart  = 0;

  /* La ISR no hace NADA salvo levantar una bandera: todo el trabajo se hace
   * en loop(). Tocar RadioLib o Serial desde una interrupción es la forma
   * clásica de acabar con un cuelgue imposible de depurar. */
  static void IRAM_ATTR onDio1() { s_dioFired = true; }

  static const char* stateName() {
    switch (s_state) {
      case State::IDLE: return "IDLE";
      case State::RX:   return "ESCUCHANDO";
      case State::TX:   return "TRANSMITIENDO";
    }
    return "?";
  }

  /* Vuelve a escuchar y AVISA si no puede. Antes se ignoraba el código de
   * retorno: si startReceive() fallaba, la radio se quedaba sorda para
   * siempre con el firmware convencido de estar escuchando. */
  static uint32_t s_tRetry   = 0;
  static uint16_t s_rxErrors = 0;

  static bool listen() {
    const int st = g_radio.startReceive();
    if (st == RADIOLIB_ERR_NONE) { s_state = State::RX; return true; }
    ++s_rxErrors;
    s_state  = State::IDLE;
    s_tRetry = millis();
    Serial.printf("[t=%8lu ms] RADIO: no pudo ponerse a escuchar (codigo %d),"
                  " se reintenta en 2 s\n", (unsigned long)millis(), st);
    return false;
  }

  uint16_t rxErrors() { return s_rxErrors; }

  bool begin() {
    g_spi.begin(PIN_LORA_SCK, PIN_LORA_MISO, PIN_LORA_MOSI, PIN_LORA_NSS);

    const int st = g_radio.begin(LORA_FREQ_MHZ, LORA_BW_KHZ, LORA_SF, LORA_CR,
                                 LORA_SYNC_WORD, LORA_POWER_DBM, LORA_PREAMBLE,
                                 LORA_TCXO_V);
    if (st != RADIOLIB_ERR_NONE) {
      Serial.printf("[t=%8lu ms] RADIO: fallo al arrancar (codigo %d). "
                    "Revisa la antena y que la placa sea una Heltec V3.\n",
                    (unsigned long)millis(), st);
      return false;
    }

    /* En la Heltec V3 el conmutador de antena se controla por DIO2. Sin esta
     * línea la placa "transmite" sin que salga nada por la antena, que es un
     * fallo especialmente cruel porque todo parece funcionar. */
    g_radio.setDio2AsRfSwitch(true);
    g_radio.setCurrentLimit(140);          // el SX1262 a 14 dBm no pide más

    /* UNA sola acción para DIO1, no setPacketReceivedAction() +
     * setPacketSentAction(): las dos escriben en el mismo sitio y la segunda
     * borraría a la primera, así que se quedaría sin avisar de una de las dos
     * cosas. Con una bandera única, loop() mira en qué estado estaba y ya. */
    g_radio.setDio1Action(onDio1);

    return listen();
  }

  bool busy() { return s_state == State::TX; }

  /* Manda una trama ya codificada. Devuelve false si la radio estaba ocupada
   * (el llamante reintentará: nada se pierde en silencio). */
  bool send(const uint8_t* buf, size_t len) {
    if (!g_radioUp || s_state == State::TX) return false;
    const int st = g_radio.startTransmit(const_cast<uint8_t*>(buf), len);
    if (st != RADIOLIB_ERR_NONE) {
      Serial.printf("[t=%8lu ms] RADIO: startTransmit fallo (codigo %d)\n",
                    (unsigned long)millis(), st);
      listen();
      return false;
    }
    s_state   = State::TX;
    s_txStart = millis();
    ++g_txCount;
    led::pulse(30);
    return true;
  }

  /* Devuelve true si ha llegado una trama; deja los bytes en `buf`. */
  bool poll(uint8_t* buf, size_t cap, size_t* outLen, float* rssi, float* snr,
            uint32_t now) {
    // La radio se quedó sin poder escuchar: se reintenta cada 2 s.
    if (g_radioUp && s_state == State::IDLE && (now - s_tRetry) >= 2000) {
      s_tRetry = now;
      if (listen()) trace("RADIO: vuelve a escuchar");
    }

    /* Seguro por si se pierde la interrupción de fin de transmisión: sin esto
     * la radio se quedaría muda para siempre. Un paquete a SF12 no pasa de
     * ~2 s en el aire, así que 5 s es de sobra. */
    if (s_state == State::TX && (now - s_txStart) > 5000) {
      trace("RADIO: la transmision no dio senal de vida; se vuelve a escuchar");
      g_radio.finishTransmit();
      listen();
      /* Se limpia la bandera: si la interrupción llegase justo después del
       * rescate, la vuelta siguiente la tomaría por una recepción estando ya
       * en RX y contaría una trama mala fantasma. */
      s_dioFired = false;
    }

    /* Leer y borrar de un golpe: si la interrupción cayera entre la lectura y
     * el borrado, ese aviso se perdería. */
    if (!__atomic_exchange_n(&s_dioFired, false, __ATOMIC_SEQ_CST)) return false;

    if (s_state == State::TX) {
      g_radio.finishTransmit();
      listen();
      return false;
    }

    const size_t len = g_radio.getPacketLength();
    if (len == 0 || len > cap) {
      ++g_rxBad;
      listen();
      return false;
    }

    const int st = g_radio.readData(buf, len);
    *rssi = g_radio.getRSSI();
    *snr  = g_radio.getSNR();
    listen();

    if (st != RADIOLIB_ERR_NONE) { ++g_rxBad; return false; }
    *outLen = len;
    led::pulse(30);
    return true;
  }

} // namespace radio

/* ==========================================================================
 *  8. ENVÍO DE TRAMAS
 * ==========================================================================*/
static uint8_t  g_state_flags = 0;      // flags que se anuncian en cada trama
static int32_t  g_lat1e7      = 0;
static int32_t  g_lon1e7      = 0;
static uint8_t  g_battery     = link::BATTERY_UNKNOWN;

static bool sendFrame(link::Type type, uint8_t aux0 = 0, uint8_t aux1 = 0) {
  link::Frame f;
  f.type    = type;
  f.src     = MY_ID;
  f.dst     = PEER_ID;
  f.seq     = s_seq;
  f.flags   = g_state_flags;
  f.battery = g_battery;
  f.lat1e7  = g_lat1e7;
  f.lon1e7  = g_lon1e7;
  f.aux0    = aux0;
  f.aux1    = aux1;

  uint8_t buf[link::FRAME_SIZE];
  const size_t n = link::encode(f, g_key, buf, sizeof(buf));
  if (n == 0) return false;
  if (!radio::send(buf, n)) return false;

  ++s_seq;   // sólo avanza si de verdad salió: así el otro lado no ve huecos
  seqTick();
  Serial.printf("[t=%8lu ms] >>> TX  %-6s seq=%u aux0=%u flags=0x%02X\n",
                (unsigned long)millis(), link::typeName(type),
                (unsigned)f.seq, (unsigned)aux0, (unsigned)f.flags);
  return true;
}

/* ==========================================================================
 *  8-bis. BATERÍA (opcional)
 * --------------------------------------------------------------------------
 *  En la Heltec V3 el divisor de la batería va a GPIO1 y sólo se conecta
 *  cuando ADC_Ctrl (GPIO37) está a nivel BAJO. Sin cablear (-1) se anuncia
 *  "desconocida" y el buscador no enseña ningún porcentaje inventado.
 * ==========================================================================*/
namespace battery {

  static const float    DIVIDER   = 4.9f;   // divisor de fábrica de la V3
  static const float    FULL_V    = 4.15f;
  static const float    EMPTY_V   = 3.30f;
  static const uint8_t  LOW_PCT   = 20;
  static float    s_volts = 0.0f;
  static bool     s_has   = false;

  void begin() {
    if (PIN_VBAT < 0) return;
    analogReadResolution(12);
    analogSetPinAttenuation(PIN_VBAT, ADC_11db);
    pinMode(PIN_VBAT_CTRL_CFG, OUTPUT);
    digitalWrite(PIN_VBAT_CTRL_CFG, HIGH);   // divisor desconectado en reposo
  }

  void poll(uint32_t now) {
    if (PIN_VBAT < 0) return;
    static uint32_t tLast = 0;
    if (s_has && (now - tLast) < 30000) return;
    tLast = now;

    digitalWrite(PIN_VBAT_CTRL_CFG, LOW);    // conecta el divisor
    uint32_t acc = 0;
    for (int i = 0; i < 8; ++i) acc += analogReadMilliVolts(PIN_VBAT);
    digitalWrite(PIN_VBAT_CTRL_CFG, HIGH);   // y lo vuelve a soltar

    s_volts = (acc / 8.0f) * DIVIDER / 1000.0f;
    s_has   = true;

    float p = (s_volts - EMPTY_V) / (FULL_V - EMPTY_V) * 100.0f;
    if (p < 0.0f)   p = 0.0f;
    if (p > 100.0f) p = 100.0f;
    g_battery = (uint8_t)(p + 0.5f);
    if (g_battery <= LOW_PCT) g_state_flags |= link::Flag::LOW_BATTERY;
    else g_state_flags = (uint8_t)(g_state_flags & ~link::Flag::LOW_BATTERY);
  }

  bool  has()   { return s_has; }
  float volts() { return s_volts; }

} // namespace battery

/* ==========================================================================
 *  9. GPS (opcional, el mismo lib/nmea de la DevKit)
 * ==========================================================================*/
#ifndef GPS_UART_ENABLED
  #define GPS_UART_ENABLED 0
#endif
#ifndef GPS_RX_PIN
  #define GPS_RX_PIN 4            // pin libre de la V3; cambia si te estorba
#endif
#ifndef GPS_BAUD
  #define GPS_BAUD 9600
#endif

namespace gps {

  static nmea::Parser s_parser;
  static uint32_t     s_tFix = 0;     // millis() del último fix válido
  static bool         s_has  = false;

  /* Una posición de hace media hora no es "la posición": si el GPS pierde el
   * fix hay que dejar de decir que lo tenemos, o el buscador enseñaría la
   * última posición conocida como si fuera de ahora mismo. */
  static const uint32_t FIX_VALIDO_MS = 120000;

  void begin() {
#if GPS_UART_ENABLED
    Serial1.begin(GPS_BAUD, SERIAL_8N1, GPS_RX_PIN, -1);
    trace("GPS: UART1 abierto, escuchando tramas NMEA");
#else
    trace("GPS: sin modulo fisico (usa el comando NMEA para inyectar tramas)");
#endif
  }

  static void adopt() {
    const nmea::Fix& f = s_parser.fix();
    if (!f.valid) return;
    g_lat1e7 = link::degToFixed(f.lat);
    g_lon1e7 = link::degToFixed(f.lon);
    g_state_flags |= link::Flag::HAS_FIX;
    s_tFix = millis();
    s_has  = true;
  }

  /* Caduca el fix si hace demasiado que no llega uno nuevo. */
  void expire(uint32_t now) {
    if (!s_has) return;
    if ((now - s_tFix) > FIX_VALIDO_MS) {
      s_has = false;
      g_state_flags = (uint8_t)(g_state_flags & ~link::Flag::HAS_FIX);
      trace("GPS: el fix ha caducado; se deja de anunciar posicion");
    }
  }

  void poll() {
#if GPS_UART_ENABLED
    for (int i = 0; i < 96 && Serial1.available(); ++i) {
      if (s_parser.feed((char)Serial1.read())) adopt();
    }
#endif
  }

  bool inject(const char* line) {
    const bool ok = s_parser.feedLine(line);
    if (ok) adopt();
    return ok;
  }

  const nmea::Fix& fix() { return s_parser.fix(); }

} // namespace gps

/* ==========================================================================
 *  10. LO PROPIO DE CADA PAPEL
 * ==========================================================================*/
#if IS_TAG
/* --------------------------------------------------------------------------
 *  LLAVERO
 * --------------------------------------------------------------------------
 *  · Baliza periódica (lenta o rápida).
 *  · Botón de pánico con la MISMA máquina de estados que la DevKit, así que
 *    el comportamiento —y la ventana de cancelación— son idénticos.
 *  · La alerta se reintenta hasta que el buscador la confirma con un ACK.
 *  · Obedece las órdenes del buscador: pitar, callar, mandar posición.
 * ------------------------------------------------------------------------*/
static const uint32_t DEBOUNCE_MS      = 50;
static const uint32_t LONG_PRESS_MS    = 3000;
static const uint32_t CANCEL_WINDOW_MS = 10000;

static panic::Fsm g_fsm(panic::Config{DEBOUNCE_MS, LONG_PRESS_MS, CANCEL_WINDOW_MS});

static uint32_t g_beaconEveryMs = BEACON_SLOW_MS;
static uint32_t g_tLastBeacon   = 0;

static bool     g_alertPending  = false;   // esperando ACK
static uint16_t g_alertSeq      = 0;       // seq del último envío, para casar el ACK
static uint8_t  g_alertCode     = 0;       // 1 = corta, 2 = prolongada
static uint8_t  g_alertTries    = 0;
static uint32_t g_tAlertSent    = 0;
static bool     g_findActive    = false;

static inline bool buttonPressed() {
#if BUTTON_ACTIVE_HIGH
  return digitalRead(PIN_BUTTON) == HIGH;
#else
  return digitalRead(PIN_BUTTON) == LOW;
#endif
}

static void setFind(bool on) {
  if (g_findActive == on) return;
  g_findActive = on;
  if (on) {
    // Fondo, no evento: una alerta puede interrumpirlo y luego vuelve solo.
    g_buzzer.setBackground(&buzzer::PATTERN_FIND);
    led::blink(300);
    g_state_flags |= link::Flag::FIND_ACTIVE;
    trace("FIND: el buscador me esta buscando -> pitando");
  } else {
    snd::stopIf(buzzer::PATTERN_FIND);   // stopIf también quita el fondo
    led::blink(0);
    g_state_flags = (uint8_t)(g_state_flags & ~link::Flag::FIND_ACTIVE);
    trace("FIND: encontrado, dejo de pitar");
  }
}

static void setFastRate(bool fast) {
  g_beaconEveryMs = fast ? BEACON_FAST_MS : BEACON_SLOW_MS;
  if (fast) g_state_flags |= link::Flag::FAST_RATE;
  else      g_state_flags = (uint8_t)(g_state_flags & ~link::Flag::FAST_RATE);
  Serial.printf("[t=%8lu ms] BALIZA: cada %lu ms\n",
                (unsigned long)millis(), (unsigned long)g_beaconEveryMs);
}

/* Arranca un envío con acuse de recibo. `code` es 1 (pulsación corta), 2
 * (prolongada) o 0 (cancelación).
 *
 * La CANCELACIÓN pasa por aquí a propósito: antes se mandaba una sola vez y,
 * si la radio estaba ocupada en ese instante, se perdía sin más. Entonces el
 * buscador seguía con la alarma puesta por algo que el usuario ya había
 * anulado, que es la peor forma posible de fallar. */
static void startAlert(uint8_t code) {
  g_alertPending = true;
  g_alertCode    = code;
  g_alertTries   = 0;
  g_tAlertSent   = 0;                  // fuerza el primer envío en este mismo tick
  if (code != 0) {
    g_state_flags |= link::Flag::ALERT_ACTIVE;
    setFastRate(true);                 // con alerta activa, posición más seguido
  }
}

static void clearAlert(const char* why) {
  if (!g_alertPending && !(g_state_flags & link::Flag::ALERT_ACTIVE)) return;
  g_alertPending = false;
  g_state_flags  = (uint8_t)(g_state_flags & ~link::Flag::ALERT_ACTIVE);
  setFastRate(false);
  trace(why);
}

static void tagButton(uint32_t now) {
  const panic::Step s = g_fsm.update(now, buttonPressed());

  switch (s.event) {
    case panic::Event::ALERT_SHORT:
      snd::play(buzzer::PATTERN_ALERT_SHORT);
      startAlert(1);
      break;
    case panic::Event::ALERT_LONG:
      snd::play(buzzer::PATTERN_ALERT_LONG);
      startAlert(2);
      break;
    case panic::Event::CANCEL:
      snd::play(buzzer::PATTERN_CANCEL);
      clearAlert("CANCEL: alerta anulada por el usuario");
      startAlert(0);          // se reintenta hasta que el buscador la confirme
      break;
    case panic::Event::WINDOW_EXPIRED:
      snd::play(buzzer::PATTERN_CONFIRMED);
      trace("VENTANA EXPIRADA: la alerta queda CONFIRMADA");
      break;
    case panic::Event::CANCEL_ARMED:
      trace("CANCEL_WINDOW: boton liberado -> cancelacion ARMADA");
      break;
    default:
      break;
  }

  if (s.changed) {
    Serial.printf("[t=%8lu ms] FSM  %-13s -> %-13s  (%s)\n",
                  (unsigned long)now, panic::Fsm::stateName(s.from),
                  panic::Fsm::stateName(s.to), s.reason);
  }
}

static void tagRadio(uint32_t now) {
  // (a) Alerta pendiente de confirmación: se reintenta hasta que llegue el ACK.
  if (g_alertPending) {
    if (g_tAlertSent == 0 || (now - g_tAlertSent) >= ALERT_RETRY_MS) {
      if (g_alertTries >= ALERT_MAX_RETRIES) {
        clearAlert(g_alertCode == 0
                       ? "CANCELACION: sin respuesta del buscador tras todos los reintentos"
                       : "ALERTA: sin respuesta del buscador tras todos los reintentos");
        snd::play(buzzer::PATTERN_LINK_LOST);
      } else if (sendFrame(link::Type::ALERT, g_alertCode)) {
        g_alertSeq = (uint16_t)(s_seq - 1);   // sendFrame ya lo incrementó
        ++g_alertTries;
        g_tAlertSent = now;
        Serial.printf("[t=%8lu ms] ALERTA: envio %u de %u, esperando ACK\n",
                      (unsigned long)now, (unsigned)g_alertTries,
                      (unsigned)ALERT_MAX_RETRIES);
      }
    }
    return;   // mientras hay alerta, la radio es para eso
  }

  // (b) Baliza periódica.
  if ((now - g_tLastBeacon) >= g_beaconEveryMs) {
    if (sendFrame(link::Type::BEACON)) g_tLastBeacon = now;
  }
}

static void tagOnFrame(const link::Frame& f, float rssi, float snr) {
  Serial.printf("[t=%8lu ms] <<< RX  %-6s de 0x%04X  RSSI %.0f dBm  SNR %.1f dB\n",
                (unsigned long)millis(), link::typeName(f.type),
                (unsigned)f.src, (double)rssi, (double)snr);

  switch (f.type) {
    case link::Type::ACK:
      /* Se comprueba que el ACK confirma ESTE envío y no uno anterior: el
       * buscador devuelve en aux1 el byte bajo del seq y en aux0 el código.
       * Sin esta comprobación, un ACK retrasado de la alerta anterior daría
       * por confirmada una cancelación que el buscador nunca recibió, y la
       * alarma se quedaría sonando por algo ya anulado. */
      if (g_alertPending &&
          f.aux1 == (uint8_t)(g_alertSeq & 0xFF) && f.aux0 == g_alertCode) {
        const bool eraCancelacion = (g_alertCode == 0);
        g_alertPending = false;
        trace(eraCancelacion ? "CANCELACION CONFIRMADA por el buscador (ACK recibido)"
                             : "ALERTA CONFIRMADA por el buscador (ACK recibido)");
        snd::play(buzzer::PATTERN_CONFIRMED);
      } else if (g_alertPending) {
        Serial.printf("[t=%8lu ms] ACK ignorado: confirma otro envio"
                      " (aux0=%u aux1=%u, esperaba aux0=%u aux1=%u)\n",
                      (unsigned long)millis(), (unsigned)f.aux0, (unsigned)f.aux1,
                      (unsigned)g_alertCode, (unsigned)(g_alertSeq & 0xFF));
      }
      break;

    case link::Type::CMD: {
      const link::Cmd c = (link::Cmd)f.aux0;
      Serial.printf("[t=%8lu ms] ORDEN recibida: %s\n",
                    (unsigned long)millis(), link::cmdName(c));
      switch (c) {
        case link::Cmd::FIND_ON:  setFind(true);  break;
        case link::Cmd::FIND_OFF: setFind(false); break;
        case link::Cmd::FAST_ON:  setFastRate(true);  break;
        case link::Cmd::FAST_OFF:
          /* Volver a la cadencia de ahorro es también la forma que tiene el
           * buscador de decir "ya está atendido": si no, el llavero se
           * quedaría marcando alerta y gastando batería para siempre, porque
           * el ACK sólo dice "me llegó", no "ya pasó". */
          clearAlert("ALERTA cerrada por el buscador");
          setFastRate(false);
          break;
        case link::Cmd::WHERE:    break;   // se responde abajo con un STATUS
        default: break;
      }
      sendFrame(link::Type::STATUS, f.aux0);   // siempre se acusa la orden
      break;
    }

    default:
      break;   // un llavero no hace caso de balizas de otro llavero
  }
}
#endif  // IS_TAG

#if IS_FINDER
/* --------------------------------------------------------------------------
 *  BUSCADOR
 * --------------------------------------------------------------------------
 *  · Oye al llavero y pinta cómo de cerca está (zona, barras, distancia).
 *  · Confirma las alertas con un ACK para que el llavero deje de reintentar.
 *  · Modo BÚSQUEDA: pita cada vez más seguido según te acercas, igual que el
 *    "caliente/frío" del AirTag, y le dice al llavero que pite él también.
 * ------------------------------------------------------------------------*/
/* El plazo de silencio se ata a la cadencia de la baliza: con un valor fijo
 * más corto que dos balizas, perder UNA trama ya diría "SIN SEÑAL". */
static ranging::Estimator g_est(ranging::Config(-55.0f, -85.0f, -110.0f, -45.0f,
                                                2.7f, 0.35f, BEACON_SLOW_MS * 3));
static bool     g_findMode    = false;
static uint32_t g_tLastBeep   = 0;
static bool     g_alarmOn     = false;   // hay una alerta del llavero sin atender
static uint32_t g_tLastReport = 0;

static inline bool buttonPressed() {
#if BUTTON_ACTIVE_HIGH
  return digitalRead(PIN_BUTTON) == HIGH;
#else
  return digitalRead(PIN_BUTTON) == LOW;
#endif
}

/* Cola de órdenes.
 *
 * La radio sólo puede estar transmitiendo una cosa a la vez, así que dos
 * órdenes seguidas (FIND_ON + FAST_ON, que es justo lo que hace el modo
 * búsqueda) perdían la segunda por el camino: `sendFrame` devolvía false y
 * nadie se enteraba. Ahora se encolan y salen en cuanto la radio queda libre,
 * respetando además un hueco entre envíos para no pisarse a sí mismas. */
static const uint8_t  CMD_QUEUE_LEN = 6;
static const uint32_t CMD_GAP_MS    = 250;
static link::Cmd g_cmdQueue[CMD_QUEUE_LEN];
static uint8_t   g_cmdCount = 0;
static uint32_t  g_tLastCmd = 0;

static void finderSendCmd(link::Cmd c) {
  if (g_cmdCount >= CMD_QUEUE_LEN) {
    trace("ORDEN descartada: la cola esta llena (la radio no da mas de si)");
    return;
  }
  g_cmdQueue[g_cmdCount++] = c;
  Serial.printf("[t=%8lu ms] ORDEN encolada: %s\n",
                (unsigned long)millis(), link::cmdName(c));
}

static void finderCmdQueue(uint32_t now) {
  if (g_cmdCount == 0 || radio::busy()) return;
  if ((now - g_tLastCmd) < CMD_GAP_MS)  return;
  if (!sendFrame(link::Type::CMD, (uint8_t)g_cmdQueue[0])) return;
  g_tLastCmd = now;
  for (uint8_t i = 1; i < g_cmdCount; ++i) g_cmdQueue[i - 1] = g_cmdQueue[i];
  --g_cmdCount;
}

static void setFindMode(bool on) {
  if (g_findMode == on) return;
  g_findMode = on;
  finderSendCmd(on ? link::Cmd::FIND_ON : link::Cmd::FIND_OFF);
  finderSendCmd(on ? link::Cmd::FAST_ON : link::Cmd::FAST_OFF);
  if (!on) { snd::stop(); led::blink(0); }
  trace(on ? "BUSQUEDA: activada (caliente/frio)" : "BUSQUEDA: desactivada");
}

/* La línea que se mira en la expo: una por trama recibida. */
static void finderReport(const link::Frame& f, float rssi, float snr) {
  const ranging::Zone z = g_est.zone();
  char barras[6] = "-----";
  const uint8_t n = ranging::zoneBars(z);
  for (uint8_t i = 0; i < 5; ++i) barras[i] = (i < n) ? '#' : '.';

  Serial.printf("[t=%8lu ms] %-6s de 0x%04X  [%s] %-9s  ~%.0f m  "
                "RSSI %.0f (suav. %.0f) SNR %.1f\n",
                (unsigned long)millis(), link::typeName(f.type), (unsigned)f.src,
                barras, ranging::zoneName(z), (double)g_est.distanceM(),
                (double)rssi, (double)g_est.rssi(), (double)snr);

  if (f.has(link::Flag::HAS_FIX)) {
    Serial.printf("             posicion: %.6f, %.6f   "
                  "https://maps.google.com/?q=%.6f,%.6f\n",
                  link::fixedToDeg(f.lat1e7), link::fixedToDeg(f.lon1e7),
                  link::fixedToDeg(f.lat1e7), link::fixedToDeg(f.lon1e7));
  }
  if (f.battery != link::BATTERY_UNKNOWN) {
    Serial.printf("             bateria del llavero: %u %%%s\n",
                  (unsigned)f.battery,
                  f.has(link::Flag::LOW_BATTERY) ? "  << BAJA" : "");
  }
  const int8_t t = g_est.trend();
  if (g_findMode && t != 0) {
    Serial.println(t > 0 ? "             >>> TE ESTAS ACERCANDO"
                         : "             <<< te estas alejando");
  }
}

static void finderOnFrame(const link::Frame& f, float rssi, float snr) {
  g_est.push(rssi, millis());

  switch (f.type) {
    case link::Type::ALERT:
      if (f.aux0 == 0) {
        g_alarmOn = false;
        g_buzzer.setBackground(nullptr);
        snd::stop();
        led::blink(0);
        trace("*** EL LLAVERO ANULO LA ALERTA ***");
      } else {
        g_alarmOn = true;
        Serial.printf("\n*** ALERTA %s DEL LLAVERO 0x%04X ***\n",
                      f.aux0 == 2 ? "PROLONGADA (ALERT:2)" : "CORTA (ALERT:1)",
                      (unsigned)f.src);
        g_buzzer.setBackground(&buzzer::PATTERN_BEACON);  // suena hasta que se atienda
        snd::play(buzzer::PATTERN_ALERT_LONG, /*force=*/true);
        led::blink(200);
      }
      sendFrame(link::Type::ACK, f.aux0, (uint8_t)(f.seq & 0xFF));   // confirmar SIEMPRE
      break;

    case link::Type::BEACON:
    case link::Type::STATUS:
      break;

    default:
      break;
  }

  finderReport(f, rssi, snr);
}

/* "Caliente/frío": el pitido se acelera al acercarse. */
static void finderBeeps(uint32_t now) {
  if (!g_findMode || g_alarmOn) return;
  const uint32_t period = ranging::beepPeriodMs(g_est.zone());
  if (period == 0) { led::blink(0); return; }
  led::blink((uint16_t)period);
  if ((now - g_tLastBeep) >= period) {
    g_tLastBeep = now;
    snd::play(buzzer::PATTERN_PING, /*force=*/true);   // pitido corto
  }
}
#endif  // IS_FINDER

/* ==========================================================================
 *  11. COMANDOS POR EL MONITOR SERIE
 * ==========================================================================*/
static const size_t CMD_BUF_SIZE = 128;
static void printHelp();
static void printStatus();

static void handleCommand(const char* raw) {
  char cmd[CMD_BUF_SIZE];
  size_t j = 0;
  for (size_t i = 0; raw[i] && j < sizeof(cmd) - 1; ++i) {
    const char ch = raw[i];
    if (ch == '\r' || ch == '\n') continue;
    if (j == 0 && ch == ' ')      continue;
    cmd[j++] = (char)toupper((unsigned char)ch);
  }
  while (j > 0 && cmd[j - 1] == ' ') j--;
  cmd[j] = '\0';

  if (strcmp(cmd, "?") == 0 || strcmp(cmd, "HELP") == 0) {
    printHelp();
  } else if (strcmp(cmd, "STATUS") == 0 || strcmp(cmd, "POS") == 0) {
    printStatus();
  } else if (strcmp(cmd, "MUTE:ON") == 0) {
    snd::mute(true);  trace("Zumbador SILENCIADO");
  } else if (strcmp(cmd, "MUTE:OFF") == 0) {
    snd::mute(false); trace("Zumbador ACTIVO");
  } else if (strcmp(cmd, "BEEP") == 0) {
    snd::play(buzzer::PATTERN_BOOT, true);
  } else if (strncmp(cmd, "NMEA ", 5) == 0) {
    trace(gps::inject(cmd + 5) ? "NMEA: trama ACEPTADA" : "NMEA: trama DESCARTADA");
#if IS_FINDER
  } else if (strcmp(cmd, "FIND") == 0) {
    setFindMode(true);
  } else if (strcmp(cmd, "STOP") == 0) {
    /* Si había una alarma, se le dice al llavero que ya está atendida aunque
     * no estuviéramos en modo búsqueda: si no, se quedaría marcando alerta y
     * balizando rápido hasta quedarse sin batería. */
    const bool habiaAlarma = g_alarmOn;
    setFindMode(false);
    g_alarmOn = false;
    g_buzzer.setBackground(nullptr);
    snd::stop();
    led::blink(0);
    if (habiaAlarma && !g_findMode) finderSendCmd(link::Cmd::FAST_OFF);
    trace("Alarma atendida");
  } else if (strcmp(cmd, "WHERE") == 0) {
    finderSendCmd(link::Cmd::WHERE);
  } else if (strcmp(cmd, "FAST") == 0) {
    finderSendCmd(link::Cmd::FAST_ON);
  } else if (strcmp(cmd, "SLOW") == 0) {
    finderSendCmd(link::Cmd::FAST_OFF);
  } else if (strncmp(cmd, "CAL ", 4) == 0) {
    /* Calibración del modelo de distancia: se pone el llavero a un metro
     * exacto, se mira el RSSI que sale en las trazas y se escribe aquí.
     * Es la diferencia entre "unos 8 m" y "unos 80 m". */
    ranging::Config c = g_est.config();
    c.rssiAt1m = (float)atof(cmd + 4);
    g_est.setConfig(c);
    Serial.printf("[t=%8lu ms] CALIBRACION: RSSI a 1 m = %.1f dBm\n",
                  (unsigned long)millis(), (double)c.rssiAt1m);
#endif
#if IS_TAG
  } else if (strcmp(cmd, "FIND") == 0) {
    setFind(true);
  } else if (strcmp(cmd, "STOP") == 0) {
    setFind(false);
  } else if (strcmp(cmd, "TEST") == 0) {
    // Dispara una alerta sin tocar el botón (para probar el enlace solo).
    startAlert(1);
    trace("TEST: alerta simulada");
#endif
  } else {
    Serial.printf("[t=%8lu ms] comando no reconocido: \"%s\"  (escribe ? para la ayuda)\n",
                  (unsigned long)millis(), cmd);
  }
}

static void serialPoll() {
  static char   line[CMD_BUF_SIZE];
  static size_t len = 0;
  while (Serial.available()) {
    const char c = (char)Serial.read();
    if (c == '\n' || c == '\r') {
      if (len > 0) { line[len] = '\0'; handleCommand(line); len = 0; }
    } else if (len < sizeof(line) - 1) {
      line[len++] = c;
    }
  }
}

/* ==========================================================================
 *  12. BANNER, AYUDA Y ESTADO
 * ==========================================================================*/
static void printHelp() {
  Serial.println();
  Serial.println("  Comandos:");
  Serial.println("     STATUS/POS   estado del enlace, posicion y contadores");
  Serial.println("     MUTE:ON/OFF  silenciar o no el zumbador");
  Serial.println("     BEEP         prueba de zumbador");
  Serial.println("     NMEA <trama> inyecta una sentencia NMEA (para probar sin GPS)");
#if IS_FINDER
  Serial.println("     FIND         modo busqueda: el llavero pita y el buscador");
  Serial.println("                  pita cada vez mas seguido segun te acercas");
  Serial.println("     STOP         salir del modo busqueda / callar la alarma");
  Serial.println("     WHERE        pedir posicion al llavero ahora mismo");
  Serial.println("     FAST / SLOW  cambiar la cadencia de la baliza del llavero");
  Serial.println("     CAL <dBm>    calibrar el RSSI a 1 m (mejora la distancia)");
  Serial.println("     El boton PRG entra y sale del modo busqueda.");
#endif
#if IS_TAG
  Serial.println("     FIND / STOP  hacer que este llavero pite o se calle");
  Serial.println("     TEST         disparar una alerta sin tocar el boton");
  Serial.println("     El boton de panico es fisico: pulsacion corta = ALERT:1,");
  Serial.println("     mantenerlo 3 s = ALERT:2, y otra pulsacion dentro de los");
  Serial.println("     10 s siguientes la CANCELA.");
#endif
  Serial.println();
}

static void printStatus() {
  const uint32_t now = millis();
  Serial.println();
  Serial.println("  --- ESTADO -------------------------------------------");
  Serial.printf ("  Papel                : %s  (id 0x%04X -> 0x%04X)\n",
                 ROLE_LABEL, (unsigned)MY_ID, (unsigned)PEER_ID);
  Serial.printf ("  Radio                : %s, %s\n",
                 g_radioUp ? "lista" : "NO DISPONIBLE", radio::stateName());
  Serial.printf ("  Encendida desde hace : %lu s\n", (unsigned long)(now / 1000));
  Serial.printf ("  Tramas  TX=%lu  RX ok=%lu  descartadas=%lu\n",
                 (unsigned long)g_txCount, (unsigned long)g_rxOk,
                 (unsigned long)g_rxBad);
  if (g_everHeard) {
    Serial.printf ("  Ultima trama del otro lado hace %lu s\n",
                   (unsigned long)((now - g_tLastHeard) / 1000));
  } else {
    Serial.println("  Todavia no se ha oido nada del otro lado");
  }
  if (PIN_VBAT >= 0 && battery::has()) {
    Serial.printf ("  Bateria propia       : %.2f V (~%u %%)\n",
                   (double)battery::volts(), (unsigned)g_battery);
  }
  const nmea::Fix& f = gps::fix();
  if (f.valid) {
    Serial.printf ("  Posicion propia      : %.6f, %.6f\n", f.lat, f.lon);
  } else {
    Serial.println("  Posicion propia      : sin fix");
  }
#if IS_FINDER
  Serial.printf ("  Modo busqueda        : %s\n", g_findMode ? "ACTIVO" : "apagado");
  Serial.printf ("  Cercania             : %s  (~%.0f m, RSSI suav. %.0f dBm)\n",
                 ranging::zoneName(g_est.zone()), (double)g_est.distanceM(),
                 (double)g_est.rssi());
#endif
#if IS_TAG
  Serial.printf ("  Baliza cada          : %lu ms\n", (unsigned long)g_beaconEveryMs);
  Serial.printf ("  Alerta pendiente     : %s\n", g_alertPending ? "SI (esperando ACK)" : "no");
  Serial.printf ("  Pitando (FIND)       : %s\n", g_findActive ? "SI" : "no");
#endif
  Serial.println("  ------------------------------------------------------");
  Serial.println();
}

static void printBanner() {
  Serial.println();
  Serial.println("============================================================");
  Serial.println("  GEO-EXPO ALERT  -  Enlace LoRa punto a punto");
  Serial.printf ("  Papel            : %s\n", ROLE_LABEL);
  Serial.println("  Placa            : Heltec WiFi LoRa 32 V3 (ESP32-S3 + SX1262)");
  Serial.printf ("  Radio            : %.1f MHz  BW %.0f kHz  SF%d  CR 4/%d  %d dBm\n",
                 (double)LORA_FREQ_MHZ, (double)LORA_BW_KHZ, (int)LORA_SF,
                 (int)LORA_CR, (int)LORA_POWER_DBM);
  Serial.printf ("  Ids              : yo 0x%04X   el otro 0x%04X\n",
                 (unsigned)MY_ID, (unsigned)PEER_ID);
  Serial.printf ("  Trama            : %u bytes, firmada (SipHash-2-4)\n",
                 (unsigned)link::FRAME_SIZE);
  Serial.printf ("  LED GPIO%-3d  Boton GPIO%-3d  Zumbador %s\n",
                 PIN_LED, PIN_BUTTON,
                 PIN_BUZZER >= 0 ? "cableado" : "no cableado");
  Serial.println("  ANTENA: no enciendas la radio sin ella puesta.");
  Serial.println("============================================================");
  printHelp();
}

/* ==========================================================================
 *  13. setup() / loop()
 * ==========================================================================*/
void setup() {
  Serial.begin(115200);
  const uint32_t t0 = millis();
  while (!Serial && (millis() - t0) < 2000) { /* el S3 usa USB-CDC */ }

#if BUTTON_ACTIVE_HIGH
  pinMode(PIN_BUTTON, INPUT_PULLDOWN);
#else
  pinMode(PIN_BUTTON, INPUT_PULLUP);
#endif
  led::begin();
  snd::begin();
  battery::begin();

  g_key = link::keyFromString(GEO_LINK_KEY);
  g_guard.reset();
  if (s_seqMagic != SEQ_MAGIC) {   // arranque en frío: la RTC trae basura
    s_seqMagic = SEQ_MAGIC;
    s_seq      = 0;
  }
  seqBegin();
  Serial.printf("[t=%8lu ms] ENLACE: contador de secuencia en %u\n",
                (unsigned long)millis(), (unsigned)s_seq);

  printBanner();
  gps::begin();

#if IS_TAG
  g_fsm.begin(buttonPressed(), millis());
#endif

  g_radioUp = radio::begin();
  if (g_radioUp) {
    trace("RADIO: escuchando");
    snd::play(buzzer::PATTERN_BOOT);
  } else {
    /* Sin radio el aparato no sirve para lo suyo, pero NO se reinicia en
     * bucle: se queda avisando por serie para poder depurarlo con calma. */
    trace("RADIO: sin radio. Revisa antena, pines y modelo de placa.");
    snd::play(buzzer::PATTERN_LINK_LOST);
    led::blink(120);
  }
}

void loop() {
  const uint32_t now = millis();

  serialPoll();
  gps::poll();
  gps::expire(now);
  battery::poll(now);

  /* --- recepción -------------------------------------------------------- */
  uint8_t buf[link::FRAME_SIZE + 8];
  size_t  len  = 0;
  float   rssi = 0.0f, snr = 0.0f;
  if (radio::poll(buf, sizeof(buf), &len, &rssi, &snr, now)) {
    link::Frame f;
    const link::Result r = link::decode(buf, len, g_key, &f);
    if (r != link::Result::OK) {
      ++g_rxBad;
      Serial.printf("[t=%8lu ms] RX descartada: %s  (RSSI %.0f dBm)\n",
                    (unsigned long)now, link::resultName(r), (double)rssi);
    } else if (f.dst != MY_ID && f.dst != link::ID_BROADCAST) {
      ++g_rxBad;   // no es para nosotros
    } else if (!g_guard.accept(f.src, f.seq)) {
      ++g_rxBad;
      Serial.printf("[t=%8lu ms] RX descartada: repetida (src 0x%04X seq %u)\n",
                    (unsigned long)now, (unsigned)f.src, (unsigned)f.seq);
      // Varias seguidas del mismo emisor = se ha reiniciado, no es un ataque.
      if (f.src == g_replaySrc) { ++g_replayStreak; }
      else                      { g_replaySrc = f.src; g_replayStreak = 1; }
      if (g_replayStreak >= RESYNC_AFTER) {
        g_guard.forget(f.src);
        g_replayStreak = 0;
        Serial.printf("[t=%8lu ms] El emisor 0x%04X parece haberse reiniciado:"
                      " se resincroniza el contador\n",
                      (unsigned long)now, (unsigned)f.src);
        // Y se ATIENDE esta misma trama: si no, la orden que destapó el
        // problema se perdería también y habría que repetirla otra vez más.
        if (g_guard.accept(f.src, f.seq)) {
          --g_rxBad;
          ++g_rxOk;
          g_tLastHeard   = now;
          g_everHeard    = true;
          g_linkLostSaid = false;
#if IS_TAG
          tagOnFrame(f, rssi, snr);
#else
          finderOnFrame(f, rssi, snr);
#endif
        }
      }
    } else {
      ++g_rxOk;
      g_replayStreak = 0;
      g_tLastHeard   = now;
      g_everHeard    = true;
      g_linkLostSaid = false;
#if IS_TAG
      tagOnFrame(f, rssi, snr);
#else
      finderOnFrame(f, rssi, snr);
#endif
    }
  }

  /* --- enlace perdido --------------------------------------------------- */
  if (g_everHeard && !g_linkLostSaid && (now - g_tLastHeard) > LINK_LOST_MS) {
    g_linkLostSaid = true;
    trace("ENLACE: no se oye al otro lado desde hace rato");
    snd::play(buzzer::PATTERN_LINK_LOST);
  }

  /* --- lo propio de cada papel ------------------------------------------ */
#if IS_TAG
  tagButton(now);
  tagRadio(now);
#else
  g_est.update(now);
  finderCmdQueue(now);
  finderBeeps(now);

  // El botón PRG del buscador entra y sale del modo búsqueda (flanco, con
  // antirrebote sencillo: aquí no hace falta la FSM entera del pánico).
  static bool     lastBtn = false;
  static uint32_t tBtn    = 0;
  const bool nowBtn = buttonPressed();
  if (nowBtn != lastBtn && (now - tBtn) > 60) {
    tBtn    = now;
    lastBtn = nowBtn;
    if (nowBtn) {
      if (g_alarmOn) {
        // Mismo criterio que el comando STOP: silenciar la alarma es decirle
        // al llavero que ya está atendida.
        g_alarmOn = false;
        g_buzzer.setBackground(nullptr);
        snd::stop();
        led::blink(0);
        finderSendCmd(link::Cmd::FAST_OFF);
        trace("Alarma atendida desde el boton");
      } else {
        setFindMode(!g_findMode);
      }
    }
  }

  // Un resumen cada 30 s aunque no llegue nada: así se ve que sigue vivo.
  if ((now - g_tLastReport) >= 30000) {
    g_tLastReport = now;
    if (!g_everHeard || (now - g_tLastHeard) > 30000) {
      Serial.printf("[t=%8lu ms] (sin noticias del llavero desde hace %lu s)\n",
                    (unsigned long)now,
                    (unsigned long)(g_everHeard ? (now - g_tLastHeard) / 1000 : 0));
    }
  }
#endif

  led::update(now);
  snd::update(now);

  delay(1);   // cesión al planificador, igual que en el firmware de la DevKit
}
