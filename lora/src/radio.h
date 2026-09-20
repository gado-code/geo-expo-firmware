/* ============================================================================
 *  GEO-EXPO LoRa  ·  Capa de radio: RadioLib + SX1262   (DISENO §12.3)
 * ============================================================================
 *  Aquí vive TODO lo que sabe de RadioLib y del SX1262. El motor (lib/geolora)
 *  no incluye este archivo ni lo conoce: sólo dice qué transmitir y se entera
 *  de lo que llega, así que se puede seguir probando entero en el PC.
 *
 *  Reparto de responsabilidades:
 *      main.cpp  ->  cuándo transmitir y qué hacer con lo recibido
 *      radio.cpp ->  cómo hablarle al chip (SPI, IRQ, CAD, RSSI/SNR)
 * ==========================================================================*/
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "radio_params.h"

namespace radio {

/* Resultado de un intento de transmisión. */
enum class ResultadoTx : uint8_t {
  OK,        // transmitida
  OCUPADO,   // el CAD oyó a alguien: no se transmitió (el motor reintenta)
  ERROR      // la radio devolvió un código de error
};

/* Arranca el chip con estos parámetros. Si devuelve false, `codigo` trae el
 * error de RadioLib y el firmware DEBE seguir vivo: la consola sigue siendo
 * útil para diagnosticar (causas típicas: TCXO o BUSY mal puestos). */
bool begin(const geolora::ParamsRadio& p, int16_t& codigo);

/* ¿El begin() salió bien? Con false no se transmite nada. */
bool viva();

/* Cambia los parámetros en caliente (consola RADIO ...). */
bool aplicar(const geolora::ParamsRadio& p, int16_t& codigo);

/* Bandera que levanta la ISR de DIO1. La consulta y la limpia. */
bool hayTrama();

/* Lee la última trama. Devuelve los bytes leídos (0 si no había nada bueno) y
 * rellena rssi (dBm) y snrQ4 (cuartos de dB). Deja la radio escuchando. */
size_t leer(uint8_t* out, size_t cap, int16_t& rssi, int8_t& snrQ4);

/* Transmite. `usarCad` = escuchar antes de hablar (§7.5); las ALERT que ya
 * han chocado CAD_MAX veces salen con usarCad = false. Deja la radio
 * escuchando pase lo que pase. */
ResultadoTx transmitir(const uint8_t* datos, uint8_t len, bool usarCad, int16_t& codigo);

/* Vuelve a modo recepción (lo normal es que no haga falta llamarla a mano). */
void escuchar();

/* Contador de tramas que el propio chip descartó por CRC de PHY malo: son
 * colisiones o ruido, no fallos del protocolo, y conviene no confundirlos. */
uint32_t crcMalos();

}  // namespace radio
