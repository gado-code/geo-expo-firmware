/* ============================================================================
 *  GEO-EXPO  ·  Estimación de cercanía — implementación
 * ==========================================================================*/
#include "ranging.h"

#include <math.h>

namespace ranging {

const char* zoneName(Zone z) {
  switch (z) {
    case Zone::LOST:     return "SIN SENAL";
    case Zone::VERY_FAR: return "MUY LEJOS";
    case Zone::FAR:      return "LEJOS";
    case Zone::NEAR:     return "CERCA";
    case Zone::HERE:     return "AQUI";
  }
  return "?";
}

uint8_t zoneBars(Zone z) {
  switch (z) {
    case Zone::LOST:     return 0;
    case Zone::VERY_FAR: return 1;
    case Zone::FAR:      return 2;
    case Zone::NEAR:     return 4;
    case Zone::HERE:     return 5;
  }
  return 0;
}

Zone zoneOf(float rssi, const Config& cfg) {
  if (rssi >= cfg.rssiHere) return Zone::HERE;
  if (rssi >= cfg.rssiNear) return Zone::NEAR;
  if (rssi >= cfg.rssiFar)  return Zone::FAR;
  return Zone::VERY_FAR;
}

float distanceM(float rssi, const Config& cfg) {
  /* Modelo log-distancia. Con n <= 0 no hay modelo posible: se devuelve 0
   * en vez de dividir por cero o sacar un infinito que luego se imprima. */
  if (cfg.pathLossExp <= 0.0f) return 0.0f;
  const float exponent = (cfg.rssiAt1m - rssi) / (10.0f * cfg.pathLossExp);
  const float d = powf(10.0f, exponent);
  if (!(d > 0.0f)) return 0.0f;          // NaN incluido
  if (d > 100000.0f) return 100000.0f;   // tope sano: 100 km no es una medida
  return d;
}

uint32_t beepPeriodMs(Zone z) {
  switch (z) {
    case Zone::HERE:     return 150;    // casi continuo: lo tienes encima
    case Zone::NEAR:     return 500;
    case Zone::FAR:      return 1200;
    case Zone::VERY_FAR: return 2500;
    case Zone::LOST:     return 0;      // sin señal no se pita: engañaría
  }
  return 0;
}

/* ==========================================================================
 *  Estimador
 * ==========================================================================*/
void Estimator::reset() {
  m_has   = false;
  m_lost  = false;
  m_rssi  = 0.0f;
  m_raw   = 0.0f;
  m_histN = 0;
  m_histW = 0;
  m_tLast = 0;
  for (uint8_t i = 0; i < TREND_LAG; ++i) m_hist[i] = 0.0f;
}

void Estimator::push(float rssi, uint32_t now) {
  m_raw   = rssi;
  m_tLast = now;

  if (!m_has || m_lost) {
    /* Primera trama (o la primera después de perderlo): el filtro arranca en
     * el valor medido. Si no, al recuperar la señal el buscador tardaría
     * varias tramas en dejar de decir que está lejísimos. */
    m_rssi  = rssi;
    m_has   = true;
    m_lost  = false;
    m_histN = 0;
    m_histW = 0;
  } else {
    float a = m_cfg.alpha;
    if (a < 0.0f) a = 0.0f;
    if (a > 1.0f) a = 1.0f;
    m_rssi = m_rssi + a * (rssi - m_rssi);
  }

  // Memoria circular para la tendencia.
  m_hist[m_histW] = m_rssi;
  m_histW = (uint8_t)((m_histW + 1) % TREND_LAG);
  if (m_histN < TREND_LAG) ++m_histN;
}

void Estimator::update(uint32_t now) {
  if (m_has && !m_lost && (now - m_tLast) >= m_cfg.lostAfterMs) m_lost = true;
}

float Estimator::distanceM() const {
  if (!m_has || m_lost) return 0.0f;
  return ranging::distanceM(m_rssi, m_cfg);
}

Zone Estimator::zone() const {
  if (!m_has || m_lost) return Zone::LOST;
  return zoneOf(m_rssi, m_cfg);
}

int8_t Estimator::trend() const {
  if (!m_has || m_lost || m_histN < 2) return 0;

  // El más antiguo de los que hay guardados frente al más reciente.
  const uint8_t newest = (uint8_t)((m_histW + TREND_LAG - 1) % TREND_LAG);
  const uint8_t oldest = (m_histN < TREND_LAG)
                             ? (uint8_t)((m_histW + TREND_LAG - m_histN) % TREND_LAG)
                             : m_histW;
  const float delta = m_hist[newest] - m_hist[oldest];

  /* Umbral de 1,5 dB: por debajo de eso es ruido del propio canal y la flecha
   * se pondría a bailar sola delante del jurado. */
  if (delta >  1.5f) return  1;
  if (delta < -1.5f) return -1;
  return 0;
}

}  // namespace ranging
