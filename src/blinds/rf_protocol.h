// rf_protocol.h - encoder for the 433 MHz blind remote protocol.
//
// Pure C++ (no Arduino dependencies) so it can be unit-tested on a PC.
// See rf-analysis/README.md for how the protocol was reverse engineered.
//
// Frame layout (MSB first, ~22.4 ms on air):
//
//   start | 24 data bits                                  | gap
//   -     | [blind ID 8 bits][0x65 8 bits][0x30+cmd 8 bits] | ____ (~23.6 ms)
//
// Every bit is 3 time units (T ~= 306 us) long and is sent LOW then HIGH:
//
//   bit 0:  _ _ -     LOW 2T, HIGH 1T
//   bit 1:  _ - -     LOW 1T, HIGH 2T
//
// The original remote repeats the frame every ~46 ms while a button is held.

#pragma once

#include <stddef.h>
#include <stdint.h>

namespace rfblinds {

enum Command : uint8_t {
  CMD_OPEN = 0,
  CMD_STOP = 1,
  CMD_CLOSE = 2,
  CMD_INVALID = 0xFF,
};

constexpr uint8_t FIXED_BYTE = 0x65;  // middle byte, identical for every blind
constexpr uint8_t CMD_BASE = 0x30;    // last byte = 0x30 + command
constexpr int FRAME_BITS = 24;

// One level held for a duration. Frames always alternate HIGH/LOW,
// starting with HIGH, which maps directly onto ESP32 RMT symbols.
struct Pulse {
  uint8_t level;  // 1 = carrier on, 0 = off
  uint16_t us;    // duration in microseconds
};

// start pulse + (LOW, HIGH) per bit + trailing gap
constexpr size_t FRAME_PULSES = 1 + 2 * FRAME_BITS + 1;

inline uint32_t makeCode(uint8_t blindId, Command cmd) {
  return (uint32_t(blindId) << 16) | (uint32_t(FIXED_BYTE) << 8) | uint32_t(CMD_BASE + cmd);
}

inline const char* commandName(Command cmd) {
  switch (cmd) {
    case CMD_OPEN:  return "open";
    case CMD_STOP:  return "stop";
    case CMD_CLOSE: return "close";
    default:        return "?";
  }
}

// Case-insensitive parse of "open" / "stop" / "close" (also "up" / "down").
inline Command parseCommand(const char* s) {
  if (!s) return CMD_INVALID;
  auto eq = [](const char* a, const char* b) {
    for (; *a && *b; ++a, ++b) {
      char ca = (*a >= 'A' && *a <= 'Z') ? char(*a + 32) : *a;
      if (ca != *b) return false;
    }
    return *a == *b;
  };
  if (eq(s, "open") || eq(s, "up")) return CMD_OPEN;
  if (eq(s, "stop")) return CMD_STOP;
  if (eq(s, "close") || eq(s, "down")) return CMD_CLOSE;
  return CMD_INVALID;
}

// Fill `out` (FRAME_PULSES entries) with one frame for `code`.
// Returns the number of pulses written.
inline size_t encodeFrame(uint32_t code, uint16_t unitUs, uint16_t gapUs, Pulse* out) {
  size_t n = 0;
  out[n++] = {1, unitUs};  // start pulse
  for (int i = FRAME_BITS - 1; i >= 0; --i) {
    const bool one = (code >> i) & 1u;
    out[n++] = {0, uint16_t(unitUs * (one ? 1 : 2))};
    out[n++] = {1, uint16_t(unitUs * (one ? 2 : 1))};
  }
  out[n++] = {0, gapUs};  // inter-frame gap, line stays LOW afterwards
  return n;
}

}  // namespace rfblinds
