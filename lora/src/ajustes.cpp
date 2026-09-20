/* ============================================================================
 *  GEO-EXPO LoRa  ·  Configuración persistente en NVS  (ver ajustes.h)
 * ==========================================================================*/
#include "ajustes.h"

#include <Preferences.h>

#include "identidad.h"

namespace ajustes {

using namespace geolora;

namespace {

const char* ESPACIO = "geolora";
Preferences prefs;

/* Marca de que la configuración de radio se ha guardado alguna vez. Sin esto
 * no se puede distinguir "no hay nada" de "hay un 0 guardado". */
const char* K_MARCA = "ok";
const char* K_FREQ  = "freq";
const char* K_SF    = "sf";
const char* K_BW    = "bw";
const char* K_CR    = "cr";
const char* K_SYNC  = "sync";
const char* K_PRE   = "pre";
const char* K_PWR   = "pwr";
const char* K_TTL   = "ttl";
const char* K_RELAY = "relay";
const char* K_ROL   = "rol";
const char* K_ID    = "id";

Guardado deFabrica() {
  Guardado g;
  g.radio = paramsDefecto();
  g.ttl = 0;        // E1 de salida (§8.3): nadie repite hasta que se pida
  g.relay = false;
  g.rol = Rol::NORMAL;
  return g;
}

}  // namespace

void begin() { /* Preferences se abre y cierra en cada operación. */ }

bool cargar(Guardado& g, bool& invalida) {
  invalida = false;
  g = deFabrica();
  if (!prefs.begin(ESPACIO, true /*sólo lectura*/)) return false;
  const bool hay = prefs.getBool(K_MARCA, false);
  Guardado leido = deFabrica();
  if (hay) {
    leido.radio.freqKHz = prefs.getUInt(K_FREQ, leido.radio.freqKHz);
    leido.radio.sf      = prefs.getUChar(K_SF, leido.radio.sf);
    leido.radio.bwKHz   = prefs.getUShort(K_BW, leido.radio.bwKHz);
    leido.radio.cr      = prefs.getUChar(K_CR, leido.radio.cr);
    leido.radio.sync    = prefs.getUChar(K_SYNC, leido.radio.sync);
    leido.radio.pre     = prefs.getUShort(K_PRE, leido.radio.pre);
    leido.radio.pwr     = static_cast<int8_t>(prefs.getChar(K_PWR, leido.radio.pwr));
    leido.ttl           = prefs.getUChar(K_TTL, leido.ttl);
    leido.relay         = prefs.getBool(K_RELAY, leido.relay);
    const uint8_t rol   = prefs.getUChar(K_ROL, static_cast<uint8_t>(leido.rol));
    leido.rol = rol <= static_cast<uint8_t>(Rol::PASARELA) ? static_cast<Rol>(rol) : Rol::NORMAL;
  }
  prefs.end();
  if (!hay) return false;
  /* La MISMA validación que usa la consola (§11): si algo no cuadra, fábrica. */
  if (validar(leido.radio) != ErrorRadio::OK || leido.ttl > TTL_MAX) {
    invalida = true;
    return false;
  }
  g = leido;
  return true;
}

bool guardar(const Guardado& g) {
  if (validar(g.radio) != ErrorRadio::OK || g.ttl > TTL_MAX) return false;
  if (!prefs.begin(ESPACIO, false)) return false;
  prefs.putUInt(K_FREQ, g.radio.freqKHz);
  prefs.putUChar(K_SF, g.radio.sf);
  prefs.putUShort(K_BW, g.radio.bwKHz);
  prefs.putUChar(K_CR, g.radio.cr);
  prefs.putUChar(K_SYNC, g.radio.sync);
  prefs.putUShort(K_PRE, g.radio.pre);
  prefs.putChar(K_PWR, g.radio.pwr);
  prefs.putUChar(K_TTL, g.ttl);
  prefs.putBool(K_RELAY, g.relay);
  prefs.putUChar(K_ROL, static_cast<uint8_t>(g.rol));
  prefs.putBool(K_MARCA, true);   // el último, para que la marca implique todo
  prefs.end();
  return true;
}

void borrarRadio() {
  if (!prefs.begin(ESPACIO, false)) return;
  prefs.remove(K_MARCA);
  prefs.end();
}

uint16_t idGuardado() {
  if (!prefs.begin(ESPACIO, true)) return 0;
  const uint16_t id = prefs.getUShort(K_ID, 0);
  prefs.end();
  /* Un ID reservado guardado (NVS a medias, o una versión anterior) se trata
   * como "no hay override": mejor volver a la MAC que quedarse muerto. */
  return idAsignable(id) ? id : 0;
}

bool guardarId(uint16_t id) {
  if (!idAsignable(id)) return false;
  if (!prefs.begin(ESPACIO, false)) return false;
  prefs.putUShort(K_ID, id);
  prefs.end();
  return true;
}

void borrarId() {
  if (!prefs.begin(ESPACIO, false)) return;
  prefs.remove(K_ID);
  prefs.end();
}

}  // namespace ajustes
