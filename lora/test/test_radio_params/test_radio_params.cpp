/* ============================================================================
 *  Pruebas de los parámetros de radio (lib/geolora/radio_params)
 *  DISENO §3 (banda), §4 (presets y airtime), §7.6 (ocupación)
 * ============================================================================
 *      cd lora && pio test -e native
 *
 *  Dos cosas se protegen aquí y las dos tienen consecuencias fuera del código:
 *
 *    1. LA BANDA. En Colombia sólo 915–928 MHz es de uso libre (Res. ANE
 *       105/2020, Anexo 1). El canal COMPLETO, centro ± BW/2, tiene que caber
 *       dentro. Por eso 915.0 MHz con BW 500 —lo que pedía el encargo— se
 *       RECHAZA: se sale por debajo. Si algún día este test se "arregla"
 *       relajando el límite, lo que se está haciendo es transmitir fuera de
 *       banda.
 *    2. EL AIRTIME. La tabla del §4.2 es la que dimensiona TODOS los timeouts
 *       del motor y el presupuesto de ocupación. Se comprueba contra los
 *       valores calculados a mano con la fórmula de Semtech (AN1200.13).
 * ==========================================================================*/
#include <string.h>
#include <unity.h>

#include "carga.h"
#include "paquete.h"
#include "radio_params.h"

using namespace geolora;

void setUp() {}
void tearDown() {}

static ParamsRadio conPreset(Preset p) {
  ParamsRadio r = paramsDefecto();
  aplicarPreset(p, r);
  return r;
}

/* ==========================================================================
 *  1. Valores de fábrica (§4.1 y decisiones D1/D2/D4)
 * ========================================================================*/
void test_defecto_es_el_perfil_normal() {
  const ParamsRadio r = paramsDefecto();
  TEST_ASSERT_EQUAL_UINT32(921500, r.freqKHz);   // D1: NO 915.0
  TEST_ASSERT_EQUAL_UINT8(11, r.sf);
  TEST_ASSERT_EQUAL_UINT16(500, r.bwKHz);        // D2: 500 kHz
  TEST_ASSERT_EQUAL_UINT8(5, r.cr);              // 4/5
  TEST_ASSERT_EQUAL_HEX8(0x12, r.sync);          // D4: privado estándar
  TEST_ASSERT_EQUAL_UINT16(16, r.pre);
  TEST_ASSERT_EQUAL_INT8(14, r.pwr);             // moderada (§3)
  TEST_ASSERT_EQUAL(ErrorRadio::OK, validar(r));
  TEST_ASSERT_EQUAL(Preset::NORMAL, presetDe(r));
}

void test_presets() {
  ParamsRadio r = conPreset(Preset::RAPIDO);
  TEST_ASSERT_EQUAL_UINT8(9, r.sf);
  TEST_ASSERT_EQUAL_UINT16(500, r.bwKHz);
  TEST_ASSERT_EQUAL(Preset::RAPIDO, presetDe(r));
  r = conPreset(Preset::NORMAL);
  TEST_ASSERT_EQUAL_UINT8(11, r.sf);
  TEST_ASSERT_EQUAL_UINT16(500, r.bwKHz);
  r = conPreset(Preset::LARGO);
  TEST_ASSERT_EQUAL_UINT8(12, r.sf);
  TEST_ASSERT_EQUAL_UINT16(500, r.bwKHz);
  r = conPreset(Preset::LAB125);
  TEST_ASSERT_EQUAL_UINT8(9, r.sf);
  TEST_ASSERT_EQUAL_UINT16(125, r.bwKHz);        // sólo laboratorio (§3)
  TEST_ASSERT_EQUAL(Preset::LAB125, presetDe(r));
}

void test_preset_conserva_freq_sync_pre_y_potencia() {
  ParamsRadio r = paramsDefecto();
  r.freqKHz = 925000;
  r.sync = 0x34;
  r.pre = 32;
  r.pwr = 22;
  aplicarPreset(Preset::LARGO, r);
  TEST_ASSERT_EQUAL_UINT32(925000, r.freqKHz);
  TEST_ASSERT_EQUAL_HEX8(0x34, r.sync);
  TEST_ASSERT_EQUAL_UINT16(32, r.pre);
  TEST_ASSERT_EQUAL_INT8(22, r.pwr);
  TEST_ASSERT_EQUAL_UINT8(12, r.sf);
}

void test_preset_a_medida_y_por_nombre() {
  ParamsRadio r = paramsDefecto();
  r.sf = 10;                                     // ninguna combinación de la tabla
  TEST_ASSERT_EQUAL(Preset::A_MEDIDA, presetDe(r));
  Preset p = Preset::A_MEDIDA;
  TEST_ASSERT_TRUE(presetPorNombre("normal", p));       // sin distinguir mayúsculas
  TEST_ASSERT_EQUAL(Preset::NORMAL, p);
  TEST_ASSERT_TRUE(presetPorNombre("LAB125", p));
  TEST_ASSERT_EQUAL(Preset::LAB125, p);
  TEST_ASSERT_TRUE(presetPorNombre("RaPiDo", p));
  TEST_ASSERT_EQUAL(Preset::RAPIDO, p);
  TEST_ASSERT_FALSE(presetPorNombre("LONGFAST", p));    // ese es de Meshtastic
  TEST_ASSERT_FALSE(presetPorNombre("", p));
  TEST_ASSERT_EQUAL_STRING("NORMAL", nombrePreset(Preset::NORMAL));
}

/* ==========================================================================
 *  2. Banda 915–928 MHz (§3, decisión D1)
 * ========================================================================*/
void test_banda_el_canal_entero_tiene_que_caber() {
  /* Los casos concretos que exige el plan de pruebas del §13.1. */
  TEST_ASSERT_FALSE(frecuenciaEnBanda(915000, 500));   // 914.75: FUERA por abajo
  TEST_ASSERT_TRUE(frecuenciaEnBanda(921500, 500));    // el defecto
  TEST_ASSERT_TRUE(frecuenciaEnBanda(927750, 500));    // 927.5–928.0: justo dentro
  TEST_ASSERT_FALSE(frecuenciaEnBanda(927800, 500));   // 928.05: FUERA por arriba
  /* Bordes exactos con cada BW. */
  TEST_ASSERT_TRUE(frecuenciaEnBanda(915250, 500));
  TEST_ASSERT_FALSE(frecuenciaEnBanda(915249, 500));
  TEST_ASSERT_TRUE(frecuenciaEnBanda(915125, 250));
  TEST_ASSERT_FALSE(frecuenciaEnBanda(915124, 250));
  TEST_ASSERT_TRUE(frecuenciaEnBanda(915063, 125));
  TEST_ASSERT_FALSE(frecuenciaEnBanda(915062, 125));
  /* 902–915 MHz NO es de uso libre en Colombia (Res. ANE 28/2026). */
  TEST_ASSERT_FALSE(frecuenciaEnBanda(902000, 125));
  TEST_ASSERT_FALSE(frecuenciaEnBanda(914000, 125));
  TEST_ASSERT_FALSE(frecuenciaEnBanda(868000, 125));   // la banda europea, tampoco
}

void test_validar_devuelve_el_primer_campo_malo() {
  ParamsRadio r = paramsDefecto();
  TEST_ASSERT_EQUAL(ErrorRadio::OK, validar(r));

  r = paramsDefecto();
  r.freqKHz = 915000;                       // el caso del encargo
  TEST_ASSERT_EQUAL(ErrorRadio::FREQ, validar(r));
  /* Ni con 125 kHz cabe 915.0 (914.9375 se sale): hace falta subir el centro
   * al menos medio ancho de banda. */
  r.bwKHz = 125;
  TEST_ASSERT_EQUAL(ErrorRadio::FREQ, validar(r));
  r.freqKHz = 915063;
  TEST_ASSERT_EQUAL(ErrorRadio::OK, validar(r));

  r = paramsDefecto();
  r.sf = 6;
  TEST_ASSERT_EQUAL(ErrorRadio::SF, validar(r));
  r.sf = 13;
  TEST_ASSERT_EQUAL(ErrorRadio::SF, validar(r));

  r = paramsDefecto();
  r.bwKHz = 62;
  TEST_ASSERT_EQUAL(ErrorRadio::BW, validar(r));

  r = paramsDefecto();
  r.cr = 4;
  TEST_ASSERT_EQUAL(ErrorRadio::CR, validar(r));
  r.cr = 9;
  TEST_ASSERT_EQUAL(ErrorRadio::CR, validar(r));

  r = paramsDefecto();
  r.pre = PRE_MIN - 1;
  TEST_ASSERT_EQUAL(ErrorRadio::PRE, validar(r));
  r.pre = PRE_MAX + 1;
  TEST_ASSERT_EQUAL(ErrorRadio::PRE, validar(r));

  r = paramsDefecto();
  r.pwr = PWR_MIN - 1;
  TEST_ASSERT_EQUAL(ErrorRadio::PWR, validar(r));
  r.pwr = PWR_MAX + 1;
  TEST_ASSERT_EQUAL(ErrorRadio::PWR, validar(r));
  r.pwr = PWR_MAX;
  TEST_ASSERT_EQUAL(ErrorRadio::OK, validar(r));
}

void test_bw_valido() {
  TEST_ASSERT_TRUE(bwValido(125));
  TEST_ASSERT_TRUE(bwValido(250));
  TEST_ASSERT_TRUE(bwValido(500));
  TEST_ASSERT_FALSE(bwValido(0));
  TEST_ASSERT_FALSE(bwValido(62));
  TEST_ASSERT_FALSE(bwValido(1000));
}

void test_parse_y_formateo_de_mhz() {
  uint32_t k = 0;
  TEST_ASSERT_TRUE(parseMHz("921.5", k));
  TEST_ASSERT_EQUAL_UINT32(921500, k);
  TEST_ASSERT_TRUE(parseMHz("921", k));
  TEST_ASSERT_EQUAL_UINT32(921000, k);
  TEST_ASSERT_TRUE(parseMHz("927.75", k));
  TEST_ASSERT_EQUAL_UINT32(927750, k);
  TEST_ASSERT_TRUE(parseMHz("915.125", k));      // 3 decimales = 1 kHz
  TEST_ASSERT_EQUAL_UINT32(915125, k);
  TEST_ASSERT_TRUE(parseMHz("921.500", k));
  TEST_ASSERT_EQUAL_UINT32(921500, k);
  /* Inválidos. */
  TEST_ASSERT_FALSE(parseMHz("921.5001", k));    // más de 3 decimales
  TEST_ASSERT_FALSE(parseMHz("-921.5", k));
  TEST_ASSERT_FALSE(parseMHz("", k));
  TEST_ASSERT_FALSE(parseMHz("abc", k));
  TEST_ASSERT_FALSE(parseMHz("921.", k));
  TEST_ASSERT_FALSE(parseMHz(".5", k));
  TEST_ASSERT_FALSE(parseMHz("921.5.5", k));

  char s[16];
  TEST_ASSERT_TRUE(formatearMHz(921500, s, sizeof(s)) > 0);
  TEST_ASSERT_EQUAL_STRING("921.500", s);
  formatearMHz(927750, s, sizeof(s));
  TEST_ASSERT_EQUAL_STRING("927.750", s);
  TEST_ASSERT_EQUAL_size_t(0, formatearMHz(921500, s, 4));   // sin sitio
  /* Ida y vuelta por toda la banda, kHz a kHz. */
  for (uint32_t f = BANDA_MIN_KHZ; f <= BANDA_MAX_KHZ; f += 37) {
    formatearMHz(f, s, sizeof(s));
    TEST_ASSERT_TRUE(parseMHz(s, k));
    TEST_ASSERT_EQUAL_UINT32(f, k);
  }
}

/* ==========================================================================
 *  3. Airtime: la tabla del §4.2, calculada con AN1200.13
 * ------------------------------------------------------------------------
 *  Tamaños de trama del §5: PING 18 · ACK 19 · PONG 21 · ALERT sin pos 17 ·
 *  ALERT con pos 25 · DATA de 32 caracteres 46 · DATA máxima 214.
 * ========================================================================*/
void test_simbolo_y_ldro() {
  ParamsRadio r = conPreset(Preset::NORMAL);     // SF11 / BW500
  TEST_ASSERT_EQUAL_UINT32(4096, simboloUs(r));  // 2^11 / 500 kHz
  TEST_ASSERT_FALSE(ldroActivo(r));
  r = conPreset(Preset::RAPIDO);                 // SF9 / BW500
  TEST_ASSERT_EQUAL_UINT32(1024, simboloUs(r));
  TEST_ASSERT_FALSE(ldroActivo(r));
  r = conPreset(Preset::LARGO);                  // SF12 / BW500
  TEST_ASSERT_EQUAL_UINT32(8192, simboloUs(r));
  TEST_ASSERT_FALSE(ldroActivo(r));
  r = conPreset(Preset::LAB125);                 // SF9 / BW125
  TEST_ASSERT_EQUAL_UINT32(4096, simboloUs(r));
  TEST_ASSERT_FALSE(ldroActivo(r));
  /* LDRO: se activa cuando T_sím >= 16 ms, es decir SF11 y SF12 a BW125. */
  r = paramsDefecto();
  r.bwKHz = 125;
  r.sf = 10;
  TEST_ASSERT_EQUAL_UINT32(8192, simboloUs(r));
  TEST_ASSERT_FALSE(ldroActivo(r));
  r.sf = 11;
  TEST_ASSERT_EQUAL_UINT32(16384, simboloUs(r));
  TEST_ASSERT_TRUE(ldroActivo(r));
  r.sf = 12;
  TEST_ASSERT_TRUE(ldroActivo(r));
}

void test_airtime_tabla_normal() {
  const ParamsRadio r = conPreset(Preset::NORMAL);
  TEST_ASSERT_EQUAL_UINT32(197632, airtimeUs(r, 18));    // PING
  TEST_ASSERT_EQUAL_UINT32(197632, airtimeUs(r, 19));    // ACK
  TEST_ASSERT_EQUAL_UINT32(197632, airtimeUs(r, 21));    // PONG
  TEST_ASSERT_EQUAL_UINT32(197632, airtimeUs(r, 17));    // ALERT sin posición
  TEST_ASSERT_EQUAL_UINT32(218112, airtimeUs(r, 25));    // ALERT con posición
  TEST_ASSERT_EQUAL_UINT32(300032, airtimeUs(r, 46));    // DATA de 32 caracteres
  TEST_ASSERT_EQUAL_UINT32(914432, airtimeUs(r, 214));   // DATA máxima
}

void test_airtime_tabla_rapido_largo_y_lab125() {
  ParamsRadio r = conPreset(Preset::RAPIDO);
  TEST_ASSERT_EQUAL_UINT32(54528, airtimeUs(r, 18));
  TEST_ASSERT_EQUAL_UINT32(49408, airtimeUs(r, 17));
  TEST_ASSERT_EQUAL_UINT32(59648, airtimeUs(r, 25));
  TEST_ASSERT_EQUAL_UINT32(85248, airtimeUs(r, 46));
  TEST_ASSERT_EQUAL_UINT32(274688, airtimeUs(r, 214));

  r = conPreset(Preset::LARGO);
  TEST_ASSERT_EQUAL_UINT32(354304, airtimeUs(r, 18));
  TEST_ASSERT_EQUAL_UINT32(395264, airtimeUs(r, 19));    // el 395 de la tabla
  TEST_ASSERT_EQUAL_UINT32(354304, airtimeUs(r, 17));
  TEST_ASSERT_EQUAL_UINT32(436224, airtimeUs(r, 25));
  TEST_ASSERT_EQUAL_UINT32(559104, airtimeUs(r, 46));
  TEST_ASSERT_EQUAL_UINT32(1705984, airtimeUs(r, 214));

  r = conPreset(Preset::LAB125);
  TEST_ASSERT_EQUAL_UINT32(218112, airtimeUs(r, 18));
  TEST_ASSERT_EQUAL_UINT32(197632, airtimeUs(r, 17));
  TEST_ASSERT_EQUAL_UINT32(238592, airtimeUs(r, 25));
  TEST_ASSERT_EQUAL_UINT32(340992, airtimeUs(r, 46));
  TEST_ASSERT_EQUAL_UINT32(1098752, airtimeUs(r, 214));
}

void test_airtime_ms_redondea_hacia_arriba() {
  const ParamsRadio r = conPreset(Preset::NORMAL);
  /* Hacia arriba a propósito: un timeout corto de más provoca reintentos
   * inútiles, uno largo de más sólo tarda un poco. */
  TEST_ASSERT_EQUAL_UINT32(198, airtimeMs(r, 18));
  TEST_ASSERT_EQUAL_UINT32(219, airtimeMs(r, 25));
  TEST_ASSERT_EQUAL_UINT32(301, airtimeMs(r, 46));
  TEST_ASSERT_EQUAL_UINT32(915, airtimeMs(r, 214));
}

void test_airtime_sf12_bw125_es_el_que_se_descarto() {
  /* El §4.2 dice que un PING en SF12/BW125 tarda 1.58 s, y por eso no hay
   * preset con 125 kHz y SF alto. */
  ParamsRadio r = paramsDefecto();
  r.sf = 12;
  r.bwKHz = 125;
  TEST_ASSERT_TRUE(ldroActivo(r));
  TEST_ASSERT_UINT32_WITHIN(20000, 1580000, airtimeUs(r, 18));
}

void test_airtime_crece_con_sf_bw_y_longitud() {
  ParamsRadio r = conPreset(Preset::NORMAL);
  /* Monótono en la longitud. */
  uint32_t prev = 0;
  for (uint16_t n = SOBRECARGA; n <= TRAMA_MAX; ++n) {
    const uint32_t t = airtimeUs(r, n);
    TEST_ASSERT_TRUE(t >= prev);
    prev = t;
  }
  /* Monótono en el SF, y BW mayor = menos tiempo. */
  prev = 0;
  for (uint8_t sf = 7; sf <= 12; ++sf) {
    r.sf = sf;
    const uint32_t t = airtimeUs(r, 18);
    TEST_ASSERT_TRUE(t > prev);
    prev = t;
  }
  r.sf = 11;
  r.bwKHz = 125;
  const uint32_t t125 = airtimeUs(r, 18);
  r.bwKHz = 500;
  TEST_ASSERT_TRUE(airtimeUs(r, 18) < t125);
}

void test_intervalo_minimo_de_range() {
  /* 10·(T_PING + T_PONG) redondeado hacia arriba a 100 ms (§7.6). */
  TEST_ASSERT_EQUAL_UINT32(4000, intervaloMinimoRangeMs(conPreset(Preset::NORMAL)));
  const uint32_t rapido = intervaloMinimoRangeMs(conPreset(Preset::RAPIDO));
  TEST_ASSERT_EQUAL_UINT32(1100, rapido);
  TEST_ASSERT_TRUE(intervaloMinimoRangeMs(conPreset(Preset::LARGO)) > 4000);
  /* Coherencia con el propio presupuesto: mandar un PING a ese ritmo cabe. */
  const ParamsRadio r = conPreset(Preset::NORMAL);
  const uint32_t nPorMinuto = 60000u / intervaloMinimoRangeMs(r);
  TEST_ASSERT_TRUE(nPorMinuto * (airtimeMs(r, 18) + airtimeMs(r, 21)) <= Ocupacion::LIMITE_MS);
}

/* ==========================================================================
 *  4. Presupuesto de ocupación (§7.6): <= 10 % en 60 s
 * ========================================================================*/
void test_ocupacion_vacia_permite() {
  Ocupacion o;
  TEST_ASSERT_EQUAL_UINT32(0, o.usadoMs(1000));
  TEST_ASSERT_EQUAL_UINT32(0, o.porMil(1000));
  TEST_ASSERT_TRUE(o.permite(1000, 200));
  TEST_ASSERT_TRUE(o.permite(1000, Ocupacion::LIMITE_MS));
  TEST_ASSERT_FALSE(o.permite(1000, Ocupacion::LIMITE_MS + 1));
}

void test_ocupacion_suma_y_bloquea_en_el_limite() {
  Ocupacion o;
  uint32_t t = 10000;
  /* 30 tramas de 200 ms = 6000 ms = el límite exacto. */
  for (int i = 0; i < 30; ++i) {
    TEST_ASSERT_TRUE(o.permite(t, 200));
    o.anotar(t, 200);
    t += 500;
  }
  TEST_ASSERT_EQUAL_UINT32(6000, o.usadoMs(t));
  TEST_ASSERT_EQUAL_UINT32(100, o.porMil(t));    // 10.0 %
  TEST_ASSERT_FALSE(o.permite(t, 1));            // ni un milisegundo más
}

void test_ocupacion_la_ventana_se_desliza() {
  Ocupacion o;
  o.anotar(10000, 6000);
  TEST_ASSERT_FALSE(o.permite(10000, 100));
  TEST_ASSERT_FALSE(o.permite(10000 + 59000, 100));   // sigue dentro del minuto
  /* Pasados los 60 s, el hueco se libera. */
  TEST_ASSERT_TRUE(o.permite(10000 + 61000, 100));
  TEST_ASSERT_EQUAL_UINT32(0, o.usadoMs(10000 + 61000));
}

void test_ocupacion_reiniciar_y_vuelta_de_millis() {
  Ocupacion o;
  o.anotar(1000, 5000);
  o.reiniciar();
  TEST_ASSERT_EQUAL_UINT32(0, o.usadoMs(1000));
  /* Vuelta de millis(): el error tiene que ir hacia PERMITIR, nunca a
   * bloquear para siempre (§7.6). */
  o.anotar(0xFFFFFF00u, 3000);
  TEST_ASSERT_TRUE(o.permite(0x00000100u, 3000));
}

void test_nombres_no_son_nulos() {
  TEST_ASSERT_EQUAL_STRING("OK", nombreErrorRadio(ErrorRadio::OK));
  /* Los nombres de error son el texto que sale por consola como
   * "ERR <motivo>": tienen que explicar el problema por sí solos. */
  TEST_ASSERT_EQUAL_STRING("FREQ_FUERA_DE_BANDA_915_928", nombreErrorRadio(ErrorRadio::FREQ));
  TEST_ASSERT_NOT_NULL(nombreErrorRadio(ErrorRadio::PWR));
  TEST_ASSERT_NOT_NULL(nombrePreset(Preset::A_MEDIDA));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_defecto_es_el_perfil_normal);
  RUN_TEST(test_presets);
  RUN_TEST(test_preset_conserva_freq_sync_pre_y_potencia);
  RUN_TEST(test_preset_a_medida_y_por_nombre);
  RUN_TEST(test_banda_el_canal_entero_tiene_que_caber);
  RUN_TEST(test_validar_devuelve_el_primer_campo_malo);
  RUN_TEST(test_bw_valido);
  RUN_TEST(test_parse_y_formateo_de_mhz);
  RUN_TEST(test_simbolo_y_ldro);
  RUN_TEST(test_airtime_tabla_normal);
  RUN_TEST(test_airtime_tabla_rapido_largo_y_lab125);
  RUN_TEST(test_airtime_ms_redondea_hacia_arriba);
  RUN_TEST(test_airtime_sf12_bw125_es_el_que_se_descarto);
  RUN_TEST(test_airtime_crece_con_sf_bw_y_longitud);
  RUN_TEST(test_intervalo_minimo_de_range);
  RUN_TEST(test_ocupacion_vacia_permite);
  RUN_TEST(test_ocupacion_suma_y_bloquea_en_el_limite);
  RUN_TEST(test_ocupacion_la_ventana_se_desliza);
  RUN_TEST(test_ocupacion_reiniciar_y_vuelta_de_millis);
  RUN_TEST(test_nombres_no_son_nulos);
  return UNITY_END();
}
