"""Tests for tools/generation_lane.py's aggregation/comparison logic.

No ollama, no network, and -- for everything except the one `_requires_faust`
test at the bottom of TestScoreRep -- no faust either. `score_rep` is the only
function here that reaches a real compiler, via bench/render_oracle.analyse_record,
and it now takes that analyser as an injectable argument so its COUNTING logic
can be tested on a machine with no toolchain.

This file used to claim the opposite:

    "Faust itself IS required for these ... skip is not applicable here since
     `faust`/`faust2sndfile` are a stated, pinned part of this machine
     (CLAUDE.md)."

That reasoned from *this machine* to *all environments*, and it was wrong. CI's
`test` job is explicitly "no API key or faust compiler required"
(.github/workflows/test.yml), so on 2026-10-21 both oracle-asserting tests below
failed there with `assert 0 == 1` while passing locally -- see score_rep's own
SUBTLE note for why an absent binary zeroes the counters instead of erroring.
tools/check.sh's `fast` level uses the same `-m "not integration"` selector CI
does, so the local ladder could not have caught it: same selection, richer
environment.

The injected fakes below mirror render_oracle.analyse()'s real return shapes
(render_oracle.py:807-810 for the two failure shapes, and the keys score_rep
actually reads for the success shape). The `_requires_faust` test keeps the real
analyser honest against those fakes wherever a toolchain exists.
"""
from __future__ import annotations

import json
import shutil
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
sys.path.insert(0, str(ROOT / "bench"))

import generation_lane as gl  # noqa: E402

# CI's `test` job (.github/workflows/test.yml:52) is explicitly "no API key or
# faust compiler required" -- `build-host` is the job that apt-installs faust.
#
# Guards on `faust2sndfile`, NOT `faust`, because that is the binary
# render_oracle.render_many() actually probes (render_oracle.py:370-371) and the
# two are separately installable: `faust` present without `faust2sndfile` would
# let this test run and then fail. Copies tests/test_render_oracle.py:23-26 --
# the closest sibling, since both files drive render_oracle -- rather than
# inventing a convention; ~29 of CI's 60 skips use this same form.
#
# Applied per-test rather than as a module-level `pytestmark` (which is what
# test_render_oracle.py does) because only this ONE test needs a toolchain here;
# the other thirteen are pure-Python counting/aggregation tests that must keep
# running in CI. Deliberately NOT `@pytest.mark.integration` either: that is
# deselected by `tools/check.sh fast` AND by CI, which would leave this path
# unexercised in both places, whereas skipif still runs it locally.
_requires_faust = pytest.mark.skipif(
    shutil.which("faust2sndfile") is None,
    reason="faust2sndfile not installed (expected in the Python-only CI job)",
)

# --- analyse_record() stand-ins, shaped from render_oracle.py ----------------
# Do not "simplify" these to bare dicts with fewer keys: score_rep reads
# r.get("unsupported"), r["rendered"] and r["measurement"]["ok"], and the point
# of the pass/fail split is that only ONE of the three increments a counter.


def _fake_pass(_rec: dict) -> dict:
    """A patch that rendered and measured clean."""
    return {"schema": 2, "rendered": True,
            "measurement": {"ok": True, "reasons": []}}


def _fake_unsupported(_rec: dict) -> dict:
    """A 0-input generator: UnsupportedPatch -> render_oracle.py:808.

    Reachable ONLY where faust2sndfile exists -- the arity check that raises it
    (render_oracle.py:375-379) sits after the toolchain probe at :370-371.
    """
    return {"schema": 2, "rendered": False, "unsupported": True,
            "error": "zero-input patch (0 in, 2 out) -- generators are outside "
                     "this harness; see UnsupportedPatch"}


def _fake_render_error(_rec: dict) -> dict:
    """Any other RenderError -> render_oracle.py:810.

    This is the exact shape an ABSENT toolchain produces, verbatim from
    render_oracle.py:371 -- the defect this file regressed on in CI.
    """
    return {"schema": 2, "rendered": False, "unsupported": False,
            "error": "faust2sndfile not on PATH (part of the Faust install)"}

GAIN_DSP = (
    'import("stdfaust.lib");\n'
    'gain = hslider("Gain [unit:dB]", 0, -60, 12, 0.1) : ba.db2linear : si.smoo;\n'
    'process = _ * gain, _ * gain;'
)

GENERATOR_DSP = (
    'import("stdfaust.lib");\n'
    'process = os.osc(440);'
)


def _record(category: str, code: str, compiles: bool = True) -> dict:
    return {
        "category": category, "code": code,
        "first_try_compiles": compiles, "retry_success": False,
        "prompt": f"a {category} test effect",
    }


class TestScoreRep:
    def test_compiling_scorable_patch_counts_compiled_and_oracle(self, tmp_path):
        path = tmp_path / "rep1.json"
        path.write_text(json.dumps([_record("filters", GAIN_DSP)]))
        out = gl.score_rep(path, analyse=_fake_pass)
        assert out["filters"]["n"] == 1
        assert out["filters"]["compiled"] == 1
        assert out["filters"]["oracle_pass"] == 1
        assert out["filters"]["oracle_na"] == 0

    def test_zero_input_generator_counts_as_compiled_but_oracle_na(self, tmp_path):
        """The generative category's own known shape: compiles, but a 0-input
        generator cannot be render-scored (UnsupportedPatch) -- this must
        show up as 'na', never silently as a pass or a fail."""
        path = tmp_path / "rep1.json"
        path.write_text(json.dumps([_record("generative", GENERATOR_DSP)]))
        out = gl.score_rep(path, analyse=_fake_unsupported)
        assert out["generative"]["compiled"] == 1
        assert out["generative"]["oracle_na"] == 1
        assert out["generative"]["oracle_pass"] == 0

    def test_render_failure_is_neither_a_pass_nor_an_na(self, tmp_path):
        """Regression test for the 2026-10-21 CI failure.

        A RenderError that is NOT UnsupportedPatch -- which is what an absent
        toolchain produces, raised at render_oracle.py:370-371 and caught into
        this shape at :810 -- must count as compiled but score neither
        oracle_pass nor oracle_na. Nothing covered this case,
        which is exactly why CI could fail on it silently: the two tests above
        asserted the counters they expected, and a toolchain-less environment
        zeroed BOTH, so the defect read as "the assertions are wrong" rather
        than "the oracle never ran."

        aggregate()'s scorable denominator is `compiled - oracle_na`
        (generation_lane.py), so miscounting a render failure as `na` would
        silently shrink the denominator and inflate the pass rate.
        """
        path = tmp_path / "rep1.json"
        path.write_text(json.dumps([_record("filters", GAIN_DSP)]))
        out = gl.score_rep(path, analyse=_fake_render_error)
        assert out["filters"]["n"] == 1
        assert out["filters"]["compiled"] == 1
        assert out["filters"]["oracle_pass"] == 0
        assert out["filters"]["oracle_na"] == 0

    def test_non_compiling_record_never_calls_the_oracle(self, tmp_path):
        """A record that did not compile must not be rendered at all -- asserted
        by injecting an analyser that fails the test if it is ever reached."""
        def _explode(_rec):
            raise AssertionError("score_rep rendered a non-compiling record")

        path = tmp_path / "rep1.json"
        path.write_text(json.dumps([_record("dynamics", "not valid faust {{{", compiles=False)]))
        out = gl.score_rep(path, analyse=_explode)
        assert out["dynamics"]["n"] == 1
        assert out["dynamics"]["compiled"] == 0
        assert out["dynamics"]["oracle_pass"] == 0

    def test_retry_success_without_first_try_still_counts_compiled(self, tmp_path):
        rec = _record("filters", GAIN_DSP, compiles=False)
        rec["retry_success"] = True
        path = tmp_path / "rep1.json"
        path.write_text(json.dumps([rec]))
        out = gl.score_rep(path, analyse=_fake_pass)
        assert out["filters"]["compiled"] == 1

    @_requires_faust
    def test_real_oracle_agrees_with_the_fakes(self, tmp_path):
        """The end-to-end test the fakes above stand in for.

        Guards against the fakes drifting from render_oracle's real return shape:
        a change to analyse()'s keys would break production while the injected
        tests kept passing. This is the ONLY test in this file that needs a
        toolchain, and it is skipped -- not failed -- where there is none.

        Asserts both real shapes in one run: GAIN_DSP is a 2-in/2-out effect
        that should render and measure clean, while GENERATOR_DSP is a 0-input
        generator that should come back UnsupportedPatch.
        """
        path = tmp_path / "rep1.json"
        path.write_text(json.dumps([
            _record("filters", GAIN_DSP),
            _record("generative", GENERATOR_DSP),
        ]))
        out = gl.score_rep(path)  # no injection: the real analyse_record
        assert out["filters"]["compiled"] == 1
        assert out["filters"]["oracle_pass"] == 1, (
            "real oracle disagrees with _fake_pass's shape")
        assert out["filters"]["oracle_na"] == 0
        assert out["generative"]["compiled"] == 1
        assert out["generative"]["oracle_na"] == 1, (
            "real oracle disagrees with _fake_unsupported's shape")
        assert out["generative"]["oracle_pass"] == 0


class TestAggregate:
    def test_sums_across_reps(self):
        rep1 = {"filters": {"n": 2, "compiled": 2, "oracle_pass": 1, "oracle_na": 0}}
        rep2 = {"filters": {"n": 2, "compiled": 1, "oracle_pass": 1, "oracle_na": 0}}
        out = gl.aggregate([rep1, rep2])
        assert out["filters"]["n"] == 4
        assert out["filters"]["compiled"] == 3
        assert out["filters"]["compile_rate"] == 0.75

    def test_oracle_rate_excludes_na_from_denominator(self):
        rep = {"generative": {"n": 3, "compiled": 3, "oracle_pass": 1, "oracle_na": 2}}
        out = gl.aggregate([rep])
        # scorable = compiled - oracle_na = 1; 1/1 pass
        assert out["generative"]["oracle_pass_rate_of_scorable"] == 1.0

    def test_no_scorable_cells_is_none_not_zero(self):
        """All-generative, all-unsupported: there is nothing to render-score.
        None must not read as '0% oracle pass' -- that would be a false
        failure signal for a category the oracle cannot evaluate at all."""
        rep = {"generative": {"n": 2, "compiled": 2, "oracle_pass": 0, "oracle_na": 2}}
        out = gl.aggregate([rep])
        assert out["generative"]["oracle_pass_rate_of_scorable"] is None


class TestCompareToBaseline:
    def test_no_baseline_says_so_and_does_not_crash(self):
        lines = gl.compare_to_baseline({"filters": {"compile_rate": 1.0,
                                                     "oracle_pass_rate_of_scorable": 1.0}}, None)
        assert any("No baseline" in ln for ln in lines)

    def test_within_tolerance_is_not_flagged(self):
        baseline = {"categories": {"filters": {"compile_rate": 0.90,
                                                "oracle_pass_rate_of_scorable": 0.90}}}
        current = {"filters": {"compile_rate": 0.85, "oracle_pass_rate_of_scorable": 0.85}}
        lines = gl.compare_to_baseline(current, baseline)
        assert not any("outside tolerance" in ln for ln in lines)

    def test_outside_tolerance_is_flagged(self):
        baseline = {"categories": {"filters": {"compile_rate": 0.90,
                                                "oracle_pass_rate_of_scorable": 0.90}}}
        current = {"filters": {"compile_rate": 0.20, "oracle_pass_rate_of_scorable": 0.20}}
        lines = gl.compare_to_baseline(current, baseline)
        assert any("outside tolerance" in ln for ln in lines)

    def test_new_category_not_in_baseline_is_reported_not_crashed(self):
        baseline = {"categories": {}}
        current = {"brand_new": {"compile_rate": 1.0, "oracle_pass_rate_of_scorable": None}}
        lines = gl.compare_to_baseline(current, baseline)
        assert any("no baseline entry" in ln for ln in lines)

    def test_none_oracle_rate_on_either_side_is_skipped_not_crashed(self):
        """A category where nothing was scorable (all generative/unsupported)
        on either side must not attempt a None-minus-None comparison."""
        baseline = {"categories": {"generative": {"compile_rate": 1.0,
                                                   "oracle_pass_rate_of_scorable": None}}}
        current = {"generative": {"compile_rate": 1.0, "oracle_pass_rate_of_scorable": None}}
        lines = gl.compare_to_baseline(current, baseline)  # must not raise
        assert any("generative" in ln for ln in lines)


class TestEffectSubsetSpansAllFiveCategories:
    """The curated EFFECT_IDS list is the lane's one piece of hand-picked
    content; a future edit that accidentally drops a category should be
    caught here rather than silently narrowing what the lane covers."""

    def test_all_five_categories_present(self):
        prefixes = {e.rsplit("-", 1)[0] for e in gl.EFFECT_IDS}
        assert prefixes == {"trivial", "filters", "time-based", "dynamics", "generative"}

    def test_matches_the_real_tiered_prompts_file(self):
        data = json.loads((ROOT / "bench" / "prompts" / "tiered_prompts.json").read_text())
        real_ids = {e["effect_id"] for e in data["effects"]}
        missing = set(gl.EFFECT_IDS) - real_ids
        assert not missing, f"EFFECT_IDS names IDs not in tiered_prompts.json: {missing}"
        assert gl.TIER in data["tiers"]
