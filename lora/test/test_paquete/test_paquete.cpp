/* ============================================================================
 *  Pruebas del formato de trama v1 (lib/geolora/paquete, crc16) — DISENO §5
 * ============================================================================
 *      cd lora && pio test -e native
 *
 *  Las "tramas de oro" del §5.5 se comprueban BYTE A BYTE: son el contrato
 *  del formato. Si alguna vez fallan, lo que está mal es el código, no el
 *  vector (se calcularon a mano y con un script independiente).
 * ==========================================================================*/
#include <string.h>
#include <unity.h>

#include "carga.h"
#include "crc16.h"
#include "paquete.h"

using namespace geolora;

void setUp() {}
void tearDown() {}

/* ---- vectores de oro (§5.5) ---------------------------------------------*/
static const uint8_t PING_ORO[] = {0x47, 0x13, 0x00, 0x2B, 0x1A, 0x4D, 0x3C, 0x07, 0x00, 0x00, 0x00, 0x04,
                                   0xE8, 0x03, 0x00, 0x00, 0xF3, 0xA5};
static const uint8_t ALERT_ORO[] = {0x47, 0x15, 0x01, 0x2B, 0x1A, 0xFE, 0xFF, 0x08, 0x00, 0x03, 0x00, 0x0B, 0x02,
                                    0x05, 0x01, 0xCC, 0x62, 0xBF, 0x02, 0xA4, 0x05, 0xD8, 0xD3, 0xBE, 0x16};
static const uint8_t ALERT_REENV_ORO[] = {0x47, 0x15, 0x01, 0x2B, 0x1A, 0xFE, 0xFF, 0x08, 0x00,
                                          0x02, 0x01, 0x0B, 0x02, 0x05, 0x01, 0xCC, 0x62, 0xBF,
                                          0x02, 0xA4, 0x05, 0xD8, 0xD3, 0xBC, 0x28};

/* Trama DATA válida de `len` bytes de carga, para las pruebas de rechazo. */
static size_t tramaData(uint8_t* out, uint8_t len, uint16_t src = 0x1A2B) {
  uint8_t carga[LEN_MAX];
  for (int i = 0; i < len; ++i) carga[i] = static_cast<uint8_t>('a' + i % 26);
  Cabecera h;
  h.tipo = static_cast<uint8_t>(Tipo::DATA);
  h.flags = FLAG_ACK_REQ;
  h.src = src;
  h.dst = 0x3C4D;
  h.msgId = 0x1234;
  h.ttl = 3;
  h.hops = 0;
  h.len = len;
  return codificar(h, carga, out, TRAMA_MAX);
}

/* ---- CRC ----------------------------------------------------------------*/
void test_crc_vector_oficial() {
  const uint8_t s[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
  TEST_ASSERT_EQUAL_HEX16(0x29B1, crc16(s, sizeof(s)));
}

void test_crc_vacio_y_encadenado() {
  TEST_ASSERT_EQUAL_HEX16(0xFFFF, crc16(nullptr, 0));
  const uint8_t s[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
  const uint16_t parcial = crc16(s, 4);
  TEST_ASSERT_EQUAL_HEX16(0x29B1, crc16(s + 4, 5, parcial));
}

/* ---- tramas de oro ------------------------------------------------------*/
void test_oro_ping_codificar() {
  Cabecera h;
  h.tipo = static_cast<uint8_t>(Tipo::PING);
  h.src = 0x1A2B;
  h.dst = 0x3C4D;
  h.msgId = 7;
  h.ttl = 0;
  h.len = 4;
  uint8_t carga[4];
  TEST_ASSERT_EQUAL_UINT8(4, codificarPing(1000, 0, carga));
  uint8_t out[TRAMA_MAX];
  TEST_ASSERT_EQUAL_UINT32(18, codificar(h, carga, out, sizeof(out)));
  TEST_ASSERT_EQUAL_HEX8_ARRAY(PING_ORO, out, sizeof(PING_ORO));
}

void test_oro_ping_decodificar() {
  Cabecera h;
  const uint8_t* c = nullptr;
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Rechazo::NINGUNO),
                        static_cast<int>(decodificar(PING_ORO, sizeof(PING_ORO), h, c)));
  TEST_ASSERT_EQUAL_UINT8(1, h.version);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Tipo::PING), h.tipo);
  TEST_ASSERT_EQUAL_HEX16(0x1A2B, h.src);
  TEST_ASSERT_EQUAL_HEX16(0x3C4D, h.dst);
  TEST_ASSERT_EQUAL_UINT16(7, h.msgId);
  TEST_ASSERT_EQUAL_UINT8(0, h.ttl);
  TEST_ASSERT_EQUAL_UINT8(0, h.hops);
  TEST_ASSERT_EQUAL_UINT8(4, h.len);
  uint32_t t = 0;
  TEST_ASSERT_TRUE(decodificarPing(c, h.len, t));
  TEST_ASSERT_EQUAL_UINT32(1000, t);
}

void test_oro_alert_codificar() {
  CargaAlerta a;
  a.codigo = ALERTA_2;
  a.seq = 5;
  a.aflags = AFLAG_POS;
  a.lat1e7 = 46097100;     //  4.6097100
  a.lon1e7 = -740817500;   // -74.0817500
  uint8_t carga[LEN_ALERTA_CON_POS];
  TEST_ASSERT_EQUAL_UINT8(11, codificarAlerta(a, carga));
  Cabecera h;
  h.tipo = static_cast<uint8_t>(Tipo::ALERT);
  h.flags = FLAG_ACK_REQ;
  h.src = 0x1A2B;
  h.dst = DIR_PASARELA;
  h.msgId = 8;
  h.ttl = 3;
  h.len = 11;
  uint8_t out[TRAMA_MAX];
  TEST_ASSERT_EQUAL_UINT32(25, codificar(h, carga, out, sizeof(out)));
  TEST_ASSERT_EQUAL_HEX8_ARRAY(ALERT_ORO, out, sizeof(ALERT_ORO));
}

void test_oro_alert_reenvio() {
  uint8_t t[sizeof(ALERT_ORO)];
  memcpy(t, ALERT_ORO, sizeof(t));
  TEST_ASSERT_TRUE(prepararReenvio(t, sizeof(t)));
  TEST_ASSERT_EQUAL_HEX8_ARRAY(ALERT_REENV_ORO, t, sizeof(t));
  Cabecera h;
  const uint8_t* c = nullptr;
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Rechazo::NINGUNO), static_cast<int>(decodificar(t, sizeof(t), h, c)));
  TEST_ASSERT_EQUAL_UINT8(2, h.ttl);
  TEST_ASSERT_EQUAL_UINT8(1, h.hops);
  /* src, dst, msgId y carga NO cambian al reenviar (§8.1) */
  TEST_ASSERT_EQUAL_HEX16(0x1A2B, h.src);
  TEST_ASSERT_EQUAL_HEX16(DIR_PASARELA, h.dst);
  TEST_ASSERT_EQUAL_UINT16(8, h.msgId);
  TEST_ASSERT_EQUAL_HEX8_ARRAY(ALERT_ORO + 12, c, 11);
}

/* ---- ida y vuelta -------------------------------------------------------*/
void test_ida_y_vuelta_todos_los_campos_le() {
  Cabecera h;
  h.tipo = static_cast<uint8_t>(Tipo::DATA);
  h.flags = FLAG_ACK_REQ | FLAG_PRUEBA;
  h.src = 0xA1B2;
  h.dst = 0xC3D4;
  h.msgId = 0xE5F6;
  h.ttl = 7;
  h.hops = 6;
  h.len = 3;
  const uint8_t carga[] = {0x10, 0x20, 0x30};
  uint8_t out[TRAMA_MAX];
  TEST_ASSERT_EQUAL_UINT32(17, codificar(h, carga, out, sizeof(out)));
  /* little-endian byte a byte */
  TEST_ASSERT_EQUAL_HEX8(0xB2, out[3]);
  TEST_ASSERT_EQUAL_HEX8(0xA1, out[4]);
  TEST_ASSERT_EQUAL_HEX8(0xD4, out[5]);
  TEST_ASSERT_EQUAL_HEX8(0xC3, out[6]);
  TEST_ASSERT_EQUAL_HEX8(0xF6, out[7]);
  TEST_ASSERT_EQUAL_HEX8(0xE5, out[8]);
  const uint16_t crc = crc16(out, 15);
  TEST_ASSERT_EQUAL_HEX8(crc & 0xFF, out[15]);
  TEST_ASSERT_EQUAL_HEX8(crc >> 8, out[16]);
  Cabecera r;
  const uint8_t* c = nullptr;
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Rechazo::NINGUNO), static_cast<int>(decodificar(out, 17, r, c)));
  TEST_ASSERT_EQUAL_UINT8(h.tipo, r.tipo);
  TEST_ASSERT_EQUAL_HEX8(h.flags, r.flags);
  TEST_ASSERT_EQUAL_HEX16(h.src, r.src);
  TEST_ASSERT_EQUAL_HEX16(h.dst, r.dst);
  TEST_ASSERT_EQUAL_HEX16(h.msgId, r.msgId);
  TEST_ASSERT_EQUAL_UINT8(7, r.ttl);
  TEST_ASSERT_EQUAL_UINT8(6, r.hops);
  TEST_ASSERT_EQUAL_HEX8_ARRAY(carga, c, 3);
}

void test_carga_vacia_y_maxima() {
  uint8_t out[TRAMA_MAX];
  TEST_ASSERT_EQUAL_UINT32(14, tramaData(out, 0));
  Cabecera h;
  const uint8_t* c = nullptr;
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Rechazo::NINGUNO), static_cast<int>(decodificar(out, 14, h, c)));
  TEST_ASSERT_EQUAL_UINT8(0, h.len);
  TEST_ASSERT_EQUAL_UINT32(214, tramaData(out, 200));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Rechazo::NINGUNO), static_cast<int>(decodificar(out, 214, h, c)));
  TEST_ASSERT_EQUAL_UINT8(200, h.len);
}

void test_codificar_rechaza_argumentos_malos() {
  uint8_t out[TRAMA_MAX];
  uint8_t carga[LEN_MAX + 1] = {0};
  Cabecera h;
  h.tipo = 1;
  h.src = 0x1111;
  h.len = 201;   // > LEN_MAX
  TEST_ASSERT_EQUAL_UINT32(0, codificar(h, carga, out, sizeof(out)));
  h.len = 10;
  TEST_ASSERT_EQUAL_UINT32(0, codificar(h, carga, out, 23));   // cap corta (hace falta 24)
  TEST_ASSERT_EQUAL_UINT32(24, codificar(h, carga, out, 24));
  TEST_ASSERT_EQUAL_UINT32(0, codificar(h, nullptr, out, sizeof(out)));   // len > 0 sin carga
  h.tipo = 0x10;                                                          // no cabe en 4 bits
  TEST_ASSERT_EQUAL_UINT32(0, codificar(h, carga, out, sizeof(out)));
}

/* ---- rechazos, en el orden del §5.1 --------------------------------------*/
static Rechazo rechazo(const uint8_t* t, size_t n) {
  Cabecera h;
  const uint8_t* c = nullptr;
  return decodificar(t, n, h, c);
}
#define ASSERT_RECHAZO(esperado, t, n) \
  TEST_ASSERT_EQUAL_STRING(nombreRechazo(esperado), nombreRechazo(rechazo((t), (n))))

void test_rechazo_corta_y_truncadas() {
  uint8_t t[TRAMA_MAX];
  const size_t n = tramaData(t, 10);
  ASSERT_RECHAZO(Rechazo::CORTA, t, 0);
  ASSERT_RECHAZO(Rechazo::CORTA, t, 13);
  ASSERT_RECHAZO(Rechazo::CORTA, nullptr, 20);
  /* Truncada: le falta un byte -> la longitud no cuadra con len */
  ASSERT_RECHAZO(Rechazo::LEN, t, n - 1);
  /* Sobra un byte (basura al final) */
  ASSERT_RECHAZO(Rechazo::LEN, t, n + 1);
}

void test_rechazo_magic_y_version() {
  uint8_t t[TRAMA_MAX];
  const size_t n = tramaData(t, 5);
  t[0] = 0x48;
  ASSERT_RECHAZO(Rechazo::MAGIC, t, n);
  tramaData(t, 5);
  t[1] = static_cast<uint8_t>((2 << 4) | 0x1);   // versión 2
  ASSERT_RECHAZO(Rechazo::VERSION, t, n);
  t[1] = static_cast<uint8_t>((0 << 4) | 0x1);   // versión 0
  ASSERT_RECHAZO(Rechazo::VERSION, t, n);
}

void test_rechazo_len_fuera_de_rango() {
  uint8_t t[TRAMA_MAX + 2];
  memset(t, 0, sizeof(t));
  tramaData(t, 200);
  t[11] = 201;   // len > 200 aunque la longitud física cuadrase
  ASSERT_RECHAZO(Rechazo::LEN, t, 14 + 201);
  t[11] = 255;
  ASSERT_RECHAZO(Rechazo::LEN, t, 14 + 200);
  /* len coherente con la PHY pero distinto del real */
  const size_t n = tramaData(t, 8);
  t[11] = 7;
  ASSERT_RECHAZO(Rechazo::LEN, t, n);
}

void test_rechazo_crc_cualquier_bit() {
  uint8_t t[TRAMA_MAX];
  const size_t n = tramaData(t, 12);
  /* Un bit cambiado en CUALQUIER byte cubierto (cabecera, carga o el propio
   * CRC) -> rechazo por CRC. Se excluyen magic, versión y len, que se
   * rechazan antes por su propio motivo. */
  for (size_t i = 0; i < n; ++i) {
    if (i == 0 || i == 1 || i == 11) continue;
    for (int b = 0; b < 8; ++b) {
      tramaData(t, 12);
      t[i] ^= static_cast<uint8_t>(1 << b);
      ASSERT_RECHAZO(Rechazo::CRC, t, n);
    }
  }
}

/* Recalcula el CRC tras tocar la cabecera a mano. */
static void reCrc(uint8_t* t, size_t n) {
  escribirU16(t + n - 2, crc16(t, n - 2));
}

void test_rechazo_auth_src_tipo() {
  uint8_t t[TRAMA_MAX];
  size_t n = tramaData(t, 4);
  t[2] |= FLAG_AUTH;
  reCrc(t, n);
  ASSERT_RECHAZO(Rechazo::AUTH, t, n);

  n = tramaData(t, 4, 0x0000);
  ASSERT_RECHAZO(Rechazo::SRC, t, n);
  n = tramaData(t, 4, 0xFFF0);
  ASSERT_RECHAZO(Rechazo::SRC, t, n);
  n = tramaData(t, 4, 0xFFFF);
  ASSERT_RECHAZO(Rechazo::SRC, t, n);
  n = tramaData(t, 4, 0xFFEF);
  ASSERT_RECHAZO(Rechazo::NINGUNO, t, n);

  /* Tipos: 0, ORDEN (reservado) y 0x7..0xF -> TIPO, pero con cabecera válida */
  const uint8_t tipos[] = {0x0, 0x6, 0x7, 0xF};
  for (size_t i = 0; i < sizeof(tipos); ++i) {
    n = tramaData(t, 4);
    t[1] = static_cast<uint8_t>((1 << 4) | tipos[i]);
    reCrc(t, n);
    Cabecera h;
    const uint8_t* c = nullptr;
    TEST_ASSERT_EQUAL_STRING("tipo", nombreRechazo(decodificar(t, n, h, c)));
    TEST_ASSERT_EQUAL_HEX16(0x1A2B, h.src);   // la cabecera sí se separó
    TEST_ASSERT_NOT_NULL(c);
  }
}

void test_flags_reservados_se_ignoran() {
  uint8_t t[TRAMA_MAX];
  const size_t n = tramaData(t, 4);
  t[2] |= 0xF8;   // bits 3..7
  reCrc(t, n);
  ASSERT_RECHAZO(Rechazo::NINGUNO, t, n);
}

void test_orden_de_validacion() {
  /* Magic malo Y CRC malo: gana MAGIC (va antes). */
  uint8_t t[TRAMA_MAX];
  const size_t n = tramaData(t, 4);
  t[0] = 0x00;
  t[n - 1] ^= 0xFF;
  ASSERT_RECHAZO(Rechazo::MAGIC, t, n);
  /* AUTH y src reservado: gana AUTH. */
  tramaData(t, 4, 0x0000);
  t[2] |= FLAG_AUTH;
  reCrc(t, n);
  ASSERT_RECHAZO(Rechazo::AUTH, t, n);
}

/* ---- reenvío ------------------------------------------------------------*/
void test_reenvio_ttl0_y_hops_max() {
  uint8_t t[TRAMA_MAX];
  size_t n = tramaData(t, 4);
  t[9] = 0;   // ttl 0 (E1): nadie la repite
  reCrc(t, n);
  uint8_t copia[TRAMA_MAX];
  memcpy(copia, t, n);
  TEST_ASSERT_FALSE(prepararReenvio(t, n));
  TEST_ASSERT_EQUAL_HEX8_ARRAY(copia, t, n);   // no se toca
  t[9] = 3;
  t[10] = HOPS_MAX;
  reCrc(t, n);
  TEST_ASSERT_FALSE(prepararReenvio(t, n));
  t[10] = HOPS_MAX - 1;
  reCrc(t, n);
  TEST_ASSERT_TRUE(prepararReenvio(t, n));
  TEST_ASSERT_EQUAL_UINT8(HOPS_MAX, t[10]);
  TEST_ASSERT_EQUAL_UINT8(2, t[9]);
  ASSERT_RECHAZO(Rechazo::NINGUNO, t, n);
  TEST_ASSERT_FALSE(prepararReenvio(t, n - 1));   // incoherente
}

void test_ids_reservados() {
  TEST_ASSERT_TRUE(idReservado(0x0000));
  TEST_ASSERT_FALSE(idReservado(0x0001));
  TEST_ASSERT_FALSE(idReservado(0xFFEF));
  for (uint32_t id = 0xFFF0; id <= 0xFFFF; ++id) TEST_ASSERT_TRUE(idReservado(static_cast<uint16_t>(id)));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_crc_vector_oficial);
  RUN_TEST(test_crc_vacio_y_encadenado);
  RUN_TEST(test_oro_ping_codificar);
  RUN_TEST(test_oro_ping_decodificar);
  RUN_TEST(test_oro_alert_codificar);
  RUN_TEST(test_oro_alert_reenvio);
  RUN_TEST(test_ida_y_vuelta_todos_los_campos_le);
  RUN_TEST(test_carga_vacia_y_maxima);
  RUN_TEST(test_codificar_rechaza_argumentos_malos);
  RUN_TEST(test_rechazo_corta_y_truncadas);
  RUN_TEST(test_rechazo_magic_y_version);
  RUN_TEST(test_rechazo_len_fuera_de_rango);
  RUN_TEST(test_rechazo_crc_cualquier_bit);
  RUN_TEST(test_rechazo_auth_src_tipo);
  RUN_TEST(test_flags_reservados_se_ignoran);
  RUN_TEST(test_orden_de_validacion);
  RUN_TEST(test_reenvio_ttl0_y_hops_max);
  RUN_TEST(test_ids_reservados);
  return UNITY_END();
}
