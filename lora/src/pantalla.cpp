/* ============================================================================
 *  GEO-EXPO LoRa  ·  OLED SSD1306 (implementación)   (DISENO §10)
 * ==========================================================================*/
#include "pantalla.h"

#if GEOLORA_OLED

#include <Arduino.h>
#include <U8g2lib.h>
#include <Wire.h>

#include "identidad.h"
#include "placa.h"

namespace pantalla {
namespace {

/* El OLED de la V3 NO va en los SDA/SCL por defecto de la variante (41/42):
 * va en 17/18 con reset propio en 21 (placa.h). */
U8G2_SSD1306_128X64_NONAME_F_HW_I2C g_oled(U8G2_R0, placa::PIN_OLED_RST,
                                           placa::PIN_OLED_SCL, placa::PIN_OLED_SDA);

bool     g_ok       = false;
uint8_t  g_pagina   = 0;        // 0..3
uint32_t g_tUltimo  = 0;        // último repintado
uint32_t g_tVolver  = 0;        // cuándo dejar de forzar la página de alertas
bool     g_forzada  = false;

const uint32_t PERIODO_MS = 250;   // 4 Hz (§10)
const uint32_t ALERTA_MS  = 10000;

void snr(char* out, size_t cap, int8_t q4) {
  const int32_t d = (int32_t)q4 * 10 / 4;      // décimas de dB
  snprintf(out, cap, "%ld.%ld", (long)(d / 10), (long)(d < 0 ? -(d % 10) : d % 10));
}

void cabecera(const Estado& e, const char* titulo) {
  char id[5];
  geolora::formatearId(e.id, id);
  g_oled.setFont(u8g2_font_5x8_tr);
  g_oled.drawStr(0, 7, id);
  g_oled.drawStr(28, 7, titulo);
  g_oled.drawHLine(0, 9, 128);
}

void paginaEstado(const Estado& e) {
  char linea[32], mhz[16];
  cabecera(e, "ESTADO");
  geolora::formatearMHz(e.radio.freqKHz, mhz, sizeof mhz);
  snprintf(linea, sizeof linea, "%s %s", geolora::nombrePreset(geolora::presetDe(e.radio)), mhz);
  g_oled.drawStr(0, 20, linea);
  snprintf(linea, sizeof linea, "SF%u BW%u CR4/%u", e.radio.sf, e.radio.bwKHz, e.radio.cr);
  g_oled.drawStr(0, 30, linea);
  snprintf(linea, sizeof linea, "PWR %d dBm  sync %02X", e.radio.pwr, e.radio.sync);
  g_oled.drawStr(0, 40, linea);
  snprintf(linea, sizeof linea, "TTL %u  RELAY %s  %s", e.ttl, e.relay ? "ON" : "OFF",
           geolora::nombreRol(e.rol));
  g_oled.drawStr(0, 50, linea);
  snprintf(linea, sizeof linea, "aire %lu.%lu%%%s", (unsigned long)(e.ocupPorMil / 10),
           (unsigned long)(e.ocupPorMil % 10), e.radioViva ? "" : "  RADIO KO");
  g_oled.drawStr(0, 60, linea);
}

void paginaEnlace(const Estado& e) {
  char linea[32], s1[8], s2[8];
  cabecera(e, "ENLACE");
  if (!e.hayEnlace) {
    g_oled.drawStr(0, 24, "sin medidas todavia");
    g_oled.drawStr(0, 34, "PING o RANGE para medir");
  } else {
    snr(s1, sizeof s1, e.snrIdaQ4);
    snr(s2, sizeof s2, e.snrVueltaQ4);
    snprintf(linea, sizeof linea, "ida  %d dBm %s dB", e.rssiIda, s1);
    g_oled.drawStr(0, 20, linea);
    snprintf(linea, sizeof linea, "vta  %d dBm %s dB", e.rssiVuelta, s2);
    g_oled.drawStr(0, 30, linea);
    snprintf(linea, sizeof linea, "RTT %lu ms  hops %u", (unsigned long)e.rttMs, e.hops);
    g_oled.drawStr(0, 40, linea);
  }
  const uint32_t perdidas =
      e.enviados ? (100UL * (e.enviados - e.recibidos) + e.enviados / 2) / e.enviados : 0;
  snprintf(linea, sizeof linea, "punto %u  %lu/%lu  %lu%%", e.punto, (unsigned long)e.recibidos,
           (unsigned long)e.enviados, (unsigned long)perdidas);
  g_oled.drawStr(0, 52, linea);
  g_oled.drawStr(0, 62, e.rangoActivo ? "RANGE en marcha" : "RANGE parado");
}

void paginaTrafico(const Estado& e) {
  char linea[32];
  cabecera(e, "TRAFICO");
  snprintf(linea, sizeof linea, "tx %lu  rx %lu", (unsigned long)e.tx, (unsigned long)e.rxValidas);
  g_oled.drawStr(0, 19, linea);
  snprintf(linea, sizeof linea, "ack %lu  rei %lu  fal %lu", (unsigned long)e.acksRx,
           (unsigned long)e.reintentos, (unsigned long)e.fallos);
  g_oled.drawStr(0, 28, linea);
  snprintf(linea, sizeof linea, "dup %lu  reenv %lu", (unsigned long)e.duplicados,
           (unsigned long)e.reenviados);
  g_oled.drawStr(0, 37, linea);
  g_oled.drawStr(0, 47, "vecinos:");
  int y = 56;
  for (uint8_t i = 0; i < e.nVecinos && i < 3; ++i) {
    char id[5], s[8];
    geolora::formatearId(e.vecId[i], id);
    snr(s, sizeof s, e.vecSnrQ4[i]);
    snprintf(linea, sizeof linea, "%s %d dBm %s dB", id, e.vecRssi[i], s);
    g_oled.drawStr(0, y, linea);
    y += 8;
  }
  if (e.nVecinos == 0) g_oled.drawStr(0, 56, "(ninguno)");
}

void paginaAlertas(const Estado& e) {
  char linea[32];
  cabecera(e, "ALERTAS");
  if (!e.hayAlerta) {
    g_oled.drawStr(0, 26, "ninguna recibida");
    return;
  }
  char id[5];
  geolora::formatearId(e.alertaSrc, id);
  const char* tok = geolora::tokenAlerta(e.alerta.codigo);
  g_oled.setFont(u8g2_font_7x13B_tr);
  snprintf(linea, sizeof linea, "%s", tok ? tok : "?");
  g_oled.drawStr(0, 24, linea);
  g_oled.setFont(u8g2_font_5x8_tr);
  snprintf(linea, sizeof linea, "de %s  seq %u%s", id, e.alerta.seq,
           e.alertaPrueba ? "  PRUEBA" : "");
  g_oled.drawStr(0, 36, linea);
  if (e.alerta.conPosicion()) {
    char lat[16], lon[16];
    geolora::formatearGrados(e.alerta.lat1e7, 5, lat, sizeof lat);
    geolora::formatearGrados(e.alerta.lon1e7, 5, lon, sizeof lon);
    snprintf(linea, sizeof linea, "%s %s", lat, lon);
    g_oled.drawStr(0, 46, linea);
  } else {
    g_oled.drawStr(0, 46, "sin posicion");
  }
  snprintf(linea, sizeof linea, "hace %lu s", (unsigned long)(e.alertaHaceMs / 1000));
  g_oled.drawStr(0, 58, linea);
}

}  // namespace

bool begin() {
  /* Vext alimenta el panel y va al revés de lo que parece: LOW = encendido. */
  pinMode(placa::PIN_VEXT, OUTPUT);
  digitalWrite(placa::PIN_VEXT, LOW);
  delay(50);
  g_ok = g_oled.begin();
  if (g_ok) {
    g_oled.setFont(u8g2_font_5x8_tr);
    g_oled.clearBuffer();
    g_oled.drawStr(0, 20, "GEO-EXPO LoRa");
    g_oled.drawStr(0, 32, "arrancando...");
    g_oled.sendBuffer();
  }
  return g_ok;
}

bool disponible() { return g_ok; }

void siguientePagina() {
  g_pagina = (uint8_t)((g_pagina + 1) % 4);
  g_forzada = false;          // pasar de página a mano manda sobre el salto automático
  g_tUltimo = 0;              // repinta en la vuelta siguiente
}

void irAAlertas(uint32_t ahoraMs) {
  g_pagina  = 3;
  g_forzada = true;
  g_tVolver = ahoraMs + ALERTA_MS;
  g_tUltimo = 0;
}

void refrescar(uint32_t ahoraMs, const Estado& e) {
  if (!g_ok) return;
  if (g_tUltimo != 0 && (uint32_t)(ahoraMs - g_tUltimo) < PERIODO_MS) return;
  g_tUltimo = ahoraMs ? ahoraMs : 1;

  if (g_forzada && (int32_t)(ahoraMs - g_tVolver) >= 0) g_forzada = false;

  g_oled.clearBuffer();
  switch (g_pagina) {
    case 0:  paginaEstado(e);   break;
    case 1:  paginaEnlace(e);   break;
    case 2:  paginaTrafico(e);  break;
    default: paginaAlertas(e);  break;
  }
  g_oled.sendBuffer();
}

}  // namespace pantalla

#endif  // GEOLORA_OLED
