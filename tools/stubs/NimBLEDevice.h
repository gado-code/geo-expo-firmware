#pragma once
#include <Arduino.h>
#include <string>
namespace NIMBLE_PROPERTY { enum { NOTIFY = 1, WRITE = 2, WRITE_NR = 4 }; }
struct NimBLEAttValue { const char* c_str() const; size_t length() const; };
class NimBLECharacteristicCallbacks;
class NimBLECharacteristic {
 public:
  void setValue(uint8_t*, size_t);
  void notify();
  void setCallbacks(NimBLECharacteristicCallbacks*);
  NimBLEAttValue getValue();
};
class NimBLECharacteristicCallbacks {
 public:
  virtual ~NimBLECharacteristicCallbacks() {}
  virtual void onWrite(NimBLECharacteristic*) {}
};
class NimBLEService {
 public:
  NimBLECharacteristic* createCharacteristic(const char*, uint32_t);
  void start();
};
class NimBLEServerCallbacks;
class NimBLEServer {
 public:
  void setCallbacks(NimBLEServerCallbacks*);
  NimBLEService* createService(const char*);
  uint32_t getConnectedCount();
};
class NimBLEServerCallbacks {
 public:
  virtual ~NimBLEServerCallbacks() {}
  virtual void onConnect(NimBLEServer*) {}
  virtual void onDisconnect(NimBLEServer*) {}
};
class NimBLEAdvertising {
 public:
  void addServiceUUID(const char*);
  void setScanResponse(bool);
};
class NimBLEDevice {
 public:
  static void init(const char*);
  static void setPower(esp_power_level_t);
  static NimBLEServer* createServer();
  static NimBLEAdvertising* getAdvertising();
  static void startAdvertising();
};
