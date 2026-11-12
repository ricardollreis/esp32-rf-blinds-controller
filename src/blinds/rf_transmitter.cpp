#include "rf_transmitter.h"

#include "config.h"

#if __has_include(<esp_arduino_version.h>)
#include <esp_arduino_version.h>
#endif
#ifndef ESP_ARDUINO_VERSION_MAJOR
#define ESP_ARDUINO_VERSION_MAJOR 1
#endif

#if RF_USE_RMT && ESP_ARDUINO_VERSION_MAJOR < 3
#include "driver/rmt.h"
static const rmt_channel_t kRmtChannel = RMT_CHANNEL_0;
#endif

using rfblinds::Pulse;

// Frames alternate HIGH/LOW starting with HIGH, so every two pulses form one
// RMT symbol: start + 24 bits + gap = 50 pulses = 25 symbols, which fits in a
// single RMT memory block on every ESP32 variant (48 or 64 symbols).
static constexpr size_t kSymbols = rfblinds::FRAME_PULSES / 2;
static_assert(rfblinds::FRAME_PULSES % 2 == 0, "frame must have an even number of pulses");

bool RfTransmitter::begin(int pin, uint16_t unitUs, uint16_t gapUs) {
  pin_ = pin;
  unitUs_ = unitUs;
  gapUs_ = gapUs;

  pinMode(pin_, OUTPUT);
  digitalWrite(pin_, LOW);

#if RF_USE_RMT && ESP_ARDUINO_VERSION_MAJOR >= 3
  // 1 MHz tick -> durations in microseconds.
  if (!rmtInit(pin_, RMT_TX_MODE, RMT_MEM_NUM_BLOCKS_1, 1000000)) {
    return false;
  }
#elif RF_USE_RMT
  rmt_config_t cfg = RMT_DEFAULT_CONFIG_TX((gpio_num_t)pin_, kRmtChannel);
  cfg.clk_div = 80;  // 80 MHz APB / 80 = 1 us per tick
  cfg.tx_config.carrier_en = false;
  cfg.tx_config.idle_output_en = true;
  cfg.tx_config.idle_level = RMT_IDLE_LEVEL_LOW;
  if (rmt_config(&cfg) != ESP_OK) return false;
  if (rmt_driver_install(kRmtChannel, 0, 0) != ESP_OK) return false;
#endif
  return true;
}

bool RfTransmitter::send(uint32_t code, uint8_t repeats) {
  if (pin_ < 0) return false;
  Pulse pulses[rfblinds::FRAME_PULSES];
  const size_t n = rfblinds::encodeFrame(code, unitUs_, gapUs_, pulses);
  bool ok = true;
  for (uint8_t i = 0; i < repeats; ++i) {
    ok &= sendFrame(pulses, n);
  }
  return ok;
}

bool RfTransmitter::sendFrame(const Pulse* p, size_t count) {
  (void)count;  // always FRAME_PULSES
#if RF_USE_RMT && ESP_ARDUINO_VERSION_MAJOR >= 3
  rmt_data_t symbols[kSymbols];
  for (size_t i = 0; i < kSymbols; ++i) {
    symbols[i].level0 = p[2 * i].level;
    symbols[i].duration0 = p[2 * i].us;
    symbols[i].level1 = p[2 * i + 1].level;
    symbols[i].duration1 = p[2 * i + 1].us;
  }
  // Blocking write; one frame takes ~46 ms including the gap.
  return rmtWrite(pin_, symbols, kSymbols, 200);
#elif RF_USE_RMT
  rmt_item32_t items[kSymbols];
  for (size_t i = 0; i < kSymbols; ++i) {
    items[i].level0 = p[2 * i].level;
    items[i].duration0 = p[2 * i].us;
    items[i].level1 = p[2 * i + 1].level;
    items[i].duration1 = p[2 * i + 1].us;
  }
  return rmt_write_items(kRmtChannel, items, kSymbols, true) == ESP_OK;
#else
  // Bit-bang fallback. delayMicroseconds() is a busy wait on the ESP32;
  // Wi-Fi runs on the other core, so jitter is normally only a few us.
  for (size_t i = 0; i < count; ++i) {
    digitalWrite(pin_, p[i].level ? HIGH : LOW);
    if (p[i].us > 10000) {
      delay(p[i].us / 1000);
      delayMicroseconds(p[i].us % 1000);
    } else {
      delayMicroseconds(p[i].us);
    }
  }
  digitalWrite(pin_, LOW);
  return true;
#endif
}
