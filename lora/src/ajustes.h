/* ============================================================================
 *  GEO-EXPO LoRa  ·  Configuración persistente en NVS  (DISENO §11)
 * ============================================================================
 *  Espacio `geolora` de Preferences. Claves: id, freq, sf, bw, cr, sync, pre,
 *  pwr, ttl, relay y rol.
 *
 *  Dos reglas del §11 que conviene no perder de vista:
 *
 *   · Lo que se lee de NVS se VALIDA con las MISMAS funciones puras que valida
 *     la consola (`validar()` de radio_params). Un valor corrupto no se aplica:
 *     se vuelve al de fábrica y se avisa con `EV NVS_INVALIDA`. Así una NVS a
 *     medio escribir no deja la placa sorda sin explicación.
 *   · Los cambios de la consola se aplican al instante pero NO se guardan
 *     solos: hace falta `RADIO SAVE`. La excepción es `ID <hex4>`, que se
 *     guarda al momento porque identifica la placa. De esta forma un
 *     experimento de banco no sobrevive a un reinicio por accidente.
 * ==========================================================================*/
#pragma once

#include <stdint.h>

#include "motor.h"
#include "radio_params.h"

namespace ajustes {

/* Lo que se guarda: los parámetros de radio y el modo de red. El ID va
 * aparte porque se guarda por su cuenta (§6). */
struct Guardado {
  geolora::ParamsRadio radio;
  uint8_t              ttl;
  bool                 relay;
  geolora::Rol         rol;
};

void begin();

/* --------------------------------------------------------------------------
 *  Radio y modo de red
 * ------------------------------------------------------------------------*/
/* Rellena `g` con lo de NVS. Devuelve:
 *    true  = había algo guardado y es válido
 *    false = no había nada, o lo que había NO pasó validar(): en ese caso `g`
 *            queda con los valores de FÁBRICA y `invalida` dice si fue por
 *            corrupción (para poder emitir EV NVS_INVALIDA). */
bool cargar(Guardado& g, bool& invalida);
bool guardar(const Guardado& g);
/* Borra las claves de radio/modo (RADIO DEFECTO deja de tener efecto tras un
 * reinicio si además no se guarda). */
void borrarRadio();

/* --------------------------------------------------------------------------
 *  ID de nodo (§6)
 * ------------------------------------------------------------------------*/
/* Override manual guardado, o 0 si no hay (o si el guardado es reservado, que
 * se trata como "no hay"). */
uint16_t idGuardado();
bool     guardarId(uint16_t id);   // false si el ID no es asignable
void     borrarId();               // "ID AUTO"

}  // namespace ajustes
