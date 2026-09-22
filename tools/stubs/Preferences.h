#pragma once
// Stub SOLO para tools/comprobar-sintaxis.sh (ver el aviso de ese script).
#include <Arduino.h>
class Preferences {
 public:
  bool     begin(const char* name, bool readOnly = false, const char* partition = nullptr);
  void     end();
  size_t   putUShort(const char* key, uint16_t value);
  uint16_t getUShort(const char* key, uint16_t defaultValue = 0);
};
