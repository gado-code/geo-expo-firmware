/* ============================================================================
 *  GEO-EXPO LoRa  ·  Parser de la consola serie  (ver consola.h)
 * ==========================================================================*/
#include "consola.h"

#include <string.h>

#include "carga.h"
#include "identidad.h"

namespace geolora {

namespace {

const int MAX_TOK = 10;

/* Línea partida en palabras, recordando dónde empieza cada una en la línea
 * ORIGINAL (para sacar el texto de SEND/BCAST tal cual, con sus espacios). */
struct Tokens {
  char   orig[CONSOLA_LINEA_MAX + 1];
  char   buf[CONSOLA_LINEA_MAX + 1];
  char*  tok[MAX_TOK];
  size_t ini[MAX_TOK];
  int    n;
};

bool esBlanco(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

char mayus(char c) { return (c >= 'a' && c <= 'z') ? static_cast<char>(c - 'a' + 'A') : c; }

bool igual(const char* a, const char* b) {
  while (*a != '\0' && *b != '\0') {
    if (mayus(*a) != mayus(*b)) return false;
    ++a; ++b;
  }
  return *a == '\0' && *b == '\0';
}

/* false si la línea es demasiado larga. */
bool trocear(const char* linea, Tokens& t) {
  const size_t len = strlen(linea);
  /* Fin de línea y blancos del final fuera. */
  size_t fin = len;
  while (fin > 0 && esBlanco(linea[fin - 1])) --fin;
  if (fin > CONSOLA_LINEA_MAX) return false;
  memcpy(t.orig, linea, fin);
  t.orig[fin] = '\0';
  memcpy(t.buf, t.orig, fin + 1);
  t.n = 0;
  size_t i = 0;
  while (i < fin) {
    while (i < fin && esBlanco(t.buf[i])) t.buf[i++] = '\0';
    if (i >= fin) break;
    if (t.n < MAX_TOK) {
      t.tok[t.n] = &t.buf[i];
      t.ini[t.n] = i;
    }
    ++t.n;
    while (i < fin && !esBlanco(t.buf[i])) ++i;
  }
  return true;
}

bool error(Orden& o, const char* motivo) {
  o.cmd = Cmd::ERROR;
  o.error = motivo;
  return false;
}

bool ok(Orden& o, Cmd c) {
  o.cmd = c;
  o.error = nullptr;
  return true;
}

/* Entero en [min, max] o error con el motivo adecuado. */
bool entero(const char* s, int32_t mn, int32_t mx, int32_t& v, Orden& o) {
  if (!parseEntero(s, v)) return error(o, "NUMERO_INVALIDO");
  if (v < mn || v > mx) return error(o, "FUERA_DE_RANGO");
  return true;
}

/* ID de destino: hex de 1–4 cifras y no reservado. */
bool destino(const char* s, uint16_t& id, Orden& o) {
  if (!parseHex4(s, id)) return error(o, "ID_INVALIDO");
  if (idReservado(id)) return error(o, "ID_RESERVADO");
  return true;
}

/* Texto libre desde la palabra `k` hasta el final de la línea original. */
bool resto(const Tokens& t, int k, Orden& o) {
  if (t.n <= k) return error(o, "TEXTO_VACIO");
  const char* s = t.orig + t.ini[k];
  const size_t n = strlen(s);
  if (n > LEN_MAX) return error(o, "TEXTO_LARGO");
  memcpy(o.texto, s, n + 1);
  o.lenTexto = static_cast<uint8_t>(n);
  return true;
}

bool parsearRadio(const Tokens& t, Orden& o) {
  if (t.n == 1) return ok(o, Cmd::RADIO_VER);
  const char* sub = t.tok[1];
  if (igual(sub, "SAVE") || igual(sub, "LOAD") || igual(sub, "DEFECTO")) {
    if (t.n > 2) return error(o, "SOBRAN_ARGUMENTOS");
    return ok(o, igual(sub, "SAVE") ? Cmd::RADIO_SAVE : igual(sub, "LOAD") ? Cmd::RADIO_LOAD : Cmd::RADIO_DEFECTO);
  }
  if (!igual(sub, "FREQ") && !igual(sub, "BW") && !igual(sub, "CR") && !igual(sub, "SYNC") &&
      !igual(sub, "PRE") && !igual(sub, "SF") && !igual(sub, "PWR") && !igual(sub, "PRESET")) {
    return error(o, "ORDEN_DESCONOCIDA");
  }
  if (t.n < 3) return error(o, "FALTAN_ARGUMENTOS");
  if (t.n > 3) return error(o, "SOBRAN_ARGUMENTOS");
  const char* v = t.tok[2];
  if (igual(sub, "FREQ")) {
    if (!parseMHz(v, o.freqKHz)) return error(o, "FREQ_INVALIDA");
    return ok(o, Cmd::RADIO_FREQ);
  }
  if (igual(sub, "BW")) {
    if (!entero(v, 0, 1000, o.valor, o)) return false;
    if (!bwValido(static_cast<uint16_t>(o.valor))) return error(o, "FUERA_DE_RANGO");
    return ok(o, Cmd::RADIO_BW);
  }
  if (igual(sub, "CR")) return entero(v, 5, 8, o.valor, o) && ok(o, Cmd::RADIO_CR);
  if (igual(sub, "PRE")) return entero(v, PRE_MIN, PRE_MAX, o.valor, o) && ok(o, Cmd::RADIO_PRE);
  if (igual(sub, "SF")) return entero(v, 7, 12, o.valor, o) && ok(o, Cmd::RADIO_SF);
  if (igual(sub, "PWR")) return entero(v, PWR_MIN, PWR_MAX, o.valor, o) && ok(o, Cmd::RADIO_PWR);
  if (igual(sub, "SYNC")) {
    uint32_t s = 0;
    if (!parseHex(v, 2, s)) return error(o, "NUMERO_INVALIDO");
    if (s == 0) return error(o, "FUERA_DE_RANGO");
    o.valor = static_cast<int32_t>(s);
    return ok(o, Cmd::RADIO_SYNC);
  }
  /* PRESET */
  if (!presetPorNombre(v, o.preset)) return error(o, "PRESET_DESCONOCIDO");
  return ok(o, Cmd::RADIO_PRESET);
}

bool parsearAlerta(const Tokens& t, Orden& o) {
  if (t.n < 2) return error(o, "FALTAN_ARGUMENTOS");
  const char* c = t.tok[1];
  if (igual(c, "1")) o.codigo = ALERTA_1;
  else if (igual(c, "2")) o.codigo = ALERTA_2;
  else if (igual(c, "C") || igual(c, "CANCEL")) o.codigo = ALERTA_CANCEL;
  else return error(o, "CODIGO_ALERTA");
  int resto = t.n - 2;
  if (resto > 0 && igual(t.tok[t.n - 1], "PRUEBA")) { o.prueba = true; --resto; }
  if (resto == 0) return ok(o, Cmd::ALERT);
  if (resto == 1) return error(o, "FALTAN_ARGUMENTOS");
  if (resto > 2) return error(o, "SOBRAN_ARGUMENTOS");
  if (!parseGrados(t.tok[2], 90, o.lat1e7) || !parseGrados(t.tok[3], 180, o.lon1e7)) {
    return error(o, "POSICION_INVALIDA");
  }
  o.conPos = true;
  return ok(o, Cmd::ALERT);
}

bool parsearRange(const Tokens& t, Orden& o) {
  if (t.n < 2) return error(o, "FALTAN_ARGUMENTOS");
  if (igual(t.tok[1], "STOP") || igual(t.tok[1], "INFORME")) {
    if (t.n > 2) return error(o, "SOBRAN_ARGUMENTOS");
    return ok(o, igual(t.tok[1], "STOP") ? Cmd::RANGE_STOP : Cmd::RANGE_INFORME);
  }
  if (t.n > 4) return error(o, "SOBRAN_ARGUMENTOS");
  if (!destino(t.tok[1], o.dst, o)) return false;
  int32_t v = 0;
  if (t.n >= 3) {
    if (!entero(t.tok[2], 0, 1000000, v, o)) return false;
    o.n = static_cast<uint32_t>(v);
  }
  if (t.n >= 4) {
    if (!entero(t.tok[3], 0, 3600000, v, o)) return false;
    o.ms = static_cast<uint32_t>(v);
  }
  return ok(o, Cmd::RANGE);
}

}  // namespace

/* ======================================================================== */
bool parseEntero(const char* s, int32_t& v) {
  if (s == nullptr || *s == '\0') return false;
  bool neg = false;
  if (*s == '-' || *s == '+') { neg = (*s == '-'); ++s; }
  if (*s == '\0') return false;
  int64_t acc = 0;
  int nd = 0;
  for (; *s != '\0'; ++s) {
    if (*s < '0' || *s > '9') return false;
    if (++nd > 10) return false;
    acc = acc * 10 + (*s - '0');
  }
  if (neg) acc = -acc;
  if (acc > 2147483647LL || acc < -2147483648LL) return false;
  v = static_cast<int32_t>(acc);
  return true;
}

bool parseGrados(const char* s, int32_t limiteGrados, int32_t& v1e7) {
  if (s == nullptr || *s == '\0') return false;
  bool neg = false;
  if (*s == '-' || *s == '+') { neg = (*s == '-'); ++s; }
  int64_t ent = 0;
  int64_t frac = 0;
  int nEnt = 0;
  int nFrac = 0;
  bool punto = false;
  for (; *s != '\0'; ++s) {
    if (*s == '.' && !punto) { punto = true; continue; }
    if (*s < '0' || *s > '9') return false;
    if (!punto) {
      if (++nEnt > 3) return false;
      ent = ent * 10 + (*s - '0');
    } else {
      if (++nFrac > 7) return false;   // 1e-7 grados = resolución del formato
      frac = frac * 10 + (*s - '0');
    }
  }
  if (nEnt == 0 || (punto && nFrac == 0)) return false;
  for (int i = nFrac; i < 7; ++i) frac *= 10;
  int64_t v = ent * 10000000LL + frac;
  if (v > static_cast<int64_t>(limiteGrados) * 10000000LL) return false;
  if (neg) v = -v;
  v1e7 = static_cast<int32_t>(v);
  return true;
}

bool parsearLinea(const char* linea, Orden& o) {
  memset(&o, 0, sizeof(o));
  o.preset = Preset::NORMAL;
  o.rol = Rol::NORMAL;
  if (linea == nullptr) return ok(o, Cmd::VACIA);
  Tokens t;
  if (!trocear(linea, t)) return error(o, "LINEA_LARGA");
  if (t.n == 0) return ok(o, Cmd::VACIA);
  if (t.n > MAX_TOK) {
    /* Sólo SEND/BCAST admiten muchas palabras (el texto); se tratan abajo
     * a partir de t.orig, que está completo. */
    if (!igual(t.tok[0], "SEND") && !igual(t.tok[0], "BCAST")) return error(o, "SOBRAN_ARGUMENTOS");
    t.n = MAX_TOK;
  }
  const char* c = t.tok[0];

  if (igual(c, "HELP") || igual(c, "STATUS")) {
    if (t.n > 1) return error(o, "SOBRAN_ARGUMENTOS");
    return ok(o, igual(c, "HELP") ? Cmd::HELP : Cmd::STATUS);
  }
  if (igual(c, "ID")) {
    if (t.n == 1) return ok(o, Cmd::ID_VER);
    if (t.n > 2) return error(o, "SOBRAN_ARGUMENTOS");
    if (igual(t.tok[1], "AUTO")) return ok(o, Cmd::ID_AUTO);
    if (!destino(t.tok[1], o.dst, o)) return false;
    return ok(o, Cmd::ID_FIJAR);
  }
  if (igual(c, "PING")) {
    if (t.n < 2) return error(o, "FALTAN_ARGUMENTOS");
    if (t.n > 3) return error(o, "SOBRAN_ARGUMENTOS");
    if (strcmp(t.tok[1], "*") == 0) {
      o.broadcast = true;
      o.dst = DIR_BROADCAST;
    } else if (!destino(t.tok[1], o.dst, o)) {
      return false;
    }
    if (t.n == 3 && !entero(t.tok[2], 0, LEN_MAX - LEN_PING_MIN, o.valor, o)) return false;
    return ok(o, Cmd::PING);
  }
  if (igual(c, "SEND")) {
    if (t.n < 2) return error(o, "FALTAN_ARGUMENTOS");
    if (!destino(t.tok[1], o.dst, o)) return false;
    if (!resto(t, 2, o)) return false;
    return ok(o, Cmd::SEND);
  }
  if (igual(c, "BCAST")) {
    if (!resto(t, 1, o)) return false;
    o.dst = DIR_BROADCAST;
    o.broadcast = true;
    return ok(o, Cmd::BCAST);
  }
  if (igual(c, "ALERT")) return parsearAlerta(t, o);
  if (igual(c, "RADIO")) return parsearRadio(t, o);
  if (igual(c, "SF") || igual(c, "PWR") || igual(c, "TTL") || igual(c, "PERDIDA")) {
    if (t.n < 2) return error(o, "FALTAN_ARGUMENTOS");
    if (t.n > 2) return error(o, "SOBRAN_ARGUMENTOS");
    if (igual(c, "SF")) return entero(t.tok[1], 7, 12, o.valor, o) && ok(o, Cmd::RADIO_SF);
    if (igual(c, "PWR")) return entero(t.tok[1], PWR_MIN, PWR_MAX, o.valor, o) && ok(o, Cmd::RADIO_PWR);
    if (igual(c, "TTL")) return entero(t.tok[1], 0, TTL_MAX, o.valor, o) && ok(o, Cmd::TTL);
    return entero(t.tok[1], 0, 100, o.valor, o) && ok(o, Cmd::PERDIDA);
  }
  if (igual(c, "RELAY")) {
    if (t.n < 2) return error(o, "FALTAN_ARGUMENTOS");
    if (t.n > 2) return error(o, "SOBRAN_ARGUMENTOS");
    if (igual(t.tok[1], "ON")) o.on = true;
    else if (igual(t.tok[1], "OFF")) o.on = false;
    else return error(o, "ON_OFF");
    return ok(o, Cmd::RELAY);
  }
  if (igual(c, "ROL")) {
    if (t.n < 2) return error(o, "FALTAN_ARGUMENTOS");
    if (t.n > 2) return error(o, "SOBRAN_ARGUMENTOS");
    if (!rolPorNombre(t.tok[1], o.rol)) return error(o, "ROL_DESCONOCIDO");
    return ok(o, Cmd::ROL);
  }
  if (igual(c, "RANGE")) return parsearRange(t, o);
  return error(o, "ORDEN_DESCONOCIDA");
}

}  // namespace geolora
