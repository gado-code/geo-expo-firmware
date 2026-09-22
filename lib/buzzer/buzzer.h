/* ============================================================================
 *  GEO-EXPO ALERT  ·  Secuenciador del zumbador piezo
 * ============================================================================
 *  Los 20 buzzers piezo PASIVOS que se pidieron ya tienen firmware esperando.
 *  "Pasivo" quiere decir que no suenan solos: hay que darles una onda cuadrada
 *  a la frecuencia que queremos oír. Este módulo decide QUÉ frecuencia toca en
 *  cada instante; quien lo usa se encarga de sacarla por el pin (con LEDC en
 *  el ESP32).
 *
 *  Es 100 % no bloqueante, igual que el resto del firmware: nada de
 *  `tone(); delay(200); noTone();`. Se le pide un patrón, y en cada vuelta de
 *  loop() se le pregunta qué debe sonar ahora.
 *
 *      buzzer::Player p;
 *      p.play(buzzer::PATTERN_ALERT_LONG, millis());
 *      ...
 *      buzzer::Out o = p.update(millis());
 *      if (o.changed) { o.freqHz ? tone(PIN, o.freqHz) : noTone(PIN); }
 *
 *  Sin Arduino: se prueba entero en el PC con `pio test -e native`.
 * ==========================================================================*/
#pragma once

#include <stdint.h>

namespace buzzer {

/* --------------------------------------------------------------------------
 *  Un paso del patrón: una frecuencia durante un tiempo.
 *  freqHz == 0 es SILENCIO (es lo que separa dos pitidos).
 * ------------------------------------------------------------------------*/
struct Step {
  uint16_t freqHz;
  uint16_t ms;
};

/* --------------------------------------------------------------------------
 *  Un patrón completo. Se apunta a un array ESTÁTICO: el reproductor guarda
 *  el puntero, no copia los pasos.
 * ------------------------------------------------------------------------*/
struct Pattern {
  const Step* steps;
  uint8_t     count;
  uint8_t     repeats;   // 0 = una vez; REPEAT_FOREVER = sin fin
  uint8_t     priority;  // a mayor número, manda

  Pattern(const Step* s = nullptr, uint8_t n = 0, uint8_t r = 0, uint8_t prio = 0)
      : steps(s), count(n), repeats(r), priority(prio) {}
};

static const uint8_t REPEAT_FOREVER = 255;

/* Prioridades.
 *
 * MANDA LA ALERTA. Antes la baliza tenía la prioridad más alta, y eso hacía
 * que con `BEACON:ON` puesto una pulsación del botón de pánico no sonara: el
 * usuario se quedaba sin confirmación acústica de que su alerta había salido,
 * que es justo lo contrario de lo que tiene que hacer este aparato. La baliza
 * no se pierde por ello: se declara como PATRÓN DE FONDO (`setBackground`) y
 * vuelve sola en cuanto la alerta termina de sonar. */
namespace Prio {
  static const uint8_t UI     = 1;   // confirmaciones, arranque
  static const uint8_t BEACON = 2;   // baliza y modo "encuéntrame"
  static const uint8_t ALERT  = 3;   // ALERT:1 / ALERT:2 / CANCEL
}

/* --------------------------------------------------------------------------
 *  Patrones estándar del proyecto (definidos en buzzer.cpp).
 * ------------------------------------------------------------------------*/
extern const Pattern PATTERN_BOOT;         // arranque: dos notas cortas
extern const Pattern PATTERN_ALERT_SHORT;  // ALERT:1  un pitido
extern const Pattern PATTERN_ALERT_LONG;   // ALERT:2  dos pitidos graves/agudos
extern const Pattern PATTERN_CANCEL;       // CANCEL   tres pitidos descendentes
extern const Pattern PATTERN_CONFIRMED;    // ventana expirada: alerta confirmada
extern const Pattern PATTERN_BEACON;       // baliza: sirena lenta, sin fin
extern const Pattern PATTERN_FIND;         // "estoy aquí": chirrido agudo, sin fin
extern const Pattern PATTERN_LINK_LOST;    // el enlace LoRa se cayó
extern const Pattern PATTERN_PING;         // pitido corto del "caliente/frío"

/* --------------------------------------------------------------------------
 *  Qué debe sonar ahora.
 * ------------------------------------------------------------------------*/
struct Out {
  bool     changed = false;   // ha cambiado respecto a la vuelta anterior
  bool     active  = false;   // hay patrón en curso
  uint16_t freqHz  = 0;       // 0 = silencio
};

class Player {
 public:
  /* Arranca un patrón. Si hay otro sonando con MÁS prioridad, se ignora
   * (devuelve false) salvo que `force` sea true. */
  bool play(const Pattern& p, uint32_t now, bool force = false);

  /* Corta lo que haya. */
  void stop();

  /* Corta sólo si lo que suena es ese patrón (para apagar la baliza sin
   * cargarse un pitido de alerta que haya entrado justo después). */
  void stopIf(const Pattern& p);

  /* Patrón de FONDO: lo que debe sonar cuando no hay nada más sonando.
   * Sirve para la baliza y para el modo "encuéntrame", que son estados y no
   * eventos: una alerta los interrumpe y al acabar vuelven solos.
   * `setBackground(nullptr)` los quita. */
  void setBackground(const Pattern* p);
  const Pattern* background() const { return m_bg; }

  /* Una vuelta de loop(). Devuelve la frecuencia que toca ahora. */
  Out update(uint32_t now);

  bool    busy()     const { return m_active; }
  uint8_t priority() const { return m_active ? m_prio : 0; }

  /* Silencio general (modo discreto): el secuenciador sigue funcionando pero
   * nunca sale sonido. Útil si en la expo hay que callar el aparato. */
  void mute(bool on) { m_muted = on; }
  bool muted() const { return m_muted; }

 private:
  uint16_t currentFreq() const;

  const Pattern* m_bg   = nullptr;   // patrón de fondo (baliza / búsqueda)
  const Step* m_steps   = nullptr;
  uint8_t     m_count   = 0;
  uint8_t     m_index   = 0;
  uint8_t     m_repeats = 0;
  uint8_t     m_loops   = 0;
  uint8_t     m_prio    = 0;
  bool        m_active  = false;
  bool        m_muted   = false;
  bool        m_primed  = false;   // aún no se ha emitido la primera salida
  uint16_t    m_lastOut = 0;
  uint32_t    m_tStep   = 0;
};

}  // namespace buzzer
