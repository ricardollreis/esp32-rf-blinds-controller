#!/usr/bin/env python3
"""
Decode the 433 MHz blind remote frames captured in the WAV recordings.

The recordings were made with an RTL-SDR (AM demodulation, 48 kHz, mono,
16-bit). Carrier "on" shows up as a positive pulse in the audio.

Protocol (see README.md in this folder for the full analysis):

  * Base time unit T ~= 310 us.
  * Every bit lasts 3T and is sent as LOW then HIGH:
        bit 0 -> LOW 2T, HIGH 1T      ( _ _ - )
        bit 1 -> LOW 1T, HIGH 2T      ( _ - - )
  * A frame starts with a single 1T HIGH "start" pulse followed by
    24 bits, MSB first (~22.6 ms on air).
  * Payload = [ID][0x65][0x30 + cmd]  with cmd 0=open, 1=stop, 2=close.
  * The remote repeats the frame for as long as the button is held.

Because every bit ends with a HIGH, each bit has exactly one rising edge,
so the decoder only needs the ratio HIGH / (LOW + HIGH) of each bit
period. That makes it robust against threshold / AGC distortion.

Usage:
    python3 decode_blinds.py                       # decode every *.wav here
    python3 decode_blinds.py file.wav [...]        # decode given files
    python3 decode_blinds.py --plot                # also write PNG plots
    python3 decode_blinds.py --frames              # list every frame

Only numpy is required (matplotlib for --plot).
"""

import argparse
import glob
import os
import sys
import wave
from collections import Counter, OrderedDict

import numpy as np

FRAME_BITS = 24
FIXED_BYTE = 0x65
COMMANDS = {0x30: "OPEN", 0x31: "STOP", 0x32: "CLOSE"}

# A gap longer than this (in microseconds) ends a frame.
FRAME_GAP_US = 2500
# A frame needs at least this many HIGH pulses (start pulse + 24 bits).
MIN_PULSES = FRAME_BITS + 1
# Accepted range for the bit period (3T).
PERIOD_MIN_US = 600
PERIOD_MAX_US = 1500
# Rolling window for the adaptive threshold, and squelch level relative
# to the strongest signal swing in the file.
WINDOW_US = 2000
SQUELCH = 0.3


def load_wav(path):
    with wave.open(path, "rb") as w:
        if w.getsampwidth() != 2:
            raise ValueError("only 16-bit WAV files are supported")
        fs = w.getframerate()
        raw = w.readframes(w.getnframes())
        ch = w.getnchannels()
    x = np.frombuffer(raw, dtype="<i2").astype(np.float64)
    if ch > 1:
        x = x.reshape(-1, ch)[:, 0]
    return fs, x


def find_runs(x, fs):
    """Slice the signal into (level, start_sample, duration_us) runs.

    The SDR's AGC and audio high-pass make both the amplitude and the
    baseline drift between (and even within) frames, so a fixed threshold
    fails on repeated frames. Instead the threshold follows the midpoint
    of a rolling min/max window that always spans at least one full bit
    (~1 ms), and quiet stretches are squelched.
    """
    win = int(round(fs * WINDOW_US / 1e6)) | 1
    pad = np.pad(x, win // 2, mode="edge")
    view = np.lib.stride_tricks.sliding_window_view(pad, win)
    hi, lo = view.max(axis=1), view.min(axis=1)
    swing = hi - lo
    squelch = SQUELCH * np.percentile(swing, 99.5)
    b = ((x > (hi + lo) / 2) & (swing > squelch)).astype(np.int8)
    edges = np.flatnonzero(np.diff(b)) + 1
    if len(edges) < 2:
        return []
    levels = b[edges[:-1]]
    durs = np.diff(edges) / fs * 1e6
    return list(zip(levels.tolist(), edges[:-1].tolist(), durs.tolist()))


def group_bursts(runs):
    """Group runs into bursts separated by long LOW gaps."""
    bursts, cur = [], []
    for lvl, start, dur in runs:
        if lvl == 0 and dur > FRAME_GAP_US:
            if sum(1 for r in cur if r[0] == 1) >= MIN_PULSES:
                bursts.append(cur)
            cur = []
        else:
            cur.append((lvl, start, dur))
    if sum(1 for r in cur if r[0] == 1) >= MIN_PULSES:
        bursts.append(cur)
    return bursts


def _try_frame(burst, k, fs):
    """Try to decode a frame whose start pulse is burst[k]."""
    if k + 2 * FRAME_BITS >= len(burst):
        return None
    pairs = []  # (low_us, high_us) per bit
    for i in range(k + 1, k + 1 + 2 * FRAME_BITS, 2):
        lo, hi = burst[i], burst[i + 1]
        if lo[0] != 0 or hi[0] != 1:
            return None
        pairs.append((lo[2], hi[2]))

    periods = np.array([lo + hi for lo, hi in pairs])
    med = np.median(periods)
    # Every bit is 3T long; reject anything with inconsistent bit periods.
    if not (PERIOD_MIN_US <= med <= PERIOD_MAX_US):
        return None
    if np.any(np.abs(periods - med) > 0.3 * med):
        return None
    start = burst[k]
    if not (0.5 * med / 3 <= start[2] <= 1.6 * med / 3):
        return None

    ratios = np.array([hi / (lo + hi) for lo, hi in pairs])
    code = 0
    for r in ratios:
        code = (code << 1) | (1 if r > 0.5 else 0)

    last_hi = burst[k + 2 * FRAME_BITS]
    return {
        "code": code,
        "t_start": start[1] / fs,
        "t_unit_us": med / 3.0,
        "start_pulse_us": start[2],
        "length_us": (last_hi[1] - start[1]) / fs * 1e6 + last_hi[2],
        # Worst-case distance of a bit's HIGH ratio from the 0.5 decision
        # point (ideal: 2/3 - 1/2 = 0.17).
        "margin": float(np.min(np.abs(ratios - 0.5))),
    }


def decode_burst(burst, fs):
    """Find every frame inside a burst (noise may precede or follow it)."""
    frames, k = [], 0
    while k < len(burst):
        f = _try_frame(burst, k, fs) if burst[k][0] == 1 else None
        if f:
            frames.append(f)
            k += 2 * FRAME_BITS + 1
        else:
            k += 1
    return frames


def describe(code):
    blind_id = (code >> 16) & 0xFF
    fixed = (code >> 8) & 0xFF
    cmd = code & 0xFF
    ok = fixed == FIXED_BYTE and cmd in COMMANDS
    name = COMMANDS.get(cmd, "?")
    return blind_id, fixed, cmd, name, ok


def presses(frames, max_gap_s=0.25):
    """Group repeated frames of one button press (same blind ID, frames less
    than max_gap_s apart) and take a majority vote over the codes, so a
    single corrupted repeat does not show up as a separate press."""
    groups = []
    for f in frames:
        g = groups[-1] if groups else None
        if g and (g["frames"][-1]["code"] >> 16) == (f["code"] >> 16) \
                and f["t_start"] - g["frames"][-1]["t_start"] < max_gap_s:
            g["frames"].append(f)
        else:
            groups.append({"frames": [f]})
    out = []
    for g in groups:
        fr = g["frames"]
        votes = Counter(f["code"] for f in fr)
        code, n = votes.most_common(1)[0]
        gaps = np.diff([f["t_start"] for f in fr])
        out.append({"code": code, "t_first": fr[0]["t_start"], "repeats": len(fr),
                    "agree": n, "spacing_ms": float(np.min(gaps) * 1e3) if len(gaps) else None})
    return out


def plot_frame(path, fs, x, frame, out_png):
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    t0 = frame["t_start"] - 0.001
    t1 = t0 + frame["length_us"] / 1e6 + 0.002
    a, b = int(t0 * fs), int(t1 * fs)
    t = (np.arange(a, b) / fs - frame["t_start"]) * 1e3
    seg = x[a:b] / 32768.0

    blind_id, _, cmd, name, _ = describe(frame["code"])
    T = frame["t_unit_us"] / 1e3
    fig, ax = plt.subplots(figsize=(14, 3.2))
    ax.plot(t, seg, lw=0.8, color="#3b6ea5")
    bits = format(frame["code"], "024b")
    origin = frame["start_pulse_us"] / 1e3
    for k, bit in enumerate(bits):
        x0 = origin + k * 3 * T
        ax.axvline(x0, color="#999999", lw=0.5, ls=":")
        ax.text(x0 + 1.5 * T, 1.02, bit, ha="center", va="bottom", fontsize=9,
                transform=ax.get_xaxis_transform(),
                color="#c0392b" if bit == "1" else "#333333")
    for k in range(4):
        ax.axvline(origin + k * 24 * T, color="#666666", lw=1.0)
    ax.set_ylim(seg.min() - 0.05, seg.max() + 0.05)
    ax.set_xlabel("time from start pulse [ms]")
    ax.set_ylabel("AM level")
    ax.set_title("{} - code 0x{:06X} (ID 0x{:02X}, {})  T={:.0f} us".format(
        os.path.basename(path), frame["code"], blind_id, name, frame["t_unit_us"]), pad=18)
    fig.tight_layout()
    fig.savefig(out_png, dpi=110)
    plt.close(fig)


def analyse(path, show_frames=False, plot_dir=None):
    fs, x = load_wav(path)
    bursts = group_bursts(find_runs(x, fs))
    frames = [f for b in bursts for f in decode_burst(b, fs)]
    valid = [f for f in frames if describe(f["code"])[4]]

    print("=" * 78)
    print(os.path.basename(path))
    print("  {:.2f} s, {} Hz, {} bursts, {} decoded frames ({} valid)".format(
        len(x) / fs, fs, len(bursts), len(frames), len(valid)))

    if show_frames:
        for f in frames:
            blind_id, fixed, cmd, name, ok = describe(f["code"])
            print("    t={:7.3f}s  0x{:06X}  ID=0x{:02X} {:5s} T={:3.0f}us "
                  "len={:5.0f}us margin={:.2f}{}".format(
                      f["t_start"], f["code"], blind_id, name, f["t_unit_us"],
                      f["length_us"], f["margin"], "" if ok else "  (invalid)"))

    print("  Button presses:")
    for p in presses(valid):
        blind_id, _, cmd, name, _ = describe(p["code"])
        gap = "  min spacing {:.0f} ms".format(p["spacing_ms"]) if p["spacing_ms"] else ""
        print("    t={:7.3f}s  0x{:06X}  ID=0x{:02X}  {:5s}  {}/{} frames agree{}".format(
            p["t_first"], p["code"], blind_id, name, p["agree"], p["repeats"], gap))

    if valid:
        tu = np.array([f["t_unit_us"] for f in valid])
        sp = np.array([f["start_pulse_us"] for f in valid])
        ln = np.array([f["length_us"] for f in valid])
        print("  Timing: T = {:.0f} us (sd {:.0f}), start pulse {:.0f} us, "
              "frame {:.1f} ms".format(tu.mean(), tu.std(), np.median(sp), np.median(ln) / 1e3))

    if plot_dir and valid:
        os.makedirs(plot_dir, exist_ok=True)
        best = max(valid, key=lambda f: f["margin"])
        png = os.path.join(plot_dir, os.path.splitext(os.path.basename(path))[0] + ".png")
        plot_frame(path, fs, x, best, png)
        print("  Plot: " + os.path.relpath(png))

    return valid


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("files", nargs="*", help="WAV files (default: all in this folder)")
    ap.add_argument("--frames", action="store_true", help="list every decoded frame")
    ap.add_argument("--plot", action="store_true", help="write a PNG of one frame per file")
    args = ap.parse_args()

    files = args.files or sorted(glob.glob(os.path.join(here, "*.wav")))
    if not files:
        sys.exit("no WAV files found")

    summary = OrderedDict()
    for path in files:
        for f in analyse(path, args.frames, os.path.join(here, "plots") if args.plot else None):
            summary.setdefault(f["code"], Counter())[os.path.basename(path)] += 1

    print("=" * 78)
    print("All codes seen:")
    for code in sorted(summary):
        blind_id, _, cmd, name, _ = describe(code)
        print("  0x{:06X}  ID=0x{:02X}  {:5s}  frames={}".format(
            code, blind_id, name, sum(summary[code].values())))


if __name__ == "__main__":
    main()
