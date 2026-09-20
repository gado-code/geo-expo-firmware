/* ============================================================================
 *  GEO-EXPO LoRa  ·  Parser de la consola serie (DISENO §9)
 * ============================================================================
 *  Sólo PARSEA: línea de texto -> struct Orden. No imprime, no toca la radio
 *  ni la NVS; eso lo hace src/main.cpp. Así cada orden, sus rangos y sus
 *  errores se prueban en el PC (test_consola).
 *
 *  Reglas:
 *    · Una orden por línea; se aceptan "\n", "\r\n" y espacios/tabuladores
 *      de más en cualquier sitio (menos DENTRO del texto de SEND/BCAST, que
 *      se conserva tal cual, mayúsculas incluidas).
 *    · Palabras clave SIN distinguir mayúsculas/minúsculas.
 *    · IDs en hexadecimal de 1 a 4 cifras; "*" = broadcast (sólo en PING).
 *    · Los rangos estáticos (SF 7-12, PWR -9..22, TTL 0-7 ...) se comprueban
 *      aquí; la banda (que depende del BW actual) la valida main con
 *      validar() de radio_params.
 *
 *  Órdenes (§9 + ROL, que no estaba en la tabla pero sí en la NVS §11):
 *    HELP · ID · ID <hex4> · ID AUTO
 *    PING <dst|*> [bytes] · SEND <dst> <texto> · BCAST <texto>
 *    ALERT <1|2|C> [lat lon] [PRUEBA]
 *    STATUS · RADIO · RADIO FREQ|BW|CR|SYNC|PRE|SF|PWR <v>
 *    RADIO PRESET <RAPIDO|NORMAL|LARGO|LAB125> · RADIO SAVE|LOAD|DEFECTO
 *    SF <7-12> · PWR <-9..22> · TTL <0-7> · RELAY ON|OFF
 *    ROL NORMAL|REPETIDOR|PASARELA
 *    RANGE <dst> [n] [ms] · RANGE STOP · RANGE INFORME
 *    PERDIDA <0-100>
 * ==========================================================================*/
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "motor.h"
#include "paquete.h"
#include "radio_params.h"

namespace geolora {

enum class Cmd : uint8_t {
  VACIA,          // línea en blanco: no se responde nada
  ERROR,          // ver Orden::error
  HELP,
  ID_VER, ID_FIJAR, ID_AUTO,
  PING, SEND, BCAST, ALERT,
  STATUS,
  RADIO_VER, RADIO_FREQ, RADIO_BW, RADIO_CR, RADIO_SYNC, RADIO_PRE, RADIO_SF, RADIO_PWR,
  RADIO_PRESET, RADIO_SAVE, RADIO_LOAD, RADIO_DEFECTO,
  TTL, RELAY, ROL,
  RANGE, RANGE_STOP, RANGE_INFORME,
  PERDIDA
};

static const size_t CONSOLA_LINEA_MAX = 255;   // sin contar '\0'

struct Orden {
  Cmd         cmd;
  const char* error;      // Cmd::ERROR: motivo (texto fijo, para "ERR <motivo>")
  uint16_t    dst;        // PING/SEND/RANGE/ID_FIJAR (id)
  bool        broadcast;  // PING *
  int32_t     valor;      // SF, PWR, TTL, CR, PRE, BW, SYNC, PERDIDA, bytes de PING
  uint32_t    freqKHz;    // RADIO FREQ
  uint32_t    n;          // RANGE: número de PINGs (0 = sin fin)
  uint32_t    ms;         // RANGE: intervalo (0 = mínimo automático)
  Preset      preset;
  Rol         rol;
  bool        on;         // RELAY
  uint8_t     codigo;     // ALERT: ALERTA_1 / ALERTA_2 / ALERTA_CANCEL
  bool        conPos;
  int32_t     lat1e7;
  int32_t     lon1e7;
  bool        prueba;
  uint8_t     lenTexto;   // SEND/BCAST
  char        texto[LEN_MAX + 1];
};

/* Parsea `linea` (con o sin fin de línea). Devuelve false si o.cmd == ERROR. */
bool parsearLinea(const char* linea, Orden& o);

/* "4.60971" / "-74.08175" -> grados·1e7 (hasta 7 decimales, sin coma
 * flotante). `limiteGrados` = 90 para latitud, 180 para longitud. */
bool parseGrados(const char* s, int32_t limiteGrados, int32_t& v1e7);

/* Entero decimal con signo opcional, sin espacios. */
bool parseEntero(const char* s, int32_t& v);

}  // namespace geolora
