#pragma once
#include <Arduino.h>
#define HSPI 2
class SPIClass { public: SPIClass(int = 0); void begin(int = -1, int = -1, int = -1, int = -1); };
extern SPIClass SPI;
