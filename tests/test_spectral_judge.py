"""Unit tests for the spectral/acoustic compliance judge (PF-041/PF-042).

bench/spectral_judge.py turns the measurements render_oracle already computes
(band_gain_db, centroid_shift_oct, crest factor, tail_ms) into a verdict against
the prompt's category. These are pure-signal tests — no faust, no network — so
they run in `tools/check.sh fast` and give the judge teeth on the red cases the
corpus run cannot.

The named regression (PF-042): the old time-based branch read a "t60_s" key
render_oracle never emits, defaulted to 0.0, and subtracted 0.5 from EVERY
reverb/delay render regardless of the actual tail. TestTimeBasedVerdict pins the
fixed behaviour.
"""
import sys
from pathlib import Path

import numpy as np

ROOT = Path(__file__).parent.parent
sys.path.insert(0, str(ROOT / "bench"))

import spectral_judge as sj


def _sine(freq: float, n: int = 44100, sr: int = 44100) -> np.ndarray:
    return np.sin(2 * np.pi * freq * np.linspace(0, 1, n, endpoint=False))


class TestSpectralCentroid:
    def test_pure_sine_sits_at_its_frequency(self):
        assert abs(sj.compute_spectral_centroid(_sine(1000)) - 1000.0) < 50.0

    def test_silence_is_zero(self):
        assert sj.compute_spectral_centroid(np.zeros(44100)) == 0.0

    def test_stereo_folds_before_measuring(self):
        mono = _sine(1000)
        stereo = np.stack([mono, mono], axis=1)
        assert abs(sj.compute_spectral_centroid(stereo) - 1000.0) < 50.0


class TestTHD:
    def test_pure_sine_has_no_harmonics(self):
        assert sj.compute_total_harmonic_distortion(_sine(1000)) < 0.01

    def test_hard_clipped_sine_reads_positive(self):
        clipped = np.clip(_sine(1000), -0.5, 0.5)
        assert sj.compute_total_harmonic_distortion(clipped) > 0.1


# A plausible lowpass render: centroid dropped an octave, 4-8 kHz band cut hard.
LOWPASS_COMPLIANT = {
    "centroid_shift_oct": -1.0,
    "thd": 0.0,
    "centroid_out_hz": 2000.0,
    "band_gain_db": {"4000-8000": -24.0},
}
# Lowpass that leaks highs: centroid moved, but the 4-8 kHz band is near flat.
LOWPASS_LEAKED_HIGH = {
    "centroid_shift_oct": -0.5,
    "thd": 0.0,
    "centroid_out_hz": 2500.0,
    "band_gain_db": {"4000-8000": -3.0},
}
LOWPASS_CENTROID_UP = {
    "centroid_shift_oct": +0.7,
    "thd": 0.0,
    "centroid_out_hz": 6000.0,
    "band_gain_db": {"4000-8000": -0.5},
}


class TestFilterVerdict:
    def test_lowpass_compliant_scores_full(self):
        score, notes = sj.judge_patch_features("a lowpass filter", LOWPASS_COMPLIANT)
        assert score == 1.0, notes

    def test_lowpass_leaked_high_band_is_penalized(self):
        score, notes = sj.judge_patch_features("a lowpass filter", LOWPASS_LEAKED_HIGH)
        assert score < 1.0
        assert "4-8 kHz" in notes

    def test_lowpass_with_rising_centroid_is_penalized(self):
        score, notes = sj.judge_patch_features("a lowpass filter", LOWPASS_CENTROID_UP)
        assert score < 1.0
        assert "increased" in notes

    def test_highpass_compliant_scores_full(self):
        score, notes = sj.judge_patch_features("a highpass filter", {
            "centroid_shift_oct": +0.8,
            "band_gain_db": {"50-200": -25.0},
        })
        assert score == 1.0, notes

    def test_highpass_with_unattenuated_lows_is_penalized(self):
        score, notes = sj.judge_patch_features("a highpass filter", {
            "centroid_shift_oct": +0.3,
            "band_gain_db": {"50-200": -1.0},
        })
        assert score < 1.0
        assert "50-200 Hz" in notes


class TestTimeBasedVerdict:
    def test_reverb_with_long_tail_scores_full(self):
        score, notes = sj.judge_patch_features("a lush reverb", {"tail_ms": 500.0})
        assert score == 1.0, notes

    def test_reverb_with_short_tail_is_penalized(self):
        score, notes = sj.judge_patch_features("a lush reverb", {"tail_ms": 5.0})
        assert score < 1.0
        assert "decay tail" in notes

    def test_delay_with_no_tail_is_penalized(self):
        score, notes = sj.judge_patch_features("a delay", {"tail_ms": 0.0})
        assert score < 1.0


class TestDynamicsVerdict:
    def test_compressor_reducing_crest_scores_full(self):
        score, notes = sj.judge_patch_features(
            "a compressor", {"crest_in_db": 12.0, "crest_out_db": 9.0})
        assert score == 1.0, notes

    def test_compressor_not_reducing_crest_is_penalized(self):
        score, notes = sj.judge_patch_features(
            "a compressor", {"crest_in_db": 12.0, "crest_out_db": 12.5})
        assert score < 1.0
        assert "crest reduction" in notes

    def test_gate_increasing_crest_scores_full(self):
        score, notes = sj.judge_patch_features(
            "a noise gate", {"crest_in_db": 12.0, "crest_out_db": 16.0})
        assert score == 1.0, notes

    def test_gate_not_increasing_crest_is_penalized(self):
        score, notes = sj.judge_patch_features(
            "a noise gate", {"crest_in_db": 12.0, "crest_out_db": 11.0})
        assert score < 1.0
        assert "crest increase" in notes


class TestDistortionVerdict:
    def test_distortion_with_harmonics_scores_full(self):
        score, notes = sj.judge_patch_features("a distortion pedal", {"thd": 0.2})
        assert score == 1.0, notes

    def test_distortion_without_harmonics_is_penalized(self):
        score, notes = sj.judge_patch_features("a distortion pedal", {"thd": 0.001})
        assert score < 1.0
        assert "THD" in notes


class TestRobustness:
    def test_absent_features_never_fail(self):
        """No measurements means nothing to contradict — never a penalty."""
        score, notes = sj.judge_patch_features("a reverb delay", {})
        assert score == 1.0, notes

    def test_score_stays_in_unit_range(self):
        """Every branch firing at once must clamp to 0, not go negative."""
        worst = {
            "centroid_shift_oct": +2.0,
            "band_gain_db": {"4000-8000": 0.0},
            "tail_ms": 0.0,
            "crest_in_db": 12.0,
            "crest_out_db": 13.0,
            "thd": 0.0,
        }
        score, notes = sj.judge_patch_features(
            "lowpass compressor reverb distortion", worst)
        assert 0.0 <= score <= 1.0, notes
        assert score == 0.0
