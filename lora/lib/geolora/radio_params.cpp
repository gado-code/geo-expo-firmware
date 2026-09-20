/* ============================================================================
 *  GEO-EXPO LoRa  ·  Parámetros de radio, banda, airtime y ocupación
 *                    (ver radio_params.h)
 * ==========================================================================*/
#include "radio_params.h"

#include <string.h>

#include "paquete.h"

namespace geolora {

ParamsRadio paramsDefecto() {
  ParamsRadio r;
  r.freqKHz = 921500;   // D1
  r.sf      = 11;       // D2 (preset NORMAL)
  r.bwKHz   = 500;
  r.cr      = 5;
  r.sync    = 0x12;     // D4: sync word privado estándar
  r.pre     = 16;       // §4.1: para que el CAD lo cace en E2
  r.pwr     = 14;       // moderada (25 mW)
  return r;
}

/* ------------------------------------------------------------- presets --*/
struct DefPreset { Preset p; const char* nombre; uint8_t sf; uint16_t bw; uint8_t cr; };
static const DefPreset PRESETS[] = {
  { Preset::RAPIDO, "RAPIDO",  9, 500, 5 },
  { Preset::NORMAL, "NORMAL", 11, 500, 5 },
  { Preset::LARGO,  "LARGO",  12, 500, 5 },
  { Preset::LAB125, "LAB125",  9, 125, 5 },
};
static const size_t N_PRESETS = sizeof(PRESETS) / sizeof(PRESETS[0]);

void aplicarPreset(Preset p, ParamsRadio& r) {
  for (size_t i = 0; i < N_PRESETS; ++i) {
    if (PRESETS[i].p == p) {
      r.sf = PRESETS[i].sf;
      r.bwKHz = PRESETS[i].bw;
      r.cr = PRESETS[i].cr;
      return;
    }
  }
}

Preset presetDe(const ParamsRadio& r) {
  for (size_t i = 0; i < N_PRESETS; ++i) {
    if (PRESETS[i].sf == r.sf && PRESETS[i].bw == r.bwKHz && PRESETS[i].cr == r.cr) return PRESETS[i].p;
  }
  return Preset::A_MEDIDA;
}

const char* nombrePreset(Preset p) {
  for (size_t i = 0; i < N_PRESETS; ++i) {
    if (PRESETS[i].p == p) return PRESETS[i].nombre;
  }
  return "A_MEDIDA";
}

static char mayus(char c) { return (c >= 'a' && c <= 'z') ? static_cast<char>(c - 'a' + 'A') : c; }

bool presetPorNombre(const char* nombre, Preset& p) {
  if (nombre == nullptr) return false;
  for (size_t i = 0; i < N_PRESETS; ++i) {
    const char* a = nombre;
    const char* b = PRESETS[i].nombre;
    while (*a != '\0' && *b != '\0' && mayus(*a) == *b) { ++a; ++b; }
    if (*a == '\0' && *b == '\0') { p = PRESETS[i].p; return true; }
  }
  return false;
}

/* ---------------------------------------------------------- validación --*/
bool bwValido(uint16_t bwKHz) { return bwKHz == 125 || bwKHz == 250 || bwKHz == 500; }

bool frecuenciaEnBanda(uint32_t freqKHz, uint16_t bwKHz) {
  /* Se compara 2·f ± bw para no perder el medio kHz de BW 125. */
  const uint64_t f2 = static_cast<uint64_t>(freqKHz) * 2u;
  if (f2 < static_cast<uint64_t>(bwKHz)) return false;
  return (f2 - bwKHz) >= static_cast<uint64_t>(BANDA_MIN_KHZ) * 2u &&
         (f2 + bwKHz) <= static_cast<uint64_t>(BANDA_MAX_KHZ) * 2u;
}

ErrorRadio validar(const ParamsRadio& r) {
  if (!bwValido(r.bwKHz)) return ErrorRadio::BW;
  if (!frecuenciaEnBanda(r.freqKHz, r.bwKHz)) return ErrorRadio::FREQ;
  if (r.sf < 7 || r.sf > 12) return ErrorRadio::SF;
  if (r.cr < 5 || r.cr > 8) return ErrorRadio::CR;
  /* 0x00 no es un sync word útil (RadioLib lo acepta, pero no hay red que lo
   * use y confunde con NVS vacía). */
  if (r.sync == 0x00) return ErrorRadio::SYNC;
  if (r.pre < PRE_MIN || r.pre > PRE_MAX) return ErrorRadio::PRE;
  if (r.pwr < PWR_MIN || r.pwr > PWR_MAX) return ErrorRadio::PWR;
  return ErrorRadio::OK;
}

const char* nombreErrorRadio(ErrorRadio e) {
  switch (e) {
    case ErrorRadio::OK:   return "OK";
    case ErrorRadio::FREQ: return "FREQ_FUERA_DE_BANDA_915_928";
    case ErrorRadio::SF:   return "SF_7_12";
    case ErrorRadio::BW:   return "BW_125_250_500";
    case ErrorRadio::CR:   return "CR_5_8";
    case ErrorRadio::SYNC: return "SYNC";
    case ErrorRadio::PRE:  return "PRE_8_64";
    case ErrorRadio::PWR:  return "PWR_-9_22";
    default:               return "?";
  }
}

bool parseMHz(const char* s, uint32_t& kHz) {
  if (s == nullptr || *s == '\0') return false;
  uint32_t entero = 0;
  uint32_t frac = 0;
  int nEnt = 0;
  int nFrac = 0;
  bool punto = false;
  for (const char* p = s; *p != '\0'; ++p) {
    if (*p == '.' && !punto) { punto = true; continue; }
    if (*p < '0' || *p > '9') return false;
    if (!punto) {
      if (++nEnt > 4) return false;           // 9999 MHz como mucho
      entero = entero * 10 + static_cast<uint32_t>(*p - '0');
    } else {
      if (++nFrac > 3) return false;          // resolución de 1 kHz
      frac = frac * 10 + static_cast<uint32_t>(*p - '0');
    }
  }
  if (nEnt == 0) return false;
  if (punto && nFrac == 0) return false;      // "921." no
  for (int i = nFrac; i < 3; ++i) frac *= 10;
  kHz = entero * 1000u + frac;
  return true;
}

size_t formatearMHz(uint32_t kHz, char* out, size_t cap) {
  char tmp[16];
  const uint32_t ent = kHz / 1000u;
  const uint32_t fr = kHz % 1000u;
  size_t n = 0;
  char dig[12];
  int nd = 0;
  uint32_t e = ent;
  do { dig[nd++] = static_cast<char>('0' + e % 10); e /= 10; } while (e != 0);
  while (nd > 0) tmp[n++] = dig[--nd];
  tmp[n++] = '.';
  tmp[n++] = static_cast<char>('0' + (fr / 100) % 10);
  tmp[n++] = static_cast<char>('0' + (fr / 10) % 10);
  tmp[n++] = static_cast<char>('0' + fr % 10);
  tmp[n] = '\0';
  if (out == nullptr || n + 1 > cap) return 0;
  memcpy(out, tmp, n + 1);
  return n;
}

/* ------------------------------------------------------------- airtime --*/
uint32_t simboloUs(const ParamsRadio& r) {
  if (r.bwKHz == 0) return 0;
  return static_cast<uint32_t>((static_cast<uint64_t>(1) << r.sf) * 1000u / r.bwKHz);
}

bool ldroActivo(const ParamsRadio& r) { return simboloUs(r) >= 16000u; }

uint32_t airtimeUs(const ParamsRadio& r, uint16_t bytesPhy) {
  const uint64_t tSim = simboloUs(r);
  const int32_t sf = r.sf;
  const int32_t de = ldroActivo(r) ? 1 : 0;
  const int32_t crc = 1;   // CRC de PHY activado (§4.1)
  const int32_t ih = 0;    // cabecera explícita
  const int32_t crIdx = static_cast<int32_t>(r.cr) - 4;   // 1..4 para 4/5..4/8
  const int32_t num = 8 * static_cast<int32_t>(bytesPhy) - 4 * sf + 28 + 16 * crc - 20 * ih;
  const int32_t den = 4 * (sf - 2 * de);
  int32_t simbCarga = 8;
  if (num > 0 && den > 0) simbCarga += ((num + den - 1) / den) * (crIdx + 4);
  /* T_pre = (N_pre + 4.25)·T_sím = (4·N_pre + 17)·T_sím / 4 */
  const uint64_t tPre = (4u * static_cast<uint64_t>(r.pre) + 17u) * tSim / 4u;
  return static_cast<uint32_t>(tPre + static_cast<uint64_t>(simbCarga) * tSim);
}

uint32_t airtimeMs(const ParamsRadio& r, uint16_t bytesPhy) {
  return (airtimeUs(r, bytesPhy) + 999u) / 1000u;
}

uint32_t intervaloMinimoRangeMs(const ParamsRadio& r) {
  const uint32_t us = airtimeUs(r, SOBRECARGA + 4) + airtimeUs(r, SOBRECARGA + 7);
  const uint32_t ms = (us * 10u + 999u) / 1000u;
  return ((ms + 99u) / 100u) * 100u;
}

/* ----------------------------------------------------------- ocupación --*/
Ocupacion::Ocupacion() { reiniciar(); }

void Ocupacion::reiniciar() {
  memset(cubos_, 0, sizeof(cubos_));
  segundo_ = 0;
  iniciado_ = false;
}

void Ocupacion::avanzar(uint32_t ahoraMs) {
  const uint32_t s = ahoraMs / 1000u;
  if (!iniciado_) { segundo_ = s; iniciado_ = true; return; }
  const uint32_t pasos = s - segundo_;
  if (pasos == 0) return;
  if (pasos >= 60u) {
    /* Más de un minuto sin actividad, o millis() dio la vuelta: se empieza
     * de cero (hacia el lado seguro: permite transmitir). */
    memset(cubos_, 0, sizeof(cubos_));
  } else {
    for (uint32_t i = 1; i <= pasos; ++i) cubos_[(segundo_ + i) % 60u] = 0;
  }
  segundo_ = s;
}

void Ocupacion::anotar(uint32_t ahoraMs, uint32_t airtime) {
  avanzar(ahoraMs);
  cubos_[segundo_ % 60u] += airtime;
}

uint32_t Ocupacion::usadoMs(uint32_t ahoraMs) {
  avanzar(ahoraMs);
  uint32_t suma = 0;
  for (int i = 0; i < 60; ++i) suma += cubos_[i];
  return suma;
}

bool Ocupacion::permite(uint32_t ahoraMs, uint32_t airtime) {
  return usadoMs(ahoraMs) + airtime <= LIMITE_MS;
}

uint32_t Ocupacion::porMil(uint32_t ahoraMs) {
  return usadoMs(ahoraMs) * 1000u / VENTANA_MS;
}

}  // namespace geolora
