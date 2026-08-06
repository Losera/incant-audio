#!/usr/bin/env python3
"""
bench/spectral_judge.py — Spectral & Acoustic Feature Compliance Judge.

Evaluates rendered Faust audio feature outputs (from bench/render_oracle.py) against
semantic expectations inferred from prompt categories (filters, dynamics, time-based, distortion).
"""

import sys
import numpy as np
from pathlib import Path
from typing import Dict, Any, Optional, Tuple

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "bench"))

try:
    import render_oracle
except ImportError:
    render_oracle = None


def compute_spectral_centroid(signal: np.ndarray, sr: int = 44100) -> float:
    """Calculate spectral centroid in Hz for a mono/stereo audio signal."""
    if signal.ndim > 1:
        signal = np.mean(signal, axis=1)
        
    spectrum = np.abs(np.fft.rfft(signal))
    freqs = np.fft.rfftfreq(len(signal), 1.0 / sr)
    
    total_energy = np.sum(spectrum)
    if total_energy < 1e-9:
        return 0.0
        
    return float(np.sum(freqs * spectrum) / total_energy)


def compute_total_harmonic_distortion(signal: np.ndarray, fundamental_freq: float = 1000.0, sr: int = 44100) -> float:
    """Calculate THD for a fundamental test frequency."""
    if signal.ndim > 1:
        signal = np.mean(signal, axis=1)
        
    spectrum = np.abs(np.fft.rfft(signal))
    freqs = np.fft.rfftfreq(len(signal), 1.0 / sr)
    
    # Locate fundamental bin
    fund_bin = np.argmin(np.abs(freqs - fundamental_freq))
    fund_mag = spectrum[fund_bin]
    
    if fund_mag < 1e-6:
        return 0.0
        
    # Harmonics 2..5
    harmonic_mags = []
    for h in range(2, 6):
        h_bin = np.argmin(np.abs(freqs - (fundamental_freq * h)))
        if h_bin < len(spectrum):
            harmonic_mags.append(spectrum[h_bin] ** 2)
            
    if not harmonic_mags:
        return 0.0
        
    return float(np.sqrt(np.sum(harmonic_mags)) / fund_mag)


def judge_patch_features(prompt_text: str, features: Dict[str, Any]) -> Tuple[float, str]:
    """
    Judge acoustic compliance of patch features against prompt intent.
    Returns (score 0.0-1.0, detailed explanation).

    Each branch subtracts only when a measured feature contradicts the prompt's
    category. Absence of a measurement (key not present) never reads as a pass or
    a fail on its own — the caller supplies what it measured.
    """
    text = prompt_text.lower()
    score = 1.0
    notes = []

    # Filter compliance: centroid shift is the coarse signal, band_gain_db the
    # shape check — a lowpass whose 4-8 kHz band is not actually attenuated leaks
    # highs even when the centroid happens to have moved.
    if "lowpass" in text:
        centroid_shift = features.get("centroid_shift_oct", 0.0)
        if centroid_shift > 0.1:
            score -= 0.4
            notes.append("Expected spectral centroid reduction for lowpass filter, but centroid increased.")
        else:
            notes.append(f"Lowpass centroid shift: {centroid_shift:.2f} octaves.")
        band = features.get("band_gain_db")
        if band:
            high = band.get("4000-8000", 0.0)
            if high > -10:
                score -= 0.3
                notes.append(f"Lowpass expected the 4-8 kHz band attenuated, but it sits at {high:+.1f} dB.")

    elif "highpass" in text:
        centroid_shift = features.get("centroid_shift_oct", 0.0)
        if centroid_shift < -0.1:
            score -= 0.4
            notes.append("Expected spectral centroid increase for highpass filter, but centroid decreased.")
        else:
            notes.append(f"Highpass centroid shift: {centroid_shift:.2f} octaves.")
        band = features.get("band_gain_db")
        if band:
            low = band.get("50-200", 0.0)
            if low > -10:
                score -= 0.3
                notes.append(f"Highpass expected the 50-200 Hz band attenuated, but it sits at {low:+.1f} dB.")

    # Time-based compliance. render_oracle measures tail_ms (Tail.tail_ms), not a
    # "t60_s" seconds key — reading the wrong name made this branch see 0.0 for
    # every reverb/delay render and falsely subtract 0.5 (PF-042).
    if any(k in text for k in ["reverb", "delay", "echo"]):
        if "tail_ms" in features:
            tail_ms = features["tail_ms"]
            if tail_ms < 50.0:
                score -= 0.5
                notes.append(f"Time-based effect produced insufficient decay tail ({tail_ms:.0f} ms).")
            else:
                notes.append(f"Decay tail: {tail_ms:.0f} ms.")

    # Dynamics compliance via crest factor (peak/RMS, dB). A compressor/limiter
    # must reduce crest; a gate/expander must increase it. Only judged when the
    # caller actually measured crest.
    if ("crest_in_db" in features) and ("crest_out_db" in features):
        crest_delta = features["crest_out_db"] - features["crest_in_db"]
        if any(k in text for k in ["compressor", "limiter"]):
            if crest_delta >= -0.5:
                score -= 0.3
                notes.append(f"Compressor expected crest reduction, but crest changed by {crest_delta:+.1f} dB.")
            else:
                notes.append(f"Crest reduced by {-crest_delta:.1f} dB (compression active).")
        elif any(k in text for k in ["gate", "expander"]):
            if crest_delta <= 0.5:
                score -= 0.3
                notes.append(f"Gate/expander expected crest increase, but crest changed by {crest_delta:+.1f} dB.")
            else:
                notes.append(f"Crest increased by {crest_delta:+.1f} dB (gating active).")

    # Distortion compliance. Deliberately lenient: THD at the 1 kHz fundamental
    # is a weak measure on a broadband probe, so only an explicit distortion word
    # with essentially zero harmonic content reads as a miss.
    if any(k in text for k in ["distortion", "distort", "saturat", "overdrive", "clipper"]):
        if "thd" in features and features["thd"] < 0.005:
            score -= 0.2
            notes.append(f"Distortion expected harmonic content, but THD is only {features['thd']:.4f}.")

    score = max(0.0, min(1.0, score))
    return score, "; ".join(notes) or "Patch output satisfies baseline acoustic criteria."


if __name__ == "__main__":
    dummy_signal = np.sin(2 * np.pi * 1000 * np.linspace(0, 1, 44100))
    c = compute_spectral_centroid(dummy_signal)
    thd = compute_total_harmonic_distortion(dummy_signal)
    print(f"Test signal — Spectral Centroid: {c:.1f} Hz, THD: {thd:.4f}")
