/* ============================================================================
 *  GEO-EXPO ALERT  ·  Módulo GPS — parseo de tramas NMEA 0183
 * ============================================================================
 *  Este módulo es DELIBERADAMENTE independiente de Arduino: no incluye
 *  <Arduino.h>, no usa millis(), no reserva memoria dinámica y no habla con
 *  ningún puerto. Sólo transforma texto NMEA en una posición.
 *
 *  Gracias a eso se puede compilar y probar en el PC (`pio test -e native`)
 *  SIN placa y SIN módulo GPS, que es justo lo que hace falta ahora mismo.
 *
 *  Uso previsto en el firmware (cuando llegue el GPS por UART2):
 *      nmea::Parser gps;
 *      while (SerialGPS.available()) {
 *        if (gps.feed((char)SerialGPS.read())) {     // true = sentencia válida
 *          if (gps.fix().valid) { ... gps.fix().lat / .lon ... }
 *        }
 *      }
 *
 *  Sentencias soportadas: GGA (posición + calidad + satélites + altitud) y
 *  RMC (posición + validez + fecha/hora + velocidad). Se aceptan todos los
 *  talkers habituales: GP (GPS), GN (multiconstelación), GL (GLONASS),
 *  GA (Galileo), BD/GB (BeiDou).
 * ==========================================================================*/
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace nmea {

/* Longitud máxima de una sentencia NMEA según el estándar: 82 caracteres
 * contando '$' y el CRLF final. Se deja algo de margen. */
static const size_t MAX_SENTENCE = 96;

/* --------------------------------------------------------------------------
 *  Resultado del parseo: la última posición conocida.
 * --------------------------------------------------------------------------
 *  lat/lon van en GRADOS DECIMALES con signo (+ norte, + este), que es el
 *  formato que quiere cualquier mapa. Se usa `double` a propósito: un `float`
 *  sólo da ~7 cifras significativas y perdería metros en la parte decimal.
 * ------------------------------------------------------------------------*/
struct Fix {
  bool     valid;      // hay posición utilizable (GGA quality>0 o RMC status='A')
  double   lat;        // grados decimales, + = norte
  double   lon;        // grados decimales, + = este
  float    altM;       // altitud sobre el geoide, metros (sólo GGA)
  float    hdop;       // dilución horizontal de precisión (sólo GGA)
  float    speedKn;    // velocidad sobre el suelo, nudos (sólo RMC)
  uint8_t  sats;       // satélites en uso (sólo GGA)
  uint8_t  hh, mm, ss; // hora UTC
  uint8_t  day, mon;   // fecha UTC (sólo RMC)
  uint16_t year;       // año UTC completo (sólo RMC)
  bool     hasTime;    // hh:mm:ss son válidos
  bool     hasDate;    // day/mon/year son válidos
};

/* --------------------------------------------------------------------------
 *  Tipo de la última sentencia procesada (útil para trazas y pruebas).
 * ------------------------------------------------------------------------*/
enum class Sentence : uint8_t { NONE, GGA, RMC, OTHER };

/* --------------------------------------------------------------------------
 *  Parser incremental. Se le van dando caracteres sueltos con feed().
 * ------------------------------------------------------------------------*/
class Parser {
 public:
  Parser() { reset(); }

  /* Vuelve al estado inicial y descarta la posición conocida. */
  void reset();

  /* Procesa UN carácter del flujo del GPS.
   * Devuelve true justo cuando se acaba de completar una sentencia con
   * checksum correcto (sea o no de las que sabemos interpretar). */
  bool feed(char c);

  /* Procesa una línea completa (con o sin '$' inicial, con o sin CRLF).
   * Pensado para pruebas y para inyectar tramas a mano por el monitor serie. */
  bool feedLine(const char* line);

  const Fix& fix() const { return m_fix; }

  /* Tipo de la última sentencia aceptada. */
  Sentence lastSentence() const { return m_last; }

  /* Contadores de diagnóstico: cuántas sentencias llegaron bien y cuántas se
   * descartaron por checksum malo o por pasarse de largo. */
  uint32_t okCount()  const { return m_ok; }
  uint32_t badCount() const { return m_bad; }

 private:
  bool parseSentence(char* body, size_t len);   // body SIN '$' ni checksum
  bool parseGGA(char* fields[], int n);
  bool parseRMC(char* fields[], int n);

  char     m_buf[MAX_SENTENCE];
  size_t   m_len;
  bool     m_inSentence;   // ya vimos el '$' de apertura
  bool     m_overflow;     // la sentencia actual se pasó de MAX_SENTENCE
  Fix      m_fix;
  Sentence m_last;
  uint32_t m_ok;
  uint32_t m_bad;
};

/* --------------------------------------------------------------------------
 *  Utilidades expuestas por separado porque también se prueban por su cuenta.
 * ------------------------------------------------------------------------*/

/* Convierte el formato NMEA ddmm.mmmm / dddmm.mmmm a grados decimales.
 * `hemi` es 'N'/'S' para latitud y 'E'/'W' para longitud.
 * Devuelve false si el texto está vacío o no es un número plausible. */
bool degMinToDegrees(const char* value, char hemi, double* out);

/* Calcula el checksum XOR de una sentencia (el texto ENTRE '$' y '*'). */
uint8_t checksum(const char* body, size_t len);

}  // namespace nmea
