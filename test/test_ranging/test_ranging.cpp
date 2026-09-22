/* ============================================================================
 *  Pruebas del estimador de cercanía (lib/ranging)
 * ----------------------------------------------------------------------------
 *      pio test -e native
 *
 *  Aquí se comprueba el "caliente/frío" del buscador sin tener que pasearse
 *  con dos placas por el patio: se le inyecta una secuencia de RSSI y se mira
 *  que la zona, el pitido y la flecha de tendencia respondan como toca.
 * ==========================================================================*/
#include <unity.h>

#include "ranging.h"

void setUp(void) {}
void tearDown(void) {}

/* ------------------------------------------------------------------ zonas -*/
static void test_zonas_por_umbral(void) {
  ranging::Config cfg;   // -55 / -85 / -110
  TEST_ASSERT_TRUE(ranging::zoneOf(-30.0f, cfg) == ranging::Zone::HERE);
  TEST_ASSERT_TRUE(ranging::zoneOf(-55.0f, cfg) == ranging::Zone::HERE);   // el borde entra
  TEST_ASSERT_TRUE(ranging::zoneOf(-56.0f, cfg) == ranging::Zone::NEAR);
  TEST_ASSERT_TRUE(ranging::zoneOf(-85.0f, cfg) == ranging::Zone::NEAR);
  TEST_ASSERT_TRUE(ranging::zoneOf(-86.0f, cfg) == ranging::Zone::FAR);
  TEST_ASSERT_TRUE(ranging::zoneOf(-110.0f, cfg) == ranging::Zone::FAR);
  TEST_ASSERT_TRUE(ranging::zoneOf(-120.0f, cfg) == ranging::Zone::VERY_FAR);
}

static void test_barras_y_nombres(void) {
  TEST_ASSERT_EQUAL_UINT8(5, ranging::zoneBars(ranging::Zone::HERE));
  TEST_ASSERT_EQUAL_UINT8(0, ranging::zoneBars(ranging::Zone::LOST));
  TEST_ASSERT_NOT_NULL(ranging::zoneName(ranging::Zone::NEAR));
}

static void test_cadencia_de_pitido(void) {
  // Cuanto más cerca, más seguido. Sin señal, no se pita.
  TEST_ASSERT_TRUE(ranging::beepPeriodMs(ranging::Zone::HERE) <
                   ranging::beepPeriodMs(ranging::Zone::NEAR));
  TEST_ASSERT_TRUE(ranging::beepPeriodMs(ranging::Zone::NEAR) <
                   ranging::beepPeriodMs(ranging::Zone::FAR));
  TEST_ASSERT_TRUE(ranging::beepPeriodMs(ranging::Zone::FAR) <
                   ranging::beepPeriodMs(ranging::Zone::VERY_FAR));
  TEST_ASSERT_EQUAL_UINT32(0u, ranging::beepPeriodMs(ranging::Zone::LOST));
}

/* -------------------------------------------------------------- distancia -*/
static void test_distancia_a_un_metro(void) {
  ranging::Config cfg;
  // Por definición del modelo: al RSSI de referencia, 1 m.
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 1.0f, ranging::distanceM(cfg.rssiAt1m, cfg));
}

static void test_la_distancia_crece_al_bajar_el_rssi(void) {
  ranging::Config cfg;
  const float d1 = ranging::distanceM(-45.0f, cfg);
  const float d2 = ranging::distanceM(-70.0f, cfg);
  const float d3 = ranging::distanceM(-100.0f, cfg);
  TEST_ASSERT_TRUE(d1 < d2);
  TEST_ASSERT_TRUE(d2 < d3);
  TEST_ASSERT_TRUE(d1 > 0.0f);
}

static void test_distancia_sin_modelo_no_revienta(void) {
  ranging::Config cfg;
  cfg.pathLossExp = 0.0f;      // configuración absurda: no debe dividir por cero
  TEST_ASSERT_EQUAL_FLOAT(0.0f, ranging::distanceM(-80.0f, cfg));
}

static void test_distancia_tiene_tope(void) {
  ranging::Config cfg;
  TEST_ASSERT_TRUE(ranging::distanceM(-200.0f, cfg) <= 100000.0f);
}

/* ------------------------------------------------------------- estimador --*/
static void test_arranca_sin_senal(void) {
  ranging::Estimator e;
  e.reset();
  TEST_ASSERT_FALSE(e.hasSignal());
  TEST_ASSERT_TRUE(e.zone() == ranging::Zone::LOST);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, e.distanceM());
}

static void test_la_primera_trama_no_se_suaviza(void) {
  /* Si el filtro arrancara en 0 dBm, la primera lectura diría "lo tienes
   * encima" aunque el llavero esté a 200 m. Tiene que arrancar en la medida. */
  ranging::Estimator e;
  e.reset();
  e.push(-90.0f, 1000);
  TEST_ASSERT_TRUE(e.hasSignal());
  TEST_ASSERT_FLOAT_WITHIN(0.01f, -90.0f, e.rssi());
  TEST_ASSERT_TRUE(e.zone() == ranging::Zone::FAR);
}

static void test_el_suavizado_amortigua_un_pico(void) {
  ranging::Config cfg;
  cfg.alpha = 0.3f;
  ranging::Estimator e(cfg);
  e.reset();
  for (uint32_t i = 0; i < 10; ++i) e.push(-80.0f, 1000 + i * 100);
  TEST_ASSERT_FLOAT_WITHIN(0.5f, -80.0f, e.rssi());

  e.push(-40.0f, 3000);                 // un pico aislado (reflexión)
  TEST_ASSERT_TRUE(e.rssi() < -60.0f);  // no debe saltar a "AQUI" de golpe
  TEST_ASSERT_TRUE(e.zone() != ranging::Zone::HERE);
}

static void test_se_pierde_tras_el_silencio(void) {
  ranging::Config cfg;
  cfg.lostAfterMs = 5000;
  ranging::Estimator e(cfg);
  e.reset();
  e.push(-60.0f, 1000);
  e.update(5000);
  TEST_ASSERT_TRUE(e.zone() == ranging::Zone::NEAR);   // aún dentro del plazo
  e.update(6001);
  TEST_ASSERT_TRUE(e.zone() == ranging::Zone::LOST);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, e.distanceM());
}

static void test_al_recuperar_la_senal_no_arrastra_lo_viejo(void) {
  ranging::Config cfg;
  cfg.lostAfterMs = 5000;
  cfg.alpha       = 0.2f;
  ranging::Estimator e(cfg);
  e.reset();
  e.push(-110.0f, 0);          // estaba lejísimos
  e.update(10000);             // se perdió
  TEST_ASSERT_TRUE(e.zone() == ranging::Zone::LOST);

  e.push(-50.0f, 10500);       // vuelve, y vuelve cerca
  TEST_ASSERT_TRUE(e.zone() == ranging::Zone::HERE);   // sin arrastrar los -110
}

static void test_tendencia_acercarse_y_alejarse(void) {
  ranging::Config cfg;
  cfg.alpha = 1.0f;            // sin filtro, para que la tendencia sea limpia
  ranging::Estimator e(cfg);

  e.reset();
  uint32_t t = 0;
  for (float r = -100.0f; r <= -60.0f; r += 5.0f) e.push(r, t += 200);
  TEST_ASSERT_EQUAL_INT8(1, e.trend());               // subiendo el RSSI = acercándose

  e.reset();
  t = 0;
  for (float r = -60.0f; r >= -100.0f; r -= 5.0f) e.push(r, t += 200);
  TEST_ASSERT_EQUAL_INT8(-1, e.trend());

  e.reset();
  t = 0;
  for (int i = 0; i < 8; ++i) e.push(-70.0f, t += 200);
  TEST_ASSERT_EQUAL_INT8(0, e.trend());               // quieto es quieto
}

static void test_silencio_medido(void) {
  ranging::Estimator e;
  e.reset();
  TEST_ASSERT_EQUAL_UINT32(0u, e.silenceMs(5000));    // sin señal no hay silencio que contar
  e.push(-70.0f, 1000);
  TEST_ASSERT_EQUAL_UINT32(4000u, e.silenceMs(5000));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_zonas_por_umbral);
  RUN_TEST(test_barras_y_nombres);
  RUN_TEST(test_cadencia_de_pitido);
  RUN_TEST(test_distancia_a_un_metro);
  RUN_TEST(test_la_distancia_crece_al_bajar_el_rssi);
  RUN_TEST(test_distancia_sin_modelo_no_revienta);
  RUN_TEST(test_distancia_tiene_tope);
  RUN_TEST(test_arranca_sin_senal);
  RUN_TEST(test_la_primera_trama_no_se_suaviza);
  RUN_TEST(test_el_suavizado_amortigua_un_pico);
  RUN_TEST(test_se_pierde_tras_el_silencio);
  RUN_TEST(test_al_recuperar_la_senal_no_arrastra_lo_viejo);
  RUN_TEST(test_tendencia_acercarse_y_alejarse);
  RUN_TEST(test_silencio_medido);
  return UNITY_END();
}
