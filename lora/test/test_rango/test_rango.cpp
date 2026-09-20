/* ============================================================================
 *  Pruebas de la prueba de alcance RANGE (lib/geolora/rango)
 *  DISENO §9, §10, §13.3
 * ============================================================================
 *      cd lora && pio test -e native
 *
 *  RANGE es la herramienta con la que se va a medir el alcance real en Bogotá
 *  (§13.3), y buena parte de esa medida se hace SIN PC: los datos se guardan
 *  en un anillo de 512 medidas y el resumen se lee al volver a casa. Si el
 *  conteo de pérdidas o el número de punto estuvieran mal, la planilla saldría
 *  mal y habría que repetir el recorrido. De ahí que se pruebe entero aquí.
 *
 *  Esta suite no estaba en la lista del §13.1 (que hablaba de 9 suites): se
 *  añade porque rango.cpp es lógica pura y no tenerla probada sería el único
 *  hueco de lib/geolora.
 * ==========================================================================*/
#include <unity.h>

#include "carga.h"
#include "radio_params.h"
#include "rango.h"

using namespace geolora;

void setUp() {}
void tearDown() {}

static const uint16_t DST = 0x1A2B;

/* Una medida recibida con valores cómodos de comprobar. */
static void pong(Rango& r, uint16_t msgId, uint32_t rtt, int16_t rssiIda, int snrIdaDb, int16_t rssiVuelta,
                 int snrVueltaDb, uint8_t hops = 0) {
  Medida m;
  TEST_ASSERT_TRUE(r.alPong(msgId, rtt, rssiIda, static_cast<int8_t>(snrIdaDb * 4), rssiVuelta,
                            static_cast<int8_t>(snrVueltaDb * 4), hops, m));
}

/* ==========================================================================
 *  1. Arranque y parada
 * ========================================================================*/
void test_inactivo_al_empezar() {
  Rango r;
  TEST_ASSERT_FALSE(r.activo());
  TEST_ASSERT_EQUAL_size_t(0, r.numMedidas());
  TEST_ASSERT_FALSE(r.hayUltima());
  TEST_ASSERT_FALSE(r.tocaPing(1000));
  TEST_ASSERT_EQUAL_UINT32(0, r.enviados());
  TEST_ASSERT_EQUAL_UINT32(0, r.recibidos());
}

void test_iniciar_y_el_primer_ping_sale_en_el_acto() {
  Rango r;
  r.iniciar(DST, 5, 4000, 1000);
  TEST_ASSERT_TRUE(r.activo());
  TEST_ASSERT_EQUAL_HEX16(DST, r.destino());
  TEST_ASSERT_EQUAL_UINT32(4000, r.intervalo());
  TEST_ASSERT_TRUE(r.tocaPing(1000));      // sin esperar
}

void test_el_intervalo_se_respeta() {
  Rango r;
  r.iniciar(DST, 0, 4000, 1000);
  TEST_ASSERT_TRUE(r.tocaPing(1000));
  r.pingEnviado(1, 1000);
  TEST_ASSERT_FALSE(r.tocaPing(1000));
  TEST_ASSERT_FALSE(r.tocaPing(4999));     // todavía no
  TEST_ASSERT_TRUE(r.tocaPing(5000));      // 1000 + 4000
  r.pingEnviado(2, 5000);
  TEST_ASSERT_FALSE(r.tocaPing(5000));
  TEST_ASSERT_TRUE(r.tocaPing(9000));
}

void test_parar_a_mano() {
  Rango r;
  r.iniciar(DST, 0, 4000, 1000);
  r.pingEnviado(1, 1000);
  r.parar();
  TEST_ASSERT_FALSE(r.activo());
  TEST_ASSERT_FALSE(r.tocaPing(100000));
  /* Las medidas ya tomadas se conservan para RANGE INFORME. */
  Medida m;
  TEST_ASSERT_TRUE(r.alPerdido(1, m));
  TEST_ASSERT_EQUAL_size_t(1, r.numMedidas());
}

void test_se_para_sola_al_completar_los_n_pedidos() {
  Rango r;
  r.iniciar(DST, 3, 1000, 0);
  for (int k = 0; k < 3; ++k) {
    const uint32_t t = static_cast<uint32_t>(k) * 1000u;
    TEST_ASSERT_TRUE(r.tocaPing(t));
    r.pingEnviado(static_cast<uint16_t>(10 + k), t);
    pong(r, static_cast<uint16_t>(10 + k), 400, -90, 5, -92, 4);
  }
  /* Mandados los 3 y sin ninguno en curso, la prueba termina. */
  TEST_ASSERT_FALSE(r.activo());
  TEST_ASSERT_FALSE(r.tocaPing(100000));
  TEST_ASSERT_EQUAL_UINT32(3, r.enviados());
  TEST_ASSERT_EQUAL_UINT32(3, r.recibidos());
}

void test_no_termina_mientras_quede_uno_en_curso() {
  Rango r;
  r.iniciar(DST, 2, 1000, 0);
  r.pingEnviado(1, 0);
  r.pingEnviado(2, 1000);
  TEST_ASSERT_TRUE(r.activo());      // los 2 mandados, pero sin respuesta
  Medida m;
  TEST_ASSERT_TRUE(r.alPong(1, 400, -90, 20, -92, 16, 0, m));
  TEST_ASSERT_TRUE(r.activo());      // falta el 2
  TEST_ASSERT_TRUE(r.alPerdido(2, m));
  TEST_ASSERT_FALSE(r.activo());     // ahora sí
}

void test_sin_fin_con_n_cero() {
  Rango r;
  r.iniciar(DST, 0, 100, 0);
  for (int k = 0; k < 50; ++k) {
    const uint32_t t = static_cast<uint32_t>(k) * 100u;
    TEST_ASSERT_TRUE(r.tocaPing(t));
    r.pingEnviado(static_cast<uint16_t>(k + 1), t);
    pong(r, static_cast<uint16_t>(k + 1), 400, -90, 5, -92, 4);
  }
  TEST_ASSERT_TRUE(r.activo());      // no se para nunca sola
  TEST_ASSERT_EQUAL_UINT32(50, r.enviados());
}

/* ==========================================================================
 *  2. Medidas
 * ========================================================================*/
void test_pong_registra_los_dos_sentidos() {
  Rango r;
  r.iniciar(DST, 0, 1000, 0);
  r.pingEnviado(7, 0);
  Medida m;
  TEST_ASSERT_TRUE(r.alPong(7, 432, -80, snrAQ4(8.0f), -100, snrAQ4(-3.0f), 1, m));
  TEST_ASSERT_TRUE(m.ok);
  TEST_ASSERT_EQUAL_UINT32(432, m.rttMs);
  TEST_ASSERT_EQUAL_INT16(-80, m.rssiIda);
  TEST_ASSERT_EQUAL_INT8(snrAQ4(8.0f), m.snrIdaQ4);
  TEST_ASSERT_EQUAL_INT16(-100, m.rssiVuelta);
  TEST_ASSERT_EQUAL_INT8(snrAQ4(-3.0f), m.snrVueltaQ4);
  TEST_ASSERT_EQUAL_UINT8(1, m.hops);
  TEST_ASSERT_EQUAL_UINT16(1, m.seq);
  TEST_ASSERT_EQUAL_UINT16(1, m.punto);   // los puntos se numeran desde 1
  TEST_ASSERT_EQUAL_UINT32(1, r.recibidos());
  TEST_ASSERT_TRUE(r.hayUltima());
  TEST_ASSERT_EQUAL_UINT32(432, r.ultima().rttMs);
  TEST_ASSERT_EQUAL_size_t(1, r.numMedidas());
}

void test_perdido_se_registra_como_medida() {
  Rango r;
  r.iniciar(DST, 0, 1000, 0);
  r.pingEnviado(7, 0);
  Medida m;
  TEST_ASSERT_TRUE(r.alPerdido(7, m));
  TEST_ASSERT_FALSE(m.ok);
  TEST_ASSERT_EQUAL_UINT16(1, m.seq);
  TEST_ASSERT_EQUAL_UINT32(1, r.enviados());
  TEST_ASSERT_EQUAL_UINT32(0, r.recibidos());
  TEST_ASSERT_EQUAL_size_t(1, r.numMedidas());   // una pérdida es un dato
}

void test_msgid_ajeno_no_cuenta() {
  /* Un PONG de otro PING (o de otra prueba anterior) no puede colarse en la
   * planilla. */
  Rango r;
  r.iniciar(DST, 0, 1000, 0);
  r.pingEnviado(7, 0);
  Medida m;
  TEST_ASSERT_FALSE(r.alPong(999, 400, -90, 20, -92, 16, 0, m));
  TEST_ASSERT_FALSE(r.alPerdido(999, m));
  TEST_ASSERT_EQUAL_UINT32(0, r.recibidos());
  TEST_ASSERT_EQUAL_size_t(0, r.numMedidas());
  /* Y el mismo msgId no se puede contar dos veces. */
  TEST_ASSERT_TRUE(r.alPong(7, 400, -90, 20, -92, 16, 0, m));
  TEST_ASSERT_FALSE(r.alPong(7, 400, -90, 20, -92, 16, 0, m));
  TEST_ASSERT_EQUAL_UINT32(1, r.recibidos());
}

void test_la_secuencia_crece() {
  Rango r;
  r.iniciar(DST, 0, 1000, 0);
  for (int k = 0; k < 5; ++k) {
    r.pingEnviado(static_cast<uint16_t>(100 + k), static_cast<uint32_t>(k) * 1000u);
    Medida m;
    TEST_ASSERT_TRUE(r.alPong(static_cast<uint16_t>(100 + k), 400, -90, 20, -92, 16, 0, m));
    TEST_ASSERT_EQUAL_UINT16(k + 1, m.seq);
  }
}

/* ==========================================================================
 *  3. Puntos (waypoints): la pulsación corta del botón en el campo (§10)
 * ========================================================================*/
void test_marcar_punto() {
  /* Los puntos se numeran desde 1: en la planilla de campo (§13.3) se apunta
   * "punto 1, punto 2…", no "punto 0". */
  Rango r;
  r.iniciar(DST, 0, 1000, 0);
  TEST_ASSERT_EQUAL_UINT16(1, r.punto());
  r.pingEnviado(1, 0);
  Medida m;
  r.alPong(1, 400, -90, 20, -92, 16, 0, m);
  TEST_ASSERT_EQUAL_UINT16(1, m.punto);
  /* Se camina hasta el sitio siguiente y se pulsa el botón. */
  TEST_ASSERT_EQUAL_UINT16(2, r.marcarPunto());
  TEST_ASSERT_EQUAL_UINT16(2, r.punto());
  r.pingEnviado(2, 1000);
  r.alPong(2, 400, -90, 20, -92, 16, 0, m);
  TEST_ASSERT_EQUAL_UINT16(2, m.punto);
  TEST_ASSERT_EQUAL_UINT16(3, r.marcarPunto());
  r.pingEnviado(3, 2000);
  r.alPong(3, 400, -90, 20, -92, 16, 0, m);
  TEST_ASSERT_EQUAL_UINT16(3, m.punto);
}

void test_el_punto_de_una_medida_es_el_de_cuando_se_envio() {
  /* Si se marca el punto mientras un PING está en el aire, esa medida sigue
   * perteneciendo al punto ANTERIOR: se midió allí. */
  Rango r;
  r.iniciar(DST, 0, 1000, 0);
  r.pingEnviado(1, 0);
  r.marcarPunto();
  Medida m;
  TEST_ASSERT_TRUE(r.alPong(1, 400, -90, 20, -92, 16, 0, m));
  TEST_ASSERT_EQUAL_UINT16(1, m.punto);
}

/* ==========================================================================
 *  4. Resumen por punto (RANGE INFORME, §9)
 * ========================================================================*/
void test_resumen_de_un_punto() {
  Rango r;
  r.iniciar(DST, 0, 1000, 0);
  /* 4 PINGs: 3 vuelven y 1 se pierde -> 25 % de pérdidas. */
  const int16_t rssi[3] = {-90, -100, -80};
  const int snr[3] = {5, 1, 9};
  const uint32_t rtt[3] = {400, 500, 600};
  for (int k = 0; k < 3; ++k) {
    r.pingEnviado(static_cast<uint16_t>(k + 1), 0);
    pong(r, static_cast<uint16_t>(k + 1), rtt[k], rssi[k], snr[k],
         static_cast<int16_t>(rssi[k] - 2), snr[k] - 1);
  }
  r.pingEnviado(4, 0);
  Medida m;
  r.alPerdido(4, m);

  ResumenPunto res[4];
  TEST_ASSERT_EQUAL_size_t(1, r.resumir(res, 4));
  TEST_ASSERT_EQUAL_UINT16(1, res[0].punto);
  TEST_ASSERT_EQUAL_UINT16(4, res[0].enviados);
  TEST_ASSERT_EQUAL_UINT16(3, res[0].recibidos);
  TEST_ASSERT_EQUAL_UINT8(25, res[0].perdidasPct);
  /* Mín / media / máx del RSSI de ida: -100, -90, -80. */
  TEST_ASSERT_EQUAL_INT32(-100, res[0].rssiIda.min);
  TEST_ASSERT_EQUAL_INT32(-90, res[0].rssiIda.media);
  TEST_ASSERT_EQUAL_INT32(-80, res[0].rssiIda.max);
  /* Y el de vuelta, 2 dB peor. */
  TEST_ASSERT_EQUAL_INT32(-102, res[0].rssiVuelta.min);
  TEST_ASSERT_EQUAL_INT32(-92, res[0].rssiVuelta.media);
  TEST_ASSERT_EQUAL_INT32(-82, res[0].rssiVuelta.max);
  /* SNR en cuartos de dB: 1, 5, 9 dB -> 4, 20, 36. */
  TEST_ASSERT_EQUAL_INT32(4, res[0].snrIdaQ4.min);
  TEST_ASSERT_EQUAL_INT32(20, res[0].snrIdaQ4.media);
  TEST_ASSERT_EQUAL_INT32(36, res[0].snrIdaQ4.max);
  /* RTT medio de los que volvieron: (400+500+600)/3. */
  TEST_ASSERT_EQUAL_UINT32(500, res[0].rttMedioMs);
}

void test_resumen_por_puntos_en_orden_de_aparicion() {
  Rango r;
  r.iniciar(DST, 0, 1000, 0);
  uint16_t msg = 1;
  for (int p = 0; p < 3; ++p) {
    if (p > 0) r.marcarPunto();
    for (int k = 0; k < 2; ++k) {
      r.pingEnviado(msg, 0);
      pong(r, msg, 400 + 100u * static_cast<uint32_t>(p), static_cast<int16_t>(-80 - 10 * p), 5, -90, 4);
      ++msg;
    }
  }
  ResumenPunto res[8];
  TEST_ASSERT_EQUAL_size_t(3, r.resumir(res, 8));
  for (int p = 0; p < 3; ++p) {
    TEST_ASSERT_EQUAL_UINT16(p + 1, res[p].punto);
    TEST_ASSERT_EQUAL_UINT16(2, res[p].enviados);
    TEST_ASSERT_EQUAL_UINT16(2, res[p].recibidos);
    TEST_ASSERT_EQUAL_UINT8(0, res[p].perdidasPct);
    TEST_ASSERT_EQUAL_INT32(-80 - 10 * p, res[p].rssiIda.media);
    TEST_ASSERT_EQUAL_UINT32(400 + 100u * static_cast<uint32_t>(p), res[p].rttMedioMs);
  }
}

void test_resumen_de_un_punto_sin_ninguna_respuesta() {
  /* El punto donde ya no hay enlace: 100 % de pérdidas y ningún RSSI. Es el
   * dato que marca el límite de alcance (§13.3). */
  Rango r;
  r.iniciar(DST, 0, 1000, 0);
  Medida m;
  for (uint16_t k = 1; k <= 5; ++k) {
    r.pingEnviado(k, 0);
    r.alPerdido(k, m);
  }
  ResumenPunto res[2];
  TEST_ASSERT_EQUAL_size_t(1, r.resumir(res, 2));
  TEST_ASSERT_EQUAL_UINT16(5, res[0].enviados);
  TEST_ASSERT_EQUAL_UINT16(0, res[0].recibidos);
  TEST_ASSERT_EQUAL_UINT8(100, res[0].perdidasPct);
  TEST_ASSERT_EQUAL_UINT32(0, res[0].rttMedioMs);
}

void test_resumen_respeta_el_maximo_que_le_dan() {
  Rango r;
  r.iniciar(DST, 0, 1000, 0);
  uint16_t msg = 1;
  for (int p = 0; p < 5; ++p) {
    if (p > 0) r.marcarPunto();
    r.pingEnviado(msg, 0);
    pong(r, msg, 400, -90, 5, -92, 4);
    ++msg;
  }
  ResumenPunto res[2];
  TEST_ASSERT_EQUAL_size_t(2, r.resumir(res, 2));   // no escribe más de lo que cabe
  TEST_ASSERT_EQUAL_UINT16(1, res[0].punto);
  TEST_ASSERT_EQUAL_UINT16(2, res[1].punto);
}

void test_resumen_vacio() {
  Rango r;
  ResumenPunto res[2];
  TEST_ASSERT_EQUAL_size_t(0, r.resumir(res, 2));
  TEST_ASSERT_EQUAL_size_t(0, r.resumir(res, 0));
}

/* ==========================================================================
 *  5. El anillo de 512 medidas (§10)
 * ========================================================================*/
void test_el_anillo_se_llena_y_conserva_las_ultimas() {
  Rango r;
  r.iniciar(DST, 0, 1, 0);
  /* 600 medidas en un anillo de 512: se pierden las 88 primeras. */
  for (uint32_t k = 0; k < 600; ++k) {
    const uint16_t msg = static_cast<uint16_t>(k + 1);
    r.pingEnviado(msg, k);
    pong(r, msg, 400 + k, -90, 5, -92, 4);
  }
  TEST_ASSERT_EQUAL_size_t(Rango::CAPACIDAD, r.numMedidas());
  /* La más antigua que queda es la número 89 (rtt 400 + 88). */
  TEST_ASSERT_EQUAL_UINT32(400 + 88, r.medida(0).rttMs);
  TEST_ASSERT_EQUAL_UINT16(89, r.medida(0).seq);
  /* Y la última es la 600. */
  TEST_ASSERT_EQUAL_UINT32(400 + 599, r.medida(Rango::CAPACIDAD - 1).rttMs);
  TEST_ASSERT_EQUAL_UINT16(600, r.medida(Rango::CAPACIDAD - 1).seq);
  /* Las cuentas de la racha NO se pierden aunque el anillo dé la vuelta. */
  TEST_ASSERT_EQUAL_UINT32(600, r.enviados());
  TEST_ASSERT_EQUAL_UINT32(600, r.recibidos());
}

void test_borrar_medidas() {
  Rango r;
  r.iniciar(DST, 0, 1000, 0);
  r.pingEnviado(1, 0);
  Medida m;
  r.alPong(1, 400, -90, 20, -92, 16, 0, m);
  TEST_ASSERT_EQUAL_size_t(1, r.numMedidas());
  r.borrarMedidas();
  TEST_ASSERT_EQUAL_size_t(0, r.numMedidas());
}

void test_reiniciar_la_prueba_pone_las_cuentas_a_cero() {
  Rango r;
  r.iniciar(DST, 0, 1000, 0);
  r.pingEnviado(1, 0);
  Medida m;
  r.alPerdido(1, m);
  TEST_ASSERT_EQUAL_UINT32(1, r.enviados());
  /* Un RANGE nuevo empieza una racha nueva… */
  r.marcarPunto();
  const uint16_t puntoAntes = r.punto();
  r.iniciar(DST, 0, 2000, 5000);
  TEST_ASSERT_EQUAL_UINT32(0, r.enviados());
  TEST_ASSERT_EQUAL_UINT32(0, r.recibidos());
  TEST_ASSERT_EQUAL_UINT32(2000, r.intervalo());
  /* …pero el número de PUNTO no se reinicia a propósito: si en el mismo
   * recorrido se para y se vuelve a lanzar, los puntos del informe tienen que
   * seguir siendo correlativos y no repetirse. */
  TEST_ASSERT_EQUAL_UINT16(puntoAntes, r.punto());
}

void test_mas_pings_en_curso_de_los_que_caben() {
  /* Sólo se siguen N_EN_CURSO PINGs a la vez. Con el intervalo mínimo del
   * §7.6 (4 s en NORMAL) y un RTT de medio segundo nunca se llega a eso, pero
   * si se fuerza, lo que no puede pasar es que las cuentas se descuadren. */
  Rango r;
  r.iniciar(DST, 0, 1, 0);
  for (uint16_t k = 1; k <= Rango::N_EN_CURSO + 3; ++k) r.pingEnviado(k, 0);
  TEST_ASSERT_TRUE(r.enviados() >= static_cast<uint32_t>(Rango::N_EN_CURSO));
  /* Los que siguen en la lista se pueden cerrar; los que se cayeron, no. */
  Medida m;
  int cerrados = 0;
  for (uint16_t k = 1; k <= Rango::N_EN_CURSO + 3; ++k) {
    if (r.alPong(k, 400, -90, 20, -92, 16, 0, m)) ++cerrados;
  }
  TEST_ASSERT_TRUE(cerrados > 0);
  TEST_ASSERT_EQUAL_UINT32(static_cast<uint32_t>(cerrados), r.recibidos());
}

/* ==========================================================================
 *  6. Coherencia con el presupuesto de ocupación (§7.6)
 * ========================================================================*/
void test_el_intervalo_minimo_de_normal_es_el_documentado() {
  /* RANGE tiene que ajustarse al mínimo automático: 4 s en NORMAL. Quien lo
   * calcula es radio_params, y aquí se comprueba que la prueba funciona con
   * ese valor. */
  const uint32_t minimo = intervaloMinimoRangeMs(paramsDefecto());
  TEST_ASSERT_EQUAL_UINT32(4000, minimo);
  Rango r;
  r.iniciar(DST, 20, minimo, 0);
  uint32_t t = 0;
  int mandados = 0;
  while (r.activo() && t < 20u * minimo + minimo) {
    if (r.tocaPing(t)) {
      ++mandados;
      r.pingEnviado(static_cast<uint16_t>(mandados), t);
      pong(r, static_cast<uint16_t>(mandados), 430, -95, 2, -97, 1);
    }
    t += 100;
  }
  /* Los 20 PINGs de una parada del §13.3 caben en 20 intervalos. */
  TEST_ASSERT_EQUAL_INT(20, mandados);
  TEST_ASSERT_FALSE(r.activo());
  TEST_ASSERT_EQUAL_UINT32(20, r.recibidos());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_inactivo_al_empezar);
  RUN_TEST(test_iniciar_y_el_primer_ping_sale_en_el_acto);
  RUN_TEST(test_el_intervalo_se_respeta);
  RUN_TEST(test_parar_a_mano);
  RUN_TEST(test_se_para_sola_al_completar_los_n_pedidos);
  RUN_TEST(test_no_termina_mientras_quede_uno_en_curso);
  RUN_TEST(test_sin_fin_con_n_cero);
  RUN_TEST(test_pong_registra_los_dos_sentidos);
  RUN_TEST(test_perdido_se_registra_como_medida);
  RUN_TEST(test_msgid_ajeno_no_cuenta);
  RUN_TEST(test_la_secuencia_crece);
  RUN_TEST(test_marcar_punto);
  RUN_TEST(test_el_punto_de_una_medida_es_el_de_cuando_se_envio);
  RUN_TEST(test_resumen_de_un_punto);
  RUN_TEST(test_resumen_por_puntos_en_orden_de_aparicion);
  RUN_TEST(test_resumen_de_un_punto_sin_ninguna_respuesta);
  RUN_TEST(test_resumen_respeta_el_maximo_que_le_dan);
  RUN_TEST(test_resumen_vacio);
  RUN_TEST(test_el_anillo_se_llena_y_conserva_las_ultimas);
  RUN_TEST(test_borrar_medidas);
  RUN_TEST(test_reiniciar_la_prueba_pone_las_cuentas_a_cero);
  RUN_TEST(test_mas_pings_en_curso_de_los_que_caben);
  RUN_TEST(test_el_intervalo_minimo_de_normal_es_el_documentado);
  return UNITY_END();
}
