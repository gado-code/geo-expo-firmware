/* ============================================================================
 *  GEO-EXPO LoRa  ·  Caché de vistos  (ver vistos.h)
 * ==========================================================================*/
#include "vistos.h"

#include <string.h>

namespace geolora {

Vistos::Vistos() { limpiar(); }

void Vistos::limpiar() {
  memset(e_, 0, sizeof(e_));
  reemplazos_ = 0;
}

int Vistos::buscar(uint16_t src, uint16_t msgId, uint32_t ahoraMs) {
  for (int i = 0; i < CAPACIDAD; ++i) {
    Entrada& e = e_[i];
    if (!e.usada) continue;
    if (!viva(e, ahoraMs)) { e.usada = false; continue; }
    if (e.src == src && e.msgId == msgId) return i;
  }
  return -1;
}

int Vistos::anadir(uint16_t src, uint16_t msgId, uint32_t ahoraMs, uint32_t ventanaMs) {
  int idx = buscar(src, msgId, ahoraMs);
  if (idx < 0) {
    /* Primer hueco libre (o caducado); si no hay, la de mayor edad. */
    uint32_t edadMax = 0;
    int masVieja = 0;
    for (int i = 0; i < CAPACIDAD; ++i) {
      if (!viva(e_[i], ahoraMs)) { idx = i; break; }
      const uint32_t edad = ahoraMs - e_[i].tVisto;
      if (edad >= edadMax) { edadMax = edad; masVieja = i; }
    }
    if (idx < 0) { idx = masVieja; ++reemplazos_; }
  }
  Entrada& e = e_[idx];
  e.src = src;
  e.msgId = msgId;
  e.tVisto = ahoraMs;
  e.tFinVentana = ahoraMs + ventanaMs;
  e.usada = true;
  return idx;
}

size_t Vistos::vivas(uint32_t ahoraMs) const {
  size_t n = 0;
  for (int i = 0; i < CAPACIDAD; ++i) if (viva(e_[i], ahoraMs)) ++n;
  return n;
}

}  // namespace geolora
