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

# 100 ms line at 0.6 feedback: t60 = 100 * ln(0.001)/ln(0.6) = 1350 ms, inside the
# 1750 ms measurement window so the non-truncated path is what gets exercised.
DELAY_TAIL = ('import("stdfaust.lib");\n'
              'echo = + ~ (de.delay(48000, 4800) : *(0.6));\n'
              'process = echo, echo;')
# The bug the tail check exists for: delay controls declared, recursion gone.
DEAD_DELAY = ('import("stdfaust.lib");\n'
              'dtime = hslider("Time [unit:ms]", 250, 10, 1000, 1);\n'
              'fb    = hslider("Feedback", 0.4, 0, 0.95, 0.01);\n'
              'process = _, _;')
# Unity loop gain: circulates forever at constant level once excited.
NEVER_DECAYS = ('import("stdfaust.lib");\n'
                'comb = + ~ de.delay(48000, 4800);\n'
                'process = comb, comb;')


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


class TestTail:
    """Does the output outlive the input? Added 2026-07-31."""

    def test_delay_outlives_its_input(self, ro):
        r = ro.analyse(DELAY_TAIL)
        assert r["rendered"], r.get("error")
        t = r["tail"]
        assert t["evaluated"], t["note"]
        assert not t["inert"]
        # Derived 1350 ms; measured 1300 ms (frame quantisation + the -60 dB floor).
        assert 900 < t["tail_ms"] < 1750, t["tail_ms"]
        assert not t["tail_truncated"]
        assert r["measurement"]["ok"]

    def test_gain_is_inert_and_that_is_correct(self, ro):
        """The case that proves the tail check is not a universal gate.

        A gain plugin correctly has no tail. It must stay green as an
        unconditional verdict, and go red ONLY against a stated expectation.
        """
        r = ro.analyse(GAIN)
        t = r["tail"]
        assert t["evaluated"] and t["inert"] and t["tail_ms"] == 0.0
        assert r["measurement"]["ok"], "an inert patch is not a failing patch"
        met, _ = ro.meets_tail_expectation(ro.Tail(**t), 10.0)
        assert not met

    def test_resonant_filter_does_not_read_as_a_tail(self, ro):
        """The burst probe's reason for existing, pinned.

        Against a bare impulse a resonant filter reads as "has a tail" because the
        reference is one sample. Against the burst's steady-state RMS it does not.
        Measured 5 ms, under INERT_MS.
        """
        t = ro.analyse(LOWPASS)["tail"]
        assert t["evaluated"] and t["inert"], t["tail_ms"]

    def test_continuous_probe_cannot_be_judged(self, ro):
        """The safety property: no silent region means no verdict, ever.

        tail() derives the drive window from the signal rather than from BURST_S,
        so it is structurally incapable of firing on a probe it was not designed
        for. If this ever passes by accident the gate could misfire corpus-wide.
        """
        x = ro.test_signal("noise")
        t = ro.tail(x, x)
        assert not t.evaluated
        assert "no silent region" in t.note


class TestExpectations:
    def test_dead_delay_fails_a_time_based_expectation(self, ro):
        """Green on every gate that existed before the tail check — that is the point."""
        r = ro.analyse(DEAD_DELAY, expect_tail_ms=120.0)
        m = r["measurement"]
        assert m["ok"], "must pass every objective gate; only the expectation catches it"
        assert r["tail"]["inert"]
        assert r["expectation"]["met"] is False
        assert "no tail" in r["expectation"]["reason"]

    def test_absent_expectation_omits_the_key(self, ro):
        """Absence of a claim must not read as a passed claim."""
        r = ro.analyse(GAIN)
        assert "expectation" not in r

    def test_expectation_lookup_prefers_prompt_over_category(self, ro):
        prompt = "a ping-pong delay that bounces between left and right"
        assert ro.expectation_for(prompt, "time-based") == 120.0
        assert ro.expectation_for("some novel prompt", "time-based") == 10.0
        assert ro.expectation_for("some novel prompt", "trivial") is None


class TestAcousticCompliance:
    """The judge's report path: analyse() with a prompt emits a verdict.

    render_oracle computes the features and bench/spectral_judge.py turns them
    into a score. Without a prompt the block must be absent — absence of a claim
    must not read as a passed claim.
    """

    def test_lowpass_prompt_reports_a_compliant_verdict(self, ro):
        r = ro.analyse(LOWPASS, prompt="a lowpass filter")
        assert r["rendered"], r.get("error")
        ac = r["acoustic_compliance"]
        assert ac["score"] > 0.5, ac["notes"]

    def test_gain_prompt_is_not_falsely_penalized(self, ro):
        # No filter/time/dynamics word in the prompt, so no branch can fire.
        r = ro.analyse(GAIN, prompt="a plain gain stage")
        assert r["rendered"], r.get("error")
        ac = r["acoustic_compliance"]
        assert ac["score"] == 1.0, ac["notes"]

    def test_without_prompt_there_is_no_compliance_block(self, ro):
        r = ro.analyse(LOWPASS)
        assert "acoustic_compliance" not in r


class TestDeterminism:
    def test_same_patch_gives_same_numbers(self, ro):
        """A seeded probe signal, or it is not an oracle."""
        a = ro.analyse(LOWPASS)["features"]["band_gain_db"]
        b = ro.analyse(LOWPASS)["features"]["band_gain_db"]
        assert a == b

    def test_same_patch_gives_same_tail(self, ro):
        a = ro.analyse(DELAY_TAIL)["tail"]
        b = ro.analyse(DELAY_TAIL)["tail"]
        assert a == b


class TestBackwardCompat:
    """bench/results/oracle_20260725.json holds schema-1 reports."""

    def test_features_keys_match_the_archive(self, ro):
        import json
        archive = ROOT / "bench" / "results" / "oracle_20260725.json"
        if not archive.exists():
            pytest.skip("no archived oracle run to compare against")
        recs = json.loads(archive.read_text())
        old = next(r["analysis"] for r in recs if r["analysis"].get("rendered"))
        fresh = ro.analyse(LOWPASS)
        assert set(fresh["features"]) == set(old["features"]), (
            "Features changed shape; the 2026-07-25 archive is no longer comparable")
        assert set(fresh["measurement"]) >= set(old["measurement"]), (
            "Measurement lost a field; schema-1 readers will KeyError")
        assert "schema" not in old and fresh["schema"] == 2


class TestArityProbe:
    def test_reports_effect_io(self, ro):
        assert ro.patch_arity(LOWPASS) == (2, 2)

    def test_reports_generator_io(self, ro):
        n_in, _ = ro.patch_arity(OSC)
        assert n_in == 0
