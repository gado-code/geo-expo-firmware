/* ============================================================================
 *  GEO-EXPO LoRa  ·  CRC-16/CCITT-FALSE  (ver crc16.h)
 * ==========================================================================*/
#include "crc16.h"

namespace geolora {

/* Implementación bit a bit, sin tabla: las tramas miden como mucho 214 B, así
 * que el coste es despreciable (≈ 1700 iteraciones) frente a los cientos de
 * milisegundos que la trama pasa en el aire, y se ahorran 512 B de flash. */
uint16_t crc16(const uint8_t* datos, size_t n, uint16_t init) {
  uint16_t crc = init;
  for (size_t i = 0; i < n; ++i) {
    crc ^= static_cast<uint16_t>(static_cast<uint16_t>(datos[i]) << 8);
    for (int b = 0; b < 8; ++b) {
      if (crc & 0x8000u) {
        crc = static_cast<uint16_t>((crc << 1) ^ 0x1021u);
      } else {
        crc = static_cast<uint16_t>(crc << 1);
      }
    }
  }
  return crc;
}

}  // namespace geolora
