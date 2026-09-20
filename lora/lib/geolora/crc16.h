/* ============================================================================
 *  GEO-EXPO LoRa  ·  CRC-16/CCITT-FALSE (DISENO §5.1)
 * ============================================================================
 *  Polinomio 0x1021, valor inicial 0xFFFF, sin reflejar entrada ni salida,
 *  xorout 0. Vector de prueba oficial: "123456789" -> 0x29B1.
 *
 *  Se usa para dos cosas:
 *    · el CRC de aplicación de cada trama (cubre cabecera + carga);
 *    · la derivación del ID de nodo cuando los 2 últimos bytes de la MAC caen
 *      en un valor reservado (§6).
 *
 *  Es C++ puro (sin Arduino) como todo lib/geolora: se prueba en el PC con
 *      pio test -e native
 * ==========================================================================*/
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace geolora {

/* CRC-16/CCITT-FALSE de `n` bytes. Con `init` distinto de 0xFFFF se puede
 * encadenar el cálculo por trozos (se pasa el CRC parcial como init). */
uint16_t crc16(const uint8_t* datos, size_t n, uint16_t init = 0xFFFF);

}  // namespace geolora
