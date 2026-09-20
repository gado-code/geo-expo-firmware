/* ============================================================================
 *  GEO-EXPO LoRa  ·  Pines y peculiaridades de la placa  (DISENO §2)
 * ============================================================================
 *  Todo lo que depende de la REVISIÓN de la placa vive aquí y sólo aquí. Así,
 *  si algún día llega una V4, se toca este archivo y nada más.
 *
 *  ── Heltec WiFi LoRa 32 V3 (ESP32-S3 + SX1262) ───────────────────────────
 *  Pines verificados contra tres fuentes (DISENO §2.1 y §17):
 *  `variants/heltec_wifi_lora_32_V3/pins_arduino.h` de arduino-esp32, el
 *  `variant.h` de Meshtastic para la V3 y la librería de ropg.
 *
 *  Dos trampas que cuestan una tarde si no se saben:
 *
 *   1. `pins_arduino.h` llama **DIO0** al GPIO14. El SX1262 NO TIENE DIO0:
 *      ese pin es **DIO1**, que es el que avisa de "paquete recibido". Aquí se
 *      llama por su nombre real.
 *   2. El OLED NO va en los `SDA`/`SCL` por defecto de la variante (41/42):
 *      va en 17/18, con un reset propio en 21 y alimentado por `Vext`.
 *
 *  DIO2 y DIO3 del SX1262 no salen a ningún GPIO: son internos y se manejan
 *  por SPI (`setDio2AsRfSwitch` y el `tcxoVoltage` de `begin()`).
 * ==========================================================================*/
#pragma once

#include <stdint.h>

#if defined(PLACA_HELTEC_V4)
/* ----------------------------------------------------------------------------
 *  V4: NO IMPLEMENTADA NI PROBADA (DISENO §2.2). Se deja el esqueleto para
 *  que quede escrito lo que habría que hacer, no para usarlo.
 *
 *  La V4 mantiene los pines del SX1262 (8–14), Vext 36, el OLED y el botón,
 *  pero añade un front-end de RF con PA externo (hasta 28 dBm) que hay que
 *  encender A MANO. Sin eso, la placa TRANSMITE Y NADIE LA OYE (hay un caso
 *  documentado en las discusiones de RadioLib, §17). Además la potencia del
 *  SX1262 ya no es la potencia de salida, así que el límite normativo del §3
 *  habría que recalcularlo.
 * --------------------------------------------------------------------------*/
#error "La V4 no está implementada: ver DISENO §2.2 antes de habilitarla."
#endif

namespace placa {

/* --- SX1262 (SPI + control) ----------------------------------------------*/
static const int PIN_LORA_NSS  = 8;    // `SS` en pins_arduino.h
static const int PIN_LORA_SCK  = 9;
static const int PIN_LORA_MOSI = 10;
static const int PIN_LORA_MISO = 11;
static const int PIN_LORA_RST  = 12;   // `RST_LoRa`
static const int PIN_LORA_BUSY = 13;   // `BUSY_LoRa`
static const int PIN_LORA_DIO1 = 14;   // ¡`DIO0` en pins_arduino.h! (ver cabecera)

/* DIO3 alimenta el TCXO a 1.8 V y DIO2 hace de conmutador de antena: los dos
 * se configuran por SPI, no por GPIO. */
static const float TCXO_V = 1.8f;

/* --- OLED SSD1306 128x64, I2C 0x3C --------------------------------------*/
static const int PIN_OLED_SDA = 17;
static const int PIN_OLED_SCL = 18;
static const int PIN_OLED_RST = 21;

/* --- Alimentación de periféricos ----------------------------------------*/
/* Vext: LOW = ENCENDIDO. Alimenta el OLED y, según Meshtastic, también el
 * "antenna boost" (sin confirmar en el esquemático). Por prudencia se deja
 * SIEMPRE encendido mientras la radio trabaja (§2.1). */
static const int PIN_VEXT = 36;

/* --- Señalización y control --------------------------------------------*/
static const int PIN_LED = 35;   // LED blanco
/* Botón PRG, activo a nivel BAJO. Es pin de arranque: no conviene pulsarlo
 * durante un reset, y el DTR del puerto serie lo baja (de ahí monitor_dtr = 0
 * en platformio.ini, §2.3). */
static const int PIN_BOTON = 0;

/* --- Batería (opcional, E1.x) -------------------------------------------*/
/* Meshtastic usa ADC_CTRL activo LOW y un multiplicador de ×4.9·1.045, pero la
 * POLARIDAD CAMBIA entre revisiones: hay que verificarla en la placa antes de
 * fiarse de la lectura. No se usa todavía. */
static const int PIN_BAT_ADC  = 1;
static const int PIN_BAT_CTRL = 37;

}  // namespace placa
