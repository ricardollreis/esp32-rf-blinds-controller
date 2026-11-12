// config.h - hardware and behaviour settings for the ESP32 RF blind controller.

#pragma once

#include <stdint.h>

// ---------------------------------------------------------------------------
// Hardware
// ---------------------------------------------------------------------------

// GPIO connected to the DATA pin of the 433 MHz transmitter module.
// GPIO 4 is free on practically every ESP32 dev board and is not a
// strapping pin. Avoid GPIO 0, 2, 5, 12, 15 (strapping) and 6-11 (flash).
#ifndef RF_TX_PIN
#define RF_TX_PIN 4
#endif

// Optional status LED (blinks while transmitting). -1 to disable.
// Most ESP32 DevKit boards have a blue LED on GPIO 2.
#ifndef STATUS_LED_PIN
#define STATUS_LED_PIN 2
#endif

// 1 = generate the pulses with the RMT peripheral (recommended, exact timing
// even with Wi-Fi active). 0 = bit-bang with delayMicroseconds().
#ifndef RF_USE_RMT
#define RF_USE_RMT 1
#endif

// ---------------------------------------------------------------------------
// RF protocol timing (measured from the original remote, see rf-analysis/)
// ---------------------------------------------------------------------------

#define RF_UNIT_US 306      // base time unit T
#define RF_GAP_US 23600     // silence after each frame (frame period ~46 ms)
#define RF_REPEATS 5        // frames sent per command (remote: ~5 per press)

// ---------------------------------------------------------------------------
// Blinds
// ---------------------------------------------------------------------------

struct BlindConfig {
  const char* name;  // shown in the web UI / API
  uint8_t id;        // first byte of the RF code
};

// IDs captured from the original remote (channels 1-4).
static const BlindConfig BLINDS[] = {
    {"Blind 1", 0x11},
    {"Blind 2", 0x12},
    {"Blind 3", 0x13},
    {"Blind 4", 0x14},
};
static const int NUM_BLINDS = sizeof(BLINDS) / sizeof(BLINDS[0]);

// Delay between blinds when a command is sent to "all" (ms).
#define ALL_BLINDS_DELAY_MS 100

// ---------------------------------------------------------------------------
// Serial (USB) interface - compatible with the original Arduino Nano version
// ---------------------------------------------------------------------------

#define SERIAL_BAUD 57600

// ---------------------------------------------------------------------------
// Wi-Fi / network
// ---------------------------------------------------------------------------
// Put your credentials in secrets.h (copy secrets.example.h). secrets.h is
// git-ignored. Without it Wi-Fi is disabled and only USB serial is used.

#if __has_include("secrets.h")
#include "secrets.h"
#endif

#ifndef WIFI_SSID
#define WIFI_SSID ""
#endif
#ifndef WIFI_PASSWORD
#define WIFI_PASSWORD ""
#endif
// mDNS host name -> http://rfblinds.local/
#ifndef HOSTNAME
#define HOSTNAME "rfblinds"
#endif
// Password for over-the-air firmware updates. Empty = OTA disabled.
#ifndef OTA_PASSWORD
#define OTA_PASSWORD ""
#endif
// Optional token required by the HTTP API (?token=... or X-Token header).
// Empty = no authentication (fine on a trusted home network).
#ifndef API_TOKEN
#define API_TOKEN ""
#endif
