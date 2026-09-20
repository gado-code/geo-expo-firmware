/* ============================================================================
 *  GEO-EXPO LoRa  ·  Identidad de nodo (DISENO §6)
 * ============================================================================
 *  Las dos placas llevan el MISMO binario; lo que las distingue es el ID:
 *
 *    · Por defecto: los 2 ÚLTIMOS bytes de la MAC base. MAC ...:1A:2B -> 1A2B,
 *      comprobable a ojo con la etiqueta de la placa o con la orden ID.
 *    · Si eso cae en un ID reservado (0x0000 o 0xFFF0..0xFFFF) se usa
 *      crc16(MAC) y, si también es reservado, crc16(MAC) ^ 0x0101 (que ya no
 *      puede serlo: 0x0000 -> 0x0101 y 0xFFFx -> 0xFEFx).
 *    · Override manual: "ID <hex4>" lo guarda en NVS (lo hace src/, aquí sólo
 *      se valida). "ID AUTO" lo borra.
 *
 *  Todo puro: se prueba en el PC con MACs inventadas.
 * ==========================================================================*/
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace geolora {

/* ID derivado de la MAC (6 bytes en el orden en que se imprimen,
 * mac[0]:mac[1]:...:mac[5]). Nunca devuelve un ID reservado. */
uint16_t derivarId(const uint8_t mac[6]);

/* Parseo de hexadecimal de 1..`maxDigitos` cifras (0-9, a-f, A-F), sin
 * prefijo "0x" ni espacios. false si está vacío, es más largo o tiene
 * caracteres no hexadecimales. */
bool parseHex(const char* s, uint8_t maxDigitos, uint32_t& out);
/* Atajo para IDs: 1..4 cifras hexadecimales. NO comprueba reservados (eso
 * depende del uso: "PING FFFF" no tiene sentido, pero el parser no lo sabe). */
bool parseHex4(const char* s, uint16_t& out);

/* ¿Se puede fijar como ID propio? (no reservado) */
bool idAsignable(uint16_t id);

/* "1A2B": siempre 4 cifras en MAYÚSCULAS + '\0' (out >= 5 B). */
void formatearId(uint16_t id, char* out);

}  // namespace geolora
