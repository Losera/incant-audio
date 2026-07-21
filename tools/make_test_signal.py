#!/usr/bin/env python3
"""Synthesize the reference test signal for artifact rendering.

6 seconds, stereo, 44.1kHz/16-bit:
  - an exponential sine sweep 60 Hz -> 8 kHz (exposes filters: lowpass visibly
    shaves the top of the spectrogram)
  - rhythmic noise bursts every 0.75s (exposes dynamics: compressor visibly
    squashes them; chorus smears them)

Usage: python tools/make_test_signal.py [out.wav]
"""
import sys
import wave
from pathlib import Path

import numpy as np

SR = 44100
DUR = 6.0
F0, F1 = 60.0, 8000.0


def main() -> None:
    out = Path(sys.argv[1] if len(sys.argv) > 1 else
               "artifacts/audio/input_testsignal.wav")
    out.parent.mkdir(parents=True, exist_ok=True)

    t = np.arange(int(SR * DUR)) / SR

    # Exponential sweep: phase = 2*pi*F0*(k^t - 1)/ln(k), k = (F1/F0)^(1/DUR)
    k = (F1 / F0) ** (1.0 / DUR)
    phase = 2.0 * np.pi * F0 * (np.power(k, t) - 1.0) / np.log(k)
    sweep = 0.45 * np.sin(phase)

    # Noise bursts: 90ms of shaped white noise every 0.75s
    rng = np.random.default_rng(20260719)
    bursts = np.zeros_like(t)
    burst_len = int(0.09 * SR)
    env = np.hanning(burst_len * 2)[burst_len:]          # sharp attack, decaying tail
    for start_s in np.arange(0.0, DUR, 0.75):
        i = int(start_s * SR)
        seg = min(burst_len, len(t) - i)
        bursts[i:i + seg] += 0.5 * env[:seg] * rng.standard_normal(seg)

    mono = sweep + bursts
    mono /= np.max(np.abs(mono)) * 1.1                   # ~ -1 dBFS headroom

    stereo = np.stack([mono, mono])                      # identical L/R keeps analysis simple
    pcm = (stereo.T * 32767).astype(np.int16)

    with wave.open(str(out), "wb") as w:
        w.setnchannels(2)
        w.setsampwidth(2)
        w.setframerate(SR)
        w.writeframes(pcm.tobytes())

    print(f"wrote {out} ({DUR}s stereo {SR}Hz)")


if __name__ == "__main__":
    main()
