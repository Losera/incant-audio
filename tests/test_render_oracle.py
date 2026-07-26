"""The audio gate, in the test suite rather than beside it.

bench/render_oracle.py carries its own `--self-test` against three patches with
answers known from theory. That is the right design — an oracle nobody checks is
worse than no oracle — but a self-test that only runs when someone remembers to
type it is the same class of dead control this project has now shipped three times.
So it runs here too.

Everything below needs `faust2sndfile`, which is part of the Faust install this
project already requires but which the Python CI job deliberately does not have.
These self-skip there, mirroring tests/test_prompt_stdlib.py; they run for real in
the build-host job, which has the compiler and stdlib.
"""
import shutil
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).parent.parent
sys.path.insert(0, str(ROOT / "bench"))

pytestmark = pytest.mark.skipif(
    shutil.which("faust2sndfile") is None,
    reason="faust2sndfile not installed (expected in the Python-only CI job)",
)


@pytest.fixture(scope="module")
def ro():
    import render_oracle
    return render_oracle


LOWPASS = 'import("stdfaust.lib");\nprocess = fi.resonlp(1000, 0.707, 1.0), fi.resonlp(1000, 0.707, 1.0);'
GAIN = 'import("stdfaust.lib");\nprocess = _*0.5, _*0.5;'
SILENT = 'import("stdfaust.lib");\nprocess = _*0, _*0;'
OSC = 'import("stdfaust.lib");\nprocess = os.osc(440) * 0.5;'


class TestKnownAnswers:
    """If these drift, the oracle is lying and every number built on it is void."""

    def test_lowpass_has_correct_corner_and_rolloff(self, ro):
        r = ro.analyse(LOWPASS)
        assert r["rendered"], r.get("error")
        assert r["measurement"]["ok"]
        bands = r["features"]["band_gain_db"]
        # -3 dB at the 1 kHz cutoff is the textbook definition of the corner.
        assert -5 < bands["800-1200"] < -1, f"corner not at -3 dB: {bands['800-1200']}"
        # ~12 dB/oct means deep attenuation two octaves up.
        assert bands["4000-8000"] < -20, f"insufficient rolloff: {bands['4000-8000']}"
        assert bands["50-200"] > -1, f"passband should be flat: {bands['50-200']}"

    def test_flat_gain_is_flat(self, ro):
        r = ro.analyse(GAIN)
        assert r["rendered"] and r["measurement"]["ok"]
        for band, val in r["features"]["band_gain_db"].items():
            assert -7 < val < -5, f"{band} should be ~-6 dB, got {val}"


class TestGatesFire:
    """A gate that cannot fail is not a gate."""

    def test_silence_is_caught(self, ro):
        r = ro.analyse(SILENT)
        assert r["rendered"]
        assert not r["measurement"]["ok"]
        assert r["measurement"]["is_silent"]
        assert any("silent" in reason for reason in r["measurement"]["reasons"])

    def test_generator_is_unsupported_not_failed(self, ro):
        """Distinguishing 'cannot measure' from 'is broken' is load-bearing.

        Conflating the two is exactly how five API billing errors ended up counted
        as Faust compile failures in the efficacy pilot.
        """
        r = ro.analyse(OSC)
        assert not r["rendered"]
        assert r["unsupported"] is True
        assert "zero-input" in r["error"]


class TestDeterminism:
    def test_same_patch_gives_same_numbers(self, ro):
        """A seeded probe signal, or it is not an oracle."""
        a = ro.analyse(LOWPASS)["features"]["band_gain_db"]
        b = ro.analyse(LOWPASS)["features"]["band_gain_db"]
        assert a == b


class TestArityProbe:
    def test_reports_effect_io(self, ro):
        assert ro.patch_arity(LOWPASS) == (2, 2)

    def test_reports_generator_io(self, ro):
        n_in, _ = ro.patch_arity(OSC)
        assert n_in == 0
