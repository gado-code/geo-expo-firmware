/* ============================================================================
 *  GEO-EXPO LoRa  ·  Formato de trama binario, versión 1 (DISENO §5)
 * ============================================================================
 *  Cabecera de 12 bytes + carga (0..200) + CRC16 de 2 bytes = 14 B de
 *  sobrecarga. Todos los enteros multibyte en LITTLE-ENDIAN, escritos byte a
 *  byte (nunca memcpy de un struct: no dependemos del empaquetado).
 *
 *    Off  Campo      Tam  Descripción
 *    0    magic       1   0x47 ('G')
 *    1    ver_tipo    1   nibble alto = versión (1), nibble bajo = tipo
 *    2    flags       1   bit0 ACK_REQ · bit1 AUTH (reservado) · bit2 PRUEBA
 *    3-4  src         2   ID del ORIGEN (no cambia al reenviar)
 *    5-6  dst         2   destino final; 0xFFFF broadcast, 0xFFFE pasarela
 *    7-8  msgId       2   contador del origen (igual en reintentos y reenvíos)
 *    9    ttl         1   reenvíos que QUEDAN (0 = nadie la repite, E1)
 *    10   hops        1   reenvíos que YA lleva (0 = directa del origen)
 *    11   len         1   bytes de carga; len_PHY == 14 + len, len <= 200
 *    12…  carga      len
 *    12+len crc       2   CRC-16/CCITT-FALSE de los bytes 0..11+len
 *
 *  El CRC cubre ttl/hops: quien reenvía lo RECALCULA (prepararReenvio()).
 *
 *  Validación del receptor, en este orden (cada rechazo tiene su contador):
 *      CORTA -> MAGIC -> VERSION -> LEN -> CRC -> AUTH -> SRC -> TIPO
 *  El diseño (§5.1) pone "tipo conocido" antes de "src no reservado"; aquí el
 *  TIPO va el ÚLTIMO a propósito: así un rechazo TIPO significa "la trama es
 *  íntegra y de nuestra versión, sólo que no sé qué es", y el motor puede
 *  REENVIARLA en E2 sin más comprobaciones (un nodo antiguo no rompe la malla
 *  de nodos nuevos). El resultado para el usuario es el mismo.
 * ==========================================================================*/
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace geolora {

/* --------------------------------------------------------------------------
 *  Constantes del formato.
 * ------------------------------------------------------------------------*/
static const uint8_t  MAGIC         = 0x47;   // 'G'
static const uint8_t  VERSION       = 1;
static const uint8_t  CABECERA      = 12;
static const uint8_t  SOBRECARGA    = 14;     // cabecera + CRC
static const uint8_t  LEN_MAX       = 200;    // carga máxima
static const uint8_t  TRAMA_MAX     = 214;    // SOBRECARGA + LEN_MAX
static const uint8_t  TTL_MAX       = 7;
static const uint8_t  HOPS_MAX      = 7;      // más allá no se reenvía nunca
static const uint16_t DIR_BROADCAST = 0xFFFF;
static const uint16_t DIR_PASARELA  = 0xFFFE; // "cualquier pasarela" (prevista, §14)

/* --------------------------------------------------------------------------
 *  Tipos (nibble bajo de ver_tipo). 0x6 ORDEN y 0x7..0xF están reservados:
 *  para v1 son "desconocidos" (no se entregan, pero se reenvían en E2).
 * ------------------------------------------------------------------------*/
enum class Tipo : uint8_t {
  DATA  = 0x1,   // texto/bytes libres (SEND / BCAST)
  ACK   = 0x2,   // confirmación: ackMsgId u16 · rssiNeg u8 · snrQ4 i8 · hopsRx u8
  PING  = 0x3,   // t_ms u32 [+ relleno]
  PONG  = 0x4,   // t_eco u32 · rssiNeg u8 · snrQ4 i8 · hopsRx u8
  ALERT = 0x5,   // alerta GEO-EXPO (§5.4)
  ORDEN = 0x6    // RESERVADO E3 (bajada hacia el llavero). No implementado.
};

/* ¿Es un tipo que v1 sabe procesar (DATA..ALERT)? */
bool tipoImplementado(uint8_t tipo);
const char* nombreTipo(uint8_t tipo);

/* --------------------------------------------------------------------------
 *  Flags (§5.3). Los bits 3..7 están reservados: el emisor los pone a 0 y el
 *  receptor los IGNORA (no son motivo de rechazo).
 * ------------------------------------------------------------------------*/
static const uint8_t FLAG_ACK_REQ = 0x01;
static const uint8_t FLAG_AUTH    = 0x02;   // reservado E3: si llega a 1 -> rechazo
static const uint8_t FLAG_PRUEBA  = 0x04;   // tráfico de prueba: la pasarela NO escala

/* --------------------------------------------------------------------------
 *  IDs de nodo reservados (§6): 0x0000 (sin asignar) y 0xFFF0..0xFFFF
 *  (broadcast, pasarela y futuros). Ningún nodo puede ser ORIGEN con ellos.
 * ------------------------------------------------------------------------*/
bool idReservado(uint16_t id);

/* --------------------------------------------------------------------------
 *  Cabecera ya separada en campos.
 * ------------------------------------------------------------------------*/
struct Cabecera {
  uint8_t  version;
  uint8_t  tipo;     // valor crudo del nibble (puede ser desconocido)
  uint8_t  flags;
  uint16_t src;
  uint16_t dst;
  uint16_t msgId;
  uint8_t  ttl;
  uint8_t  hops;
  uint8_t  len;
  Cabecera()
      : version(VERSION), tipo(0), flags(0), src(0), dst(0), msgId(0), ttl(0), hops(0), len(0) {}
};

/* --------------------------------------------------------------------------
 *  Motivos de rechazo. El orden de la enumeración es el de la validación.
 *  CARGA no lo produce decodificar(): lo usa el motor cuando la cabecera es
 *  buena pero la carga no cuadra con su tipo (p. ej. un ACK de 3 bytes).
 * ------------------------------------------------------------------------*/
enum class Rechazo : uint8_t {
  NINGUNO = 0,
  CORTA,     // menos de 14 bytes
  MAGIC,     // no empieza por 0x47
  VERSION,   // versión distinta de 1
  LEN,       // len > 200 o longitud PHY != 14 + len
  CRC,       // CRC16 de aplicación incorrecto
  AUTH,      // flag AUTH a 1 (reservado E3)
  SRC,       // origen con ID reservado
  TIPO,      // tipo no implementado en v1 (la trama es íntegra: se puede reenviar)
  CARGA,     // (motor) carga incoherente con su tipo
  N_RECHAZOS
};
const char* nombreRechazo(Rechazo r);

/* --------------------------------------------------------------------------
 *  Codificación / decodificación.
 * ------------------------------------------------------------------------*/

/* Serializa cabecera + carga + CRC en `out`. Usa h.len como longitud de la
 * carga (y siempre VERSION como versión). Devuelve el tamaño total de la
 * trama, o 0 si h.len > LEN_MAX, el tipo no cabe en 4 bits, falta la carga
 * con len > 0 o `cap` no alcanza. */
size_t codificar(const Cabecera& h, const uint8_t* carga, uint8_t* out, size_t cap);

/* Valida y separa una trama recibida. Si devuelve NINGUNO o TIPO, `h` y
 * `carga` (puntero DENTRO de `trama`) son válidos. Con cualquier otro rechazo
 * su contenido no está garantizado. */
Rechazo decodificar(const uint8_t* trama, size_t n, Cabecera& h, const uint8_t*& carga);

/* Convierte, EN SITIO, una trama válida en la copia que sale al reenviarla:
 * ttl-1, hops+1 y CRC recalculado. Devuelve false (y no toca nada) si la
 * trama no es coherente, si ttl == 0 o si hops >= HOPS_MAX. */
bool prepararReenvio(uint8_t* trama, size_t n);

/* --------------------------------------------------------------------------
 *  Utilidades little-endian (las usan también carga.cpp y los tests).
 * ------------------------------------------------------------------------*/
inline void escribirU16(uint8_t* p, uint16_t v) {
  p[0] = static_cast<uint8_t>(v & 0xFF);
  p[1] = static_cast<uint8_t>(v >> 8);
}
inline uint16_t leerU16(const uint8_t* p) {
  return static_cast<uint16_t>(p[0] | (static_cast<uint16_t>(p[1]) << 8));
}
inline void escribirU32(uint8_t* p, uint32_t v) {
  p[0] = static_cast<uint8_t>(v & 0xFF);
  p[1] = static_cast<uint8_t>((v >> 8) & 0xFF);
  p[2] = static_cast<uint8_t>((v >> 16) & 0xFF);
  p[3] = static_cast<uint8_t>((v >> 24) & 0xFF);
}
inline uint32_t leerU32(const uint8_t* p) {
  return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
         (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

}  // namespace geolora
