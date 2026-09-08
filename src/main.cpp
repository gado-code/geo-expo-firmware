/* ============================================================================
 *  GEO-EXPO ALERT  ·  Firmware etapa 1  (solo periféricos integrados)
 * ============================================================================
 *  Placa de desarrollo : ESP32 DevKit V1  (ESP32-WROOM-32, Xtensa LX6, BT clásico)
 *  Placa final (19-sep) : Heltec WiFi LoRa 32 V3 (ESP32-S3 + SX1262, NimBLE)
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
 * ==========================================================================*/

#include <Arduino.h>
#include <ctype.h>
#include <string.h>
#include <NimBLEDevice.h>

/* ==========================================================================
 *  1. CONFIGURACIÓN DE PINES
 * --------------------------------------------------------------------------
 *  Se selecciona con -D BOARD_HELTEC_V3 desde platformio.ini.
 *  La tabla documenta el valor equivalente para la Heltec V3 (migración
 *  en 2 semanas):
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
 *  Se usa analogWrite(): funciona IGUAL en ESP32 y ESP32-S3, no hay que
 *  tocar nada en la migración (a diferencia de ledcSetup/ledcAttachPin,
 *  que cambian de firma entre arduino-esp32 2.x y 3.x).
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
 *  5. ESTADO GLOBAL Y BLE
 * ==========================================================================*/
static NimBLECharacteristic* g_txChar = nullptr;
static volatile bool         g_bleConnected = false;
static bool                  g_beaconActive = false;   // estado lógico de la baliza

// Cola mínima RX: los WRITE del cliente se copian aquí y se procesan en
// loop(); NUNCA se hace trabajo pesado dentro del callback de NimBLE.
static volatile bool  g_rxPending = false;
static char           g_rxBuf[40];
static portMUX_TYPE   g_rxMux = portMUX_INITIALIZER_UNLOCKED;

/* ---- Trazas -------------------------------------------------------------- */
static void trace(const char* msg) {
  Serial.printf("[t=%8lu ms] %s\n", (unsigned long)millis(), msg);
}

/* ---- Emisión por TX: BLE NOTIFY si hay cliente, y SIEMPRE por serie ------ */
static void emitTx(const char* token) {
  Serial.printf("[t=%8lu ms] >>> TX  %-8s  %s\n",
                (unsigned long)millis(), token,
                g_bleConnected ? "(enviado por BLE)"
                               : "(sin cliente BLE: registrado solo en serie)");
  if (g_txChar && g_bleConnected) {
    char buf[24];
    int n = snprintf(buf, sizeof(buf), "%s\n", token);   // contrato: termina en '\n'
    g_txChar->setValue(reinterpret_cast<uint8_t*>(buf), n);
    g_txChar->notify();
  }
}

/* ---- Callbacks del servidor BLE ---------------------------------------- */
class ServerCB : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer* /*server*/) override {
    g_bleConnected = true;
    trace("BLE: cliente CONECTADO");
  }
  void onDisconnect(NimBLEServer* /*server*/) override {
    g_bleConnected = false;
    trace("BLE: cliente DESCONECTADO -> se reanuda el advertising");
    NimBLEDevice::startAdvertising();
  }
};

/* ---- Callback de la característica RX (app -> ESP32) -------------------- */
class RxCB : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* c) override {
    NimBLEAttValue v = c->getValue();
    const char*  s   = v.c_str();
    size_t       len = v.length();
    portENTER_CRITICAL(&g_rxMux);
    size_t n = (len < sizeof(g_rxBuf) - 1) ? len : sizeof(g_rxBuf) - 1;
    memcpy(g_rxBuf, s, n);
    g_rxBuf[n] = '\0';
    g_rxPending = true;
    portEXIT_CRITICAL(&g_rxMux);
  }
};

/* ==========================================================================
 *  6. INICIALIZACIÓN BLE
 * ==========================================================================*/
static void bleBegin() {
  NimBLEDevice::init(DEVICE_NAME);
  NimBLEDevice::setPower(ESP_PWR_LVL_P9);          // +9 dBm: alcance de demostración

  NimBLEServer* server = NimBLEDevice::createServer();
  server->setCallbacks(new ServerCB());

  NimBLEService* svc = server->createService(NUS_SERVICE_UUID);

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
 *  7. PROCESAMIENTO DE COMANDOS RX  (BLE o serie)
 * ==========================================================================*/
static void printHelp();

static void handleCommand(const char* rawCmd) {
  // Normaliza: quita CR/LF, pasa a mayúsculas, recorta espacios.
  char cmd[40];
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
    trace("RX  BEACON:ON   -> baliza ACTIVADA  (LED parpadeando a 2 Hz)");
  } else if (strcmp(cmd, "BEACON:OFF") == 0) {
    g_beaconActive = false;
    led::setBeacon(false);
    trace("RX  BEACON:OFF  -> baliza DESACTIVADA");
  } else if (strcmp(cmd, "?") == 0 || strcmp(cmd, "HELP") == 0) {
    printHelp();
  } else {
    Serial.printf("[t=%8lu ms] RX  comando no reconocido: \"%s\"\n",
                  (unsigned long)millis(), cmd);
  }
}

/* ---- Vaciar la cola RX de BLE (se llama desde loop()) ----------------- */
static void blePollRx() {
  if (!g_rxPending) return;
  char local[40];
  portENTER_CRITICAL(&g_rxMux);
  strncpy(local, g_rxBuf, sizeof(local));
  local[sizeof(local) - 1] = '\0';
  g_rxPending = false;
  portEXIT_CRITICAL(&g_rxMux);
  Serial.printf("[t=%8lu ms] <<< RX (BLE) \"%s\"\n", (unsigned long)millis(), local);
  handleCommand(local);
}

/* ---- Comandos por el monitor serie (para probar SIN celular) ---------- */
static void serialPoll() {
  static char   line[40];
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
 *  8. MÁQUINA DE ESTADOS DEL BOTÓN
 * --------------------------------------------------------------------------
 *  Estados: IDLE -> DEBOUNCE -> PRESSED -> CANCEL_WINDOW -> IDLE
 *
 *  · Pulsación corta  : soltar antes de 3 s -> "ALERT:1" + ventana de cancelación
 *  · Pulsación larga  : mantener 3 s        -> "ALERT:2" EN EL INSTANTE de los
 *                       3 s (sin esperar a soltar) + ventana de cancelación
 *  · Ventana (10 s)   : cualquier pulsación nueva -> "CANCEL" y vuelta a IDLE
 *  · Ventana expira   : vuelta a IDLE (la alerta queda CONFIRMADA)
 *  · Antirrebote de 50 ms aplicado al flanco de bajada Y al de subida.
 * ==========================================================================*/
enum ButtonState { ST_IDLE, ST_DEBOUNCE, ST_PRESSED, ST_CANCEL_WINDOW };

static ButtonState g_st            = ST_IDLE;
static uint32_t    g_tDebounce     = 0;    // entrada a DEBOUNCE
static uint32_t    g_tPressed      = 0;    // pulsación confirmada (para contar los 3 s)
static uint32_t    g_tCancelStart  = 0;    // inicio de la ventana de cancelación
static uint32_t    g_tRawChange    = 0;    // último cambio del nivel crudo (antirrebote de subida)
static int         g_lastRaw       = HIGH;
static bool        g_longFired     = false;
static bool        g_cancelArmed   = false;

static const char* stName(ButtonState s) {
  switch (s) {
    case ST_IDLE:          return "IDLE";
    case ST_DEBOUNCE:      return "DEBOUNCE";
    case ST_PRESSED:       return "PRESSED";
    case ST_CANCEL_WINDOW: return "CANCEL_WINDOW";
  }
  return "?";
}

static void go(ButtonState next, const char* motivo) {
  Serial.printf("[t=%8lu ms] FSM  %-13s -> %-13s  (%s)\n",
                (unsigned long)millis(), stName(g_st), stName(next), motivo);
  g_st = next;
}

static void fsmUpdate(uint32_t now) {
  int raw = digitalRead(PIN_BUTTON);      // LOW = pulsado (pull-up interno)

  switch (g_st) {

    /* ---------------------------------------------------------------- */
    case ST_IDLE:
      if (raw == LOW) {
        g_tDebounce = now;
        go(ST_DEBOUNCE, "flanco de bajada detectado");
      }
      break;

    /* ---------------------------------------------------------------- */
    case ST_DEBOUNCE:
      if (raw == HIGH) {
        go(ST_IDLE, "rebote/ruido: se soltó antes de DEBOUNCE_MS");
      } else if (now - g_tDebounce >= DEBOUNCE_MS) {
        g_tPressed   = now;
        g_longFired  = false;
        g_lastRaw    = LOW;
        g_tRawChange = now;
        go(ST_PRESSED, "pulsación confirmada tras 50 ms");
      }
      break;

    /* ---------------------------------------------------------------- */
    case ST_PRESSED: {
      // (a) Pulsación prolongada: emitir EN EL INSTANTE de los 3 s.
      if (!g_longFired && (now - g_tPressed >= LONG_PRESS_MS)) {
        g_longFired    = true;
        emitTx("ALERT:2");
        led::blink(2, 250, 200);          // dos parpadeos
        g_tCancelStart = now;
        g_cancelArmed  = false;           // hay que esperar a que suelte el botón
        g_lastRaw      = raw;
        g_tRawChange   = now;
        led::setBreathing(true);
        go(ST_CANCEL_WINDOW, "ALERT:2 : mantenido >= 3 s");
        break;
      }
      // (b) Antirrebote del flanco de subida (soltar el botón).
      if (raw != g_lastRaw) { g_lastRaw = raw; g_tRawChange = now; }
      if (raw == HIGH && (now - g_tRawChange >= DEBOUNCE_MS)) {
        // Se soltó antes de los 3 s -> pulsación corta.
        emitTx("ALERT:1");
        led::blink(1, 800, 0);            // un parpadeo largo
        g_tCancelStart = now;
        g_cancelArmed  = true;            // el botón ya está liberado
        g_lastRaw      = HIGH;
        g_tRawChange   = now;
        led::setBreathing(true);
        go(ST_CANCEL_WINDOW, "ALERT:1 : pulsación corta");
      }
      break;
    }

    /* ---------------------------------------------------------------- */
    case ST_CANCEL_WINDOW: {
      // Antirrebote común a ambos flancos dentro de la ventana.
      if (raw != g_lastRaw) { g_lastRaw = raw; g_tRawChange = now; }
      bool stable = (now - g_tRawChange >= DEBOUNCE_MS);

      if (!g_cancelArmed) {
        // Venimos de una pulsación prolongada con el botón aún pulsado:
        // se arma la cancelación cuando el botón se suelta de forma estable.
        if (raw == HIGH && stable) {
          g_cancelArmed = true;
          trace("CANCEL_WINDOW: botón liberado -> cancelación ARMADA");
        }
      } else if (raw == LOW && stable) {
        // Nueva pulsación dentro de la ventana -> CANCELAR.
        emitTx("CANCEL");
        led::blink(3, 90, 90);            // tres parpadeos rápidos
        led::setBreathing(false);
        go(ST_IDLE, "CANCEL dentro de la ventana");
        break;
      }

      if (now - g_tCancelStart >= CANCEL_WINDOW_MS) {
        led::setBreathing(false);
        go(ST_IDLE, "ventana expirada: alerta CONFIRMADA");
      }
      break;
    }
  }
}

/* ==========================================================================
 *  9. BANNER / AYUDA
 * ==========================================================================*/
static void printHelp() {
  Serial.println();
  Serial.println("  Comandos por el monitor serie (equivalen a un WRITE en RX):");
  Serial.println("     BEACON:ON    activa la baliza (LED a 2 Hz)");
  Serial.println("     BEACON:OFF   desactiva la baliza");
  Serial.println("     ?            muestra esta ayuda");
  Serial.println("  El botón de pánico es físico: BOOT (GPIO0) en la placa.");
  Serial.println();
}

static void printBanner() {
  Serial.println();
  Serial.println("============================================================");
  Serial.println("  GEO-EXPO ALERT  -  Firmware etapa 1");
  Serial.printf ("  Placa            : %s\n", BOARD_LABEL);
  Serial.printf ("  LED en GPIO%-2d     Boton BOOT en GPIO%d\n", PIN_LED, PIN_BUTTON);
  Serial.println("------------------------------------------------------------");
  Serial.println("  BLE  Nordic UART Service");
  Serial.printf ("    Nombre   : %s\n", DEVICE_NAME);
  Serial.println("    Servicio : 6E400001-B5A3-F393-E0A9-E50E24DCCA9E");
  Serial.println("    TX NOTIFY: 6E400003-... (ESP32 -> app)");
  Serial.println("    RX WRITE : 6E400002-... (app  -> ESP32)");
  Serial.println("------------------------------------------------------------");
  Serial.printf ("  DEBOUNCE_MS=%lu  LONG_PRESS_MS=%lu  CANCEL_WINDOW_MS=%lu\n",
                 (unsigned long)DEBOUNCE_MS, (unsigned long)LONG_PRESS_MS,
                 (unsigned long)CANCEL_WINDOW_MS);
  Serial.println("============================================================");
  printHelp();
}

/* ==========================================================================
 *  10. setup() / loop()
 * ==========================================================================*/
void setup() {
  Serial.begin(115200);
  uint32_t t0 = millis();
  while (!Serial && (millis() - t0) < 2000) { /* espera opcional al USB-CDC (S3) */ }

  pinMode(PIN_BUTTON, INPUT_PULLUP);
  led::begin();

  printBanner();
  bleBegin();
  trace("BLE: advertising iniciado como \"GEOEXPO-ALERT\"");
  trace("Sistema listo. Estado inicial: IDLE");
}

void loop() {
  uint32_t now = millis();

  blePollRx();          // comandos recibidos por BLE
  serialPoll();         // comandos recibidos por el monitor serie
  fsmUpdate(now);       // máquina de estados del botón
  led::update(now);     // realimentación visual

  // Cesión cooperativa al planificador de FreeRTOS para no matar de hambre a
  // la tarea IDLE (evita el "Task watchdog"). NO se usa para temporizar:
  // toda la lógica temporal va por millis().
  delay(1);
}
