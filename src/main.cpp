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

/* ==========================================================================
 *  1. CONFIGURACIÓN DE PINES
 * --------------------------------------------------------------------------
 *  Se selecciona con -D BOARD_HELTEC_V3 desde platformio.ini.
 *  La tabla deja anotado el pin equivalente en la Heltec V3 por si algún día
 *  se hace el salto; hoy NO es el camino del proyecto (ver cabecera):
 *
 *     Función         | DevKit V1 (WROOM-32) | Heltec WiFi LoRa 32 V3 (S3)
 *     ----------------|----------------------|---------------------------
 *     LED integrado   | GPIO2                | GPIO35  (LED blanco)
 *     Botón integrado | GPIO0  (BOOT)        | GPIO0   (botón PRG / USER)
 *     LED activo en   | ALTO                 | ALTO
 * ==========================================================================*/
#if defined(BOARD_HELTEC_V3)
  static const int   PIN_LED         = 35;    // Heltec V3: LED blanco integrado
  static const int   PIN_BUTTON      = 0;     // Heltec V3: botón PRG (GPIO0)
  static const bool  LED_ACTIVE_HIGH = true;
  static const char* BOARD_LABEL     = "Heltec WiFi LoRa 32 V3 (ESP32-S3)";
#else
  static const int   PIN_LED         = 2;     // DevKit V1: LED azul  -> Heltec V3 = GPIO35
  static const int   PIN_BUTTON      = 0;     // DevKit V1: BOOT      -> Heltec V3 = GPIO0
  static const bool  LED_ACTIVE_HIGH = true;  //                      -> Heltec V3 = true
  static const char* BOARD_LABEL     = "ESP32 DevKit V1 (ESP32-WROOM-32)";
#endif

/* --------------------------------------------------------------------------
 *  1-bis. HARDWARE QUE LLEGA: PULSADOR EXTERNO Y BUZZER   (19-sep-2026)
 * --------------------------------------------------------------------------
 *  Todo esto se configura con -D desde platformio.ini; con los valores por
 *  defecto el llavero se comporta EXACTAMENTE igual que hasta ahora.
 *
 *  EXT_BUTTON_PIN      GPIO del pulsador externo, o -1 para no usarlo.
 *  BOOT_BUTTON_ENABLED 1 = el boton BOOT (GPIO0) tambien dispara alertas.
 *  BUZZER_ENABLED      1 = compila el buzzer piezo pasivo (§4-bis).
 *
 *  ELECCION DEL PIN (GPIO4 por defecto). Descartados:
 *     · 0, 2, 5, 12, 15 -> strapping: su nivel al arrancar decide el modo de
 *       arranque. OJO: el README decia que GPIO5 era "libre y seguro" y NO lo
 *       es (lleva pull-up interno y define el timing de arranque del SDIO).
 *     · 6..11  -> conectados a la flash SPI: usarlos cuelga la placa.
 *     · 34..39 -> son SOLO ENTRADA y NO tienen pull-up interno, asi que
 *       necesitarian una resistencia externa obligatoria.
 *     · 1 y 3  -> UART0, el puerto del monitor serie.
 *     · 21, 22 -> SDA/SCL por defecto; libres hoy, pero se reservan por si
 *       llega una pantalla I2C.
 *     · 16     -> reservado para el RX2 del GPS (§5).
 *  Quedan libres y seguros: 4 (elegido), 13, 14, 17, 18, 19, 23, 25, 26, 27,
 *  32 y 33.
 *
 *  CABLEADO del pulsador (el codigo asume INPUT_PULLUP, LOW = pulsado):
 *
 *      GPIO4 ----+---- [ pulsador ] ---- GND
 *                |
 *                +---- [ 10 kR ] ---- 3V3      (opcional pero recomendable)
 *                |
 *                +---- [ 100 nF ] --- GND      (opcional, antirruido)
 *
 *  El pull-up interno del ESP32 son unos 45 kR: suficiente con 10-20 cm de
 *  cable, flojo para un cable largo de llavero, que hace de antena. Con el
 *  condensador y la resistencia externa el flanco entra limpio. El
 *  antirrebote de 50 ms de lib/panic sigue filtrando los rebotes mecanicos.
 *
 *  AVISO IMPORTANTE (incidencia I10): en esta placa el DTR del CP2102 esta
 *  cableado a GPIO0, el mismo pin de BOOT. Si un terminal abre el puerto con
 *  DTR activo, el firmware lo lee como boton mantenido. Mientras el boton
 *  sea BOOT eso se evita con monitor_dtr = 0. CUANDO SE CABLEE EL PULSADOR
 *  EXTERNO, lo limpio es compilar con -D BOOT_BUTTON_ENABLED=0: asi el
 *  llavero deja de depender de un pin que el USB puede mover solo.
 * ------------------------------------------------------------------------*/
#ifndef EXT_BUTTON_PIN
  #define EXT_BUTTON_PIN 4        // -1 = sin pulsador externo
#endif
#ifndef BOOT_BUTTON_ENABLED
  #define BOOT_BUTTON_ENABLED 1   // 0 al cablear el pulsador externo (I10)
#endif
#ifndef BUZZER_ENABLED
  #define BUZZER_ENABLED 0        // 1 cuando el buzzer este cableado
#endif
#ifndef BUZZER_PIN
  #if defined(BOARD_HELTEC_V3)
    #define BUZZER_PIN 6          // S3: el 25 NO existe. Verificar esquematico.
  #else
    #define BUZZER_PIN 25         // DevKit V1: libre, con PWM
  #endif
#endif
#ifndef BUZZER_FREQ_HZ
  #define BUZZER_FREQ_HZ 2700     // resonancia tipica de un piezo de 12 mm
#endif

/* Guardas: es mejor no compilar que descubrir el problema con la placa
 * colgada o con alertas fantasma en la feria. */
#if !BOOT_BUTTON_ENABLED && (EXT_BUTTON_PIN < 0)
  #error "No queda ninguna fuente de boton: activa BOOT_BUTTON_ENABLED o define EXT_BUTTON_PIN."
#endif
#if !defined(BOARD_HELTEC_V3) && EXT_BUTTON_PIN >= 0
  #if EXT_BUTTON_PIN == 0 || EXT_BUTTON_PIN == 2 || EXT_BUTTON_PIN == 5 || \
      EXT_BUTTON_PIN == 12 || EXT_BUTTON_PIN == 15
    #error "EXT_BUTTON_PIN es un strapping pin (0/2/5/12/15): puede impedir el arranque."
  #endif
  #if EXT_BUTTON_PIN >= 6 && EXT_BUTTON_PIN <= 11
    #error "EXT_BUTTON_PIN esta conectado a la flash SPI (6..11)."
  #endif
  #if EXT_BUTTON_PIN >= 34 && EXT_BUTTON_PIN <= 39
    #error "EXT_BUTTON_PIN (34..39) es solo entrada y no tiene pull-up interno."
  #endif
  #if EXT_BUTTON_PIN == 1 || EXT_BUTTON_PIN == 3
    #error "EXT_BUTTON_PIN es del UART0 (monitor serie)."
  #endif
  #if BUZZER_ENABLED && (BUZZER_PIN == EXT_BUTTON_PIN)
    #error "BUZZER_PIN y EXT_BUTTON_PIN no pueden ser el mismo pin."
  #endif
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
 *  4-bis. BUZZER PIEZO PASIVO   (opcional; con BUZZER_ENABLED=0 no existe)
 * --------------------------------------------------------------------------
 *  Un piezo PASIVO no lleva oscilador dentro: hay que darle una frecuencia.
 *  Aqui se usa LEDC directamente y NO analogWrite() ni tone(), por dos
 *  motivos comprobados en el codigo del core (arduino-esp32 2.0.17):
 *
 *   · analogWrite() reparte canales de ARRIBA ABAJO (esp32-hal-ledc.c: parte
 *     de cnt_channel = 16 y va decrementando), asi que el LED de GPIO2 se
 *     queda con el canal 15; y ademas llama a ledcSetup() EN CADA ESCRITURA,
 *     o sea en cada vuelta del loop(). Si el buzzer cayera en un canal del
 *     mismo timer (el 14), el LED le reescribiria la frecuencia mil veces por
 *     segundo y el pitido saldria destrozado.
 *   · tone() se monta una tarea de FreeRTOS entera solo para esto.
 *
 *  Solucion: el buzzer usa el CANAL 0, que esta en otro timer que el 15.
 *
 *  Prioridad del sonido, la misma que la del LED:
 *      baliza  >  patron puntual (ALERT/CANCEL)  >  silencio
 * ==========================================================================*/
#if BUZZER_ENABLED
namespace buzzer {

  static const uint8_t CANAL = 0;          // ver comentario de arriba

  static bool     s_beacon    = false;
  static bool     s_patActive = false;
  static uint8_t  s_patTotal  = 0;
  static uint8_t  s_patDone   = 0;
  static bool     s_patPhaseOn= false;
  static uint16_t s_patOnMs   = 0;
  static uint16_t s_patOffMs  = 0;
  static uint32_t s_patT0     = 0;

  static inline void tono(bool on) {
    ledcWriteTone(CANAL, on ? BUZZER_FREQ_HZ : 0);
  }

  void begin() {
    ledcSetup(CANAL, BUZZER_FREQ_HZ, 10);
    ledcAttachPin(BUZZER_PIN, CANAL);
    tono(false);
  }

  // Encola N pitidos. Misma forma que led::blink() para que las dos
  // realimentaciones se disparen en el mismo sitio y no se desincronicen.
  void pitar(uint8_t count, uint16_t onMs, uint16_t offMs) {
    s_patActive  = true;
    s_patTotal   = count;
    s_patDone    = 0;
    s_patPhaseOn = true;
    s_patOnMs    = onMs;
    s_patOffMs   = offMs;
    s_patT0      = millis();
    tono(true);
  }

  void setBeacon(bool on) {
    s_beacon = on;
    if (!on) tono(false);
  }

  void update(uint32_t now) {
    if (s_beacon) {                                   // baliza sonora a 2 Hz
      tono((now % 500) < 250);
      return;
    }
    if (s_patActive) {
      uint32_t el = now - s_patT0;
      if (s_patPhaseOn && el >= s_patOnMs) {
        s_patPhaseOn = false;
        s_patT0 = now;
        tono(false);
        if (++s_patDone >= s_patTotal) s_patActive = false;
      } else if (!s_patPhaseOn && el >= s_patOffMs) {
        s_patPhaseOn = true;
        s_patT0 = now;
        tono(true);
      }
      return;
    }
    tono(false);
  }

} // namespace buzzer
#else
/* Buzzer desactivado: funciones vacias para no llenar el codigo de #if.
 * El compilador las elimina enteras, asi que el binario por defecto no
 * carga absolutamente nada de este bloque. */
namespace buzzer {
  static inline void begin()                            {}
  static inline void pitar(uint8_t, uint16_t, uint16_t)  {}
  static inline void setBeacon(bool)                     {}
  static inline void update(uint32_t)                    {}
} // namespace buzzer
#endif

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
/* trace() se define más abajo (§6); aquí basta con declararla. */
static void trace(const char* msg);

#ifndef GPS_UART_ENABLED
  #define GPS_UART_ENABLED 0        // 1 = leer de verdad del GPS por UART2
#endif
#ifndef GPS_RX_PIN
  #define GPS_RX_PIN 16             // GPIO16 = RX2 en la DevKit V1
#endif
#ifndef GPS_BAUD
  #define GPS_BAUD 9600             // valor de fabrica de NEO-6M / NEO-7M
#endif

/* El pulsador externo no puede robarle el pin al GPS (§1-bis). */
#if EXT_BUTTON_PIN >= 0 && (EXT_BUTTON_PIN == GPS_RX_PIN)
  #error "EXT_BUTTON_PIN coincide con GPS_RX_PIN: elige otro GPIO para el pulsador."
#endif
#if BUZZER_ENABLED && (BUZZER_PIN == GPS_RX_PIN)
  #error "BUZZER_PIN coincide con GPS_RX_PIN: elige otro GPIO para el buzzer."
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

  /* Estar CONECTADO no es lo mismo que estar ESCUCHANDO: el cliente ademas
   * tiene que suscribirse a las notificaciones de TX (escribir 0x0100 en el
   * CCCD). Si no lo hace, notify() no manda nada y la alerta se pierde en
   * silencio. Es exactamente la incidencia I5 (el icono de nRF Connect es un
   * interruptor y un toque de mas apaga las notificaciones), asi que el log
   * tiene que distinguir los tres casos en vez de cantar "enviado por BLE". */
  const bool hayCliente = bleHasClient();
  const bool escuchando = (g_txChar != nullptr) && (g_txChar->getSubscribedCount() > 0);
  Serial.printf("[t=%8lu ms] >>> TX  %-24s  %s\n",
                (unsigned long)millis(), msg,
                !hayCliente ? "(sin cliente BLE: registrado solo en serie)"
                            : escuchando
                                ? "(enviado por BLE)"
                                : "(cliente conectado pero SIN suscribirse a TX: NO lo recibe)");
  if (g_txChar && escuchando) {
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
    /* NimBLE PARA el advertising en cuanto acepta una conexion. Si no se
     * relanza aqui, el llavero queda INVISIBLE mientras haya un movil
     * conectado: ningun segundo movil puede entrar (ni siquiera para probar
     * la incidencia I14) y, peor, un movil ajeno que se conecte primero deja
     * fuera al del dueno. Se relanza mientras queden huecos libres.
     * (incidencia I16, 19-sep-2026) */
    if (server->getConnectedCount() < CONFIG_BT_NIMBLE_MAX_CONNECTIONS) {
      NimBLEDevice::startAdvertising();
    }
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

static void handleCommand(const char* rawCmd) {
  // Normaliza: quita CR/LF, pasa a mayúsculas, recorta espacios.
  /* Se guardan DOS copias: `cmd` en mayusculas para comparar ordenes, y
   * `tal_cual` con el texto original. El checksum de una trama NMEA es el XOR
   * de sus bytes, asi que pasarla a mayusculas lo cambia y la trama se
   * descartaba por "checksum malo" cuando traia minusculas. */
  char cmd[CMD_BUF_SIZE];
  char tal_cual[CMD_BUF_SIZE];
  size_t j = 0;
  for (size_t i = 0; rawCmd[i] && j < sizeof(cmd) - 1; ++i) {
    char ch = rawCmd[i];
    if (ch == '\r' || ch == '\n') continue;
    if (j == 0 && ch == ' ')      continue;             // espacios iniciales
    tal_cual[j] = ch;
    cmd[j++]    = (char)toupper((unsigned char)ch);
  }
  while (j > 0 && cmd[j - 1] == ' ') j--;               // espacios finales
  cmd[j]      = '\0';
  tal_cual[j] = '\0';

  if (strcmp(cmd, "BEACON:ON") == 0) {
    g_beaconActive = true;
    led::setBeacon(true);
    buzzer::setBeacon(true);
    trace(BUZZER_ENABLED
              ? "RX  BEACON:ON   -> baliza ACTIVADA  (LED y buzzer a 2 Hz)"
              : "RX  BEACON:ON   -> baliza ACTIVADA  (LED parpadeando a 2 Hz)");
  } else if (strcmp(cmd, "BEACON:OFF") == 0) {
    g_beaconActive = false;
    led::setBeacon(false);
    buzzer::setBeacon(false);
    trace("RX  BEACON:OFF  -> baliza DESACTIVADA");
  } else if (strncmp(cmd, "NMEA ", 5) == 0) {
    // Inyecta una trama a mano: permite probar el GPS sin tener el módulo.
    const uint32_t now = millis();
    if (gps::inject(tal_cual + 5, now)) {
      trace("RX  NMEA        -> trama ACEPTADA (checksum correcto)");
    } else {
      trace("RX  NMEA        -> trama DESCARTADA (checksum o formato malos)");
    }
    gps::printStatus(now);
  } else if (strcmp(cmd, "POS") == 0 || strcmp(cmd, "GPS") == 0) {
    gps::printStatus(millis());
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
static panic::Fsm g_fsm(panic::Config{DEBOUNCE_MS, LONG_PRESS_MS, CANCEL_WINDOW_MS});

/* El boton puede venir de DOS sitios a la vez (§1-bis): el BOOT de la placa y
 * el pulsador externo. Se hace un OR: cualquiera de los dos vale. Un pin con
 * INPUT_PULLUP y nada conectado lee HIGH, asi que tener el pulsador externo
 * compilado pero SIN cablear no dispara nada. */
static inline bool botonPulsado() {
  bool pulsado = false;
#if BOOT_BUTTON_ENABLED
  pulsado = (digitalRead(PIN_BUTTON) == LOW);             // pull-up: LOW = pulsado
#endif
#if EXT_BUTTON_PIN >= 0
  pulsado = pulsado || (digitalRead(EXT_BUTTON_PIN) == LOW);
#endif
  return pulsado;
}

static void fsmUpdate(uint32_t now) {
  const bool pressed = botonPulsado();
  const panic::Step s = g_fsm.update(now, pressed);

  // (a) Lo que viaja por el contrato BLE: ALERT:1 / ALERT:2 / CANCEL.
  const char* token = panic::Fsm::txToken(s.event);
  if (token) emitTx(token);

  // (b) Realimentación visual y trazas propias de cada evento.
  switch (s.event) {
    case panic::Event::ALERT_SHORT:
      led::blink(1, 800, 0);              // un parpadeo largo
      buzzer::pitar(1, 800, 0);           // y un pitido largo (si esta cableado)
      led::setBreathing(true);
      break;
    case panic::Event::ALERT_LONG:
      led::blink(2, 250, 200);            // dos parpadeos
      buzzer::pitar(2, 250, 200);
      led::setBreathing(true);
      break;
    case panic::Event::CANCEL:
      led::blink(3, 90, 90);              // tres parpadeos rapidos
      buzzer::pitar(3, 90, 90);
      led::setBreathing(false);
      break;
    case panic::Event::CANCEL_ARMED:
      trace("CANCEL_WINDOW: botón liberado -> cancelación ARMADA");
      break;
    case panic::Event::WINDOW_EXPIRED:
      led::setBreathing(false);
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
  Serial.println("     BEACON:ON    activa la baliza (LED a 2 Hz)");
  Serial.println("     BEACON:OFF   desactiva la baliza");
  Serial.println("     POS          muestra la ultima posicion GPS conocida");
  Serial.println("     NMEA <trama> inyecta una sentencia NMEA a mano, por ejemplo:");
  Serial.println("                  NMEA $GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47");
  Serial.println("     ?            muestra esta ayuda");
#if BOOT_BUTTON_ENABLED && (EXT_BUTTON_PIN >= 0)
  Serial.printf ("  El boton de panico es fisico: BOOT (GPIO%d) o el pulsador externo (GPIO%d).\n",
                 PIN_BUTTON, EXT_BUTTON_PIN);
#elif BOOT_BUTTON_ENABLED
  Serial.printf ("  El boton de panico es fisico: BOOT (GPIO%d) en la placa.\n", PIN_BUTTON);
#else
  Serial.printf ("  El boton de panico es el pulsador externo (GPIO%d); BOOT esta desactivado.\n",
                 EXT_BUTTON_PIN);
#endif
  Serial.println();
}

static void printBanner() {
  Serial.println();
  Serial.println("============================================================");
  Serial.println("  GEO-EXPO ALERT  -  Firmware etapa 1");
  Serial.printf ("  Placa            : %s\n", BOARD_LABEL);
  Serial.printf ("  LED en GPIO%-2d     Boton BOOT en GPIO%d %s\n",
                 PIN_LED, PIN_BUTTON, BOOT_BUTTON_ENABLED ? "(activo)" : "(DESACTIVADO)");
#if EXT_BUTTON_PIN >= 0
  Serial.printf ("  Pulsador externo : GPIO%d  (a masa, con pull-up interno)\n", EXT_BUTTON_PIN);
#else
  Serial.println("  Pulsador externo : sin usar");
#endif
#if BUZZER_ENABLED
  Serial.printf ("  Buzzer           : GPIO%d a %d Hz (canal LEDC 0)\n", BUZZER_PIN, BUZZER_FREQ_HZ);
#else
  Serial.println("  Buzzer           : desactivado (la baliza se ve en el LED)");
#endif
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

  pinMode(PIN_BUTTON, INPUT_PULLUP);
#if EXT_BUTTON_PIN >= 0
  pinMode(EXT_BUTTON_PIN, INPUT_PULLUP);
#endif
  // Arrancar con el boton pulsado (o con el DTR del monitor tirando de GPIO0)
  // no cuenta como flanco: la FSM exige soltarlo antes de armar una pulsacion.
  g_fsm.begin(botonPulsado(), millis());
  led::begin();
  buzzer::begin();

  printBanner();
  gps::begin();
  bleBegin();
  trace("BLE: advertising iniciado como \"GEOEXPO-ALERT\"");
  trace("Sistema listo. Estado inicial: IDLE");
}

void loop() {
  uint32_t now = millis();

  blePollRx();          // comandos recibidos por BLE
  serialPoll();         // comandos recibidos por el monitor serie
  gps::poll(now);       // tramas NMEA del receptor GPS (si está conectado)
  fsmUpdate(now);       // maquina de estados del boton
  led::update(now);     // realimentacion visual
  buzzer::update(now);  // realimentacion sonora (si BUZZER_ENABLED)

  // Cesión cooperativa al planificador de FreeRTOS para no matar de hambre a
  // la tarea IDLE (evita el "Task watchdog"). NO se usa para temporizar:
  // toda la lógica temporal va por millis().
  delay(1);
}
