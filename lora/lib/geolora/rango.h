/* ============================================================================
 *  GEO-EXPO LoRa  ·  Prueba de alcance RANGE (DISENO §9, §10, §13.3)
 * ============================================================================
 *  Lógica pura de la prueba de campo: cuándo toca el siguiente PING, a qué
 *  número de "punto" (waypoint) pertenece cada medida, el anillo de 512
 *  medidas en RAM y el resumen POR PUNTO de "RANGE INFORME".
 *
 *  El motor hace el PING y avisa con PONG / PING_PERDIDO; main.cpp pasa esos
 *  sucesos aquí (alPong / alPerdido) e imprime la línea CSV de cada medida:
 *
 *      R,punto,seq,ok,rtt_ms,rssi_ida,snr_ida,rssi_vuelta,snr_vuelta,hops
 *
 *  (ida = medida en el OTRO extremo, viene en el PONG; vuelta = medida aquí;
 *  SNR en dB con un decimal.)
 *
 *  La RAM se pierde al reiniciar: guardarlo en LittleFS queda para E1.x.
 * ==========================================================================*/
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace geolora {

struct Medida {
  uint16_t punto;
  uint16_t seq;
  bool     ok;
  uint32_t rttMs;
  int16_t  rssiIda;
  int8_t   snrIdaQ4;
  int16_t  rssiVuelta;
  int8_t   snrVueltaQ4;
  uint8_t  hops;
};

struct EstadisticaQ {        // mínimo / media / máximo de una magnitud
  int32_t min;
  int32_t media;             // redondeada
  int32_t max;
};

struct ResumenPunto {
  uint16_t     punto;
  uint16_t     enviados;
  uint16_t     recibidos;
  uint8_t      perdidasPct;  // redondeado
  EstadisticaQ rssiIda;      // dBm
  EstadisticaQ snrIdaQ4;     // cuartos de dB
  EstadisticaQ rssiVuelta;
  EstadisticaQ snrVueltaQ4;
  uint32_t     rttMedioMs;
};

class Rango {
 public:
  static const uint16_t CAPACIDAD     = 512;
  static const int      N_EN_CURSO    = 4;

  Rango();

  /* n = 0 -> sin fin. El intervalo lo decide quien llama (con el mínimo de
   * intervaloMinimoRangeMs()). El primer PING sale en el acto. */
  void     iniciar(uint16_t dst, uint32_t n, uint32_t intervaloMs, uint32_t ahoraMs);
  void     parar() { activo_ = false; }
  bool     activo() const { return activo_; }
  uint16_t destino() const { return dst_; }
  uint32_t intervalo() const { return intervaloMs_; }

  /* Pulsación corta en RANGE: las medidas siguientes van al punto nuevo. */
  uint16_t marcarPunto() { return ++punto_; }
  uint16_t punto() const { return punto_; }

  /* ¿Toca mandar otro PING? Si sí, quien llama lo manda y avisa con
   * pingEnviado(). Cuando se han mandado los n pedidos y no queda ninguno
   * en curso, la prueba se da por terminada (activo() pasa a false). */
  bool tocaPing(uint32_t ahoraMs);
  void pingEnviado(uint16_t msgId, uint32_t ahoraMs);

  /* Resultado de un PING. Devuelven true (y la medida registrada) sólo si el
   * msgId era uno de los PING de esta prueba. */
  bool alPong(uint16_t msgIdPing, uint32_t rttMs, int16_t rssiIda, int8_t snrIdaQ4, int16_t rssiVuelta,
              int8_t snrVueltaQ4, uint8_t hops, Medida& out);
  bool alPerdido(uint16_t msgIdPing, Medida& out);

  /* Racha actual (desde iniciar()). */
  uint32_t enviados() const { return enviados_; }
  uint32_t recibidos() const { return recibidos_; }
  bool     hayUltima() const { return hayUltima_; }
  const Medida& ultima() const { return ultima_; }

  /* Anillo de medidas (de la más antigua a la más reciente). */
  size_t        numMedidas() const { return n_; }
  const Medida& medida(size_t i) const { return m_[(ini_ + i) % CAPACIDAD]; }
  void          borrarMedidas() { n_ = 0; ini_ = 0; }

  /* Resumen por punto en orden de aparición. Devuelve cuántos puntos. */
  size_t resumir(ResumenPunto* out, size_t max) const;

 private:
  void registrar(const Medida& m);
  int  buscarEnCurso(uint16_t msgId) const;
  void comprobarFin();

  bool     activo_;
  uint16_t dst_;
  uint32_t nPedidos_;
  uint32_t intervaloMs_;
  uint32_t tProximo_;
  uint16_t punto_;
  uint16_t seq_;
  uint32_t enviados_;
  uint32_t recibidos_;

  struct EnCurso { bool usado; uint16_t msgId; uint16_t seq; uint16_t punto; };
  EnCurso  curso_[N_EN_CURSO];

  Medida   m_[CAPACIDAD];
  size_t   ini_;
  size_t   n_;
  bool     hayUltima_;
  Medida   ultima_;
};

}  // namespace geolora
