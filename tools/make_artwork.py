#!/usr/bin/env python3
"""Render artwork cards (waveform + spectrogram) for every wav in artifacts/audio/.

Design notes (per the dataviz method):
  - Each card is a single-series magnitude view -> sequential color: ONE hue,
    dark -> light, on the plugin's own dark surface (#1e1e2e). No rainbow maps.
  - Text/axes wear muted ink, never the data color; grid/axes are recessive.
  - The dB range is FIXED across all files so cards are honestly comparable
    (the compressor genuinely looks squashed, the lowpass genuinely loses its top).

Usage: python tools/make_artwork.py
"""
import wave
from pathlib import Path

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np
from matplotlib.colors import LinearSegmentedColormap

AUDIO_DIR = Path("artifacts/audio")
OUT_DIR = Path("artifacts/images")

SURFACE = "#1e1e2e"
SURFACE_DEEP = "#11111b"
ACCENT = "#94e2d5"
INK_MUTED = "#a6adc8"

# Sequential ramp: surface-deep -> accent -> near-white (one hue family, dark->light)
SPEC_CMAP = LinearSegmentedColormap.from_list(
    "forge_teal", [SURFACE_DEEP, "#2a4d4a", ACCENT, "#f2fffc"])

DB_RANGE = (-90.0, 0.0)          # fixed across all cards
NFFT, HOP = 2048, 512

CAPTIONS = {
    "input_testsignal": "Reference input — 60 Hz→8 kHz sweep + noise bursts",
    "gain_processed": "gain.dsp — clean gain stage",
    "lowpass_processed": "lowpass.dsp — resonant low-pass (watch the top shave off)",
    "compressor_processed": "compressor.dsp — dynamics squashed on every burst",
    "chorus_processed": "chorus.dsp — modulated delay smear",
}


def read_wav(path: Path) -> tuple[np.ndarray, int]:
    with wave.open(str(path), "rb") as w:
        sr = w.getframerate()
        n = w.getnframes()
        data = np.frombuffer(w.readframes(n), dtype=np.int16)
        ch = w.getnchannels()
    x = data.reshape(-1, ch).mean(axis=1) / 32768.0
    return x, sr


def spectrogram_db(x: np.ndarray, sr: int) -> tuple[np.ndarray, float]:
    frames = np.lib.stride_tricks.sliding_window_view(x, NFFT)[::HOP]
    win = np.hanning(NFFT)
    spec = np.abs(np.fft.rfft(frames * win, axis=1)) ** 2
    spec_db = 10.0 * np.log10(spec / (NFFT ** 2) + 1e-12)
    return spec_db.T, len(x) / sr


def make_card(path: Path) -> Path:
    x, sr = read_wav(path)
    name = path.stem

    fig, (ax_w, ax_s) = plt.subplots(
        2, 1, figsize=(11, 6.2), height_ratios=[1, 2.2],
        facecolor=SURFACE, constrained_layout=True)

    # Waveform: min/max envelope, thin outline + translucent fill
    cols = 1400
    seg = max(1, len(x) // cols)
    trimmed = x[: seg * cols].reshape(cols, seg)
    t_axis = np.linspace(0, len(x) / sr, cols)
    ax_w.fill_between(t_axis, trimmed.min(axis=1), trimmed.max(axis=1),
                      color=ACCENT, alpha=0.85, linewidth=0)
    ax_w.set_facecolor(SURFACE)
    ax_w.set_ylim(-1.05, 1.05)
    ax_w.set_xlim(0, len(x) / sr)

    # Spectrogram
    spec, dur = spectrogram_db(x, sr)
    ax_s.imshow(spec, origin="lower", aspect="auto", cmap=SPEC_CMAP,
                vmin=DB_RANGE[0], vmax=DB_RANGE[1],
                extent=(0, dur, 0, sr / 2000.0))
    ax_s.set_ylim(0, 10)
    ax_s.set_ylabel("kHz", color=INK_MUTED, fontsize=9)
    ax_s.set_xlabel("seconds", color=INK_MUTED, fontsize=9)

    for ax in (ax_w, ax_s):
        for spine in ax.spines.values():
            spine.set_visible(False)
        ax.tick_params(colors=INK_MUTED, labelsize=8, length=0)
    ax_w.set_yticks([])
    ax_w.set_xticks([])

    fig.suptitle(f"PluginForge — {name.replace('_processed', '')}",
                 color="#ffffff", fontsize=14, x=0.02, ha="left")
    ax_w.set_title(CAPTIONS.get(name, name), color=INK_MUTED,
                   fontsize=10, loc="left", pad=6)

    out = OUT_DIR / f"{name}_card.png"
    fig.savefig(out, dpi=150, facecolor=SURFACE)
    plt.close(fig)
    return out


def main() -> None:
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    for wav in sorted(AUDIO_DIR.glob("*.wav")):
        print("card:", make_card(wav))


if __name__ == "__main__":
    main()
