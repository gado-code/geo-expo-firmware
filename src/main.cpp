/* ============================================================================
 *  GEO-EXPO ALERT  ·  Firmware etapa 1  (solo periféricos integrados)
 * ============================================================================
 *  Placa : ESP32 DevKit V1  (ESP32-WROOM-32, Xtensa LX6, BT clásico)
 *          La Heltec V3 es OPCIONAL y no tiene fecha: la prioridad es
 *          terminar el llavero sobre la DevKit V1. Los bloques
 *          BOARD_HELTEC_V3 se conservan pero NO se compilan ni se prueban.
 *
 *  Esta etapa NO usa hardware externo (no hay protoboard, buzzer ni pulsadores):
 *      · Botón : BOOT en GPIO0  (activo en BAJO, con pull-up interno)
 *      · LED   : LED integrado en GPIO2
 *
 *  El firmware:
 *   1. Expone un servidor BLE con perfil Nordic UART Service (NUS).
 *   2. Implementa la máquina de estados del botón de pánico + ventana de
 *      cancelación, 100 % no bloqueante (todo por millis()).
 *   3. Registra cada transición por el monitor serie, haya o no cliente BLE.
 *   4. Integra el módulo GPS (lib/nmea): parsea tramas NMEA y mantiene la
 *      última posición conocida. Todavía sin receptor físico, así que las
 *      tramas se pueden inyectar a mano con el comando "NMEA" (§5).
 *
 *  --------------------------------------------------------------------------
 *  CONTRATO BLE (DEFINITIVO — no modificar, ya comprometido con el equipo de
 *  la app):
 *      Nombre     : GEOEXPO-ALERT
 *      Servicio   : 6E400001-B5A3-F393-E0A9-E50E24DCCA9E
 *      TX  NOTIFY : 6E400003-B5A3-F393-E0A9-E50E24DCCA9E   (ESP32 -> app)
 *      RX  WRITE  : 6E400002-B5A3-F393-E0A9-E50E24DCCA9E   (app  -> ESP32)
 *
 *  Mensajes que EMITE el ESP32 por TX (ASCII, terminados en '\n'):
 *      "ALERT:1"  -> pulsación corta
 *      "ALERT:2"  -> pulsación prolongada (>= 3 s)
 *      "CANCEL"   -> cancelación dentro de la ventana
 *
 *  Mensajes que RECIBE por RX:
 *      "BEACON:ON"   -> activa la baliza (se simula: LED a 2 Hz)
 *      "BEACON:OFF"  -> la desactiva
 *
 *  Comandos EXTRA sólo de depuración (no forman parte del contrato):
 *      "POS"          -> vuelca la última posición GPS conocida
 *      "NMEA <trama>" -> inyecta una sentencia NMEA a mano
 * ==========================================================================*/

#include <Arduino.h>
#include <ctype.h>
#include <string.h>
#include <NimBLEDevice.h>

#include "nmea.h"           // lib/nmea:  parseo de tramas GPS   (probado en test/)
#include "panic.h"          // lib/panic: FSM del boton de panico (probada en test/)
#include "buzzer.h"         // lib/buzzer: patrones del zumbador   (probada en test/)

/* ==========================================================================
 *  1. CONFIGURACIÓN DE PINES
 * --------------------------------------------------------------------------
 *  TODOS los pines se pueden cambiar desde platformio.ini con -D, sin tocar
 *  este archivo. Los valores de aquí son los que usa el entorno de siempre
 *  (`devkit_v1`, sólo periféricos integrados); el entorno `devkit_llavero`
 *  los sobreescribe con los del llavero cableado de verdad.
 *
 *     Función        | integrado (devkit_v1) | llavero (devkit_llavero) | Heltec V3
 *     ---------------|-----------------------|--------------------------|----------
 *     LED            | GPIO2  (LED azul)     | GPIO2                    | GPIO35
 *     Botón          | GPIO0  (BOOT)         | GPIO4  (a masa)          | GPIO0
 *     Zumbador piezo | —                     | GPIO25                   | GPIO25
 *     Medida batería | —                     | GPIO34 (divisor 2:1)     | GPIO37
 *
 *  ⚠️ El botón definitivo NO debe ir en GPIO0, 2, 12 ni 15: son pines de
 *  arranque (*strapping*) y un botón pulsado en el momento del reset puede
 *  dejar la placa sin arrancar. Libres y seguros: GPIO4, 5, 18, 19, 21, 22,
 *  23, 25, 26, 27. GPIO34-39 son SÓLO entrada y no tienen pull-up interno,
 *  así que no sirven para el botón (sí para medir la batería).
 * ==========================================================================*/
#if defined(BOARD_HELTEC_V3)
  #ifndef PIN_LED_CFG
    #define PIN_LED_CFG 35              // Heltec V3: LED blanco integrado
  #endif
  #ifndef PIN_BUTTON_CFG
    #define PIN_BUTTON_CFG 0            // Heltec V3: botón PRG (GPIO0)
  #endif
  static const char* BOARD_LABEL = "Heltec WiFi LoRa 32 V3 (ESP32-S3)";
#else
  #ifndef PIN_LED_CFG
    #define PIN_LED_CFG 2               // DevKit V1: LED azul integrado
  #endif
  #ifndef PIN_BUTTON_CFG
    #define PIN_BUTTON_CFG 0            // DevKit V1: BOOT (sólo para probar sin cablear)
  #endif
  static const char* BOARD_LABEL = "ESP32 DevKit V1 (ESP32-WROOM-32)";
#endif

/* Zumbador piezo PASIVO. -1 = no hay zumbador (se sigue simulando con el LED).
 * Cableado: una pata al pin, la otra a GND. Si el piezo pide más corriente de
 * la que da un GPIO (40 mA máx.), va con un transistor NPN y su resistencia
 * de base; los piezo pequeños de 3 V tiran directos sin problema. */
#ifndef PIN_BUZZER_CFG
  #define PIN_BUZZER_CFG -1
#endif

/* Medida de batería por ADC. -1 = no se mide (se informa "desconocida").
 * Cableado: divisor de dos resistencias iguales (100 k) entre V+ de la
 * batería y GND, y el punto medio al pin. Así una LiPo de 4,2 V llega al
 * ADC como 2,1 V, dentro de lo que el ESP32 aguanta. */
#ifndef PIN_VBAT_CFG
  #define PIN_VBAT_CFG -1
#endif

/* El botón normal va a masa con pull-up interno (LOW = pulsado). Si se cablea
 * al revés (a 3V3 con pull-down externo), compilar con -D BUTTON_ACTIVE_HIGH=1. */
#ifndef BUTTON_ACTIVE_HIGH
  #define BUTTON_ACTIVE_HIGH 0
#endif

/* Algunas placas clónicas llevan el LED invertido. */
#ifndef LED_ACTIVE_HIGH_CFG
  #define LED_ACTIVE_HIGH_CFG 1
#endif

static const int  PIN_LED         = PIN_LED_CFG;
static const int  PIN_BUTTON      = PIN_BUTTON_CFG;
static const int  PIN_BUZZER      = PIN_BUZZER_CFG;
static const int  PIN_VBAT        = PIN_VBAT_CFG;
static const bool LED_ACTIVE_HIGH = (LED_ACTIVE_HIGH_CFG != 0);

/* Comprobaciones que saltan al COMPILAR, no el día de la expo. */
#if PIN_BUTTON_CFG == PIN_LED_CFG
  #error "El boton y el LED no pueden compartir pin (revisa PIN_BUTTON_CFG / PIN_LED_CFG)"
#endif
#if (PIN_BUZZER_CFG >= 0) && (PIN_BUZZER_CFG == PIN_BUTTON_CFG || PIN_BUZZER_CFG == PIN_LED_CFG)
  #error "El zumbador no puede compartir pin con el boton ni con el LED"
#endif
#if (PIN_BUZZER_CFG >= 0) && (PIN_BUZZER_CFG >= 34)
  #error "GPIO34-39 son SOLO entrada: no pueden mover un zumbador"
#endif
#if (PIN_BUTTON_CFG >= 34) && (BUTTON_ACTIVE_HIGH == 0)
  #error "GPIO34-39 no tienen pull-up interno: ese pin no vale para el boton a masa"
#endif

/* ==========================================================================
 *  2. PARÁMETROS DE LA MÁQUINA DE ESTADOS  (constantes, sin números mágicos)
 * ==========================================================================*/
static const uint32_t DEBOUNCE_MS      = 50;      // antirrebote (flanco de bajada Y de subida)
static const uint32_t LONG_PRESS_MS    = 3000;    // umbral de pulsación prolongada
static const uint32_t CANCEL_WINDOW_MS = 10000;   // duración de la ventana de cancelación

/* ==========================================================================
 *  3. CONTRATO BLE (Nordic UART Service)
 * ==========================================================================*/
static const char* DEVICE_NAME  = "GEOEXPO-ALERT";
#define NUS_SERVICE_UUID   "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define NUS_RX_CHAR_UUID   "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"   // app  -> ESP32 (WRITE)
#define NUS_TX_CHAR_UUID   "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"   // ESP32 -> app  (NOTIFY)

/* ==========================================================================
 *  4. CONTROLADOR DE LED  (100 % no bloqueante, todo por millis())
 * --------------------------------------------------------------------------
 *  Prioridad de visualización:
 *     baliza 2 Hz  >  patrón puntual (ALERT/CANCEL)  >  respiración  >  apagado
 *
 *  Se usa analogWrite(): funciona IGUAL en ESP32 y ESP32-S3 (a diferencia de
 *  ledcSetup/ledcAttachPin, que cambian de firma entre arduino-esp32 2.x
 *  y 3.x), así que este bloque no habría que tocarlo en un cambio de placa.
 * ==========================================================================*/
namespace led {

  static bool     s_beaconOn  = false;   // baliza activa (RX "BEACON:ON")
  static bool     s_breatheOn = false;   // ventana de cancelación activa

  // patrón puntual de parpadeos
  static bool     s_patActive  = false;
  static uint8_t  s_patTotal   = 0;
  static uint8_t  s_patDone    = 0;
  static bool     s_patPhaseOn = false;
  static uint16_t s_patOnMs    = 0;
  static uint16_t s_patOffMs   = 0;
  static uint32_t s_patT0      = 0;

  static inline void write(int value /* 0..255 */) {
    analogWrite(PIN_LED, LED_ACTIVE_HIGH ? value : (255 - value));
  }

  void begin() {
    pinMode(PIN_LED, OUTPUT);
    write(0);
  }

  // Encola N parpadeos. Al terminar el LED vuelve solo a respiración/apagado.
  void blink(uint8_t count, uint16_t onMs, uint16_t offMs) {
    s_patActive  = true;
    s_patTotal   = count;
    s_patDone    = 0;
    s_patPhaseOn = true;
    s_patOnMs    = onMs;
    s_patOffMs   = offMs;
    s_patT0      = millis();
    write(255);
  }

  void setBeacon(bool on)    { s_beaconOn  = on; }
  void setBreathing(bool on) { s_breatheOn = on; }

  void update(uint32_t now) {
    // (1) baliza: máxima prioridad
    if (s_beaconOn) {
      write((now % 500) < 250 ? 255 : 0);        // 2 Hz = 250 ms ON / 250 ms OFF
      return;
    }
    // (2) patrón puntual de parpadeos
    if (s_patActive) {
      uint32_t el = now - s_patT0;
      if (s_patPhaseOn && el >= s_patOnMs) {
        s_patPhaseOn = false;
        s_patT0 = now;
        write(0);
        if (++s_patDone >= s_patTotal) s_patActive = false;   // fin del patrón
      } else if (!s_patPhaseOn && el >= s_patOffMs) {
        s_patPhaseOn = true;
        s_patT0 = now;
        write(255);
      }
      return;
    }
    // (3) respiración lenta durante la ventana de cancelación
    if (s_breatheOn) {
      float ph = (now % 3000) / 3000.0f * TWO_PI;
      int   b  = (int)((sinf(ph - HALF_PI) * 0.5f + 0.5f) * 200.0f) + 12;  // 12..212
      write(b);
      return;
    }
    // (4) reposo
    write(0);
  }

} // namespace led

/* ==========================================================================
 *  4-bis. ZUMBADOR PIEZO  (lib/buzzer + LEDC)
 * --------------------------------------------------------------------------
 *  lib/buzzer decide QUÉ frecuencia toca en cada instante (y eso se prueba en
 *  el PC); aquí sólo se saca por el pin. Se usa LEDC directamente en vez de
 *  tone() porque tone() en arduino-esp32 se lleva un canal LEDC fijo y un
 *  temporizador, y ya hay otro canal ocupado por el analogWrite() del LED.
 *
 *  Si PIN_BUZZER es -1 (no hay zumbador cableado) todo esto se queda en nada:
 *  el secuenciador sigue corriendo —para que las trazas sean las mismas— pero
 *  no se toca ningún pin.
 * ==========================================================================*/
/* trace() se define más abajo (§6); aquí basta con declararla. */
static void trace(const char* msg);

namespace snd {

  static buzzer::Player s_player;
  static bool           s_ready = false;

  /* Canal y temporizador LEDC propios, elegidos para no chocar con los que
   * analogWrite() reparte solo (el core 2.x los asigna desde el más alto
   * hacia abajo, así que el 0 es el que más tarda en tocarle). */
  static const uint8_t  LEDC_CHANNEL = 0;
  static const uint8_t  LEDC_BITS    = 10;      // resolución del duty
  static const uint32_t LEDC_DUTY    = 512;     // 50 % = onda cuadrada

  void begin() {
    if (PIN_BUZZER < 0) { trace("BUZZER: no hay zumbador cableado (se simula con el LED)"); return; }
#if ESP_ARDUINO_VERSION_MAJOR >= 3
    ledcAttach(PIN_BUZZER, 2000, LEDC_BITS);          // API de arduino-esp32 3.x
#else
    ledcSetup(LEDC_CHANNEL, 2000, LEDC_BITS);         // API de arduino-esp32 2.x
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

  void play(const buzzer::Pattern& p, bool force = false) { s_player.play(p, millis(), force); }
  void stopIf(const buzzer::Pattern& p)                   { s_player.stopIf(p); }
  void stop()                                             { s_player.stop(); }
  void mute(bool on)                                      { s_player.mute(on); }
  bool muted()                                            { return s_player.muted(); }

  void update(uint32_t now) {
    const buzzer::Out o = s_player.update(now);
    if (o.changed) output(o.freqHz);
  }

} // namespace snd

/* ==========================================================================
 *  4-ter. BATERÍA  (opcional: sólo si PIN_VBAT está cableado)
 * --------------------------------------------------------------------------
 *  Un botón de pánico que se queda sin batería sin avisar no sirve de nada,
 *  así que el firmware sabe leerla desde ya. Sin divisor cableado devuelve
 *  "desconocida" y no molesta a nadie.
 * ==========================================================================*/
namespace battery {

  /* Divisor 2:1 -> el ADC ve la mitad de la tensión real. */
  static const float DIVIDER      = 2.0f;
  static const float FULL_V       = 4.15f;   // LiPo cargada
  static const float EMPTY_V      = 3.30f;   // por debajo, apagar
  static const uint8_t LOW_PCT    = 20;      // umbral de aviso

  static float s_volts = 0.0f;
  static bool  s_has   = false;

  void begin() {
    if (PIN_VBAT < 0) return;
    analogReadResolution(12);
    // 11 dB = fondo de escala ~3,3 V, que es lo que necesita el divisor.
    analogSetPinAttenuation(PIN_VBAT, ADC_11db);
  }

  void poll(uint32_t now) {
    if (PIN_VBAT < 0) return;
    static uint32_t tLast = 0;
    if (s_has && (now - tLast) < 10000) return;    // una lectura cada 10 s sobra
    tLast = now;

    uint32_t acc = 0;
    for (int i = 0; i < 8; ++i) acc += analogReadMilliVolts(PIN_VBAT);
    s_volts = (acc / 8.0f) * DIVIDER / 1000.0f;
    s_has   = true;
  }

  bool has()    { return s_has; }
  float volts() { return s_volts; }

  /* Porcentaje aproximado y lineal. Una LiPo no se descarga en línea recta,
   * pero para "le queda media pila" es más que suficiente y es honesto
   * llamarlo aproximado en vez de fingir una curva que no se ha medido. */
  uint8_t percent() {
    if (!s_has) return 255;
    float p = (s_volts - EMPTY_V) / (FULL_V - EMPTY_V) * 100.0f;
    if (p < 0.0f)   p = 0.0f;
    if (p > 100.0f) p = 100.0f;
    return (uint8_t)(p + 0.5f);
  }

  bool low() { return s_has && percent() <= LOW_PCT; }

} // namespace battery

/* ==========================================================================
 *  5. MÓDULO GPS  (lib/nmea)
 * --------------------------------------------------------------------------
 *  ESTADO: el receptor GPS todavía NO está conectado a la placa. Por eso el
 *  lector de UART2 está desactivado por defecto (GPS_UART_ENABLED = 0) y la
 *  posición se puede alimentar A MANO inyectando tramas NMEA por el monitor
 *  serie o por BLE:
 *
 *      NMEA $GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47
 *      POS
 *
 *  Así el camino completo (trama -> parser -> posición -> mensaje) queda
 *  probado hoy, y cuando llegue el módulo sólo hay que poner
 *  GPS_UART_ENABLED a 1 y cablear TX del GPS a GPS_RX_PIN.
 *
 *  Cableado previsto (el GPS sólo necesita que le ESCUCHEMOS):
 *      GPS VCC -> 3V3      GPS GND -> GND      GPS TX -> GPIO16 (RX2)
 *  ATENCIÓN: muchos módulos NEO-6M son de 5 V en VCC pero su TX saca 3,3 V,
 *  que es lo que espera el ESP32. No conectes TX del ESP32 al RX del GPS sin
 *  comprobar niveles: aquí no hace falta.
 * ==========================================================================*/
#ifndef GPS_UART_ENABLED
  #define GPS_UART_ENABLED 0        // 1 = leer de verdad del GPS por UART2
#endif
#ifndef GPS_RX_PIN
  #define GPS_RX_PIN 16             // GPIO16 = RX2 en la DevKit V1
#endif
#ifndef GPS_BAUD
  #define GPS_BAUD 9600             // valor de fábrica de NEO-6M / NEO-7M
#endif

namespace gps {

  static nmea::Parser s_parser;
  static uint32_t     s_tLastFix  = 0;      // millis() del último fix válido
  static bool         s_everFixed = false;

  void begin() {
#if GPS_UART_ENABLED
    // Sólo RX: al GPS no le mandamos nada, así que TX queda sin asignar (-1).
    Serial2.begin(GPS_BAUD, SERIAL_8N1, GPS_RX_PIN, -1);
    trace("GPS: UART2 abierto, escuchando tramas NMEA");
#else
    trace("GPS: sin modulo fisico (usa el comando NMEA para inyectar tramas)");
#endif
  }

  // Anota que acabamos de recibir una posición utilizable.
  static void noteFix(uint32_t now) { s_tLastFix = now; s_everFixed = true; }

  void poll(uint32_t now) {
#if GPS_UART_ENABLED
    // Se limita el número de bytes por vuelta para no bloquear el loop() si
    // el GPS escupe una ráfaga entera de sentencias.
    for (int i = 0; i < 96 && Serial2.available(); ++i) {
      if (s_parser.feed((char)Serial2.read()) && s_parser.fix().valid) noteFix(now);
    }
#else
    (void)now;
#endif
  }

  // Inyección manual de una trama (comando "NMEA ..."), para probar sin GPS.
  bool inject(const char* line, uint32_t now) {
    const bool ok = s_parser.feedLine(line);
    if (ok && s_parser.fix().valid) noteFix(now);
    return ok;
  }

  const nmea::Fix& fix()  { return s_parser.fix(); }
  bool             has()  { return s_parser.fix().valid; }
  uint32_t         ageMs(uint32_t now) { return s_everFixed ? (now - s_tLastFix) : 0; }

  /* Escribe "LAT;LON" en `out` con 6 decimales (~11 cm, de sobra).
   * Devuelve false si no hay posición: el llamante decide qué hacer. */
  bool format(char* out, size_t cap) {
    if (!has()) return false;
    snprintf(out, cap, "%.6f;%.6f", s_parser.fix().lat, s_parser.fix().lon);
    return true;
  }

  void printStatus(uint32_t now) {
    const nmea::Fix& f = s_parser.fix();
    Serial.printf("[t=%8lu ms] GPS  tramas OK=%lu  descartadas=%lu\n",
                  (unsigned long)now,
                  (unsigned long)s_parser.okCount(),
                  (unsigned long)s_parser.badCount());
    if (!f.valid) {
      Serial.println("             sin posicion valida (aun no hay fix)");
      return;
    }
    // sats/hdop/alt sólo vienen en GGA: tras una RMC son los de la última GGA.
    Serial.printf("             lat=%.6f  lon=%.6f  sats=%u  hdop=%.1f  alt=%.1f m (ultima GGA)\n",
                  f.lat, f.lon, (unsigned)f.sats, f.hdop, f.altM);
    Serial.printf("             UTC %02u:%02u:%02u  antiguedad del fix: %lu ms\n",
                  (unsigned)f.hh, (unsigned)f.mm, (unsigned)f.ss,
                  (unsigned long)ageMs(now));
  }

} // namespace gps

/* ==========================================================================
 *  6. ESTADO GLOBAL Y BLE
 * ==========================================================================*/
/* Tamaño de los buffers de comandos. Una sentencia NMEA puede ocupar 82
 * caracteres y el prefijo "NMEA " suma 5 más, así que 128 va sobrado. */
static const size_t CMD_BUF_SIZE = 128;

static NimBLEServer*         g_server  = nullptr;
static NimBLECharacteristic* g_txChar  = nullptr;
static bool                  g_beaconActive = false;   // estado lógico de la baliza
static bool                  g_bleUp        = false;   // BLE arrancó sin colgarse

/* --------------------------------------------------------------------------
 *  ARRANQUE A PRUEBA DE FALLOS  (incidencia I12)
 * --------------------------------------------------------------------------
 *  La I12 es un cuelgue DENTRO de NimBLEDevice::init(), esperando un `sync`
 *  del controlador de Bluetooth que a veces no llega; el perro guardián
 *  reinicia la placa y el ciclo se repite: la placa queda en bucle y el botón
 *  de pánico NO funciona. Eso es lo grave: no que falle el BLE, sino que se
 *  lleve por delante el resto del aparato.
 *
 *  Arreglo: un contador en la memoria RTC (que sobrevive al reinicio del
 *  perro guardián, pero no a quitar la corriente). Se incrementa ANTES de
 *  entrar en init() y se pone a cero en cuanto init() vuelve. Si al arrancar
 *  ya hay tres intentos fallidos seguidos, el firmware arranca SIN BLE: el
 *  botón, el LED, el zumbador, el GPS y el monitor serie funcionan igual, y
 *  el log lo dice bien claro. Para volver a intentarlo: comando "BLE:RETRY"
 *  o quitar y poner la alimentación.
 * ------------------------------------------------------------------------*/
RTC_NOINIT_ATTR static uint32_t s_bootMagic;
RTC_NOINIT_ATTR static uint8_t  s_bleAttempts;
static const uint32_t BOOT_MAGIC       = 0x6EA71C03;   // "panic" con imaginación
static const uint8_t  BLE_MAX_ATTEMPTS = 3;

/* ¿Hay alguien escuchando?
 *
 * Antes esto era un simple `bool g_bleConnected` que ponía a false CUALQUIER
 * desconexión. Con NimBLE admitiendo hasta 3 clientes a la vez, bastaba que
 * un segundo móvil se desconectara para que el firmware creyera que ya no
 * había nadie y DEJARA DE MANDAR LAS ALERTAS por BLE al móvil que seguía
 * conectado. En un botón de pánico eso es el fallo más grave posible, así que
 * ahora se le pregunta al propio servidor cuántos clientes hay. */
static bool bleHasClient() {
  return g_server != nullptr && g_server->getConnectedCount() > 0;
}

/* Cola RX: los WRITE del cliente se copian aquí y se procesan en loop();
 * NUNCA se hace trabajo pesado dentro del callback de NimBLE.
 *
 * Es un anillo de 4 huecos, no un único buffer. Con un solo hueco, dos WRITE
 * seguidos (la app mandando "BEACON:OFF" justo después de "BEACON:ON", o el
 * usuario pegando comandos) hacían que el segundo pisara al primero y el
 * primero se PERDÍA sin dejar rastro. */
static const uint8_t    RX_QUEUE_LEN = 4;
static char             g_rxQueue[RX_QUEUE_LEN][CMD_BUF_SIZE];
static volatile uint8_t g_rxHead = 0;      // donde escribe el callback BLE
static volatile uint8_t g_rxTail = 0;      // donde lee loop()
static volatile uint16_t g_rxDropped = 0;  // comandos perdidos por cola llena
static portMUX_TYPE     g_rxMux = portMUX_INITIALIZER_UNLOCKED;

/* ---- Trazas -------------------------------------------------------------- */
static void trace(const char* msg) {
  Serial.printf("[t=%8lu ms] %s\n", (unsigned long)millis(), msg);
}

/* ==========================================================================
 *  ¿Se adjunta la posición a las alertas?
 * --------------------------------------------------------------------------
 *  El contrato BLE de la §3 está CONGELADO con el equipo de la app: hoy sólo
 *  espera "ALERT:1", "ALERT:2" y "CANCEL". Enviar "ALERT:1;4.205760;-74.094648"
 *  por las buenas rompería su parser, así que la extensión queda DESACTIVADA
 *  por defecto y el firmware sigue cumpliendo el contrato al pie de la letra.
 *
 *  Propuesta a acordar con ellos antes de activarla:
 *      ALERT:1;<lat>;<lon>     lat/lon en grados decimales con 6 decimales
 *      ALERT:1                 exactamente igual que hoy cuando no hay fix
 *  Es compatible hacia atrás si su parser corta por el primer ';'.
 *
 *  Para probarla:  pio run -e devkit_v1 -t upload
 *                  (tras poner ALERT_WITH_POSITION a 1, o -D ALERT_WITH_POSITION=1)
 * ==========================================================================*/
#ifndef ALERT_WITH_POSITION
  #define ALERT_WITH_POSITION 0
#endif

/* ---- Emisión por TX: BLE NOTIFY si hay cliente, y SIEMPRE por serie ------ */
static void emitTx(const char* token) {
  // El mensaje que sale por el aire. Por defecto es el token tal cual.
  char msg[64];
  snprintf(msg, sizeof(msg), "%s", token);

#if ALERT_WITH_POSITION
  // Sólo las alertas llevan posición; "CANCEL" no la necesita.
  if (strncmp(token, "ALERT", 5) == 0) {
    char pos[32];
    if (gps::format(pos, sizeof(pos))) {
      snprintf(msg, sizeof(msg), "%s;%s", token, pos);
    }
  }
#endif

  const bool hayCliente = bleHasClient();
  Serial.printf("[t=%8lu ms] >>> TX  %-24s  %s\n",
                (unsigned long)millis(), msg,
                hayCliente ? "(enviado por BLE)"
                           : "(sin cliente BLE: registrado solo en serie)");
  if (g_txChar && hayCliente) {
    char buf[72];
    int n = snprintf(buf, sizeof(buf), "%s\n", msg);     // contrato: termina en '\n'
    g_txChar->setValue(reinterpret_cast<uint8_t*>(buf), n);
    g_txChar->notify();
  }
}

/* ---- Callbacks del servidor BLE ---------------------------------------- */
class ServerCB : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer* server) override {
    Serial.printf("[t=%8lu ms] BLE: cliente CONECTADO (%u en total)\n",
                  (unsigned long)millis(), (unsigned)server->getConnectedCount());
  }
  void onDisconnect(NimBLEServer* server) override {
    // El advertising se reanuda SIEMPRE: NimBLE lo para al aceptar una
    // conexión, y si no se relanza el llavero queda invisible para el resto.
    Serial.printf("[t=%8lu ms] BLE: cliente DESCONECTADO (quedan %u) -> se reanuda el advertising\n",
                  (unsigned long)millis(), (unsigned)server->getConnectedCount());
    NimBLEDevice::startAdvertising();
  }
};

/* ---- Callback de la característica RX (app -> ESP32) -------------------- */
class RxCB : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* c) override {
    NimBLEAttValue v   = c->getValue();
    const char*    s   = v.c_str();
    size_t         len = v.length();
    const size_t   n   = (len < CMD_BUF_SIZE - 1) ? len : CMD_BUF_SIZE - 1;

    portENTER_CRITICAL(&g_rxMux);
    const uint8_t next = (uint8_t)((g_rxHead + 1) % RX_QUEUE_LEN);
    if (next == g_rxTail) {
      ++g_rxDropped;                  // cola llena: se avisa desde loop()
    } else {
      memcpy(g_rxQueue[g_rxHead], s, n);
      g_rxQueue[g_rxHead][n] = '\0';
      g_rxHead = next;
    }
    portEXIT_CRITICAL(&g_rxMux);
  }
};

/* ==========================================================================
 *  7. INICIALIZACIÓN BLE
 * ==========================================================================*/
static void bleBegin() {
  // Marcadores a ambos lados de init(): si la placa vuelve a entrar en bucle
  // de reinicio (incidencia I12) el log dice sin ambigüedad si se colgó
  // DENTRO de NimBLEDevice::init() o después. Ver README.
  trace("BLE: entrando en NimBLEDevice::init() ...");
  NimBLEDevice::init(DEVICE_NAME);
  trace("BLE: NimBLEDevice::init() completado (host y controlador sincronizados)");

  NimBLEDevice::setPower(ESP_PWR_LVL_P9);          // +9 dBm: alcance de demostración

  g_server = NimBLEDevice::createServer();
  g_server->setCallbacks(new ServerCB());

  NimBLEService* svc = g_server->createService(NUS_SERVICE_UUID);

  g_txChar = svc->createCharacteristic(NUS_TX_CHAR_UUID, NIMBLE_PROPERTY::NOTIFY);

  NimBLECharacteristic* rxChar = svc->createCharacteristic(
      NUS_RX_CHAR_UUID,
      NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR);
  rxChar->setCallbacks(new RxCB());

  svc->start();

  NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
  adv->addServiceUUID(NUS_SERVICE_UUID);
  adv->setScanResponse(true);                      // el nombre viaja en el scan response
  NimBLEDevice::startAdvertising();
}

/* ==========================================================================
 *  8. PROCESAMIENTO DE COMANDOS RX  (BLE o serie)
 * ==========================================================================*/
static void printHelp();
static void printStatus();

static void handleCommand(const char* rawCmd) {
  // Normaliza: quita CR/LF, pasa a mayúsculas, recorta espacios.
  char cmd[CMD_BUF_SIZE];
  size_t j = 0;
  for (size_t i = 0; rawCmd[i] && j < sizeof(cmd) - 1; ++i) {
    char ch = rawCmd[i];
    if (ch == '\r' || ch == '\n') continue;
    if (j == 0 && ch == ' ')      continue;             // espacios iniciales
    cmd[j++] = (char)toupper((unsigned char)ch);
  }
  while (j > 0 && cmd[j - 1] == ' ') j--;               // espacios finales
  cmd[j] = '\0';

  if (strcmp(cmd, "BEACON:ON") == 0) {
    g_beaconActive = true;
    led::setBeacon(true);
    snd::play(buzzer::PATTERN_BEACON);
    trace(PIN_BUZZER >= 0
              ? "RX  BEACON:ON   -> baliza ACTIVADA  (LED a 2 Hz + zumbador)"
              : "RX  BEACON:ON   -> baliza ACTIVADA  (LED parpadeando a 2 Hz)");
  } else if (strcmp(cmd, "BEACON:OFF") == 0) {
    g_beaconActive = false;
    led::setBeacon(false);
    snd::stopIf(buzzer::PATTERN_BEACON);
    trace("RX  BEACON:OFF  -> baliza DESACTIVADA");
  } else if (strncmp(cmd, "NMEA ", 5) == 0) {
    // Inyecta una trama a mano: permite probar el GPS sin tener el módulo.
    const uint32_t now = millis();
    if (gps::inject(cmd + 5, now)) {
      trace("RX  NMEA        -> trama ACEPTADA (checksum correcto)");
    } else {
      trace("RX  NMEA        -> trama DESCARTADA (checksum o formato malos)");
    }
    gps::printStatus(now);
  } else if (strcmp(cmd, "POS") == 0 || strcmp(cmd, "GPS") == 0) {
    gps::printStatus(millis());
  } else if (strcmp(cmd, "MUTE:ON") == 0) {
    snd::mute(true);
    trace("RX  MUTE:ON     -> zumbador SILENCIADO (el resto sigue igual)");
  } else if (strcmp(cmd, "MUTE:OFF") == 0) {
    snd::mute(false);
    trace("RX  MUTE:OFF    -> zumbador ACTIVO");
  } else if (strcmp(cmd, "BEEP") == 0) {
    // Prueba de sonido: útil para comprobar el cableado del piezo sin
    // tener que disparar una alerta de verdad delante de la gente.
    snd::play(buzzer::PATTERN_BOOT, /*force=*/true);
    trace("RX  BEEP        -> prueba de zumbador");
  } else if (strcmp(cmd, "BAT") == 0 || strcmp(cmd, "STATUS") == 0) {
    printStatus();
  } else if (strcmp(cmd, "BLE:RETRY") == 0) {
    // Borra el contador de la I12 y reinicia para volver a intentar el BLE.
    s_bleAttempts = 0;
    trace("RX  BLE:RETRY   -> contador de arranques fallidos a cero, reiniciando...");
    Serial.flush();
    ESP.restart();
  } else if (strcmp(cmd, "?") == 0 || strcmp(cmd, "HELP") == 0) {
    printHelp();
  } else {
    Serial.printf("[t=%8lu ms] RX  comando no reconocido: \"%s\"\n",
                  (unsigned long)millis(), cmd);
  }
}

/* ---- Vaciar la cola RX de BLE (se llama desde loop()) -----------------
 * Se procesan TODOS los comandos que haya pendientes, no sólo el último. */
static void blePollRx() {
  for (;;) {
    char     local[CMD_BUF_SIZE];
    uint16_t perdidos = 0;
    bool     hay      = false;

    portENTER_CRITICAL(&g_rxMux);
    if (g_rxTail != g_rxHead) {
      memcpy(local, g_rxQueue[g_rxTail], sizeof(local));
      g_rxTail = (uint8_t)((g_rxTail + 1) % RX_QUEUE_LEN);
      hay = true;
    }
    perdidos    = g_rxDropped;
    g_rxDropped = 0;
    portEXIT_CRITICAL(&g_rxMux);

    if (perdidos) {
      Serial.printf("[t=%8lu ms] <<< RX (BLE) AVISO: %u comando(s) perdidos, cola llena\n",
                    (unsigned long)millis(), (unsigned)perdidos);
    }
    if (!hay) return;

    local[sizeof(local) - 1] = '\0';
    Serial.printf("[t=%8lu ms] <<< RX (BLE) \"%s\"\n", (unsigned long)millis(), local);
    handleCommand(local);
  }
}

/* ---- Comandos por el monitor serie (para probar SIN celular) ---------- */
static void serialPoll() {
  static char   line[CMD_BUF_SIZE];
  static size_t len = 0;
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\n' || c == '\r') {
      if (len > 0) {
        line[len] = '\0';
        Serial.printf("[t=%8lu ms] <<< RX (serie) \"%s\"\n",
                      (unsigned long)millis(), line);
        handleCommand(line);
        len = 0;
      }
    } else if (len < sizeof(line) - 1) {
      line[len++] = c;
    }
  }
}

/* ==========================================================================
 *  9. MÁQUINA DE ESTADOS DEL BOTÓN
 * --------------------------------------------------------------------------
 *  La lógica vive en lib/panic y NO depende de Arduino, así que se prueba
 *  entera en el PC con `pio test -e native` (34 casos, sin placa). Aquí sólo
 *  quedan las tres cosas que sí necesitan hardware:
 *
 *      leer el pin  ->  pasarle el nivel a la FSM  ->  NOTIFY + LED + traza
 *
 *  El comportamiento y los textos del log son los mismos de siempre; lo que
 *  cambia es que ahora los bordes de temporización están cubiertos por
 *  pruebas en vez de por pulsaciones a ojo.
 * ==========================================================================*/
/* Lectura del botón con la polaridad que toque. Por defecto va a masa con
 * pull-up interno (LOW = pulsado); con -D BUTTON_ACTIVE_HIGH=1 se invierte. */
static inline bool buttonPressed() {
#if BUTTON_ACTIVE_HIGH
  return digitalRead(PIN_BUTTON) == HIGH;
#else
  return digitalRead(PIN_BUTTON) == LOW;
#endif
}

static void buttonBegin() {
#if BUTTON_ACTIVE_HIGH
  pinMode(PIN_BUTTON, INPUT_PULLDOWN);
#else
  pinMode(PIN_BUTTON, INPUT_PULLUP);
#endif
}

static panic::Fsm g_fsm(panic::Config{DEBOUNCE_MS, LONG_PRESS_MS, CANCEL_WINDOW_MS});

static void fsmUpdate(uint32_t now) {
  const bool pressed = buttonPressed();
  const panic::Step s = g_fsm.update(now, pressed);

  // (a) Lo que viaja por el contrato BLE: ALERT:1 / ALERT:2 / CANCEL.
  const char* token = panic::Fsm::txToken(s.event);
  if (token) emitTx(token);

  // (b) Realimentación visual y trazas propias de cada evento.
  switch (s.event) {
    case panic::Event::ALERT_SHORT:
      led::blink(1, 800, 0);              // un parpadeo largo
      led::setBreathing(true);
      snd::play(buzzer::PATTERN_ALERT_SHORT);
      break;
    case panic::Event::ALERT_LONG:
      led::blink(2, 250, 200);            // dos parpadeos
      led::setBreathing(true);
      snd::play(buzzer::PATTERN_ALERT_LONG);
      break;
    case panic::Event::CANCEL:
      led::blink(3, 90, 90);              // tres parpadeos rápidos
      led::setBreathing(false);
      snd::play(buzzer::PATTERN_CANCEL);
      break;
    case panic::Event::CANCEL_ARMED:
      trace("CANCEL_WINDOW: botón liberado -> cancelación ARMADA");
      break;
    case panic::Event::WINDOW_EXPIRED:
      led::setBreathing(false);
      // Dos pitidos secos: la alerta ya no se puede cancelar, va en serio.
      snd::play(buzzer::PATTERN_CONFIRMED);
      break;
    default:
      break;                              // NONE y BOUNCE no pintan nada
  }

  // (c) Traza de la transición, en el mismo formato de siempre.
  if (s.changed) {
    Serial.printf("[t=%8lu ms] FSM  %-13s -> %-13s  (%s)\n",
                  (unsigned long)now,
                  panic::Fsm::stateName(s.from),
                  panic::Fsm::stateName(s.to),
                  s.reason);
  }
}

/* ==========================================================================
 *  10. BANNER / AYUDA
 * ==========================================================================*/
static void printHelp() {
  Serial.println();
  Serial.println("  Comandos por el monitor serie (equivalen a un WRITE en RX):");
  Serial.println("     BEACON:ON    activa la baliza (LED a 2 Hz + zumbador)");
  Serial.println("     BEACON:OFF   desactiva la baliza");
  Serial.println("     MUTE:ON      silencia el zumbador (el LED y el BLE siguen)");
  Serial.println("     MUTE:OFF     vuelve a dejar sonar el zumbador");
  Serial.println("     BEEP         prueba de zumbador");
  Serial.println("     POS          muestra la ultima posicion GPS conocida");
  Serial.println("     BAT/STATUS   estado completo: bateria, BLE, baliza, GPS");
  Serial.println("     NMEA <trama> inyecta una sentencia NMEA a mano, por ejemplo:");
  Serial.println("                  NMEA $GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47");
  Serial.println("     ?            muestra esta ayuda");
  Serial.printf ("  El boton de panico es fisico: GPIO%d en la placa.\n", PIN_BUTTON);
  Serial.println();
}

/* Una foto del aparato en una sola orden. Es lo primero que hay que pedirle
 * cuando algo no va, y lo que conviene enseñar en la expo si preguntan. */
static void printStatus() {
  const uint32_t now = millis();
  Serial.println();
  Serial.println("  --- ESTADO -------------------------------------------");
  Serial.printf ("  Encendida desde hace : %lu s\n", (unsigned long)(now / 1000));
  Serial.printf ("  Estado del boton     : %s%s\n",
                 panic::Fsm::stateName(g_fsm.state()),
                 buttonPressed() ? "  (pulsado ahora mismo)" : "");
  Serial.printf ("  BLE                  : %s\n",
                 !g_bleUp        ? "DESACTIVADO (arranque a prueba de fallos, ver I12)"
                 : bleHasClient() ? "con cliente conectado" : "anunciandose, sin cliente");
  Serial.printf ("  Baliza               : %s\n", g_beaconActive ? "ENCENDIDA" : "apagada");
  Serial.printf ("  Zumbador             : %s\n",
                 PIN_BUZZER < 0 ? "no cableado" : (snd::muted() ? "SILENCIADO" : "activo"));
  if (PIN_VBAT < 0) {
    Serial.println("  Bateria              : no medida (sin divisor cableado)");
  } else if (!battery::has()) {
    Serial.println("  Bateria              : aun sin lectura");
  } else {
    Serial.printf ("  Bateria              : %.2f V  (~%u %%)%s\n",
                   battery::volts(), (unsigned)battery::percent(),
                   battery::low() ? "  << BAJA" : "");
  }
  gps::printStatus(now);
  Serial.println("  ------------------------------------------------------");
  Serial.println();
}

static void printBanner() {
  Serial.println();
  Serial.println("============================================================");
  Serial.println("  GEO-EXPO ALERT  -  Firmware etapa 1");
  Serial.printf ("  Placa            : %s\n", BOARD_LABEL);
  Serial.printf ("  LED  GPIO%-3d  Boton GPIO%-3d (%s)\n", PIN_LED, PIN_BUTTON,
                 BUTTON_ACTIVE_HIGH ? "activo en ALTO" : "a masa, pull-up");
  if (PIN_BUZZER >= 0) Serial.printf ("  Zumbador piezo   : GPIO%d\n", PIN_BUZZER);
  else                 Serial.println("  Zumbador piezo   : no cableado (la baliza se simula con el LED)");
  if (PIN_VBAT >= 0)   Serial.printf ("  Medida de bateria: GPIO%d (divisor 2:1)\n", PIN_VBAT);
  else                 Serial.println("  Medida de bateria: no cableada");
  Serial.println("------------------------------------------------------------");
  Serial.println("  BLE  Nordic UART Service");
  Serial.printf ("    Nombre   : %s\n", DEVICE_NAME);
  Serial.println("    Servicio : 6E400001-B5A3-F393-E0A9-E50E24DCCA9E");
  Serial.println("    TX NOTIFY: 6E400003-... (ESP32 -> app)");
  Serial.println("    RX WRITE : 6E400002-... (app  -> ESP32)");
  Serial.println("------------------------------------------------------------");
  Serial.printf ("  GPS              : %s   posicion en las alertas: %s\n",
                 GPS_UART_ENABLED ? "UART2 activo" : "sin modulo (inyeccion manual)",
                 ALERT_WITH_POSITION ? "SI" : "NO (contrato congelado)");
  Serial.println("------------------------------------------------------------");
  Serial.printf ("  DEBOUNCE_MS=%lu  LONG_PRESS_MS=%lu  CANCEL_WINDOW_MS=%lu\n",
                 (unsigned long)DEBOUNCE_MS, (unsigned long)LONG_PRESS_MS,
                 (unsigned long)CANCEL_WINDOW_MS);
  Serial.println("============================================================");
  printHelp();
}

/* ==========================================================================
 *  11. setup() / loop()
 * ==========================================================================*/
void setup() {
  Serial.begin(115200);
  uint32_t t0 = millis();
  while (!Serial && (millis() - t0) < 2000) { /* espera opcional al USB-CDC (S3) */ }

  buttonBegin();
  // Arrancar con el botón pulsado (o con el DTR del monitor tirando de GPIO0) no
  // cuenta como flanco: la FSM exige soltarlo antes de armar una pulsación.
  g_fsm.begin(buttonPressed(), millis());
  led::begin();
  snd::begin();
  battery::begin();

  printBanner();
  gps::begin();

  /* BLE con red de seguridad (ver §6, incidencia I12): si los tres últimos
   * arranques se colgaron dentro de init(), este arranca sin BLE para que el
   * botón de pánico siga sirviendo de algo. */
  if (s_bootMagic != BOOT_MAGIC) {      // arranque en frío: la RTC trae basura
    s_bootMagic   = BOOT_MAGIC;
    s_bleAttempts = 0;
  }
  if (s_bleAttempts >= BLE_MAX_ATTEMPTS) {
    Serial.printf("[t=%8lu ms] BLE: DESACTIVADO tras %u arranques colgados en init()"
                  " (incidencia I12).\n",
                  (unsigned long)millis(), (unsigned)s_bleAttempts);
    trace("BLE: el resto del aparato funciona igual. Escribe BLE:RETRY para reintentar.");
    snd::play(buzzer::PATTERN_LINK_LOST);
  } else {
    ++s_bleAttempts;                    // se anota ANTES de entrar en init()
    bleBegin();
    s_bleAttempts = 0;                  // si se llega aquí, init() no se colgó
    g_bleUp       = true;
    trace("BLE: advertising iniciado como \"GEOEXPO-ALERT\"");
  }

  snd::play(buzzer::PATTERN_BOOT);
  trace("Sistema listo. Estado inicial: IDLE");
}

void loop() {
  uint32_t now = millis();

  blePollRx();          // comandos recibidos por BLE
  serialPoll();         // comandos recibidos por el monitor serie
  gps::poll(now);       // tramas NMEA del receptor GPS (si está conectado)
  fsmUpdate(now);       // máquina de estados del botón
  led::update(now);     // realimentación visual
  snd::update(now);     // realimentación sonora
  battery::poll(now);   // lectura periódica de la batería (si está cableada)

  // Cesión cooperativa al planificador de FreeRTOS para no matar de hambre a
  // la tarea IDLE (evita el "Task watchdog"). NO se usa para temporizar:
  // toda la lógica temporal va por millis().
  delay(1);
}
