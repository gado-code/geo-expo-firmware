/* ============================================================================
 *  GEO-EXPO LoRa  ·  Parámetros de radio, banda, airtime y ocupación
 *                    (DISENO §3, §4, §7.6)
 * ============================================================================
 *  ── Banda (Colombia, Res. ANE 105/2020 Anexo 1) ──────────────────────────
 *  Sólo 915–928 MHz es de uso libre; 902–915 MHz NO (y la Res. ANE 28/2026 lo
 *  da al enlace ascendente móvil). TODO el canal, centro ± BW/2, tiene que
 *  caer dentro: 915.0 MHz con BW 500 se sale por abajo y se RECHAZA. Por eso
 *  la frecuencia por defecto es 921.5 MHz (decisión D1) y no 915.0.
 *  Esto es una nota prudente, no asesoría legal (ver DISENO §3 y §16).
 *
 *  ── Perfil por defecto (D2) ──────────────────────────────────────────────
 *  SF11 / BW 500 kHz / CR 4/5, sync 0x12 (privado, D4), preámbulo 16,
 *  +14 dBm. El ancho de 500 kHz es el que encaja en la categoría de
 *  "modulación digital" de la norma; 125/250 kHz quedan para pruebas cortas
 *  de laboratorio (preset LAB125).
 *
 *  La frecuencia se guarda en kHz ENTEROS (921500) para validar la banda sin
 *  errores de coma flotante; RadioLib la recibe en MHz (freqKHz / 1000.0f).
 *
 *  ── Airtime ──────────────────────────────────────────────────────────────
 *  Fórmula de Semtech (AN1200.13) con cabecera explícita, CRC de PHY y LDRO
 *  automático (T_sím >= 16 ms, igual criterio que RadioLib), calculada en
 *  microsegundos ENTEROS: con BW 125/250/500 kHz y SF >= 7 el tiempo de
 *  símbolo es exacto en µs y la tabla del §4.2 sale al microsegundo.
 * ==========================================================================*/
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace geolora {

static const uint32_t BANDA_MIN_KHZ = 915000;
static const uint32_t BANDA_MAX_KHZ = 928000;
static const int8_t   PWR_MIN       = -9;
static const int8_t   PWR_MAX       = 22;
static const int8_t   PWR_AVISO     = 14;   // por encima, la consola avisa
static const uint16_t PRE_MIN       = 8;
static const uint16_t PRE_MAX       = 64;

struct ParamsRadio {
  uint32_t freqKHz;   // centro del canal
  uint8_t  sf;        // 7..12
  uint16_t bwKHz;     // 125 / 250 / 500
  uint8_t  cr;        // denominador de 4/x: 5..8 (lo que espera RadioLib)
  uint8_t  sync;      // sync word de 1 byte (0x12 privado)
  uint16_t pre;       // símbolos de preámbulo
  int8_t   pwr;       // dBm en el SX1262
};

/* Valores de fábrica: NORMAL a 921.5 MHz, sync 0x12, preámbulo 16, +14 dBm. */
ParamsRadio paramsDefecto();

/* --------------------------------------------------------------------------
 *  Presets (§4.1). Sólo fijan SF/BW/CR; freq, sync, preámbulo y potencia se
 *  conservan.
 * ------------------------------------------------------------------------*/
enum class Preset : uint8_t { RAPIDO, NORMAL, LARGO, LAB125, A_MEDIDA };
void        aplicarPreset(Preset p, ParamsRadio& r);
Preset      presetDe(const ParamsRadio& r);      // A_MEDIDA si no coincide ninguno
const char* nombrePreset(Preset p);
bool        presetPorNombre(const char* nombre, Preset& p);  // sin distinguir mayúsculas

/* --------------------------------------------------------------------------
 *  Validación: la MISMA para la consola, para lo que se lee de NVS y para el
 *  arranque. Devuelve el primer campo inválido.
 * ------------------------------------------------------------------------*/
enum class ErrorRadio : uint8_t { OK, FREQ, SF, BW, CR, SYNC, PRE, PWR };
ErrorRadio  validar(const ParamsRadio& r);
const char* nombreErrorRadio(ErrorRadio e);
bool        bwValido(uint16_t bwKHz);
/* ¿Cabe el canal completo [f - bw/2, f + bw/2] en 915–928 MHz? */
bool        frecuenciaEnBanda(uint32_t freqKHz, uint16_t bwKHz);

/* "921.5" / "921" / "927.75" -> kHz. Hasta 3 decimales, sin signo. */
bool parseMHz(const char* s, uint32_t& kHz);
/* kHz -> "921.500" (3 decimales). Devuelve longitud. */
size_t formatearMHz(uint32_t kHz, char* out, size_t cap);

/* --------------------------------------------------------------------------
 *  Airtime.
 * ------------------------------------------------------------------------*/
uint32_t simboloUs(const ParamsRadio& r);               // 2^SF / BW
bool     ldroActivo(const ParamsRadio& r);              // T_sím >= 16 ms
uint32_t airtimeUs(const ParamsRadio& r, uint16_t bytesPhy);
uint32_t airtimeMs(const ParamsRadio& r, uint16_t bytesPhy);   // redondeo hacia ARRIBA

/* Intervalo mínimo de RANGE para no pasar del 10 % de ocupación contando el
 * PING propio y el PONG del otro extremo (§7.6): 10·(T_PING + T_PONG),
 * redondeado hacia arriba a 100 ms. NORMAL -> 4000 ms. */
uint32_t intervaloMinimoRangeMs(const ParamsRadio& r);

/* --------------------------------------------------------------------------
 *  Presupuesto de ocupación autoimpuesto (§7.6): <= 10 % en cualquier
 *  ventana de 60 s para el tráfico que no es ALERT/ACK.
 *
 *  60 cubos de 1 s con los ms de aire transmitidos en cada segundo: memoria
 *  fija (240 B), coste constante, resolución de 1 s (de sobra para un límite
 *  de 6 s por minuto). Cuando millis() da la vuelta (49 días) se pierde como
 *  mucho un minuto de historia: el error va hacia PERMITIR, nunca bloquea.
 * ------------------------------------------------------------------------*/
class Ocupacion {
 public:
  static const uint32_t VENTANA_MS = 60000;
  static const uint32_t LIMITE_MS  = 6000;    // 10 % de 60 s

  Ocupacion();
  void     reiniciar();
  void     anotar(uint32_t ahoraMs, uint32_t airtimeMs);
  uint32_t usadoMs(uint32_t ahoraMs);                        // último minuto
  bool     permite(uint32_t ahoraMs, uint32_t airtimeMs);   // usado + nuevo <= límite
  uint32_t porMil(uint32_t ahoraMs);                         // ‰ del último minuto

 private:
  void     avanzar(uint32_t ahoraMs);
  uint32_t cubos_[60];
  uint32_t segundo_;     // segundo (ahora/1000) del cubo más reciente
  bool     iniciado_;
};

}  // namespace geolora
