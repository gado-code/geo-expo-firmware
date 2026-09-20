/* ============================================================================
 *  GEO-EXPO LoRa  ·  Gestos del botón PRG  (ver gestos.h)
 * ==========================================================================*/
#include "gestos.h"

namespace geolora {

const char* nombreGesto(Gesto g) {
  switch (g) {
    case Gesto::NINGUNO: return "NINGUNO";
    case Gesto::CORTA:   return "CORTA";
    case Gesto::DOBLE:   return "DOBLE";
    case Gesto::LARGA:   return "LARGA";
    default:             return "?";
  }
}

Gestos::Gestos(const ConfigGestos& cfg)
    : cfg_(cfg), estado_(Estado::REPOSO), crudo_(false), tCrudo_(0), estable_(false), tFlanco_(0), rebotes_(0) {}

void Gestos::begin(bool pulsado, uint32_t ahoraMs) {
  crudo_ = pulsado;
  estable_ = pulsado;
  tCrudo_ = ahoraMs;
  tFlanco_ = ahoraMs;
  rebotes_ = 0;
  estado_ = pulsado ? Estado::SOLTAR : Estado::REPOSO;
}

Gesto Gestos::update(uint32_t ahoraMs, bool pulsado) {
  /* 1) Antirrebote: el nivel filtrado sólo cambia cuando el crudo lleva
   *    antirreboteMs sin moverse. Un cambio crudo que se deshace antes es un
   *    rebote (se cuenta, para depurar). */
  if (pulsado != crudo_) {
    if (crudo_ != estable_) ++rebotes_;   // se deshace un cambio sin confirmar
    crudo_ = pulsado;
    tCrudo_ = ahoraMs;
  }
  bool flancoBaja = false;    // se acaba de pulsar
  bool flancoSube = false;    // se acaba de soltar
  if (crudo_ != estable_ && (ahoraMs - tCrudo_) >= cfg_.antirreboteMs) {
    estable_ = crudo_;
    flancoBaja = estable_;
    flancoSube = !estable_;
  }

  /* 2) Gestos sobre el nivel filtrado. */
  switch (estado_) {
    case Estado::REPOSO:
      if (flancoBaja) { estado_ = Estado::PULSADO1; tFlanco_ = ahoraMs; }
      break;
    case Estado::PULSADO1:
      if (flancoSube) {
        estado_ = Estado::ESPERA2;
        tFlanco_ = ahoraMs;
      } else if ((ahoraMs - tFlanco_) >= cfg_.largaMs) {
        estado_ = Estado::SOLTAR;
        return Gesto::LARGA;
      }
      break;
    case Estado::ESPERA2:
      if (flancoBaja) {
        estado_ = Estado::SOLTAR;
        return Gesto::DOBLE;
      }
      if ((ahoraMs - tFlanco_) >= cfg_.dobleMs) {
        estado_ = Estado::REPOSO;
        return Gesto::CORTA;
      }
      break;
    case Estado::SOLTAR:
      if (flancoSube) estado_ = Estado::REPOSO;
      break;
  }
  return Gesto::NINGUNO;
}

}  // namespace geolora
