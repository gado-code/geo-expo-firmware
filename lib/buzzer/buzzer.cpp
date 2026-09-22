/* ============================================================================
 *  GEO-EXPO ALERT  ·  Secuenciador del zumbador — implementación
 * ==========================================================================*/
#include "buzzer.h"

namespace buzzer {

/* ==========================================================================
 *  Patrones
 * --------------------------------------------------------------------------
 *  Las frecuencias están en la zona donde un piezo pequeño rinde más (2-4 kHz,
 *  que es donde suenan las alarmas de casa) para que se oiga en un salón con
 *  gente hablando. Las notas graves de los patrones existen para dar contraste,
 *  no para sonar bonito.
 * ==========================================================================*/
static const Step STEPS_BOOT[] = {
    {1800, 70}, {0, 40}, {2400, 70},
};
static const Step STEPS_ALERT_SHORT[] = {
    {2700, 250},
};
static const Step STEPS_ALERT_LONG[] = {
    {2200, 200}, {0, 90}, {3100, 350},
};
static const Step STEPS_CANCEL[] = {
    {2600, 90}, {0, 60}, {2100, 90}, {0, 60}, {1600, 140},
};
static const Step STEPS_CONFIRMED[] = {
    {3100, 120}, {0, 70}, {3100, 120},
};
static const Step STEPS_BEACON[] = {
    {2500, 300}, {0, 200},
};
static const Step STEPS_FIND[] = {
    {3400, 120}, {0, 80}, {3900, 120}, {0, 380},
};
static const Step STEPS_LINK_LOST[] = {
    {1400, 200}, {0, 120}, {1100, 300},
};

const Pattern PATTERN_BOOT(STEPS_BOOT, 3, 0, Prio::UI);
const Pattern PATTERN_ALERT_SHORT(STEPS_ALERT_SHORT, 1, 0, Prio::ALERT);
const Pattern PATTERN_ALERT_LONG(STEPS_ALERT_LONG, 3, 1, Prio::ALERT);
const Pattern PATTERN_CANCEL(STEPS_CANCEL, 5, 0, Prio::ALERT);
const Pattern PATTERN_CONFIRMED(STEPS_CONFIRMED, 3, 0, Prio::ALERT);
const Pattern PATTERN_BEACON(STEPS_BEACON, 2, REPEAT_FOREVER, Prio::BEACON);
const Pattern PATTERN_FIND(STEPS_FIND, 4, REPEAT_FOREVER, Prio::BEACON);
const Pattern PATTERN_LINK_LOST(STEPS_LINK_LOST, 3, 0, Prio::UI);

/* ==========================================================================
 *  Reproductor
 * ==========================================================================*/
bool Player::play(const Pattern& p, uint32_t now, bool force) {
  if (p.steps == nullptr || p.count == 0) return false;

  // Un patrón menos importante no interrumpe al que ya suena.
  if (m_active && !force && p.priority < m_prio) return false;

  m_steps   = p.steps;
  m_count   = p.count;
  m_repeats = p.repeats;
  m_prio    = p.priority;
  m_index   = 0;
  m_loops   = 0;
  m_active  = true;
  m_primed  = false;          // la primera salida se emite en el update()
  m_tStep   = now;
  return true;
}

void Player::stop() {
  m_active = false;
  m_steps  = nullptr;
  m_count  = 0;
  m_index  = 0;
  m_prio   = 0;
  m_primed = false;
}

void Player::stopIf(const Pattern& p) {
  if (m_active && m_steps == p.steps) stop();
}

uint16_t Player::currentFreq() const {
  if (!m_active || m_steps == nullptr || m_index >= m_count) return 0;
  return m_steps[m_index].freqHz;
}

Out Player::update(uint32_t now) {
  Out out;

  if (m_active) {
    /* ¿Se acabó el paso actual? Se avanza en bucle (y no un paso por vuelta)
     * porque loop() puede tardar más que un paso corto y si no el patrón se
     * estiraría. El tope de vueltas es un seguro: un patrón mal escrito con
     * todos los pasos a 0 ms y repetición infinita colgaría la placa aquí. */
    uint16_t guard = (uint16_t)(m_count * 4u + 4u);
    while (m_active && m_primed && guard-- > 0 && (now - m_tStep) >= m_steps[m_index].ms) {
      m_tStep = (uint32_t)(m_tStep + m_steps[m_index].ms);
      ++m_index;
      if (m_index >= m_count) {
        if (m_repeats == REPEAT_FOREVER) {
          m_index = 0;
        } else if (m_loops < m_repeats) {
          ++m_loops;
          m_index = 0;
        } else {
          stop();
        }
      }
    }
    if (!m_primed) { m_primed = true; m_tStep = now; }
  }

  const uint16_t freq = m_muted ? 0 : currentFreq();
  out.active  = m_active;
  out.freqHz  = freq;
  out.changed = (freq != m_lastOut);
  m_lastOut   = freq;
  return out;
}

}  // namespace buzzer
