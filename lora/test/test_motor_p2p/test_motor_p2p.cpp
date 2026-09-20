/* ============================================================================
 *  Pruebas del motor en E1, punto a punto (lib/geolora/motor)
 *  DISENO §7 (fiabilidad), §13.1
 * ============================================================================
 *      cd lora && pio test -e native
 *
 *  Dos nodos sobre el canal simulado de ../ayuda_motor.h. Aquí se comprueba la
 *  política de fiabilidad completa, que es lo que hace que una ALERTA llegue:
 *
 *    · ACK a la primera y RTT medido;
 *    · ACK perdido -> reintento con el MISMO msgId -> re-ACK SIN segunda
 *      entrega (§7.1). Esto es lo que evita que el destinatario vea la misma
 *      alerta dos veces y lo que evita un FALLO falso en el emisor;
 *    · calendario de backoff exponencial + jitter (§7.3);
 *    · agotamiento -> EV FALLO;
 *    · ALERT: insiste, pasa al ciclo lento y la corta un CANCEL local;
 *    · PING -> PONG con RTT y calidad de los DOS sentidos.
 * ==========================================================================*/
#include <stdio.h>

#include "../ayuda_motor.h"

using namespace geolora;
using namespace ayuda;

void setUp() {}
void tearDown() {}

/* Dos nodos que se oyen, en E1. A = 0 (id 1), B = 1 (id 2). */
static void montarPareja(Canal& c, bool sinJitter = false) {
  c.anadir(cfgE1(), 0, sinJitter);
  c.anadir(cfgE1(), 0, sinJitter);
  c.enlazar(0, 1, 5, -90);
}

/* ==========================================================================
 *  1. DATA con ACK a la primera
 * ========================================================================*/
void test_data_entregado_y_confirmado() {
  Canal c;
  montarPareja(c);
  const int32_t msgId = c.nodo(0).enviarDatos(c.id(1), bytes("hola"), largo("hola"), c.ahora());
  TEST_ASSERT_TRUE(msgId >= 0);

  /* B lo entrega. */
  Evento ent;
  TEST_ASSERT_TRUE(c.esperar(1, TipoEvento::ENTREGADO, 3000, &ent));
  TEST_ASSERT_EQUAL_HEX16(c.id(0), ent.nodo);
  TEST_ASSERT_EQUAL_HEX16(msgId, ent.msgId);
  TEST_ASSERT_EQUAL_UINT8(4, ent.len);
  TEST_ASSERT_EQUAL_STRING("hola", reinterpret_cast<const char*>(ent.datos));
  TEST_ASSERT_EQUAL_UINT8(0, ent.hops);            // directa
  TEST_ASSERT_EQUAL_INT16(-90, ent.rssiVuelta);    // medida por B
  TEST_ASSERT_TRUE((ent.flags & FLAG_ACK_REQ) != 0);

  /* A recibe el ACK, con la calidad de la IDA medida por B. */
  Evento ack;
  TEST_ASSERT_TRUE(c.esperar(0, TipoEvento::ACK, 3000, &ack));
  TEST_ASSERT_EQUAL_HEX16(c.id(1), ack.nodo);
  TEST_ASSERT_EQUAL_HEX16(msgId, ack.msgId);
  TEST_ASSERT_EQUAL_UINT8(1, ack.intentos);        // a la primera
  TEST_ASSERT_TRUE(ack.hayIda);
  TEST_ASSERT_EQUAL_INT16(-90, ack.rssiIda);
  TEST_ASSERT_EQUAL_INT8(snrAQ4(5.0f), ack.snrIdaQ4);
  TEST_ASSERT_EQUAL_INT16(-90, ack.rssiVuelta);
  TEST_ASSERT_EQUAL_UINT8(0, ack.hopsIda);
  /* RTT = trama + giro + ACK: con NORMAL, del orden de 430 ms (§13.2). */
  TEST_ASSERT_UINT32_WITHIN(150, 430, ack.valor);

  /* Ni fallos, ni duplicados, ni reintentos. */
  TEST_ASSERT_EQUAL_INT(0, c.cuenta(-1, TipoEvento::FALLO));
  TEST_ASSERT_EQUAL_INT(0, c.cuenta(-1, TipoEvento::REINTENTO));
  TEST_ASSERT_EQUAL_UINT32(0, c.nodo(1).contadores().duplicados);
  TEST_ASSERT_EQUAL_UINT32(1, c.nodo(1).contadores().entregados);
  TEST_ASSERT_EQUAL_UINT32(1, c.nodo(1).contadores().acksTx);
  TEST_ASSERT_EQUAL_UINT32(1, c.nodo(0).contadores().acksRx);
  /* Y el emisor se queda sin nada pendiente. */
  TEST_ASSERT_EQUAL_INT(0, c.nodo(0).salientesVivas());
}

void test_cien_mensajes_seguidos_sin_duplicados() {
  /* El criterio de banco del §13.2.4: 100 SEND, 100 % confirmados y CERO
   * duplicados entregados.
   *
   * Ojo con el ritmo: una DATA no está exenta del presupuesto de ocupación
   * del 10 % (§7.6), así que a partir de ~30 mensajes por minuto el motor
   * RETRASA las salidas a propósito. Por eso cada ACK se espera con holgura:
   * lo que se comprueba es que no se pierde ni se duplica nada, no la
   * velocidad. */
  Canal c(5);
  montarPareja(c);
  for (int k = 0; k < 100; ++k) {
    char t[16];
    snprintf(t, sizeof(t), "m%d", k);
    TEST_ASSERT_TRUE(c.nodo(0).enviarDatos(c.id(1), bytes(t), largo(t), c.ahora()) >= 0);
    /* Se espera el ACK antes del siguiente, como haría un script. */
    TEST_ASSERT_TRUE(c.esperar(0, TipoEvento::ACK, 90000));
  }
  TEST_ASSERT_EQUAL_INT(100, c.cuenta(1, TipoEvento::ENTREGADO));
  TEST_ASSERT_EQUAL_INT(100, c.cuenta(0, TipoEvento::ACK));
  TEST_ASSERT_EQUAL_INT(0, c.cuenta(-1, TipoEvento::FALLO));
  TEST_ASSERT_EQUAL_UINT32(0, c.nodo(1).contadores().duplicados);
  TEST_ASSERT_EQUAL_UINT32(100, c.nodo(1).contadores().entregados);
  /* Cada mensaje llevó un msgId distinto. */
  TEST_ASSERT_EQUAL_UINT32(100, c.nodo(0).contadores().acksRx);
}

void test_msgid_crece_y_no_se_reusa() {
  Canal c(2);
  montarPareja(c);
  const uint16_t primero = c.nodo(0).proximoMsgId();
  const int32_t a = c.nodo(0).enviarDatos(c.id(1), bytes("a"), 1, c.ahora());
  const int32_t b = c.nodo(0).enviarDatos(c.id(1), bytes("b"), 1, c.ahora());
  TEST_ASSERT_EQUAL_HEX16(primero, a);
  TEST_ASSERT_EQUAL_HEX16(static_cast<uint16_t>(primero + 1), b);
  TEST_ASSERT_NOT_EQUAL(a, b);
}

/* ==========================================================================
 *  2. ACK perdido: reintento + re-ACK sin segunda entrega (§7.1)
 * ------------------------------------------------------------------------
 *  Es LA prueba de esta suite. Si el re-ACK no existiera, A daría FALLO de
 *  algo que sí llegó; si el duplicado se entregara, B vería la alerta dos
 *  veces.
 * ========================================================================*/
void test_ack_perdido_reintento_mismo_msgid_y_sin_segunda_entrega() {
  Canal c(2);
  montarPareja(c);
  /* A no oye nada (el ACK se pierde), pero B sí oye a A. */
  c.perder(0, true);
  const int32_t msgId = c.nodo(0).enviarDatos(c.id(1), bytes("una vez"), largo("una vez"), c.ahora());
  TEST_ASSERT_TRUE(c.esperar(1, TipoEvento::ENTREGADO, 3000));
  /* El ACK sale tras el retardo de giro de 30 ms y tarda su airtime: hay que
   * dejarlo terminar antes de contarlo. */
  c.avanzar(1000);
  TEST_ASSERT_EQUAL_UINT32(1, c.nodo(1).contadores().acksTx);

  /* A no recibe el ACK y reintenta. */
  Evento re;
  TEST_ASSERT_TRUE(c.esperar(0, TipoEvento::REINTENTO, 5000, &re));
  TEST_ASSERT_EQUAL_HEX16(msgId, re.msgId);        // el MISMO msgId (§7.3)
  TEST_ASSERT_EQUAL_UINT8(1, re.intentos);

  /* B ve el duplicado: lo DESCARTA y lo vuelve a confirmar. */
  c.avanzar(2000);
  TEST_ASSERT_EQUAL_INT(1, c.cuenta(1, TipoEvento::ENTREGADO));   // UNA sola entrega
  TEST_ASSERT_EQUAL_UINT32(1, c.nodo(1).contadores().entregados);
  TEST_ASSERT_TRUE(c.nodo(1).contadores().duplicados >= 1);
  TEST_ASSERT_TRUE(c.nodo(1).contadores().acksTx >= 2);           // re-ACK (§7.1)

  /* Ahora A vuelve a oír: el siguiente reintento se confirma y no hay FALLO. */
  c.perder(0, false);
  Evento ack;
  TEST_ASSERT_TRUE(c.esperar(0, TipoEvento::ACK, 20000, &ack));
  TEST_ASSERT_EQUAL_HEX16(msgId, ack.msgId);
  TEST_ASSERT_TRUE(ack.intentos >= 2);
  TEST_ASSERT_EQUAL_INT(0, c.cuenta(-1, TipoEvento::FALLO));
  TEST_ASSERT_EQUAL_INT(1, c.cuenta(1, TipoEvento::ENTREGADO));
}

void test_perdida_simulada_en_rx_ejercita_los_reintentos() {
  /* Es el equivalente nativo de la orden PERDIDA <pct> (§9) que usa la prueba
   * de banco §13.2.5. Con una fuente de azar fija, es repetible. */
  Canal c(2);
  montarPareja(c);
  c.nodo(1).setPerdida(60);
  TEST_ASSERT_EQUAL_UINT8(60, c.nodo(1).perdida());
  int exitos = 0;
  for (int k = 0; k < 20; ++k) {
    c.nodo(0).enviarDatos(c.id(1), bytes("x"), 1, c.ahora());
    if (c.esperar(0, TipoEvento::ACK, 40000)) ++exitos;
    c.avanzar(500);
  }
  /* Con 4 intentos y 60 % de pérdida por intento, la probabilidad de fallar
   * los cuatro es 0.6^4 ≈ 13 %: los reintentos tienen que rescatar la gran
   * mayoría. */
  TEST_ASSERT_TRUE(exitos >= 14);
  TEST_ASSERT_TRUE(c.nodo(1).contadores().perdidaSimulada > 0);
  TEST_ASSERT_TRUE(c.cuenta(0, TipoEvento::REINTENTO) > 0);
  /* Y, pese a todos los reintentos, B entregó EXACTAMENTE uno por mensaje
   * confirmado: ni duplicados, ni entregas fantasma. Los que fallaron son los
   * que no llegaron ni una vez (aquí sólo se pierde en el RX de B, así que un
   * mensaje entregado siempre acaba confirmado). */
  TEST_ASSERT_EQUAL_INT(exitos, c.cuenta(1, TipoEvento::ENTREGADO));
  TEST_ASSERT_EQUAL_INT(20 - exitos, c.cuenta(0, TipoEvento::FALLO));
}

/* ==========================================================================
 *  3. Calendario de reintentos (§7.3)
 * ========================================================================*/
void test_backoff_exponencial_sin_jitter() {
  /* Con la fuente de azar a 0 no hay jitter y los tiempos son los de la
   * fórmula: espera_k = T_ack + min(B0·2^(k-1), B_max), con
   * B0 = T_aire(trama) + 100 ms. */
  Canal c(1);
  c.anadir(cfgE1(), 0, true);
  c.anadir(cfgE1(), 0, true);
  c.dirigir(0, 1);            // A alcanza a B, pero B NO alcanza a A
  c.nodo(1).setPerdida(100);  // y B ni siquiera lo procesa: nunca hay ACK

  const ParamsRadio r = paramsDefecto();
  const uint8_t lenTrama = static_cast<uint8_t>(SOBRECARGA + 1);
  const uint32_t tAck = c.nodo(0).timeoutAck(lenTrama, SOBRECARGA + LEN_ACK, 0, c.id(1));
  const uint32_t b0 = airtimeMs(r, lenTrama) + 100u;

  c.nodo(0).enviarDatos(c.id(1), bytes("x"), 1, c.ahora());
  /* Se apuntan los instantes de los 3 reintentos. */
  uint32_t t[4] = {0, 0, 0, 0};
  int n = 0;
  const int desde0 = c.numEventos();
  c.avanzar(60000);
  for (int k = desde0; k < c.numEventos(); ++k) {
    const EventoDe& e = c.evento(k);
    if (e.nodo == 0 && e.ev.tipo == TipoEvento::REINTENTO && n < 4) t[n++] = e.t;
  }
  TEST_ASSERT_EQUAL_INT(3, n);   // 1 + 3 reintentos = DATA_INTENTOS
  /* Entre dos reintentos pasa: lo que tarda en el aire el reintento anterior,
   * más el timeout del ACK, más el término exponencial B0·2^(k-1). */
  const uint32_t aire = airtimeMs(r, lenTrama);
  const uint32_t hueco1 = t[1] - t[0];
  const uint32_t hueco2 = t[2] - t[1];
  TEST_ASSERT_UINT32_WITHIN(20, aire + tAck + 2u * b0, hueco1);
  TEST_ASSERT_UINT32_WITHIN(20, aire + tAck + 4u * b0, hueco2);
  TEST_ASSERT_TRUE(hueco2 > hueco1);   // crece
  /* Y acaba en FALLO. */
  const EventoDe* f = c.primero(0, TipoEvento::FALLO);
  TEST_ASSERT_NOT_NULL(f);
  TEST_ASSERT_EQUAL_UINT8(DATA_INTENTOS, f->ev.intentos);
}

void test_el_jitter_descorrelaciona_y_esta_acotado() {
  /* El jitter es U(0, B0) (§7.3): tiene que existir (dos nodos que colisionan
   * no repiten a la vez) y estar ACOTADO (si no, un reintento llegaría tarde
   * para la caché de vistos). */
  const ParamsRadio r = paramsDefecto();
  const uint8_t lenTrama = static_cast<uint8_t>(SOBRECARGA + 1);
  const uint32_t b0 = airtimeMs(r, lenTrama) + 100u;
  uint32_t huecoMin = 0xFFFFFFFFu;
  uint32_t huecoMax = 0;
  uint32_t sinJitter = 0;
  for (int semilla = 0; semilla < 6; ++semilla) {
    Canal c(1);
    c.anadir(cfgE1());
    c.anadir(cfgE1());
    c.dirigir(0, 1);
    c.nodo(1).setPerdida(100);
    /* Cada pasada con una semilla distinta: el jitter tiene que moverse. */
    c.sembrar(0, 0xA5A50000u + 0x9E3779B9u * static_cast<uint32_t>(semilla));
    sinJitter = airtimeMs(r, lenTrama) +
                c.nodo(0).timeoutAck(lenTrama, SOBRECARGA + LEN_ACK, 0, c.id(1)) + 2u * b0;
    const int desde = c.numEventos();
    c.nodo(0).enviarDatos(c.id(1), bytes("x"), 1, c.ahora());
    c.avanzar(30000);
    uint32_t t[2] = {0, 0};
    int n = 0;
    for (int k = desde; k < c.numEventos(); ++k) {
      const EventoDe& e = c.evento(k);
      if (e.nodo == 0 && e.ev.tipo == TipoEvento::REINTENTO && n < 2) t[n++] = e.t;
    }
    TEST_ASSERT_EQUAL_INT(2, n);
    const uint32_t hueco = t[1] - t[0];
    if (hueco < huecoMin) huecoMin = hueco;
    if (hueco > huecoMax) huecoMax = hueco;
  }
  /* Acotado: el hueco nunca se pasa del valor sin jitter más B0. */
  TEST_ASSERT_TRUE(huecoMin >= sinJitter - 20);
  TEST_ASSERT_TRUE(huecoMax <= sinJitter + b0 + 20);
  /* Y varía de verdad. */
  TEST_ASSERT_TRUE(huecoMax > huecoMin);
}

void test_fallo_tras_agotar_los_intentos() {
  /* El caso de banco §13.2.6: se apaga B y SEND acaba en EV FALLO. */
  Canal c(2);
  montarPareja(c);
  c.perder(1, true);   // B no oye nada
  const int32_t msgId = c.nodo(0).enviarDatos(c.id(1), bytes("nadie"), 5, c.ahora());
  Evento f;
  TEST_ASSERT_TRUE(c.esperar(0, TipoEvento::FALLO, 90000, &f));
  TEST_ASSERT_EQUAL_HEX16(msgId, f.msgId);
  TEST_ASSERT_EQUAL_HEX16(c.id(1), f.nodo);
  TEST_ASSERT_EQUAL_UINT8(DATA_INTENTOS, f.intentos);   // 1 + 3
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Tipo::DATA), f.tipoTrama);
  TEST_ASSERT_EQUAL_INT(0, c.cuenta(-1, TipoEvento::ACK));
  TEST_ASSERT_EQUAL_UINT32(1, c.nodo(0).contadores().fallos);
  /* Y el hueco queda libre: el nodo no se atasca. */
  TEST_ASSERT_EQUAL_INT(0, c.nodo(0).salientesVivas());
  TEST_ASSERT_EQUAL_UINT32(DATA_INTENTOS, c.txs(0));
}

/* ==========================================================================
 *  4. ALERT: la política de insistir (§7.3)
 * ========================================================================*/
void test_alerta_llega_y_lleva_el_token_y_la_posicion() {
  Canal c;
  montarPareja(c);
  /* Para que alguien conteste a una ALERT dirigida a 0xFFFE hace falta un
   * nodo con rol PASARELA (§14). */
  ConfigNodo cb = c.nodo(1).config();
  cb.rol = Rol::PASARELA;
  c.nodo(1).configurar(cb);

  const int32_t msgId = c.nodo(0).enviarAlerta(ALERTA_2, true, 46097100, -740817500, true, c.ahora());
  TEST_ASSERT_TRUE(msgId >= 0);
  Evento al;
  TEST_ASSERT_TRUE(c.esperar(1, TipoEvento::ALERTA, 3000, &al));
  TEST_ASSERT_EQUAL_HEX16(c.id(0), al.nodo);
  TEST_ASSERT_EQUAL_UINT8(ALERTA_2, al.alerta.codigo);
  TEST_ASSERT_EQUAL_STRING("ALERT:2", tokenAlerta(al.alerta.codigo));   // token congelado
  TEST_ASSERT_TRUE(al.alerta.conPosicion());
  TEST_ASSERT_EQUAL_INT32(46097100, al.alerta.lat1e7);
  TEST_ASSERT_EQUAL_INT32(-740817500, al.alerta.lon1e7);
  TEST_ASSERT_TRUE((al.flags & FLAG_PRUEBA) != 0);      // no la escalaría una pasarela
  TEST_ASSERT_TRUE((al.flags & FLAG_ACK_REQ) != 0);     // la ALERT SIEMPRE pide ACK

  /* Y la pasarela la confirma. */
  Evento ack;
  TEST_ASSERT_TRUE(c.esperar(0, TipoEvento::ACK, 3000, &ack));
  TEST_ASSERT_EQUAL_HEX16(msgId, ack.msgId);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Tipo::ALERT), ack.tipoTrama);
  TEST_ASSERT_FALSE(c.nodo(0).alertaActiva());   // ya no hay nada pendiente
}

void test_alerta_sin_pasarela_insiste_y_pasa_al_ciclo_lento() {
  Canal c(10);
  montarPareja(c);
  c.perder(1, true);   // no hay nadie escuchando
  c.nodo(0).enviarAlerta(ALERTA_1, false, 0, 0, false, c.ahora());
  TEST_ASSERT_TRUE(c.nodo(0).alertaActiva());

  /* Los primeros 8 intentos son rápidos (§7.3). */
  c.avanzar(120000);
  TEST_ASSERT_TRUE(c.txs(0) >= ALERTA_RAPIDOS);
  /* Y a partir de ahí avisa en cada ciclo lento: nunca se rinde en silencio. */
  Evento sa;
  TEST_ASSERT_TRUE(c.esperar(0, TipoEvento::ALERTA_SIN_ACK, 120000, &sa));
  TEST_ASSERT_EQUAL_UINT8(ALERTA_1, sa.alerta.codigo);
  TEST_ASSERT_TRUE(sa.intentos >= ALERTA_RAPIDOS);
  TEST_ASSERT_TRUE(c.nodo(0).alertaActiva());
  TEST_ASSERT_EQUAL_INT(0, c.cuenta(0, TipoEvento::FALLO));   // todavía no
}

void test_alerta_se_rinde_a_los_diez_minutos_con_fallo() {
  Canal c(20);
  montarPareja(c);
  c.perder(1, true);
  c.nodo(0).enviarAlerta(ALERTA_2, false, 0, 0, false, c.ahora());
  /* A los 10 min (ALERTA_MAX_MS) para, pero avisando con FALLO. */
  Evento f;
  TEST_ASSERT_TRUE(c.esperar(0, TipoEvento::FALLO, ALERTA_MAX_MS + 60000, &f));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Tipo::ALERT), f.tipoTrama);
  TEST_ASSERT_TRUE(f.valor >= ALERTA_MAX_MS);
  TEST_ASSERT_FALSE(c.nodo(0).alertaActiva());
  TEST_ASSERT_TRUE(c.cuenta(0, TipoEvento::ALERTA_SIN_ACK) > 5);
}

void test_cancel_corta_la_insistencia_y_reusa_el_seq() {
  Canal c(10);
  montarPareja(c);
  c.perder(1, true);
  c.nodo(0).enviarAlerta(ALERTA_2, false, 0, 0, false, c.ahora());
  const uint8_t seq = c.nodo(0).seqAlerta();
  TEST_ASSERT_TRUE(c.nodo(0).alertaActiva());
  c.avanzar(30000);
  TEST_ASSERT_TRUE(c.txs(0) >= 2);   // estaba insistiendo

  /* El CANCEL local para los reintentos de esa alerta (§7.3). */
  c.reiniciarTxs();
  TEST_ASSERT_TRUE(c.nodo(0).enviarAlerta(ALERTA_CANCEL, false, 0, 0, false, c.ahora()) >= 0);
  /* Y lleva el MISMO seqAlerta, para que la pasarela los empareje (§5.4). */
  TEST_ASSERT_EQUAL_UINT8(seq, c.nodo(0).seqAlerta());
  /* Ahora el único que insiste es el CANCEL. */
  c.avanzar(60000);
  TEST_ASSERT_EQUAL_INT(1, c.nodo(0).salientesVivas());
}

void test_alerta_cancel_llega_con_el_seq_de_la_que_anula() {
  Canal c;
  montarPareja(c);
  ConfigNodo cb = c.nodo(1).config();
  cb.rol = Rol::PASARELA;
  c.nodo(1).configurar(cb);

  c.nodo(0).enviarAlerta(ALERTA_2, false, 0, 0, false, c.ahora());
  Evento a1;
  TEST_ASSERT_TRUE(c.esperar(1, TipoEvento::ALERTA, 3000, &a1));
  TEST_ASSERT_TRUE(c.esperar(0, TipoEvento::ACK, 3000));

  c.nodo(0).enviarAlerta(ALERTA_CANCEL, false, 0, 0, false, c.ahora());
  Evento a2;
  TEST_ASSERT_TRUE(c.esperar(1, TipoEvento::ALERTA, 3000, &a2));
  TEST_ASSERT_EQUAL_UINT8(ALERTA_CANCEL, a2.alerta.codigo);
  TEST_ASSERT_EQUAL_STRING("CANCEL", tokenAlerta(a2.alerta.codigo));
  TEST_ASSERT_EQUAL_UINT8(a1.alerta.seq, a2.alerta.seq);   // el mismo número
}

void test_alertas_sucesivas_llevan_seq_distinto() {
  Canal c;
  montarPareja(c);
  ConfigNodo cb = c.nodo(1).config();
  cb.rol = Rol::PASARELA;
  c.nodo(1).configurar(cb);
  uint8_t seqs[3] = {0, 0, 0};
  for (int k = 0; k < 3; ++k) {
    c.nodo(0).enviarAlerta(ALERTA_1, false, 0, 0, false, c.ahora());
    Evento a;
    TEST_ASSERT_TRUE(c.esperar(1, TipoEvento::ALERTA, 5000, &a));
    seqs[k] = a.alerta.seq;
    TEST_ASSERT_TRUE(c.esperar(0, TipoEvento::ACK, 5000));
  }
  TEST_ASSERT_NOT_EQUAL(seqs[0], seqs[1]);
  TEST_ASSERT_NOT_EQUAL(seqs[1], seqs[2]);
}

void test_alerta_rechaza_codigos_que_no_son_tokens() {
  Canal c;
  montarPareja(c);
  TEST_ASSERT_EQUAL_INT32(-1, c.nodo(0).enviarAlerta(0, false, 0, 0, false, c.ahora()));
  TEST_ASSERT_EQUAL_INT32(-1, c.nodo(0).enviarAlerta(4, false, 0, 0, false, c.ahora()));
  TEST_ASSERT_EQUAL_INT32(-1, c.nodo(0).enviarAlerta(ALERTA_1, true, 900000001, 0, false, c.ahora()));
}

/* ==========================================================================
 *  5. PING / PONG (§5.2): la medida del enlace en los DOS sentidos
 * ========================================================================*/
void test_ping_pong_con_rtt_y_calidad_de_los_dos_sentidos() {
  Canal c;
  c.anadir(cfgE1());
  c.anadir(cfgE1());
  /* Enlace ASIMÉTRICO: A->B con −80 dBm y B->A con −100 dBm. Así se ve que
   * cada sentido se mide donde toca. */
  c.dirigir(0, 1, 8, -80);
  c.dirigir(1, 0, -3, -100);

  TEST_ASSERT_TRUE(c.nodo(0).enviarPing(c.id(1), 0, c.ahora()) >= 0);
  Evento p;
  TEST_ASSERT_TRUE(c.esperar(0, TipoEvento::PONG, 3000, &p));
  TEST_ASSERT_EQUAL_HEX16(c.id(1), p.nodo);
  TEST_ASSERT_TRUE(p.hayIda);
  TEST_ASSERT_EQUAL_INT16(-80, p.rssiIda);              // medido por B
  TEST_ASSERT_EQUAL_INT8(snrAQ4(8.0f), p.snrIdaQ4);
  TEST_ASSERT_EQUAL_INT16(-100, p.rssiVuelta);          // medido aquí
  TEST_ASSERT_EQUAL_INT8(snrAQ4(-3.0f), p.snrVueltaQ4);
  TEST_ASSERT_EQUAL_UINT8(0, p.hops);
  TEST_ASSERT_EQUAL_UINT8(0, p.hopsIda);
  /* RTT ≈ T_PING + giro + T_PONG ≈ 430 ms en NORMAL (§13.2.3). */
  TEST_ASSERT_UINT32_WITHIN(150, 430, p.valor);
  TEST_ASSERT_EQUAL_UINT32(1, c.nodo(1).contadores().pingsRx);
  TEST_ASSERT_EQUAL_UINT32(1, c.nodo(0).contadores().pongsRx);
  /* El PING no se reintenta: cuenta como pérdida si no vuelve (§7.3). */
  TEST_ASSERT_EQUAL_INT(0, c.cuenta(0, TipoEvento::REINTENTO));
  /* Y B queda apuntado como vecino directo, que es lo que usa el botón PRG. */
  uint16_t v = 0;
  TEST_ASSERT_TRUE(c.nodo(0).ultimoVecino(v));
  TEST_ASSERT_EQUAL_HEX16(c.id(1), v);
}

void test_ping_perdido() {
  Canal c(2);
  montarPareja(c);
  c.perder(1, true);
  const int32_t msgId = c.nodo(0).enviarPing(c.id(1), 0, c.ahora());
  Evento p;
  TEST_ASSERT_TRUE(c.esperar(0, TipoEvento::PING_PERDIDO, 5000, &p));
  TEST_ASSERT_EQUAL_HEX16(msgId, p.msgId);
  TEST_ASSERT_EQUAL_HEX16(c.id(1), p.nodo);
  TEST_ASSERT_EQUAL_UINT32(1, c.txs(0));   // una sola transmisión, sin reintento
  TEST_ASSERT_EQUAL_INT(0, c.nodo(0).salientesVivas());
}

void test_ping_con_relleno_alarga_la_trama() {
  Canal c;
  montarPareja(c);
  TEST_ASSERT_TRUE(c.nodo(0).enviarPing(c.id(1), 100, c.ahora()) >= 0);
  Evento p;
  TEST_ASSERT_TRUE(c.esperar(0, TipoEvento::PONG, 5000, &p));
  /* El RTT crece con el tamaño: un PING de 118 B tarda más en el aire. */
  const ParamsRadio r = paramsDefecto();
  TEST_ASSERT_TRUE(p.valor >= airtimeMs(r, SOBRECARGA + LEN_PING_MIN + 100));
}

void test_ping_broadcast_descubre_vecinos() {
  /* PING * hace de descubrimiento sin un tipo HELLO aparte (§8.1). */
  Canal c(2);
  c.anadir(cfgE1());
  c.anadir(cfgE1());
  c.anadir(cfgE1());
  c.enlazarTodos();
  TEST_ASSERT_TRUE(c.nodo(0).enviarPing(DIR_BROADCAST, 0, c.ahora()) >= 0);
  /* Los dos contestan, escalonados en U(0, 2 s) para no chocar. */
  Evento fin;
  TEST_ASSERT_TRUE(c.esperar(0, TipoEvento::PING_FIN, 20000, &fin));
  TEST_ASSERT_EQUAL_UINT32(2, fin.n);
  TEST_ASSERT_EQUAL_INT(2, c.cuenta(0, TipoEvento::PONG));
  TEST_ASSERT_EQUAL_UINT32(2, c.nodo(0).contadores().pongsRx);
}

/* ==========================================================================
 *  6. Casos de borde del emisor
 * ========================================================================*/
void test_no_se_envia_a_uno_mismo_ni_a_ids_reservados() {
  Canal c;
  montarPareja(c);
  TEST_ASSERT_EQUAL_INT32(-1, c.nodo(0).enviarDatos(c.id(0), bytes("yo"), 2, c.ahora()));
  TEST_ASSERT_EQUAL_INT32(-1, c.nodo(0).enviarDatos(0x0000, bytes("x"), 1, c.ahora()));
  TEST_ASSERT_EQUAL_INT32(-1, c.nodo(0).enviarDatos(DIR_PASARELA, bytes("x"), 1, c.ahora()));
  TEST_ASSERT_EQUAL_INT32(-1, c.nodo(0).enviarPing(0xFFF0, 0, c.ahora()));
  /* El broadcast SÍ vale como destino de DATA y de PING. */
  TEST_ASSERT_TRUE(c.nodo(0).enviarDatos(DIR_BROADCAST, bytes("x"), 1, c.ahora()) >= 0);
}

void test_la_cola_de_salientes_se_llena_y_lo_dice() {
  Canal c;
  montarPareja(c);
  c.perder(1, true);
  int aceptados = 0;
  for (int k = 0; k < 20; ++k) {
    if (c.nodo(0).enviarDatos(c.id(1), bytes("x"), 1, c.ahora()) >= 0) ++aceptados;
  }
  /* No hay memoria dinámica: 8 huecos y se acabó (§12.2). Mejor un -1 claro
   * que un desbordamiento. */
  TEST_ASSERT_EQUAL_INT(Motor::N_SALIENTES, aceptados);
  TEST_ASSERT_EQUAL_INT(Motor::N_SALIENTES, c.nodo(0).salientesVivas());
}

void test_carga_maxima_ida_y_vuelta() {
  Canal c(2);
  montarPareja(c);
  uint8_t carga[LEN_MAX];
  for (int i = 0; i < LEN_MAX; ++i) carga[i] = static_cast<uint8_t>('A' + i % 26);
  TEST_ASSERT_TRUE(c.nodo(0).enviarDatos(c.id(1), carga, LEN_MAX, c.ahora()) >= 0);
  Evento e;
  TEST_ASSERT_TRUE(c.esperar(1, TipoEvento::ENTREGADO, 10000, &e));
  TEST_ASSERT_EQUAL_UINT8(LEN_MAX, e.len);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(carga, e.datos, LEN_MAX);
  TEST_ASSERT_TRUE(c.esperar(0, TipoEvento::ACK, 10000));
  /* Y se rechaza lo que no cabe. */
  TEST_ASSERT_EQUAL_INT32(-1, c.nodo(0).enviarDatos(c.id(1), nullptr, 10, c.ahora()));
}

void test_broadcast_en_e1_no_espera_confirmacion() {
  /* Con ttl 0 un broadcast no tiene eco posible: se manda y se olvida
   * (nada de FALLO ni de reintentos inútiles). */
  Canal c;
  c.anadir(cfgE1());
  c.anadir(cfgE1());
  c.anadir(cfgE1());
  c.enlazarTodos();
  TEST_ASSERT_TRUE(c.nodo(0).enviarDatos(DIR_BROADCAST, bytes("a todos"), 7, c.ahora()) >= 0);
  c.avanzar(5000);
  TEST_ASSERT_EQUAL_INT(1, c.cuenta(1, TipoEvento::ENTREGADO));
  TEST_ASSERT_EQUAL_INT(1, c.cuenta(2, TipoEvento::ENTREGADO));
  TEST_ASSERT_EQUAL_INT(0, c.cuenta(-1, TipoEvento::FALLO));
  TEST_ASSERT_EQUAL_INT(0, c.cuenta(-1, TipoEvento::BCAST_SIN_ECO));
  TEST_ASSERT_EQUAL_UINT32(1, c.txs(0));               // UNA transmisión
  TEST_ASSERT_EQUAL_UINT32(0, c.nodo(1).contadores().acksTx);   // sin ACK
  TEST_ASSERT_EQUAL_INT(0, c.nodo(0).salientesVivas());
}

/* ==========================================================================
 *  7. Timeouts y ocupación
 * ========================================================================*/
void test_timeout_de_ack_en_e1() {
  /* §7.2 E1: T_aire(trama) + giro + T_aire(ACK) + margen. */
  Canal c;
  montarPareja(c);
  const ParamsRadio r = paramsDefecto();
  const uint16_t lenTrama = SOBRECARGA + 32;
  const uint32_t esperado = airtimeMs(r, lenTrama) + GIRO_MS + airtimeMs(r, SOBRECARGA + LEN_ACK) + MARGEN_MS;
  TEST_ASSERT_EQUAL_UINT32(esperado, c.nodo(0).timeoutAck(lenTrama, SOBRECARGA + LEN_ACK, 0, c.id(1)));
  /* Con NORMAL y una DATA de 32 caracteres: ≈ 630 ms (§7.2). */
  TEST_ASSERT_UINT32_WITHIN(30, 630, esperado);
}

void test_timeout_de_ack_crece_con_el_ttl() {
  Canal c;
  montarPareja(c);
  const uint16_t lenTrama = SOBRECARGA + LEN_ALERTA_CON_POS;
  const uint32_t e1 = c.nodo(0).timeoutAck(lenTrama, SOBRECARGA + LEN_ACK, 0, DIR_PASARELA);
  const uint32_t e2 = c.nodo(0).timeoutAck(lenTrama, SOBRECARGA + LEN_ACK, 3, DIR_PASARELA);
  TEST_ASSERT_TRUE(e2 > e1 * 4);
  /* ALERT con ttl 3 en E2: del orden de 8.7 s (§7.2). */
  TEST_ASSERT_UINT32_WITHIN(1500, 8700, e2);
}

void test_el_presupuesto_de_ocupacion_frena_el_trafico_normal() {
  /* §7.6: <= 10 % en 60 s para lo que no es ALERT/ACK. Se llena con PINGs. */
  Canal c(2);
  montarPareja(c);
  c.perder(1, true);   // que no conteste, para no gastar el presupuesto de B
  for (int k = 0; k < Motor::N_SALIENTES; ++k) c.nodo(0).enviarPing(c.id(1), 100, c.ahora());
  c.avanzar(20000);
  const uint32_t pormil = c.nodo(0).ocupacionPorMil(c.ahora());
  TEST_ASSERT_TRUE(pormil <= 100);   // nunca pasa del 10.0 %
}

void test_la_alerta_esta_exenta_del_presupuesto() {
  /* Una ALERT tiene que salir aunque el minuto esté lleno (§7.6). */
  Canal c(2);
  montarPareja(c);
  ConfigNodo cb = c.nodo(1).config();
  cb.rol = Rol::PASARELA;
  c.nodo(1).configurar(cb);
  /* Se llena el presupuesto con tráfico normal. */
  for (int k = 0; k < Motor::N_SALIENTES; ++k) c.nodo(0).enviarPing(c.id(1), 196, c.ahora());
  c.avanzar(30000);
  c.reiniciarTxs();
  TEST_ASSERT_TRUE(c.nodo(0).enviarAlerta(ALERTA_2, false, 0, 0, false, c.ahora()) >= 0);
  /* Sale en el acto, sin esperar a que se vacíe la ventana de 60 s. */
  TEST_ASSERT_TRUE(c.esperar(1, TipoEvento::ALERTA, 3000));
  TEST_ASSERT_TRUE(c.esperar(0, TipoEvento::ACK, 3000));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_data_entregado_y_confirmado);
  RUN_TEST(test_cien_mensajes_seguidos_sin_duplicados);
  RUN_TEST(test_msgid_crece_y_no_se_reusa);
  RUN_TEST(test_ack_perdido_reintento_mismo_msgid_y_sin_segunda_entrega);
  RUN_TEST(test_perdida_simulada_en_rx_ejercita_los_reintentos);
  RUN_TEST(test_backoff_exponencial_sin_jitter);
  RUN_TEST(test_el_jitter_descorrelaciona_y_esta_acotado);
  RUN_TEST(test_fallo_tras_agotar_los_intentos);
  RUN_TEST(test_alerta_llega_y_lleva_el_token_y_la_posicion);
  RUN_TEST(test_alerta_sin_pasarela_insiste_y_pasa_al_ciclo_lento);
  RUN_TEST(test_alerta_se_rinde_a_los_diez_minutos_con_fallo);
  RUN_TEST(test_cancel_corta_la_insistencia_y_reusa_el_seq);
  RUN_TEST(test_alerta_cancel_llega_con_el_seq_de_la_que_anula);
  RUN_TEST(test_alertas_sucesivas_llevan_seq_distinto);
  RUN_TEST(test_alerta_rechaza_codigos_que_no_son_tokens);
  RUN_TEST(test_ping_pong_con_rtt_y_calidad_de_los_dos_sentidos);
  RUN_TEST(test_ping_perdido);
  RUN_TEST(test_ping_con_relleno_alarga_la_trama);
  RUN_TEST(test_ping_broadcast_descubre_vecinos);
  RUN_TEST(test_no_se_envia_a_uno_mismo_ni_a_ids_reservados);
  RUN_TEST(test_la_cola_de_salientes_se_llena_y_lo_dice);
  RUN_TEST(test_carga_maxima_ida_y_vuelta);
  RUN_TEST(test_broadcast_en_e1_no_espera_confirmacion);
  RUN_TEST(test_timeout_de_ack_en_e1);
  RUN_TEST(test_timeout_de_ack_crece_con_el_ttl);
  RUN_TEST(test_el_presupuesto_de_ocupacion_frena_el_trafico_normal);
  RUN_TEST(test_la_alerta_esta_exenta_del_presupuesto);
  return UNITY_END();
}
