#!/usr/bin/env bash
# Host-side tests for the RF encoder (no ESP32 needed).
#
#   1. builds and runs test_protocol.cpp (unit checks)
#   2. renders every blind/command as a simulated SDR recording
#   3. decodes that recording with rf-analysis/decode_blinds.py and checks
#      that all 12 codes come back exactly as the original remote sends them
#
# Requires: g++ (C++11), python3 + numpy
set -euo pipefail

here="$(cd "$(dirname "$0")" && pwd)"
root="$(cd "$here/../.." && pwd)"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

g++ -std=c++11 -Wall -Wextra -Werror -O2 -o "$tmp/test_protocol" "$here/test_protocol.cpp"
"$tmp/test_protocol" "$tmp/simulated.wav"

python3 - "$root/rf-analysis" "$tmp/simulated.wav" <<'PY'
import sys
from collections import Counter
sys.path.insert(0, sys.argv[1])
import decode_blinds as d

fs, x = d.load_wav(sys.argv[2])
frames = [f for b in d.group_bursts(d.find_runs(x, fs)) for f in d.decode_burst(b, fs)]
got = Counter(f["code"] for f in frames)
want = Counter({(i << 16) | 0x6500 | (0x30 + c): 5 for i in range(0x11, 0x15) for c in range(3)})
if got != want:
    print("decoded:", {hex(k): v for k, v in sorted(got.items())})
    print("expected:", {hex(k): v for k, v in sorted(want.items())})
    sys.exit("FAIL: simulated transmission did not decode as expected")
t = sum(f["t_unit_us"] for f in frames) / len(frames)
print("decoder: {} frames, all 12 codes x5 decoded correctly (T = {:.0f} us)".format(len(frames), t))
PY
echo "OK"
