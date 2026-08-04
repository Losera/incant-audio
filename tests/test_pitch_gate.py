"""The first semantic gate this project has: did the instrument play the note
it was sent?

Every other gate in bench/render_oracle.py answers "is this patch broken" --
NaN, silence, runaway gain, a delay whose feedback got dropped. None of them
can tell a saw playing 440 Hz from one playing 110 Hz; both are perfectly
finite, non-silent, bounded audio. The pitch gate closes that hole: send MIDI
note 45 through host/tests/OfflineRenderTest --capture (which now plays it --
see runCapture in that file) and assert the fundamental lands within
PITCH_TOLERANCE_CENTS of 110.0 Hz.

TWO CLASSES OF TEST, split for the same reason tests/test_render_oracle.py
splits: the estimator (autocorrelation + parabolic interpolation, pure numpy)
needs no Faust and no build, so it belongs in `fast`. Locating and driving the
capture binary needs a built host/tests/OfflineRenderTest, so it is
`@pytest.mark.integration` and runs in `tools/check.sh full`, after the build
step -- putting it in an unmarked class here would make it silently skip in
`fast` (no binary yet) and then never run again, which is the exact dead-control
failure CLAUDE.md's "a control counts only once it has been seen failing" rule
exists to catch.
"""
import subprocess
import sys
from pathlib import Path

import numpy as np
import pytest

ROOT = Path(__file__).parent.parent
sys.path.insert(0, str(ROOT / "bench"))

import render_oracle as ro  # noqa: E402
from p6_capture import find_capture_binary  # noqa: E402  (one binary-finding rule)

SR = ro.SR


def _tone(freq_hz: float, seconds: float = 1.0, sr: int = SR,
         harmonics: tuple[int, ...] = (1,)) -> np.ndarray:
    """A deterministic sum of harmonics of `freq_hz`, amplitude 1/k per harmonic."""
    t = np.arange(int(sr * seconds)) / sr
    return sum(np.sin(2 * np.pi * freq_hz * k * t) / k for k in harmonics)


class TestEstimator:
    """Pure numpy. No Faust, no build -- runs in `tools/check.sh fast`."""

    def test_sine_at_110_is_110(self):
        f0, conf = ro.fundamental_hz(_tone(110.0))
        assert abs(ro.cents(f0, 110.0)) < 1.0
        assert conf > 0.9

    def test_missing_fundamental_does_not_read_an_octave_up(self):
        # Harmonics 2..10 of 110 Hz, with NO 110 Hz component at all. This is
        # the estimator's own red case: an FFT-peak estimator reports 220 Hz
        # here (the loudest bin), because the 2nd harmonic of a sawish tone
        # commonly outweighs the fundamental. Verified directly below against
        # fft_peak_hz() -- the autocorrelation estimator must not make the
        # same mistake.
        y = _tone(110.0, harmonics=tuple(range(2, 11)))
        f0, conf = ro.fundamental_hz(y)
        assert conf > ro.PITCH_CONFIDENCE_MIN
        assert abs(ro.cents(f0, 110.0)) < 5.0, (
            f"ACF read {f0:.2f} Hz -- an octave-up misread would land near 220 Hz")
        # And confirm the trap is real: the FFT peak DOES land an octave up,
        # which is exactly why fundamental_hz() is not built on argmax(|FFT|).
        assert abs(ro.cents(ro.fft_peak_hz(y), 220.0)) < 5.0

    def test_220_is_1200_cents_from_110(self):
        assert abs(ro.cents(220.0, 110.0) - 1200.0) < 1e-6

    def test_noise_is_not_evaluated(self):
        rng = np.random.default_rng(0)
        y = rng.standard_normal(SR)  # 1s of white noise: no periodicity
        f0, conf = ro.fundamental_hz(y)
        assert conf < ro.PITCH_CONFIDENCE_MIN
        # And the full pitch_of() path reports "not evaluated" rather than a
        # number -- absence of a claim must not read as a passed claim.
        p = ro.pitch_of(y, midi_note=45, sr=SR)
        assert not p.evaluated
        assert p.why

    def test_note_45_is_exactly_110(self):
        # Pinned against FaustEngine::noteOn's 440*2**((n-69)/12) formula.
        assert ro.note_hz(45) == pytest.approx(110.0, abs=1e-9)
        assert ro.note_hz(69) == pytest.approx(440.0, abs=1e-9)


def _build_or_skip() -> Path:
    binary = find_capture_binary()
    if binary is None:
        pytest.skip("OfflineRenderTest binary not built (run tools/check.sh full)")
    return binary


def _capture(binary: Path, dsp: Path, wav: Path, note: int | None = None,
            timeout_s: int = 60) -> dict:
    args = [str(binary), "--capture", str(dsp), str(wav)]
    if note is not None:
        args += ["--note", str(note)]
    proc = subprocess.run(args, capture_output=True, text=True, timeout=timeout_s)
    for raw in proc.stdout.splitlines():
        if raw.startswith("CAPTURE_OK"):
            return dict(tok.split("=", 1) for tok in raw.split()[1:])
    pytest.fail(f"no CAPTURE_OK from {args}: {proc.stdout}\n{proc.stderr}")


@pytest.mark.integration
class TestCaptureGate:
    """Needs the built capture binary. Runs after the build, in `check.sh full`."""

    def test_generated_instrument_plays_the_note_it_was_sent(self, tmp_path):
        binary = _build_or_skip()
        dsp = ROOT / "tests" / "fixtures" / "instrument_sine.dsp"
        wav = tmp_path / "sine.wav"
        cap = _capture(binary, dsp, wav)

        assert cap["instrument"] == "1"
        assert cap["nan"] == "0" and cap["inf"] == "0" and cap["muted"] == "0"
        assert float(cap["rms"]) > 1e-5

        p = ro.pitch_of_wav(wav, midi_note=45, held_end=int(cap["held_end"]))
        assert p.evaluated, p.why
        assert p.in_tune, f"cents_error={p.cents_error:.1f} (tolerance {p.tolerance_cents})"
        assert abs(p.cents_error) < 5.0

    def test_it_tracks(self, tmp_path):
        # --note 57 -> 220 Hz. Catches a patch that ignores `freq` entirely and
        # would report the SAME pitch regardless of which note was sent.
        binary = _build_or_skip()
        dsp = ROOT / "tests" / "fixtures" / "instrument_sine.dsp"
        wav = tmp_path / "sine57.wav"
        cap = _capture(binary, dsp, wav, note=57)

        p = ro.pitch_of_wav(wav, midi_note=57, held_end=int(cap["held_end"]))
        assert p.evaluated, p.why
        assert p.in_tune, f"cents_error={p.cents_error:.1f}"

    def test_fixed_pitch_patch_is_green_on_every_old_gate_and_red_here(self, tmp_path):
        # The shape a model produces when it wires the envelope and forgets the
        # pitch: os.osc(440) hardcoded, with `freq` referenced only as a filter
        # corner so the voice contract still holds and the patch still compiles,
        # sounds, starts and stops with the key, and decays on release. It must
        # pass every gate that predates this one and fail ONLY the pitch gate.
        binary = _build_or_skip()
        dsp = ROOT / "tests" / "fixtures" / "instrument_fixed_pitch.dsp"
        wav = tmp_path / "fixed.wav"
        cap = _capture(binary, dsp, wav)

        # Every OLD gate: green.
        assert cap["instrument"] == "1"
        assert cap["nan"] == "0" and cap["inf"] == "0" and cap["muted"] == "0"
        assert float(cap["rms"]) > 1e-5
        assert float(cap["held_rms"]) > float(cap["tail_rms"])  # sounds, then releases

        # The NEW gate: red, by a wide margin (~2400 cents; tolerance is 50).
        p = ro.pitch_of_wav(wav, midi_note=45, held_end=int(cap["held_end"]))
        assert p.evaluated, p.why
        assert not p.in_tune
        assert abs(p.cents_error) > 1000.0

        m = ro.apply_pitch_gate(ro.Measurement(ok=True), p)
        assert m.pitch_gate_evaluated
        assert m.out_of_tune
        assert not m.ok
        assert "out_of_tune" in m.codes
