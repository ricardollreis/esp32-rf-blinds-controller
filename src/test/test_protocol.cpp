// Host-side test for src/blinds/rf_protocol.h.
//
// 1. Checks code generation and command parsing.
// 2. Renders the frames the ESP32 would transmit for every blind/command as a
//    48 kHz WAV that looks like an RTL-SDR AM recording, so it can be fed to
//    rf-analysis/decode_blinds.py and decoded exactly like the real remote.
//
// Build & run: see run_tests.sh

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <vector>

#include "../blinds/rf_protocol.h"

using namespace rfblinds;

static int failures = 0;
#define CHECK(cond)                                                    \
  do {                                                                 \
    if (!(cond)) {                                                     \
      std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
      ++failures;                                                      \
    }                                                                  \
  } while (0)

static void testCodes() {
  // Codes captured from the original remote (rf-analysis/README.md).
  CHECK(makeCode(0x11, CMD_OPEN) == 0x116530);
  CHECK(makeCode(0x11, CMD_STOP) == 0x116531);
  CHECK(makeCode(0x11, CMD_CLOSE) == 0x116532);
  CHECK(makeCode(0x14, CMD_CLOSE) == 0x146532);

  // Same result as the original Arduino Nano serial byte decoding.
  for (int b = 0; b < 0xFF; ++b) {
    const uint8_t id = b & 0x3F, cmd = (b & 0xC0) >> 6;
    if (cmd > CMD_CLOSE) continue;
    unsigned long nano = (unsigned long)id << 16;
    nano ^= 0x65UL << 8;
    nano ^= (unsigned long)(cmd + 0x30);
    CHECK(makeCode(id, Command(cmd)) == nano);
  }

  CHECK(parseCommand("open") == CMD_OPEN);
  CHECK(parseCommand("OPEN") == CMD_OPEN);
  CHECK(parseCommand("up") == CMD_OPEN);
  CHECK(parseCommand("Stop") == CMD_STOP);
  CHECK(parseCommand("close") == CMD_CLOSE);
  CHECK(parseCommand("down") == CMD_CLOSE);
  CHECK(parseCommand("opens") == CMD_INVALID);
  CHECK(parseCommand("ope") == CMD_INVALID);
  CHECK(parseCommand("") == CMD_INVALID);
  CHECK(parseCommand(nullptr) == CMD_INVALID);
}

static void testFrame() {
  Pulse p[FRAME_PULSES];
  const size_t n = encodeFrame(0x116530, 306, 23600, p);
  CHECK(n == FRAME_PULSES);
  CHECK(n % 2 == 0);
  // Strictly alternating levels starting HIGH (required for RMT symbols).
  for (size_t i = 0; i < n; ++i) CHECK(p[i].level == (i % 2 == 0 ? 1 : 0));
  CHECK(p[0].us == 306);
  CHECK(p[n - 1].us == 23600);
  uint32_t onAir = 0;
  for (size_t i = 0; i + 1 < n; ++i) onAir += p[i].us;
  CHECK(onAir == 306u * (1 + 3 * FRAME_BITS));  // 22.338 ms
  // First data bits of 0x11 = 0001: LOW2T HIGH1T x3, then LOW1T HIGH2T
  CHECK(p[1].us == 612 && p[2].us == 306);
  CHECK(p[7].us == 306 && p[8].us == 612);
}

// Write a mono 16-bit WAV that mimics an AM-demodulated SDR capture.
static bool writeWav(const char* path) {
  const int fs = 48000;
  std::vector<float> sig;
  std::mt19937 rng(1234);
  std::normal_distribution<float> noise(0.0f, 0.02f);

  auto silence = [&](double sec) { sig.insert(sig.end(), size_t(sec * fs), 0.0f); };
  silence(0.5);

  Pulse p[FRAME_PULSES];
  for (int b = 0x11; b <= 0x14; ++b) {
    for (int c = CMD_OPEN; c <= CMD_CLOSE; ++c) {
      const size_t n = encodeFrame(makeCode(b, Command(c)), 306, 23600, p);
      for (int rep = 0; rep < 5; ++rep) {
        double t = 0;
        for (size_t i = 0; i < n; ++i) {
          // accumulate exact time to avoid rounding drift
          const size_t s0 = size_t(t * fs / 1e6);
          t += p[i].us;
          const size_t s1 = size_t(t * fs / 1e6);
          sig.insert(sig.end(), s1 - s0, p[i].level ? 0.8f : 0.0f);
        }
      }
      silence(0.5);
    }
  }

  // One-pole low-pass (~5 kHz) to round the edges like the SDR audio path.
  float y = 0;
  const float a = 1.0f - std::exp(-2.0f * float(M_PI) * 5000.0f / fs);
  std::vector<int16_t> pcm(sig.size());
  for (size_t i = 0; i < sig.size(); ++i) {
    y += a * (sig[i] - y);
    float v = y + noise(rng);
    v = v > 1 ? 1 : (v < -1 ? -1 : v);
    pcm[i] = int16_t(v * 32767);
  }

  FILE* f = std::fopen(path, "wb");
  if (!f) return false;
  auto u32 = [&](uint32_t v) { std::fwrite(&v, 4, 1, f); };
  auto u16 = [&](uint16_t v) { std::fwrite(&v, 2, 1, f); };
  const uint32_t data = uint32_t(pcm.size() * 2);
  std::fwrite("RIFF", 1, 4, f); u32(36 + data); std::fwrite("WAVE", 1, 4, f);
  std::fwrite("fmt ", 1, 4, f); u32(16); u16(1); u16(1); u32(fs); u32(fs * 2); u16(2); u16(16);
  std::fwrite("data", 1, 4, f); u32(data);
  std::fwrite(pcm.data(), 2, pcm.size(), f);
  std::fclose(f);
  return true;
}

int main(int argc, char** argv) {
  testCodes();
  testFrame();
  if (argc > 1 && !writeWav(argv[1])) {
    std::fprintf(stderr, "could not write %s\n", argv[1]);
    return 1;
  }
  if (failures) {
    std::fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  std::printf("rf_protocol: all checks passed\n");
  return 0;
}
