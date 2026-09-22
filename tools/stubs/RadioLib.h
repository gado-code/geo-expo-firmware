#pragma once
#include <Arduino.h>
#include <SPI.h>
#define RADIOLIB_ERR_NONE 0
#define RADIOLIB_NC 0xFFFFFFFF
class Module {
 public:
  Module(uint32_t cs, uint32_t irq, uint32_t rst, uint32_t gpio = RADIOLIB_NC);
  Module(uint32_t cs, uint32_t irq, uint32_t rst, uint32_t gpio, SPIClass& spi);
};
class SX1262 {
 public:
  SX1262(Module* m);
  int16_t begin(float freq = 434.0, float bw = 125.0, uint8_t sf = 9, uint8_t cr = 7,
                uint8_t syncWord = 0x12, int8_t power = 10, uint16_t preambleLength = 8,
                float tcxoVoltage = 1.6, bool useRegulatorLDO = false);
  int16_t setDio2AsRfSwitch(bool enable = true);
  int16_t setCurrentLimit(float limit);
  void    setDio1Action(void (*func)(void));
  int16_t startReceive();
  int16_t startTransmit(uint8_t* data, size_t len, uint8_t addr = 0);
  int16_t finishTransmit();
  size_t  getPacketLength(bool update = true);
  int16_t readData(uint8_t* data, size_t len);
  float   getRSSI();
  float   getSNR();
};
