/* ============================================================================
 *  GEO-LINK v1  ·  Protocolo punto a punto para el enlace LoRa
 * ============================================================================
 *  Este módulo es el "idioma" que hablan las dos Heltec WiFi LoRa 32 V3:
 *
 *      LLAVERO (TAG)    -> emite balizas periódicas y alertas del botón
 *      BUSCADOR (FINDER)-> escucha, confirma con ACK y manda órdenes
 *
 *  La idea es la del AirTag pero sin nube y sin móvil: el buscador oye al
 *  llavero, mide la potencia con la que le llega (RSSI) y, si hace falta, le
 *  manda "FIND" para que el llavero pite y se deje encontrar.
 *
 *  Igual que lib/nmea y lib/panic, este módulo es DELIBERADAMENTE independiente
 *  de Arduino y de RadioLib: sólo convierte estructuras en bytes y bytes en
 *  estructuras. Así el formato de trama se prueba entero en el PC:
 *
 *      pio test -e native
 *
 *  --------------------------------------------------------------------------
 *  FORMATO DE TRAMA (28 bytes, fijo, little-endian)
 *
 *      off  tam  campo
 *       0    1   magic   'G' (0x47)
 *       1    1   ver<<4 | tipo        (ver = 1)
 *       2    2   src     id del emisor
 *       4    2   dst     id del destinatario (0xFFFF = a todos)
 *       6    2   seq     contador que sube en cada envío (antirrepetición)
 *       8    1   flags   ver Flag::*
 *       9    1   bateria 0..100 %, 255 = desconocida
 *      10    4   lat     grados * 1e7   (int32, 0 si no hay fix)
 *      14    4   lon     grados * 1e7
 *      18    1   aux0    código de alerta (ALERT) u orden (CMD)
 *      19    1   aux1    reservado / eco de secuencia en los ACK
 *      20    8   auth    SipHash-2-4 de los 20 bytes anteriores con la clave
 *
 *  El tamaño fijo es a propósito: cabe de sobra en un paquete LoRa con SF12 y
 *  hace el parseo trivial y sin sorpresas.
 *
 *  --------------------------------------------------------------------------
 *  POR QUÉ LLEVA FIRMA
 *
 *  LoRa es radio abierta: cualquiera con otra placa puede inventarse una
 *  alerta o mandar "deja de pitar". Los 8 bytes de SipHash-2-4 con clave
 *  compartida hacen que sólo quien conoce la clave pueda fabricar una trama
 *  válida, y el contador `seq` con su ventana impide repetir una trama
 *  grabada del aire. No es criptografía de banco —no hay cifrado, el contenido
 *  viaja a la vista— pero corta en seco las dos travesuras evidentes.
 * ==========================================================================*/
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace link {

/* Tamaño exacto de una trama codificada. */
static const size_t FRAME_SIZE = 28;

/* Versión del protocolo que entiende este código. */
static const uint8_t VERSION = 1;

/* Identificador reservado para "a todos". */
static const uint16_t ID_BROADCAST = 0xFFFF;

/* Batería desconocida (no hay divisor conectado todavía). */
static const uint8_t BATTERY_UNKNOWN = 255;

/* --------------------------------------------------------------------------
 *  Tipos de trama.
 * ------------------------------------------------------------------------*/
enum class Type : uint8_t {
  BEACON = 1,   // llavero -> "sigo aquí", con posición si la tiene
  ALERT  = 2,   // llavero -> botón de pánico pulsado (aux0 = código)
  ACK    = 3,   // buscador -> "recibido" (aux1 = byte bajo del seq confirmado)
  CMD    = 4,   // buscador -> orden para el llavero (aux0 = Cmd)
  STATUS = 5    // llavero -> respuesta a un CMD (estado actual)
};

/* --------------------------------------------------------------------------
 *  Órdenes que el buscador puede mandar al llavero (campo aux0 de un CMD).
 * ------------------------------------------------------------------------*/
enum class Cmd : uint8_t {
  NONE      = 0,
  FIND_ON   = 1,   // ponte a pitar y parpadear: te estoy buscando
  FIND_OFF  = 2,   // ya te encontré, cállate
  WHERE     = 3,   // mándame posición y estado ahora, sin esperar a la baliza
  FAST_ON   = 4,   // baliza rápida (modo búsqueda activa)
  FAST_OFF  = 5    // vuelve a la cadencia de ahorro
};

/* --------------------------------------------------------------------------
 *  Bits del campo `flags`.
 * ------------------------------------------------------------------------*/
namespace Flag {
  static const uint8_t ALERT_ACTIVE = 0x01;  // hay una alerta sin confirmar
  static const uint8_t FIND_ACTIVE  = 0x02;  // el llavero está pitando
  static const uint8_t HAS_FIX      = 0x04;  // lat/lon son de verdad
  static const uint8_t LOW_BATTERY  = 0x08;  // batería por debajo del umbral
  static const uint8_t FAST_RATE    = 0x10;  // está en baliza rápida
  static const uint8_t BEACON_ON    = 0x20;  // baliza pedida por la app (BLE)
}

/* --------------------------------------------------------------------------
 *  Contenido de una trama, ya en formato cómodo.
 * ------------------------------------------------------------------------*/
struct Frame {
  Type     type    = Type::BEACON;
  uint16_t src     = 0;
  uint16_t dst     = ID_BROADCAST;
  uint16_t seq     = 0;
  uint8_t  flags   = 0;
  uint8_t  battery = BATTERY_UNKNOWN;
  int32_t  lat1e7  = 0;      // grados * 1e7 (4,205760 -> 42057600)
  int32_t  lon1e7  = 0;
  uint8_t  aux0    = 0;      // código de alerta o Cmd
  uint8_t  aux1    = 0;

  bool has(uint8_t flag) const { return (flags & flag) != 0; }
  void set(uint8_t flag, bool on) { if (on) flags |= flag; else flags = (uint8_t)(flags & ~flag); }
};

/* --------------------------------------------------------------------------
 *  Clave compartida (16 bytes). Las dos placas tienen que llevar la misma.
 * ------------------------------------------------------------------------*/
struct Key {
  uint8_t b[16];
};

/* Deriva una clave de 16 bytes a partir de una frase. Es determinista: la
 * misma frase da siempre la misma clave, que es justo lo que hace falta para
 * poder poner la frase en platformio.ini y olvidarse. */
Key keyFromString(const char* passphrase);

/* --------------------------------------------------------------------------
 *  Resultado de decodificar.
 * ------------------------------------------------------------------------*/
enum class Result : uint8_t {
  OK,
  BAD_SIZE,      // no mide FRAME_SIZE
  BAD_MAGIC,     // no empieza por 'G': ruido o trama de otro sistema
  BAD_VERSION,   // protocolo de otra versión
  BAD_TYPE,      // tipo fuera de la enumeración
  BAD_AUTH       // la firma no cuadra: clave distinta o trama manipulada
};

const char* resultName(Result r);
const char* typeName(Type t);
const char* cmdName(Cmd c);

/* --------------------------------------------------------------------------
 *  Codificar / decodificar.
 * ------------------------------------------------------------------------*/

/* Escribe la trama firmada en `out`. Devuelve los bytes escritos (FRAME_SIZE)
 * o 0 si no cabe. */
size_t encode(const Frame& f, const Key& key, uint8_t* out, size_t cap);

/* Comprueba magic, versión, tipo y firma. Sólo toca `*out` si devuelve OK. */
Result decode(const uint8_t* buf, size_t len, const Key& key, Frame* out);

/* --------------------------------------------------------------------------
 *  Ayudas de coordenadas: el protocolo viaja en enteros (1e7 ≈ 1,1 cm) para
 *  no depender de cómo cada placa representa los `double`.
 * ------------------------------------------------------------------------*/
int32_t degToFixed(double deg);
double  fixedToDeg(int32_t fixed);

/* --------------------------------------------------------------------------
 *  Antirrepetición.
 * --------------------------------------------------------------------------
 *  Guarda el último `seq` visto de cada emisor y rechaza lo repetido o lo
 *  demasiado viejo. La comparación es circular (el contador desborda a los
 *  65535 y eso es normal, no un ataque).
 *
 *  `MAX_JUMP` es cuánto se permite que un emisor salte hacia adelante de una
 *  vez. Sin ese tope, la protección se cae sola: como la comparación circular
 *  considera "más nuevo" a todo lo que esté dentro de media vuelta del
 *  contador, en cuanto el emisor pasa de 32768 tramas las grabaciones VIEJAS
 *  vuelven a parecer nuevas y colarían otra vez. Con el tope, un salto enorme
 *  se rechaza igual que un retroceso, y quien de verdad se reinició se
 *  recupera por la vía de `forget()`.
 * ------------------------------------------------------------------------*/

/* ¿`a` es más nuevo que `b` en aritmética circular de 16 bits? */
bool seqNewer(uint16_t a, uint16_t b);

class ReplayGuard {
 public:
  static const uint8_t  MAX_PEERS = 4;
  /* Salto máximo hacia adelante que se acepta de golpe. 4096 tramas son más
   * de 17 horas de baliza lenta: de sobra para cualquier hueco normal (una
   * placa fuera de alcance un rato), y corta el agujero de la media vuelta. */
  static const uint16_t MAX_JUMP = 4096;

  void reset();

  /* Devuelve true si la trama es nueva (y la anota). False si es repetida o
   * anterior a la última vista de ese emisor. */
  bool accept(uint16_t src, uint16_t seq);

  /* Olvida a un emisor (por ejemplo tras reiniciarlo a propósito). */
  void forget(uint16_t src);

  uint16_t rejected() const { return m_rejected; }

 private:
  struct Peer { bool used = false; uint16_t id = 0; uint16_t lastSeq = 0; };
  Peer     m_peers[MAX_PEERS];
  uint8_t  m_next     = 0;   // a quién se desaloja si hay más de MAX_PEERS
  uint16_t m_rejected = 0;
};

}  // namespace link
