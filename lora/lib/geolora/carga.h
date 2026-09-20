/* ============================================================================
 *  GEO-EXPO LoRa  ·  Cargas de PING / PONG / ACK / ALERT (DISENO §5.2, §5.4)
 * ============================================================================
 *  Todo en little-endian y byte a byte, como la cabecera.
 *
 *    ACK    ackMsgId u16 · rssiNeg u8 · snrQ4 i8 · hopsRx u8          = 5 B
 *    PING   t_ms u32 [+ relleno opcional para pruebas de tamaño]      >= 4 B
 *    PONG   t_eco u32 · rssiNeg u8 · snrQ4 i8 · hopsRx u8             = 7 B
 *    ALERT  codigo u8 · seqAlerta u8 · aflags u8 [· lat i32 · lon i32] 3/11 B
 *
 *    rssiNeg = -RSSI en dBm saturado a 0..255     (-87 dBm -> 87)
 *    snrQ4   = SNR en cuartos de dB, saturado     (-10.25 dB -> -41)
 *    hopsRx  = campo hops de la trama a la que se responde
 *
 *  ── Transporte de los tokens GEO-EXPO (contrato BLE CONGELADO) ──────────
 *  La ALERT lleva un CÓDIGO de 1 byte, no el texto (ahorra airtime). Quien la
 *  traduce de vuelta (consola, OLED, futura pasarela) usa tokenAlerta(), que
 *  devuelve EXACTAMENTE los tokens del README raíz §1:
 *
 *      1 -> "ALERT:1"     2 -> "ALERT:2"     3 -> "CANCEL"
 *
 *  BEACON:ON/OFF son órdenes HACIA el llavero, no alertas: no viajan en ALERT
 *  (irían en el tipo ORDEN, reservado para E3).
 * ==========================================================================*/
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace geolora {

/* --------------------------------------------------------------------------
 *  ALERT
 * ------------------------------------------------------------------------*/
static const uint8_t ALERTA_1      = 1;   // "ALERT:1"  pulsación corta del llavero
static const uint8_t ALERTA_2      = 2;   // "ALERT:2"  pulsación larga
static const uint8_t ALERTA_CANCEL = 3;   // "CANCEL"

static const uint8_t AFLAG_POS     = 0x01;  // siguen lat/lon
static const uint8_t AFLAG_LLAVERO = 0x02;  // reservado E3 (id del llavero)

static const uint8_t LEN_ALERTA_SIN_POS = 3;
static const uint8_t LEN_ALERTA_CON_POS = 11;

struct CargaAlerta {
  uint8_t codigo;
  uint8_t seq;       // número de alerta del origen; un CANCEL lleva el de la que anula
  uint8_t aflags;
  int32_t lat1e7;    // grados * 1e7 (con signo), sólo si AFLAG_POS
  int32_t lon1e7;
  CargaAlerta() : codigo(0), seq(0), aflags(0), lat1e7(0), lon1e7(0) {}
  bool conPosicion() const { return (aflags & AFLAG_POS) != 0; }
};

/* Token GEO-EXPO exacto del código (o nullptr si el código es reservado). */
const char* tokenAlerta(uint8_t codigo);
/* Operación inversa, comparación EXACTA (mayúsculas incluidas). 0 = no es un
 * token de alerta (p. ej. "BEACON:ON" devuelve 0). */
uint8_t codigoDeToken(const char* token);

/* Serializa en `out` (>= 11 B). Devuelve la longitud (3 u 11) o 0 si el
 * código no es 1..3 o la posición está fuera de ±90 / ±180 grados. */
uint8_t codificarAlerta(const CargaAlerta& a, uint8_t* out);
/* Valida y separa. Acepta bytes de más al final (compatibilidad hacia
 * delante con extensiones de E3, p. ej. AFLAG_LLAVERO); rechaza cargas
 * truncadas, códigos reservados y posiciones fuera de rango. */
bool decodificarAlerta(const uint8_t* p, uint8_t len, CargaAlerta& a);

/* Línea que una FUTURA pasarela entregaría: el token exacto y, si la alerta
 * trae posición y se acuerda la extensión del README §8.4, ";lat;lon" con 6
 * decimales (p. ej. "ALERT:2;4.609710;-74.081750"). Sin terminador de línea:
 * lo añade quien la transmite, igual que el firmware del llavero.
 * Devuelve la longitud escrita o 0 si no cabe o el código no es válido. */
size_t lineaPasarela(const CargaAlerta& a, bool conPosicion, char* out, size_t cap);

/* Grados*1e7 -> texto con `decimales` (0..7), redondeo a la mitad lejos de
 * cero, sin coma flotante ("-74.0817500", "4.609710"). Devuelve longitud. */
size_t formatearGrados(int32_t v1e7, uint8_t decimales, char* out, size_t cap);

/* --------------------------------------------------------------------------
 *  ACK / PING / PONG
 * ------------------------------------------------------------------------*/
static const uint8_t LEN_ACK      = 5;
static const uint8_t LEN_PONG     = 7;
static const uint8_t LEN_PING_MIN = 4;

struct CargaAck {
  uint16_t ackMsgId;
  uint8_t  rssiNeg;
  int8_t   snrQ4;
  uint8_t  hopsRx;
  CargaAck() : ackMsgId(0), rssiNeg(0), snrQ4(0), hopsRx(0) {}
};
struct CargaPong {
  uint32_t tEco;
  uint8_t  rssiNeg;
  int8_t   snrQ4;
  uint8_t  hopsRx;
  CargaPong() : tEco(0), rssiNeg(0), snrQ4(0), hopsRx(0) {}
};

uint8_t codificarAck(const CargaAck& a, uint8_t* out);            // devuelve 5
bool    decodificarAck(const uint8_t* p, uint8_t len, CargaAck& a); // exige len == 5
uint8_t codificarPong(const CargaPong& a, uint8_t* out);          // devuelve 7
bool    decodificarPong(const uint8_t* p, uint8_t len, CargaPong& a);
/* PING: t_ms + `relleno` bytes (0x00). Devuelve la longitud o 0 si se pasa
 * de LEN_MAX. */
uint8_t codificarPing(uint32_t tMs, uint8_t relleno, uint8_t* out);
bool    decodificarPing(const uint8_t* p, uint8_t len, uint32_t& tMs);

/* --------------------------------------------------------------------------
 *  Saturaciones de calidad de enlace.
 * ------------------------------------------------------------------------*/
uint8_t rssiANeg(int16_t rssiDbm);           // -87 -> 87 ; > 0 -> 0 ; < -255 -> 255
inline int16_t rssiDeNeg(uint8_t neg) { return static_cast<int16_t>(-static_cast<int16_t>(neg)); }
int8_t  snrAQ4(float snrDb);                 // redondeo al cuarto más cercano, saturado
int8_t  saturarQ4(int32_t q4);               // -128..127

}  // namespace geolora
