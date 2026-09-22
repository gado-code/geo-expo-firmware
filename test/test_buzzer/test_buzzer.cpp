/* ============================================================================
 *  Pruebas del secuenciador del zumbador (lib/buzzer)
 * ----------------------------------------------------------------------------
 *      pio test -e native
 *
 *  El zumbador es la parte que MÁS fácil se rompe en un firmware de estos,
 *  porque la tentación es escribirlo con delay(). Estas pruebas simulan el
 *  paso del tiempo milisegundo a milisegundo y comprueban que los patrones
 *  suenan como deben, que se repiten lo que toca y que una alerta no se queda
 *  muda por culpa de la baliza (ni al revés).
 * ==========================================================================*/
#include <unity.h>

#include "buzzer.h"

void setUp(void) {}
void tearDown(void) {}

/* Avanza el reloj hasta `until` en pasos de 1 ms y devuelve la última salida. */
static buzzer::Out runUntil(buzzer::Player& p, uint32_t from, uint32_t until) {
  buzzer::Out o;
  for (uint32_t t = from; t <= until; ++t) o = p.update(t);
  return o;
}

/* Un patrón de laboratorio: 100 ms de tono, 50 ms de silencio. */
static const buzzer::Step STEPS_TEST[] = {{1000, 100}, {0, 50}};

/* ------------------------------------------------------------- lo básico --*/
static void test_arranca_en_silencio(void) {
  buzzer::Player p;
  const buzzer::Out o = p.update(0);
  TEST_ASSERT_FALSE(o.active);
  TEST_ASSERT_EQUAL_UINT16(0, o.freqHz);
  TEST_ASSERT_FALSE(p.busy());
}

static void test_un_patron_suena_y_termina(void) {
  const buzzer::Pattern pat(STEPS_TEST, 2, 0, buzzer::Prio::UI);
  buzzer::Player p;

  TEST_ASSERT_TRUE(p.play(pat, 0));
  buzzer::Out o = p.update(0);
  TEST_ASSERT_TRUE(o.changed);
  TEST_ASSERT_EQUAL_UINT16(1000, o.freqHz);

  o = runUntil(p, 1, 99);
  TEST_ASSERT_EQUAL_UINT16(1000, o.freqHz);      // sigue el tono

  o = runUntil(p, 100, 100);
  TEST_ASSERT_EQUAL_UINT16(0, o.freqHz);         // pasa al silencio
  TEST_ASSERT_TRUE(o.active);

  o = runUntil(p, 101, 200);
  TEST_ASSERT_FALSE(o.active);                   // patrón terminado
  TEST_ASSERT_FALSE(p.busy());
}

static void test_repeticion_finita(void) {
  const buzzer::Pattern pat(STEPS_TEST, 2, 1, buzzer::Prio::UI);   // suena 2 veces
  buzzer::Player p;
  p.play(pat, 0);
  p.update(0);

  // Segunda vuelta: a los 150 ms vuelve a haber tono.
  buzzer::Out o = runUntil(p, 1, 150);
  TEST_ASSERT_EQUAL_UINT16(1000, o.freqHz);
  TEST_ASSERT_TRUE(o.active);

  o = runUntil(p, 151, 400);
  TEST_ASSERT_FALSE(o.active);                   // y ahí se acaba
}

static void test_repeticion_infinita_no_para_sola(void) {
  buzzer::Player p;
  p.play(buzzer::PATTERN_BEACON, 0);
  p.update(0);
  buzzer::Out o = runUntil(p, 1, 60000);         // un minuto entero
  TEST_ASSERT_TRUE(o.active);
  TEST_ASSERT_TRUE(p.busy());
  p.stop();
  o = p.update(60001);
  TEST_ASSERT_FALSE(o.active);
  TEST_ASSERT_EQUAL_UINT16(0, o.freqHz);
}

static void test_un_hueco_grande_de_loop_no_estira_el_patron(void) {
  /* Si loop() se atasca 500 ms (una trama LoRa larga, por ejemplo), el patrón
   * no puede quedarse congelado en el primer paso: tiene que ponerse al día. */
  const buzzer::Pattern pat(STEPS_TEST, 2, 0, buzzer::Prio::UI);
  buzzer::Player p;
  p.play(pat, 0);
  p.update(0);
  const buzzer::Out o = p.update(500);
  TEST_ASSERT_FALSE(o.active);
  TEST_ASSERT_EQUAL_UINT16(0, o.freqHz);
}

/* ---------------------------------------------------------- prioridades ---*/
static void test_una_alerta_si_interrumpe_la_baliza(void) {
  /* Con la baliza encendida, pulsar el boton de panico TIENE que sonar. Antes
   * la baliza tenia mas prioridad y la alerta se quedaba muda: el usuario no
   * sabia si su alerta habia salido. */
  buzzer::Player p;
  TEST_ASSERT_TRUE(p.play(buzzer::PATTERN_BEACON, 0));
  TEST_ASSERT_TRUE(p.play(buzzer::PATTERN_ALERT_SHORT, 10));
  TEST_ASSERT_EQUAL_UINT8(buzzer::Prio::ALERT, p.priority());
}

static void test_la_baliza_de_fondo_vuelve_tras_la_alerta(void) {
  /* ...y la baliza no se pierde por ello: como patron de FONDO, vuelve sola
   * en cuanto la alerta termina de sonar. */
  buzzer::Player p;
  p.setBackground(&buzzer::PATTERN_BEACON);
  p.update(0);
  TEST_ASSERT_TRUE(p.busy());                       // arranca sola
  TEST_ASSERT_EQUAL_UINT8(buzzer::Prio::BEACON, p.priority());

  TEST_ASSERT_TRUE(p.play(buzzer::PATTERN_ALERT_SHORT, 100));
  buzzer::Out o = p.update(100);
  TEST_ASSERT_EQUAL_UINT8(buzzer::Prio::ALERT, p.priority());

  o = runUntil(p, 101, 600);                        // la alerta dura 250 ms
  TEST_ASSERT_TRUE(o.active);                       // sigue sonando algo...
  TEST_ASSERT_EQUAL_UINT8(buzzer::Prio::BEACON, p.priority());   // ...la baliza

  p.setBackground(nullptr);
  p.stop();
  TEST_ASSERT_FALSE(p.update(700).active);          // y sin fondo, se calla
}

static void test_stopIf_tambien_quita_el_fondo(void) {
  // BEACON:OFF tiene que apagar la baliza de verdad, no dejarla renaciendo.
  buzzer::Player p;
  p.setBackground(&buzzer::PATTERN_BEACON);
  p.update(0);
  TEST_ASSERT_TRUE(p.busy());
  p.stopIf(buzzer::PATTERN_BEACON);
  TEST_ASSERT_NULL(p.background());
  TEST_ASSERT_FALSE(p.update(1).active);
}

static void test_el_ping_cabe_en_el_periodo_mas_rapido(void) {
  /* El "caliente/frio" repite el ping cada 150 ms en la zona AQUI: si el
   * patron durase mas, se cortaria a si mismo en la zona que mas importa. */
  uint32_t total = 0;
  for (uint8_t i = 0; i < buzzer::PATTERN_PING.count; ++i) {
    total += buzzer::PATTERN_PING.steps[i].ms;
  }
  TEST_ASSERT_TRUE(total < 150);
}

static void test_una_alerta_si_pisa_un_bip_de_interfaz(void) {
  buzzer::Player p;
  TEST_ASSERT_TRUE(p.play(buzzer::PATTERN_BOOT, 0));        // prioridad UI
  TEST_ASSERT_TRUE(p.play(buzzer::PATTERN_ALERT_LONG, 10));
  TEST_ASSERT_EQUAL_UINT8(buzzer::Prio::ALERT, p.priority());
}

static void test_force_manda_siempre(void) {
  buzzer::Player p;
  p.play(buzzer::PATTERN_BEACON, 0);
  TEST_ASSERT_TRUE(p.play(buzzer::PATTERN_CANCEL, 10, /*force=*/true));
  TEST_ASSERT_EQUAL_UINT8(buzzer::Prio::ALERT, p.priority());
}

static void test_stopIf_solo_para_lo_suyo(void) {
  buzzer::Player p;
  p.play(buzzer::PATTERN_BEACON, 0);
  p.stopIf(buzzer::PATTERN_FIND);        // no es lo que suena: no toca nada
  TEST_ASSERT_TRUE(p.busy());
  p.stopIf(buzzer::PATTERN_BEACON);
  TEST_ASSERT_FALSE(p.busy());
}

/* --------------------------------------------------------------- silencio -*/
static void test_modo_mudo(void) {
  buzzer::Player p;
  p.mute(true);
  p.play(buzzer::PATTERN_ALERT_LONG, 0);
  const buzzer::Out o = p.update(0);
  TEST_ASSERT_TRUE(o.active);            // el patrón corre...
  TEST_ASSERT_EQUAL_UINT16(0, o.freqHz); // ...pero no sale sonido
  TEST_ASSERT_TRUE(p.muted());
}

static void test_changed_solo_cuando_cambia(void) {
  const buzzer::Pattern pat(STEPS_TEST, 2, 0, buzzer::Prio::UI);
  buzzer::Player p;
  p.play(pat, 0);
  TEST_ASSERT_TRUE(p.update(0).changed);       // silencio -> 1000 Hz
  TEST_ASSERT_FALSE(p.update(1).changed);      // sigue igual
  TEST_ASSERT_FALSE(p.update(50).changed);
  TEST_ASSERT_TRUE(p.update(100).changed);     // 1000 Hz -> silencio
}

static void test_patron_vacio_se_ignora(void) {
  buzzer::Player p;
  const buzzer::Pattern vacio(nullptr, 0, 0, buzzer::Prio::ALERT);
  TEST_ASSERT_FALSE(p.play(vacio, 0));
  TEST_ASSERT_FALSE(p.busy());
}

static void test_los_patrones_del_proyecto_estan_completos(void) {
  // Que nadie deje un patrón a medias al añadir uno nuevo.
  const buzzer::Pattern* todos[] = {
      &buzzer::PATTERN_BOOT,      &buzzer::PATTERN_ALERT_SHORT,
      &buzzer::PATTERN_ALERT_LONG,&buzzer::PATTERN_CANCEL,
      &buzzer::PATTERN_CONFIRMED, &buzzer::PATTERN_BEACON,
      &buzzer::PATTERN_FIND,      &buzzer::PATTERN_LINK_LOST,
      &buzzer::PATTERN_PING};
  for (unsigned i = 0; i < sizeof(todos) / sizeof(todos[0]); ++i) {
    TEST_ASSERT_NOT_NULL(todos[i]->steps);
    TEST_ASSERT_TRUE(todos[i]->count > 0);
    TEST_ASSERT_TRUE(todos[i]->priority > 0);
    for (uint8_t s = 0; s < todos[i]->count; ++s) {
      TEST_ASSERT_TRUE(todos[i]->steps[s].ms > 0);   // un paso de 0 ms no se oiría
    }
  }
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_arranca_en_silencio);
  RUN_TEST(test_un_patron_suena_y_termina);
  RUN_TEST(test_repeticion_finita);
  RUN_TEST(test_repeticion_infinita_no_para_sola);
  RUN_TEST(test_un_hueco_grande_de_loop_no_estira_el_patron);
  RUN_TEST(test_una_alerta_si_interrumpe_la_baliza);
  RUN_TEST(test_la_baliza_de_fondo_vuelve_tras_la_alerta);
  RUN_TEST(test_stopIf_tambien_quita_el_fondo);
  RUN_TEST(test_el_ping_cabe_en_el_periodo_mas_rapido);
  RUN_TEST(test_una_alerta_si_pisa_un_bip_de_interfaz);
  RUN_TEST(test_force_manda_siempre);
  RUN_TEST(test_stopIf_solo_para_lo_suyo);
  RUN_TEST(test_modo_mudo);
  RUN_TEST(test_changed_solo_cuando_cambia);
  RUN_TEST(test_patron_vacio_se_ignora);
  RUN_TEST(test_los_patrones_del_proyecto_estan_completos);
  return UNITY_END();
}
