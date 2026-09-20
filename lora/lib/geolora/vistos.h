/* ============================================================================
 *  GEO-EXPO LoRa  ·  Caché de "vistos" (src, msgId)  (DISENO §7.4, §8.1)
 * ============================================================================
 *  Es lo que impide entregar dos veces el mismo mensaje (un reintento cuyo
 *  ACK se perdió) y lo que corta los bucles de la inundación en E2.
 *
 *    · 128 entradas fijas {src, msgId, tVisto, tFinVentana} (≈ 1.5 KiB).
 *    · Vida de 15 min DESDE LA ÚLTIMA VEZ que se renovó: mayor que la ventana
 *      de reintentos de una ALERT (10 min), si no un reintento tardío se
 *      entregaría dos veces.
 *    · Llena -> se reemplaza la entrada MÁS ANTIGUA (la de mayor edad).
 *    · Aritmética unsigned con millis(): "edad = ahora - t" sobrevive a la
 *      vuelta de los 49 días (hay test).
 *    · msgId da la vuelta 0xFFFF -> 0x0000: son claves distintas y ya está;
 *      con 15 min de vida harían falta 65536 mensajes de un mismo origen en
 *      15 min para confundir uno viejo con uno nuevo.
 *
 *  tFinVentana es un añadido de la implementación (ver README, desviaciones):
 *  marca hasta cuándo una copia REENVIADA (hops >= 1) se considera parte de
 *  la MISMA inundación que la primera que vimos. Pasado ese instante, una
 *  copia nueva es un REINTENTO del origen y el motor la vuelve a reenviar y
 *  a confirmar (sin volver a entregarla).
 * ==========================================================================*/
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace geolora {

class Vistos {
 public:
  static const uint16_t CAPACIDAD = 128;
  static const uint32_t VIDA_MS   = 15UL * 60UL * 1000UL;

  struct Entrada {
    uint16_t src;
    uint16_t msgId;
    uint32_t tVisto;        // última vez que se añadió o renovó
    uint32_t tFinVentana;   // fin de la ventana de "misma inundación"
    bool     usada;
  };

  Vistos();
  void limpiar();

  /* Índice de la entrada viva con esa clave, o -1. Las caducadas no cuentan
   * (y se liberan al encontrarlas). */
  int buscar(uint16_t src, uint16_t msgId, uint32_t ahoraMs);
  bool contiene(uint16_t src, uint16_t msgId, uint32_t ahoraMs) { return buscar(src, msgId, ahoraMs) >= 0; }

  /* Añade la clave (o la renueva si ya estaba) con una ventana de inundación
   * de `ventanaMs` desde ahora. Si no hay hueco, reemplaza la más antigua.
   * Devuelve el índice usado. */
  int anadir(uint16_t src, uint16_t msgId, uint32_t ahoraMs, uint32_t ventanaMs);

  Entrada&       entrada(int i) { return e_[i]; }
  const Entrada& entrada(int i) const { return e_[i]; }

  size_t   vivas(uint32_t ahoraMs) const;
  uint32_t reemplazos() const { return reemplazos_; }

 private:
  bool viva(const Entrada& e, uint32_t ahoraMs) const {
    return e.usada && (ahoraMs - e.tVisto) < VIDA_MS;
  }
  Entrada  e_[CAPACIDAD];
  uint32_t reemplazos_;
};

}  // namespace geolora
