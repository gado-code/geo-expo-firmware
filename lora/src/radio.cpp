/* ============================================================================
 *  GEO-EXPO LoRa  ·  Capa de radio (implementación)   (DISENO §12.3)
 * ==========================================================================*/
#include "radio.h"

#include <Arduino.h>
#include <RadioLib.h>
#include <SPI.h>
#include <math.h>

#include "carga.h"    // snrAQ4()
#include "placa.h"

namespace radio {
namespace {

/* Module(NSS, IRQ, RST, BUSY). OJO: el tercer pin del constructor es el de
 * interrupción y en el SX1262 es DIO1; `pins_arduino.h` de la Heltec V3 lo
 * llama DIO0, que en este chip NO EXISTE (ver placa.h). */
SX1262 g_radio = new Module(placa::PIN_LORA_NSS, placa::PIN_LORA_DIO1,
                            placa::PIN_LORA_RST, placa::PIN_LORA_BUSY);

volatile bool g_bandera = false;   // la levanta la ISR
bool          g_viva    = false;
uint32_t      g_crcMalos = 0;

/* La ISR no puede hablar por SPI ni imprimir: sólo levanta la bandera. En el
 * ESP32 tiene que estar en IRAM porque puede dispararse con la caché de flash
 * ocupada. */
void IRAM_ATTR alDispararseDio1() { g_bandera = true; }

/* Traduce los parámetros del motor a llamadas de RadioLib. La frecuencia se
 * guarda en kHz enteros y RadioLib la quiere en MHz. */
int16_t aplicarParams(const geolora::ParamsRadio& p) {
  int16_t st;
  if ((st = g_radio.setFrequency(p.freqKHz / 1000.0f))        != RADIOLIB_ERR_NONE) return st;
  if ((st = g_radio.setBandwidth((float)p.bwKHz))             != RADIOLIB_ERR_NONE) return st;
  if ((st = g_radio.setSpreadingFactor(p.sf))                 != RADIOLIB_ERR_NONE) return st;
  if ((st = g_radio.setCodingRate(p.cr))                      != RADIOLIB_ERR_NONE) return st;
  if ((st = g_radio.setSyncWord(p.sync))                      != RADIOLIB_ERR_NONE) return st;
  if ((st = g_radio.setPreambleLength(p.pre))                 != RADIOLIB_ERR_NONE) return st;
  if ((st = g_radio.setOutputPower(p.pwr))                    != RADIOLIB_ERR_NONE) return st;
  return RADIOLIB_ERR_NONE;
}

}  // namespace

bool begin(const geolora::ParamsRadio& p, int16_t& codigo) {
  /* El SPI de la V3 no es el de por defecto: hay que decirle los pines. */
  SPI.begin(placa::PIN_LORA_SCK, placa::PIN_LORA_MISO, placa::PIN_LORA_MOSI,
            placa::PIN_LORA_NSS);

  codigo = g_radio.begin(p.freqKHz / 1000.0f, (float)p.bwKHz, p.sf, p.cr, p.sync,
                         p.pwr, p.pre, placa::TCXO_V, /*useRegulatorLDO=*/false);
  if (codigo != RADIOLIB_ERR_NONE) {
    g_viva = false;
    return false;
  }

  /* DIO2 conmuta la antena y DIO3 alimenta el TCXO: los dos son internos del
   * SX1262 y se mandan por SPI, no por GPIO (placa.h). Sin esto la placa
   * "transmite" pero no sale nada por la antena. */
  g_radio.setDio2AsRfSwitch(true);
  g_radio.setCRC(2);                 // CRC de PHY de 2 bytes (aparte del nuestro)
  g_radio.setRxBoostedGainMode(true);
  g_radio.setPacketReceivedAction(alDispararseDio1);

  g_viva = true;
  escuchar();
  return true;
}

bool viva() { return g_viva; }

bool aplicar(const geolora::ParamsRadio& p, int16_t& codigo) {
  if (!g_viva) { codigo = RADIOLIB_ERR_CHIP_NOT_FOUND; return false; }
  g_radio.standby();
  codigo = aplicarParams(p);
  escuchar();
  return codigo == RADIOLIB_ERR_NONE;
}

bool hayTrama() {
  if (!g_bandera) return false;
  g_bandera = false;
  return true;
}

size_t leer(uint8_t* out, size_t cap, int16_t& rssi, int8_t& snrQ4) {
  if (!g_viva) return 0;

  size_t n = g_radio.getPacketLength();
  if (n == 0 || n > cap) {
    /* Trama absurda o más larga que nuestro búfer: se tira, pero hay que
     * vaciar el chip igualmente o la siguiente lectura sale corrida. */
    g_radio.readData(out, cap);
    escuchar();
    return 0;
  }

  const int16_t st = g_radio.readData(out, n);
  /* RSSI y SNR se leen DESPUÉS de readData(): se refieren a esa trama. */
  rssi  = (int16_t)lroundf(g_radio.getRSSI());
  snrQ4 = geolora::snrAQ4(g_radio.getSNR());
  escuchar();

  if (st == RADIOLIB_ERR_CRC_MISMATCH) {
    /* Lo tiró el CRC del propio chip: es ruido o una colisión, no un fallo
     * del protocolo. Se cuenta aparte para no confundirlo con los rechazos
     * de nuestra validación. */
    ++g_crcMalos;
    return 0;
  }
  return (st == RADIOLIB_ERR_NONE) ? n : 0;
}

ResultadoTx transmitir(const uint8_t* datos, uint8_t len, bool usarCad, int16_t& codigo) {
  codigo = RADIOLIB_ERR_NONE;
  if (!g_viva) { codigo = RADIOLIB_ERR_CHIP_NOT_FOUND; return ResultadoTx::ERROR; }

  if (usarCad) {
    const int16_t cad = g_radio.scanChannel();
    /* El CAD también termina en DIO1: hay que bajar la bandera o el loop()
     * creería que ha llegado una trama. */
    g_bandera = false;
    if (cad != RADIOLIB_CHANNEL_FREE) {
      escuchar();
      return ResultadoTx::OCUPADO;
    }
  }

  /* TX bloqueante: aceptable en E1 (el peor caso del preset LARGO son ~1,7 s).
   * Si en E2 estorba, se pasa a startTransmit() + bandera de fin (§12.3). */
  codigo = g_radio.transmit(datos, len);
  g_bandera = false;       // el fin de transmisión también dispara DIO1
  escuchar();
  return (codigo == RADIOLIB_ERR_NONE) ? ResultadoTx::OK : ResultadoTx::ERROR;
}

void escuchar() {
  if (!g_viva) return;
  g_radio.startReceive();
}

uint32_t crcMalos() { return g_crcMalos; }

}  // namespace radio
