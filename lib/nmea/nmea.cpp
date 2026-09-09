/* ============================================================================
 *  GEO-EXPO ALERT  ·  Módulo GPS — implementación del parseo NMEA 0183
 * ==========================================================================*/
#include "nmea.h"

#include <stdlib.h>
#include <string.h>

namespace nmea {

/* ==========================================================================
 *  Utilidades
 * ==========================================================================*/

uint8_t checksum(const char* body, size_t len) {
  uint8_t cs = 0;
  for (size_t i = 0; i < len; ++i) cs ^= (uint8_t)body[i];
  return cs;
}

/* Convierte un dígito hexadecimal a su valor; -1 si no lo es. */
static int hexVal(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  return -1;
}

static bool isDigit(char c) { return c >= '0' && c <= '9'; }

/* Dos dígitos ASCII -> entero; -1 si alguno no es dígito. */
static int two(const char* s) {
  if (!isDigit(s[0]) || !isDigit(s[1])) return -1;
  return (s[0] - '0') * 10 + (s[1] - '0');
}

bool degMinToDegrees(const char* value, char hemi, double* out) {
  if (!value || !out) return false;

  /* El formato es ddmm.mmmm (latitud) o dddmm.mmmm (longitud): SIEMPRE dos
   * dígitos de minutos justo antes del punto decimal. Por eso el número de
   * dígitos de grados se deduce de la longitud de la parte entera, y así la
   * misma función sirve para latitud y longitud sin pasarle un flag. */
  size_t intLen = 0;
  while (isDigit(value[intLen])) ++intLen;
  if (intLen < 3) return false;                 // hacen falta >= 1 grado + 2 minutos
  if (value[intLen] != '.' && value[intLen] != '\0') return false;

  const size_t degDigits = intLen - 2;
  if (degDigits > 3) return false;

  double deg = 0.0;
  for (size_t i = 0; i < degDigits; ++i) deg = deg * 10.0 + (value[i] - '0');

  char* end = nullptr;
  const double minutes = strtod(value + degDigits, &end);
  if (end == value + degDigits) return false;   // no había minutos
  if (minutes < 0.0 || minutes >= 60.0) return false;

  double result = deg + minutes / 60.0;

  switch (hemi) {
    case 'N': case 'E': break;
    case 'S': case 'W': result = -result; break;
    default: return false;                      // hemisferio ausente o basura
  }

  const double limit = (hemi == 'N' || hemi == 'S') ? 90.0 : 180.0;
  if (result < -limit || result > limit) return false;

  *out = result;
  return true;
}

/* ==========================================================================
 *  Parser
 * ==========================================================================*/

void Parser::reset() {
  m_len        = 0;
  m_inSentence = false;
  m_overflow   = false;
  m_last       = Sentence::NONE;
  m_ok         = 0;
  m_bad        = 0;
  memset(&m_fix, 0, sizeof(m_fix));
}

bool Parser::feed(char c) {
  /* '$' siempre reinicia: si una trama llegó cortada, la siguiente no se
   * contamina con los restos de la anterior. */
  if (c == '$') {
    m_inSentence = true;
    m_overflow   = false;
    m_len        = 0;
    return false;
  }
  if (!m_inSentence) return false;              // ruido antes del primer '$'

  if (c == '\r' || c == '\n') {
    m_inSentence = false;
    if (m_overflow || m_len == 0) {             // trama demasiado larga o vacía
      if (m_overflow) ++m_bad;
      m_len = 0;
      return false;
    }
    m_buf[m_len] = '\0';
    const bool ok = parseSentence(m_buf, m_len);
    m_len = 0;
    return ok;
  }

  if (m_len >= MAX_SENTENCE - 1) { m_overflow = true; return false; }
  m_buf[m_len++] = c;
  return false;
}

bool Parser::feedLine(const char* line) {
  if (!line) return false;
  bool result = false;
  for (const char* p = line; *p; ++p) {
    if (feed(*p)) result = true;
  }
  /* Si la línea venía sin salto final, lo simulamos para cerrarla. */
  if (feed('\n')) result = true;
  return result;
}

bool Parser::parseSentence(char* body, size_t len) {
  /* --- (1) Checksum: "...*HH" al final -------------------------------- */
  char* star = nullptr;
  for (size_t i = 0; i < len; ++i) {
    if (body[i] == '*') star = &body[i];
  }
  if (!star || (size_t)(star - body) + 3 > len) { ++m_bad; return false; }

  const int hi = hexVal(star[1]);
  const int lo = hexVal(star[2]);
  if (hi < 0 || lo < 0) { ++m_bad; return false; }

  const size_t bodyLen = (size_t)(star - body);
  if (checksum(body, bodyLen) != (uint8_t)((hi << 4) | lo)) { ++m_bad; return false; }
  *star = '\0';                                  // recorta el checksum

  ++m_ok;

  /* --- (2) Trocear por comas ------------------------------------------ */
  char* fields[24];
  int   n = 0;
  fields[n++] = body;
  for (char* p = body; *p; ++p) {
    if (*p == ',') {
      *p = '\0';
      if (n < (int)(sizeof(fields) / sizeof(fields[0]))) fields[n++] = p + 1;
    }
  }

  /* --- (3) Despachar por tipo ----------------------------------------- */
  const size_t idLen = strlen(fields[0]);
  if (idLen < 5) { m_last = Sentence::OTHER; return true; }
  const char* type = fields[0] + idLen - 3;      // últimos 3: ignora el talker

  if (strcmp(type, "GGA") == 0) { m_last = Sentence::GGA; parseGGA(fields, n); }
  else if (strcmp(type, "RMC") == 0) { m_last = Sentence::RMC; parseRMC(fields, n); }
  else { m_last = Sentence::OTHER; }

  return true;
}

/* Extrae hhmmss(.sss) al Fix. Devuelve false si el campo no sirve. */
static bool parseTime(const char* f, Fix* fix) {
  if (strlen(f) < 6) return false;
  const int h = two(f), m = two(f + 2), s = two(f + 4);
  if (h < 0 || m < 0 || s < 0 || h > 23 || m > 59 || s > 60) return false;
  fix->hh = (uint8_t)h; fix->mm = (uint8_t)m; fix->ss = (uint8_t)s;
  fix->hasTime = true;
  return true;
}

bool Parser::parseGGA(char* f[], int n) {
  /* $xxGGA,hora,lat,N/S,lon,E/W,calidad,sats,hdop,alt,M,... */
  if (n < 10) return false;

  parseTime(f[1], &m_fix);

  const int quality = (f[6][0] != '\0') ? (int)strtol(f[6], nullptr, 10) : 0;

  double lat, lon;
  const bool haveCoords = degMinToDegrees(f[2], f[3][0], &lat) &&
                          degMinToDegrees(f[4], f[5][0], &lon);

  /* quality 0 = sin fix. Sin coordenadas válidas tampoco hay fix, por muy
   * bien que venga el resto de la trama. */
  if (quality > 0 && haveCoords) {
    m_fix.lat   = lat;
    m_fix.lon   = lon;
    m_fix.valid = true;
  } else {
    m_fix.valid = false;
  }

  if (f[7][0]) m_fix.sats = (uint8_t)strtol(f[7], nullptr, 10);
  if (f[8][0]) m_fix.hdop = (float)strtod(f[8], nullptr);
  if (f[9][0]) m_fix.altM = (float)strtod(f[9], nullptr);
  return true;
}

bool Parser::parseRMC(char* f[], int n) {
  /* $xxRMC,hora,estado,lat,N/S,lon,E/W,velocidad,rumbo,fecha,... */
  if (n < 10) return false;

  parseTime(f[1], &m_fix);

  const bool active = (f[2][0] == 'A');

  double lat, lon;
  const bool haveCoords = degMinToDegrees(f[3], f[4][0], &lat) &&
                          degMinToDegrees(f[5], f[6][0], &lon);

  if (active && haveCoords) {
    m_fix.lat   = lat;
    m_fix.lon   = lon;
    m_fix.valid = true;
  } else {
    m_fix.valid = false;
  }

  if (f[7][0]) m_fix.speedKn = (float)strtod(f[7], nullptr);

  /* Fecha ddmmyy. Para el año de 2 cifras se usa la convención habitual en
   * GPS: 80..99 -> 19xx, 00..79 -> 20xx. */
  if (strlen(f[9]) >= 6) {
    const int d = two(f[9]), mo = two(f[9] + 2), y = two(f[9] + 4);
    if (d >= 1 && d <= 31 && mo >= 1 && mo <= 12 && y >= 0) {
      m_fix.day     = (uint8_t)d;
      m_fix.mon     = (uint8_t)mo;
      m_fix.year    = (uint16_t)((y >= 80 ? 1900 : 2000) + y);
      m_fix.hasDate = true;
    }
  }
  return true;
}

}  // namespace nmea
