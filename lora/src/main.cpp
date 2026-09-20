/* ============================================================================
 *  GEO-EXPO LoRa  ·  Firmware del nodo   (DISENO §12.4)
 * ============================================================================
 *  Las DOS placas llevan EL MISMO binario: lo que las distingue es el ID, que
 *  sale de la MAC (o de la NVS si se fijó a mano con "ID <hex4>").
 *
 *  Este archivo es sólo el PEGAMENTO. Toda la lógica de red vive en
 *  lib/geolora, sin Arduino, y se prueba en el PC con `pio test -e native`
 *  (197 casos). Aquí queda lo que de verdad necesita la placa:
 *
 *      radio.cpp  <->  motor  <->  consola serie / OLED / botón / LED
 *
 *  Vuelta del loop(), sin un solo delay() (§12.4):
 *      1. ¿trama recibida?      -> motor.alRecibir()
 *      2. temporizadores        -> motor.tick()
 *      3. ¿algo que mandar?     -> CAD + TX -> txHecha / canalOcupado / txFallida
 *      4. sucesos del motor     -> consola ("EV ..."), OLED y LED
 *      5. bytes del puerto      -> parser de la consola
 *      6. botón PRG             -> gestos (corta / doble / larga)
 *      7. prueba de alcance     -> ¿toca otro PING?
 *      8. OLED si toca (<= 4 Hz)
 *
 *  Respuestas deterministas, pensadas para leerlas con un script (§9):
 *      "OK ..."   lo pedido salió bien
 *      "ERR ..."  no se pudo, con el motivo
 *      "EV ..."   suceso asíncrono (llegó un ACK, se agotaron los intentos...)
 *      "R,..."    una medida de RANGE, en CSV
 * ==========================================================================*/
#include <Arduino.h>
#include <esp_system.h>
#include <stdio.h>
#include <string.h>

#include "ajustes.h"
#include "carga.h"
#include "consola.h"
#include "gestos.h"
#include "identidad.h"
#include "motor.h"
#include "pantalla.h"
#include "paquete.h"
#include "placa.h"
#include "radio.h"
#include "radio_params.h"
#include "rango.h"

using namespace geolora;

/* ==========================================================================
 *  1. ESTADO GLOBAL
 * ==========================================================================*/
static ConfigNodo g_cfg;
static Motor*     g_motor = nullptr;     // se construye en setup(), con el ID ya sabido
static Gestos     g_gestos;
static Rango      g_rango;

static uint8_t  g_mac[6]   = {0};
static bool     g_idDeNvs  = false;
static uint32_t g_tArranque = 0;

/* Última medida de enlace, para el OLED. */
static pantalla::Estado g_vista;

/* El motor pide azar por una función inyectada: en la placa es el generador
 * por hardware del ESP32; en los tests, uno determinista con semilla. */
static uint32_t azarHw(void*) { return esp_random(); }

/* ==========================================================================
 *  2. LED (GPIO35): destello en TX, doble en RX, 4 Hz con una ALERT  (§10)
 * ==========================================================================*/
namespace led {

static uint8_t  s_pendientes = 0;     // destellos que quedan
static bool     s_encendido  = false;
static uint32_t s_t0         = 0;
static uint32_t s_alertaHasta = 0;
static const uint32_t DESTELLO_MS = 20;

static void begin() {
  pinMode(placa::PIN_LED, OUTPUT);
  digitalWrite(placa::PIN_LED, LOW);
}
static void destellos(uint8_t n) {
  s_pendientes = n;
  s_t0 = millis();
  s_encendido = true;
  digitalWrite(placa::PIN_LED, HIGH);
}
static void alerta(uint32_t ahoraMs) { s_alertaHasta = ahoraMs + 10000; }

static void update(uint32_t ahoraMs) {
  if ((int32_t)(s_alertaHasta - ahoraMs) > 0) {       // 4 Hz mientras dure
    digitalWrite(placa::PIN_LED, ((ahoraMs % 250) < 125) ? HIGH : LOW);
    return;
  }
  if (s_pendientes == 0) { digitalWrite(placa::PIN_LED, LOW); return; }
  if ((uint32_t)(ahoraMs - s_t0) < DESTELLO_MS) return;
  s_t0 = ahoraMs;
  s_encendido = !s_encendido;
  digitalWrite(placa::PIN_LED, s_encendido ? HIGH : LOW);
  if (!s_encendido && --s_pendientes == 0) digitalWrite(placa::PIN_LED, LOW);
}

}  // namespace led

/* ==========================================================================
 *  3. UTILIDADES DE IMPRESIÓN
 * ==========================================================================*/
/* SNR en cuartos de dB -> "12.5" con un decimal, sin coma flotante. */
static void textoSnr(int8_t q4, char* out, size_t cap) {
  const int32_t d = (int32_t)q4 * 10 / 4;
  snprintf(out, cap, "%ld.%ld", (long)(d / 10), (long)(d < 0 ? -(d % 10) : d % 10));
}

static void imprimirId(uint16_t id, char* out) { formatearId(id, out); }

static void imprimirRadio() {
  char mhz[16];
  formatearMHz(g_cfg.radio.freqKHz, mhz, sizeof mhz);
  const uint32_t tAlerta = airtimeMs(g_cfg.radio, SOBRECARGA + LEN_ALERTA_CON_POS);
  Serial.printf("OK RADIO freq=%s MHz sf=%u bw=%u cr=4/%u sync=%02X pre=%u pwr=%d preset=%s\n",
                mhz, g_cfg.radio.sf, g_cfg.radio.bwKHz, g_cfg.radio.cr, g_cfg.radio.sync,
                g_cfg.radio.pre, g_cfg.radio.pwr, nombrePreset(presetDe(g_cfg.radio)));
  Serial.printf("OK RADIO airtime_alerta=%lu ms  range_min=%lu ms  ldro=%s\n",
                (unsigned long)tAlerta,
                (unsigned long)intervaloMinimoRangeMs(g_cfg.radio),
                ldroActivo(g_cfg.radio) ? "si" : "no");
}

static void imprimirEstado() {
  char id[5];
  imprimirId(g_cfg.id, id);
  const Contadores& c = g_motor->contadores();
  const uint32_t up = (millis() - g_tArranque) / 1000;
  Serial.printf("OK STATUS id=%s uptime=%lu s radio=%s modo=%s\n", id, (unsigned long)up,
                radio::viva() ? "ok" : "KO",
                (g_cfg.ttl == 0 && !g_cfg.relay) ? "E1 (punto a punto)" : "E2 (multi-salto)");
  Serial.printf("OK STATUS ttl=%u relay=%s rol=%s aire=%lu.%lu%%\n", g_cfg.ttl,
                g_cfg.relay ? "ON" : "OFF", nombreRol(g_cfg.rol),
                (unsigned long)(g_motor->ocupacionPorMil(millis()) / 10),
                (unsigned long)(g_motor->ocupacionPorMil(millis()) % 10));
  Serial.printf("OK STATUS tx=%lu rx=%lu validas=%lu entregados=%lu dup=%lu ecos=%lu\n",
                (unsigned long)c.tx, (unsigned long)c.rxTramas, (unsigned long)c.rxValidas,
                (unsigned long)c.entregados, (unsigned long)c.duplicados, (unsigned long)c.ecos);
  Serial.printf("OK STATUS ack_tx=%lu ack_rx=%lu huerfanos=%lu reintentos=%lu fallos=%lu\n",
                (unsigned long)c.acksTx, (unsigned long)c.acksRx,
                (unsigned long)c.acksHuerfanos, (unsigned long)c.reintentos,
                (unsigned long)c.fallos);
  Serial.printf("OK STATUS reenviados=%lu suprimidos=%lu descartes=%lu cad=%lu txerr=%lu crc_phy=%lu\n",
                (unsigned long)c.reenviados, (unsigned long)c.suprimidos,
                (unsigned long)c.descartesCola, (unsigned long)c.cadOcupado,
                (unsigned long)c.txErrores, (unsigned long)radio::crcMalos());
  /* Rechazos por motivo: el orden es el de la validación (paquete.h). */
  Serial.print("OK STATUS rechazos:");
  for (int i = 1; i < (int)Rechazo::N_RECHAZOS; ++i) {
    if (c.rechazos[i]) Serial.printf(" %s=%lu", nombreRechazo((Rechazo)i), (unsigned long)c.rechazos[i]);
  }
  Serial.println();

  int n = 0;
  for (int i = 0; i < Motor::N_VECINOS; ++i) {
    const Vecino& v = g_motor->vecino(i);
    if (!v.usado) continue;
    char vid[5], s[8];
    imprimirId(v.id, vid);
    textoSnr(v.snrQ4, s, sizeof s);
    Serial.printf("OK VECINO %s rssi=%d snr=%s edad=%lu s %s saltos=%s\n", vid, v.rssi, s,
                  (unsigned long)((millis() - v.t) / 1000), v.directo ? "directo" : "indirecto",
                  v.saltos == 0xFF ? "?" : String(v.saltos).c_str());
    ++n;
  }
  if (n == 0) Serial.println("OK VECINO (ninguno todavia)");
}

static void imprimirAyuda() {
  Serial.println("OK HELP ordenes (una por linea, mayusculas o minusculas):");
  Serial.println("  ID | ID <hex4> | ID AUTO");
  Serial.println("  PING <dst|*> [bytes] | SEND <dst> <texto> | BCAST <texto>");
  Serial.println("  ALERT <1|2|C> [lat lon] [PRUEBA]");
  Serial.println("  STATUS | RADIO");
  Serial.println("  RADIO FREQ <MHz> | BW <kHz> | CR <5-8> | SYNC <hex2> | PRE <n> | SF <7-12> | PWR <dBm>");
  Serial.println("  RADIO PRESET <RAPIDO|NORMAL|LARGO|LAB125> | RADIO SAVE|LOAD|DEFECTO");
  Serial.println("  SF <7-12> | PWR <-9..22> | TTL <0-7> | RELAY ON|OFF | ROL NORMAL|REPETIDOR|PASARELA");
  Serial.println("  RANGE <dst> [n] [ms] | RANGE STOP | RANGE INFORME");
  Serial.println("  PERDIDA <0-100>   (solo depuracion)");
  Serial.println("  Boton PRG: corta = PING / punto nuevo en RANGE, doble = pagina del OLED,");
  Serial.println("             larga = iniciar o parar RANGE");
}

static void imprimirBanner() {
  char id[5], mhz[16];
  imprimirId(g_cfg.id, id);
  formatearMHz(g_cfg.radio.freqKHz, mhz, sizeof mhz);
  Serial.println();
  Serial.println("============================================================");
  Serial.println("  GEO-EXPO LoRa  -  nodo (etapa E1: punto a punto)");
  Serial.printf ("  Placa  : Heltec WiFi LoRa 32 V3 (ESP32-S3 + SX1262)\n");
  Serial.printf ("  ID     : %s  (%s)   MAC %02X:%02X:%02X:%02X:%02X:%02X\n", id,
                 g_idDeNvs ? "fijado en NVS" : "derivado de la MAC",
                 g_mac[0], g_mac[1], g_mac[2], g_mac[3], g_mac[4], g_mac[5]);
  Serial.printf ("  Radio  : %s MHz  SF%u  BW%u  CR4/%u  sync %02X  %d dBm  (%s)\n", mhz,
                 g_cfg.radio.sf, g_cfg.radio.bwKHz, g_cfg.radio.cr, g_cfg.radio.sync,
                 g_cfg.radio.pwr, nombrePreset(presetDe(g_cfg.radio)));
  Serial.printf ("  Modo   : TTL %u  RELAY %s  ->  %s\n", g_cfg.ttl, g_cfg.relay ? "ON" : "OFF",
                 (g_cfg.ttl == 0 && !g_cfg.relay) ? "E1 punto a punto" : "E2 multi-salto");
  Serial.println("------------------------------------------------------------");
  Serial.println("  Las dos placas llevan el MISMO binario; las distingue el ID.");
  Serial.println("  Escribe HELP para ver las ordenes.");
  Serial.println("============================================================");
}

/* ==========================================================================
 *  4. APLICAR PARÁMETROS DE RADIO
 * --------------------------------------------------------------------------
 *  Se valida SIEMPRE con la misma función pura que usan la consola y la NVS.
 *  Si el cambio no vale, no se toca nada: más vale quedarse como estaba que
 *  dejar la placa sorda y sin saber por qué.
 * ==========================================================================*/
static bool aplicarRadio(const ParamsRadio& nuevo, const char*& motivo) {
  const ErrorRadio e = validar(nuevo);
  if (e != ErrorRadio::OK) { motivo = nombreErrorRadio(e); return false; }

  int16_t codigo = 0;
  if (!radio::aplicar(nuevo, codigo)) {
    static char buf[32];
    snprintf(buf, sizeof buf, "RADIO %d", codigo);
    motivo = buf;
    return false;
  }
  g_cfg.radio = nuevo;
  g_motor->configurar(g_cfg);
  return true;
}

/* ==========================================================================
 *  5. EJECUCIÓN DE LAS ÓRDENES DE LA CONSOLA
 * ==========================================================================*/
static void guardarSiProcede() { /* sólo con RADIO SAVE: ver §11 */ }

static void ejecutar(const Orden& o) {
  const uint32_t ahora = millis();
  char idtxt[5];

  switch (o.cmd) {
    case Cmd::VACIA:
      break;

    case Cmd::ERROR:
      Serial.printf("ERR %s\n", o.error ? o.error : "orden no reconocida");
      break;

    case Cmd::HELP:
      imprimirAyuda();
      break;

    /* ---- identidad ---------------------------------------------------- */
    case Cmd::ID_VER:
      imprimirId(g_cfg.id, idtxt);
      Serial.printf("OK ID %s origen=%s mac=%02X:%02X:%02X:%02X:%02X:%02X\n", idtxt,
                    g_idDeNvs ? "NVS" : "MAC", g_mac[0], g_mac[1], g_mac[2], g_mac[3],
                    g_mac[4], g_mac[5]);
      break;

    case Cmd::ID_FIJAR:
      if (!ajustes::guardarId(o.dst)) { Serial.println("ERR ID reservado"); break; }
      g_cfg.id   = o.dst;
      g_idDeNvs  = true;
      g_motor->configurar(g_cfg);
      imprimirId(g_cfg.id, idtxt);
      Serial.printf("OK ID %s guardado\n", idtxt);
      break;

    case Cmd::ID_AUTO: {
      ajustes::borrarId();
      g_cfg.id  = derivarId(g_mac);
      g_idDeNvs = false;
      g_motor->configurar(g_cfg);
      imprimirId(g_cfg.id, idtxt);
      Serial.printf("OK ID %s (derivado de la MAC)\n", idtxt);
      break;
    }

    /* ---- tráfico ------------------------------------------------------ */
    case Cmd::PING: {
      const uint16_t dst = o.broadcast ? DIR_BROADCAST : o.dst;
      const int32_t  id  = g_motor->enviarPing(dst, (uint8_t)o.valor, ahora);
      if (id < 0) { Serial.println("ERR cola llena"); break; }
      imprimirId(dst, idtxt);
      Serial.printf("OK PING %s msgId=%04X\n", idtxt, (unsigned)id);
      break;
    }

    case Cmd::SEND: {
      const int32_t id = g_motor->enviarDatos(o.dst, (const uint8_t*)o.texto, o.lenTexto, ahora);
      if (id < 0) { Serial.println("ERR cola llena"); break; }
      imprimirId(o.dst, idtxt);
      Serial.printf("OK SEND %s msgId=%04X len=%u\n", idtxt, (unsigned)id, o.lenTexto);
      break;
    }

    case Cmd::BCAST: {
      const int32_t id =
          g_motor->enviarDatos(DIR_BROADCAST, (const uint8_t*)o.texto, o.lenTexto, ahora);
      if (id < 0) { Serial.println("ERR cola llena"); break; }
      Serial.printf("OK BCAST msgId=%04X len=%u\n", (unsigned)id, o.lenTexto);
      break;
    }

    case Cmd::ALERT: {
      const int32_t id =
          g_motor->enviarAlerta(o.codigo, o.conPos, o.lat1e7, o.lon1e7, o.prueba, ahora);
      if (id < 0) { Serial.println("ERR cola llena"); break; }
      Serial.printf("OK ALERT %s msgId=%04X seq=%u%s\n", tokenAlerta(o.codigo), (unsigned)id,
                    g_motor->seqAlerta(), o.prueba ? " PRUEBA" : "");
      break;
    }

    /* ---- estado ------------------------------------------------------- */
    case Cmd::STATUS:    imprimirEstado(); break;
    case Cmd::RADIO_VER: imprimirRadio();  break;

    /* ---- parámetros de radio ------------------------------------------ */
    case Cmd::RADIO_FREQ:
    case Cmd::RADIO_BW:
    case Cmd::RADIO_CR:
    case Cmd::RADIO_SYNC:
    case Cmd::RADIO_PRE:
    case Cmd::RADIO_SF:
    case Cmd::RADIO_PWR:
    case Cmd::RADIO_PRESET: {
      ParamsRadio p = g_cfg.radio;
      switch (o.cmd) {
        case Cmd::RADIO_FREQ:   p.freqKHz = o.freqKHz;            break;
        case Cmd::RADIO_BW:     p.bwKHz   = (uint16_t)o.valor;    break;
        case Cmd::RADIO_CR:     p.cr      = (uint8_t)o.valor;     break;
        case Cmd::RADIO_SYNC:   p.sync    = (uint8_t)o.valor;     break;
        case Cmd::RADIO_PRE:    p.pre     = (uint16_t)o.valor;    break;
        case Cmd::RADIO_SF:     p.sf      = (uint8_t)o.valor;     break;
        case Cmd::RADIO_PWR:    p.pwr     = (int8_t)o.valor;      break;
        default:                aplicarPreset(o.preset, p);       break;
      }
      const char* motivo = "";
      if (!aplicarRadio(p, motivo)) { Serial.printf("ERR %s\n", motivo); break; }
      if (o.cmd == Cmd::RADIO_PWR && p.pwr > PWR_AVISO) {
        Serial.printf("OK PWR %d dBm  AVISO: por encima de %d dBm revisa el limite legal (DISENO 3)\n",
                      p.pwr, PWR_AVISO);
      }
      imprimirRadio();
      Serial.println("OK RADIO aplicado (no guardado: usa RADIO SAVE)");
      break;
    }

    case Cmd::RADIO_SAVE: {
      ajustes::Guardado g;
      g.radio = g_cfg.radio; g.ttl = g_cfg.ttl; g.relay = g_cfg.relay; g.rol = g_cfg.rol;
      Serial.println(ajustes::guardar(g) ? "OK RADIO guardado en NVS" : "ERR no se pudo guardar");
      break;
    }

    case Cmd::RADIO_LOAD: {
      ajustes::Guardado g;
      bool invalida = false;
      if (!ajustes::cargar(g, invalida)) {
        Serial.println(invalida ? "ERR NVS invalida (se usan los valores de fabrica)"
                                : "ERR no hay nada guardado");
        break;
      }
      const char* motivo = "";
      if (!aplicarRadio(g.radio, motivo)) { Serial.printf("ERR %s\n", motivo); break; }
      g_cfg.ttl = g.ttl; g_cfg.relay = g.relay; g_cfg.rol = g.rol;
      g_motor->configurar(g_cfg);
      imprimirRadio();
      Serial.println("OK RADIO cargado de NVS");
      break;
    }

    case Cmd::RADIO_DEFECTO: {
      const char* motivo = "";
      if (!aplicarRadio(paramsDefecto(), motivo)) { Serial.printf("ERR %s\n", motivo); break; }
      imprimirRadio();
      Serial.println("OK RADIO valores de fabrica (no guardado)");
      break;
    }

    /* ---- modo de red -------------------------------------------------- */
    case Cmd::TTL:
      g_cfg.ttl = (uint8_t)o.valor;
      g_motor->configurar(g_cfg);
      Serial.printf("OK TTL %u  (%s)\n", g_cfg.ttl,
                    (g_cfg.ttl == 0 && !g_cfg.relay) ? "E1 punto a punto" : "E2 multi-salto");
      break;

    case Cmd::RELAY:
      g_cfg.relay = o.on;
      g_motor->configurar(g_cfg);
      Serial.printf("OK RELAY %s  (%s)\n", g_cfg.relay ? "ON" : "OFF",
                    (g_cfg.ttl == 0 && !g_cfg.relay) ? "E1 punto a punto" : "E2 multi-salto");
      break;

    case Cmd::ROL:
      g_cfg.rol = o.rol;
      g_motor->configurar(g_cfg);
      Serial.printf("OK ROL %s\n", nombreRol(g_cfg.rol));
      break;

    /* ---- prueba de alcance -------------------------------------------- */
    case Cmd::RANGE: {
      const uint32_t minimo = intervaloMinimoRangeMs(g_cfg.radio);
      uint32_t intervalo = o.ms ? o.ms : minimo;
      if (intervalo < minimo) {
        Serial.printf("OK RANGE intervalo subido a %lu ms (presupuesto de aire, DISENO 7.6)\n",
                      (unsigned long)minimo);
        intervalo = minimo;
      }
      g_rango.iniciar(o.dst, o.n, intervalo, ahora);
      imprimirId(o.dst, idtxt);
      Serial.printf("OK RANGE %s n=%lu cada=%lu ms punto=%u\n", idtxt, (unsigned long)o.n,
                    (unsigned long)intervalo, g_rango.punto());
      Serial.println("R,punto,seq,ok,rtt_ms,rssi_ida,snr_ida,rssi_vuelta,snr_vuelta,hops");
      break;
    }

    case Cmd::RANGE_STOP:
      g_rango.parar();
      Serial.printf("OK RANGE parado  enviados=%lu recibidos=%lu\n",
                    (unsigned long)g_rango.enviados(), (unsigned long)g_rango.recibidos());
      break;

    case Cmd::RANGE_INFORME: {
      ResumenPunto r[16];
      const size_t n = g_rango.resumir(r, 16);
      if (n == 0) { Serial.println("OK RANGE sin medidas"); break; }
      Serial.println("OK RANGE informe por punto:");
      Serial.println("P,punto,enviados,recibidos,perdidas_pct,rssi_ida_min,rssi_ida_med,rssi_ida_max,"
                     "snr_ida_med,rssi_vta_min,rssi_vta_med,rssi_vta_max,snr_vta_med,rtt_medio_ms");
      for (size_t i = 0; i < n; ++i) {
        char si[8], sv[8];
        textoSnr((int8_t)r[i].snrIdaQ4.media, si, sizeof si);
        textoSnr((int8_t)r[i].snrVueltaQ4.media, sv, sizeof sv);
        Serial.printf("P,%u,%u,%u,%u,%ld,%ld,%ld,%s,%ld,%ld,%ld,%s,%lu\n", r[i].punto,
                      r[i].enviados, r[i].recibidos, r[i].perdidasPct, (long)r[i].rssiIda.min,
                      (long)r[i].rssiIda.media, (long)r[i].rssiIda.max, si,
                      (long)r[i].rssiVuelta.min, (long)r[i].rssiVuelta.media,
                      (long)r[i].rssiVuelta.max, sv, (unsigned long)r[i].rttMedioMs);
      }
      break;
    }

    /* ---- depuración ---------------------------------------------------- */
    case Cmd::PERDIDA:
      g_motor->setPerdida((uint8_t)o.valor);
      Serial.printf("OK PERDIDA %u%%  (solo depuracion: se pierde al reiniciar)\n",
                    g_motor->perdida());
      break;
  }
  guardarSiProcede();
}

/* ==========================================================================
 *  6. SUCESOS DEL MOTOR -> consola, LED y OLED
 * ==========================================================================*/
static void imprimirMedida(const Medida& m) {
  char si[8], sv[8];
  textoSnr(m.snrIdaQ4, si, sizeof si);
  textoSnr(m.snrVueltaQ4, sv, sizeof sv);
  if (m.ok) {
    Serial.printf("R,%u,%u,1,%lu,%d,%s,%d,%s,%u\n", m.punto, m.seq, (unsigned long)m.rttMs,
                  m.rssiIda, si, m.rssiVuelta, sv, m.hops);
  } else {
    Serial.printf("R,%u,%u,0,,,,,,\n", m.punto, m.seq);
  }
}

static void procesarEventos(uint32_t ahora) {
  Evento ev;
  while (g_motor->siguienteEvento(ev)) {
    char id[5], s1[8], s2[8];
    imprimirId(ev.nodo, id);

    switch (ev.tipo) {
      case TipoEvento::ENTREGADO:
        Serial.printf("EV DATOS de=%s msgId=%04X hops=%u len=%u texto=\"%s\"\n", id,
                      (unsigned)ev.msgId, ev.hops, ev.len, (const char*)ev.datos);
        led::destellos(2);
        break;

      case TipoEvento::ACK:
        textoSnr(ev.snrIdaQ4, s1, sizeof s1);
        textoSnr(ev.snrVueltaQ4, s2, sizeof s2);
        Serial.printf("EV ACK de=%s msgId=%04X rtt=%lu ms intentos=%u hops_ida=%u "
                      "rssi_ida=%d snr_ida=%s rssi_vta=%d snr_vta=%s\n",
                      id, (unsigned)ev.msgId, (unsigned long)ev.valor, ev.intentos, ev.hopsIda,
                      ev.rssiIda, s1, ev.rssiVuelta, s2);
        g_vista.hayEnlace   = true;
        g_vista.rssiIda     = ev.rssiIda;
        g_vista.snrIdaQ4    = ev.snrIdaQ4;
        g_vista.rssiVuelta  = ev.rssiVuelta;
        g_vista.snrVueltaQ4 = ev.snrVueltaQ4;
        g_vista.rttMs       = ev.valor;
        g_vista.hops        = ev.hopsIda;
        break;

      case TipoEvento::FALLO:
        Serial.printf("EV FALLO para=%s msgId=%04X intentos=%u (sin ACK)\n", id,
                      (unsigned)ev.msgId, ev.intentos);
        break;

      case TipoEvento::REINTENTO:
        Serial.printf("EV REINTENTO msgId=%04X intento=%u espera=%lu ms\n", (unsigned)ev.msgId,
                      (unsigned)(ev.intentos + 1), (unsigned long)ev.valor);
        break;

      case TipoEvento::PONG: {
        textoSnr(ev.snrIdaQ4, s1, sizeof s1);
        textoSnr(ev.snrVueltaQ4, s2, sizeof s2);
        Serial.printf("EV PONG de=%s msgId=%04X rtt=%lu ms hops_ida=%u "
                      "rssi_ida=%d snr_ida=%s rssi_vta=%d snr_vta=%s\n",
                      id, (unsigned)ev.msgId, (unsigned long)ev.valor, ev.hopsIda, ev.rssiIda,
                      s1, ev.rssiVuelta, s2);
        g_vista.hayEnlace   = true;
        g_vista.rssiIda     = ev.rssiIda;
        g_vista.snrIdaQ4    = ev.snrIdaQ4;
        g_vista.rssiVuelta  = ev.rssiVuelta;
        g_vista.snrVueltaQ4 = ev.snrVueltaQ4;
        g_vista.rttMs       = ev.valor;
        g_vista.hops        = ev.hopsIda;
        Medida m;
        if (g_rango.alPong(ev.msgId, ev.valor, ev.rssiIda, ev.snrIdaQ4, ev.rssiVuelta,
                           ev.snrVueltaQ4, ev.hopsIda, m)) {
          imprimirMedida(m);
        }
        led::destellos(2);
        break;
      }

      case TipoEvento::PING_PERDIDO: {
        Serial.printf("EV PING_PERDIDO para=%s msgId=%04X\n", id, (unsigned)ev.msgId);
        Medida m;
        if (g_rango.alPerdido(ev.msgId, m)) imprimirMedida(m);
        break;
      }

      case TipoEvento::PING_FIN:
        Serial.printf("EV PING_FIN respuestas=%lu\n", (unsigned long)ev.n);
        break;

      case TipoEvento::ALERTA: {
        char linea[64];
        lineaPasarela(ev.alerta, ev.alerta.conPosicion(), linea, sizeof linea);
        Serial.printf("EV ALERTA de=%s seq=%u hops=%u %s%s\n", id, ev.alerta.seq, ev.hops, linea,
                      (ev.flags & FLAG_PRUEBA) ? "  PRUEBA" : "");
        g_vista.hayAlerta    = true;
        g_vista.alertaSrc    = ev.nodo;
        g_vista.alerta       = ev.alerta;
        g_vista.alertaPrueba = (ev.flags & FLAG_PRUEBA) != 0;
        g_vista.alertaHaceMs = 0;
        led::alerta(ahora);
        pantalla::irAAlertas(ahora);
        break;
      }

      case TipoEvento::ALERTA_SIN_ACK:
        Serial.printf("EV ALERTA_SIN_ACK msgId=%04X lleva=%lu s insistiendo\n", (unsigned)ev.msgId,
                      (unsigned long)ev.n);
        break;

      case TipoEvento::ECO_BCAST:
        Serial.printf("EV ECO_BCAST msgId=%04X repetido por=%s\n", (unsigned)ev.msgId, id);
        break;

      case TipoEvento::BCAST_SIN_ECO:
        Serial.printf("EV BCAST_SIN_ECO msgId=%04X (nadie lo repitio)\n", (unsigned)ev.msgId);
        break;

      case TipoEvento::ID_DUPLICADO:
        Serial.printf("EV ID_DUPLICADO %s: otra radio usa MI id. Cambialo con ID <hex4>\n", id);
        break;

      case TipoEvento::DESCARTE_COLA:
        Serial.printf("EV DESCARTE_COLA msgId=%04X de=%s\n", (unsigned)ev.msgId, id);
        break;
    }
  }
}

/* ==========================================================================
 *  7. BOTÓN PRG  (§10)
 * ==========================================================================*/
static void atenderGesto(Gesto g, uint32_t ahora) {
  if (g == Gesto::NINGUNO) return;

  if (g == Gesto::DOBLE) {                 // pasar de página, dentro y fuera de RANGE
    pantalla::siguientePagina();
    Serial.println("EV BOTON doble -> pagina siguiente");
    return;
  }

  if (g == Gesto::CORTA) {
    if (g_rango.activo()) {
      const uint16_t p = g_rango.marcarPunto();
      Serial.printf("EV BOTON corta -> punto %u\n", p);
    } else {
      uint16_t dst = DIR_BROADCAST;
      g_motor->ultimoVecino(dst);          // si no hay ninguno, se queda en broadcast
      const int32_t id = g_motor->enviarPing(dst, 0, ahora);
      char idtxt[5];
      imprimirId(dst, idtxt);
      Serial.printf("EV BOTON corta -> PING %s msgId=%04X\n", idtxt, (unsigned)id);
    }
    return;
  }

  /* LARGA: arranca o para la prueba de alcance. */
  if (g_rango.activo()) {
    g_rango.parar();
    Serial.printf("EV BOTON larga -> RANGE parado  enviados=%lu recibidos=%lu\n",
                  (unsigned long)g_rango.enviados(), (unsigned long)g_rango.recibidos());
    return;
  }
  uint16_t dst = 0;
  if (!g_motor->ultimoVecino(dst)) {
    Serial.println("EV BOTON larga -> no hay vecino conocido: pulsacion corta primero (PING *)");
    return;
  }
  const uint32_t intervalo = intervaloMinimoRangeMs(g_cfg.radio);
  g_rango.iniciar(dst, 0, intervalo, ahora);
  char idtxt[5];
  imprimirId(dst, idtxt);
  Serial.printf("EV BOTON larga -> RANGE %s cada %lu ms\n", idtxt, (unsigned long)intervalo);
  Serial.println("R,punto,seq,ok,rtt_ms,rssi_ida,snr_ida,rssi_vuelta,snr_vuelta,hops");
}

/* ==========================================================================
 *  8. setup() / loop()
 * ==========================================================================*/
void setup() {
  Serial.begin(115200);
  uint32_t t0 = millis();
  while (!Serial && (millis() - t0) < 1500) { /* espera corta y opcional */ }
  g_tArranque = millis();

  led::begin();
  ajustes::begin();

  /* --- identidad (§6) --- */
  esp_read_mac(g_mac, ESP_MAC_WIFI_STA);
  const uint16_t idNvs = ajustes::idGuardado();
  g_idDeNvs = (idNvs != 0);
  g_cfg.id  = g_idDeNvs ? idNvs : derivarId(g_mac);

  /* --- configuración guardada (§11) --- */
  ajustes::Guardado g;
  bool invalida = false;
  const bool habia = ajustes::cargar(g, invalida);
  g_cfg.radio = g.radio;                 // cargar() deja los de fábrica si no había
  g_cfg.ttl   = g.ttl;
  g_cfg.relay = g.relay;
  g_cfg.rol   = g.rol;

  /* El msgId inicial se siembra con el azar del hardware: si una placa se
   * reinicia, sus msgId no vuelven a empezar en 1 y la caché de vistos del
   * otro extremo no los confunde con duplicados de antes del reinicio. */
  static Motor motor(g_cfg, azarHw, nullptr, (uint16_t)(esp_random() & 0xFFFF));
  g_motor = &motor;

  pantalla::begin();

  int16_t codigo = 0;
  const bool radioOk = radio::begin(g_cfg.radio, codigo);

  imprimirBanner();
  if (invalida)  Serial.println("EV NVS_INVALIDA: lo guardado no paso la validacion, se usan los valores de fabrica");
  else if (!habia) Serial.println("EV NVS vacia: valores de fabrica");
  if (!radioOk) {
    Serial.printf("ERR RADIO %d: el nodo sigue vivo para la consola, pero NO transmite\n", codigo);
    Serial.println("ERR RADIO causas tipicas: TCXO o pin BUSY mal puestos, o la placa no es una V3");
  } else {
    Serial.println("OK RADIO lista, escuchando");
  }
  imprimirAyuda();

  g_gestos.begin(digitalRead(placa::PIN_BOTON) == LOW, millis());
  memset(&g_vista, 0, sizeof g_vista);
}

void loop() {
  const uint32_t ahora = millis();

  /* 1. ¿llegó algo? */
  if (radio::hayTrama()) {
    uint8_t  trama[TRAMA_MAX];
    int16_t  rssi = 0;
    int8_t   snrQ4 = 0;
    const size_t n = radio::leer(trama, sizeof trama, rssi, snrQ4);
    if (n > 0) {
      g_motor->alRecibir(trama, n, rssi, snrQ4, ahora);
      led::destellos(2);
    }
  }

  /* 2. temporizadores del motor */
  g_motor->tick(ahora);

  /* 3. ¿algo que transmitir? */
  TramaSalida tx;
  if (radio::viva() && g_motor->siguienteTx(ahora, tx)) {
    int16_t codigo = 0;
    const radio::ResultadoTx r = radio::transmitir(tx.datos, tx.len, tx.usarCad, codigo);
    const uint32_t tras = millis();
    if (r == radio::ResultadoTx::OK) {
      g_motor->txHecha(tras, tx.airtimeMs);
      led::destellos(1);
    } else if (r == radio::ResultadoTx::OCUPADO) {
      g_motor->canalOcupado(tras);
    } else {
      g_motor->txFallida(tras);
      Serial.printf("EV TX_ERROR codigo=%d\n", codigo);
    }
  }

  /* 4. sucesos */
  procesarEventos(ahora);

  /* 5. consola */
  static char   linea[CONSOLA_LINEA_MAX + 1];
  static size_t len = 0;
  while (Serial.available()) {
    const char c = (char)Serial.read();
    if (c == '\n' || c == '\r') {
      if (len > 0) {
        linea[len] = '\0';
        Orden o;
        parsearLinea(linea, o);
        ejecutar(o);
        len = 0;
      }
    } else if (len < CONSOLA_LINEA_MAX) {
      linea[len++] = c;
    } else {
      /* Línea más larga que el búfer: se descarta entera para no ejecutar
       * media orden. */
      len = 0;
      Serial.println("ERR linea demasiado larga");
    }
  }

  /* 6. botón */
  atenderGesto(g_gestos.update(ahora, digitalRead(placa::PIN_BOTON) == LOW), ahora);

  /* 7. prueba de alcance: ¿toca otro PING? */
  if (g_rango.activo() && g_rango.tocaPing(ahora)) {
    const int32_t id = g_motor->enviarPing(g_rango.destino(), 0, ahora);
    if (id >= 0) g_rango.pingEnviado((uint16_t)id, ahora);
  }

  /* 8. realimentación */
  led::update(ahora);

  g_vista.id          = g_cfg.id;
  g_vista.radio       = g_cfg.radio;
  g_vista.radioViva   = radio::viva();
  g_vista.ttl         = g_cfg.ttl;
  g_vista.relay       = g_cfg.relay;
  g_vista.rol         = g_cfg.rol;
  g_vista.ocupPorMil  = g_motor->ocupacionPorMil(ahora);
  g_vista.rangoActivo = g_rango.activo();
  g_vista.punto       = g_rango.punto();
  g_vista.enviados    = g_rango.enviados();
  g_vista.recibidos   = g_rango.recibidos();
  const Contadores& c = g_motor->contadores();
  g_vista.tx          = c.tx;
  g_vista.rxValidas   = c.rxValidas;
  g_vista.acksRx      = c.acksRx;
  g_vista.reintentos  = c.reintentos;
  g_vista.fallos      = c.fallos;
  g_vista.duplicados  = c.duplicados;
  g_vista.reenviados  = c.reenviados;
  g_vista.nVecinos    = 0;
  for (int i = 0; i < Motor::N_VECINOS && g_vista.nVecinos < 3; ++i) {
    const Vecino& v = g_motor->vecino(i);
    if (!v.usado) continue;
    g_vista.vecId[g_vista.nVecinos]    = v.id;
    g_vista.vecRssi[g_vista.nVecinos]  = v.rssi;
    g_vista.vecSnrQ4[g_vista.nVecinos] = v.snrQ4;
    ++g_vista.nVecinos;
  }
  pantalla::refrescar(ahora, g_vista);
}
