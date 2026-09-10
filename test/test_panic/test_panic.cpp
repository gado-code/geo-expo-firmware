/* ============================================================================
 *  Pruebas de la máquina de estados del botón de pánico (lib/panic)
 * ============================================================================
 *      pio test -e native       <- en el PC, SIN placa  (lo normal)
 *      pio test -e devkit_v1    <- en la placa
 *
 *  Aquí está lo que NO se puede comprobar mirando el monitor serie: los
 *  bordes de temporización. Se simula el botón tick a tick (1 ms por tick,
 *  igual que el loop() real) y se comprueba QUÉ eventos salen y CUÁNDO.
 *
 *  Cubre, entre otras cosas, las dos incidencias que se nos escaparon:
 *    · I11  soltar entre 2950 y 3000 ms daba ALERT:2 en vez de ALERT:1
 *    · I13  un rebote en DEBOUNCE hacía que la pulsación siguiente se
 *           PERDIERA hasta soltar y volver a pulsar
 * ==========================================================================*/
#include <unity.h>
#include <stdio.h>

#include "panic.h"

using panic::Event;
using panic::Fsm;
using panic::State;

/* -------------------------------------------------------------------------
 *  Simulador: mantiene el botón en un nivel durante N ms, tick a tick, y
 *  apunta todos los eventos con su marca de tiempo.
 * -----------------------------------------------------------------------*/
static const int MAX_EV = 40;

struct Sim {
  Fsm      fsm;
  uint32_t t = 0;
  Event    ev[MAX_EV];
  uint32_t at[MAX_EV];
  int      n = 0;

  explicit Sim(uint32_t t0 = 0) { fsm.begin(false, t0); t = t0; }

  /* Mantiene `pressed` durante `ms` milisegundos. */
  void hold(uint32_t ms, bool pressed) {
    for (uint32_t i = 0; i < ms; ++i) {
      panic::Step s = fsm.update(t, pressed);
      if (s.event != Event::NONE && n < MAX_EV) { ev[n] = s.event; at[n] = t; ++n; }
      ++t;
    }
  }

  /* ¿Salió alguna vez este evento? */
  bool saw(Event e) const {
    for (int i = 0; i < n; ++i) if (ev[i] == e) return true;
    return false;
  }
  /* Instante del primer evento de ese tipo (o 0xFFFFFFFF si no salió). */
  uint32_t when(Event e) const {
    for (int i = 0; i < n; ++i) if (ev[i] == e) return at[i];
    return 0xFFFFFFFFu;
  }
  int count(Event e) const {
    int c = 0;
    for (int i = 0; i < n; ++i) if (ev[i] == e) ++c;
    return c;
  }
};

/* Una pulsación completa: `downMs` pulsado y luego `upMs` suelto. */
static void press(Sim& s, uint32_t downMs, uint32_t upMs) {
  s.hold(downMs, true);
  s.hold(upMs, false);
}

/* -------------------------------------------------------------------------
 *  1. Pulsación corta -> ALERT:1
 * -----------------------------------------------------------------------*/
void test_pulsacion_corta_da_alert1(void) {
  Sim s;
  press(s, 300, 200);
  TEST_ASSERT_TRUE(s.saw(Event::ALERT_SHORT));
  TEST_ASSERT_FALSE(s.saw(Event::ALERT_LONG));
  TEST_ASSERT_EQUAL_INT(1, s.count(Event::ALERT_SHORT));   // uno solo, no varios
  /* Sale 50 ms (antirrebote de subida) después de soltar. */
  TEST_ASSERT_EQUAL_UINT32(300 + 50, s.when(Event::ALERT_SHORT));
  TEST_ASSERT_EQUAL_INT((int)State::CANCEL_WINDOW, (int)s.fsm.state());
}

/* -------------------------------------------------------------------------
 *  2. Pulsación larga -> ALERT:2 en el instante exacto de los 3 s
 * -----------------------------------------------------------------------*/
void test_pulsacion_larga_da_alert2_a_los_3s(void) {
  Sim s;
  s.hold(4000, true);
  TEST_ASSERT_TRUE(s.saw(Event::ALERT_LONG));
  TEST_ASSERT_FALSE(s.saw(Event::ALERT_SHORT));
  /* Los 3 s se cuentan desde que la pulsación queda CONFIRMADA, o sea 50 ms
   * después del flanco: 50 + 3000. Es el comportamiento que ya se midió en
   * la placa (PRESSED 481993 -> TX 484993). */
  TEST_ASSERT_EQUAL_UINT32(50 + 3000, s.when(Event::ALERT_LONG));
  /* Y no se repite mientras se sigue apretando. */
  TEST_ASSERT_EQUAL_INT(1, s.count(Event::ALERT_LONG));
}

/* -------------------------------------------------------------------------
 *  3. INCIDENCIA I11: soltar justo antes de los 3 s
 *     Barrido por toda la zona que fallaba (2950-3000 ms) y alrededores.
 * -----------------------------------------------------------------------*/
void test_i11_soltar_antes_de_3s_nunca_da_alert2(void) {
  const uint32_t sueltas[] = {2000, 2900, 2949, 2950, 2951, 2970, 2980,
                              2990, 2995, 2998, 2999};
  for (unsigned k = 0; k < sizeof(sueltas) / sizeof(sueltas[0]); ++k) {
    Sim s;
    /* `sueltas[k]` se mide desde que la pulsación queda confirmada. */
    press(s, 50 + sueltas[k], 200);
    char msg[64];
    snprintf(msg, sizeof(msg), "soltando a %lu ms", (unsigned long)sueltas[k]);
    TEST_ASSERT_TRUE_MESSAGE(s.saw(Event::ALERT_SHORT), msg);
    TEST_ASSERT_FALSE_MESSAGE(s.saw(Event::ALERT_LONG), msg);
  }
}

void test_i11_el_umbral_cae_limpio_en_3000(void) {
  /* Ni un milisegundo de zona gris: el criterio es si el boton SIGUE pulsado
   * en el muestreo de los 3000 ms (que cae en t = 50 + 3000, porque los 3 s
   * se cuentan desde que la pulsacion queda confirmada).
   *
   *   · soltado justo ANTES de ese muestreo -> ALERT:1
   *   · todavia pulsado EN ese muestreo     -> ALERT:2                    */
  const uint32_t umbral = 50 + 3000;

  Sim corta;
  press(corta, umbral, 200);            // ultimo tick pulsado: umbral - 1
  TEST_ASSERT_TRUE(corta.saw(Event::ALERT_SHORT));
  TEST_ASSERT_FALSE(corta.saw(Event::ALERT_LONG));

  Sim larga;
  press(larga, umbral + 1, 200);        // ultimo tick pulsado: umbral
  TEST_ASSERT_TRUE(larga.saw(Event::ALERT_LONG));
  TEST_ASSERT_FALSE(larga.saw(Event::ALERT_SHORT));
  TEST_ASSERT_EQUAL_UINT32(umbral, larga.when(Event::ALERT_LONG));
}

/* -------------------------------------------------------------------------
 *  4. Antirrebote del flanco de bajada: un roce no dispara nada
 * -----------------------------------------------------------------------*/
void test_roce_breve_no_dispara_alerta(void) {
  Sim s;
  press(s, 20, 200);                       // 20 ms < DEBOUNCE_MS
  TEST_ASSERT_TRUE(s.saw(Event::BOUNCE));
  TEST_ASSERT_FALSE(s.saw(Event::ALERT_SHORT));
  TEST_ASSERT_FALSE(s.saw(Event::ALERT_LONG));
  TEST_ASSERT_EQUAL_INT((int)State::IDLE, (int)s.fsm.state());
}

/* -------------------------------------------------------------------------
 *  5. INCIDENCIA I13 (regresión): tras un rebote, la pulsación BUENA que
 *     viene justo detrás tiene que detectarse igual.
 *
 *     Antes se perdía: al volver de DEBOUNCE a IDLE el nivel previo se
 *     quedaba en "pulsado", así que ya no había flanco que detectar y el
 *     botón se quedaba muerto hasta soltarlo del todo.
 * -----------------------------------------------------------------------*/
void test_i13_pulsacion_tras_rebote_no_se_pierde(void) {
  Sim s;
  s.hold(20, true);    // contacto sucio
  s.hold(1,  false);   // rebota UN tick -> DEBOUNCE vuelve a IDLE
  s.hold(400, true);   // y ahora el dedo apoya de verdad
  s.hold(200, false);  // se suelta
  TEST_ASSERT_TRUE_MESSAGE(s.saw(Event::ALERT_SHORT),
                           "la pulsacion posterior al rebote se perdio");
  TEST_ASSERT_EQUAL_INT(1, s.count(Event::ALERT_SHORT));
}

void test_i13_varios_rebotes_seguidos_acaban_disparando(void) {
  Sim s;
  for (int i = 0; i < 6; ++i) { s.hold(15, true); s.hold(1, false); }
  s.hold(400, true);
  s.hold(200, false);
  TEST_ASSERT_TRUE(s.saw(Event::ALERT_SHORT));
  TEST_ASSERT_EQUAL_INT(1, s.count(Event::ALERT_SHORT));
}

/* -------------------------------------------------------------------------
 *  6. Cancelación desde una pulsación corta
 * -----------------------------------------------------------------------*/
void test_cancelar_desde_pulsacion_corta(void) {
  Sim s;
  press(s, 300, 500);        // ALERT:1 y ventana abierta
  press(s, 300, 200);        // segunda pulsación dentro de la ventana
  TEST_ASSERT_TRUE(s.saw(Event::CANCEL));
  TEST_ASSERT_FALSE(s.saw(Event::WINDOW_EXPIRED));
  TEST_ASSERT_EQUAL_INT((int)State::IDLE, (int)s.fsm.state());
}

/* -------------------------------------------------------------------------
 *  7. Cancelación desde una pulsación LARGA (prueba 1.6 de PRUEBAS.md)
 * -----------------------------------------------------------------------*/
void test_cancelar_desde_pulsacion_larga(void) {
  Sim s;
  s.hold(3200, true);        // ALERT:2 a los 3050
  TEST_ASSERT_TRUE(s.saw(Event::ALERT_LONG));
  s.hold(300, false);        // al soltar se ARMA la cancelación
  TEST_ASSERT_TRUE(s.saw(Event::CANCEL_ARMED));
  press(s, 300, 100);        // y ahora sí se puede cancelar
  TEST_ASSERT_TRUE(s.saw(Event::CANCEL));
  TEST_ASSERT_EQUAL_INT((int)State::IDLE, (int)s.fsm.state());
}

void test_pulsacion_larga_no_se_autocancela_sin_soltar(void) {
  /* Se mantiene el botón apretado 9 s seguidos: ALERT:2 y NADA más. Que la
   * propia pulsación que dispara la alerta la cancelara sola sería el peor
   * fallo posible en un botón de pánico. */
  Sim s;
  s.hold(9000, true);
  TEST_ASSERT_TRUE(s.saw(Event::ALERT_LONG));
  TEST_ASSERT_FALSE(s.saw(Event::CANCEL));
  TEST_ASSERT_FALSE(s.saw(Event::CANCEL_ARMED));
}

/* -------------------------------------------------------------------------
 *  8. La ventana expira -> alerta CONFIRMADA
 * -----------------------------------------------------------------------*/
void test_ventana_expira_y_confirma(void) {
  Sim s;
  press(s, 300, 11000);
  TEST_ASSERT_TRUE(s.saw(Event::ALERT_SHORT));
  TEST_ASSERT_TRUE(s.saw(Event::WINDOW_EXPIRED));
  TEST_ASSERT_FALSE(s.saw(Event::CANCEL));
  /* La ventana son 10 s desde que salió el ALERT:1. */
  TEST_ASSERT_EQUAL_UINT32(s.when(Event::ALERT_SHORT) + 10000,
                           s.when(Event::WINDOW_EXPIRED));
  TEST_ASSERT_EQUAL_INT((int)State::IDLE, (int)s.fsm.state());
}

/* -------------------------------------------------------------------------
 *  9. Después de un CANCEL hay que SOLTAR antes de volver a armar
 * -----------------------------------------------------------------------*/
void test_tras_cancel_hay_que_soltar_el_boton(void) {
  Sim s;
  press(s, 300, 500);        // ALERT:1
  s.hold(4000, true);        // se cancela y se SIGUE apretando 4 s más
  TEST_ASSERT_TRUE(s.saw(Event::CANCEL));
  /* Ese mismo dedo apoyado no puede convertirse en una alerta nueva. */
  TEST_ASSERT_EQUAL_INT(1, s.count(Event::ALERT_SHORT));
  TEST_ASSERT_FALSE(s.saw(Event::ALERT_LONG));
  TEST_ASSERT_EQUAL_INT((int)State::IDLE, (int)s.fsm.state());
}

/* -------------------------------------------------------------------------
 *  10. Arrancar con el botón ya pulsado no dispara nada
 *      (es el caso del DTR del monitor serie, incidencia I10)
 * -----------------------------------------------------------------------*/
void test_arranque_con_boton_pulsado_no_dispara(void) {
  Sim s;
  s.fsm.begin(true, 0);      // la placa arranca con BOOT a nivel bajo
  s.hold(6000, true);
  TEST_ASSERT_FALSE(s.saw(Event::ALERT_SHORT));
  TEST_ASSERT_FALSE(s.saw(Event::ALERT_LONG));
  /* Y en cuanto se suelta, el botón vuelve a funcionar con normalidad. */
  s.hold(200, false);
  press(s, 300, 200);
  TEST_ASSERT_TRUE(s.saw(Event::ALERT_SHORT));
}

/* -------------------------------------------------------------------------
 *  11. Ciclos encadenados: no quedan residuos de estado (prueba 1.10)
 * -----------------------------------------------------------------------*/
void test_tres_ciclos_encadenados(void) {
  Sim s;
  for (int i = 0; i < 3; ++i) press(s, 300, 11000);
  TEST_ASSERT_EQUAL_INT(3, s.count(Event::ALERT_SHORT));
  TEST_ASSERT_EQUAL_INT(3, s.count(Event::WINDOW_EXPIRED));
  TEST_ASSERT_EQUAL_INT(0, s.count(Event::CANCEL));
}

void test_alterna_corta_y_larga(void) {
  Sim s;
  press(s, 300, 11000);          // corta
  press(s, 3200, 11000);         // larga
  press(s, 300, 11000);          // corta
  TEST_ASSERT_EQUAL_INT(2, s.count(Event::ALERT_SHORT));
  TEST_ASSERT_EQUAL_INT(1, s.count(Event::ALERT_LONG));
  TEST_ASSERT_EQUAL_INT(0, s.count(Event::CANCEL));
}

/* -------------------------------------------------------------------------
 *  12. Rebote AL SOLTAR: un solo ALERT:1, no varios (prueba 1.8)
 * -----------------------------------------------------------------------*/
void test_rebote_al_soltar_no_duplica_la_alerta(void) {
  Sim s;
  s.hold(300, true);
  for (int i = 0; i < 5; ++i) { s.hold(3, false); s.hold(3, true); }  // chispazos
  s.hold(500, false);
  TEST_ASSERT_EQUAL_INT(1, s.count(Event::ALERT_SHORT));
  TEST_ASSERT_FALSE(s.saw(Event::ALERT_LONG));
}

/* -------------------------------------------------------------------------
 *  13. millis() desborda a los ~49,7 días: la FSM tiene que aguantarlo
 * -----------------------------------------------------------------------*/
void test_desbordamiento_de_millis(void) {
  Sim s(0xFFFFF000u);            // faltan ~4 s para el desbordamiento
  press(s, 300, 200);
  TEST_ASSERT_TRUE(s.saw(Event::ALERT_SHORT));
  s.hold(11000, false);          // la ventana expira YA pasado el desbordamiento
  TEST_ASSERT_TRUE(s.saw(Event::WINDOW_EXPIRED));

  Sim l(0xFFFFF000u);
  l.hold(4000, true);            // el umbral de 3 s cae al otro lado del cero
  TEST_ASSERT_TRUE(l.saw(Event::ALERT_LONG));
  TEST_ASSERT_FALSE(l.saw(Event::ALERT_SHORT));
}

/* -------------------------------------------------------------------------
 *  14. Los tokens del contrato BLE son EXACTAMENTE los acordados con la app
 * -----------------------------------------------------------------------*/
void test_tokens_del_contrato_ble(void) {
  TEST_ASSERT_EQUAL_STRING("ALERT:1", Fsm::txToken(Event::ALERT_SHORT));
  TEST_ASSERT_EQUAL_STRING("ALERT:2", Fsm::txToken(Event::ALERT_LONG));
  TEST_ASSERT_EQUAL_STRING("CANCEL",  Fsm::txToken(Event::CANCEL));
  /* Estos son internos: NO se transmiten. */
  TEST_ASSERT_NULL(Fsm::txToken(Event::CANCEL_ARMED));
  TEST_ASSERT_NULL(Fsm::txToken(Event::WINDOW_EXPIRED));
  TEST_ASSERT_NULL(Fsm::txToken(Event::BOUNCE));
  TEST_ASSERT_NULL(Fsm::txToken(Event::NONE));
}

/* -------------------------------------------------------------------------
 *  Arranque de Unity
 * -----------------------------------------------------------------------*/
void setUp(void) {}
void tearDown(void) {}

static void runAll(void) {
  UNITY_BEGIN();
  RUN_TEST(test_pulsacion_corta_da_alert1);
  RUN_TEST(test_pulsacion_larga_da_alert2_a_los_3s);
  RUN_TEST(test_i11_soltar_antes_de_3s_nunca_da_alert2);
  RUN_TEST(test_i11_el_umbral_cae_limpio_en_3000);
  RUN_TEST(test_roce_breve_no_dispara_alerta);
  RUN_TEST(test_i13_pulsacion_tras_rebote_no_se_pierde);
  RUN_TEST(test_i13_varios_rebotes_seguidos_acaban_disparando);
  RUN_TEST(test_cancelar_desde_pulsacion_corta);
  RUN_TEST(test_cancelar_desde_pulsacion_larga);
  RUN_TEST(test_pulsacion_larga_no_se_autocancela_sin_soltar);
  RUN_TEST(test_ventana_expira_y_confirma);
  RUN_TEST(test_tras_cancel_hay_que_soltar_el_boton);
  RUN_TEST(test_arranque_con_boton_pulsado_no_dispara);
  RUN_TEST(test_tres_ciclos_encadenados);
  RUN_TEST(test_alterna_corta_y_larga);
  RUN_TEST(test_rebote_al_soltar_no_duplica_la_alerta);
  RUN_TEST(test_desbordamiento_de_millis);
  RUN_TEST(test_tokens_del_contrato_ble);
  UNITY_END();
}

#ifdef ARDUINO
#include <Arduino.h>
void setup() { delay(2000); runAll(); }
void loop()  {}
#else
int main(void) { runAll(); return 0; }
#endif
