/* ============================================================================
 *  Pruebas del módulo GPS (lib/nmea). Se pueden correr de dos maneras:
 *
 *      pio test -e devkit_v1    <- EN LA PLACA (funciona ya, sin GPS físico)
 *      pio test -e native       <- en el PC; necesita `sudo dnf install gcc-c++`
 *
 *  Son pruebas de verdad, no una demo: cubren el caso bueno, los hemisferios
 *  con signo negativo, el checksum corrupto, la trama sin fix y el flujo
 *  entrando carácter a carácter (que es como llegará del GPS real).
 * ==========================================================================*/
#include <unity.h>
#include <string.h>

#include "nmea.h"

/* Tolerancia: 1e-6 grados son ~11 cm. Más que suficiente para el proyecto. */
static const double EPS = 1e-6;

/* Tramas de referencia (checksums reales, verificados). */
static const char* GGA_MUNICH =
    "$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47";
static const char* RMC_MUNICH =
    "$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*6A";
static const char* GGA_SIN_FIX =
    "$GPGGA,123519,,,,,0,00,,,M,,M,,*6B";
static const char* GGA_SUR_ESTE =
    "$GNGGA,041515.00,3352.9871,S,15112.4567,E,2,11,0.78,25.6,M,22.1,M,,*61";
static const char* RMC_NORTE_OESTE =
    "$GNRMC,041515.00,A,0412.3456,N,07405.6789,W,0.06,,090926,,,A*4F";
static const char* RMC_SIN_FIX =
    "$GPRMC,041515.00,V,,,,,,,090926,,,N*7D";
static const char* GSV_IGNORADA =
    "$GPGSV,3,1,11,01,05,123,25,03,42,214,30,06,71,097,35,09,18,301,22*73";

/* -------------------------------------------------------------------------
 *  Conversión ddmm.mmmm -> grados decimales
 * -----------------------------------------------------------------------*/
void test_conversion_latitud_norte(void) {
  double d = 0;
  TEST_ASSERT_TRUE(nmea::degMinToDegrees("4807.038", 'N', &d));
  TEST_ASSERT_DOUBLE_WITHIN(EPS, 48.1173, d);
}

void test_conversion_longitud_este(void) {
  double d = 0;
  TEST_ASSERT_TRUE(nmea::degMinToDegrees("01131.000", 'E', &d));
  TEST_ASSERT_DOUBLE_WITHIN(EPS, 11.5166666, d);
}

void test_conversion_hemisferios_negativos(void) {
  double lat = 0, lon = 0;
  TEST_ASSERT_TRUE(nmea::degMinToDegrees("3352.9871", 'S', &lat));
  TEST_ASSERT_TRUE(nmea::degMinToDegrees("15112.4567", 'W', &lon));
  TEST_ASSERT_DOUBLE_WITHIN(EPS, -33.8831183, lat);
  TEST_ASSERT_DOUBLE_WITHIN(EPS, -151.2076116, lon);
}

void test_conversion_rechaza_basura(void) {
  double d = 0;
  TEST_ASSERT_FALSE(nmea::degMinToDegrees("", 'N', &d));          // campo vacío
  TEST_ASSERT_FALSE(nmea::degMinToDegrees("4807.038", 0, &d));    // sin hemisferio
  TEST_ASSERT_FALSE(nmea::degMinToDegrees("4807.038", 'X', &d));  // hemisferio inválido
  TEST_ASSERT_FALSE(nmea::degMinToDegrees("12", 'N', &d));        // demasiado corto
  TEST_ASSERT_FALSE(nmea::degMinToDegrees("4861.000", 'N', &d));  // 61 minutos no existen
}

/* -------------------------------------------------------------------------
 *  Checksum
 * -----------------------------------------------------------------------*/
void test_checksum_conocido(void) {
  const char* body = "GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,";
  TEST_ASSERT_EQUAL_HEX8(0x47, nmea::checksum(body, strlen(body)));
}

/* -------------------------------------------------------------------------
 *  GGA
 * -----------------------------------------------------------------------*/
void test_gga_completa(void) {
  nmea::Parser p;
  TEST_ASSERT_TRUE(p.feedLine(GGA_MUNICH));
  TEST_ASSERT_EQUAL(nmea::Sentence::GGA, p.lastSentence());

  const nmea::Fix& f = p.fix();
  TEST_ASSERT_TRUE(f.valid);
  TEST_ASSERT_DOUBLE_WITHIN(EPS, 48.1173, f.lat);
  TEST_ASSERT_DOUBLE_WITHIN(EPS, 11.5166666, f.lon);
  TEST_ASSERT_EQUAL_UINT8(8, f.sats);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.9f, f.hdop);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 545.4f, f.altM);
  TEST_ASSERT_TRUE(f.hasTime);
  TEST_ASSERT_EQUAL_UINT8(12, f.hh);
  TEST_ASSERT_EQUAL_UINT8(35, f.mm);
  TEST_ASSERT_EQUAL_UINT8(19, f.ss);
}

void test_gga_sin_fix_no_da_posicion(void) {
  nmea::Parser p;
  TEST_ASSERT_TRUE(p.feedLine(GGA_SIN_FIX));      // la trama es válida...
  TEST_ASSERT_FALSE(p.fix().valid);               // ...pero no hay posición
  TEST_ASSERT_EQUAL_UINT32(1, p.okCount());
  TEST_ASSERT_EQUAL_UINT32(0, p.badCount());
}

void test_gga_talker_gn_y_hemisferios_sur_este(void) {
  nmea::Parser p;
  TEST_ASSERT_TRUE(p.feedLine(GGA_SUR_ESTE));
  const nmea::Fix& f = p.fix();
  TEST_ASSERT_TRUE(f.valid);                      // calidad 2 (DGPS) también vale
  TEST_ASSERT_DOUBLE_WITHIN(EPS, -33.8831183, f.lat);
  TEST_ASSERT_DOUBLE_WITHIN(EPS, 151.2076116, f.lon);
  TEST_ASSERT_EQUAL_UINT8(11, f.sats);
}

/* -------------------------------------------------------------------------
 *  RMC
 * -----------------------------------------------------------------------*/
void test_rmc_completa(void) {
  nmea::Parser p;
  TEST_ASSERT_TRUE(p.feedLine(RMC_MUNICH));
  TEST_ASSERT_EQUAL(nmea::Sentence::RMC, p.lastSentence());

  const nmea::Fix& f = p.fix();
  TEST_ASSERT_TRUE(f.valid);
  TEST_ASSERT_DOUBLE_WITHIN(EPS, 48.1173, f.lat);
  TEST_ASSERT_DOUBLE_WITHIN(EPS, 11.5166666, f.lon);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 22.4f, f.speedKn);
  TEST_ASSERT_TRUE(f.hasDate);
  TEST_ASSERT_EQUAL_UINT8(23, f.day);
  TEST_ASSERT_EQUAL_UINT8(3, f.mon);
  TEST_ASSERT_EQUAL_UINT16(1994, f.year);         // ddmmyy=230394: 94 -> 1994
}

void test_rmc_estado_v_invalida_el_fix(void) {
  nmea::Parser p;
  TEST_ASSERT_TRUE(p.feedLine(GGA_MUNICH));
  TEST_ASSERT_TRUE(p.fix().valid);
  TEST_ASSERT_TRUE(p.feedLine(RMC_SIN_FIX));      // el GPS pierde la señal
  TEST_ASSERT_FALSE(p.fix().valid);
}

void test_rmc_norte_oeste(void) {
  nmea::Parser p;
  TEST_ASSERT_TRUE(p.feedLine(RMC_NORTE_OESTE));
  const nmea::Fix& f = p.fix();
  TEST_ASSERT_TRUE(f.valid);
  TEST_ASSERT_DOUBLE_WITHIN(EPS, 4.2057600, f.lat);
  TEST_ASSERT_DOUBLE_WITHIN(EPS, -74.0946483, f.lon);
}

/* -------------------------------------------------------------------------
 *  Robustez del flujo
 * -----------------------------------------------------------------------*/
void test_checksum_corrupto_se_descarta(void) {
  nmea::Parser p;
  char malo[128];
  strcpy(malo, GGA_MUNICH);
  malo[strlen(malo) - 1] = '0';                   // *47 -> *40
  TEST_ASSERT_FALSE(p.feedLine(malo));
  TEST_ASSERT_FALSE(p.fix().valid);
  TEST_ASSERT_EQUAL_UINT32(0, p.okCount());
  TEST_ASSERT_EQUAL_UINT32(1, p.badCount());
}

void test_sentencia_desconocida_es_valida_pero_no_toca_el_fix(void) {
  nmea::Parser p;
  TEST_ASSERT_TRUE(p.feedLine(GGA_MUNICH));
  const double lat = p.fix().lat;
  TEST_ASSERT_TRUE(p.feedLine(GSV_IGNORADA));     // GSV: checksum OK, no interpretada
  TEST_ASSERT_EQUAL(nmea::Sentence::OTHER, p.lastSentence());
  TEST_ASSERT_TRUE(p.fix().valid);
  TEST_ASSERT_DOUBLE_WITHIN(EPS, lat, p.fix().lat);
}

void test_flujo_caracter_a_caracter_con_ruido(void) {
  nmea::Parser p;
  /* Así llega de verdad: basura de arranque, una trama cortada por la mitad
   * y a continuación la buena. El '$' debe resincronizar el parser. */
  const char* ruido = "\xFF\x00 basura $GPGGA,1235";
  for (const char* c = ruido; *c; ++c) p.feed(*c);

  bool completa = false;
  for (const char* c = GGA_MUNICH; *c; ++c) {
    if (p.feed(*c)) completa = true;
  }
  if (p.feed('\r')) completa = true;
  if (p.feed('\n')) completa = true;

  TEST_ASSERT_TRUE(completa);
  TEST_ASSERT_TRUE(p.fix().valid);
  TEST_ASSERT_DOUBLE_WITHIN(EPS, 48.1173, p.fix().lat);
}

void test_sentencia_demasiado_larga_no_desborda(void) {
  nmea::Parser p;
  char larga[400];
  memset(larga, 'A', sizeof(larga) - 1);
  larga[0] = '$';
  larga[sizeof(larga) - 1] = '\0';
  TEST_ASSERT_FALSE(p.feedLine(larga));
  TEST_ASSERT_EQUAL_UINT32(1, p.badCount());

  /* Y después de un desbordamiento el parser sigue funcionando. */
  TEST_ASSERT_TRUE(p.feedLine(GGA_MUNICH));
  TEST_ASSERT_TRUE(p.fix().valid);
}

void test_reset_borra_el_fix(void) {
  nmea::Parser p;
  TEST_ASSERT_TRUE(p.feedLine(GGA_MUNICH));
  TEST_ASSERT_TRUE(p.fix().valid);
  p.reset();
  TEST_ASSERT_FALSE(p.fix().valid);
  TEST_ASSERT_EQUAL_UINT32(0, p.okCount());
}

/* -------------------------------------------------------------------------
 *  Arranque de Unity
 * -----------------------------------------------------------------------*/
void setUp(void) {}
void tearDown(void) {}

static void runAll(void) {
  UNITY_BEGIN();
  RUN_TEST(test_conversion_latitud_norte);
  RUN_TEST(test_conversion_longitud_este);
  RUN_TEST(test_conversion_hemisferios_negativos);
  RUN_TEST(test_conversion_rechaza_basura);
  RUN_TEST(test_checksum_conocido);
  RUN_TEST(test_gga_completa);
  RUN_TEST(test_gga_sin_fix_no_da_posicion);
  RUN_TEST(test_gga_talker_gn_y_hemisferios_sur_este);
  RUN_TEST(test_rmc_completa);
  RUN_TEST(test_rmc_estado_v_invalida_el_fix);
  RUN_TEST(test_rmc_norte_oeste);
  RUN_TEST(test_checksum_corrupto_se_descarta);
  RUN_TEST(test_sentencia_desconocida_es_valida_pero_no_toca_el_fix);
  RUN_TEST(test_flujo_caracter_a_caracter_con_ruido);
  RUN_TEST(test_sentencia_demasiado_larga_no_desborda);
  RUN_TEST(test_reset_borra_el_fix);
  UNITY_END();
}

#ifdef ARDUINO
/* En la placa, Unity arranca desde setup()/loop() y habla por el puerto serie.
 * La espera inicial evita perder las primeras líneas mientras el USB-serie
 * termina de levantarse tras el reset. */
#include <Arduino.h>
void setup() {
  delay(2000);
  runAll();
}
void loop() {}
#else
int main(int, char**) {
  runAll();
  return 0;
}
#endif
