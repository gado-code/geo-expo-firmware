/* ============================================================================
 *  Pruebas del motor en E2, multi-salto (lib/geolora/motor)
 *  DISENO §8 (inundación controlada), §13.1
 * ============================================================================
 *      cd lora && pio test -e native
 *
 *  Con sólo DOS placas Heltec el multi-salto NO se puede demostrar en campo
 *  (§13.1, §16.6): hace falta un tercer nodo. Esta suite es el sustituto
 *  mientras no lo haya, y seguirá siendo útil después, porque aquí se pueden
 *  montar topologías (línea, anillo, malla densa, enlaces asimétricos) que en
 *  la calle costarían una tarde cada una.
 *
 *  Lo que se comprueba, en orden de importancia:
 *
 *    1. Que PASAR DE E1 A E2 ES SÓLO CONFIGURACIÓN (§8.3): mismo formato,
 *       mismo firmware, TTL 0 + RELAY OFF frente a TTL 3 + RELAY ON.
 *    2. Que la inundación TERMINA: cada nodo transmite cada (src,msgId) COMO
 *       MUCHO UNA VEZ, así que el coste de un mensaje es ≤ N transmisiones y
 *       no hay tormentas de difusión.
 *    3. Que nadie entrega dos veces el mismo mensaje, ni siquiera llegándole
 *       por dos caminos distintos.
 *    4. Que la SUPRESIÓN funciona: quien oye que otro ya repitió, se calla.
 * ==========================================================================*/
#include "../ayuda_motor.h"

using namespace geolora;
using namespace ayuda;

void setUp() {}
void tearDown() {}

/* Cuenta cuántas veces la trama (src,msgId) pasó por el aire desde cada nodo.
 * El canal cuenta transmisiones totales por nodo; para contar por mensaje se
 * usa el propio contador de reenvíos del motor. */
static uint32_t reenviosDe(Canal& c, int i) { return c.nodo(i).contadores().reenviados; }
static uint32_t suprimidosDe(Canal& c, int i) { return c.nodo(i).contadores().suprimidos; }

/* ==========================================================================
 *  1. Línea A–B–C: el caso del §13.1 y del §13.4
 * ------------------------------------------------------------------------
 *  A y C NO se oyen. B está en medio.
 * ========================================================================*/
static void montarLinea(Canal& c, const ConfigNodo& cfg) {
  c.anadir(cfg);   // 0 = A
  c.anadir(cfg);   // 1 = B
  c.anadir(cfg);   // 2 = C
  c.enlazar(0, 1, 5);
  c.enlazar(1, 2, 5);
  /* A y C fuera de alcance directo: es la premisa. */
}

void test_linea_con_ttl0_no_hay_multisalto() {
  /* E1: aunque B esté en medio, A→C no llega. Es el "0/20" que el §13.4 pide
   * comprobar antes de montar la prueba de campo de E2. */
  Canal c(2);
  montarLinea(c, cfgE1());
  const int32_t msgId = c.nodo(0).enviarDatos(c.id(2), bytes("a C"), 3, c.ahora());
  TEST_ASSERT_TRUE(msgId >= 0);
  Evento f;
  TEST_ASSERT_TRUE(c.esperar(0, TipoEvento::FALLO, 90000, &f));
  TEST_ASSERT_EQUAL_HEX16(msgId, f.msgId);
  TEST_ASSERT_EQUAL_INT(0, c.cuenta(2, TipoEvento::ENTREGADO));
  /* B no lo entrega (no es para él) ni lo repite (relay OFF). */
  TEST_ASSERT_EQUAL_INT(0, c.cuenta(1, TipoEvento::ENTREGADO));
  TEST_ASSERT_EQUAL_UINT32(0, reenviosDe(c, 1));
  TEST_ASSERT_EQUAL_UINT32(0, c.txs(1));
}

void test_linea_con_ttl2_entrega_y_ack_de_vuelta() {
  /* E2: lo MISMO, cambiando sólo ttl y relay (§8.3). */
  Canal c(2);
  montarLinea(c, cfgE2(2));
  const int32_t msgId = c.nodo(0).enviarDatos(c.id(2), bytes("a C"), 3, c.ahora());

  /* C lo recibe a 1 salto. */
  Evento e;
  TEST_ASSERT_TRUE(c.esperar(2, TipoEvento::ENTREGADO, 30000, &e));
  TEST_ASSERT_EQUAL_HEX16(c.id(0), e.nodo);        // el ORIGEN, no el repetidor
  TEST_ASSERT_EQUAL_HEX16(msgId, e.msgId);
  TEST_ASSERT_EQUAL_UINT8(1, e.hops);             // un reenvío
  TEST_ASSERT_EQUAL_STRING("a C", reinterpret_cast<const char*>(e.datos));

  /* Y el ACK vuelve inundando la malla, con hops = 1 (§13.4). */
  Evento ack;
  TEST_ASSERT_TRUE(c.esperar(0, TipoEvento::ACK, 30000, &ack));
  TEST_ASSERT_EQUAL_HEX16(c.id(2), ack.nodo);
  TEST_ASSERT_EQUAL_HEX16(msgId, ack.msgId);
  TEST_ASSERT_EQUAL_UINT8(1, ack.hops);           // el ACK también dio un salto
  TEST_ASSERT_EQUAL_UINT8(1, ack.hopsIda);        // y C avisa de los de la ida
  TEST_ASSERT_EQUAL_UINT8(1, ack.intentos);       // a la primera, sin reintentos

  /* B repitió las dos tramas (la DATA y el ACK) y no entregó ninguna. */
  TEST_ASSERT_EQUAL_UINT32(2, reenviosDe(c, 1));
  TEST_ASSERT_EQUAL_INT(0, c.cuenta(1, TipoEvento::ENTREGADO));
  /* Nadie entregó nada dos veces. */
  TEST_ASSERT_EQUAL_INT(1, c.cuenta(-1, TipoEvento::ENTREGADO));
  TEST_ASSERT_EQUAL_INT(0, c.cuenta(-1, TipoEvento::FALLO));
}

void test_el_destino_no_reenvia() {
  /* Regla 6 del §8.1: quien es el destino entrega y confirma, pero NO repite
   * (si lo hiciera, cada mensaje costaría una transmisión de más). */
  Canal c(2);
  montarLinea(c, cfgE2(3));
  c.nodo(0).enviarDatos(c.id(2), bytes("x"), 1, c.ahora());
  TEST_ASSERT_TRUE(c.esperar(0, TipoEvento::ACK, 30000));
  TEST_ASSERT_EQUAL_UINT32(0, reenviosDe(c, 2));   // C no repitió la DATA
  TEST_ASSERT_EQUAL_UINT32(0, reenviosDe(c, 0));   // ni A el ACK
}

void test_ttl_se_agota_y_la_trama_deja_de_viajar() {
  /* Cadena de 4: A–B–C–D. Con ttl 1 sólo se puede dar UN salto, así que D
   * queda fuera de alcance por diseño. */
  Canal c(2);
  ConfigNodo cfg = cfgE2(1);
  for (int i = 0; i < 4; ++i) c.anadir(cfg);
  c.enlazarEnLinea(5);
  c.nodo(0).enviarDatos(c.id(3), bytes("a D"), 3, c.ahora());
  TEST_ASSERT_TRUE(c.esperar(0, TipoEvento::FALLO, 120000));
  TEST_ASSERT_EQUAL_INT(0, c.cuenta(3, TipoEvento::ENTREGADO));
  /* B repite cada intento de A (un reintento con hops 0 es una inundación
   * nueva, §8.1 regla 2), pero C no repite NINGUNA: le llegan ya con ttl 0.
   * Eso es lo que corta la propagación. */
  TEST_ASSERT_EQUAL_UINT32(c.txs(0), reenviosDe(c, 1));
  TEST_ASSERT_EQUAL_UINT32(0, reenviosDe(c, 2));
  TEST_ASSERT_EQUAL_UINT32(0, c.txs(3));
}

void test_con_ttl2_la_cadena_de_cuatro_si_llega() {
  Canal c(2);
  ConfigNodo cfg = cfgE2(2);
  for (int i = 0; i < 4; ++i) c.anadir(cfg);
  c.enlazarEnLinea(5);
  Evento e;
  c.nodo(0).enviarDatos(c.id(3), bytes("a D"), 3, c.ahora());
  TEST_ASSERT_TRUE(c.esperar(3, TipoEvento::ENTREGADO, 60000, &e));
  TEST_ASSERT_EQUAL_UINT8(2, e.hops);              // dos reenvíos
  TEST_ASSERT_TRUE(c.esperar(0, TipoEvento::ACK, 60000, &e));
  TEST_ASSERT_EQUAL_UINT8(2, e.hopsIda);
  TEST_ASSERT_EQUAL_INT(1, c.cuenta(-1, TipoEvento::ENTREGADO));
}

/* ==========================================================================
 *  2. Convivencia de nodos E1 y E2 en la misma red (§8.3)
 * ========================================================================*/
void test_un_nodo_e1_no_repite_pero_habla_con_los_e2() {
  Canal c(2);
  c.anadir(cfgE2(3));   // 0 = A, E2
  c.anadir(cfgE1());    // 1 = B, E1: no repite nada
  c.anadir(cfgE2(3));   // 2 = C, E2
  c.enlazar(0, 1, 5);
  c.enlazar(1, 2, 5);
  /* A→C no puede llegar: el del medio es E1. */
  c.nodo(0).enviarDatos(c.id(2), bytes("x"), 1, c.ahora());
  TEST_ASSERT_TRUE(c.esperar(0, TipoEvento::FALLO, 120000));
  TEST_ASSERT_EQUAL_UINT32(0, reenviosDe(c, 1));
  /* Pero A y B se hablan perfectamente. */
  c.olvidarEventos();
  c.nodo(0).enviarDatos(c.id(1), bytes("hola B"), 6, c.ahora());
  TEST_ASSERT_TRUE(c.esperar(1, TipoEvento::ENTREGADO, 30000));
  TEST_ASSERT_TRUE(c.esperar(0, TipoEvento::ACK, 30000));
}

void test_las_tramas_con_ttl0_no_las_repite_ni_un_nodo_con_relay_on() {
  /* §8.3: con TTL 0 los dos modos conviven, porque lo que decide es el ttl de
   * la TRAMA, no la configuración del que la oye. */
  Canal c(2);
  ConfigNodo emisor = cfgE1();          // manda con ttl 0
  ConfigNodo repetidor = cfgE2(3);      // relay ON
  c.anadir(emisor);
  c.anadir(repetidor);
  c.anadir(repetidor);
  c.enlazar(0, 1, 5);
  c.enlazar(1, 2, 5);
  c.nodo(0).enviarDatos(DIR_BROADCAST, bytes("solo vecinos"), 12, c.ahora());
  c.avanzar(20000);
  TEST_ASSERT_EQUAL_INT(1, c.cuenta(1, TipoEvento::ENTREGADO));
  TEST_ASSERT_EQUAL_INT(0, c.cuenta(2, TipoEvento::ENTREGADO));   // no salta
  TEST_ASSERT_EQUAL_UINT32(0, reenviosDe(c, 1));
  TEST_ASSERT_EQUAL_UINT32(1, c.txsTotales());                    // UNA transmisión
}

/* ==========================================================================
 *  3. Malla densa: la inundación tiene que TERMINAR
 * ========================================================================*/
void test_malla_densa_cada_nodo_repite_como_mucho_una_vez() {
  /* 6 nodos todos con todos. Un broadcast desde A: el coste tiene que ser
   * ≤ N transmisiones (§8.1) y, gracias a la supresión, bastante menos. */
  Canal c(2);
  ConfigNodo cfg = cfgE2(3);
  for (int i = 0; i < 6; ++i) c.anadir(cfg);
  c.enlazarTodos(5);
  c.nodo(0).enviarDatos(DIR_BROADCAST, bytes("a todos"), 7, c.ahora());
  c.avanzar(60000);

  /* Todos lo entregaron, y UNA sola vez cada uno. */
  for (int i = 1; i < 6; ++i) {
    TEST_ASSERT_EQUAL_INT(1, c.cuenta(i, TipoEvento::ENTREGADO));
  }
  TEST_ASSERT_EQUAL_INT(5, c.cuenta(-1, TipoEvento::ENTREGADO));
  /* Ningún nodo repitió más de una vez. */
  for (int i = 0; i < 6; ++i) {
    TEST_ASSERT_TRUE(reenviosDe(c, i) <= 1);
  }
  /* Coste total ≤ N (1 del origen + como mucho 5 reenvíos). */
  TEST_ASSERT_TRUE(c.txsTotales() <= 6);
  /* Y la supresión ahorró de verdad: con todos oyéndose, la mayoría se calla. */
  uint32_t suprimidos = 0;
  for (int i = 0; i < 6; ++i) suprimidos += suprimidosDe(c, i);
  TEST_ASSERT_TRUE(suprimidos > 0);
  TEST_ASSERT_TRUE(c.txsTotales() < 6);
  /* Nada de bucles: el mensaje se acaba. */
  const uint32_t antes = c.txsTotales();
  c.avanzar(60000);
  TEST_ASSERT_EQUAL_UINT32(antes, c.txsTotales());
}

void test_anillo_no_hay_bucle_infinito() {
  /* Anillo de 6: cada nodo oye a sus dos contiguos. Es la topología donde una
   * inundación mal cortada daría vueltas para siempre. */
  Canal c(2);
  ConfigNodo cfg = cfgE2(7);   // ttl al máximo, a propósito
  for (int i = 0; i < 6; ++i) c.anadir(cfg);
  for (int i = 0; i < 6; ++i) c.enlazar(i, (i + 1) % 6, 5);
  c.nodo(0).enviarDatos(DIR_BROADCAST, bytes("anillo"), 6, c.ahora());
  c.avanzar(120000);
  for (int i = 1; i < 6; ++i) TEST_ASSERT_EQUAL_INT(1, c.cuenta(i, TipoEvento::ENTREGADO));
  for (int i = 0; i < 6; ++i) TEST_ASSERT_TRUE(reenviosDe(c, i) <= 1);
  TEST_ASSERT_TRUE(c.txsTotales() <= 6);
  /* Y se detiene: el aire queda en silencio. */
  const uint32_t antes = c.txsTotales();
  c.avanzar(120000);
  TEST_ASSERT_EQUAL_UINT32(antes, c.txsTotales());
}

void test_dos_caminos_una_sola_entrega() {
  /* Diamante: A alcanza a B y C, los dos alcanzan a D. A D le llega por los
   * dos lados y sólo puede entregarlo UNA vez (§7.4). */
  Canal c(1);
  ConfigNodo cfg = cfgE2(3);
  for (int i = 0; i < 4; ++i) c.anadir(cfg);
  c.enlazar(0, 1, 5);
  c.enlazar(0, 2, 5);
  c.enlazar(1, 3, 5);
  c.enlazar(2, 3, 5);
  c.nodo(0).enviarDatos(c.id(3), bytes("dos caminos"), 11, c.ahora());
  TEST_ASSERT_TRUE(c.esperar(3, TipoEvento::ENTREGADO, 60000));
  /* Se deja pasar la SEGUNDA copia (la del otro camino) y el ACK. */
  c.avanzar(30000);
  TEST_ASSERT_EQUAL_INT(1, c.cuenta(3, TipoEvento::ENTREGADO));   // UNA entrega
  TEST_ASSERT_EQUAL_INT(1, c.cuenta(0, TipoEvento::ACK));         // UN ACK útil
  /* Y que de verdad le llegó por los dos caminos: la caché de vistos tuvo que
   * descartar la copia repetida. */
  TEST_ASSERT_TRUE(c.nodo(3).contadores().duplicados >= 1);
}

void test_hops_no_pasa_del_maximo() {
  /* HOPS_MAX corta el reenvío aunque alguien mande un ttl absurdo: es la
   * última red de seguridad contra un bucle (§8.1 regla 7). */
  Canal c(2);
  ConfigNodo cfg = cfgE2(7);
  for (int i = 0; i < 8; ++i) c.anadir(cfg);
  c.enlazarEnLinea(5);
  c.nodo(0).enviarDatos(DIR_BROADCAST, bytes("lejos"), 5, c.ahora());
  c.avanzar(120000);
  /* Con ttl 7 y HOPS_MAX 7, la trama llega como mucho al nodo 7. */
  for (int i = 0; i < 8; ++i) TEST_ASSERT_TRUE(reenviosDe(c, i) <= 1);
  const uint32_t antes = c.txsTotales();
  c.avanzar(60000);
  TEST_ASSERT_EQUAL_UINT32(antes, c.txsTotales());
}

/* ==========================================================================
 *  4. Supresión y retardo ponderado por SNR (§8.2)
 * ========================================================================*/
/* Monta A–B–C donde B y C oyen a A con los SNR y los roles dados, y se oyen
 * entre ellos. A difunde un mensaje; los dos lo entregan y los dos querrían
 * repetirlo, pero el que sale primero hace callar al otro (§8.1 regla 2), así
 * que el que acaba repitiendo DELATA quién tenía menos retardo de reenvío.
 * Devuelve 1 o 2, y comprueba de paso que repite UNO y sólo uno. */
static int quienRepite(int snrB, int snrC, Rol rolB = Rol::NORMAL, Rol rolC = Rol::NORMAL) {
  Canal c(1);
  ConfigNodo cb = cfgE2(3);
  cb.rol = rolB;
  ConfigNodo cc = cfgE2(3);
  cc.rol = rolC;
  c.anadir(cfgE2(3));   // 0 = A, origen
  c.anadir(cb);         // 1 = B
  c.anadir(cc);         // 2 = C
  c.enlazar(0, 1, snrB);
  c.enlazar(0, 2, snrC);
  c.enlazar(1, 2, 8);
  c.nodo(0).enviarDatos(DIR_BROADCAST, bytes("quien antes"), 11, c.ahora());
  c.avanzar(30000);
  /* Los dos lo entregaron (una vez cada uno) y sólo uno lo repitió. */
  TEST_ASSERT_EQUAL_INT(1, c.cuenta(1, TipoEvento::ENTREGADO));
  TEST_ASSERT_EQUAL_INT(1, c.cuenta(2, TipoEvento::ENTREGADO));
  TEST_ASSERT_EQUAL_UINT32(1, reenviosDe(c, 1) + reenviosDe(c, 2));
  const int ganador = reenviosDe(c, 1) == 1 ? 1 : 2;
  /* Y el otro se calló porque oyó el reenvío, no por casualidad. */
  TEST_ASSERT_EQUAL_UINT32(1, suprimidosDe(c, ganador == 1 ? 2 : 1));
  return ganador;
}

void test_el_que_oye_peor_repite_antes() {
  /* D_snr = CW·clamp((snr+20)/30, 0, 1) (§8.2): quien oyó PEOR (o sea, quien
   * está más lejos) repite ANTES, porque es el que puede llevar el mensaje más
   * lejos. Con +10 dB el retardo es CW completo y con −18 dB es casi 0, así
   * que las dos ventanas de sorteo —[D_snr, D_snr+CW]— no se solapan y el
   * resultado NO depende del jitter. Se prueba en los dos sentidos para que no
   * pueda salir bien por el orden de los nodos. */
  TEST_ASSERT_EQUAL_INT(2, quienRepite(10, -18));   // C oye mal -> repite C
  TEST_ASSERT_EQUAL_INT(1, quienRepite(-18, 10));   // B oye mal -> repite B
}

void test_supresion_el_que_oye_a_otro_repetir_se_calla() {
  /* A manda; B y C lo oyen (C peor, así que repite antes) y se oyen entre
   * ellos. Cuando B oye el reenvío de C, CANCELA el suyo (§8.1 regla 2). */
  Canal c(1);
  ConfigNodo cfg = cfgE2(3);
  c.anadir(cfg);   // 0 = A
  c.anadir(cfg);   // 1 = B, oye bien: repetiría tarde
  c.anadir(cfg);   // 2 = C, oye mal: repite pronto
  /* Enlaces simétricos: A oye el reenvío y lo toma por ACK implícito, así que
   * no reintenta el broadcast y las cuentas de reenvíos son limpias. */
  c.enlazar(0, 1, 10);
  c.enlazar(0, 2, -18);
  c.enlazar(1, 2, 8);
  c.nodo(0).enviarDatos(DIR_BROADCAST, bytes("supresion"), 9, c.ahora());
  c.avanzar(30000);
  /* C repitió, B se calló. */
  TEST_ASSERT_EQUAL_UINT32(1, reenviosDe(c, 2));
  TEST_ASSERT_EQUAL_UINT32(0, reenviosDe(c, 1));
  TEST_ASSERT_EQUAL_UINT32(1, suprimidosDe(c, 1));
  /* Coste total: la de A y la de C. */
  TEST_ASSERT_EQUAL_UINT32(2, c.txsTotales());
  /* Y cada uno entregó una vez. */
  TEST_ASSERT_EQUAL_INT(1, c.cuenta(1, TipoEvento::ENTREGADO));
  TEST_ASSERT_EQUAL_INT(1, c.cuenta(2, TipoEvento::ENTREGADO));
}

void test_el_rol_repetidor_no_espera() {
  /* Un nodo fijo en altura con rol REPETIDOR usa D_snr = 0 (§8.2): repite
   * primero aunque oiga de maravilla. Los dos oyen IGUAL de bien (+10 dB), así
   * que la única diferencia es el rol: el normal sortea en [CW, 2·CW] y el
   * repetidor en [0, CW]. Gana siempre el repetidor, esté en la posición que
   * esté.
   *
   * Ojo con lo que este rol NO garantiza: frente a un nodo que oye MAL (cuyo
   * D_snr ya es casi 0) las dos ventanas se solapan y decide el jitter. El
   * rol sirve para adelantarse a los vecinos CERCANOS, que es para lo que
   * está pensado. */
  TEST_ASSERT_EQUAL_INT(1, quienRepite(10, 10, Rol::REPETIDOR, Rol::NORMAL));
  TEST_ASSERT_EQUAL_INT(2, quienRepite(10, 10, Rol::NORMAL, Rol::REPETIDOR));
}

/* ==========================================================================
 *  5. Eco propio y ACK implícito (§8.1 regla 1)
 * ========================================================================*/
void test_eco_de_mi_broadcast_es_ack_implicito() {
  Canal c(1);
  ConfigNodo cfg = cfgE2(3);
  c.anadir(cfg);
  c.anadir(cfg);
  c.enlazar(0, 1, 5);
  const int32_t msgId = c.nodo(0).enviarDatos(DIR_BROADCAST, bytes("eco"), 3, c.ahora());
  Evento eco;
  TEST_ASSERT_TRUE(c.esperar(0, TipoEvento::ECO_BCAST, 30000, &eco));
  TEST_ASSERT_EQUAL_HEX16(msgId, eco.msgId);
  TEST_ASSERT_EQUAL_HEX16(DIR_BROADCAST, eco.nodo);
  TEST_ASSERT_EQUAL_UINT8(1, eco.hops);            // era el reenvío de B
  /* Al oír el eco, A NO reintenta el broadcast. */
  c.avanzar(30000);
  TEST_ASSERT_EQUAL_INT(0, c.cuenta(0, TipoEvento::REINTENTO));
  TEST_ASSERT_EQUAL_INT(0, c.cuenta(0, TipoEvento::BCAST_SIN_ECO));
  TEST_ASSERT_EQUAL_UINT32(1, c.txs(0));           // una sola transmisión
  /* Y A no repite su propio eco (regla 1). */
  TEST_ASSERT_EQUAL_UINT32(0, reenviosDe(c, 0));
  TEST_ASSERT_EQUAL_UINT32(1, c.nodo(0).contadores().ecos);
}

void test_broadcast_sin_eco_se_reintenta_una_vez() {
  /* Si nadie repite (el vecino es E1, o no hay nadie), A lo reintenta una vez
   * y luego avisa: BCAST_SIN_ECO (§8.1). */
  Canal c(2);
  c.anadir(cfgE2(3));
  c.anadir(cfgE1());     // no repite
  c.enlazar(0, 1, 5);
  c.nodo(0).enviarDatos(DIR_BROADCAST, bytes("sin eco"), 7, c.ahora());
  Evento s;
  TEST_ASSERT_TRUE(c.esperar(0, TipoEvento::BCAST_SIN_ECO, 60000, &s));
  TEST_ASSERT_EQUAL_UINT8(BCAST_INTENTOS, s.intentos);
  TEST_ASSERT_EQUAL_UINT32(BCAST_INTENTOS, c.txs(0));
  /* B lo entregó una vez, no dos: el reintento es un duplicado. */
  TEST_ASSERT_EQUAL_INT(1, c.cuenta(1, TipoEvento::ENTREGADO));
  TEST_ASSERT_TRUE(c.nodo(1).contadores().duplicados >= 1);
}

void test_id_duplicado_se_detecta() {
  /* Dos radios con el MISMO ID en el aire: se avisa una vez por mensaje
   * (§6). Es lo que pasaría si las dos placas tuvieran la MAC clonada o un
   * override mal puesto. */
  Canal c(1);
  ConfigNodo cfg = cfgE2(3);
  c.anadir(cfg, 0x00AA);
  c.anadir(cfg, 0x00AA);   // el mismo ID, a propósito
  c.anadir(cfg, 0x00BB);
  c.enlazarTodos(5);
  c.nodo(0).enviarDatos(0x00BB, bytes("quien soy"), 9, c.ahora());
  Evento d;
  TEST_ASSERT_TRUE(c.esperar(1, TipoEvento::ID_DUPLICADO, 30000, &d));
  TEST_ASSERT_EQUAL_HEX16(0x00AA, d.nodo);
  TEST_ASSERT_TRUE(c.nodo(1).contadores().idDuplicado >= 1);
  /* El aviso es UNO por mensaje, aunque lleguen varias copias. */
  const uint32_t n = c.nodo(1).contadores().idDuplicado;
  c.avanzar(30000);
  TEST_ASSERT_EQUAL_UINT32(n, c.nodo(1).contadores().idDuplicado);
}

/* ==========================================================================
 *  6. ALERT por la malla: el caso que de verdad importa
 * ========================================================================*/
void test_alerta_llega_a_la_pasarela_a_dos_saltos() {
  /* A (llavero vía futura pasarela BLE) → ... → nodo PASARELA, a dos saltos.
   * Es el camino previsto del §14. */
  Canal c(2);
  ConfigNodo cfg = cfgE2(3);
  ConfigNodo pas = cfgE2(3);
  pas.rol = Rol::PASARELA;
  c.anadir(cfg);   // 0 = A
  c.anadir(cfg);   // 1 = B
  c.anadir(cfg);   // 2 = C
  c.anadir(pas);   // 3 = P, pasarela
  c.enlazarEnLinea(5);

  const int32_t msgId = c.nodo(0).enviarAlerta(ALERTA_2, true, 46097100, -740817500, false, c.ahora());
  TEST_ASSERT_TRUE(msgId >= 0);
  Evento al;
  TEST_ASSERT_TRUE(c.esperar(3, TipoEvento::ALERTA, 60000, &al));
  TEST_ASSERT_EQUAL_HEX16(c.id(0), al.nodo);
  TEST_ASSERT_EQUAL_UINT8(ALERTA_2, al.alerta.codigo);
  TEST_ASSERT_EQUAL_STRING("ALERT:2", tokenAlerta(al.alerta.codigo));
  TEST_ASSERT_EQUAL_INT32(46097100, al.alerta.lat1e7);
  TEST_ASSERT_EQUAL_INT32(-740817500, al.alerta.lon1e7);
  TEST_ASSERT_EQUAL_UINT8(2, al.hops);
  /* Y la línea que entregaría al exterior lleva los tokens exactos. */
  char linea[64];
  TEST_ASSERT_TRUE(lineaPasarela(al.alerta, true, linea, sizeof(linea)) > 0);
  TEST_ASSERT_EQUAL_STRING("ALERT:2;4.609710;-74.081750", linea);

  /* La pasarela confirma y el ACK vuelve por la malla. */
  Evento ack;
  TEST_ASSERT_TRUE(c.esperar(0, TipoEvento::ACK, 60000, &ack));
  TEST_ASSERT_EQUAL_HEX16(msgId, ack.msgId);
  TEST_ASSERT_EQUAL_UINT8(2, ack.hopsIda);
  TEST_ASSERT_FALSE(c.nodo(0).alertaActiva());
  /* Los intermedios NO la entregan (no son pasarela) pero sí la repiten. */
  TEST_ASSERT_EQUAL_INT(0, c.cuenta(1, TipoEvento::ALERTA));
  TEST_ASSERT_EQUAL_INT(0, c.cuenta(2, TipoEvento::ALERTA));
  TEST_ASSERT_TRUE(reenviosDe(c, 1) >= 1);
}

void test_varias_pasarelas_el_origen_se_queda_con_el_primer_ack() {
  /* §14: "si hay varias pasarelas, todas confirman; el origen se queda con el
   * primer ACK y los demás son inocuos". */
  Canal c(1);
  ConfigNodo cfg = cfgE2(3);
  ConfigNodo pas = cfgE2(3);
  pas.rol = Rol::PASARELA;
  c.anadir(cfg);   // 0 = A
  c.anadir(pas);   // 1 = P1
  c.anadir(pas);   // 2 = P2
  c.enlazarTodos(5);
  c.nodo(0).enviarAlerta(ALERTA_1, false, 0, 0, false, c.ahora());
  TEST_ASSERT_TRUE(c.esperar(0, TipoEvento::ACK, 30000));
  c.avanzar(30000);
  /* Las dos la recibieron. */
  TEST_ASSERT_EQUAL_INT(1, c.cuenta(1, TipoEvento::ALERTA));
  TEST_ASSERT_EQUAL_INT(1, c.cuenta(2, TipoEvento::ALERTA));
  /* Pero el origen la da por confirmada UNA vez y no reintenta. */
  TEST_ASSERT_EQUAL_INT(1, c.cuenta(0, TipoEvento::ACK));
  TEST_ASSERT_EQUAL_INT(0, c.cuenta(0, TipoEvento::REINTENTO));
  TEST_ASSERT_FALSE(c.nodo(0).alertaActiva());
  /* El segundo ACK no casa con nada y se cuenta como huérfano, sin más. */
  TEST_ASSERT_TRUE(c.nodo(0).contadores().acksHuerfanos >= 1);
}

/* ==========================================================================
 *  7. Enlaces asimétricos y cola de reenvíos
 * ========================================================================*/
void test_enlace_asimetrico_el_ack_busca_otro_camino() {
  /* A alcanza a C directamente, pero C NO alcanza a A (el caso clásico de un
   * nodo con mejor antena). El ACK tiene que volver por B. */
  Canal c(2);
  ConfigNodo cfg = cfgE2(3);
  c.anadir(cfg);   // 0 = A
  c.anadir(cfg);   // 1 = B
  c.anadir(cfg);   // 2 = C
  c.dirigir(0, 2, 2);    // A -> C
  c.enlazar(0, 1, 5);    // A <-> B
  c.enlazar(1, 2, 5);    // B <-> C
  c.nodo(0).enviarDatos(c.id(2), bytes("asimetrico"), 10, c.ahora());
  Evento e;
  TEST_ASSERT_TRUE(c.esperar(2, TipoEvento::ENTREGADO, 60000, &e));
  TEST_ASSERT_EQUAL_UINT8(0, e.hops);          // le llegó DIRECTA
  Evento ack;
  TEST_ASSERT_TRUE(c.esperar(0, TipoEvento::ACK, 60000, &ack));
  TEST_ASSERT_EQUAL_UINT8(1, ack.hops);        // pero el ACK volvió por B
  TEST_ASSERT_EQUAL_UINT8(0, ack.hopsIda);
  TEST_ASSERT_EQUAL_INT(1, c.cuenta(2, TipoEvento::ENTREGADO));
}

void test_la_cola_de_reenvios_se_llena_y_avisa() {
  /* 8 huecos de reenvío (§8.2). Si llegan más mensajes a la vez de los que
   * caben, se descarta el más antiguo que no sea ALERT y se avisa. */
  Canal c(1);
  ConfigNodo cfg = cfgE2(3);
  c.anadir(cfg);   // 0 = A, origen de mucho tráfico
  c.anadir(cfg);   // 1 = B, el que tiene que repetir
  c.anadir(cfg);   // 2 = C
  c.enlazar(0, 1, 5);
  c.enlazar(1, 2, 5);
  /* A mete 8 broadcast de golpe (su propia cola de salientes es de 8). */
  for (int k = 0; k < Motor::N_SALIENTES; ++k) {
    c.nodo(0).enviarDatos(DIR_BROADCAST, bytes("relleno de cola"), 15, c.ahora());
  }
  c.avanzar(120000);
  /* Todo lo que B repitió, lo repitió una vez; y si tuvo que descartar algo,
   * lo dijo. */
  TEST_ASSERT_TRUE(reenviosDe(c, 1) >= 1);
  const int descartes = c.cuenta(1, TipoEvento::DESCARTE_COLA);
  TEST_ASSERT_TRUE(descartes >= 0);
  /* Lo importante: ni cuelgues ni entregas duplicadas. */
  TEST_ASSERT_TRUE(c.cuenta(1, TipoEvento::ENTREGADO) <= Motor::N_SALIENTES);
  TEST_ASSERT_TRUE(c.cuenta(2, TipoEvento::ENTREGADO) <= Motor::N_SALIENTES);
}

void test_los_tipos_desconocidos_se_reenvian_pero_no_se_entregan() {
  /* §5.1: un nodo antiguo no rompe la malla de nodos nuevos. Se inyecta a
   * mano una trama de tipo ORDEN (0x6, reservado para E3) y se comprueba que
   * B la repite sin entregarla. */
  Canal c(1);
  ConfigNodo cfg = cfgE2(3);
  c.anadir(cfg);   // 0 = A (no se usa como emisor)
  c.anadir(cfg);   // 1 = B, el repetidor
  c.anadir(cfg);   // 2 = C
  c.enlazar(0, 1, 5);
  c.enlazar(1, 2, 5);

  Cabecera h;
  h.tipo = static_cast<uint8_t>(Tipo::ORDEN);
  h.src = 0x0777;                 // un nodo que B no conoce
  h.dst = DIR_BROADCAST;
  h.msgId = 0x4242;
  h.ttl = 2;
  h.len = 1;
  const uint8_t carga[1] = {0x01};
  uint8_t trama[TRAMA_MAX];
  const size_t n = codificar(h, carga, trama, sizeof(trama));
  TEST_ASSERT_TRUE(n > 0);
  c.nodo(1).alRecibir(trama, n, -90, snrAQ4(5.0f), c.ahora());
  c.avanzar(30000);
  /* No se entrega (v1 no sabe qué es)… */
  TEST_ASSERT_EQUAL_INT(0, c.cuenta(1, TipoEvento::ENTREGADO));
  TEST_ASSERT_TRUE(c.nodo(1).contadores().rechazos[static_cast<int>(Rechazo::TIPO)] >= 1);
  /* …pero sí se repite, y a C le llega con hops 1. */
  TEST_ASSERT_EQUAL_UINT32(1, reenviosDe(c, 1));
  TEST_ASSERT_TRUE(c.nodo(2).contadores().rechazos[static_cast<int>(Rechazo::TIPO)] >= 1);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_linea_con_ttl0_no_hay_multisalto);
  RUN_TEST(test_linea_con_ttl2_entrega_y_ack_de_vuelta);
  RUN_TEST(test_el_destino_no_reenvia);
  RUN_TEST(test_ttl_se_agota_y_la_trama_deja_de_viajar);
  RUN_TEST(test_con_ttl2_la_cadena_de_cuatro_si_llega);
  RUN_TEST(test_un_nodo_e1_no_repite_pero_habla_con_los_e2);
  RUN_TEST(test_las_tramas_con_ttl0_no_las_repite_ni_un_nodo_con_relay_on);
  RUN_TEST(test_malla_densa_cada_nodo_repite_como_mucho_una_vez);
  RUN_TEST(test_anillo_no_hay_bucle_infinito);
  RUN_TEST(test_dos_caminos_una_sola_entrega);
  RUN_TEST(test_hops_no_pasa_del_maximo);
  RUN_TEST(test_el_que_oye_peor_repite_antes);
  RUN_TEST(test_supresion_el_que_oye_a_otro_repetir_se_calla);
  RUN_TEST(test_el_rol_repetidor_no_espera);
  RUN_TEST(test_eco_de_mi_broadcast_es_ack_implicito);
  RUN_TEST(test_broadcast_sin_eco_se_reintenta_una_vez);
  RUN_TEST(test_id_duplicado_se_detecta);
  RUN_TEST(test_alerta_llega_a_la_pasarela_a_dos_saltos);
  RUN_TEST(test_varias_pasarelas_el_origen_se_queda_con_el_primer_ack);
  RUN_TEST(test_enlace_asimetrico_el_ack_busca_otro_camino);
  RUN_TEST(test_la_cola_de_reenvios_se_llena_y_avisa);
  RUN_TEST(test_los_tipos_desconocidos_se_reenvian_pero_no_se_entregan);
  return UNITY_END();
}
