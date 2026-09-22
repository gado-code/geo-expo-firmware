/* ============================================================================
 *  GEO-LINK v1  ·  implementación
 * ==========================================================================*/
#include "link.h"

#include <string.h>

namespace link {

/* ==========================================================================
 *  SipHash-2-4  (Aumasson & Bernstein, dominio público)
 * --------------------------------------------------------------------------
 *  Se usa como firma corta de las tramas. Es pequeño (cabe de sobra en el
 *  ESP32 y en cualquier PC), rápido y está pensado justo para esto: mensajes
 *  muy cortos con clave compartida. No cifra nada: sólo demuestra que quien
 *  mandó la trama conoce la clave.
 * ==========================================================================*/
namespace {

inline uint64_t rotl64(uint64_t x, int b) { return (x << b) | (x >> (64 - b)); }

inline uint64_t u8to64le(const uint8_t* p) {
  return (uint64_t)p[0]        | ((uint64_t)p[1] << 8)  | ((uint64_t)p[2] << 16) |
         ((uint64_t)p[3] << 24) | ((uint64_t)p[4] << 32) | ((uint64_t)p[5] << 40) |
         ((uint64_t)p[6] << 48) | ((uint64_t)p[7] << 56);
}

#define SIP_ROUND()                    \
  do {                                 \
    v0 += v1; v1 = rotl64(v1, 13);     \
    v1 ^= v0; v0 = rotl64(v0, 32);     \
    v2 += v3; v3 = rotl64(v3, 16);     \
    v3 ^= v2;                          \
    v0 += v3; v3 = rotl64(v3, 21);     \
    v3 ^= v0;                          \
    v2 += v1; v1 = rotl64(v1, 17);     \
    v1 ^= v2; v2 = rotl64(v2, 32);     \
  } while (0)

uint64_t siphash24(const uint8_t* in, size_t len, const uint8_t key[16]) {
  const uint64_t k0 = u8to64le(key);
  const uint64_t k1 = u8to64le(key + 8);

  uint64_t v0 = 0x736f6d6570736575ULL ^ k0;
  uint64_t v1 = 0x646f72616e646f6dULL ^ k1;
  uint64_t v2 = 0x6c7967656e657261ULL ^ k0;
  uint64_t v3 = 0x7465646279746573ULL ^ k1;

  const size_t left     = len & 7;
  const uint8_t* end    = in + len - left;
  for (const uint8_t* p = in; p != end; p += 8) {
    const uint64_t m = u8to64le(p);
    v3 ^= m;
    SIP_ROUND();
    SIP_ROUND();
    v0 ^= m;
  }

  uint64_t b = ((uint64_t)len) << 56;
  switch (left) {
    case 7: b |= ((uint64_t)end[6]) << 48;  // fall through
    case 6: b |= ((uint64_t)end[5]) << 40;  // fall through
    case 5: b |= ((uint64_t)end[4]) << 32;  // fall through
    case 4: b |= ((uint64_t)end[3]) << 24;  // fall through
    case 3: b |= ((uint64_t)end[2]) << 16;  // fall through
    case 2: b |= ((uint64_t)end[1]) << 8;   // fall through
    case 1: b |= ((uint64_t)end[0]);        break;
    case 0: break;
  }

  v3 ^= b;
  SIP_ROUND();
  SIP_ROUND();
  v0 ^= b;

  v2 ^= 0xff;
  SIP_ROUND();
  SIP_ROUND();
  SIP_ROUND();
  SIP_ROUND();

  return v0 ^ v1 ^ v2 ^ v3;
}

#undef SIP_ROUND

/* Escritura/lectura little-endian, sin depender de la endianness de la placa
 * ni de punteros desalineados (el ESP32-S3 los tolera, pero un `memcpy` de un
 * int32 a un buffer no alineado es exactamente el tipo de detalle que luego
 * da un fallo raro en la otra placa). */
inline void put16(uint8_t* p, uint16_t v) { p[0] = (uint8_t)(v & 0xFF); p[1] = (uint8_t)(v >> 8); }
inline void put32(uint8_t* p, uint32_t v) {
  p[0] = (uint8_t)(v & 0xFF);         p[1] = (uint8_t)((v >> 8) & 0xFF);
  p[2] = (uint8_t)((v >> 16) & 0xFF); p[3] = (uint8_t)((v >> 24) & 0xFF);
}
inline uint16_t get16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }
inline uint32_t get32(const uint8_t* p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static const uint8_t MAGIC       = 0x47;   // 'G'
static const size_t  SIGNED_LEN  = 20;     // bytes cubiertos por la firma
static const size_t  AUTH_LEN    = 8;

bool typeIsKnown(uint8_t t) {
  return t >= (uint8_t)Type::BEACON && t <= (uint8_t)Type::STATUS;
}

}  // namespace

/* ==========================================================================
 *  Clave
 * ==========================================================================*/
Key keyFromString(const char* passphrase) {
  Key k;
  memset(k.b, 0, sizeof(k.b));
  if (passphrase == nullptr) return k;

  const size_t len = strlen(passphrase);

  /* Dos pasadas de SipHash con semillas distintas: la primera da los 8 bytes
   * bajos y la segunda los altos. Así una frase corta ("expo2026") acaba
   * repartida por los 16 bytes en vez de dejar medio clave a cero. */
  static const uint8_t SEED_A[16] = {'G','E','O','-','E','X','P','O','-','L','I','N','K','-','A','1'};
  static const uint8_t SEED_B[16] = {'G','E','O','-','E','X','P','O','-','L','I','N','K','-','B','2'};

  const uint64_t a = siphash24(reinterpret_cast<const uint8_t*>(passphrase), len, SEED_A);
  const uint64_t b = siphash24(reinterpret_cast<const uint8_t*>(passphrase), len, SEED_B);
  put32(k.b + 0,  (uint32_t)(a & 0xFFFFFFFFu));
  put32(k.b + 4,  (uint32_t)(a >> 32));
  put32(k.b + 8,  (uint32_t)(b & 0xFFFFFFFFu));
  put32(k.b + 12, (uint32_t)(b >> 32));
  return k;
}

/* ==========================================================================
 *  Nombres para las trazas
 * ==========================================================================*/
const char* resultName(Result r) {
  switch (r) {
    case Result::OK:          return "OK";
    case Result::BAD_SIZE:    return "tamano incorrecto";
    case Result::BAD_MAGIC:   return "no es una trama GEO-LINK";
    case Result::BAD_VERSION: return "version de protocolo distinta";
    case Result::BAD_TYPE:    return "tipo desconocido";
    case Result::BAD_AUTH:    return "firma invalida (clave distinta o manipulada)";
  }
  return "?";
}

const char* typeName(Type t) {
  switch (t) {
    case Type::BEACON: return "BEACON";
    case Type::ALERT:  return "ALERT";
    case Type::ACK:    return "ACK";
    case Type::CMD:    return "CMD";
    case Type::STATUS: return "STATUS";
  }
  return "?";
}

const char* cmdName(Cmd c) {
  switch (c) {
    case Cmd::NONE:     return "NONE";
    case Cmd::FIND_ON:  return "FIND_ON";
    case Cmd::FIND_OFF: return "FIND_OFF";
    case Cmd::WHERE:    return "WHERE";
    case Cmd::FAST_ON:  return "FAST_ON";
    case Cmd::FAST_OFF: return "FAST_OFF";
  }
  return "?";
}

/* ==========================================================================
 *  Codificar / decodificar
 * ==========================================================================*/
size_t encode(const Frame& f, const Key& key, uint8_t* out, size_t cap) {
  if (out == nullptr || cap < FRAME_SIZE) return 0;

  out[0] = MAGIC;
  out[1] = (uint8_t)((VERSION << 4) | ((uint8_t)f.type & 0x0F));
  put16(out + 2, f.src);
  put16(out + 4, f.dst);
  put16(out + 6, f.seq);
  out[8] = f.flags;
  out[9] = f.battery;
  put32(out + 10, (uint32_t)f.lat1e7);
  put32(out + 14, (uint32_t)f.lon1e7);
  out[18] = f.aux0;
  out[19] = f.aux1;

  const uint64_t tag = siphash24(out, SIGNED_LEN, key.b);
  put32(out + 20, (uint32_t)(tag & 0xFFFFFFFFu));
  put32(out + 24, (uint32_t)(tag >> 32));
  return FRAME_SIZE;
}

Result decode(const uint8_t* buf, size_t len, const Key& key, Frame* out) {
  if (buf == nullptr || len != FRAME_SIZE) return Result::BAD_SIZE;
  if (buf[0] != MAGIC)                     return Result::BAD_MAGIC;

  const uint8_t ver  = (uint8_t)(buf[1] >> 4);
  const uint8_t type = (uint8_t)(buf[1] & 0x0F);
  if (ver != VERSION)      return Result::BAD_VERSION;
  if (!typeIsKnown(type))  return Result::BAD_TYPE;

  /* La firma se comprueba ANTES de dar por buenos los campos: si no cuadra,
   * la trama no existe para nosotros y `*out` no se toca. */
  const uint64_t tag      = siphash24(buf, SIGNED_LEN, key.b);
  const uint64_t received = (uint64_t)get32(buf + 20) | ((uint64_t)get32(buf + 24) << 32);

  /* Comparación en tiempo constante: no por paranoia de laboratorio, sino
   * porque cuesta lo mismo y evita explicarle a nadie por qué no se hizo. */
  uint64_t diff = tag ^ received;
  uint8_t  acc  = 0;
  for (int i = 0; i < 8; ++i) acc |= (uint8_t)((diff >> (i * 8)) & 0xFF);
  if (acc != 0) return Result::BAD_AUTH;

  if (out != nullptr) {
    out->type    = (Type)type;
    out->src     = get16(buf + 2);
    out->dst     = get16(buf + 4);
    out->seq     = get16(buf + 6);
    out->flags   = buf[8];
    out->battery = buf[9];
    out->lat1e7  = (int32_t)get32(buf + 10);
    out->lon1e7  = (int32_t)get32(buf + 14);
    out->aux0    = buf[18];
    out->aux1    = buf[19];
  }
  return Result::OK;
}

/* ==========================================================================
 *  Coordenadas
 * ==========================================================================*/
int32_t degToFixed(double deg) {
  /* Redondeo al entero más cercano, con el signo bien tratado: truncar haría
   * que el sur y el oeste (negativos) se fueran sistemáticamente ~1 cm hacia
   * el ecuador. Es irrelevante para el proyecto, pero es gratis hacerlo bien. */
  const double scaled = deg * 1e7;
  if (scaled >= 2147483647.0)  return 2147483647;
  if (scaled <= -2147483648.0) return -2147483647 - 1;
  return (int32_t)(scaled >= 0 ? scaled + 0.5 : scaled - 0.5);
}

double fixedToDeg(int32_t fixed) { return (double)fixed / 1e7; }

/* ==========================================================================
 *  Antirrepetición
 * ==========================================================================*/
bool seqNewer(uint16_t a, uint16_t b) {
  /* Aritmética circular: `a` es más nuevo que `b` si la distancia de b a a,
   * dando la vuelta, es menor que media circunferencia. Con esto el
   * desbordamiento 65535 -> 0 es una progresión normal y no un salto atrás. */
  return (uint16_t)(a - b) != 0 && (uint16_t)(a - b) < 0x8000;
}

void ReplayGuard::reset() {
  for (uint8_t i = 0; i < MAX_PEERS; ++i) m_peers[i] = Peer();
  m_next     = 0;
  m_rejected = 0;
}

void ReplayGuard::forget(uint16_t src) {
  for (uint8_t i = 0; i < MAX_PEERS; ++i) {
    if (m_peers[i].used && m_peers[i].id == src) m_peers[i] = Peer();
  }
}

bool ReplayGuard::accept(uint16_t src, uint16_t seq) {
  for (uint8_t i = 0; i < MAX_PEERS; ++i) {
    if (m_peers[i].used && m_peers[i].id == src) {
      // Ni hacia atrás ni un salto disparatado hacia adelante (ver MAX_JUMP).
      const uint16_t avance = (uint16_t)(seq - m_peers[i].lastSeq);
      if (!seqNewer(seq, m_peers[i].lastSeq) || avance > MAX_JUMP) {
        ++m_rejected;
        return false;
      }
      m_peers[i].lastSeq = seq;
      return true;
    }
  }

  // Emisor nuevo: se acepta la primera trama y se empieza a vigilarlo.
  for (uint8_t i = 0; i < MAX_PEERS; ++i) {
    if (!m_peers[i].used) {
      m_peers[i].used    = true;
      m_peers[i].id      = src;
      m_peers[i].lastSeq = seq;
      return true;
    }
  }

  /* Tabla llena: se desaloja por turno rotatorio. En este proyecto hay dos
   * placas, así que esto no debería ocurrir nunca; si ocurriera, lo peor que
   * pasa es que se vuelve a aceptar una trama de un emisor ya olvidado. */
  m_peers[m_next].id      = src;
  m_peers[m_next].lastSeq = seq;
  m_peers[m_next].used    = true;
  m_next = (uint8_t)((m_next + 1) % MAX_PEERS);
  return true;
}

}  // namespace link
