/* ============================================================================
 *  GEO-EXPO  ·  Estimación de cercanía a partir del RSSI
 * ============================================================================
 *  Es la parte "caliente/frío" del buscador: el AirTag no sabe la distancia
 *  exacta a la que está, sabe si te estás acercando. Aquí se hace lo mismo con
 *  la potencia con la que llega cada trama LoRa (RSSI, en dBm):
 *
 *      RSSI crudo -> suavizado -> zona (AQUI / CERCA / LEJOS / MUY_LEJOS)
 *                              -> cadencia de pitido
 *                              -> distancia ESTIMADA en metros
 *
 *  ⚠️ La distancia en metros es ORIENTATIVA y hay que presentarla así en la
 *  expo. El RSSI depende de la antena, de si el llavero está en un bolsillo,
 *  de las paredes y hasta de cómo se sujeta la placa. Sirve para "está a unos
 *  pocos metros" o "está lejos", no para medir. La zona es mucho más fiable
 *  que el número, y por eso es lo que manda en el pitido.
 *
 *  Sin Arduino: se prueba entero en el PC con `pio test -e native`.
 * ==========================================================================*/
#pragma once

#include <stdint.h>

namespace ranging {

/* --------------------------------------------------------------------------
 *  Zonas de cercanía. Los umbrales son de RSSI YA SUAVIZADO.
 * ------------------------------------------------------------------------*/
enum class Zone : uint8_t {
  LOST,       // hace demasiado que no se oye nada
  VERY_FAR,   // se oye, pero muy flojo
  FAR,
  NEAR,
  HERE        // lo tienes encima
};

const char* zoneName(Zone z);

/* Barras de 0 a 5 para pintar en la pantalla o en el monitor serie. */
uint8_t zoneBars(Zone z);

/* --------------------------------------------------------------------------
 *  Parámetros del modelo. Se dejan como datos (y no como #define) para poder
 *  calibrarlos el día de la expo sin recompilar la lógica.
 * ------------------------------------------------------------------------*/
struct Config {
  float rssiHere;      // por encima de esto: lo tienes al lado
  float rssiNear;
  float rssiFar;       // por debajo de esto: muy lejos
  float rssiAt1m;      // RSSI medido a 1 m (referencia del modelo de distancia)
  float pathLossExp;   // exponente de pérdidas: 2,0 campo abierto; 2,7-3,5 interiores
  float alpha;         // suavizado exponencial: 0 = no se mueve, 1 = sin filtro
  uint32_t lostAfterMs;// sin tramas durante esto -> LOST

  /* OJO con `lostAfterMs`: tiene que ser MAYOR que dos o tres balizas
   * seguidas del otro lado. Con balizas cada 15 s y un plazo de 20 s bastaba
   * con perder UNA trama —cosa rutinaria en radio— para declarar "SIN SEÑAL",
   * apagar los pitidos y volver a la vida a la trama siguiente: en la expo se
   * habría visto como un parpadeo constante entre "CERCA" y "SIN SEÑAL". */
  Config(float here = -55.0f, float near_ = -85.0f, float far = -110.0f,
         float at1m = -45.0f, float n = 2.7f, float a = 0.35f,
         uint32_t lost = 45000)
      : rssiHere(here), rssiNear(near_), rssiFar(far), rssiAt1m(at1m),
        pathLossExp(n), alpha(a), lostAfterMs(lost) {}
};

/* --------------------------------------------------------------------------
 *  Funciones puras (útiles sueltas y fáciles de probar).
 * ------------------------------------------------------------------------*/

/* Zona que corresponde a un RSSI, sin tener en cuenta el tiempo. */
Zone zoneOf(float rssi, const Config& cfg = Config());

/* Distancia estimada en metros con el modelo log-distancia:
 *      d = 10 ^ ((RSSI@1m - RSSI) / (10 * n))
 * Devuelve un número siempre >= 0. Ver el aviso de la cabecera. */
float distanceM(float rssi, const Config& cfg = Config());

/* Cada cuánto debe pitar el buscador en esa zona (ms). 0 = no pitar. */
uint32_t beepPeriodMs(Zone z);

/* --------------------------------------------------------------------------
 *  Estimador con memoria: suaviza el RSSI y vigila el silencio.
 * ------------------------------------------------------------------------*/
class Estimator {
 public:
  explicit Estimator(const Config& cfg = Config()) : m_cfg(cfg) {}

  void reset();

  /* Una trama recibida: `rssi` en dBm, `now` en ms. */
  void push(float rssi, uint32_t now);

  /* Hay que llamarlo aunque no llegue nada: es lo que detecta el silencio. */
  void update(uint32_t now);

  bool  hasSignal() const { return m_has; }
  float rssi()      const { return m_rssi; }        // suavizado
  float rawRssi()   const { return m_raw; }         // última medida cruda
  float distanceM() const;
  Zone  zone()      const;
  uint32_t silenceMs(uint32_t now) const { return m_has ? (now - m_tLast) : 0; }

  /* ¿Nos estamos acercando? Compara el RSSI suavizado con el de hace unas
   * cuantas tramas; devuelve +1 acercándose, -1 alejándose, 0 igual. */
  int8_t trend() const;

  const Config& config() const { return m_cfg; }
  void setConfig(const Config& cfg) { m_cfg = cfg; }

 private:
  static const uint8_t TREND_LAG = 4;   // tramas de memoria para la tendencia

  Config   m_cfg;
  bool     m_has   = false;
  bool     m_lost  = false;
  float    m_rssi  = 0.0f;
  float    m_raw   = 0.0f;
  float    m_hist[TREND_LAG] = {0, 0, 0, 0};
  uint8_t  m_histN = 0;
  uint8_t  m_histW = 0;
  uint32_t m_tLast = 0;
};

}  // namespace ranging
