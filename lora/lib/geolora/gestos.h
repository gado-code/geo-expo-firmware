/* ============================================================================
 *  GEO-EXPO LoRa  ·  Gestos del botón PRG: corta / doble / larga (DISENO §10)
 * ============================================================================
 *  Misma filosofía que lib/panic del llavero (antirrebote de 50 ms en los dos
 *  flancos, tiempos como parámetros, nada de Arduino), pero REIMPLEMENTADO
 *  aquí dentro: el subproyecto lora/ no depende del firmware del llavero.
 *
 *    · Corta : pulsar y soltar antes de 1.5 s, y NO volver a pulsar en 400 ms
 *              -> se emite al cerrarse la ventana de doble pulsación.
 *    · Doble : segunda pulsación dentro de los 400 ms -> se emite EN EL
 *              FLANCO de la segunda pulsación.
 *    · Larga : mantener 1.5 s -> se emite EN EL INSTANTE de los 1.5 s (no
 *              al soltar), para que en el campo se note sin mirar.
 *    · Tras una larga o una doble, nada hasta SOLTAR el botón.
 *    · Si el botón ya está pulsado al arrancar (GPIO0 es pin de arranque,
 *      o el DTR del monitor lo baja) no cuenta hasta que se suelte.
 *
 *  Uso en la placa (una vez por vuelta de loop, sin delay):
 *      Gesto g = gestos.update(millis(), digitalRead(0) == LOW);
 * ==========================================================================*/
#pragma once

#include <stdint.h>

namespace geolora {

enum class Gesto : uint8_t { NINGUNO, CORTA, DOBLE, LARGA };
const char* nombreGesto(Gesto g);

struct ConfigGestos {
  uint32_t antirreboteMs;
  uint32_t largaMs;
  uint32_t dobleMs;
  ConfigGestos(uint32_t antirrebote = 50, uint32_t larga = 1500, uint32_t doble = 400)
      : antirreboteMs(antirrebote), largaMs(larga), dobleMs(doble) {}
};

class Gestos {
 public:
  explicit Gestos(const ConfigGestos& cfg = ConfigGestos());
  void  begin(bool pulsado, uint32_t ahoraMs);
  Gesto update(uint32_t ahoraMs, bool pulsado);
  bool  pulsadoFiltrado() const { return estable_; }
  uint32_t rebotes() const { return rebotes_; }

 private:
  enum class Estado : uint8_t { REPOSO, PULSADO1, ESPERA2, SOLTAR };
  ConfigGestos cfg_;
  Estado   estado_;
  bool     crudo_;
  uint32_t tCrudo_;     // desde cuándo el nivel crudo es el actual
  bool     estable_;    // nivel filtrado
  uint32_t tFlanco_;    // instante del último flanco filtrado
  uint32_t rebotes_;
};

}  // namespace geolora
