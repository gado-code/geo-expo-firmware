/* ============================================================================
 *  GEO-EXPO LoRa  ·  Prueba de alcance RANGE  (ver rango.h)
 * ==========================================================================*/
#include "rango.h"

#include <string.h>

namespace geolora {

Rango::Rango()
    : activo_(false), dst_(0), nPedidos_(0), intervaloMs_(0), tProximo_(0), punto_(1), seq_(0), enviados_(0),
      recibidos_(0), ini_(0), n_(0), hayUltima_(false) {
  memset(curso_, 0, sizeof(curso_));
  memset(m_, 0, sizeof(m_));
  memset(&ultima_, 0, sizeof(ultima_));
}

void Rango::iniciar(uint16_t dst, uint32_t n, uint32_t intervaloMs, uint32_t ahoraMs) {
  activo_ = true;
  dst_ = dst;
  nPedidos_ = n;
  intervaloMs_ = intervaloMs;
  tProximo_ = ahoraMs;
  seq_ = 0;
  enviados_ = 0;
  recibidos_ = 0;
  hayUltima_ = false;
  memset(curso_, 0, sizeof(curso_));
  /* El número de punto NO se reinicia: si se para y se vuelve a lanzar en el
   * mismo recorrido, los puntos siguen correlativos en el informe. */
}

bool Rango::tocaPing(uint32_t ahoraMs) {
  if (!activo_) return false;
  if (nPedidos_ != 0 && enviados_ >= nPedidos_) return false;
  return static_cast<int32_t>(ahoraMs - tProximo_) >= 0;
}

void Rango::pingEnviado(uint16_t msgId, uint32_t ahoraMs) {
  ++seq_;
  ++enviados_;
  tProximo_ = ahoraMs + intervaloMs_;
  /* Hueco para seguirlo; si los 4 están ocupados (intervalo menor que el
   * timeout del PING) se pisa el más viejo, que ya no llegará a tiempo. */
  int idx = -1;
  for (int i = 0; i < N_EN_CURSO; ++i) if (!curso_[i].usado) { idx = i; break; }
  if (idx < 0) {
    idx = 0;
    for (int i = 1; i < N_EN_CURSO; ++i) {
      if (static_cast<int16_t>(curso_[i].seq - curso_[idx].seq) < 0) idx = i;
    }
  }
  curso_[idx].usado = true;
  curso_[idx].msgId = msgId;
  curso_[idx].seq = seq_;
  curso_[idx].punto = punto_;
}

int Rango::buscarEnCurso(uint16_t msgId) const {
  for (int i = 0; i < N_EN_CURSO; ++i) if (curso_[i].usado && curso_[i].msgId == msgId) return i;
  return -1;
}

void Rango::registrar(const Medida& m) {
  if (n_ < CAPACIDAD) {
    m_[(ini_ + n_) % CAPACIDAD] = m;
    ++n_;
  } else {
    m_[ini_] = m;                       // anillo lleno: se pisa la más antigua
    ini_ = (ini_ + 1) % CAPACIDAD;
  }
  ultima_ = m;
  hayUltima_ = true;
}

void Rango::comprobarFin() {
  if (!activo_ || nPedidos_ == 0 || enviados_ < nPedidos_) return;
  for (int i = 0; i < N_EN_CURSO; ++i) if (curso_[i].usado) return;
  activo_ = false;
}

bool Rango::alPong(uint16_t msgIdPing, uint32_t rttMs, int16_t rssiIda, int8_t snrIdaQ4, int16_t rssiVuelta,
                   int8_t snrVueltaQ4, uint8_t hops, Medida& out) {
  const int i = buscarEnCurso(msgIdPing);
  if (i < 0) return false;
  Medida m;
  memset(&m, 0, sizeof(m));
  m.punto = curso_[i].punto;
  m.seq = curso_[i].seq;
  m.ok = true;
  m.rttMs = rttMs;
  m.rssiIda = rssiIda;
  m.snrIdaQ4 = snrIdaQ4;
  m.rssiVuelta = rssiVuelta;
  m.snrVueltaQ4 = snrVueltaQ4;
  m.hops = hops;
  curso_[i].usado = false;
  ++recibidos_;
  registrar(m);
  out = m;
  comprobarFin();
  return true;
}

bool Rango::alPerdido(uint16_t msgIdPing, Medida& out) {
  const int i = buscarEnCurso(msgIdPing);
  if (i < 0) return false;
  Medida m;
  memset(&m, 0, sizeof(m));
  m.punto = curso_[i].punto;
  m.seq = curso_[i].seq;
  m.ok = false;
  curso_[i].usado = false;
  registrar(m);
  out = m;
  comprobarFin();
  return true;
}

namespace {
struct Acum {
  int32_t mn, mx;
  int64_t suma;
  uint32_t n;
  void reset() { mn = 0; mx = 0; suma = 0; n = 0; }
  void add(int32_t v) {
    if (n == 0 || v < mn) mn = v;
    if (n == 0 || v > mx) mx = v;
    suma += v;
    ++n;
  }
  EstadisticaQ est() const {
    EstadisticaQ e;
    e.min = mn;
    e.max = mx;
    if (n == 0) { e.media = 0; return e; }
    /* media redondeada a la mitad lejos de cero */
    const int64_t num = suma >= 0 ? suma + static_cast<int64_t>(n) / 2 : suma - static_cast<int64_t>(n) / 2;
    e.media = static_cast<int32_t>(num / static_cast<int64_t>(n));
    return e;
  }
};
}  // namespace

size_t Rango::resumir(ResumenPunto* out, size_t max) const {
  size_t nPuntos = 0;
  for (size_t i = 0; i < n_ && nPuntos < max; ++i) {
    const uint16_t p = medida(i).punto;
    bool ya = false;
    for (size_t k = 0; k < nPuntos; ++k) if (out[k].punto == p) { ya = true; break; }
    if (ya) continue;
    Acum ri, si, rv, sv;
    ri.reset(); si.reset(); rv.reset(); sv.reset();
    uint64_t rtt = 0;
    uint32_t env = 0, rec = 0;
    for (size_t j = i; j < n_; ++j) {
      const Medida& m = medida(j);
      if (m.punto != p) continue;
      ++env;
      if (!m.ok) continue;
      ++rec;
      ri.add(m.rssiIda);
      si.add(m.snrIdaQ4);
      rv.add(m.rssiVuelta);
      sv.add(m.snrVueltaQ4);
      rtt += m.rttMs;
    }
    ResumenPunto& r = out[nPuntos++];
    memset(&r, 0, sizeof(r));
    r.punto = p;
    r.enviados = static_cast<uint16_t>(env);
    r.recibidos = static_cast<uint16_t>(rec);
    r.perdidasPct = static_cast<uint8_t>(env == 0 ? 0 : ((env - rec) * 100u + env / 2u) / env);
    r.rssiIda = ri.est();
    r.snrIdaQ4 = si.est();
    r.rssiVuelta = rv.est();
    r.snrVueltaQ4 = sv.est();
    r.rttMedioMs = rec == 0 ? 0 : static_cast<uint32_t>((rtt + rec / 2u) / rec);
  }
  return nPuntos;
}

}  // namespace geolora
