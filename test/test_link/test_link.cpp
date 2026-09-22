/* ============================================================================
 *  Pruebas del protocolo GEO-LINK (lib/link)
 * ----------------------------------------------------------------------------
 *  Corren en el PC, sin placas y sin radio:
 *      pio test -e native
 *
 *  Lo que se comprueba aquí es justo lo que NO se puede comprobar mirando dos
 *  placas parpadear: que una trama sobrevive al viaje de ida y vuelta, que un
 *  bit cambiado se detecta, que una trama grabada del aire no cuela dos veces
 *  y que una placa con otra clave no puede dar órdenes.
 * ==========================================================================*/
#include <unity.h>

#include <string.h>

#include "link.h"

static link::Key KEY;
static link::Key OTHER_KEY;

void setUp(void) {
  KEY       = link::keyFromString("geo-expo-2026");
  OTHER_KEY = link::keyFromString("otra-clave");
}
void tearDown(void) {}

/* Trama de ejemplo con todos los campos distintos de cero, para que un campo
 * intercambiado con otro salte a la vista. */
static link::Frame sampleFrame() {
  link::Frame f;
  f.type    = link::Type::ALERT;
  f.src     = 0x1234;
  f.dst     = 0x9ABC;
  f.seq     = 0x5678;
  f.flags   = link::Flag::ALERT_ACTIVE | link::Flag::HAS_FIX;
  f.battery = 87;
  f.lat1e7  = 42057600;     // 4,205760  (Bogotá)
  f.lon1e7  = -740946480;   // -74,094648
  f.aux0    = 2;            // ALERT:2
  f.aux1    = 0x77;
  return f;
}

/* ---------------------------------------------------------------- formato -*/
static void test_tamano_de_trama_fijo(void) {
  uint8_t buf[64];
  memset(buf, 0, sizeof(buf));
  TEST_ASSERT_EQUAL_UINT32(28u, (uint32_t)link::FRAME_SIZE);
  TEST_ASSERT_EQUAL_UINT32((uint32_t)link::FRAME_SIZE,
                           (uint32_t)link::encode(sampleFrame(), KEY, buf, sizeof(buf)));
}

static void test_no_escribe_si_no_cabe(void) {
  uint8_t buf[link::FRAME_SIZE - 1];
  memset(buf, 0xAA, sizeof(buf));
  TEST_ASSERT_EQUAL_UINT32(0u, (uint32_t)link::encode(sampleFrame(), KEY, buf, sizeof(buf)));
  TEST_ASSERT_EQUAL_UINT8(0xAA, buf[0]);   // ni un byte tocado
}

static void test_ida_y_vuelta_conserva_todos_los_campos(void) {
  uint8_t buf[link::FRAME_SIZE];
  const link::Frame in = sampleFrame();
  TEST_ASSERT_EQUAL_UINT32((uint32_t)link::FRAME_SIZE,
                           (uint32_t)link::encode(in, KEY, buf, sizeof(buf)));

  link::Frame out;
  TEST_ASSERT_TRUE(link::decode(buf, sizeof(buf), KEY, &out) == link::Result::OK);
  TEST_ASSERT_TRUE(out.type == in.type);
  TEST_ASSERT_EQUAL_UINT16(in.src, out.src);
  TEST_ASSERT_EQUAL_UINT16(in.dst, out.dst);
  TEST_ASSERT_EQUAL_UINT16(in.seq, out.seq);
  TEST_ASSERT_EQUAL_UINT8(in.flags, out.flags);
  TEST_ASSERT_EQUAL_UINT8(in.battery, out.battery);
  TEST_ASSERT_EQUAL_INT32(in.lat1e7, out.lat1e7);
  TEST_ASSERT_EQUAL_INT32(in.lon1e7, out.lon1e7);
  TEST_ASSERT_EQUAL_UINT8(in.aux0, out.aux0);
  TEST_ASSERT_EQUAL_UINT8(in.aux1, out.aux1);
}

static void test_coordenadas_negativas_sobreviven(void) {
  /* El oeste y el sur son negativos: si el entero se tratara como sin signo,
   * el llavero aparecería en China. */
  uint8_t buf[link::FRAME_SIZE];
  link::Frame in = sampleFrame();
  in.lat1e7 = -123456789;
  in.lon1e7 = -740946480;
  link::encode(in, KEY, buf, sizeof(buf));

  link::Frame out;
  TEST_ASSERT_TRUE(link::decode(buf, sizeof(buf), KEY, &out) == link::Result::OK);
  TEST_ASSERT_EQUAL_INT32(-123456789, out.lat1e7);
  TEST_ASSERT_EQUAL_INT32(-740946480, out.lon1e7);
}

static void test_conversion_de_grados(void) {
  TEST_ASSERT_EQUAL_INT32(42057600, link::degToFixed(4.20576));
  TEST_ASSERT_EQUAL_INT32(-740946480, link::degToFixed(-74.094648));
  TEST_ASSERT_DOUBLE_WITHIN(1e-7, 4.20576, link::fixedToDeg(42057600));
  TEST_ASSERT_DOUBLE_WITHIN(1e-7, -74.094648, link::fixedToDeg(-740946480));
  // El redondeo no debe sesgar los negativos hacia el ecuador.
  TEST_ASSERT_EQUAL_INT32(-5, link::degToFixed(-0.00000045));
  TEST_ASSERT_EQUAL_INT32(5, link::degToFixed(0.00000045));
}

/* ------------------------------------------------------------- validación -*/
static void test_rechaza_tamano_incorrecto(void) {
  uint8_t buf[link::FRAME_SIZE];
  link::encode(sampleFrame(), KEY, buf, sizeof(buf));
  link::Frame out;
  TEST_ASSERT_TRUE(link::decode(buf, link::FRAME_SIZE - 1, KEY, &out) == link::Result::BAD_SIZE);
  TEST_ASSERT_TRUE(link::decode(buf, link::FRAME_SIZE + 1, KEY, &out) == link::Result::BAD_SIZE);
  TEST_ASSERT_TRUE(link::decode(buf, 0, KEY, &out) == link::Result::BAD_SIZE);
}

static void test_rechaza_ruido_que_no_es_del_protocolo(void) {
  uint8_t ruido[link::FRAME_SIZE];
  memset(ruido, 0x5A, sizeof(ruido));
  link::Frame out;
  TEST_ASSERT_TRUE(link::decode(ruido, sizeof(ruido), KEY, &out) == link::Result::BAD_MAGIC);
}

static void test_rechaza_otra_version(void) {
  uint8_t buf[link::FRAME_SIZE];
  link::encode(sampleFrame(), KEY, buf, sizeof(buf));
  buf[1] = (uint8_t)((2 << 4) | ((uint8_t)link::Type::ALERT));  // versión 2
  link::Frame out;
  TEST_ASSERT_TRUE(link::decode(buf, sizeof(buf), KEY, &out) == link::Result::BAD_VERSION);
}

static void test_rechaza_tipo_desconocido(void) {
  uint8_t buf[link::FRAME_SIZE];
  link::encode(sampleFrame(), KEY, buf, sizeof(buf));
  buf[1] = (uint8_t)((link::VERSION << 4) | 0x0D);   // tipo 13: no existe
  link::Frame out;
  TEST_ASSERT_TRUE(link::decode(buf, sizeof(buf), KEY, &out) == link::Result::BAD_TYPE);
}

static void test_un_bit_cambiado_invalida_la_trama(void) {
  uint8_t buf[link::FRAME_SIZE];
  link::encode(sampleFrame(), KEY, buf, sizeof(buf));

  // Se recorre TODA la parte firmada cambiando un bit cada vez.
  for (size_t i = 0; i < 20; ++i) {
    uint8_t copia[link::FRAME_SIZE];
    memcpy(copia, buf, sizeof(copia));
    copia[i] ^= 0x01;
    link::Frame out;
    const link::Result r = link::decode(copia, sizeof(copia), KEY, &out);
    TEST_ASSERT_TRUE(r != link::Result::OK);
  }
}

static void test_clave_distinta_no_cuela(void) {
  uint8_t buf[link::FRAME_SIZE];
  link::encode(sampleFrame(), OTHER_KEY, buf, sizeof(buf));
  link::Frame out;
  TEST_ASSERT_TRUE(link::decode(buf, sizeof(buf), KEY, &out) == link::Result::BAD_AUTH);
}

static void test_trama_invalida_no_toca_la_salida(void) {
  uint8_t buf[link::FRAME_SIZE];
  link::encode(sampleFrame(), OTHER_KEY, buf, sizeof(buf));

  link::Frame out;
  out.src = 0xBEEF;
  out.seq = 0x0042;
  TEST_ASSERT_TRUE(link::decode(buf, sizeof(buf), KEY, &out) == link::Result::BAD_AUTH);
  TEST_ASSERT_EQUAL_UINT16(0xBEEF, out.src);     // intacta
  TEST_ASSERT_EQUAL_UINT16(0x0042, out.seq);
}

static void test_claves_de_frases_distintas_son_distintas(void) {
  const link::Key a = link::keyFromString("expo");
  const link::Key b = link::keyFromString("expo ");
  TEST_ASSERT_TRUE(memcmp(a.b, b.b, sizeof(a.b)) != 0);

  // Y la misma frase da siempre la misma clave (si no, no se hablarían).
  const link::Key c = link::keyFromString("expo");
  TEST_ASSERT_EQUAL_UINT8_ARRAY(a.b, c.b, sizeof(a.b));

  // Una frase corta debe repartirse por los 16 bytes, no dejar medio a cero.
  uint8_t ceros = 0;
  for (size_t i = 0; i < sizeof(a.b); ++i) if (a.b[i] == 0) ++ceros;
  TEST_ASSERT_TRUE(ceros < 8);
}

/* ---------------------------------------------------------- flags y ayudas -*/
static void test_flags(void) {
  link::Frame f;
  TEST_ASSERT_FALSE(f.has(link::Flag::ALERT_ACTIVE));
  f.set(link::Flag::ALERT_ACTIVE, true);
  f.set(link::Flag::LOW_BATTERY, true);
  TEST_ASSERT_TRUE(f.has(link::Flag::ALERT_ACTIVE));
  TEST_ASSERT_TRUE(f.has(link::Flag::LOW_BATTERY));
  f.set(link::Flag::ALERT_ACTIVE, false);
  TEST_ASSERT_FALSE(f.has(link::Flag::ALERT_ACTIVE));
  TEST_ASSERT_TRUE(f.has(link::Flag::LOW_BATTERY));   // no se lleva por delante al vecino
}

/* ------------------------------------------------------- antirrepetición --*/
static void test_secuencia_circular(void) {
  TEST_ASSERT_TRUE(link::seqNewer(2, 1));
  TEST_ASSERT_FALSE(link::seqNewer(1, 2));
  TEST_ASSERT_FALSE(link::seqNewer(7, 7));           // la misma no es más nueva
  TEST_ASSERT_TRUE(link::seqNewer(0, 65535));        // el desbordamiento es normal
  TEST_ASSERT_FALSE(link::seqNewer(65535, 0));
}

static void test_guardia_acepta_lo_nuevo_y_rechaza_lo_repetido(void) {
  link::ReplayGuard g;
  g.reset();
  TEST_ASSERT_TRUE(g.accept(1, 100));     // emisor nuevo
  TEST_ASSERT_TRUE(g.accept(1, 101));
  TEST_ASSERT_FALSE(g.accept(1, 101));    // repetida: la grabación no cuela
  TEST_ASSERT_FALSE(g.accept(1, 50));     // vieja
  TEST_ASSERT_TRUE(g.accept(1, 102));
  TEST_ASSERT_EQUAL_UINT16(2, g.rejected());
}

static void test_guardia_separa_emisores(void) {
  link::ReplayGuard g;
  g.reset();
  TEST_ASSERT_TRUE(g.accept(1, 500));
  TEST_ASSERT_TRUE(g.accept(2, 3));       // otro emisor, su propio contador
  TEST_ASSERT_TRUE(g.accept(2, 4));
  TEST_ASSERT_FALSE(g.accept(2, 4));
  TEST_ASSERT_TRUE(g.accept(1, 501));
}

static void test_guardia_sobrevive_al_desbordamiento(void) {
  link::ReplayGuard g;
  g.reset();
  TEST_ASSERT_TRUE(g.accept(9, 65534));
  TEST_ASSERT_TRUE(g.accept(9, 65535));
  TEST_ASSERT_TRUE(g.accept(9, 0));       // 65535 -> 0 es progreso, no ataque
  TEST_ASSERT_TRUE(g.accept(9, 1));
  TEST_ASSERT_FALSE(g.accept(9, 65535));  // eso sí es ir hacia atrás
}

static void test_guardia_olvida_a_quien_se_le_pide(void) {
  link::ReplayGuard g;
  g.reset();
  TEST_ASSERT_TRUE(g.accept(5, 900));
  TEST_ASSERT_FALSE(g.accept(5, 10));     // el llavero se reinició: seq bajo
  g.forget(5);                            // se le perdona tras el reinicio
  TEST_ASSERT_TRUE(g.accept(5, 10));
}

/* ---------------------------------------------------- caso de uso completo -*/
static void test_orden_falsificada_no_apaga_la_alerta(void) {
  /* Escenario real: alguien con otra Heltec ve el tráfico y quiere mandar
   * FIND_OFF para que el llavero deje de pitar. Sin la clave, no puede. */
  link::Frame orden;
  orden.type = link::Type::CMD;
  orden.src  = 0x0002;
  orden.dst  = 0x0001;
  orden.seq  = 7;
  orden.aux0 = (uint8_t)link::Cmd::FIND_OFF;

  uint8_t buf[link::FRAME_SIZE];
  link::encode(orden, OTHER_KEY, buf, sizeof(buf));

  link::Frame out;
  TEST_ASSERT_TRUE(link::decode(buf, sizeof(buf), KEY, &out) == link::Result::BAD_AUTH);

  // Con la clave buena, la misma orden sí pasa.
  link::encode(orden, KEY, buf, sizeof(buf));
  TEST_ASSERT_TRUE(link::decode(buf, sizeof(buf), KEY, &out) == link::Result::OK);
  TEST_ASSERT_TRUE((link::Cmd)out.aux0 == link::Cmd::FIND_OFF);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_tamano_de_trama_fijo);
  RUN_TEST(test_no_escribe_si_no_cabe);
  RUN_TEST(test_ida_y_vuelta_conserva_todos_los_campos);
  RUN_TEST(test_coordenadas_negativas_sobreviven);
  RUN_TEST(test_conversion_de_grados);
  RUN_TEST(test_rechaza_tamano_incorrecto);
  RUN_TEST(test_rechaza_ruido_que_no_es_del_protocolo);
  RUN_TEST(test_rechaza_otra_version);
  RUN_TEST(test_rechaza_tipo_desconocido);
  RUN_TEST(test_un_bit_cambiado_invalida_la_trama);
  RUN_TEST(test_clave_distinta_no_cuela);
  RUN_TEST(test_trama_invalida_no_toca_la_salida);
  RUN_TEST(test_claves_de_frases_distintas_son_distintas);
  RUN_TEST(test_flags);
  RUN_TEST(test_secuencia_circular);
  RUN_TEST(test_guardia_acepta_lo_nuevo_y_rechaza_lo_repetido);
  RUN_TEST(test_guardia_separa_emisores);
  RUN_TEST(test_guardia_sobrevive_al_desbordamiento);
  RUN_TEST(test_guardia_olvida_a_quien_se_le_pide);
  RUN_TEST(test_orden_falsificada_no_apaga_la_alerta);
  return UNITY_END();
}
