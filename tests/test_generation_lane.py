"""Tests for tools/generation_lane.py's aggregation/comparison logic.

No ollama, no faust, no network: `score_rep` is the one function that touches
a real compiler (via bench/render_oracle.analyse_record), and it is exercised
directly with already-compiled Faust source strings rather than live
generation -- the same "no C++ build required" discipline
tests/test_health_report.py documents for its own harness-output tests. Faust
itself IS required for these (analyse_record shells out to faust2sndfile),
matching tests/test_render_oracle.py's own `integration`-adjacent dependency;
skip is not applicable here since `faust`/`faust2sndfile` are a stated,
pinned part of this machine (CLAUDE.md).
"""
from __future__ import annotations

import json
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
sys.path.insert(0, str(ROOT / "bench"))

import generation_lane as gl  # noqa: E402

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
        out = gl.score_rep(path)
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
        out = gl.score_rep(path)
        assert out["generative"]["compiled"] == 1
        assert out["generative"]["oracle_na"] == 1
        assert out["generative"]["oracle_pass"] == 0

    def test_non_compiling_record_counts_only_n(self, tmp_path):
        path = tmp_path / "rep1.json"
        path.write_text(json.dumps([_record("dynamics", "not valid faust {{{", compiles=False)]))
        out = gl.score_rep(path)
        assert out["dynamics"]["n"] == 1
        assert out["dynamics"]["compiled"] == 0
        assert out["dynamics"]["oracle_pass"] == 0

    def test_retry_success_without_first_try_still_counts_compiled(self, tmp_path):
        rec = _record("filters", GAIN_DSP, compiles=False)
        rec["retry_success"] = True
        path = tmp_path / "rep1.json"
        path.write_text(json.dumps([rec]))
        out = gl.score_rep(path)
        assert out["filters"]["compiled"] == 1


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
