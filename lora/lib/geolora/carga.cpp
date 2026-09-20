/* ============================================================================
 *  GEO-EXPO LoRa  ·  Cargas de PING / PONG / ACK / ALERT  (ver carga.h)
 * ==========================================================================*/
#include "carga.h"

#include <string.h>

#include "paquete.h"

namespace geolora {

/* ---------------------------------------------------------------- ALERT --*/
const char* tokenAlerta(uint8_t codigo) {
  switch (codigo) {
    case ALERTA_1:      return "ALERT:1";
    case ALERTA_2:      return "ALERT:2";
    case ALERTA_CANCEL: return "CANCEL";
    default:            return nullptr;
  }
}

uint8_t codigoDeToken(const char* token) {
  if (token == nullptr) return 0;
  for (uint8_t c = ALERTA_1; c <= ALERTA_CANCEL; ++c) {
    if (strcmp(token, tokenAlerta(c)) == 0) return c;
  }
  return 0;
}

static bool posicionValida(int32_t lat, int32_t lon) {
  return lat >= -900000000 && lat <= 900000000 && lon >= -1800000000 && lon <= 1800000000;
}

uint8_t codificarAlerta(const CargaAlerta& a, uint8_t* out) {
  if (tokenAlerta(a.codigo) == nullptr || out == nullptr) return 0;
  out[0] = a.codigo;
  out[1] = a.seq;
  out[2] = a.aflags;
  if (!a.conPosicion()) return LEN_ALERTA_SIN_POS;
  if (!posicionValida(a.lat1e7, a.lon1e7)) return 0;
  escribirU32(out + 3, static_cast<uint32_t>(a.lat1e7));
  escribirU32(out + 7, static_cast<uint32_t>(a.lon1e7));
  return LEN_ALERTA_CON_POS;
}

bool decodificarAlerta(const uint8_t* p, uint8_t len, CargaAlerta& a) {
  if (p == nullptr || len < LEN_ALERTA_SIN_POS) return false;
  if (tokenAlerta(p[0]) == nullptr) return false;
  CargaAlerta r;
  r.codigo = p[0];
  r.seq    = p[1];
  r.aflags = p[2];
  if (r.conPosicion()) {
    if (len < LEN_ALERTA_CON_POS) return false;
    r.lat1e7 = static_cast<int32_t>(leerU32(p + 3));
    r.lon1e7 = static_cast<int32_t>(leerU32(p + 7));
    if (!posicionValida(r.lat1e7, r.lon1e7)) return false;
  }
  a = r;
  return true;
}

/* Escribe `s` en out+*pos respetando `cap` (deja sitio para el '\0'). */
static bool anadir(char* out, size_t cap, size_t* pos, const char* s) {
  const size_t n = strlen(s);
  if (*pos + n + 1 > cap) return false;
  memcpy(out + *pos, s, n);
  *pos += n;
  out[*pos] = '\0';
  return true;
}

size_t formatearGrados(int32_t v1e7, uint8_t decimales, char* out, size_t cap) {
  if (out == nullptr || cap == 0 || decimales > 7) return 0;
  /* Se trabaja con el valor absoluto en 64 bits (INT32_MIN no tiene opuesto
   * en 32) y se redondea a `decimales` a la mitad lejos de cero. */
  const bool neg = v1e7 < 0;
  uint64_t a = neg ? static_cast<uint64_t>(-static_cast<int64_t>(v1e7)) : static_cast<uint64_t>(v1e7);
  uint64_t div = 1;
  for (uint8_t i = decimales; i < 7; ++i) div *= 10;
  a = (a + div / 2) / div;                 // ahora en unidades de 10^-decimales
  uint64_t escala = 1;
  for (uint8_t i = 0; i < decimales; ++i) escala *= 10;
  const uint64_t entero = a / escala;
  const uint64_t frac   = a % escala;

  char tmp[32];
  size_t n = 0;
  if (neg && a != 0) tmp[n++] = '-';
  char dig[24];
  int nd = 0;
  uint64_t e = entero;
  do { dig[nd++] = static_cast<char>('0' + (e % 10)); e /= 10; } while (e != 0);
  while (nd > 0) tmp[n++] = dig[--nd];
  if (decimales > 0) {
    tmp[n++] = '.';
    uint64_t f = frac;
    char fd[8];
    for (int i = decimales - 1; i >= 0; --i) { fd[i] = static_cast<char>('0' + (f % 10)); f /= 10; }
    for (int i = 0; i < decimales; ++i) tmp[n++] = fd[i];
  }
  tmp[n] = '\0';
  if (n + 1 > cap) return 0;
  memcpy(out, tmp, n + 1);
  return n;
}

size_t lineaPasarela(const CargaAlerta& a, bool conPosicion, char* out, size_t cap) {
  const char* tok = tokenAlerta(a.codigo);
  if (tok == nullptr || out == nullptr || cap == 0) return 0;
  size_t pos = 0;
  out[0] = '\0';
  if (!anadir(out, cap, &pos, tok)) return 0;
  if (conPosicion && a.conPosicion()) {
    char g[24];
    if (!anadir(out, cap, &pos, ";")) return 0;
    formatearGrados(a.lat1e7, 6, g, sizeof(g));
    if (!anadir(out, cap, &pos, g)) return 0;
    if (!anadir(out, cap, &pos, ";")) return 0;
    formatearGrados(a.lon1e7, 6, g, sizeof(g));
    if (!anadir(out, cap, &pos, g)) return 0;
  }
  return pos;
}

/* ------------------------------------------------------ ACK/PING/PONG --*/
uint8_t codificarAck(const CargaAck& a, uint8_t* out) {
  escribirU16(out, a.ackMsgId);
  out[2] = a.rssiNeg;
  out[3] = static_cast<uint8_t>(a.snrQ4);
  out[4] = a.hopsRx;
  return LEN_ACK;
}

bool decodificarAck(const uint8_t* p, uint8_t len, CargaAck& a) {
  if (p == nullptr || len != LEN_ACK) return false;
  a.ackMsgId = leerU16(p);
  a.rssiNeg  = p[2];
  a.snrQ4    = static_cast<int8_t>(p[3]);
  a.hopsRx   = p[4];
  return true;
}

uint8_t codificarPong(const CargaPong& a, uint8_t* out) {
  escribirU32(out, a.tEco);
  out[4] = a.rssiNeg;
  out[5] = static_cast<uint8_t>(a.snrQ4);
  out[6] = a.hopsRx;
  return LEN_PONG;
}

bool decodificarPong(const uint8_t* p, uint8_t len, CargaPong& a) {
  if (p == nullptr || len != LEN_PONG) return false;
  a.tEco    = leerU32(p);
  a.rssiNeg = p[4];
  a.snrQ4   = static_cast<int8_t>(p[5]);
  a.hopsRx  = p[6];
  return true;
}

uint8_t codificarPing(uint32_t tMs, uint8_t relleno, uint8_t* out) {
  if (static_cast<unsigned>(LEN_PING_MIN) + relleno > LEN_MAX) return 0;
  escribirU32(out, tMs);
  memset(out + LEN_PING_MIN, 0, relleno);
  return static_cast<uint8_t>(LEN_PING_MIN + relleno);
}

bool decodificarPing(const uint8_t* p, uint8_t len, uint32_t& tMs) {
  if (p == nullptr || len < LEN_PING_MIN) return false;
  tMs = leerU32(p);
  return true;
}

/* ------------------------------------------------------- saturaciones --*/
uint8_t rssiANeg(int16_t rssiDbm) {
  if (rssiDbm >= 0) return 0;
  if (rssiDbm <= -255) return 255;
  return static_cast<uint8_t>(-rssiDbm);
}

int8_t saturarQ4(int32_t q4) {
  if (q4 > 127) return 127;
  if (q4 < -128) return -128;
  return static_cast<int8_t>(q4);
}

int8_t snrAQ4(float snrDb) {
  /* Redondeo al cuarto más cercano (mitad lejos de cero) sin depender de
   * lroundf, y saturación ANTES de convertir para no desbordar. */
  const float x = snrDb * 4.0f;
  if (x >= 127.0f) return 127;
  if (x <= -128.0f) return -128;
  const int32_t r = static_cast<int32_t>(x >= 0 ? x + 0.5f : x - 0.5f);
  return saturarQ4(r);
}

}  // namespace geolora
