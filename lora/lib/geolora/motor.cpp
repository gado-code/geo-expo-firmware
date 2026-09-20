/* ============================================================================
 *  GEO-EXPO LoRa  ·  Motor del nodo  (ver motor.h y DISENO §7–§8)
 * ==========================================================================*/
#include "motor.h"

#include <string.h>

#include "crc16.h"

namespace geolora {

/* ============================================================ nombres ===*/
const char* nombreRol(Rol r) {
  switch (r) {
    case Rol::NORMAL:    return "NORMAL";
    case Rol::REPETIDOR: return "REPETIDOR";
    case Rol::PASARELA:  return "PASARELA";
    default:             return "?";
  }
}

static bool igualSinMayus(const char* a, const char* b) {
  if (a == nullptr || b == nullptr) return false;
  while (*a != '\0' && *b != '\0') {
    char x = *a, y = *b;
    if (x >= 'a' && x <= 'z') x = static_cast<char>(x - 'a' + 'A');
    if (y >= 'a' && y <= 'z') y = static_cast<char>(y - 'a' + 'A');
    if (x != y) return false;
    ++a; ++b;
  }
  return *a == '\0' && *b == '\0';
}

bool rolPorNombre(const char* s, Rol& r) {
  const Rol todos[] = { Rol::NORMAL, Rol::REPETIDOR, Rol::PASARELA };
  for (size_t i = 0; i < 3; ++i) {
    if (igualSinMayus(s, nombreRol(todos[i]))) { r = todos[i]; return true; }
  }
  return false;
}

const char* nombreEvento(TipoEvento t) {
  switch (t) {
    case TipoEvento::ENTREGADO:      return "ENTREGADO";
    case TipoEvento::ACK:            return "ACK";
    case TipoEvento::FALLO:          return "FALLO";
    case TipoEvento::REINTENTO:      return "REINTENTO";
    case TipoEvento::PONG:           return "PONG";
    case TipoEvento::PING_PERDIDO:   return "PING_PERDIDO";
    case TipoEvento::PING_FIN:       return "PING_FIN";
    case TipoEvento::ALERTA:         return "ALERTA";
    case TipoEvento::ALERTA_SIN_ACK: return "ALERTA_SIN_ACK";
    case TipoEvento::ECO_BCAST:      return "ECO_BCAST";
    case TipoEvento::BCAST_SIN_ECO:  return "BCAST_SIN_ECO";
    case TipoEvento::ID_DUPLICADO:   return "ID_DUPLICADO";
    case TipoEvento::DESCARTE_COLA:  return "DESCARTE_COLA";
    default:                         return "?";
  }
}

/* ======================================================== construcción ==*/
Motor::Motor(const ConfigNodo& cfg, FuenteAzar azar, void* ctxAzar, uint16_t msgIdInicial)
    : cfg_(cfg),
      azar_(azar),
      ctxAzar_(ctxAzar),
      msgId_(msgIdInicial),
      seqAlerta_(0),
      perdidaPct_(0),
      vueloOrigen_(Origen::NINGUNO),
      vueloIdx_(-1),
      evIni_(0),
      evN_(0) {
  memset(sal_, 0, sizeof(sal_));
  memset(resp_, 0, sizeof(resp_));
  memset(reenv_, 0, sizeof(reenv_));
  memset(vec_, 0, sizeof(vec_));
  memset(ev_, 0, sizeof(ev_));
  for (int i = 0; i < N_SALIENTES; ++i) sal_[i].estado = EstadoS::LIBRE;
  reiniciarContadores();
}

void Motor::reiniciarContadores() { memset(&cnt_, 0, sizeof(cnt_)); }

/* ============================================================ helpers ===*/
uint32_t Motor::azar() { return azar_ != nullptr ? azar_(ctxAzar_) : 0u; }

uint32_t Motor::uniforme(uint32_t maxIncl) {
  if (maxIncl == 0) return 0;
  return azar() % (maxIncl + 1u);
}

uint16_t Motor::nuevoMsgId() {
  const uint16_t id = msgId_;
  msgId_ = static_cast<uint16_t>(msgId_ + 1u);
  return id;
}

bool Motor::soyDestinoConAck(const Cabecera& h) const {
  return h.dst == cfg_.id || (h.dst == DIR_PASARELA && cfg_.rol == Rol::PASARELA);
}

int Motor::saliente(uint16_t msgId) {
  for (int i = 0; i < N_SALIENTES; ++i) {
    if (sal_[i].estado != EstadoS::LIBRE && sal_[i].msgId == msgId) return i;
  }
  return -1;
}

Evento Motor::evento(TipoEvento t) const {
  Evento e;
  memset(&e, 0, sizeof(e));
  e.tipo = t;
  return e;
}

void Motor::emitir(const Evento& ev) {
  if (evN_ >= N_EVENTOS) { ++cnt_.eventosPerdidos; return; }
  ev_[(evIni_ + evN_) % N_EVENTOS] = ev;
  ++evN_;
}

bool Motor::siguienteEvento(Evento& ev) {
  if (evN_ == 0) return false;
  ev = ev_[evIni_];
  evIni_ = (evIni_ + 1) % N_EVENTOS;
  --evN_;
  return true;
}

int Motor::salientesVivas() const {
  int n = 0;
  for (int i = 0; i < N_SALIENTES; ++i) if (sal_[i].estado != EstadoS::LIBRE) ++n;
  return n;
}

bool Motor::alertaActiva() const {
  for (int i = 0; i < N_SALIENTES; ++i) {
    if (sal_[i].estado != EstadoS::LIBRE && sal_[i].clase == Clase::ALERTA) return true;
  }
  return false;
}

/* ========================================================== timeouts ====*/
/* §7.2. En E1 (ttl 0): T_trama + giro + T_resp + margen.
 * En E2: (ttl+1)·(T + 2·CW) + (ttl+1)·(T_resp + 2·CW_resp) + giro + margen,
 * con CW = 2·T, es decir 5·T por salto en cada sentido.
 *
 * El diseño proponía un timeout "adaptativo" (hopsRx+1 en vez de ttl+1 al
 * conocer el camino). NO se aplica: con ttl grande y camino corto el
 * reintento podría caer dentro de la ventana de "misma inundación" de los
 * nodos intermedios, que lo tomarían por una copia y no lo reenviarían ni lo
 * confirmarían (ver ventanaInundacion y README, desviaciones). Los saltos
 * aprendidos se guardan igualmente y se ven en STATUS. */
uint32_t Motor::timeoutAck(uint16_t lenTrama, uint16_t lenRespuesta, uint8_t ttl, uint16_t dst) const {
  (void)dst;
  const uint32_t t = airtime(lenTrama);
  const uint32_t tr = airtime(lenRespuesta);
  if (ttl == 0) return t + GIRO_MS + tr + MARGEN_MS;
  const uint32_t n = static_cast<uint32_t>(ttl) + 1u;
  return n * 5u * t + n * 5u * tr + GIRO_MS + MARGEN_MS;
}

/* Tiempo, desde la PRIMERA copia vista, durante el que otra copia reenviada
 * de la misma trama pertenece a la misma inundación: cada salto restante
 * puede añadir hasta T + 2·CW = 5·T. */
uint32_t Motor::ventanaInundacion(uint16_t lenTrama, uint8_t ttl) const {
  return (static_cast<uint32_t>(ttl) + 1u) * 5u * airtime(lenTrama) + MARGEN_MS;
}

/* Espera entre el intento k y el k+1 SIN el jitter (§7.3):
 *   T_ack + min(B0·2^(k-1), B_max)       B0 = T_aire(trama) + 100 ms
 * y, para ALERT a partir del 8.º intento, el ciclo lento de 30 s. */
uint32_t Motor::espera(const Saliente& s) const {
  const uint8_t lenResp = (s.clase == Clase::PING_UNI || s.clase == Clase::PING_BCAST)
                              ? static_cast<uint8_t>(SOBRECARGA + LEN_PONG)
                              : static_cast<uint8_t>(SOBRECARGA + LEN_ACK);
  const uint32_t tack = timeoutAck(s.len, lenResp, s.trama[9], s.dst);
  const uint32_t b0 = airtime(s.len) + 100u;
  const uint8_t k = s.intentos == 0 ? 1 : s.intentos;
  const uint8_t sh = static_cast<uint8_t>(k - 1 > 16 ? 16 : k - 1);
  uint32_t b = b0 << sh;
  if (s.clase == Clase::ALERTA) {
    if (k >= ALERTA_RAPIDOS) return ALERTA_LENTO_MS;
    if (b > ALERTA_BMAX_MS) b = ALERTA_BMAX_MS;
  } else {
    if (b > DATA_BMAX_MS) b = DATA_BMAX_MS;
  }
  return tack + b;
}

void Motor::programarSiguiente(Saliente& s, uint32_t ahoraMs) {
  const uint8_t lenResp = (s.clase == Clase::PING_UNI || s.clase == Clase::PING_BCAST)
                              ? static_cast<uint8_t>(SOBRECARGA + LEN_PONG)
                              : static_cast<uint8_t>(SOBRECARGA + LEN_ACK);
  const uint32_t tack = timeoutAck(s.len, lenResp, s.trama[9], s.dst);
  const uint32_t t = airtime(s.len);
  s.estado = EstadoS::ESPERA;
  switch (s.clase) {
    case Clase::DATA_UNI:
    case Clase::ALERTA:
      s.tLimite = ahoraMs + tack;
      s.tProximo = ahoraMs + espera(s) + uniforme(t + 100u);   // + U(0, B0)
      break;
    case Clase::PING_UNI:
      s.tLimite = ahoraMs + tack;
      break;
    case Clase::PING_BCAST:
      s.tLimite = ahoraMs + PONG_BCAST_MAX_MS + tack;
      break;
    case Clase::DATA_BCAST:
      /* Eco: un vecino lo repite tras D_snr + U(0,CW) <= 2·CW = 4·T y tarda
       * T en el aire. */
      s.tLimite = ahoraMs + 5u * t + GIRO_MS + MARGEN_MS;
      break;
  }
}

/* ============================================================ envíos ====*/
int Motor::alta(Clase c, uint16_t dst, uint8_t tipo, uint8_t flags, const uint8_t* carga, uint8_t len,
                uint32_t ahoraMs) {
  int idx = -1;
  for (int i = 0; i < N_SALIENTES; ++i) {
    if (sal_[i].estado == EstadoS::LIBRE) { idx = i; break; }
  }
  if (idx < 0) return -1;
  Saliente& s = sal_[idx];
  Cabecera h;
  h.tipo = tipo;
  h.flags = flags;
  h.src = cfg_.id;
  h.dst = dst;
  h.msgId = nuevoMsgId();
  h.ttl = cfg_.ttl > TTL_MAX ? TTL_MAX : cfg_.ttl;
  h.hops = 0;
  h.len = len;
  const size_t n = codificar(h, carga, s.trama, sizeof(s.trama));
  if (n == 0) return -1;
  s.len = static_cast<uint8_t>(n);
  s.clase = c;
  s.dst = dst;
  s.msgId = h.msgId;
  s.intentos = 0;
  s.cadOcupado = 0;
  s.codigoAlerta = 0;
  s.seqAlerta = 0;
  s.tNoAntes = ahoraMs;
  s.tLimite = s.tProximo = s.tPrimera = s.tInicioUltima = s.tPing = ahoraMs;
  s.pongs = 0;
  s.estado = EstadoS::COLA;
  /* Mis propias tramas entran en vistos: así, en E2, su eco se reconoce
   * (regla 1) y no se toma por un ID duplicado. */
  vistos_.anadir(cfg_.id, h.msgId, ahoraMs, ventanaInundacion(s.len, h.ttl));
  return idx;
}

int32_t Motor::enviarDatos(uint16_t dst, const uint8_t* datos, uint8_t len, uint32_t ahoraMs) {
  if (len > LEN_MAX || (len > 0 && datos == nullptr)) return -1;
  const bool bcast = dst == DIR_BROADCAST;
  if (!bcast && idReservado(dst)) return -1;
  if (dst == cfg_.id) return -1;
  const int i = alta(bcast ? Clase::DATA_BCAST : Clase::DATA_UNI, dst, static_cast<uint8_t>(Tipo::DATA),
                     bcast ? 0 : FLAG_ACK_REQ, datos, len, ahoraMs);
  return i < 0 ? -1 : sal_[i].msgId;
}

int32_t Motor::enviarPing(uint16_t dst, uint8_t relleno, uint32_t ahoraMs) {
  const bool bcast = dst == DIR_BROADCAST;
  if (!bcast && idReservado(dst)) return -1;
  if (dst == cfg_.id) return -1;
  uint8_t carga[LEN_MAX];
  const uint8_t len = codificarPing(ahoraMs, relleno, carga);   // t_ms se reescribe al transmitir
  if (len == 0) return -1;
  const int i = alta(bcast ? Clase::PING_BCAST : Clase::PING_UNI, dst, static_cast<uint8_t>(Tipo::PING), 0,
                     carga, len, ahoraMs);
  return i < 0 ? -1 : sal_[i].msgId;
}

int32_t Motor::enviarAlerta(uint8_t codigo, bool conPos, int32_t lat1e7, int32_t lon1e7, bool prueba,
                            uint32_t ahoraMs) {
  if (tokenAlerta(codigo) == nullptr) return -1;
  CargaAlerta a;
  a.codigo = codigo;
  if (codigo == ALERTA_CANCEL) {
    /* El CANCEL lleva el seq de la alerta que anula y la deja de reintentar
     * (§7.3: "parada por CANCEL local"). */
    a.seq = seqAlerta_;
    for (int i = 0; i < N_SALIENTES; ++i) {
      Saliente& s = sal_[i];
      if (s.estado != EstadoS::LIBRE && s.clase == Clase::ALERTA && s.codigoAlerta != ALERTA_CANCEL &&
          s.seqAlerta == seqAlerta_ && !(vueloOrigen_ == Origen::SALIENTE && vueloIdx_ == i)) {
        liberar(s);
      }
    }
  } else {
    a.seq = static_cast<uint8_t>(seqAlerta_ + 1u);
  }
  if (conPos) {
    a.aflags |= AFLAG_POS;
    a.lat1e7 = lat1e7;
    a.lon1e7 = lon1e7;
  }
  uint8_t carga[LEN_ALERTA_CON_POS];
  const uint8_t len = codificarAlerta(a, carga);
  if (len == 0) return -1;
  const uint8_t flags = static_cast<uint8_t>(FLAG_ACK_REQ | (prueba ? FLAG_PRUEBA : 0));
  const int i = alta(Clase::ALERTA, DIR_PASARELA, static_cast<uint8_t>(Tipo::ALERT), flags, carga, len, ahoraMs);
  if (i < 0) return -1;
  sal_[i].codigoAlerta = codigo;
  sal_[i].seqAlerta = a.seq;
  if (codigo != ALERTA_CANCEL) seqAlerta_ = a.seq;
  return sal_[i].msgId;
}

/* ====================================================== respuestas ======*/
void Motor::encolarRespuesta(uint16_t dst, Tipo tipo, const uint8_t* carga, uint8_t len, uint32_t retardo,
                             uint32_t ahoraMs) {
  int idx = -1;
  for (int i = 0; i < N_RESPUESTAS; ++i) {
    if (!resp_[i].usado) { idx = i; break; }
  }
  if (idx < 0) {
    /* Cola de respuestas llena: se tira la más vieja que no esté en vuelo. */
    uint32_t edadMax = 0;
    for (int i = 0; i < N_RESPUESTAS; ++i) {
      if (vueloOrigen_ == Origen::RESPUESTA && vueloIdx_ == i) continue;
      const uint32_t edad = ahoraMs - resp_[i].tAlta;
      if (idx < 0 || edad > edadMax) { edadMax = edad; idx = i; }
    }
    if (idx < 0) return;
    ++cnt_.descartesCola;
  }
  Pendiente& p = resp_[idx];
  Cabecera h;
  h.tipo = static_cast<uint8_t>(tipo);
  h.flags = 0;
  h.src = cfg_.id;
  h.dst = dst;
  h.msgId = nuevoMsgId();
  h.ttl = cfg_.ttl > TTL_MAX ? TTL_MAX : cfg_.ttl;
  h.hops = 0;
  h.len = len;
  const size_t n = codificar(h, carga, p.trama, sizeof(p.trama));
  if (n == 0) return;
  p.usado = true;
  p.exento = (tipo == Tipo::ACK);
  p.len = static_cast<uint8_t>(n);
  p.src = h.src;
  p.msgId = h.msgId;
  p.tNoAntes = ahoraMs + retardo;
  p.tAlta = ahoraMs;
  p.cadOcupado = 0;
  vistos_.anadir(cfg_.id, h.msgId, ahoraMs, ventanaInundacion(p.len, h.ttl));
}

void Motor::encolarAck(const Cabecera& h, int16_t rssi, int8_t snrQ4, uint32_t ahoraMs) {
  CargaAck a;
  a.ackMsgId = h.msgId;
  a.rssiNeg = rssiANeg(rssi);
  a.snrQ4 = snrQ4;
  a.hopsRx = h.hops;
  uint8_t c[LEN_ACK];
  codificarAck(a, c);
  encolarRespuesta(h.src, Tipo::ACK, c, LEN_ACK, GIRO_MS, ahoraMs);
}

/* ========================================================== reenvío =====*/
/* §8.1 regla 7 y §8.2: copia con ttl-1, hops+1, CRC nuevo, y retardo
 * D_snr + U(0, CW): quien oyó PEOR (más lejos) repite ANTES. */
void Motor::programarReenvio(const uint8_t* trama, size_t len, const Cabecera& h, int8_t snrQ4,
                             uint32_t ahoraMs) {
  if (!cfg_.relay || h.ttl == 0 || h.hops >= HOPS_MAX || len > TRAMA_MAX) return;
  for (int i = 0; i < N_REENVIOS; ++i) {
    if (reenv_[i].usado && reenv_[i].src == h.src && reenv_[i].msgId == h.msgId) return;   // ya pendiente
  }
  const bool alerta = h.tipo == static_cast<uint8_t>(Tipo::ALERT);
  int idx = -1;
  for (int i = 0; i < N_REENVIOS; ++i) {
    if (!reenv_[i].usado) { idx = i; break; }
  }
  if (idx < 0) {
    /* Llena (§8.2): fuera el más antiguo que no sea ALERT; si todos lo son,
     * sólo entra otra ALERT (en lugar de la más antigua). */
    uint32_t edadMax = 0;
    for (int i = 0; i < N_REENVIOS; ++i) {
      if (vueloOrigen_ == Origen::REENVIO && vueloIdx_ == i) continue;
      const bool esAlerta = (reenv_[i].trama[1] & 0x0F) == static_cast<uint8_t>(Tipo::ALERT);
      if (esAlerta) continue;
      const uint32_t edad = ahoraMs - reenv_[i].tAlta;
      if (idx < 0 || edad > edadMax) { edadMax = edad; idx = i; }
    }
    if (idx < 0 && alerta) {
      for (int i = 0; i < N_REENVIOS; ++i) {
        if (vueloOrigen_ == Origen::REENVIO && vueloIdx_ == i) continue;
        const uint32_t edad = ahoraMs - reenv_[i].tAlta;
        if (idx < 0 || edad > edadMax) { edadMax = edad; idx = i; }
      }
    }
    ++cnt_.descartesCola;
    Evento ev = evento(TipoEvento::DESCARTE_COLA);
    ev.nodo = h.src;
    ev.msgId = h.msgId;
    ev.tipoTrama = h.tipo;
    emitir(ev);
    if (idx < 0) return;   // se descarta la nueva
  }
  Pendiente& p = reenv_[idx];
  memcpy(p.trama, trama, len);
  if (!prepararReenvio(p.trama, len)) { p.usado = false; return; }
  const uint32_t t = airtime(static_cast<uint16_t>(len));
  const uint32_t cw = 2u * t;
  uint32_t dsnr = 0;
  if (cfg_.rol != Rol::REPETIDOR) {
    /* clamp((snr + 20)/30, 0, 1) con snr en cuartos de dB: (q4 + 80)/120 */
    int32_t num = static_cast<int32_t>(snrQ4) + 80;
    if (num < 0) num = 0;
    if (num > 120) num = 120;
    dsnr = cw * static_cast<uint32_t>(num) / 120u;
  }
  p.usado = true;
  p.exento = alerta || h.tipo == static_cast<uint8_t>(Tipo::ACK);
  p.len = static_cast<uint8_t>(len);
  p.src = h.src;
  p.msgId = h.msgId;
  p.tNoAntes = ahoraMs + dsnr + uniforme(cw);
  p.tAlta = ahoraMs;
  p.cadOcupado = 0;
}

bool Motor::suprimir(uint16_t src, uint16_t msgId) {
  for (int i = 0; i < N_REENVIOS; ++i) {
    Pendiente& p = reenv_[i];
    if (!p.usado || p.src != src || p.msgId != msgId) continue;
    if (vueloOrigen_ == Origen::REENVIO && vueloIdx_ == i) continue;
    p.usado = false;
    ++cnt_.suprimidos;
    return true;
  }
  return false;
}

/* ========================================================== vecinos =====*/
int Motor::buscarVecino(uint16_t id) const {
  for (int i = 0; i < N_VECINOS; ++i) if (vec_[i].usado && vec_[i].id == id) return i;
  return -1;
}

int Motor::nuevoVecino(uint16_t id, uint32_t ahoraMs) {
  int idx = -1;
  uint32_t edadMax = 0;
  for (int i = 0; i < N_VECINOS; ++i) {
    if (!vec_[i].usado) { idx = i; break; }
    const uint32_t edad = ahoraMs - vec_[i].t;
    if (idx < 0 || edad > edadMax) { edadMax = edad; idx = i; }
  }
  Vecino& v = vec_[idx];
  memset(&v, 0, sizeof(v));
  v.usado = true;
  v.id = id;
  v.saltos = 0xFF;
  v.t = ahoraMs;
  return idx;
}

void Motor::vecinoDirecto(uint16_t id, int16_t rssi, int8_t snrQ4, uint32_t ahoraMs) {
  int i = buscarVecino(id);
  if (i < 0) i = nuevoVecino(id, ahoraMs);
  Vecino& v = vec_[i];
  v.rssi = rssi;
  v.snrQ4 = snrQ4;
  v.t = ahoraMs;
  v.directo = true;
}

void Motor::aprenderSaltos(uint16_t id, uint8_t saltos, uint32_t ahoraMs) {
  int i = buscarVecino(id);
  if (i < 0) i = nuevoVecino(id, ahoraMs);
  vec_[i].saltos = saltos;
}

bool Motor::ultimoVecino(uint16_t& id) const {
  int mejor = -1;
  for (int i = 0; i < N_VECINOS; ++i) {
    if (!vec_[i].usado || !vec_[i].directo) continue;
    if (mejor < 0 || static_cast<int32_t>(vec_[i].t - vec_[mejor].t) > 0) mejor = i;
  }
  if (mejor < 0) return false;
  id = vec_[mejor].id;
  return true;
}

/* ========================================================= recepción ====*/
void Motor::alRecibir(const uint8_t* trama, size_t len, int16_t rssiDbm, int8_t snrQ4, uint32_t ahoraMs) {
  ++cnt_.rxTramas;
  if (perdidaPct_ > 0 && (azar() % 100u) < perdidaPct_) {
    ++cnt_.perdidaSimulada;
    return;
  }
  Cabecera h;
  const uint8_t* carga = nullptr;
  const Rechazo r = decodificar(trama, len, h, carga);
  if (r != Rechazo::NINGUNO) {
    ++cnt_.rechazos[static_cast<int>(r)];
    if (r != Rechazo::TIPO) return;   // sólo un TIPO desconocido sigue (para reenviarlo)
  }
  ++cnt_.rxValidas;

  /* ---- Regla 1: la trama dice venir de MÍ -------------------------------*/
  if (h.src == cfg_.id) {
    if (vistos_.contiene(cfg_.id, h.msgId, ahoraMs)) {
      ++cnt_.ecos;
      const int i = saliente(h.msgId);
      if (i >= 0 && sal_[i].clase == Clase::DATA_BCAST && sal_[i].estado == EstadoS::ESPERA) {
        Evento ev = evento(TipoEvento::ECO_BCAST);   // ACK implícito (§8.1)
        ev.nodo = DIR_BROADCAST;
        ev.msgId = h.msgId;
        ev.intentos = sal_[i].intentos;
        ev.hops = h.hops;
        ev.rssiVuelta = rssiDbm;
        ev.snrVueltaQ4 = snrQ4;
        emitir(ev);
        liberar(sal_[i]);
      }
    } else {
      ++cnt_.idDuplicado;
      Evento ev = evento(TipoEvento::ID_DUPLICADO);
      ev.nodo = h.src;
      ev.msgId = h.msgId;
      ev.tipoTrama = h.tipo;
      ev.rssiVuelta = rssiDbm;
      ev.snrVueltaQ4 = snrQ4;
      emitir(ev);
      /* Se apunta para avisar UNA vez por mensaje, no por cada copia. */
      vistos_.anadir(h.src, h.msgId, ahoraMs, ventanaInundacion(static_cast<uint16_t>(len), h.ttl));
    }
    return;
  }

  /* ---- Regla 2: ya visto -------------------------------------------------
   * Una copia con hops == 0 viene DIRECTA del origen, y el origen transmite
   * cada intento una sola vez: es un REINTENTO seguro. Una copia reenviada
   * (hops >= 1) es de la misma inundación si llega dentro de la ventana
   * abierta con la primera copia; si llega después, es un reintento. */
  const int iv = vistos_.buscar(h.src, h.msgId, ahoraMs);
  if (iv >= 0) {
    ++cnt_.duplicados;
    const bool nuevoIntento = h.hops == 0 || vencido(ahoraMs, vistos_.entrada(iv).tFinVentana);
    if (!nuevoIntento) {
      suprimir(h.src, h.msgId);   // otro ya lo repitió: me callo (§8.2)
      return;
    }
    vistos_.anadir(h.src, h.msgId, ahoraMs, ventanaInundacion(static_cast<uint16_t>(len), h.ttl));
    /* Re-ACK SIN volver a entregar (§7.1): si no, un ACK perdido acabaría en
     * FALLO aunque el mensaje hubiera llegado. */
    if (r == Rechazo::NINGUNO && (h.flags & FLAG_ACK_REQ) && soyDestinoConAck(h) &&
        h.tipo != static_cast<uint8_t>(Tipo::ACK)) {
      encolarAck(h, rssiDbm, snrQ4, ahoraMs);
    }
    if (h.dst != cfg_.id) programarReenvio(trama, len, h, snrQ4, ahoraMs);
    return;
  }

  /* ---- Reglas 3 y 4 ------------------------------------------------------*/
  vistos_.anadir(h.src, h.msgId, ahoraMs, ventanaInundacion(static_cast<uint16_t>(len), h.ttl));
  if (h.hops == 0) vecinoDirecto(h.src, rssiDbm, snrQ4, ahoraMs);

  /* ---- Reglas 5 y 6: entregar -------------------------------------------*/
  const bool paraMi = h.dst == cfg_.id;
  const bool entregar = paraMi || h.dst == DIR_BROADCAST || (h.dst == DIR_PASARELA && cfg_.rol == Rol::PASARELA);
  if (r == Rechazo::NINGUNO && entregar) procesarLocal(h, carga, rssiDbm, snrQ4, ahoraMs);
  if (paraMi) return;   // el destino no reenvía

  /* ---- Regla 7: reenviar -------------------------------------------------*/
  programarReenvio(trama, len, h, snrQ4, ahoraMs);
}

void Motor::procesarLocal(const Cabecera& h, const uint8_t* carga, int16_t rssi, int8_t snrQ4, uint32_t ahoraMs) {
  switch (static_cast<Tipo>(h.tipo)) {
    case Tipo::DATA: {
      ++cnt_.entregados;
      Evento ev = evento(TipoEvento::ENTREGADO);
      ev.nodo = h.src;
      ev.msgId = h.msgId;
      ev.flags = h.flags;
      ev.hops = h.hops;
      ev.rssiVuelta = rssi;
      ev.snrVueltaQ4 = snrQ4;
      ev.len = h.len;
      if (h.len > 0) memcpy(ev.datos, carga, h.len);
      ev.datos[h.len] = '\0';
      emitir(ev);
      if ((h.flags & FLAG_ACK_REQ) && soyDestinoConAck(h)) encolarAck(h, rssi, snrQ4, ahoraMs);
      break;
    }
    case Tipo::ACK: {
      if (h.dst != cfg_.id) break;
      CargaAck a;
      if (!decodificarAck(carga, h.len, a)) { ++cnt_.rechazos[static_cast<int>(Rechazo::CARGA)]; break; }
      const int i = saliente(a.ackMsgId);
      bool casa = false;
      if (i >= 0) {
        Saliente& s = sal_[i];
        casa = (s.clase == Clase::DATA_UNI || s.clase == Clase::ALERTA) && s.intentos > 0 &&
               (s.dst == h.src || s.dst == DIR_PASARELA) && !(vueloOrigen_ == Origen::SALIENTE && vueloIdx_ == i);
        if (casa) {
          ++cnt_.acksRx;
          Evento ev = evento(TipoEvento::ACK);
          ev.nodo = h.src;
          ev.msgId = s.msgId;
          ev.tipoTrama = s.trama[1] & 0x0F;
          ev.intentos = s.intentos;
          ev.valor = ahoraMs - s.tInicioUltima;
          ev.hops = h.hops;
          ev.hopsIda = a.hopsRx;
          ev.hayIda = true;
          ev.rssiIda = rssiDeNeg(a.rssiNeg);
          ev.snrIdaQ4 = a.snrQ4;
          ev.rssiVuelta = rssi;
          ev.snrVueltaQ4 = snrQ4;
          emitir(ev);
          aprenderSaltos(h.src, a.hopsRx, ahoraMs);
          liberar(s);
        }
      }
      if (!casa) ++cnt_.acksHuerfanos;
      break;
    }
    case Tipo::PING: {
      uint32_t tMs = 0;
      if (!decodificarPing(carga, h.len, tMs)) { ++cnt_.rechazos[static_cast<int>(Rechazo::CARGA)]; break; }
      ++cnt_.pingsRx;
      CargaPong p;
      p.tEco = tMs;
      p.rssiNeg = rssiANeg(rssi);
      p.snrQ4 = snrQ4;
      p.hopsRx = h.hops;
      uint8_t c[LEN_PONG];
      codificarPong(p, c);
      /* PING * : respuesta escalonada U(0, 2 s) para que no choquen todas. */
      const uint32_t retardo = h.dst == DIR_BROADCAST ? uniforme(PONG_BCAST_MAX_MS) : GIRO_MS;
      encolarRespuesta(h.src, Tipo::PONG, c, LEN_PONG, retardo, ahoraMs);
      break;
    }
    case Tipo::PONG: {
      if (h.dst != cfg_.id) break;
      CargaPong p;
      if (!decodificarPong(carga, h.len, p)) { ++cnt_.rechazos[static_cast<int>(Rechazo::CARGA)]; break; }
      bool casa = false;
      for (int i = 0; i < N_SALIENTES; ++i) {
        Saliente& s = sal_[i];
        if (s.estado != EstadoS::ESPERA || s.tPing != p.tEco) continue;
        if (!(s.clase == Clase::PING_BCAST || (s.clase == Clase::PING_UNI && s.dst == h.src))) continue;
        casa = true;
        ++cnt_.pongsRx;
        Evento ev = evento(TipoEvento::PONG);
        ev.nodo = h.src;
        ev.msgId = s.msgId;
        ev.valor = ahoraMs - p.tEco;
        ev.hops = h.hops;
        ev.hopsIda = p.hopsRx;
        ev.hayIda = true;
        ev.rssiIda = rssiDeNeg(p.rssiNeg);
        ev.snrIdaQ4 = p.snrQ4;
        ev.rssiVuelta = rssi;
        ev.snrVueltaQ4 = snrQ4;
        emitir(ev);
        aprenderSaltos(h.src, p.hopsRx, ahoraMs);
        if (s.clase == Clase::PING_UNI) liberar(s);
        else ++s.pongs;
        break;
      }
      if (!casa) ++cnt_.acksHuerfanos;
      break;
    }
    case Tipo::ALERT: {
      CargaAlerta a;
      if (!decodificarAlerta(carga, h.len, a)) { ++cnt_.rechazos[static_cast<int>(Rechazo::CARGA)]; break; }
      ++cnt_.alertasRx;
      Evento ev = evento(TipoEvento::ALERTA);
      ev.nodo = h.src;
      ev.msgId = h.msgId;
      ev.flags = h.flags;
      ev.hops = h.hops;
      ev.rssiVuelta = rssi;
      ev.snrVueltaQ4 = snrQ4;
      ev.alerta = a;
      emitir(ev);
      if ((h.flags & FLAG_ACK_REQ) && soyDestinoConAck(h)) encolarAck(h, rssi, snrQ4, ahoraMs);
      break;
    }
    default:
      break;
  }
}

/* ============================================================== tick ====*/
void Motor::tick(uint32_t ahoraMs) {
  for (int i = 0; i < N_SALIENTES; ++i) {
    Saliente& s = sal_[i];
    if (s.estado != EstadoS::ESPERA) continue;
    switch (s.clase) {
      case Clase::DATA_UNI:
      case Clase::ALERTA: {
        const bool alerta = s.clase == Clase::ALERTA;
        const bool agotado = alerta ? vencido(ahoraMs, s.tPrimera + ALERTA_MAX_MS)
                                    : (s.intentos >= DATA_INTENTOS && vencido(ahoraMs, s.tLimite));
        if (agotado) {
          ++cnt_.fallos;
          Evento ev = evento(TipoEvento::FALLO);
          ev.nodo = s.dst;
          ev.msgId = s.msgId;
          ev.tipoTrama = s.trama[1] & 0x0F;
          ev.intentos = s.intentos;
          ev.valor = ahoraMs - s.tPrimera;
          emitir(ev);
          liberar(s);
          break;
        }
        if ((alerta || s.intentos < DATA_INTENTOS) && vencido(ahoraMs, s.tProximo)) {
          if (alerta && s.intentos >= ALERTA_RAPIDOS) {
            Evento ev = evento(TipoEvento::ALERTA_SIN_ACK);   // nunca en silencio
            ev.nodo = s.dst;
            ev.msgId = s.msgId;
            ev.intentos = s.intentos;
            ev.n = (ahoraMs - s.tPrimera) / 1000u;
            ev.alerta.codigo = s.codigoAlerta;
            ev.alerta.seq = s.seqAlerta;
            emitir(ev);
          }
          ++cnt_.reintentos;
          Evento ev = evento(TipoEvento::REINTENTO);
          ev.nodo = s.dst;
          ev.msgId = s.msgId;
          ev.tipoTrama = s.trama[1] & 0x0F;
          ev.intentos = s.intentos;
          ev.valor = ahoraMs - s.tInicioUltima;   // desde el inicio de la TX anterior
          emitir(ev);
          s.estado = EstadoS::COLA;
          s.tNoAntes = ahoraMs;
        }
        break;
      }
      case Clase::PING_UNI:
        if (vencido(ahoraMs, s.tLimite)) {
          Evento ev = evento(TipoEvento::PING_PERDIDO);
          ev.nodo = s.dst;
          ev.msgId = s.msgId;
          emitir(ev);
          liberar(s);
        }
        break;
      case Clase::PING_BCAST:
        if (vencido(ahoraMs, s.tLimite)) {
          Evento ev = evento(TipoEvento::PING_FIN);
          ev.nodo = DIR_BROADCAST;
          ev.msgId = s.msgId;
          ev.n = s.pongs;
          emitir(ev);
          liberar(s);
        }
        break;
      case Clase::DATA_BCAST:
        if (vencido(ahoraMs, s.tLimite)) {
          if (s.intentos < BCAST_INTENTOS) {
            ++cnt_.reintentos;
            Evento ev = evento(TipoEvento::REINTENTO);
            ev.nodo = DIR_BROADCAST;
            ev.msgId = s.msgId;
            ev.tipoTrama = s.trama[1] & 0x0F;
            ev.intentos = s.intentos;
            ev.valor = ahoraMs - s.tInicioUltima;
            emitir(ev);
            s.estado = EstadoS::COLA;
            s.tNoAntes = ahoraMs;
          } else {
            Evento ev = evento(TipoEvento::BCAST_SIN_ECO);
            ev.nodo = DIR_BROADCAST;
            ev.msgId = s.msgId;
            ev.intentos = s.intentos;
            emitir(ev);
            liberar(s);
          }
        }
        break;
    }
  }
  /* Reenvíos que llevan demasiado esperando (presupuesto agotado, canal
   * ocupado sin fin): ya no ayudan a nadie. */
  for (int i = 0; i < N_REENVIOS; ++i) {
    Pendiente& p = reenv_[i];
    if (!p.usado || (vueloOrigen_ == Origen::REENVIO && vueloIdx_ == i)) continue;
    if (ahoraMs - p.tAlta >= VIDA_REENVIO_MS) {
      p.usado = false;
      ++cnt_.descartesCola;
      Evento ev = evento(TipoEvento::DESCARTE_COLA);
      ev.nodo = p.src;
      ev.msgId = p.msgId;
      ev.tipoTrama = p.trama[1] & 0x0F;
      emitir(ev);
    }
  }
}

/* ============================================================= radio ====*/
bool Motor::siguienteTx(uint32_t ahoraMs, TramaSalida& out) {
  if (vueloOrigen_ != Origen::NINGUNO) return false;   // falta cerrar la anterior
  Origen mejorO = Origen::NINGUNO;
  int mejorI = -1;
  int mejorPrio = 99;
  uint32_t mejorT = 0;

  /* Prioridad: 0 ACK · 1 ALERT (propia o reenviada) · 2 resto. A igual
   * prioridad, la que lleva más esperando. El tráfico no exento sólo sale si
   * cabe en el presupuesto del 10 % (§7.6). */
  struct Local {
    static void considerar(Origen o, int i, int prio, uint32_t t, Origen& mo, int& mi, int& mp, uint32_t& mt) {
      if (prio < mp || (prio == mp && static_cast<int32_t>(t - mt) < 0)) { mo = o; mi = i; mp = prio; mt = t; }
    }
  };
  for (int i = 0; i < N_SALIENTES; ++i) {
    const Saliente& s = sal_[i];
    if (s.estado != EstadoS::COLA || !vencido(ahoraMs, s.tNoAntes)) continue;
    const bool exento = s.clase == Clase::ALERTA;
    if (!exento && !ocup_.permite(ahoraMs, airtime(s.len))) continue;
    Local::considerar(Origen::SALIENTE, i, exento ? 1 : 2, s.tNoAntes, mejorO, mejorI, mejorPrio, mejorT);
  }
  for (int i = 0; i < N_RESPUESTAS; ++i) {
    const Pendiente& p = resp_[i];
    if (!p.usado || !vencido(ahoraMs, p.tNoAntes)) continue;
    if (!p.exento && !ocup_.permite(ahoraMs, airtime(p.len))) continue;
    Local::considerar(Origen::RESPUESTA, i, p.exento ? 0 : 2, p.tNoAntes, mejorO, mejorI, mejorPrio, mejorT);
  }
  for (int i = 0; i < N_REENVIOS; ++i) {
    const Pendiente& p = reenv_[i];
    if (!p.usado || !vencido(ahoraMs, p.tNoAntes)) continue;
    if (!p.exento && !ocup_.permite(ahoraMs, airtime(p.len))) continue;
    Local::considerar(Origen::REENVIO, i, p.exento ? 1 : 2, p.tNoAntes, mejorO, mejorI, mejorPrio, mejorT);
  }
  if (mejorO == Origen::NINGUNO) return false;

  uint8_t cad = 0;
  bool esAlerta = false;
  if (mejorO == Origen::SALIENTE) {
    Saliente& s = sal_[mejorI];
    if (s.clase == Clase::PING_UNI || s.clase == Clase::PING_BCAST) {
      /* t_ms = instante real de salida: el RTT no incluye la espera en cola. */
      const size_t lc = static_cast<size_t>(s.len) - SOBRECARGA;
      escribirU32(s.trama + CABECERA, ahoraMs);
      escribirU16(s.trama + CABECERA + lc, crc16(s.trama, CABECERA + lc));
      s.tPing = ahoraMs;
    }
    memcpy(out.datos, s.trama, s.len);
    out.len = s.len;
    cad = s.cadOcupado;
    esAlerta = s.clase == Clase::ALERTA;
  } else {
    Pendiente& p = (mejorO == Origen::RESPUESTA) ? resp_[mejorI] : reenv_[mejorI];
    memcpy(out.datos, p.trama, p.len);
    out.len = p.len;
    cad = p.cadOcupado;
    esAlerta = (p.trama[1] & 0x0F) == static_cast<uint8_t>(Tipo::ALERT);
  }
  out.usarCad = !(esAlerta && cad >= CAD_MAX);
  out.airtimeMs = airtime(out.len);
  vueloOrigen_ = mejorO;
  vueloIdx_ = mejorI;
  return true;
}

void Motor::txHecha(uint32_t ahoraMs, uint32_t airtimeMs) {
  if (vueloOrigen_ == Origen::NINGUNO) return;
  ocup_.anotar(ahoraMs, airtimeMs);
  ++cnt_.tx;
  const Origen o = vueloOrigen_;
  const int i = vueloIdx_;
  vueloOrigen_ = Origen::NINGUNO;
  vueloIdx_ = -1;
  if (o == Origen::SALIENTE) {
    Saliente& s = sal_[i];
    ++s.intentos;
    s.cadOcupado = 0;
    s.tInicioUltima = ahoraMs - airtimeMs;
    if (s.intentos == 1) s.tPrimera = s.tInicioUltima;
    if (s.clase == Clase::DATA_BCAST && s.trama[9] == 0) {
      liberar(s);   // E1: un broadcast con ttl 0 no tiene confirmación posible
      return;
    }
    programarSiguiente(s, ahoraMs);
  } else if (o == Origen::RESPUESTA) {
    if ((resp_[i].trama[1] & 0x0F) == static_cast<uint8_t>(Tipo::ACK)) ++cnt_.acksTx;
    resp_[i].usado = false;
  } else {
    ++cnt_.reenviados;
    reenv_[i].usado = false;
  }
}

void Motor::canalOcupado(uint32_t ahoraMs) {
  if (vueloOrigen_ == Origen::NINGUNO) return;
  ++cnt_.cadOcupado;
  /* §7.5: esperar U(1,4)·T_aire(PING) y volver a escuchar. */
  const uint32_t tPing = airtime(SOBRECARGA + LEN_PING_MIN);
  const uint32_t retardo = tPing + uniforme(3u * tPing);
  uint8_t* cad = nullptr;
  uint32_t* tNoAntes = nullptr;
  bool esAlerta = false;
  if (vueloOrigen_ == Origen::SALIENTE) {
    Saliente& s = sal_[vueloIdx_];
    cad = &s.cadOcupado;
    tNoAntes = &s.tNoAntes;
    esAlerta = s.clase == Clase::ALERTA;
  } else {
    Pendiente& p = (vueloOrigen_ == Origen::RESPUESTA) ? resp_[vueloIdx_] : reenv_[vueloIdx_];
    cad = &p.cadOcupado;
    tNoAntes = &p.tNoAntes;
    esAlerta = (p.trama[1] & 0x0F) == static_cast<uint8_t>(Tipo::ALERT);
  }
  if (*cad < 255) ++*cad;
  /* Tras CAD_MAX: la ALERT sale sin escuchar (usarCad = false en el próximo
   * siguienteTx); el resto vuelve a la cola y empieza de nuevo. */
  if (!esAlerta && *cad >= CAD_MAX) *cad = 0;
  *tNoAntes = ahoraMs + retardo;
  vueloOrigen_ = Origen::NINGUNO;
  vueloIdx_ = -1;
}

void Motor::txFallida(uint32_t ahoraMs) {
  if (vueloOrigen_ == Origen::NINGUNO) return;
  ++cnt_.txErrores;
  const uint32_t retardo = airtime(SOBRECARGA + LEN_PING_MIN);
  if (vueloOrigen_ == Origen::SALIENTE) {
    sal_[vueloIdx_].tNoAntes = ahoraMs + retardo;
  } else {
    Pendiente& p = (vueloOrigen_ == Origen::RESPUESTA) ? resp_[vueloIdx_] : reenv_[vueloIdx_];
    p.tNoAntes = ahoraMs + retardo;
  }
  vueloOrigen_ = Origen::NINGUNO;
  vueloIdx_ = -1;
}

}  // namespace geolora
