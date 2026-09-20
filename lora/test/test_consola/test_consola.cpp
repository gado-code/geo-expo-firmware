/* ============================================================================
 *  Pruebas del parser de la consola serie (lib/geolora/consola) — DISENO §9
 * ============================================================================
 *      cd lora && pio test -e native
 *
 *  La consola es la interfaz de las pruebas de banco y de campo (§13.2) y está
 *  pensada para que un script la maneje: por eso cada orden se comprueba aquí
 *  con sus rangos, sus errores y sus formas de escribirla (mayúsculas, "\r\n",
 *  espacios de más). Un parser que acepta lo que no debe hace perder una
 *  tarde de campo mandando cosas que no son.
 *
 *  El parser SÓLO parsea: no toca radio ni NVS. Lo que depende del estado
 *  (que la frecuencia quepa en la banda con el BW ACTUAL) lo valida main con
 *  validar() de radio_params, y tiene su propia prueba en test_radio_params.
 * ==========================================================================*/
#include <string.h>
#include <unity.h>

#include "carga.h"
#include "consola.h"
#include "motor.h"
#include "paquete.h"

using namespace geolora;

void setUp() {}
void tearDown() {}

/* Parsea y exige éxito con el comando esperado. */
static Orden parsearOk(const char* linea, Cmd esperado) {
  Orden o;
  const bool r = parsearLinea(linea, o);
  if (!r) {
    TEST_FAIL_MESSAGE(o.error != nullptr ? o.error : "ERROR sin motivo");
  }
  TEST_ASSERT_EQUAL(esperado, o.cmd);
  return o;
}

/* Parsea y exige ERROR con ese motivo exacto. */
static void parsearErr(const char* linea, const char* motivo) {
  Orden o;
  TEST_ASSERT_FALSE(parsearLinea(linea, o));
  TEST_ASSERT_EQUAL(Cmd::ERROR, o.cmd);
  TEST_ASSERT_NOT_NULL(o.error);
  TEST_ASSERT_EQUAL_STRING(motivo, o.error);
}

/* ==========================================================================
 *  1. Forma de la línea
 * ========================================================================*/
void test_linea_vacia_no_es_error() {
  /* Una línea en blanco no se responde (el usuario pulsó Enter): ni OK ni
   * ERR, para no llenar el log en el campo. */
  Orden o;
  TEST_ASSERT_TRUE(parsearLinea("", o));
  TEST_ASSERT_EQUAL(Cmd::VACIA, o.cmd);
  TEST_ASSERT_TRUE(parsearLinea("\r\n", o));
  TEST_ASSERT_EQUAL(Cmd::VACIA, o.cmd);
  TEST_ASSERT_TRUE(parsearLinea("   \t  ", o));
  TEST_ASSERT_EQUAL(Cmd::VACIA, o.cmd);
  TEST_ASSERT_TRUE(parsearLinea(nullptr, o));
  TEST_ASSERT_EQUAL(Cmd::VACIA, o.cmd);
}

void test_terminadores_y_espacios() {
  parsearOk("STATUS", Cmd::STATUS);
  parsearOk("STATUS\n", Cmd::STATUS);
  parsearOk("STATUS\r\n", Cmd::STATUS);
  parsearOk("  STATUS  \r\n", Cmd::STATUS);
  parsearOk("\tSTATUS\t", Cmd::STATUS);
  Orden o = parsearOk("PING    1A2B     8  \r\n", Cmd::PING);
  TEST_ASSERT_EQUAL_HEX16(0x1A2B, o.dst);
  TEST_ASSERT_EQUAL_INT32(8, o.valor);
}

void test_mayusculas_y_minusculas() {
  parsearOk("status", Cmd::STATUS);
  parsearOk("Status", Cmd::STATUS);
  parsearOk("sTaTuS", Cmd::STATUS);
  parsearOk("radio preset largo", Cmd::RADIO_PRESET);
  parsearOk("relay on", Cmd::RELAY);
  parsearOk("id auto", Cmd::ID_AUTO);
}

void test_orden_desconocida_y_linea_larga() {
  parsearErr("PANIC", "ORDEN_DESCONOCIDA");
  parsearErr("MESHTASTIC", "ORDEN_DESCONOCIDA");
  parsearErr("SENDX 1A2B hola", "ORDEN_DESCONOCIDA");
  char larga[CONSOLA_LINEA_MAX + 40];
  memset(larga, 'A', sizeof(larga));
  larga[sizeof(larga) - 1] = '\0';
  parsearErr(larga, "LINEA_LARGA");
}

/* ==========================================================================
 *  2. HELP / STATUS / ID
 * ========================================================================*/
void test_help_y_status() {
  parsearOk("HELP", Cmd::HELP);
  parsearOk("STATUS", Cmd::STATUS);
  parsearErr("HELP ME", "SOBRAN_ARGUMENTOS");
  parsearErr("STATUS 1", "SOBRAN_ARGUMENTOS");
}

void test_id() {
  parsearOk("ID", Cmd::ID_VER);
  parsearOk("ID AUTO", Cmd::ID_AUTO);
  Orden o = parsearOk("ID 00AA", Cmd::ID_FIJAR);
  TEST_ASSERT_EQUAL_HEX16(0x00AA, o.dst);
  o = parsearOk("ID aa", Cmd::ID_FIJAR);
  TEST_ASSERT_EQUAL_HEX16(0x00AA, o.dst);
  /* Los IDs reservados no se pueden fijar (§6). */
  parsearErr("ID 0000", "ID_RESERVADO");
  parsearErr("ID FFFF", "ID_RESERVADO");
  parsearErr("ID FFFE", "ID_RESERVADO");
  parsearErr("ID FFF0", "ID_RESERVADO");
  parsearErr("ID ZZZZ", "ID_INVALIDO");
  parsearErr("ID 12345", "ID_INVALIDO");
  parsearErr("ID 1A2B AUTO", "SOBRAN_ARGUMENTOS");
}

/* ==========================================================================
 *  3. PING / SEND / BCAST
 * ========================================================================*/
void test_ping() {
  Orden o = parsearOk("PING 1A2B", Cmd::PING);
  TEST_ASSERT_EQUAL_HEX16(0x1A2B, o.dst);
  TEST_ASSERT_FALSE(o.broadcast);
  TEST_ASSERT_EQUAL_INT32(0, o.valor);
  /* PING * = descubrimiento de vecinos (§8.1). */
  o = parsearOk("PING *", Cmd::PING);
  TEST_ASSERT_TRUE(o.broadcast);
  TEST_ASSERT_EQUAL_HEX16(DIR_BROADCAST, o.dst);
  /* Con relleno, para probar tamaños de trama. */
  o = parsearOk("PING 1A2B 100", Cmd::PING);
  TEST_ASSERT_EQUAL_INT32(100, o.valor);
  o = parsearOk("PING * 0", Cmd::PING);
  TEST_ASSERT_TRUE(o.broadcast);
}

void test_ping_invalido() {
  parsearErr("PING", "FALTAN_ARGUMENTOS");
  parsearErr("PING 1A2B 10 20", "SOBRAN_ARGUMENTOS");
  parsearErr("PING FFFF", "ID_RESERVADO");   // para broadcast está el '*'
  parsearErr("PING 0000", "ID_RESERVADO");
  parsearErr("PING xyz", "ID_INVALIDO");
  /* El relleno no puede pasar de LEN_MAX - 4. */
  parsearErr("PING 1A2B 197", "FUERA_DE_RANGO");
  parsearErr("PING 1A2B -1", "FUERA_DE_RANGO");
  parsearErr("PING 1A2B ocho", "NUMERO_INVALIDO");
  Orden o = parsearOk("PING 1A2B 196", Cmd::PING);
  TEST_ASSERT_EQUAL_INT32(LEN_MAX - LEN_PING_MIN, o.valor);
}

void test_send_conserva_el_texto_tal_cual() {
  Orden o = parsearOk("SEND 1A2B Hola Mundo", Cmd::SEND);
  TEST_ASSERT_EQUAL_HEX16(0x1A2B, o.dst);
  TEST_ASSERT_EQUAL_STRING("Hola Mundo", o.texto);
  TEST_ASSERT_EQUAL_UINT8(10, o.lenTexto);
  /* Mayúsculas, espacios interiores y signos se conservan: es carga útil, no
   * una palabra clave. */
  o = parsearOk("send 1a2b  DOS  espacios ", Cmd::SEND);
  TEST_ASSERT_EQUAL_STRING("DOS  espacios", o.texto);
  o = parsearOk("SEND 1A2B RELAY ON STATUS HELP", Cmd::SEND);
  TEST_ASSERT_EQUAL_STRING("RELAY ON STATUS HELP", o.texto);
  o = parsearOk("SEND 1A2B 3;-74.08175;prueba", Cmd::SEND);
  TEST_ASSERT_EQUAL_STRING("3;-74.08175;prueba", o.texto);
}

void test_send_limites_y_errores() {
  parsearErr("SEND", "FALTAN_ARGUMENTOS");
  parsearErr("SEND 1A2B", "TEXTO_VACIO");
  parsearErr("SEND FFFF hola", "ID_RESERVADO");
  parsearErr("SEND zz hola", "ID_INVALIDO");
  /* El texto cabe justo en la carga máxima (200 B). */
  char linea[LEN_MAX + 40];
  strcpy(linea, "SEND 1A2B ");
  const size_t pre = strlen(linea);
  memset(linea + pre, 'x', LEN_MAX);
  linea[pre + LEN_MAX] = '\0';
  Orden o = parsearOk(linea, Cmd::SEND);
  TEST_ASSERT_EQUAL_UINT8(LEN_MAX, o.lenTexto);
  /* Un byte más y ya no cabe. */
  linea[pre + LEN_MAX] = 'x';
  linea[pre + LEN_MAX + 1] = '\0';
  parsearErr(linea, "TEXTO_LARGO");
}

void test_bcast() {
  Orden o = parsearOk("BCAST hola a todos", Cmd::BCAST);
  TEST_ASSERT_EQUAL_HEX16(DIR_BROADCAST, o.dst);
  TEST_ASSERT_TRUE(o.broadcast);
  TEST_ASSERT_EQUAL_STRING("hola a todos", o.texto);
  parsearErr("BCAST", "TEXTO_VACIO");
}

void test_send_con_muchas_palabras() {
  /* El texto puede tener más palabras que el tope del troceador: se saca de
   * la línea original, no de las palabras. */
  Orden o = parsearOk("SEND 1A2B a b c d e f g h i j k l m n o p", Cmd::SEND);
  TEST_ASSERT_EQUAL_STRING("a b c d e f g h i j k l m n o p", o.texto);
  o = parsearOk("BCAST a b c d e f g h i j k l m n o p q r", Cmd::BCAST);
  TEST_ASSERT_EQUAL_STRING("a b c d e f g h i j k l m n o p q r", o.texto);
  /* Pero una orden que NO es SEND/BCAST con demasiadas palabras es error. */
  parsearErr("STATUS a b c d e f g h i j k l", "SOBRAN_ARGUMENTOS");
}

/* ==========================================================================
 *  4. ALERT (transporte de los tokens congelados, §5.4)
 * ========================================================================*/
void test_alert_codigos() {
  Orden o = parsearOk("ALERT 1", Cmd::ALERT);
  TEST_ASSERT_EQUAL_UINT8(ALERTA_1, o.codigo);
  TEST_ASSERT_FALSE(o.conPos);
  TEST_ASSERT_FALSE(o.prueba);
  o = parsearOk("ALERT 2", Cmd::ALERT);
  TEST_ASSERT_EQUAL_UINT8(ALERTA_2, o.codigo);
  o = parsearOk("ALERT C", Cmd::ALERT);
  TEST_ASSERT_EQUAL_UINT8(ALERTA_CANCEL, o.codigo);
  o = parsearOk("alert cancel", Cmd::ALERT);
  TEST_ASSERT_EQUAL_UINT8(ALERTA_CANCEL, o.codigo);
  /* Y los tokens que salen de esos códigos son los del contrato BLE. */
  TEST_ASSERT_EQUAL_STRING("CANCEL", tokenAlerta(o.codigo));
  parsearErr("ALERT", "FALTAN_ARGUMENTOS");
  parsearErr("ALERT 3", "CODIGO_ALERTA");
  parsearErr("ALERT 0", "CODIGO_ALERTA");
  parsearErr("ALERT X", "CODIGO_ALERTA");
}

void test_alert_con_posicion_de_bogota() {
  Orden o = parsearOk("ALERT 2 4.60971 -74.08175", Cmd::ALERT);
  TEST_ASSERT_EQUAL_UINT8(ALERTA_2, o.codigo);
  TEST_ASSERT_TRUE(o.conPos);
  TEST_ASSERT_EQUAL_INT32(46097100, o.lat1e7);
  TEST_ASSERT_EQUAL_INT32(-740817500, o.lon1e7);
  TEST_ASSERT_FALSE(o.prueba);
}

void test_alert_prueba() {
  /* El flag PRUEBA va al final y es opcional, con o sin posición (§5.3). */
  Orden o = parsearOk("ALERT 2 PRUEBA", Cmd::ALERT);
  TEST_ASSERT_TRUE(o.prueba);
  TEST_ASSERT_FALSE(o.conPos);
  o = parsearOk("ALERT 2 4.60971 -74.08175 PRUEBA", Cmd::ALERT);
  TEST_ASSERT_TRUE(o.prueba);
  TEST_ASSERT_TRUE(o.conPos);
  TEST_ASSERT_EQUAL_INT32(46097100, o.lat1e7);
  o = parsearOk("alert c prueba", Cmd::ALERT);
  TEST_ASSERT_TRUE(o.prueba);
}

void test_alert_posicion_invalida() {
  parsearErr("ALERT 2 4.60971", "FALTAN_ARGUMENTOS");        // falta la longitud
  parsearErr("ALERT 2 4.60971 PRUEBA", "FALTAN_ARGUMENTOS");
  parsearErr("ALERT 2 91 0", "POSICION_INVALIDA");           // latitud > 90
  parsearErr("ALERT 2 0 181", "POSICION_INVALIDA");          // longitud > 180
  parsearErr("ALERT 2 norte oeste", "POSICION_INVALIDA");
  parsearErr("ALERT 2 4.60971 -74.08175 3.0", "SOBRAN_ARGUMENTOS");
}

void test_parse_grados() {
  int32_t v = 0;
  TEST_ASSERT_TRUE(parseGrados("4.60971", 90, v));
  TEST_ASSERT_EQUAL_INT32(46097100, v);
  TEST_ASSERT_TRUE(parseGrados("-74.08175", 180, v));
  TEST_ASSERT_EQUAL_INT32(-740817500, v);
  TEST_ASSERT_TRUE(parseGrados("0", 90, v));
  TEST_ASSERT_EQUAL_INT32(0, v);
  TEST_ASSERT_TRUE(parseGrados("90", 90, v));
  TEST_ASSERT_EQUAL_INT32(900000000, v);
  TEST_ASSERT_TRUE(parseGrados("-90", 90, v));
  TEST_ASSERT_EQUAL_INT32(-900000000, v);
  TEST_ASSERT_TRUE(parseGrados("4.6097123", 90, v));    // 7 decimales exactos
  TEST_ASSERT_EQUAL_INT32(46097123, v);
  TEST_ASSERT_TRUE(parseGrados("+4.5", 90, v));
  TEST_ASSERT_EQUAL_INT32(45000000, v);
  /* Inválidos. */
  TEST_ASSERT_FALSE(parseGrados("90.0000001", 90, v));  // se pasa del límite
  TEST_ASSERT_FALSE(parseGrados("4.60971234", 90, v));  // 8 decimales
  TEST_ASSERT_FALSE(parseGrados("4.", 90, v));
  TEST_ASSERT_FALSE(parseGrados(".5", 90, v));
  TEST_ASSERT_FALSE(parseGrados("4,5", 90, v));         // coma, no punto
  TEST_ASSERT_FALSE(parseGrados("", 90, v));
  TEST_ASSERT_FALSE(parseGrados("4.5.6", 90, v));
  TEST_ASSERT_FALSE(parseGrados("1000", 180, v));       // más de 3 cifras enteras
  TEST_ASSERT_FALSE(parseGrados(nullptr, 90, v));
}

void test_parse_entero() {
  int32_t v = 0;
  TEST_ASSERT_TRUE(parseEntero("0", v));
  TEST_ASSERT_EQUAL_INT32(0, v);
  TEST_ASSERT_TRUE(parseEntero("-9", v));
  TEST_ASSERT_EQUAL_INT32(-9, v);
  TEST_ASSERT_TRUE(parseEntero("+22", v));
  TEST_ASSERT_EQUAL_INT32(22, v);
  TEST_ASSERT_TRUE(parseEntero("2147483647", v));
  TEST_ASSERT_EQUAL_INT32(2147483647, v);
  TEST_ASSERT_FALSE(parseEntero("", v));
  TEST_ASSERT_FALSE(parseEntero("-", v));
  TEST_ASSERT_FALSE(parseEntero("1.5", v));
  TEST_ASSERT_FALSE(parseEntero("12a", v));
  TEST_ASSERT_FALSE(parseEntero("99999999999", v));   // desborda
  TEST_ASSERT_FALSE(parseEntero(nullptr, v));
}

/* ==========================================================================
 *  5. RADIO
 * ========================================================================*/
void test_radio_ver_y_subordenes_sin_argumento() {
  parsearOk("RADIO", Cmd::RADIO_VER);
  parsearOk("RADIO SAVE", Cmd::RADIO_SAVE);
  parsearOk("RADIO LOAD", Cmd::RADIO_LOAD);
  parsearOk("RADIO DEFECTO", Cmd::RADIO_DEFECTO);
  parsearErr("RADIO SAVE YA", "SOBRAN_ARGUMENTOS");
  parsearErr("RADIO QUEPASA", "ORDEN_DESCONOCIDA");
  parsearErr("RADIO FREQ", "FALTAN_ARGUMENTOS");
  parsearErr("RADIO SF 9 10", "SOBRAN_ARGUMENTOS");
}

void test_radio_freq() {
  Orden o = parsearOk("RADIO FREQ 921.5", Cmd::RADIO_FREQ);
  TEST_ASSERT_EQUAL_UINT32(921500, o.freqKHz);
  o = parsearOk("RADIO FREQ 915", Cmd::RADIO_FREQ);
  TEST_ASSERT_EQUAL_UINT32(915000, o.freqKHz);
  /* Ojo: el parser SÓLO convierte. Que 915.0 con BW 500 se salga de la banda
   * lo decide validar() con el BW actual (§3, test_radio_params). */
  parsearErr("RADIO FREQ 921.5001", "FREQ_INVALIDA");
  parsearErr("RADIO FREQ -921", "FREQ_INVALIDA");
  parsearErr("RADIO FREQ mucha", "FREQ_INVALIDA");
}

void test_radio_bw_cr_sync_pre() {
  Orden o = parsearOk("RADIO BW 500", Cmd::RADIO_BW);
  TEST_ASSERT_EQUAL_INT32(500, o.valor);
  parsearOk("RADIO BW 125", Cmd::RADIO_BW);
  parsearOk("RADIO BW 250", Cmd::RADIO_BW);
  parsearErr("RADIO BW 62", "FUERA_DE_RANGO");
  parsearErr("RADIO BW 800", "FUERA_DE_RANGO");

  o = parsearOk("RADIO CR 5", Cmd::RADIO_CR);
  TEST_ASSERT_EQUAL_INT32(5, o.valor);        // denominador de 4/5, como RadioLib
  parsearOk("RADIO CR 8", Cmd::RADIO_CR);
  parsearErr("RADIO CR 4", "FUERA_DE_RANGO");
  parsearErr("RADIO CR 9", "FUERA_DE_RANGO");

  o = parsearOk("RADIO SYNC 12", Cmd::RADIO_SYNC);
  TEST_ASSERT_EQUAL_INT32(0x12, o.valor);     // hexadecimal (D4)
  o = parsearOk("RADIO SYNC 34", Cmd::RADIO_SYNC);
  TEST_ASSERT_EQUAL_INT32(0x34, o.valor);
  o = parsearOk("RADIO SYNC ab", Cmd::RADIO_SYNC);
  TEST_ASSERT_EQUAL_INT32(0xAB, o.valor);
  parsearErr("RADIO SYNC 00", "FUERA_DE_RANGO");
  parsearErr("RADIO SYNC 123", "NUMERO_INVALIDO");
  parsearErr("RADIO SYNC zz", "NUMERO_INVALIDO");

  o = parsearOk("RADIO PRE 16", Cmd::RADIO_PRE);
  TEST_ASSERT_EQUAL_INT32(16, o.valor);
  parsearOk("RADIO PRE 8", Cmd::RADIO_PRE);
  parsearOk("RADIO PRE 64", Cmd::RADIO_PRE);
  parsearErr("RADIO PRE 7", "FUERA_DE_RANGO");
  parsearErr("RADIO PRE 65", "FUERA_DE_RANGO");
}

void test_radio_preset() {
  Orden o = parsearOk("RADIO PRESET NORMAL", Cmd::RADIO_PRESET);
  TEST_ASSERT_EQUAL(Preset::NORMAL, o.preset);
  o = parsearOk("RADIO PRESET rapido", Cmd::RADIO_PRESET);
  TEST_ASSERT_EQUAL(Preset::RAPIDO, o.preset);
  o = parsearOk("RADIO PRESET LARGO", Cmd::RADIO_PRESET);
  TEST_ASSERT_EQUAL(Preset::LARGO, o.preset);
  o = parsearOk("RADIO PRESET LAB125", Cmd::RADIO_PRESET);
  TEST_ASSERT_EQUAL(Preset::LAB125, o.preset);
  parsearErr("RADIO PRESET LONGFAST", "PRESET_DESCONOCIDO");
}

void test_atajos_sf_y_pwr() {
  Orden o = parsearOk("SF 9", Cmd::RADIO_SF);
  TEST_ASSERT_EQUAL_INT32(9, o.valor);
  parsearOk("SF 7", Cmd::RADIO_SF);
  parsearOk("SF 12", Cmd::RADIO_SF);
  parsearErr("SF 6", "FUERA_DE_RANGO");
  parsearErr("SF 13", "FUERA_DE_RANGO");
  parsearErr("SF", "FALTAN_ARGUMENTOS");
  parsearErr("SF 9 9", "SOBRAN_ARGUMENTOS");
  parsearErr("SF once", "NUMERO_INVALIDO");

  o = parsearOk("PWR 0", Cmd::RADIO_PWR);      // pruebas de banco (§2.4)
  TEST_ASSERT_EQUAL_INT32(0, o.valor);
  o = parsearOk("PWR 22", Cmd::RADIO_PWR);     // pruebas de alcance
  TEST_ASSERT_EQUAL_INT32(22, o.valor);
  o = parsearOk("PWR -9", Cmd::RADIO_PWR);
  TEST_ASSERT_EQUAL_INT32(-9, o.valor);
  parsearErr("PWR 23", "FUERA_DE_RANGO");
  parsearErr("PWR -10", "FUERA_DE_RANGO");
  /* Los atajos y la forma larga dan el MISMO comando. */
  TEST_ASSERT_EQUAL(Cmd::RADIO_SF, parsearOk("RADIO SF 9", Cmd::RADIO_SF).cmd);
  TEST_ASSERT_EQUAL(Cmd::RADIO_PWR, parsearOk("RADIO PWR 14", Cmd::RADIO_PWR).cmd);
}

/* ==========================================================================
 *  6. TTL / RELAY / ROL: el paso de E1 a E2 (§8.3)
 * ========================================================================*/
void test_ttl_y_relay() {
  Orden o = parsearOk("TTL 0", Cmd::TTL);      // E1
  TEST_ASSERT_EQUAL_INT32(0, o.valor);
  o = parsearOk("TTL 3", Cmd::TTL);            // E2
  TEST_ASSERT_EQUAL_INT32(3, o.valor);
  o = parsearOk("TTL 7", Cmd::TTL);
  TEST_ASSERT_EQUAL_INT32(TTL_MAX, o.valor);
  parsearErr("TTL 8", "FUERA_DE_RANGO");
  parsearErr("TTL -1", "FUERA_DE_RANGO");

  o = parsearOk("RELAY ON", Cmd::RELAY);
  TEST_ASSERT_TRUE(o.on);
  o = parsearOk("RELAY off", Cmd::RELAY);
  TEST_ASSERT_FALSE(o.on);
  parsearErr("RELAY", "FALTAN_ARGUMENTOS");
  parsearErr("RELAY SI", "ON_OFF");
  parsearErr("RELAY ON OFF", "SOBRAN_ARGUMENTOS");
}

void test_rol() {
  Orden o = parsearOk("ROL NORMAL", Cmd::ROL);
  TEST_ASSERT_EQUAL(Rol::NORMAL, o.rol);
  o = parsearOk("ROL repetidor", Cmd::ROL);
  TEST_ASSERT_EQUAL(Rol::REPETIDOR, o.rol);
  o = parsearOk("ROL PASARELA", Cmd::ROL);
  TEST_ASSERT_EQUAL(Rol::PASARELA, o.rol);
  parsearErr("ROL JEFE", "ROL_DESCONOCIDO");
  parsearErr("ROL", "FALTAN_ARGUMENTOS");
}

/* ==========================================================================
 *  7. RANGE y PERDIDA
 * ========================================================================*/
void test_range() {
  Orden o = parsearOk("RANGE 1A2B", Cmd::RANGE);
  TEST_ASSERT_EQUAL_HEX16(0x1A2B, o.dst);
  TEST_ASSERT_EQUAL_UINT32(0, o.n);     // 0 = sin fin
  TEST_ASSERT_EQUAL_UINT32(0, o.ms);    // 0 = mínimo automático (§7.6)
  o = parsearOk("RANGE 1A2B 20", Cmd::RANGE);
  TEST_ASSERT_EQUAL_UINT32(20, o.n);
  TEST_ASSERT_EQUAL_UINT32(0, o.ms);
  o = parsearOk("RANGE 1A2B 20 5000", Cmd::RANGE);
  TEST_ASSERT_EQUAL_UINT32(20, o.n);
  TEST_ASSERT_EQUAL_UINT32(5000, o.ms);
  parsearOk("RANGE STOP", Cmd::RANGE_STOP);
  parsearOk("range informe", Cmd::RANGE_INFORME);
  parsearErr("RANGE", "FALTAN_ARGUMENTOS");
  parsearErr("RANGE STOP YA", "SOBRAN_ARGUMENTOS");
  parsearErr("RANGE 1A2B 20 5000 1", "SOBRAN_ARGUMENTOS");
  parsearErr("RANGE FFFF", "ID_RESERVADO");
  parsearErr("RANGE 1A2B -1", "FUERA_DE_RANGO");
}

void test_perdida() {
  Orden o = parsearOk("PERDIDA 50", Cmd::PERDIDA);
  TEST_ASSERT_EQUAL_INT32(50, o.valor);
  o = parsearOk("PERDIDA 0", Cmd::PERDIDA);
  TEST_ASSERT_EQUAL_INT32(0, o.valor);
  o = parsearOk("PERDIDA 100", Cmd::PERDIDA);
  TEST_ASSERT_EQUAL_INT32(100, o.valor);
  parsearErr("PERDIDA 101", "FUERA_DE_RANGO");
  parsearErr("PERDIDA -1", "FUERA_DE_RANGO");
  parsearErr("PERDIDA", "FALTAN_ARGUMENTOS");
}

/* ==========================================================================
 *  8. La orden no deja basura de la anterior
 * ========================================================================*/
void test_cada_orden_parte_de_cero() {
  Orden o;
  TEST_ASSERT_TRUE(parsearLinea("ALERT 2 4.60971 -74.08175 PRUEBA", o));
  TEST_ASSERT_TRUE(o.conPos);
  TEST_ASSERT_TRUE(o.prueba);
  /* La siguiente orden no puede heredar conPos/prueba/texto. */
  TEST_ASSERT_TRUE(parsearLinea("ALERT 1", o));
  TEST_ASSERT_FALSE(o.conPos);
  TEST_ASSERT_FALSE(o.prueba);
  TEST_ASSERT_EQUAL_INT32(0, o.lat1e7);
  TEST_ASSERT_TRUE(parsearLinea("SEND 1A2B hola", o));
  TEST_ASSERT_EQUAL_STRING("hola", o.texto);
  TEST_ASSERT_TRUE(parsearLinea("STATUS", o));
  TEST_ASSERT_EQUAL_UINT8(0, o.lenTexto);
  TEST_ASSERT_EQUAL_UINT16(0, o.dst);
  TEST_ASSERT_NULL(o.error);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_linea_vacia_no_es_error);
  RUN_TEST(test_terminadores_y_espacios);
  RUN_TEST(test_mayusculas_y_minusculas);
  RUN_TEST(test_orden_desconocida_y_linea_larga);
  RUN_TEST(test_help_y_status);
  RUN_TEST(test_id);
  RUN_TEST(test_ping);
  RUN_TEST(test_ping_invalido);
  RUN_TEST(test_send_conserva_el_texto_tal_cual);
  RUN_TEST(test_send_limites_y_errores);
  RUN_TEST(test_bcast);
  RUN_TEST(test_send_con_muchas_palabras);
  RUN_TEST(test_alert_codigos);
  RUN_TEST(test_alert_con_posicion_de_bogota);
  RUN_TEST(test_alert_prueba);
  RUN_TEST(test_alert_posicion_invalida);
  RUN_TEST(test_parse_grados);
  RUN_TEST(test_parse_entero);
  RUN_TEST(test_radio_ver_y_subordenes_sin_argumento);
  RUN_TEST(test_radio_freq);
  RUN_TEST(test_radio_bw_cr_sync_pre);
  RUN_TEST(test_radio_preset);
  RUN_TEST(test_atajos_sf_y_pwr);
  RUN_TEST(test_ttl_y_relay);
  RUN_TEST(test_rol);
  RUN_TEST(test_range);
  RUN_TEST(test_perdida);
  RUN_TEST(test_cada_orden_parte_de_cero);
  return UNITY_END();
}
