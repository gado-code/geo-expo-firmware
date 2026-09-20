/* ============================================================================
 *  Pruebas de la identidad de nodo (lib/geolora/identidad) — DISENO §6
 * ============================================================================
 *      cd lora && pio test -e native
 *
 *  Las DOS placas llevan el MISMO binario: lo único que las distingue es el
 *  ID derivado de la MAC. Si esta derivación se rompe, las dos placas podrían
 *  arrancar con el mismo ID y la red no funcionaría (todo se vería como "eco
 *  propio", regla 1 del §8.1). Por eso se prueba con MACs inventadas, sin
 *  placa, incluidos los casos en que la MAC cae en un ID RESERVADO.
 * ==========================================================================*/
#include <string.h>
#include <unity.h>

#include "crc16.h"
#include "identidad.h"
#include "paquete.h"

using namespace geolora;

void setUp() {}
void tearDown() {}

static void mac(uint8_t out[6], uint8_t b0, uint8_t b1, uint8_t b2, uint8_t b3, uint8_t b4, uint8_t b5) {
  out[0] = b0; out[1] = b1; out[2] = b2; out[3] = b3; out[4] = b4; out[5] = b5;
}

/* ==========================================================================
 *  Derivación normal: los 2 ÚLTIMOS bytes de la MAC, en el orden en que se
 *  imprime (mac[4] es el byte alto y mac[5] el bajo), para poderlo comprobar
 *  a ojo contra la etiqueta de la placa.
 * ========================================================================*/
void test_derivar_dos_ultimos_bytes() {
  uint8_t m[6];
  mac(m, 0x34, 0x85, 0x18, 0x00, 0x1A, 0x2B);
  TEST_ASSERT_EQUAL_HEX16(0x1A2B, derivarId(m));   // el ejemplo del §6
  mac(m, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF);
  TEST_ASSERT_EQUAL_HEX16(0xEEFF, derivarId(m));
  mac(m, 0, 0, 0, 0, 0x00, 0x01);
  TEST_ASSERT_EQUAL_HEX16(0x0001, derivarId(m));
}

void test_derivar_placas_del_mismo_lote() {
  /* Las MAC de un lote son consecutivas: los IDs tienen que salir distintos,
   * que es la premisa de "el mismo binario en las dos placas" (§6). */
  uint8_t a[6], b[6];
  mac(a, 0x34, 0x85, 0x18, 0x9A, 0x7C, 0x44);
  mac(b, 0x34, 0x85, 0x18, 0x9A, 0x7C, 0x45);
  TEST_ASSERT_NOT_EQUAL(derivarId(a), derivarId(b));
}

/* ==========================================================================
 *  Caída a crc16 cuando la MAC da un ID reservado (§6).
 * ========================================================================*/
void test_derivar_nunca_devuelve_reservado() {
  uint8_t m[6];
  /* 0x0000 y todo el rango 0xFFF0..0xFFFF están reservados. */
  const uint16_t malos[] = {0x0000, 0xFFF0, 0xFFF5, 0xFFFE, 0xFFFF};
  for (int i = 0; i < 5; ++i) {
    mac(m, 0x34, 0x85, 0x18, 0x01, static_cast<uint8_t>(malos[i] >> 8),
        static_cast<uint8_t>(malos[i] & 0xFF));
    const uint16_t id = derivarId(m);
    TEST_ASSERT_FALSE(idReservado(id));
    TEST_ASSERT_NOT_EQUAL(malos[i], id);
    /* La alternativa documentada es crc16(MAC) (y, si también fuese
     * reservada, crc16 ^ 0x0101). */
    const uint16_t c = crc16(m, 6);
    TEST_ASSERT_TRUE(id == c || id == static_cast<uint16_t>(c ^ 0x0101));
  }
}

void test_derivar_es_determinista() {
  uint8_t m[6];
  mac(m, 0x34, 0x85, 0x18, 0x9A, 0x00, 0x00);
  const uint16_t primera = derivarId(m);
  for (int i = 0; i < 5; ++i) TEST_ASSERT_EQUAL_HEX16(primera, derivarId(m));
}

void test_derivar_barrido_completo_del_ultimo_byte() {
  /* 65536 MAC distintas en los 2 últimos bytes: ninguna puede dar un ID
   * reservado. Es la garantía que necesita el arranque sin supervisión. */
  uint8_t m[6];
  for (uint32_t v = 0; v <= 0xFFFF; ++v) {
    mac(m, 0x34, 0x85, 0x18, 0x9A, static_cast<uint8_t>(v >> 8), static_cast<uint8_t>(v & 0xFF));
    TEST_ASSERT_FALSE(idReservado(derivarId(m)));
  }
}

/* ==========================================================================
 *  Asignables y parseo de hexadecimal (orden "ID <hex4>", §9)
 * ========================================================================*/
void test_id_asignable() {
  TEST_ASSERT_FALSE(idAsignable(0x0000));
  TEST_ASSERT_TRUE(idAsignable(0x0001));
  TEST_ASSERT_TRUE(idAsignable(0x1A2B));
  TEST_ASSERT_TRUE(idAsignable(0xFFEF));
  TEST_ASSERT_FALSE(idAsignable(0xFFF0));
  TEST_ASSERT_FALSE(idAsignable(DIR_PASARELA));
  TEST_ASSERT_FALSE(idAsignable(DIR_BROADCAST));
}

void test_parse_hex4_validos() {
  uint16_t v = 0;
  TEST_ASSERT_TRUE(parseHex4("1A2B", v));
  TEST_ASSERT_EQUAL_HEX16(0x1A2B, v);
  TEST_ASSERT_TRUE(parseHex4("1a2b", v));      // minúsculas
  TEST_ASSERT_EQUAL_HEX16(0x1A2B, v);
  TEST_ASSERT_TRUE(parseHex4("AA", v));        // menos de 4 cifras
  TEST_ASSERT_EQUAL_HEX16(0x00AA, v);
  TEST_ASSERT_TRUE(parseHex4("7", v));
  TEST_ASSERT_EQUAL_HEX16(0x0007, v);
  TEST_ASSERT_TRUE(parseHex4("FFFF", v));      // se parsea; que sea reservado
  TEST_ASSERT_EQUAL_HEX16(0xFFFF, v);          // lo decide idAsignable()
  TEST_ASSERT_TRUE(parseHex4("0000", v));
  TEST_ASSERT_EQUAL_HEX16(0x0000, v);
}

void test_parse_hex4_invalidos() {
  uint16_t v = 0;
  TEST_ASSERT_FALSE(parseHex4("", v));
  TEST_ASSERT_FALSE(parseHex4("12345", v));    // más de 4 cifras
  TEST_ASSERT_FALSE(parseHex4("0x1A", v));     // sin prefijo
  TEST_ASSERT_FALSE(parseHex4("1G2B", v));
  TEST_ASSERT_FALSE(parseHex4(" 1A2B", v));    // el troceo ya quitó los blancos
  TEST_ASSERT_FALSE(parseHex4("1A2B ", v));
  TEST_ASSERT_FALSE(parseHex4("-1", v));
  TEST_ASSERT_FALSE(parseHex4(nullptr, v));
}

void test_parse_hex_generico() {
  uint32_t v = 0;
  TEST_ASSERT_TRUE(parseHex("12", 2, v));      // sync word: "RADIO SYNC 12"
  TEST_ASSERT_EQUAL_UINT32(0x12, v);
  TEST_ASSERT_TRUE(parseHex("F", 2, v));
  TEST_ASSERT_EQUAL_UINT32(0x0F, v);
  TEST_ASSERT_FALSE(parseHex("123", 2, v));    // se pasa del máximo de cifras
  TEST_ASSERT_TRUE(parseHex("FFFFFFFF", 8, v));
  TEST_ASSERT_EQUAL_UINT32(0xFFFFFFFFu, v);
}

void test_formatear_id() {
  char s[5];
  formatearId(0x1A2B, s);
  TEST_ASSERT_EQUAL_STRING("1A2B", s);
  formatearId(0x0001, s);
  TEST_ASSERT_EQUAL_STRING("0001", s);   // siempre 4 cifras
  formatearId(0xABCD, s);
  TEST_ASSERT_EQUAL_STRING("ABCD", s);   // siempre en MAYÚSCULAS
  formatearId(0x0000, s);
  TEST_ASSERT_EQUAL_STRING("0000", s);
}

void test_formatear_y_parsear_es_ida_y_vuelta() {
  char s[5];
  uint16_t v = 0;
  for (uint32_t id = 0; id <= 0xFFFF; id += 7) {
    formatearId(static_cast<uint16_t>(id), s);
    TEST_ASSERT_TRUE(parseHex4(s, v));
    TEST_ASSERT_EQUAL_HEX16(static_cast<uint16_t>(id), v);
  }
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_derivar_dos_ultimos_bytes);
  RUN_TEST(test_derivar_placas_del_mismo_lote);
  RUN_TEST(test_derivar_nunca_devuelve_reservado);
  RUN_TEST(test_derivar_es_determinista);
  RUN_TEST(test_derivar_barrido_completo_del_ultimo_byte);
  RUN_TEST(test_id_asignable);
  RUN_TEST(test_parse_hex4_validos);
  RUN_TEST(test_parse_hex4_invalidos);
  RUN_TEST(test_parse_hex_generico);
  RUN_TEST(test_formatear_id);
  RUN_TEST(test_formatear_y_parsear_es_ida_y_vuelta);
  return UNITY_END();
}
