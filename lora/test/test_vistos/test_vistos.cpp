/* ============================================================================
 *  Pruebas de la caché de vistos (lib/geolora/vistos) — DISENO §7.4, §8.1
 * ============================================================================
 *      cd lora && pio test -e native
 *
 *  Esta caché hace dos trabajos y los dos son críticos:
 *
 *    · en E1 impide ENTREGAR DOS VECES un mensaje cuyo ACK se perdió y que el
 *      emisor reintentó (§7.1);
 *    · en E2 es lo único que corta los BUCLES de la inundación (§8.1).
 *
 *  Si se llena y expulsa mal, o si la aritmética de millis() se rompe al dar
 *  la vuelta (49 días), las dos cosas fallan a la vez. De ahí el detalle.
 * ==========================================================================*/
#include <unity.h>

#include "vistos.h"

using namespace geolora;

void setUp() {}
void tearDown() {}

static const uint32_t VENTANA = 2000;   // ventana de inundación típica en NORMAL

/* ==========================================================================
 *  Insertar y buscar
 * ========================================================================*/
void test_vacia_no_contiene_nada() {
  Vistos v;
  TEST_ASSERT_FALSE(v.contiene(0x1A2B, 1, 1000));
  TEST_ASSERT_EQUAL_INT(-1, v.buscar(0x1A2B, 1, 1000));
  TEST_ASSERT_EQUAL_size_t(0, v.vivas(1000));
  TEST_ASSERT_EQUAL_UINT32(0, v.reemplazos());
}

void test_anadir_y_contener() {
  Vistos v;
  const int i = v.anadir(0x1A2B, 7, 1000, VENTANA);
  TEST_ASSERT_TRUE(i >= 0);
  TEST_ASSERT_TRUE(v.contiene(0x1A2B, 7, 1000));
  TEST_ASSERT_EQUAL_INT(i, v.buscar(0x1A2B, 7, 1000));
  TEST_ASSERT_EQUAL_size_t(1, v.vivas(1000));
  /* La clave es el PAR (src, msgId): cambiar cualquiera de los dos es otra. */
  TEST_ASSERT_FALSE(v.contiene(0x1A2B, 8, 1000));
  TEST_ASSERT_FALSE(v.contiene(0x1A2C, 7, 1000));
  /* Y la ventana de inundación se guarda. */
  TEST_ASSERT_EQUAL_UINT32(1000 + VENTANA, v.entrada(i).tFinVentana);
}

void test_varias_claves_no_se_pisan() {
  Vistos v;
  for (uint16_t n = 0; n < 20; ++n) v.anadir(0x0100, n, 1000 + n, VENTANA);
  for (uint16_t n = 0; n < 20; ++n) TEST_ASSERT_TRUE(v.contiene(0x0100, n, 1020));
  TEST_ASSERT_EQUAL_size_t(20, v.vivas(1020));
  TEST_ASSERT_EQUAL_UINT32(0, v.reemplazos());   // 20 caben de sobra en 128
}

void test_renovar_actualiza_el_instante_y_la_ventana() {
  Vistos v;
  const int i = v.anadir(0x1A2B, 7, 1000, VENTANA);
  const int j = v.anadir(0x1A2B, 7, 500000, 5000);
  TEST_ASSERT_EQUAL_INT(i, j);                   // la MISMA entrada, no una nueva
  TEST_ASSERT_EQUAL_size_t(1, v.vivas(500000));
  TEST_ASSERT_EQUAL_UINT32(500000, v.entrada(i).tVisto);
  TEST_ASSERT_EQUAL_UINT32(505000, v.entrada(i).tFinVentana);
}

void test_limpiar() {
  Vistos v;
  v.anadir(1, 1, 1000, VENTANA);
  v.anadir(2, 2, 1000, VENTANA);
  v.limpiar();
  TEST_ASSERT_EQUAL_size_t(0, v.vivas(1000));
  TEST_ASSERT_FALSE(v.contiene(1, 1, 1000));
}

/* ==========================================================================
 *  Caducidad a los 15 min (§7.4)
 * ------------------------------------------------------------------------
 *  Tiene que ser MAYOR que los 10 min que insiste una ALERT (§7.3): si no,
 *  un reintento tardío se entregaría por segunda vez.
 * ========================================================================*/
void test_vida_es_mayor_que_la_insistencia_de_una_alerta() {
  TEST_ASSERT_EQUAL_UINT32(15UL * 60UL * 1000UL, Vistos::VIDA_MS);
  TEST_ASSERT_TRUE(Vistos::VIDA_MS > 600000UL);   // ALERTA_MAX_MS
}

void test_caduca_a_los_quince_minutos() {
  Vistos v;
  v.anadir(0x1A2B, 7, 1000, VENTANA);
  TEST_ASSERT_TRUE(v.contiene(0x1A2B, 7, 1000 + Vistos::VIDA_MS - 1));
  TEST_ASSERT_FALSE(v.contiene(0x1A2B, 7, 1000 + Vistos::VIDA_MS));
  TEST_ASSERT_EQUAL_size_t(0, v.vivas(1000 + Vistos::VIDA_MS));
}

void test_la_entrada_caducada_libera_su_hueco() {
  Vistos v;
  /* Llenar del todo y dejar que caduque: la caché tiene que volver a estar
   * vacía sin necesidad de expulsar nada. */
  for (uint16_t n = 0; n < Vistos::CAPACIDAD; ++n) v.anadir(0x0100, n, 1000, VENTANA);
  TEST_ASSERT_EQUAL_size_t(Vistos::CAPACIDAD, v.vivas(1000));
  const uint32_t despues = 1000 + Vistos::VIDA_MS + 1;
  TEST_ASSERT_EQUAL_size_t(0, v.vivas(despues));
  v.anadir(0x0200, 1, despues, VENTANA);
  TEST_ASSERT_EQUAL_size_t(1, v.vivas(despues));
  TEST_ASSERT_EQUAL_UINT32(0, v.reemplazos());   // caducar no es reemplazar
}

/* ==========================================================================
 *  Llena: se expulsa la MÁS ANTIGUA (§7.4)
 * ========================================================================*/
void test_llena_expulsa_la_mas_antigua() {
  Vistos v;
  /* 128 entradas, cada una 10 ms más nueva que la anterior. */
  for (uint16_t n = 0; n < Vistos::CAPACIDAD; ++n) {
    v.anadir(0x0100, n, 1000 + 10u * n, VENTANA);
  }
  const uint32_t t = 1000 + 10u * Vistos::CAPACIDAD;
  TEST_ASSERT_EQUAL_size_t(Vistos::CAPACIDAD, v.vivas(t));
  /* Una más: cae la n = 0, que es la más vieja; el resto sigue. */
  v.anadir(0x0200, 999, t, VENTANA);
  TEST_ASSERT_EQUAL_UINT32(1, v.reemplazos());
  TEST_ASSERT_FALSE(v.contiene(0x0100, 0, t));
  TEST_ASSERT_TRUE(v.contiene(0x0100, 1, t));
  TEST_ASSERT_TRUE(v.contiene(0x0100, Vistos::CAPACIDAD - 1, t));
  TEST_ASSERT_TRUE(v.contiene(0x0200, 999, t));
  TEST_ASSERT_EQUAL_size_t(Vistos::CAPACIDAD, v.vivas(t));
  /* Otra más: cae la n = 1. */
  v.anadir(0x0200, 1000, t, VENTANA);
  TEST_ASSERT_EQUAL_UINT32(2, v.reemplazos());
  TEST_ASSERT_FALSE(v.contiene(0x0100, 1, t));
  TEST_ASSERT_TRUE(v.contiene(0x0100, 2, t));
}

void test_renovar_protege_de_la_expulsion() {
  Vistos v;
  for (uint16_t n = 0; n < Vistos::CAPACIDAD; ++n) v.anadir(0x0100, n, 1000 + 10u * n, VENTANA);
  uint32_t t = 1000 + 10u * Vistos::CAPACIDAD;
  /* Renovar la más vieja la pone al final de la cola de expulsión. */
  v.anadir(0x0100, 0, t, VENTANA);
  t += 10;
  v.anadir(0x0200, 999, t, VENTANA);
  TEST_ASSERT_TRUE(v.contiene(0x0100, 0, t));    // sobrevive
  TEST_ASSERT_FALSE(v.contiene(0x0100, 1, t));   // ahora la víctima es la 1
}

void test_capacidad_es_la_documentada() {
  TEST_ASSERT_EQUAL_UINT16(128, Vistos::CAPACIDAD);
}

/* ==========================================================================
 *  Aritmética de millis() y de msgId
 * ========================================================================*/
void test_vuelta_de_millis() {
  /* Se añade 1 s antes del desbordamiento de los 49 días y se consulta
   * después: con resta unsigned la edad sale bien (§7.4). */
  Vistos v;
  const uint32_t t0 = 0xFFFFFFFFu - 1000u;
  v.anadir(0x1A2B, 7, t0, VENTANA);
  TEST_ASSERT_TRUE(v.contiene(0x1A2B, 7, t0));
  TEST_ASSERT_TRUE(v.contiene(0x1A2B, 7, 500));            // ya dio la vuelta
  TEST_ASSERT_TRUE(v.contiene(0x1A2B, 7, 1000 + 60000));   // un minuto después
  /* Y caduca a los 15 min de reloj, no antes ni "para siempre". */
  const uint32_t tCad = static_cast<uint32_t>(t0 + Vistos::VIDA_MS);
  TEST_ASSERT_TRUE(v.contiene(0x1A2B, 7, tCad - 1));
  TEST_ASSERT_FALSE(v.contiene(0x1A2B, 7, tCad));
}

void test_vuelta_de_millis_en_la_expulsion() {
  /* Con la caché llena a caballo del desbordamiento, la víctima sigue siendo
   * la de MÁS EDAD, no la del número de millis() más bajo. */
  Vistos v;
  const uint32_t t0 = 0xFFFFFFFFu - 640u;   // 128 entradas cada 10 ms cruzan la vuelta
  for (uint16_t n = 0; n < Vistos::CAPACIDAD; ++n) {
    v.anadir(0x0100, n, static_cast<uint32_t>(t0 + 10u * n), VENTANA);
  }
  const uint32_t t = static_cast<uint32_t>(t0 + 10u * Vistos::CAPACIDAD);
  v.anadir(0x0200, 999, t, VENTANA);
  TEST_ASSERT_EQUAL_UINT32(1, v.reemplazos());
  TEST_ASSERT_FALSE(v.contiene(0x0100, 0, t));     // la más vieja, aunque su t
  TEST_ASSERT_TRUE(v.contiene(0x0100, 64, t));     // sea el número MÁS ALTO
  TEST_ASSERT_TRUE(v.contiene(0x0100, Vistos::CAPACIDAD - 1, t));
}

void test_vuelta_de_msgid() {
  /* msgId es un u16 que da la vuelta: 0xFFFF y 0x0000 son claves DISTINTAS
   * y las dos tienen que convivir (§7.4). */
  Vistos v;
  v.anadir(0x1A2B, 0xFFFE, 1000, VENTANA);
  v.anadir(0x1A2B, 0xFFFF, 1010, VENTANA);
  v.anadir(0x1A2B, 0x0000, 1020, VENTANA);
  v.anadir(0x1A2B, 0x0001, 1030, VENTANA);
  TEST_ASSERT_EQUAL_size_t(4, v.vivas(1030));
  TEST_ASSERT_TRUE(v.contiene(0x1A2B, 0xFFFF, 1030));
  TEST_ASSERT_TRUE(v.contiene(0x1A2B, 0x0000, 1030));
}

void test_src_reservado_o_cero_se_guarda_igual() {
  /* La caché no juzga: guarda lo que le den. Quien filtra los src reservados
   * es decodificar() (§5.1). Así la caché puede usarse también para las
   * tramas PROPIAS, que es lo que hace el motor (§8.1 regla 1). */
  Vistos v;
  v.anadir(0x0000, 1, 1000, VENTANA);
  v.anadir(0xFFFF, 1, 1000, VENTANA);
  TEST_ASSERT_TRUE(v.contiene(0x0000, 1, 1000));
  TEST_ASSERT_TRUE(v.contiene(0xFFFF, 1, 1000));
}

/* ==========================================================================
 *  Ventana de inundación (añadido de la implementación, ver README)
 * ========================================================================*/
void test_la_ventana_es_independiente_de_la_vida() {
  Vistos v;
  const int i = v.anadir(0x1A2B, 7, 1000, VENTANA);
  /* Pasada la ventana la entrada sigue VIVA (la vida son 15 min): lo que
   * cambia es que una copia reenviada ya no es "de la misma inundación". */
  const uint32_t t = 1000 + VENTANA + 1;
  TEST_ASSERT_TRUE(v.contiene(0x1A2B, 7, t));
  TEST_ASSERT_TRUE(static_cast<int32_t>(t - v.entrada(i).tFinVentana) > 0);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_vacia_no_contiene_nada);
  RUN_TEST(test_anadir_y_contener);
  RUN_TEST(test_varias_claves_no_se_pisan);
  RUN_TEST(test_renovar_actualiza_el_instante_y_la_ventana);
  RUN_TEST(test_limpiar);
  RUN_TEST(test_vida_es_mayor_que_la_insistencia_de_una_alerta);
  RUN_TEST(test_caduca_a_los_quince_minutos);
  RUN_TEST(test_la_entrada_caducada_libera_su_hueco);
  RUN_TEST(test_llena_expulsa_la_mas_antigua);
  RUN_TEST(test_renovar_protege_de_la_expulsion);
  RUN_TEST(test_capacidad_es_la_documentada);
  RUN_TEST(test_vuelta_de_millis);
  RUN_TEST(test_vuelta_de_millis_en_la_expulsion);
  RUN_TEST(test_vuelta_de_msgid);
  RUN_TEST(test_src_reservado_o_cero_se_guarda_igual);
  RUN_TEST(test_la_ventana_es_independiente_de_la_vida);
  return UNITY_END();
}
