/* ============================================================================
 *  GEO-EXPO ALERT  ·  Máquina de estados del botón de pánico
 * ============================================================================
 *  Igual que lib/nmea, este módulo es DELIBERADAMENTE independiente de
 *  Arduino: no incluye <Arduino.h>, no llama a millis() ni a digitalRead(),
 *  no toca el LED y no imprime nada. Sólo recibe (tiempo, ¿botón pulsado?) y
 *  devuelve QUÉ ha pasado. Quien lo usa decide si eso se convierte en un
 *  NOTIFY por BLE, en un parpadeo o en una línea del monitor serie.
 *
 *  Se separó así para poder probar la lógica del botón EN EL PC:
 *
 *      pio test -e native
 *
 *  que es la única forma de cazar los fallos de temporización (el umbral de
 *  los 3 s, los rebotes, la ventana de cancelación) cuando no se tiene la
 *  placa delante. Los tiempos son parámetros, así que un test puede simular
 *  una pulsación de 2999 ms sin esperar 3 segundos de verdad.
 *
 *  Comportamiento (idéntico al que ya estaba validado en la placa):
 *
 *      IDLE -> DEBOUNCE -> PRESSED -> CANCEL_WINDOW -> IDLE
 *
 *   · Pulsación corta : soltar antes de 3 s -> ALERT:1 + ventana de cancelación
 *   · Pulsación larga : mantener 3 s        -> ALERT:2 EN EL INSTANTE de los 3 s
 *   · Ventana (10 s)  : una pulsación nueva -> CANCEL y vuelta a IDLE
 *   · Tras un CANCEL  : no se arma otra pulsación hasta SOLTAR el botón
 *   · Ventana expirada: vuelta a IDLE, la alerta queda CONFIRMADA
 *   · Antirrebote de 50 ms en el flanco de bajada Y en el de subida
 * ==========================================================================*/
#pragma once

#include <stdint.h>

namespace panic {

/* --------------------------------------------------------------------------
 *  Estados de la máquina.
 * ------------------------------------------------------------------------*/
enum class State : uint8_t { IDLE, DEBOUNCE, PRESSED, CANCEL_WINDOW };

/* --------------------------------------------------------------------------
 *  Lo que puede ocurrir en un tick. Como mucho UNO por llamada a update().
 * ------------------------------------------------------------------------*/
enum class Event : uint8_t {
  NONE,            // no ha pasado nada reseñable
  ALERT_SHORT,     // -> emitir "ALERT:1"
  ALERT_LONG,      // -> emitir "ALERT:2"
  CANCEL,          // -> emitir "CANCEL"
  CANCEL_ARMED,    // botón liberado dentro de la ventana: ya se puede cancelar
  WINDOW_EXPIRED,  // la ventana se agotó: la alerta queda CONFIRMADA
  BOUNCE           // rebote/ruido descartado por el antirrebote
};

/* --------------------------------------------------------------------------
 *  Tiempos. Van como parámetros para que las pruebas puedan acortarlos.
 * ------------------------------------------------------------------------*/
struct Config {
  uint32_t debounceMs;
  uint32_t longPressMs;
  uint32_t cancelWindowMs;

  /* Constructor explícito (y no inicializadores de miembro) para que se pueda
   * construir con Config{a,b,c} también en C++11, que es el estándar con el
   * que compila arduino-esp32 2.0.x. */
  Config(uint32_t debounce = 50, uint32_t longPress = 3000, uint32_t window = 10000)
      : debounceMs(debounce), longPressMs(longPress), cancelWindowMs(window) {}
};

/* --------------------------------------------------------------------------
 *  Resultado de un tick: el evento y, si lo hubo, el cambio de estado con su
 *  motivo (que es justo lo que la placa escribe en el monitor serie).
 * ------------------------------------------------------------------------*/
struct Step {
  Event       event   = Event::NONE;
  bool        changed = false;      // hubo transición de estado
  State       from    = State::IDLE;
  State       to      = State::IDLE;
  const char* reason  = nullptr;    // motivo de la transición (texto de traza)
};

class Fsm {
 public:
  explicit Fsm(const Config& cfg = Config()) : m_cfg(cfg) { begin(false, 0); }

  /* Arranque. `pressedNow` es el nivel real del botón en ese instante: si la
   * placa arranca con BOOT pulsado, eso NO cuenta como una pulsación nueva. */
  void begin(bool pressedNow, uint32_t now);

  /* Un tick. `now` en milisegundos (puede desbordar: toda la aritmética es
   * con restas sin signo). `pressed` = true cuando el botón está pulsado. */
  Step update(uint32_t now, bool pressed);

  State state() const { return m_state; }

  /* Nombre del estado, para las trazas. */
  static const char* stateName(State s);

  /* Token del contrato BLE que corresponde al evento, o nullptr si el evento
   * no se transmite ("CANCEL_ARMED", "BOUNCE", ...). */
  static const char* txToken(Event e);

 private:
  Step enterIdle(Step st, bool pressed, const char* reason);

  Config   m_cfg;
  State    m_state           = State::IDLE;
  uint32_t m_tDebounce       = 0;   // entrada a DEBOUNCE
  uint32_t m_tPressed        = 0;   // pulsación confirmada (para contar los 3 s)
  uint32_t m_tCancelStart    = 0;   // inicio de la ventana de cancelación
  uint32_t m_tRawChange      = 0;   // último cambio del nivel crudo
  bool     m_lastRaw         = false;
  bool     m_idlePrevPressed = false;  // nivel previo en IDLE (exige flanco real)
  bool     m_longFired       = false;
  bool     m_cancelArmed     = false;
};

}  // namespace panic
