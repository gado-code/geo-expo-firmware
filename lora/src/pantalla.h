/* ============================================================================
 *  GEO-EXPO LoRa  ·  OLED SSD1306 con U8g2   (DISENO §10)
 * ============================================================================
 *  Cuatro páginas, que se pasan con la DOBLE pulsación del botón PRG:
 *      1 ESTADO   ID, preset, frecuencia, potencia, TTL/RELAY, % de aire
 *      2 ENLACE   RSSI/SNR de ida y vuelta, RTT, punto y recibidos/enviados
 *      3 TRAFICO  contadores y vecinos
 *      4 ALERTAS  última ALERT recibida (se salta aquí sola al recibir una)
 *
 *  Con -D GEOLORA_OLED=0 todas estas funciones son cuerpos vacíos: la placa
 *  funciona igual, sin pantalla y sin enlazar U8g2.
 * ==========================================================================*/
#pragma once

#include <stdint.h>

#include "carga.h"
#include "motor.h"
#include "radio_params.h"

namespace pantalla {

/* Todo lo que se pinta, copiado por main.cpp una vez por refresco: la
 * pantalla no consulta al motor por su cuenta. */
struct Estado {
  uint16_t             id;
  geolora::ParamsRadio radio;
  bool                 radioViva;
  uint8_t              ttl;
  bool                 relay;
  geolora::Rol         rol;
  uint32_t             ocupPorMil;

  /* Enlace (último PONG o ACK con medidas de los dos lados). */
  bool     hayEnlace;
  int16_t  rssiIda, rssiVuelta;
  int8_t   snrIdaQ4, snrVueltaQ4;
  uint32_t rttMs;
  uint8_t  hops;

  /* Prueba de alcance. */
  bool     rangoActivo;
  uint16_t punto;
  uint32_t enviados, recibidos;

  /* Tráfico. */
  uint32_t tx, rxValidas, acksRx, reintentos, fallos, duplicados, reenviados;

  /* Vecinos (los 3 más recientes ya ordenados por main.cpp). */
  uint8_t  nVecinos;
  uint16_t vecId[3];
  int16_t  vecRssi[3];
  int8_t   vecSnrQ4[3];

  /* Última alerta recibida. */
  bool                hayAlerta;
  uint16_t            alertaSrc;
  geolora::CargaAlerta alerta;
  bool                alertaPrueba;
  uint32_t            alertaHaceMs;
};

#if GEOLORA_OLED

/* Enciende Vext, espera y arranca el panel. Devuelve false si no responde. */
bool begin();
bool disponible();
void siguientePagina();
void irAAlertas(uint32_t ahoraMs);   // salta a la página 4 y se queda 10 s
/* Repinta como mucho a 4 Hz; las llamadas de más no cuestan nada. */
void refrescar(uint32_t ahoraMs, const Estado& e);

#else

inline bool begin()                                   { return false; }
inline bool disponible()                              { return false; }
inline void siguientePagina()                         {}
inline void irAAlertas(uint32_t)                      {}
inline void refrescar(uint32_t, const Estado&)        {}

#endif

}  // namespace pantalla
