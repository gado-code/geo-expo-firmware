/* ============================================================================
 *  GEO-EXPO LoRa  ·  Canal de radio SIMULADO para las pruebas del motor
 *  (lo usan test_motor_p2p y test_motor_malla)  —  DISENO §12.2, §13.1
 * ============================================================================
 *  El Motor no toca la radio ni el reloj: el tiempo entra por parámetro y el
 *  azar por una función inyectada (§12.2). Eso permite montar aquí, en el PC,
 *  N nodos sobre un canal de mentira y probar TODA la lógica de E1 y E2 sin
 *  placas, incluido el multi-salto, que con sólo 2 Heltec no se puede
 *  demostrar en campo (§13.1, §16.6).
 *
 *  ── Lo que simula, y por qué así ─────────────────────────────────────────
 *  El bucle de cada nodo es el MISMO que el loop() de src/main.cpp (§12.4):
 *
 *      1. tramas que acaban de llegar   -> alRecibir()
 *      2. tick()
 *      3. siguienteTx() -> CAD -> TX    -> txHecha() / canalOcupado()
 *
 *  · Una transmisión OCUPA el aire desde que empieza hasta `airtime` después.
 *    `txHecha()` se llama al TERMINAR (con el reloj ya avanzado), igual que en
 *    la placa, donde transmit() es bloqueante.
 *  · SEMIDÚPLEX: un nodo que está transmitiendo NO oye nada. Es la limitación
 *    física real del SX1262 y la razón del retardo de giro de 30 ms (§7.1).
 *  · CAD: `siguienteTx()` devuelve `usarCad`; si hay algo en el aire que llega
 *    hasta mí, se llama a `canalOcupado()`, como haría scanChannel() (§7.5).
 *  · La matriz de ALCANCE es dirigida (alcanza[a][b] no implica alcanza[b][a]):
 *    los enlaces asimétricos existen y son justo lo que rompe las mallas.
 *  · Cada enlace lleva su SNR, que es lo que decide el retardo de reenvío
 *    ponderado (§8.2) y por tanto QUIÉN repite primero.
 *
 *  ── Lo que NO simula ─────────────────────────────────────────────────────
 *  Colisiones entre dos tramas que se solapan en el aire (salvo el caso
 *  semidúplex). Es deliberado: estas pruebas comprueban la LÓGICA (quién
 *  reenvía, quién se calla, quién confirma), y una colisión aleatoria la
 *  volvería no determinista. Las pérdidas se provocan a propósito con
 *  perder() o con Motor::setPerdida(), que sí son deterministas.
 * ==========================================================================*/
#pragma once

#include <string.h>

#include <unity.h>

#include "carga.h"
#include "motor.h"
#include "paquete.h"
#include "radio_params.h"

namespace ayuda {

using namespace geolora;

/* --------------------------------------------------------------------------
 *  Azar determinista (xorshift32). El motor lo usa para el jitter de los
 *  reintentos, el escalonado de los PONG y el retardo de reenvío; con semilla
 *  fija los tiempos son REPETIBLES y se pueden comprobar al milisegundo.
 * ------------------------------------------------------------------------*/
struct Azar {
  uint32_t s;
  explicit Azar(uint32_t semilla = 0x12345678u) : s(semilla == 0 ? 1u : semilla) {}
};
inline uint32_t azarDet(void* ctx) {
  Azar* a = static_cast<Azar*>(ctx);
  uint32_t x = a->s;
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  a->s = x;
  return x;
}
/* Fuente de azar que siempre devuelve 0: elimina el jitter y deja ver los
 * tiempos "de libro" de la fórmula del §7.3. */
inline uint32_t azarCero(void*) { return 0u; }

/* --------------------------------------------------------------------------
 *  Un suceso ya recogido, con el nodo que lo emitió.
 * ------------------------------------------------------------------------*/
struct EventoDe {
  int      nodo;        // índice en el canal
  uint32_t t;           // instante en que se recogió
  Evento   ev;
};

/* ==========================================================================
 *  Canal
 * ========================================================================*/
class Canal {
 public:
  static const int N_MAX      = 8;
  static const int VUELOS_MAX = 16;
  static const int EVENTOS_MAX = 4096;

  /* `paso` es la resolución del bucle simulado. 1 ms es lo más fiel; para las
   * pruebas de 10 minutos (la insistencia de una ALERT, §7.3) se usa 10 ms,
   * que sigue siendo muy inferior a cualquier timeout del motor. */
  explicit Canal(uint32_t paso = 1) : n_(0), ahora_(0), paso_(paso), nEvTotal_(0) {
    memset(alcanza_, 0, sizeof(alcanza_));
    memset(snr_, 0, sizeof(snr_));
    memset(rssi_, 0, sizeof(rssi_));
    memset(vuelo_, 0, sizeof(vuelo_));
    memset(m_, 0, sizeof(m_));
    for (int i = 0; i < N_MAX; ++i) {
      azar_[i] = Azar(0x1000u + 0x9E3779B9u * static_cast<uint32_t>(i + 1));
      perder_[i] = false;
      txs_[i] = 0;
    }
  }
  ~Canal() {
    for (int i = 0; i < n_; ++i) delete m_[i];
  }

  /* ---- montaje -----------------------------------------------------------*/
  /* Añade un nodo. Devuelve su índice. El ID por defecto es 0x0001, 0x0002…
   * para que se lean bien en los fallos. Con `sinJitter` el nodo usa la
   * fuente de azar que devuelve 0. */
  int anadir(const ConfigNodo& cfgBase, uint16_t id = 0, bool sinJitter = false) {
    TEST_ASSERT_TRUE(n_ < N_MAX);
    const int i = n_++;
    ConfigNodo c = cfgBase;
    c.id = id != 0 ? id : static_cast<uint16_t>(i + 1);
    m_[i] = new Motor(c, sinJitter ? azarCero : azarDet, &azar_[i], static_cast<uint16_t>(0x100 * (i + 1)));
    return i;
  }

  /* Enlace SIMÉTRICO a/b con ese SNR (en dB) y RSSI. */
  void enlazar(int a, int b, int snrDb = 5, int16_t rssiDbm = -90) {
    dirigir(a, b, snrDb, rssiDbm);
    dirigir(b, a, snrDb, rssiDbm);
  }
  /* Enlace de UN sentido (a oye a b... no: a ALCANZA a b, o sea b oye a a). */
  void dirigir(int a, int b, int snrDb = 5, int16_t rssiDbm = -90) {
    alcanza_[a][b] = true;
    snr_[a][b] = snrAQ4(static_cast<float>(snrDb));
    rssi_[a][b] = rssiDbm;
  }
  void cortar(int a, int b) { alcanza_[a][b] = false; alcanza_[b][a] = false; }
  /* Todos con todos (malla densa). */
  void enlazarTodos(int snrDb = 5) {
    for (int a = 0; a < n_; ++a)
      for (int b = 0; b < n_; ++b)
        if (a != b) dirigir(a, b, snrDb);
  }
  /* Cadena 0–1–2–…: sólo se oyen los contiguos. */
  void enlazarEnLinea(int snrDb = 5) {
    for (int a = 0; a + 1 < n_; ++a) enlazar(a, a + 1, snrDb);
  }

  /* El nodo `i` deja de oír (se apaga la recepción, como una placa apagada o
   * fuera de cobertura). Sigue pudiendo transmitir. */
  void perder(int i, bool s) { perder_[i] = s; }

  /* Cambia la semilla de azar de un nodo. Sirve para repetir una prueba con
   * jitters distintos y ver que el resultado no depende de la suerte. */
  void sembrar(int i, uint32_t semilla) { azar_[i] = Azar(semilla); }

  /* ---- acceso ------------------------------------------------------------*/
  Motor&   nodo(int i) { return *m_[i]; }
  uint16_t id(int i) const { return m_[i]->config().id; }
  uint32_t ahora() const { return ahora_; }
  int      numNodos() const { return n_; }
  /* Transmisiones que ha hecho el nodo (las cuenta el canal, no el motor). */
  uint32_t txs(int i) const { return txs_[i]; }
  uint32_t txsTotales() const {
    uint32_t s = 0;
    for (int i = 0; i < n_; ++i) s += txs_[i];
    return s;
  }
  void reiniciarTxs() { for (int i = 0; i < n_; ++i) txs_[i] = 0; }

  /* ---- bucle --------------------------------------------------------------
   *  Avanza el reloj simulado `ms` milisegundos ejecutando el bucle de todos
   *  los nodos. Devuelve cuántos sucesos nuevos se recogieron.
   * ----------------------------------------------------------------------*/
  int avanzar(uint32_t ms) {
    const int antes = nEvTotal_;
    const uint32_t fin = ahora_ + ms;
    while (static_cast<int32_t>(fin - ahora_) > 0) {
      paso();
      ahora_ += paso_;
    }
    paso();   // una vuelta más en el instante final
    return nEvTotal_ - antes;
  }

  /* Avanza hasta que el nodo `i` emita ese suceso, o hasta `limiteMs`.
   * Devuelve true si apareció; el suceso queda en `out` (y también en el
   * registro). */
  bool esperar(int i, TipoEvento t, uint32_t limiteMs, Evento* out = nullptr) {
    const uint32_t t0 = ahora_;
    while (static_cast<int32_t>(ahora_ - t0) < static_cast<int32_t>(limiteMs)) {
      const int desde = nEvTotal_;
      avanzar(paso_);
      for (int k = desde; k < nEvTotal_; ++k) {
        const EventoDe& e = ev_[k];
        if (e.nodo == i && e.ev.tipo == t) {
          if (out != nullptr) *out = e.ev;
          return true;
        }
      }
    }
    return false;
  }

  /* ---- registro de sucesos ----------------------------------------------*/
  int  numEventos() const { return nEvTotal_; }
  const EventoDe& evento(int k) const { return ev_[k]; }
  void olvidarEventos() { nEvTotal_ = 0; }

  /* ¿Cuántas veces ha emitido el nodo `i` ese suceso? (-1 en `i` = cualquiera) */
  int cuenta(int i, TipoEvento t) const {
    int c = 0;
    for (int k = 0; k < nEvTotal_; ++k) {
      const EventoDe& e = ev_[k];
      if ((i < 0 || e.nodo == i) && e.ev.tipo == t) ++c;
    }
    return c;
  }
  /* El primero de ese tipo en ese nodo, o nullptr. */
  const EventoDe* primero(int i, TipoEvento t) const {
    for (int k = 0; k < nEvTotal_; ++k) {
      const EventoDe& e = ev_[k];
      if ((i < 0 || e.nodo == i) && e.ev.tipo == t) return &e;
    }
    return nullptr;
  }
  const EventoDe* ultimo(int i, TipoEvento t) const {
    const EventoDe* r = nullptr;
    for (int k = 0; k < nEvTotal_; ++k) {
      const EventoDe& e = ev_[k];
      if ((i < 0 || e.nodo == i) && e.ev.tipo == t) r = &e;
    }
    return r;
  }

 private:
  struct Vuelo {
    bool     usado;
    int      emisor;
    uint8_t  trama[TRAMA_MAX];
    uint8_t  len;
    uint32_t tIni;
    uint32_t tFin;
    uint32_t airtime;
  };

  static bool vencido(uint32_t ahora, uint32_t t) { return static_cast<int32_t>(ahora - t) >= 0; }

  /* ¿El nodo `i` está transmitiendo ahora mismo? (semidúplex) */
  bool transmitiendo(int i) const {
    for (int v = 0; v < VUELOS_MAX; ++v) {
      if (vuelo_[v].usado && vuelo_[v].emisor == i) return true;
    }
    return false;
  }
  /* ¿Hay algo en el aire que le llegue al nodo `i`? (lo que ve el CAD) */
  bool aireOcupado(int i) const {
    for (int v = 0; v < VUELOS_MAX; ++v) {
      const Vuelo& f = vuelo_[v];
      if (f.usado && f.emisor != i && alcanza_[f.emisor][i]) return true;
    }
    return false;
  }

  void recoger(int i) {
    Evento e;
    while (m_[i]->siguienteEvento(e)) {
      /* Si el registro se llenara, los contadores de las pruebas mentirían:
       * mejor fallar aquí y subir EVENTOS_MAX que dar un resultado falso. */
      TEST_ASSERT_TRUE_MESSAGE(nEvTotal_ < EVENTOS_MAX, "registro de sucesos lleno");
      EventoDe& d = ev_[nEvTotal_];
      d.nodo = i;
      d.t = ahora_;
      d.ev = e;
      ++nEvTotal_;
    }
  }

  void paso() {
    /* 1. Transmisiones que terminan ahora. Primero se entregan TODAS (para
     *    que el solape semidúplex se vea aunque dos acaben en el mismo paso)
     *    y sólo después se cierran. */
    bool acaba[VUELOS_MAX];
    for (int v = 0; v < VUELOS_MAX; ++v) {
      acaba[v] = vuelo_[v].usado && vencido(ahora_, vuelo_[v].tFin);
    }
    for (int v = 0; v < VUELOS_MAX; ++v) {
      if (!acaba[v]) continue;
      const Vuelo& f = vuelo_[v];
      for (int b = 0; b < n_; ++b) {
        if (b == f.emisor || !alcanza_[f.emisor][b] || perder_[b]) continue;
        /* Semidúplex: si el receptor estuvo transmitiendo mientras la trama
         * pasaba, no la oyó. */
        if (ocupadoEntre(b, f.tIni, f.tFin, v)) continue;
        m_[b]->alRecibir(f.trama, f.len, rssi_[f.emisor][b], snr_[f.emisor][b], ahora_);
      }
    }
    for (int v = 0; v < VUELOS_MAX; ++v) {
      if (!acaba[v]) continue;
      m_[vuelo_[v].emisor]->txHecha(ahora_, vuelo_[v].airtime);
      ++txs_[vuelo_[v].emisor];
      vuelo_[v].usado = false;
    }
    /* 2. Temporizadores. */
    for (int i = 0; i < n_; ++i) m_[i]->tick(ahora_);
    /* 3. ¿Alguien quiere transmitir? */
    for (int i = 0; i < n_; ++i) {
      if (transmitiendo(i)) continue;
      TramaSalida out;
      if (!m_[i]->siguienteTx(ahora_, out)) continue;
      if (out.usarCad && aireOcupado(i)) {
        m_[i]->canalOcupado(ahora_);
        continue;
      }
      int v = -1;
      for (int k = 0; k < VUELOS_MAX; ++k) {
        if (!vuelo_[k].usado) { v = k; break; }
      }
      if (v < 0) {                       // el aire de la simulación, lleno
        m_[i]->txFallida(ahora_);
        continue;
      }
      Vuelo& f = vuelo_[v];
      f.usado = true;
      f.emisor = i;
      memcpy(f.trama, out.datos, out.len);
      f.len = out.len;
      f.tIni = ahora_;
      f.airtime = out.airtimeMs;
      f.tFin = ahora_ + out.airtimeMs;
    }
    /* 4. Sucesos. */
    for (int i = 0; i < n_; ++i) recoger(i);
  }

  /* ¿El nodo `b` transmitió algo que se solape con [tIni, tFin]? */
  bool ocupadoEntre(int b, uint32_t tIni, uint32_t tFin, int salvo) const {
    for (int v = 0; v < VUELOS_MAX; ++v) {
      if (v == salvo) continue;
      const Vuelo& f = vuelo_[v];
      if (!f.usado || f.emisor != b) continue;
      if (static_cast<int32_t>(f.tIni - tFin) <= 0 && static_cast<int32_t>(tIni - f.tFin) <= 0) return true;
    }
    return false;
  }

  int        n_;
  Motor*     m_[N_MAX];
  Azar       azar_[N_MAX];
  bool       alcanza_[N_MAX][N_MAX];
  int8_t     snr_[N_MAX][N_MAX];
  int16_t    rssi_[N_MAX][N_MAX];
  bool       perder_[N_MAX];
  uint32_t   txs_[N_MAX];
  Vuelo      vuelo_[VUELOS_MAX];
  uint32_t   ahora_;
  uint32_t   paso_;
  EventoDe   ev_[EVENTOS_MAX];
  int        nEvTotal_;
};

/* --------------------------------------------------------------------------
 *  Configuraciones de uso corriente.
 * ------------------------------------------------------------------------*/
/* E1: punto a punto, sin reenvío. */
inline ConfigNodo cfgE1() {
  ConfigNodo c;
  c.ttl = 0;
  c.relay = false;
  c.rol = Rol::NORMAL;
  c.radio = paramsDefecto();
  return c;
}
/* E2: multi-salto (§8.3). Lo único que cambia es ttl y relay. */
inline ConfigNodo cfgE2(uint8_t ttl = 3) {
  ConfigNodo c = cfgE1();
  c.ttl = ttl;
  c.relay = true;
  return c;
}

/* Texto como carga de DATA. */
inline const uint8_t* bytes(const char* s) { return reinterpret_cast<const uint8_t*>(s); }
inline uint8_t largo(const char* s) { return static_cast<uint8_t>(strlen(s)); }

}  // namespace ayuda
