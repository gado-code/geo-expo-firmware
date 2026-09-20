/* ============================================================================
 *  GEO-EXPO LoRa  ·  Motor del nodo: ACK, reintentos, duplicados, reenvío
 *                    (DISENO §7, §8, §12.2)
 * ============================================================================
 *  Es TODA la lógica de red, sin Arduino, sin radio y sin reloj propio:
 *
 *    · el TIEMPO entra como parámetro (ahoraMs) en cada llamada;
 *    · el AZAR entra por una función inyectada (en la placa esp_random, en
 *      los tests un generador determinista con semilla);
 *    · la RADIO la maneja quien lo usa: el motor sólo dice QUÉ transmitir
 *      (siguienteTx) y se entera de CÓMO fue (txHecha / canalOcupado /
 *      txFallida) y de lo que llega (alRecibir).
 *
 *  Así el mismo Motor se instancia N veces en test_motor_malla sobre un canal
 *  simulado y toda la lógica de E2 se prueba en el PC.
 *
 *  Sin memoria dinámica: colas y cachés son arrays fijos (≈ 11 KiB por
 *  instancia, casi todo búferes de trama de 214 B).
 *
 *  ── Ciclo de uso (lo que hace src/main.cpp en cada vuelta de loop) ──────
 *      1. trama recibida           -> alRecibir(trama, n, rssi, snrQ4, ahora)
 *      2. temporizadores           -> tick(ahora)
 *      3. ¿algo que transmitir?    -> siguienteTx(ahora, out)
 *            CAD libre + TX bien   -> txHecha(ahora, airtime)
 *            CAD ocupado           -> canalOcupado(ahora)
 *            error de la radio     -> txFallida(ahora)
 *         (entre siguienteTx y su cierre no se llama a nada más del motor)
 *      4. sucesos                  -> while (siguienteEvento(ev)) ...
 *
 *  ── E1 -> E2 sólo con configuración (§8.3) ──────────────────────────────
 *      E1: ttl = 0, relay = false      E2: ttl = 3, relay = true
 *  Con ttl 0 las tramas propias salen con ttl 0 y nadie las repite aunque
 *  tenga relay: los dos modos conviven en la misma red.
 * ==========================================================================*/
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "carga.h"
#include "paquete.h"
#include "radio_params.h"
#include "vistos.h"

namespace geolora {

/* --------------------------------------------------------------------------
 *  Rol del nodo (§8.2, §14).
 * ------------------------------------------------------------------------*/
enum class Rol : uint8_t {
  NORMAL,      // nodo corriente
  REPETIDOR,   // nodo fijo en altura: reenvía SIN retardo por SNR (D_snr = 0)
  PASARELA     // entrega y confirma lo dirigido a 0xFFFE (futura pasarela, §14)
};
const char* nombreRol(Rol r);
bool        rolPorNombre(const char* s, Rol& r);   // sin distinguir mayúsculas

struct ConfigNodo {
  uint16_t    id;
  uint8_t     ttl;      // ttl de las tramas PROPIAS (0 = E1 punto a punto)
  bool        relay;    // reenviar tramas ajenas (E2)
  Rol         rol;
  ParamsRadio radio;    // para calcular airtime y timeouts
  ConfigNodo() : id(0x0001), ttl(0), relay(false), rol(Rol::NORMAL), radio(paramsDefecto()) {}
};

/* --------------------------------------------------------------------------
 *  Tiempos y límites de la política de fiabilidad (§7.1–§7.3).
 * ------------------------------------------------------------------------*/
static const uint32_t GIRO_MS             = 30;      // retardo antes de contestar (semidúplex)
static const uint32_t MARGEN_MS           = 100;     // margen de los timeouts
static const uint8_t  DATA_INTENTOS       = 4;       // 1 + 3 reintentos
static const uint32_t DATA_BMAX_MS        = 8000;
static const uint8_t  ALERTA_RAPIDOS      = 8;       // 1 + 7 rápidos, luego lentos
static const uint32_t ALERTA_BMAX_MS      = 30000;
static const uint32_t ALERTA_LENTO_MS     = 30000;   // un intento cada 30 s
static const uint32_t ALERTA_MAX_MS       = 600000;  // se insiste 10 min
static const uint8_t  BCAST_INTENTOS      = 2;       // en E2: 1 + 1 si no se oye el eco
static const uint32_t PONG_BCAST_MAX_MS   = 2000;    // PING *: respuesta tras U(0, 2 s)
static const uint8_t  CAD_MAX             = 5;       // canal ocupado N veces -> ALERT sale igual
static const uint32_t VIDA_REENVIO_MS     = 30000;   // un reenvío más viejo se descarta

/* --------------------------------------------------------------------------
 *  Sucesos que el motor comunica hacia fuera (consola "EV ...", OLED, LED).
 * ------------------------------------------------------------------------*/
enum class TipoEvento : uint8_t {
  ENTREGADO,       // DATA para mí o broadcast               (nodo=src)
  ACK,             // mi DATA/ALERT confirmado               (nodo=quien confirma)
  FALLO,           // mi DATA/ALERT sin ACK tras agotar       (nodo=dst)
  REINTENTO,       // va a salir el intento `intentos`+1      (valor=espera usada)
  PONG,            // respuesta a mi PING                     (nodo=src del PONG)
  PING_PERDIDO,    // PING unicast sin PONG a tiempo          (nodo=dst)
  PING_FIN,        // PING * cerrado                          (n=PONGs recibidos)
  ALERTA,          // ALERT recibida (entregada)              (nodo=src)
  ALERTA_SIN_ACK,  // ciclo lento de una ALERT propia         (n=segundos desde la 1.ª)
  ECO_BCAST,       // ACK implícito: oí a otro repetir mi broadcast
  BCAST_SIN_ECO,   // broadcast en E2 sin eco tras el reintento
  ID_DUPLICADO,    // otra radio transmite con MI ID
  DESCARTE_COLA    // un reenvío se tiró por cola llena o por viejo
};
const char* nombreEvento(TipoEvento t);

struct Evento {
  TipoEvento  tipo;
  uint16_t    nodo;
  uint16_t    msgId;
  uint8_t     tipoTrama;
  uint8_t     flags;
  uint8_t     intentos;
  uint8_t     hops;          // saltos de la trama recibida (vuelta)
  uint8_t     hopsIda;       // saltos de la ida (hopsRx del ACK/PONG)
  uint32_t    valor;         // RTT en ms (ACK/PONG) o espera (REINTENTO)
  uint32_t    n;             // PONGs (PING_FIN) o segundos (ALERTA_SIN_ACK)
  bool        hayIda;        // rssiIda/snrIda válidos (vienen en el ACK/PONG)
  int16_t     rssiIda;       // medido en el OTRO extremo
  int8_t      snrIdaQ4;
  int16_t     rssiVuelta;    // medido AQUÍ
  int8_t      snrVueltaQ4;
  CargaAlerta alerta;        // ALERTA
  uint8_t     len;           // ENTREGADO: bytes de datos
  uint8_t     datos[LEN_MAX + 1];   // ENTREGADO: carga + '\0' (para imprimir)
};

struct Contadores {
  uint32_t tx;                // tramas transmitidas (todas)
  uint32_t rxTramas;          // tramas que llegaron de la radio
  uint32_t rxValidas;         // superaron la validación
  uint32_t entregados;        // DATA entregados
  uint32_t duplicados;        // descartados por la caché de vistos
  uint32_t ecos;              // copias de mis propias tramas
  uint32_t acksTx;
  uint32_t acksRx;            // ACK que confirmaron algo
  uint32_t acksHuerfanos;     // ACK/PONG que no casan con nada pendiente
  uint32_t reintentos;
  uint32_t fallos;
  uint32_t reenviados;        // reenvíos transmitidos
  uint32_t suprimidos;        // reenvíos cancelados porque otro ya lo repitió
  uint32_t descartesCola;
  uint32_t cadOcupado;
  uint32_t txErrores;
  uint32_t perdidaSimulada;   // PERDIDA <pct>
  uint32_t idDuplicado;
  uint32_t alertasRx;
  uint32_t pongsRx;
  uint32_t pingsRx;
  uint32_t eventosPerdidos;
  uint32_t rechazos[static_cast<int>(Rechazo::N_RECHAZOS)];
};

struct Vecino {
  bool     usado;
  uint16_t id;
  int16_t  rssi;       // última trama directa (hops 0)
  int8_t   snrQ4;
  uint32_t t;          // cuándo se oyó (o se aprendió)
  bool     directo;    // se le ha oído sin intermediarios
  uint8_t  saltos;     // hopsRx aprendido de su ACK/PONG (0xFF = desconocido)
};

struct TramaSalida {
  uint8_t  datos[TRAMA_MAX];
  uint8_t  len;
  bool     usarCad;     // false = transmitir sin escuchar (ALERT tras CAD_MAX)
  uint32_t airtimeMs;   // estimado con airtimeMs()
};

class Motor {
 public:
  typedef uint32_t (*FuenteAzar)(void* ctx);

  static const int N_SALIENTES  = 8;
  static const int N_RESPUESTAS = 4;
  static const int N_REENVIOS   = 8;
  static const int N_EVENTOS    = 16;
  static const int N_VECINOS    = 16;

  Motor(const ConfigNodo& cfg, FuenteAzar azar, void* ctxAzar, uint16_t msgIdInicial);

  /* Cambia ID/ttl/relay/rol/radio en caliente. Lo pendiente sigue su curso. */
  void              configurar(const ConfigNodo& cfg) { cfg_ = cfg; }
  const ConfigNodo& config() const { return cfg_; }

  /* ---- envíos propios. Devuelven el msgId, o -1 si no hay hueco o los
   *      argumentos no valen. Nada sale en el acto: sale por siguienteTx(). */
  /* DATA: unicast con ACK_REQ; a DIR_BROADCAST sin ACK (en E2, ACK implícito). */
  int32_t enviarDatos(uint16_t dst, const uint8_t* datos, uint8_t len, uint32_t ahoraMs);
  /* PING a un nodo o a DIR_BROADCAST (descubrimiento), con `relleno` bytes. */
  int32_t enviarPing(uint16_t dst, uint8_t relleno, uint32_t ahoraMs);
  /* ALERT a DIR_PASARELA con ACK_REQ. codigo ALERTA_1/ALERTA_2 abre una alerta
   * nueva (seqAlerta+1); ALERTA_CANCEL reutiliza el seq de la última y DETIENE
   * los reintentos de esa alerta antes de salir. */
  int32_t enviarAlerta(uint8_t codigo, bool conPos, int32_t lat1e7, int32_t lon1e7, bool prueba,
                       uint32_t ahoraMs);

  /* ---- entradas ---- */
  void alRecibir(const uint8_t* trama, size_t len, int16_t rssiDbm, int8_t snrQ4, uint32_t ahoraMs);
  void tick(uint32_t ahoraMs);

  /* ---- radio ---- */
  bool siguienteTx(uint32_t ahoraMs, TramaSalida& out);
  void txHecha(uint32_t ahoraMs, uint32_t airtimeMs);
  void canalOcupado(uint32_t ahoraMs);
  void txFallida(uint32_t ahoraMs);

  /* ---- salidas ---- */
  bool siguienteEvento(Evento& ev);

  /* ---- estado ---- */
  const Contadores& contadores() const { return cnt_; }
  void              reiniciarContadores();
  uint32_t          ocupacionPorMil(uint32_t ahoraMs) { return ocup_.porMil(ahoraMs); }
  const Vecino&     vecino(int i) const { return vec_[i]; }
  /* Vecino DIRECTO oído más recientemente (para el botón PRG). */
  bool              ultimoVecino(uint16_t& id) const;
  int               salientesVivas() const;
  bool              alertaActiva() const;
  uint8_t           seqAlerta() const { return seqAlerta_; }
  uint16_t          proximoMsgId() const { return msgId_; }
  /* Sólo depuración (§9 PERDIDA): descarta en RX ese % de tramas. */
  void              setPerdida(uint8_t pct) { perdidaPct_ = pct > 100 ? 100 : pct; }
  uint8_t           perdida() const { return perdidaPct_; }

  /* Timeouts expuestos para los tests y para la consola. */
  uint32_t timeoutAck(uint16_t lenTrama, uint16_t lenRespuesta, uint8_t ttl, uint16_t dst) const;
  uint32_t ventanaInundacion(uint16_t lenTrama, uint8_t ttl) const;

 private:
  enum class Clase : uint8_t { DATA_UNI, DATA_BCAST, PING_UNI, PING_BCAST, ALERTA };
  enum class EstadoS : uint8_t { LIBRE, COLA, ESPERA };
  enum class Origen : uint8_t { NINGUNO, SALIENTE, RESPUESTA, REENVIO };

  struct Saliente {
    EstadoS  estado;
    Clase    clase;
    uint8_t  trama[TRAMA_MAX];
    uint8_t  len;
    uint16_t dst;
    uint16_t msgId;
    uint8_t  intentos;       // transmisiones hechas
    uint8_t  cadOcupado;
    uint8_t  codigoAlerta;   // ALERTA: 1/2/3
    uint8_t  seqAlerta;
    uint32_t tNoAntes;       // COLA: no salir antes de
    uint32_t tLimite;        // ESPERA: vence el timeout del intento
    uint32_t tProximo;       // ESPERA: toca el siguiente intento
    uint32_t tPrimera;       // inicio de la 1.ª TX
    uint32_t tInicioUltima;  // inicio de la última TX (RTT)
    uint32_t tPing;          // PING: t_ms que viajó
    uint16_t pongs;          // PING *: respuestas
  };

  struct Pendiente {
    bool     usado;
    bool     exento;         // ACK y ALERT: no cuentan para el presupuesto
    uint8_t  trama[TRAMA_MAX];
    uint8_t  len;
    uint16_t src;            // clave de la trama (supresión)
    uint16_t msgId;
    uint32_t tNoAntes;
    uint32_t tAlta;
    uint8_t  cadOcupado;
  };

  /* helpers */
  uint32_t azar();
  uint32_t uniforme(uint32_t maxIncl);         // U(0, maxIncl)
  uint16_t nuevoMsgId();
  uint32_t airtime(uint16_t len) const { return airtimeMs(cfg_.radio, len); }
  static bool vencido(uint32_t ahora, uint32_t t) { return static_cast<int32_t>(ahora - t) >= 0; }
  bool     soyDestinoConAck(const Cabecera& h) const;
  int      saliente(uint16_t msgId);
  int      alta(Clase c, uint16_t dst, uint8_t tipo, uint8_t flags, const uint8_t* carga, uint8_t len,
                uint32_t ahoraMs);
  uint32_t espera(const Saliente& s) const;     // entre el intento k y el k+1 (sin jitter)
  void     programarSiguiente(Saliente& s, uint32_t ahoraMs);
  void     liberar(Saliente& s) { s.estado = EstadoS::LIBRE; }
  void     emitir(const Evento& ev);
  Evento   evento(TipoEvento t) const;
  void     encolarRespuesta(uint16_t dst, Tipo tipo, const uint8_t* carga, uint8_t len, uint32_t retardo,
                            uint32_t ahoraMs);
  void     encolarAck(const Cabecera& h, int16_t rssi, int8_t snrQ4, uint32_t ahoraMs);
  void     programarReenvio(const uint8_t* trama, size_t len, const Cabecera& h, int8_t snrQ4,
                            uint32_t ahoraMs);
  bool     suprimir(uint16_t src, uint16_t msgId);
  void     procesarLocal(const Cabecera& h, const uint8_t* carga, int16_t rssi, int8_t snrQ4,
                         uint32_t ahoraMs);
  void     vecinoDirecto(uint16_t id, int16_t rssi, int8_t snrQ4, uint32_t ahoraMs);
  void     aprenderSaltos(uint16_t id, uint8_t saltos, uint32_t ahoraMs);
  int      buscarVecino(uint16_t id) const;
  int      nuevoVecino(uint16_t id, uint32_t ahoraMs);

  ConfigNodo  cfg_;
  FuenteAzar  azar_;
  void*       ctxAzar_;
  uint16_t    msgId_;
  uint8_t     seqAlerta_;
  uint8_t     perdidaPct_;

  Saliente    sal_[N_SALIENTES];
  Pendiente   resp_[N_RESPUESTAS];
  Pendiente   reenv_[N_REENVIOS];
  Origen      vueloOrigen_;
  int         vueloIdx_;

  Evento      ev_[N_EVENTOS];
  int         evIni_;
  int         evN_;

  Vistos      vistos_;
  Vecino      vec_[N_VECINOS];
  Ocupacion   ocup_;
  Contadores  cnt_;
};

}  // namespace geolora
