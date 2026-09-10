/* ============================================================================
 *  GEO-EXPO ALERT  ·  Máquina de estados del botón de pánico — implementación
 * ==========================================================================*/
#include "panic.h"

namespace panic {

const char* Fsm::stateName(State s) {
  switch (s) {
    case State::IDLE:          return "IDLE";
    case State::DEBOUNCE:      return "DEBOUNCE";
    case State::PRESSED:       return "PRESSED";
    case State::CANCEL_WINDOW: return "CANCEL_WINDOW";
  }
  return "?";
}

const char* Fsm::txToken(Event e) {
  switch (e) {
    case Event::ALERT_SHORT: return "ALERT:1";
    case Event::ALERT_LONG:  return "ALERT:2";
    case Event::CANCEL:      return "CANCEL";
    default:                 return nullptr;   // no viaja por BLE
  }
}

void Fsm::begin(bool pressedNow, uint32_t now) {
  m_state           = State::IDLE;
  m_tDebounce       = now;
  m_tPressed        = now;
  m_tCancelStart    = now;
  m_tRawChange      = now;
  m_lastRaw         = pressedNow;
  m_idlePrevPressed = pressedNow;   // arrancar con BOOT pulsado no es un flanco
  m_longFired       = false;
  m_cancelArmed     = false;
}

/* Vuelta a IDLE. SIEMPRE se pasa por aquí, y aquí se resincroniza el nivel
 * previo con el nivel real del botón.
 *
 * Esto es lo que arregla el fallo del rebote: antes, si el botón rebotaba y
 * DEBOUNCE volvía a IDLE, `m_idlePrevPressed` se quedaba en "pulsado" (el
 * valor que dejó la detección del flanco). Cuando el botón se asentaba de
 * verdad ya no había transición suelto->pulsado que detectar, así que la
 * pulsación se PERDÍA hasta que el usuario soltaba y volvía a pulsar. En un
 * botón de pánico eso es inaceptable. */
Step Fsm::enterIdle(Step st, bool pressed, const char* reason) {
  m_idlePrevPressed = pressed;
  st.changed = true;
  st.to      = State::IDLE;
  st.reason  = reason;
  m_state    = State::IDLE;
  return st;
}

Step Fsm::update(uint32_t now, bool pressed) {
  Step st;
  st.from = m_state;
  st.to   = m_state;

  switch (m_state) {

    /* ------------------------------------------------------------------ */
    case State::IDLE: {
      // Se exige un flanco real suelto->pulsado. Si se entra a IDLE con el
      // botón aún pulsado (tras un CANCEL, o si la placa arranca con BOOT
      // pulsado) hay que soltarlo antes de armar una pulsación nueva.
      const bool falling = (pressed && !m_idlePrevPressed);
      m_idlePrevPressed  = pressed;
      if (falling) {
        m_tDebounce = now;
        m_state     = State::DEBOUNCE;
        st.changed  = true;
        st.to       = State::DEBOUNCE;
        st.reason   = "flanco de bajada detectado";
      }
      break;
    }

    /* ------------------------------------------------------------------ */
    case State::DEBOUNCE:
      if (!pressed) {
        st.event = Event::BOUNCE;
        st = enterIdle(st, pressed, "rebote/ruido: se soltó antes de DEBOUNCE_MS");
      } else if (now - m_tDebounce >= m_cfg.debounceMs) {
        m_tPressed   = now;
        m_longFired  = false;
        m_lastRaw    = true;
        m_tRawChange = now;
        m_state      = State::PRESSED;
        st.changed   = true;
        st.to        = State::PRESSED;
        st.reason    = "pulsación confirmada tras el antirrebote";
      }
      break;

    /* ------------------------------------------------------------------ */
    case State::PRESSED: {
      // (a) Pulsación prolongada: se emite EN EL INSTANTE de los 3 s.
      //     Se exige que el botón SIGA pulsado. Sin esa condición, al soltar
      //     entre 2950 y 3000 ms esta rama se adelantaba al antirrebote del
      //     flanco de subida (que tarda DEBOUNCE_MS en confirmarse) y salía
      //     ALERT:2 —emergencia grave— en vez de ALERT:1 (incidencia I11).
      if (!m_longFired && pressed && (now - m_tPressed >= m_cfg.longPressMs)) {
        m_longFired    = true;
        m_tCancelStart = now;
        m_cancelArmed  = false;          // hay que esperar a que suelte el botón
        m_lastRaw      = true;
        m_tRawChange   = now;
        m_state        = State::CANCEL_WINDOW;
        st.event       = Event::ALERT_LONG;
        st.changed     = true;
        st.to          = State::CANCEL_WINDOW;
        st.reason      = "ALERT:2 : mantenido >= 3 s";
        break;
      }
      // (b) Antirrebote del flanco de subida (soltar el botón).
      if (pressed != m_lastRaw) { m_lastRaw = pressed; m_tRawChange = now; }
      if (!pressed && (now - m_tRawChange >= m_cfg.debounceMs)) {
        // Se soltó antes de los 3 s -> pulsación corta.
        m_tCancelStart = now;
        m_cancelArmed  = true;           // el botón ya está liberado
        m_lastRaw      = false;
        m_tRawChange   = now;
        m_state        = State::CANCEL_WINDOW;
        st.event       = Event::ALERT_SHORT;
        st.changed     = true;
        st.to          = State::CANCEL_WINDOW;
        st.reason      = "ALERT:1 : pulsación corta";
      }
      break;
    }

    /* ------------------------------------------------------------------ */
    case State::CANCEL_WINDOW: {
      // Antirrebote común a ambos flancos dentro de la ventana.
      if (pressed != m_lastRaw) { m_lastRaw = pressed; m_tRawChange = now; }
      const bool stable = (now - m_tRawChange >= m_cfg.debounceMs);

      if (!m_cancelArmed) {
        // Venimos de una pulsación prolongada con el botón aún pulsado: se
        // arma la cancelación cuando el botón se suelta de forma estable.
        if (!pressed && stable) {
          m_cancelArmed = true;
          st.event      = Event::CANCEL_ARMED;
          break;   // la expiración se evalúa en el tick siguiente
        }
      } else if (pressed && stable) {
        // Nueva pulsación dentro de la ventana -> CANCELAR.
        st.event = Event::CANCEL;
        st = enterIdle(st, pressed, "CANCEL dentro de la ventana");
        break;
      }

      if (now - m_tCancelStart >= m_cfg.cancelWindowMs) {
        st.event = Event::WINDOW_EXPIRED;
        st = enterIdle(st, pressed, "ventana expirada: alerta CONFIRMADA");
      }
      break;
    }
  }

  return st;
}

}  // namespace panic
