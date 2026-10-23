#!/usr/bin/env python3
"""Generate synthetic machine-sound WAVs for the DSP unit tests.

No third-party deps (uses the stdlib `wave` module). All files are mono,
16-bit PCM at 16 kHz to match MHN_SAMPLE_RATE_HZ.

Scenarios (each is "a machine humming" plus a specific fault):
  normal.wav     : line hum (120 Hz) + harmonics + light broadband noise.
  rattle.wav     : normal + intermittent broadband clicks (loose part).
  shifted.wav    : hum fundamental moved 120 -> 138 Hz (imbalance/speed drift).
  broadband.wav  : normal + raised broadband noise floor (bearing wear-ish).

These are deliberately simple and deterministic (fixed seed) so the C test
harness can assert a stable health-score ordering. They are NOT real machine
recordings; see docs/HARDWARE.md for what real data is needed to make any
accuracy claim.
"""
import argparse
import math
import os
import random
import struct
import wave

SR = 16000


def _write_wav(path, samples):
    with wave.open(path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(SR)
        frames = bytearray()
        for s in samples:
            v = int(max(-1.0, min(1.0, s)) * 32767.0)
            frames += struct.pack("<h", v)
        w.writeframes(bytes(frames))


def _hum(n, fundamental, amp=0.35, harmonics=(1, 2, 3, 4), noise=0.01, rng=None):
    rng = rng or random.Random(0)
    out = []
    for i in range(n):
        t = i / SR
        v = 0.0
        for h in harmonics:
            v += (amp / h) * math.sin(2 * math.pi * fundamental * h * t)
        v += noise * (rng.random() * 2 - 1)
        out.append(v)
    return out


def gen_normal(seconds, rng):
    return _hum(int(seconds * SR), 120.0, rng=rng)


def gen_broadband(seconds, rng):
    base = _hum(int(seconds * SR), 120.0, rng=rng)
    return [v + 0.08 * (rng.random() * 2 - 1) for v in base]


def gen_shifted(seconds, rng):
    # Fundamental drifts up; harmonics move with it.
    return _hum(int(seconds * SR), 138.0, rng=rng)


def gen_rattle(seconds, rng):
    base = _hum(int(seconds * SR), 120.0, rng=rng)
    n = len(base)
    # Add short broadband bursts a few times per second (a loose part tapping).
    burst_len = int(0.01 * SR)
    i = 0
    while i < n:
        if rng.random() < 0.02:
            for j in range(burst_len):
                if i + j < n:
                    base[i + j] += 0.5 * (rng.random() * 2 - 1)
            i += burst_len
        else:
            i += int(0.01 * SR)
    return base


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("-o", "--outdir", default="test/wav")
    ap.add_argument("-s", "--seconds", type=float, default=6.0)
    args = ap.parse_args()
    os.makedirs(args.outdir, exist_ok=True)

    rng = random.Random(1234)
    sets = {
        "normal": gen_normal(args.seconds, rng),
        "rattle": gen_rattle(args.seconds, rng),
        "shifted": gen_shifted(args.seconds, rng),
        "broadband": gen_broadband(args.seconds, rng),
    }
    for name, samples in sets.items():
        path = os.path.join(args.outdir, name + ".wav")
        _write_wav(path, samples)
        print(f"wrote {path} ({len(samples)} samples, {len(samples)/SR:.1f}s)")


if __name__ == "__main__":
    main()
