/* ============================================================================
 *  Pruebas del detector de gestos del botón PRG (lib/geolora/gestos)
 *  DISENO §10
 * ============================================================================
 *      cd lora && pio test -e native
 *
 *  El tiempo se simula: `update(t, pulsado)` se llama como lo haría el loop
 *  de la placa, milisegundo a milisegundo. Así se prueban los rebotes y los
 *  tres gestos sin tocar un botón (misma idea que test_panic del llavero).
 *
 *  Lo crítico en el campo: la pulsación LARGA tiene que avisar a los 1.5 s
 *  EXACTOS y sin soltar (el operario no está mirando la pantalla), y una
 *  pulsación corta no puede colarse como doble por un rebote.
 * ==========================================================================*/
#include <unity.h>

#include "gestos.h"

using namespace geolora;

void setUp() {}
void tearDown() {}

/* Avanza el reloj de `ms` en pasos de 1 ms con el botón en `pulsado` y
 * devuelve el PRIMER gesto que aparezca (NINGUNO si no aparece ninguno).
 * `t` se deja donde quedó. */
static Gesto avanzar(Gestos& g, uint32_t& t, uint32_t ms, bool pulsado, uint32_t* tGesto = nullptr) {
  Gesto salida = Gesto::NINGUNO;
  for (uint32_t i = 0; i < ms; ++i) {
    ++t;
    const Gesto x = g.update(t, pulsado);
    if (x != Gesto::NINGUNO && salida == Gesto::NINGUNO) {
      salida = x;
      if (tGesto != nullptr) *tGesto = t;
    }
  }
  return salida;
}

/* Un Gestos recién arrancado con el botón suelto y ya estabilizado. */
static void arrancar(Gestos& g, uint32_t& t) {
  t = 1000;
  g.begin(false, t);
  avanzar(g, t, 200, false);
}

/* ==========================================================================
 *  Pulsación corta
 * ========================================================================*/
void test_corta() {
  Gestos g;
  uint32_t t = 0;
  arrancar(g, t);
  /* Pulsar 200 ms (más que el antirrebote, menos que la larga) y soltar. */
  TEST_ASSERT_EQUAL(Gesto::NINGUNO, avanzar(g, t, 200, true));
  /* Al soltar todavía no se emite: hay que esperar la ventana de doble. */
  TEST_ASSERT_EQUAL(Gesto::NINGUNO, avanzar(g, t, 100, false));
  /* Cerrada la ventana de 400 ms, sale la CORTA. */
  TEST_ASSERT_EQUAL(Gesto::CORTA, avanzar(g, t, 500, false));
  /* Y no se repite. */
  TEST_ASSERT_EQUAL(Gesto::NINGUNO, avanzar(g, t, 1000, false));
}

void test_corta_muy_breve_pero_estable() {
  Gestos g;
  uint32_t t = 0;
  arrancar(g, t);
  /* 60 ms pulsado: pasa el antirrebote de 50 ms por los pelos. */
  avanzar(g, t, 60, true);
  TEST_ASSERT_EQUAL(Gesto::CORTA, avanzar(g, t, 600, false));
}

void test_pulsacion_mas_corta_que_el_antirrebote_no_cuenta() {
  Gestos g;
  uint32_t t = 0;
  arrancar(g, t);
  /* 30 ms: es ruido, no llega a nivel estable. */
  avanzar(g, t, 30, true);
  TEST_ASSERT_EQUAL(Gesto::NINGUNO, avanzar(g, t, 1000, false));
  TEST_ASSERT_FALSE(g.pulsadoFiltrado());
}

/* ==========================================================================
 *  Pulsación larga: se emite EN EL INSTANTE de los 1.5 s, sin soltar
 * ========================================================================*/
void test_larga_en_el_instante_exacto() {
  Gestos g;
  uint32_t t = 0;
  arrancar(g, t);
  const uint32_t tPulsa = t + 1;
  uint32_t tGesto = 0;
  /* 2 s pulsado: la LARGA sale a los 1500 ms del flanco filtrado, que se
   * detecta 50 ms (antirrebote) después de tocar el botón. */
  TEST_ASSERT_EQUAL(Gesto::LARGA, avanzar(g, t, 2000, true, &tGesto));
  TEST_ASSERT_UINT32_WITHIN(5, tPulsa + 50 + 1500, tGesto);
  /* Mientras no se suelte, nada más. */
  TEST_ASSERT_EQUAL(Gesto::NINGUNO, avanzar(g, t, 3000, true));
  /* Al soltar tampoco sale una CORTA de propina. */
  TEST_ASSERT_EQUAL(Gesto::NINGUNO, avanzar(g, t, 1000, false));
}

void test_justo_por_debajo_de_larga_es_corta() {
  Gestos g;
  uint32_t t = 0;
  arrancar(g, t);
  /* 1.4 s (contando el antirrebote): todavía es corta. */
  TEST_ASSERT_EQUAL(Gesto::NINGUNO, avanzar(g, t, 1400, true));
  TEST_ASSERT_EQUAL(Gesto::CORTA, avanzar(g, t, 600, false));
}

/* ==========================================================================
 *  Doble pulsación: se emite EN EL FLANCO de la segunda
 * ========================================================================*/
void test_doble() {
  Gestos g;
  uint32_t t = 0;
  arrancar(g, t);
  avanzar(g, t, 150, true);                                     // 1.ª
  TEST_ASSERT_EQUAL(Gesto::NINGUNO, avanzar(g, t, 150, false));  // dentro de los 400 ms
  TEST_ASSERT_EQUAL(Gesto::DOBLE, avanzar(g, t, 150, true));     // 2.ª -> DOBLE ya
  /* Tras una doble, nada hasta soltar. */
  TEST_ASSERT_EQUAL(Gesto::NINGUNO, avanzar(g, t, 500, true));
  TEST_ASSERT_EQUAL(Gesto::NINGUNO, avanzar(g, t, 1000, false));
}

void test_segunda_pulsacion_demasiado_tarde_son_dos_cortas() {
  Gestos g;
  uint32_t t = 0;
  arrancar(g, t);
  avanzar(g, t, 150, true);
  /* 500 ms de pausa: la ventana de 400 ms se cierra y sale la 1.ª CORTA. */
  TEST_ASSERT_EQUAL(Gesto::CORTA, avanzar(g, t, 500, false));
  avanzar(g, t, 150, true);
  TEST_ASSERT_EQUAL(Gesto::CORTA, avanzar(g, t, 600, false));   // y luego la 2.ª
}

void test_larga_despues_de_una_doble_no_se_mezclan() {
  Gestos g;
  uint32_t t = 0;
  arrancar(g, t);
  avanzar(g, t, 150, true);
  avanzar(g, t, 150, false);
  TEST_ASSERT_EQUAL(Gesto::DOBLE, avanzar(g, t, 100, true));
  /* Seguir pulsando 3 s tras una doble NO produce una larga. */
  TEST_ASSERT_EQUAL(Gesto::NINGUNO, avanzar(g, t, 3000, true));
  TEST_ASSERT_EQUAL(Gesto::NINGUNO, avanzar(g, t, 600, false));
  /* Ahora sí, una larga nueva desde reposo. */
  TEST_ASSERT_EQUAL(Gesto::LARGA, avanzar(g, t, 2000, true));
}

/* ==========================================================================
 *  Rebotes
 * ========================================================================*/
void test_rebotes_al_pulsar_no_generan_dobles() {
  Gestos g;
  uint32_t t = 0;
  arrancar(g, t);
  /* Tren de rebotes de 5 ms durante 40 ms y luego pulsación limpia. */
  for (int i = 0; i < 4; ++i) {
    avanzar(g, t, 5, true);
    avanzar(g, t, 5, false);
  }
  avanzar(g, t, 200, true);
  TEST_ASSERT_EQUAL(Gesto::CORTA, avanzar(g, t, 600, false));   // UNA sola corta
  TEST_ASSERT_TRUE(g.rebotes() > 0);                            // y se contaron
}

void test_rebotes_al_soltar_no_parten_la_larga() {
  Gestos g;
  uint32_t t = 0;
  arrancar(g, t);
  /* Pulsación larga con micro-cortes de 10 ms cada 300 ms: el antirrebote de
   * 50 ms los absorbe y la LARGA sale igual. */
  Gesto visto = Gesto::NINGUNO;
  for (int i = 0; i < 8 && visto == Gesto::NINGUNO; ++i) {
    Gesto a = avanzar(g, t, 290, true);
    Gesto b = avanzar(g, t, 10, false);
    if (a != Gesto::NINGUNO) visto = a;
    else if (b != Gesto::NINGUNO) visto = b;
  }
  TEST_ASSERT_EQUAL(Gesto::LARGA, visto);
}

/* ==========================================================================
 *  Arranque con el botón ya pulsado (GPIO0 es pin de arranque, y el DTR del
 *  monitor serie lo baja: §2.3 y §10).
 * ========================================================================*/
void test_boton_pulsado_al_arrancar_no_cuenta_hasta_soltar() {
  Gestos g;
  uint32_t t = 1000;
  g.begin(true, t);
  /* 5 s "pulsado" desde el arranque: ni larga ni nada. */
  TEST_ASSERT_EQUAL(Gesto::NINGUNO, avanzar(g, t, 5000, true));
  /* Al soltar tampoco sale una corta. */
  TEST_ASSERT_EQUAL(Gesto::NINGUNO, avanzar(g, t, 1000, false));
  /* Desde aquí ya funciona con normalidad. */
  avanzar(g, t, 200, true);
  TEST_ASSERT_EQUAL(Gesto::CORTA, avanzar(g, t, 600, false));
}

/* ==========================================================================
 *  Tiempos configurables y desbordamiento de millis()
 * ========================================================================*/
void test_tiempos_configurables() {
  Gestos g(ConfigGestos(10, 500, 150));   // antirrebote 10, larga 500, doble 150
  uint32_t t = 0;
  t = 1000;
  g.begin(false, t);
  avanzar(g, t, 50, false);
  TEST_ASSERT_EQUAL(Gesto::LARGA, avanzar(g, t, 700, true));
  avanzar(g, t, 100, false);
  avanzar(g, t, 60, true);
  TEST_ASSERT_EQUAL(Gesto::CORTA, avanzar(g, t, 250, false));
}

void test_vuelta_de_millis() {
  /* millis() da la vuelta a los 49 días. Con resta unsigned el gesto sale
   * igual: se arranca justo antes del desbordamiento. */
  Gestos g;
  const uint32_t tIni = 0xFFFFFF00u;   // 256 ms antes de la vuelta
  uint32_t t = tIni;
  g.begin(false, t);
  avanzar(g, t, 200, false);
  uint32_t tGesto = 0;
  const uint32_t tPulsa = t + 1;
  TEST_ASSERT_EQUAL(Gesto::LARGA, avanzar(g, t, 2000, true, &tGesto));
  TEST_ASSERT_UINT32_WITHIN(5, static_cast<uint32_t>(tPulsa + 50 + 1500), tGesto);
  TEST_ASSERT_TRUE(t < tIni);   // el reloj dio la vuelta durante la prueba
}

void test_nombre_gesto() {
  TEST_ASSERT_EQUAL_STRING("CORTA", nombreGesto(Gesto::CORTA));
  TEST_ASSERT_EQUAL_STRING("DOBLE", nombreGesto(Gesto::DOBLE));
  TEST_ASSERT_EQUAL_STRING("LARGA", nombreGesto(Gesto::LARGA));
  TEST_ASSERT_NOT_NULL(nombreGesto(Gesto::NINGUNO));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_corta);
  RUN_TEST(test_corta_muy_breve_pero_estable);
  RUN_TEST(test_pulsacion_mas_corta_que_el_antirrebote_no_cuenta);
  RUN_TEST(test_larga_en_el_instante_exacto);
  RUN_TEST(test_justo_por_debajo_de_larga_es_corta);
  RUN_TEST(test_doble);
  RUN_TEST(test_segunda_pulsacion_demasiado_tarde_son_dos_cortas);
  RUN_TEST(test_larga_despues_de_una_doble_no_se_mezclan);
  RUN_TEST(test_rebotes_al_pulsar_no_generan_dobles);
  RUN_TEST(test_rebotes_al_soltar_no_parten_la_larga);
  RUN_TEST(test_boton_pulsado_al_arrancar_no_cuenta_hasta_soltar);
  RUN_TEST(test_tiempos_configurables);
  RUN_TEST(test_vuelta_de_millis);
  RUN_TEST(test_nombre_gesto);
  return UNITY_END();
}
