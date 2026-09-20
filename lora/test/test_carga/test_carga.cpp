/* ============================================================================
 *  Pruebas de las cargas PING / PONG / ACK / ALERT (lib/geolora/carga)
 *  DISENO §5.2 y §5.4
 * ============================================================================
 *      cd lora && pio test -e native
 *
 *  Lo que se protege aquí, por orden de importancia:
 *
 *    1. Los TOKENS GEO-EXPO son contrato CONGELADO (README raíz §1). El código
 *       de 1 byte que viaja por el aire tiene que traducirse a "ALERT:1",
 *       "ALERT:2" y "CANCEL" EXACTOS, mayúsculas incluidas. Si algún día
 *       falla test_tokens_exactos, lo que está roto es el enlace con la app,
 *       no la prueba.
 *    2. La posición de Bogotá (latitud positiva, longitud NEGATIVA) tiene que
 *       ir y volver sin perder ni un 1e-7 de grado: es el caso real, y el
 *       signo es donde se cuelan los errores de serialización.
 *    3. Las saturaciones de rssiNeg/snrQ4: son las que evitan que un RSSI
 *       absurdo de la radio desborde un campo de 1 byte.
 * ==========================================================================*/
#include <string.h>
#include <unity.h>

#include "carga.h"
#include "paquete.h"

using namespace geolora;

void setUp() {}
void tearDown() {}

/* Bogotá, el punto que usa el DISENO (§5.5, §13.2). */
static const int32_t BOG_LAT = 46097100;     // 4.60971 N
static const int32_t BOG_LON = -740817500;   // 74.08175 O

/* ==========================================================================
 *  1. Tokens GEO-EXPO (contrato congelado)
 * ========================================================================*/
void test_tokens_exactos() {
  TEST_ASSERT_EQUAL_STRING("ALERT:1", tokenAlerta(ALERTA_1));
  TEST_ASSERT_EQUAL_STRING("ALERT:2", tokenAlerta(ALERTA_2));
  TEST_ASSERT_EQUAL_STRING("CANCEL", tokenAlerta(ALERTA_CANCEL));
  /* 0 y 4..255 están reservados (§5.4): no hay token que darles. */
  TEST_ASSERT_NULL(tokenAlerta(0));
  for (int c = 4; c <= 255; ++c) TEST_ASSERT_NULL(tokenAlerta(static_cast<uint8_t>(c)));
}

void test_token_a_codigo_ida_y_vuelta() {
  for (uint8_t c = ALERTA_1; c <= ALERTA_CANCEL; ++c) {
    TEST_ASSERT_EQUAL_UINT8(c, codigoDeToken(tokenAlerta(c)));
  }
}

void test_token_a_codigo_rechaza_lo_demas() {
  /* La comparación es EXACTA: ni minúsculas, ni espacios, ni prefijos. */
  TEST_ASSERT_EQUAL_UINT8(0, codigoDeToken("alert:1"));
  TEST_ASSERT_EQUAL_UINT8(0, codigoDeToken("ALERT:3"));
  TEST_ASSERT_EQUAL_UINT8(0, codigoDeToken("ALERT:"));
  TEST_ASSERT_EQUAL_UINT8(0, codigoDeToken("ALERT:1 "));
  TEST_ASSERT_EQUAL_UINT8(0, codigoDeToken("cancel"));
  TEST_ASSERT_EQUAL_UINT8(0, codigoDeToken(""));
  TEST_ASSERT_EQUAL_UINT8(0, codigoDeToken(nullptr));
  /* BEACON:ON/OFF son órdenes HACIA el llavero, no alertas (§5.4). */
  TEST_ASSERT_EQUAL_UINT8(0, codigoDeToken("BEACON:ON"));
  TEST_ASSERT_EQUAL_UINT8(0, codigoDeToken("BEACON:OFF"));
}

/* ==========================================================================
 *  2. Carga de ALERT
 * ========================================================================*/
void test_alerta_sin_posicion() {
  CargaAlerta a;
  a.codigo = ALERTA_1;
  a.seq = 7;
  uint8_t b[LEN_ALERTA_CON_POS];
  TEST_ASSERT_EQUAL_UINT8(LEN_ALERTA_SIN_POS, codificarAlerta(a, b));
  TEST_ASSERT_EQUAL_UINT8(ALERTA_1, b[0]);
  TEST_ASSERT_EQUAL_UINT8(7, b[1]);
  TEST_ASSERT_EQUAL_UINT8(0, b[2]);

  CargaAlerta r;
  TEST_ASSERT_TRUE(decodificarAlerta(b, LEN_ALERTA_SIN_POS, r));
  TEST_ASSERT_EQUAL_UINT8(ALERTA_1, r.codigo);
  TEST_ASSERT_EQUAL_UINT8(7, r.seq);
  TEST_ASSERT_FALSE(r.conPosicion());
}

void test_alerta_bogota_ida_y_vuelta() {
  CargaAlerta a;
  a.codigo = ALERTA_2;
  a.seq = 5;
  a.aflags = AFLAG_POS;
  a.lat1e7 = BOG_LAT;
  a.lon1e7 = BOG_LON;
  uint8_t b[LEN_ALERTA_CON_POS];
  TEST_ASSERT_EQUAL_UINT8(LEN_ALERTA_CON_POS, codificarAlerta(a, b));
  /* Los mismos bytes de la trama de oro del §5.5 (little-endian, con signo). */
  static const uint8_t ESPERADO[] = {0x02, 0x05, 0x01, 0xCC, 0x62, 0xBF, 0x02, 0xA4, 0x05, 0xD8, 0xD3};
  TEST_ASSERT_EQUAL_UINT8_ARRAY(ESPERADO, b, LEN_ALERTA_CON_POS);

  CargaAlerta r;
  TEST_ASSERT_TRUE(decodificarAlerta(b, LEN_ALERTA_CON_POS, r));
  TEST_ASSERT_TRUE(r.conPosicion());
  TEST_ASSERT_EQUAL_INT32(BOG_LAT, r.lat1e7);
  TEST_ASSERT_EQUAL_INT32(BOG_LON, r.lon1e7);   // el signo sobrevive
}

void test_alerta_posiciones_extremas() {
  const int32_t lat[] = {0, 900000000, -900000000, 1, -1};
  const int32_t lon[] = {0, 1800000000, -1800000000, 1, -1};
  for (int i = 0; i < 5; ++i) {
    CargaAlerta a;
    a.codigo = ALERTA_1;
    a.aflags = AFLAG_POS;
    a.lat1e7 = lat[i];
    a.lon1e7 = lon[i];
    uint8_t b[LEN_ALERTA_CON_POS];
    TEST_ASSERT_EQUAL_UINT8(LEN_ALERTA_CON_POS, codificarAlerta(a, b));
    CargaAlerta r;
    TEST_ASSERT_TRUE(decodificarAlerta(b, LEN_ALERTA_CON_POS, r));
    TEST_ASSERT_EQUAL_INT32(lat[i], r.lat1e7);
    TEST_ASSERT_EQUAL_INT32(lon[i], r.lon1e7);
  }
}

void test_alerta_rechaza_codigo_y_posicion_malos() {
  uint8_t b[LEN_ALERTA_CON_POS];
  CargaAlerta a;
  a.codigo = 0;
  TEST_ASSERT_EQUAL_UINT8(0, codificarAlerta(a, b));
  a.codigo = 4;
  TEST_ASSERT_EQUAL_UINT8(0, codificarAlerta(a, b));
  /* Posición fuera de ±90 / ±180 grados. */
  a.codigo = ALERTA_1;
  a.aflags = AFLAG_POS;
  a.lat1e7 = 900000001;
  a.lon1e7 = 0;
  TEST_ASSERT_EQUAL_UINT8(0, codificarAlerta(a, b));
  a.lat1e7 = 0;
  a.lon1e7 = -1800000001;
  TEST_ASSERT_EQUAL_UINT8(0, codificarAlerta(a, b));
  /* Sin POS, una posición absurda no estorba: no se serializa. */
  a.aflags = 0;
  a.lat1e7 = 2000000000;
  TEST_ASSERT_EQUAL_UINT8(LEN_ALERTA_SIN_POS, codificarAlerta(a, b));
}

void test_alerta_rechaza_truncadas_y_codigos_reservados() {
  CargaAlerta a;
  a.codigo = ALERTA_2;
  a.aflags = AFLAG_POS;
  a.lat1e7 = BOG_LAT;
  a.lon1e7 = BOG_LON;
  uint8_t b[LEN_ALERTA_CON_POS];
  codificarAlerta(a, b);
  CargaAlerta r;
  /* Con POS puesto pero sin los 8 bytes de lat/lon: truncada. */
  for (uint8_t len = 0; len < LEN_ALERTA_CON_POS; ++len) {
    TEST_ASSERT_FALSE(decodificarAlerta(b, len, r));
  }
  TEST_ASSERT_TRUE(decodificarAlerta(b, LEN_ALERTA_CON_POS, r));
  /* Código reservado en el aire. */
  b[0] = 0;
  TEST_ASSERT_FALSE(decodificarAlerta(b, LEN_ALERTA_CON_POS, r));
  b[0] = 9;
  TEST_ASSERT_FALSE(decodificarAlerta(b, LEN_ALERTA_CON_POS, r));
  /* Posición imposible recibida (no la genera codificarAlerta). */
  b[0] = ALERTA_1;
  escribirU32(b + 3, static_cast<uint32_t>(999999999));
  TEST_ASSERT_FALSE(decodificarAlerta(b, LEN_ALERTA_CON_POS, r));
}

void test_alerta_acepta_bytes_de_mas() {
  /* Compatibilidad hacia delante (§5.4, AFLAG_LLAVERO de E3): una carga más
   * larga de lo que v1 entiende se acepta y los bytes de más se ignoran. */
  uint8_t b[LEN_ALERTA_CON_POS + 4];
  memset(b, 0xAA, sizeof(b));
  CargaAlerta a;
  a.codigo = ALERTA_CANCEL;
  a.seq = 3;
  TEST_ASSERT_EQUAL_UINT8(LEN_ALERTA_SIN_POS, codificarAlerta(a, b));
  CargaAlerta r;
  TEST_ASSERT_TRUE(decodificarAlerta(b, static_cast<uint8_t>(sizeof(b)), r));
  TEST_ASSERT_EQUAL_UINT8(ALERTA_CANCEL, r.codigo);
  TEST_ASSERT_EQUAL_UINT8(3, r.seq);
  TEST_ASSERT_FALSE(r.conPosicion());
}

/* ==========================================================================
 *  3. Línea que entregaría la futura pasarela (§5.4, README raíz §8.4)
 * ========================================================================*/
void test_linea_pasarela_token_solo() {
  char s[64];
  CargaAlerta a;
  a.codigo = ALERTA_2;
  TEST_ASSERT_EQUAL_size_t(7, lineaPasarela(a, false, s, sizeof(s)));
  TEST_ASSERT_EQUAL_STRING("ALERT:2", s);
  a.codigo = ALERTA_CANCEL;
  TEST_ASSERT_EQUAL_size_t(6, lineaPasarela(a, true, s, sizeof(s)));
  TEST_ASSERT_EQUAL_STRING("CANCEL", s);   // sin POS no se añade nada
}

void test_linea_pasarela_con_posicion() {
  char s[64];
  CargaAlerta a;
  a.codigo = ALERTA_2;
  a.aflags = AFLAG_POS;
  a.lat1e7 = BOG_LAT;
  a.lon1e7 = BOG_LON;
  TEST_ASSERT_TRUE(lineaPasarela(a, true, s, sizeof(s)) > 0);
  TEST_ASSERT_EQUAL_STRING("ALERT:2;4.609710;-74.081750", s);
  /* Con conPosicion = false manda quien llama: sólo el token. */
  TEST_ASSERT_TRUE(lineaPasarela(a, false, s, sizeof(s)) > 0);
  TEST_ASSERT_EQUAL_STRING("ALERT:2", s);
}

void test_linea_pasarela_sin_sitio() {
  char s[8];
  CargaAlerta a;
  a.codigo = ALERTA_2;
  a.aflags = AFLAG_POS;
  a.lat1e7 = BOG_LAT;
  a.lon1e7 = BOG_LON;
  TEST_ASSERT_EQUAL_size_t(0, lineaPasarela(a, true, s, sizeof(s)));   // no cabe
  TEST_ASSERT_EQUAL_size_t(7, lineaPasarela(a, false, s, sizeof(s)));  // "ALERT:2" justo
  a.codigo = 0;
  TEST_ASSERT_EQUAL_size_t(0, lineaPasarela(a, false, s, sizeof(s)));  // código inválido
}

void test_formatear_grados() {
  char s[32];
  TEST_ASSERT_EQUAL_size_t(9, formatearGrados(BOG_LAT, 7, s, sizeof(s)));   // "4." + 7 decimales
  TEST_ASSERT_EQUAL_STRING("4.6097100", s);
  formatearGrados(BOG_LON, 7, s, sizeof(s));
  TEST_ASSERT_EQUAL_STRING("-74.0817500", s);
  formatearGrados(BOG_LON, 6, s, sizeof(s));
  TEST_ASSERT_EQUAL_STRING("-74.081750", s);
  formatearGrados(0, 6, s, sizeof(s));
  TEST_ASSERT_EQUAL_STRING("0.000000", s);
  formatearGrados(BOG_LAT, 0, s, sizeof(s));
  TEST_ASSERT_EQUAL_STRING("5", s);   // 4.6097 redondea a 5, sin punto
  /* Redondeo a la mitad LEJOS DE CERO, con el signo bien puesto. */
  formatearGrados(15, 6, s, sizeof(s));
  TEST_ASSERT_EQUAL_STRING("0.000002", s);
  formatearGrados(-15, 6, s, sizeof(s));
  TEST_ASSERT_EQUAL_STRING("-0.000002", s);
  /* Un valor negativo que redondea a cero no sale como "-0". */
  formatearGrados(-4, 6, s, sizeof(s));
  TEST_ASSERT_EQUAL_STRING("0.000000", s);
  /* Acarreo que sube el entero. */
  formatearGrados(19999999, 6, s, sizeof(s));
  TEST_ASSERT_EQUAL_STRING("2.000000", s);
  /* Más de 7 decimales no existe, y sin sitio devuelve 0. */
  TEST_ASSERT_EQUAL_size_t(0, formatearGrados(BOG_LAT, 8, s, sizeof(s)));
  TEST_ASSERT_EQUAL_size_t(0, formatearGrados(BOG_LON, 7, s, 5));
}

/* ==========================================================================
 *  4. ACK / PING / PONG
 * ========================================================================*/
void test_ack_ida_y_vuelta() {
  CargaAck a;
  a.ackMsgId = 0xBEEF;
  a.rssiNeg = 87;
  a.snrQ4 = -41;
  a.hopsRx = 2;
  uint8_t b[LEN_ACK];
  TEST_ASSERT_EQUAL_UINT8(LEN_ACK, codificarAck(a, b));
  TEST_ASSERT_EQUAL_UINT8(0xEF, b[0]);   // little-endian
  TEST_ASSERT_EQUAL_UINT8(0xBE, b[1]);
  CargaAck r;
  TEST_ASSERT_TRUE(decodificarAck(b, LEN_ACK, r));
  TEST_ASSERT_EQUAL_UINT16(0xBEEF, r.ackMsgId);
  TEST_ASSERT_EQUAL_UINT8(87, r.rssiNeg);
  TEST_ASSERT_EQUAL_INT8(-41, r.snrQ4);    // el signo de snrQ4 sobrevive
  TEST_ASSERT_EQUAL_UINT8(2, r.hopsRx);
  /* La longitud es EXACTA: un ACK de 3 o de 6 bytes no es un ACK. */
  for (uint8_t len = 0; len < 12; ++len) {
    if (len == LEN_ACK) continue;
    TEST_ASSERT_FALSE(decodificarAck(b, len, r));
  }
}

void test_pong_ida_y_vuelta() {
  CargaPong p;
  p.tEco = 0x12345678;
  p.rssiNeg = 120;
  p.snrQ4 = 40;
  p.hopsRx = 1;
  uint8_t b[LEN_PONG];
  TEST_ASSERT_EQUAL_UINT8(LEN_PONG, codificarPong(p, b));
  TEST_ASSERT_EQUAL_UINT8(0x78, b[0]);
  TEST_ASSERT_EQUAL_UINT8(0x12, b[3]);
  CargaPong r;
  TEST_ASSERT_TRUE(decodificarPong(b, LEN_PONG, r));
  TEST_ASSERT_EQUAL_UINT32(0x12345678, r.tEco);
  TEST_ASSERT_EQUAL_UINT8(120, r.rssiNeg);
  TEST_ASSERT_EQUAL_INT8(40, r.snrQ4);
  TEST_ASSERT_EQUAL_UINT8(1, r.hopsRx);
  for (uint8_t len = 0; len < 12; ++len) {
    if (len == LEN_PONG) continue;
    TEST_ASSERT_FALSE(decodificarPong(b, len, r));
  }
}

void test_ping_con_relleno() {
  uint8_t b[LEN_MAX];
  memset(b, 0xFF, sizeof(b));
  TEST_ASSERT_EQUAL_UINT8(LEN_PING_MIN, codificarPing(1000, 0, b));
  uint32_t t = 0;
  TEST_ASSERT_TRUE(decodificarPing(b, LEN_PING_MIN, t));
  TEST_ASSERT_EQUAL_UINT32(1000, t);
  /* El relleno es de ceros y alarga la trama (PING <dst> <bytes>, §9). */
  memset(b, 0xFF, sizeof(b));
  TEST_ASSERT_EQUAL_UINT8(LEN_PING_MIN + 20, codificarPing(0xFFFFFFFF, 20, b));
  for (int i = LEN_PING_MIN; i < LEN_PING_MIN + 20; ++i) TEST_ASSERT_EQUAL_UINT8(0, b[i]);
  TEST_ASSERT_TRUE(decodificarPing(b, LEN_PING_MIN + 20, t));
  TEST_ASSERT_EQUAL_UINT32(0xFFFFFFFF, t);
  /* Tope: 4 + relleno <= LEN_MAX. */
  TEST_ASSERT_EQUAL_UINT8(LEN_MAX, codificarPing(0, LEN_MAX - LEN_PING_MIN, b));
  TEST_ASSERT_EQUAL_UINT8(0, codificarPing(0, LEN_MAX - LEN_PING_MIN + 1, b));
  /* Un PING de menos de 4 bytes no lleva t_ms. */
  for (uint8_t len = 0; len < LEN_PING_MIN; ++len) TEST_ASSERT_FALSE(decodificarPing(b, len, t));
}

/* ==========================================================================
 *  5. Saturaciones de calidad de enlace
 * ========================================================================*/
void test_rssi_a_neg() {
  TEST_ASSERT_EQUAL_UINT8(87, rssiANeg(-87));
  TEST_ASSERT_EQUAL_UINT8(1, rssiANeg(-1));
  TEST_ASSERT_EQUAL_UINT8(0, rssiANeg(0));
  TEST_ASSERT_EQUAL_UINT8(0, rssiANeg(15));      // positivo imposible -> 0
  TEST_ASSERT_EQUAL_UINT8(254, rssiANeg(-254));
  TEST_ASSERT_EQUAL_UINT8(255, rssiANeg(-255));
  TEST_ASSERT_EQUAL_UINT8(255, rssiANeg(-1000));  // saturado, no desbordado
  /* Ida y vuelta en el rango real de la radio (-30 a -140 dBm). */
  for (int16_t r = -140; r <= -30; ++r) TEST_ASSERT_EQUAL_INT16(r, rssiDeNeg(rssiANeg(r)));
}

void test_snr_a_q4() {
  TEST_ASSERT_EQUAL_INT8(-41, snrAQ4(-10.25f));   // el ejemplo del §5.2
  TEST_ASSERT_EQUAL_INT8(0, snrAQ4(0.0f));
  TEST_ASSERT_EQUAL_INT8(40, snrAQ4(10.0f));
  TEST_ASSERT_EQUAL_INT8(-80, snrAQ4(-20.0f));
  /* Redondeo al cuarto más cercano, mitad lejos de cero. */
  TEST_ASSERT_EQUAL_INT8(1, snrAQ4(0.125f));
  TEST_ASSERT_EQUAL_INT8(-1, snrAQ4(-0.125f));
  TEST_ASSERT_EQUAL_INT8(2, snrAQ4(0.5f));
  /* Saturación: el SX126x no da estos valores, pero un búfer corrupto sí. */
  TEST_ASSERT_EQUAL_INT8(127, snrAQ4(100.0f));
  TEST_ASSERT_EQUAL_INT8(-128, snrAQ4(-100.0f));
  TEST_ASSERT_EQUAL_INT8(127, saturarQ4(1000));
  TEST_ASSERT_EQUAL_INT8(-128, saturarQ4(-1000));
  TEST_ASSERT_EQUAL_INT8(-41, saturarQ4(-41));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_tokens_exactos);
  RUN_TEST(test_token_a_codigo_ida_y_vuelta);
  RUN_TEST(test_token_a_codigo_rechaza_lo_demas);
  RUN_TEST(test_alerta_sin_posicion);
  RUN_TEST(test_alerta_bogota_ida_y_vuelta);
  RUN_TEST(test_alerta_posiciones_extremas);
  RUN_TEST(test_alerta_rechaza_codigo_y_posicion_malos);
  RUN_TEST(test_alerta_rechaza_truncadas_y_codigos_reservados);
  RUN_TEST(test_alerta_acepta_bytes_de_mas);
  RUN_TEST(test_linea_pasarela_token_solo);
  RUN_TEST(test_linea_pasarela_con_posicion);
  RUN_TEST(test_linea_pasarela_sin_sitio);
  RUN_TEST(test_formatear_grados);
  RUN_TEST(test_ack_ida_y_vuelta);
  RUN_TEST(test_pong_ida_y_vuelta);
  RUN_TEST(test_ping_con_relleno);
  RUN_TEST(test_rssi_a_neg);
  RUN_TEST(test_snr_a_q4);
  return UNITY_END();
}
