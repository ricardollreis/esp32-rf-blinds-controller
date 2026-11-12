// rf_transmitter.h - drives a 433 MHz ASK/OOK transmitter module from an ESP32.
//
// By default the ESP32 RMT peripheral generates the pulses in hardware, so
// the timing is exact even while Wi-Fi is busy. Works with Arduino-ESP32
// core 2.x (legacy IDF RMT driver) and 3.x (new rmtInit/rmtWrite API).
// Set RF_USE_RMT to 0 in config.h to fall back to plain bit-banging.

#pragma once

#include <Arduino.h>

#include "rf_protocol.h"

class RfTransmitter {
 public:
  bool begin(int pin, uint16_t unitUs, uint16_t gapUs);
  // Send `code` (24 bits) `repeats` times. Blocks for ~repeats * 46 ms.
  bool send(uint32_t code, uint8_t repeats);

 private:
  bool sendFrame(const rfblinds::Pulse* pulses, size_t count);

  int pin_ = -1;
  uint16_t unitUs_ = 306;
  uint16_t gapUs_ = 23600;
};
