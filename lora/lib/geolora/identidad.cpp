/* ============================================================================
 *  GEO-EXPO LoRa  ·  Identidad de nodo  (ver identidad.h)
 * ==========================================================================*/
#include "identidad.h"

#include "crc16.h"
#include "paquete.h"

namespace geolora {

uint16_t derivarId(const uint8_t mac[6]) {
  const uint16_t directo = static_cast<uint16_t>((static_cast<uint16_t>(mac[4]) << 8) | mac[5]);
  if (!idReservado(directo)) return directo;
  const uint16_t c = crc16(mac, 6);
  if (!idReservado(c)) return c;
  return static_cast<uint16_t>(c ^ 0x0101u);
}

static int valorHex(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

bool parseHex(const char* s, uint8_t maxDigitos, uint32_t& out) {
  if (s == nullptr || s[0] == '\0' || maxDigitos == 0 || maxDigitos > 8) return false;
  uint32_t v = 0;
  uint8_t n = 0;
  for (const char* p = s; *p != '\0'; ++p) {
    const int d = valorHex(*p);
    if (d < 0 || ++n > maxDigitos) return false;
    v = (v << 4) | static_cast<uint32_t>(d);
  }
  out = v;
  return true;
}

bool parseHex4(const char* s, uint16_t& out) {
  uint32_t v = 0;
  if (!parseHex(s, 4, v)) return false;
  out = static_cast<uint16_t>(v);
  return true;
}

bool idAsignable(uint16_t id) { return !idReservado(id); }

void formatearId(uint16_t id, char* out) {
  static const char HEX_DIG[] = "0123456789ABCDEF";
  out[0] = HEX_DIG[(id >> 12) & 0xF];
  out[1] = HEX_DIG[(id >> 8) & 0xF];
  out[2] = HEX_DIG[(id >> 4) & 0xF];
  out[3] = HEX_DIG[id & 0xF];
  out[4] = '\0';
}

}  // namespace geolora
