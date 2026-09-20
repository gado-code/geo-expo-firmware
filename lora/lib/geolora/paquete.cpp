/* ============================================================================
 *  GEO-EXPO LoRa  ·  Formato de trama v1  (ver paquete.h)
 * ==========================================================================*/
#include "paquete.h"

#include <string.h>

#include "crc16.h"

namespace geolora {

bool tipoImplementado(uint8_t tipo) {
  return tipo >= static_cast<uint8_t>(Tipo::DATA) && tipo <= static_cast<uint8_t>(Tipo::ALERT);
}

const char* nombreTipo(uint8_t tipo) {
  switch (tipo) {
    case 0x1: return "DATA";
    case 0x2: return "ACK";
    case 0x3: return "PING";
    case 0x4: return "PONG";
    case 0x5: return "ALERT";
    case 0x6: return "ORDEN";
    default:  return "?";
  }
}

bool idReservado(uint16_t id) { return id == 0x0000 || id >= 0xFFF0; }

const char* nombreRechazo(Rechazo r) {
  switch (r) {
    case Rechazo::NINGUNO: return "ninguno";
    case Rechazo::CORTA:   return "corta";
    case Rechazo::MAGIC:   return "magic";
    case Rechazo::VERSION: return "version";
    case Rechazo::LEN:     return "len";
    case Rechazo::CRC:     return "crc";
    case Rechazo::AUTH:    return "auth";
    case Rechazo::SRC:     return "src";
    case Rechazo::TIPO:    return "tipo";
    case Rechazo::CARGA:   return "carga";
    default:               return "?";
  }
}

size_t codificar(const Cabecera& h, const uint8_t* carga, uint8_t* out, size_t cap) {
  if (h.len > LEN_MAX) return 0;
  if (h.tipo > 0x0F) return 0;
  if (h.len > 0 && carga == nullptr) return 0;
  const size_t total = static_cast<size_t>(SOBRECARGA) + h.len;
  if (out == nullptr || cap < total) return 0;

  out[0] = MAGIC;
  out[1] = static_cast<uint8_t>((VERSION << 4) | (h.tipo & 0x0F));
  out[2] = h.flags;
  escribirU16(out + 3, h.src);
  escribirU16(out + 5, h.dst);
  escribirU16(out + 7, h.msgId);
  out[9]  = h.ttl;
  out[10] = h.hops;
  out[11] = h.len;
  /* memmove y no memcpy: permite codificar "en sitio" si la carga ya está en
   * out + CABECERA (lo hace el motor para no duplicar búferes). */
  if (h.len > 0) memmove(out + CABECERA, carga, h.len);
  const uint16_t crc = crc16(out, CABECERA + h.len);
  escribirU16(out + CABECERA + h.len, crc);
  return total;
}

Rechazo decodificar(const uint8_t* trama, size_t n, Cabecera& h, const uint8_t*& carga) {
  carga = nullptr;
  if (trama == nullptr || n < SOBRECARGA) return Rechazo::CORTA;
  if (trama[0] != MAGIC) return Rechazo::MAGIC;
  const uint8_t version = static_cast<uint8_t>(trama[1] >> 4);
  if (version != VERSION) return Rechazo::VERSION;
  const uint8_t len = trama[11];
  if (len > LEN_MAX || n != static_cast<size_t>(SOBRECARGA) + len) return Rechazo::LEN;
  const uint16_t crcCalc = crc16(trama, CABECERA + len);
  if (crcCalc != leerU16(trama + CABECERA + len)) return Rechazo::CRC;

  h.version = version;
  h.tipo    = static_cast<uint8_t>(trama[1] & 0x0F);
  h.flags   = trama[2];
  h.src     = leerU16(trama + 3);
  h.dst     = leerU16(trama + 5);
  h.msgId   = leerU16(trama + 7);
  h.ttl     = trama[9];
  h.hops    = trama[10];
  h.len     = len;
  carga     = trama + CABECERA;

  if (h.flags & FLAG_AUTH) return Rechazo::AUTH;
  if (idReservado(h.src)) return Rechazo::SRC;
  if (!tipoImplementado(h.tipo)) return Rechazo::TIPO;
  return Rechazo::NINGUNO;
}

bool prepararReenvio(uint8_t* trama, size_t n) {
  if (trama == nullptr || n < SOBRECARGA || n > TRAMA_MAX) return false;
  const uint8_t len = trama[11];
  if (n != static_cast<size_t>(SOBRECARGA) + len) return false;
  if (trama[9] == 0 || trama[10] >= HOPS_MAX) return false;
  trama[9]  = static_cast<uint8_t>(trama[9] - 1);
  trama[10] = static_cast<uint8_t>(trama[10] + 1);
  escribirU16(trama + CABECERA + len, crc16(trama, CABECERA + len));
  return true;
}

}  // namespace geolora
