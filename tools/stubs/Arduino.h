#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <ctype.h>
#include <stdlib.h>
#define ESP_ARDUINO_VERSION_MAJOR 2
#define IRAM_ATTR
#define RTC_NOINIT_ATTR
#define LOW 0
#define HIGH 1
#define OUTPUT 1
#define INPUT_PULLUP 2
#define INPUT_PULLDOWN 3
#define SERIAL_8N1 0x800001c
#define TWO_PI 6.283185307179586f
#define HALF_PI 1.5707963267948966f
typedef enum { ADC_0db, ADC_2_5db, ADC_6db, ADC_11db } adc_attenuation_t;
typedef enum { ESP_PWR_LVL_P9 = 7 } esp_power_level_t;
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
inline void portENTER_CRITICAL(portMUX_TYPE*) {}
inline void portEXIT_CRITICAL(portMUX_TYPE*) {}
unsigned long millis();
void delay(unsigned long);
void pinMode(int, int);
int digitalRead(int);
void digitalWrite(int, int);
void analogWrite(int, int);
void analogReadResolution(int);
void analogSetPinAttenuation(int, adc_attenuation_t);
uint32_t analogReadMilliVolts(int);
void ledcSetup(uint8_t, double, uint8_t);
void ledcAttachPin(int, uint8_t);
void ledcWrite(uint8_t, uint32_t);
void ledcWriteTone(uint8_t, double);
void attachInterrupt(int, void(*)(void), int);
struct HardwareSerial {
  void begin(unsigned long, uint32_t = 0, int = -1, int = -1);
  int available();
  int read();
  void flush();
  int printf(const char*, ...);
  void println(const char* = "");
  void print(const char*);
  operator bool() const { return true; }
};
extern HardwareSerial Serial;
extern HardwareSerial Serial1;
extern HardwareSerial Serial2;
struct EspClass { void restart(); };
extern EspClass ESP;
