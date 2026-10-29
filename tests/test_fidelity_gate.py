#!/usr/bin/env python3
"""Unit tests for bench/fidelity_gate.py — the compiler-free half of the
the "did the repair keep the program?" question.

No faust, no faust-rs, no network. Two kinds of test:

  * pure-function checks of the shrink ratio and the primitives truth table;
  * an independently-derived oracle: the shrink tier over the *committed*
    frozen 3b result file must produce exactly A 35/151, B 35/88, C 27/86.
    That triple was hand-recomputed from bench/results/repair_ab/ +
    bench/corpora/ before fidelity_gate.py existed; if the module drifts, this
    breaks.
"""
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "bench"))

import fidelity_gate as fg  # noqa: E402

FROZEN_3B = ROOT / "bench" / "results" / "repair_ab" / "repair_ab_20260830.json"
FROZEN_7B = ROOT / "bench" / "results" / "repair_ab" / "repair_ab_20260830_7b.json"
CORPUS = ROOT / "bench" / "corpora" / "repair_corpus_20260830.json"
PROMPTS = ROOT / "bench" / "prompts" / "tiered_prompts.json"


# ── nonblank / shrink ratio ──────────────────────────────────────────────────

def test_nonblank_ignores_whitespace_only_lines():
    assert fg.nonblank("a\n\n  \n\tb\n") == 2
    assert fg.nonblank("") == 0
    assert fg.nonblank(None) == 0


def test_shrink_ratio_boundary_is_strict_less_than():
    # 3 / 5 == 0.60 exactly -> NOT shrank (threshold is `< 0.60`)
    pre = "\n".join(f"l{i}" for i in range(5))
    post_at = "\n".join(f"l{i}" for i in range(3))
    post_below = "\n".join(f"l{i}" for i in range(2))
    assert fg.shrink_ratio(pre, post_at) == pytest.approx(0.60)
    assert not (fg.shrink_ratio(pre, post_at) < fg.SHRINK_THRESHOLD)
    assert fg.shrink_ratio(pre, post_below) < fg.SHRINK_THRESHOLD


def test_shrink_threshold_value_is_pinned():
    assert fg.SHRINK_THRESHOLD == 0.60


def test_blank_only_diff_is_ratio_one():
    pre = "import(\"stdfaust.lib\");\nprocess = _;"
    post = "\n\nimport(\"stdfaust.lib\");\n\n\nprocess = _;\n\n"
    assert fg.shrink_ratio(pre, post) == 1.0


def test_empty_original_ratio_is_one_not_zero_division():
    assert fg.shrink_ratio("", "process = _;") == 1.0
    assert fg.shrink_ratio("   \n\n", "process = _;") == 1.0


# ── primitives truth table ───────────────────────────────────────────────────

_CORPUS_ROW = {
    "code_sha": "sha_pre",
    "prompt_id": "trivial-01/L1",
    "code": 'import("stdfaust.lib");\nprocess = ba.db2linear(g);',
}


def _cell(post_code, *, expected=("ba.db2linear",), pre_code=None, repaired=True):
    by_sha = {"sha_pre": {**_CORPUS_ROW,
                          **({"code": pre_code} if pre_code is not None else {})}}
    primitives = {"trivial-01": list(expected)} if expected is not None else {}
    rec = {
        "code_sha": "sha_pre",
        "arm": "A",
        "repaired": repaired,
        "attempt_log": [{"code": post_code, "code_sha": "sha_post"}],
    }
    return fg.evaluate_record(rec, by_sha, primitives)


def test_primitive_present_then_present_not_lost():
    c = _cell("process = ba.db2linear(g) * 2;")
    assert c["pre_has_primitive"] is True
    assert c["post_has_primitive"] is True
    assert c["primitive_lost"] is False


def test_primitive_present_then_gone_is_lost():
    c = _cell("process = g * 0.5;")
    assert c["pre_has_primitive"] is True
    assert c["post_has_primitive"] is False
    assert c["primitive_lost"] is True


def test_primitive_absent_in_pre_is_never_lost():
    c = _cell("process = g;", pre_code='import("stdfaust.lib");\nprocess = g;')
    assert c["pre_has_primitive"] is False
    assert c["primitive_lost"] is False


def test_primitive_gained_is_not_counted_as_lost():
    c = _cell("process = ba.db2linear(g);",
              pre_code='import("stdfaust.lib");\nprocess = g;')
    assert c["pre_has_primitive"] is False
    assert c["post_has_primitive"] is True
    assert c["primitive_lost"] is False


def test_no_expected_primitives_reports_none_and_is_excluded():
    c = _cell("process = g;", expected=None)
    assert c["primitives_expected"] is None
    assert c["primitive_lost"] is None
    assert c["pre_has_primitive"] is None


def test_any_of_semantics_one_token_survives():
    c = _cell("process = pow(10, g / 20);", expected=("ba.db2linear", "pow(10"))
    assert c["post_has_primitive"] is True
    assert c["primitive_lost"] is False


# ── join / skip behaviour ────────────────────────────────────────────────────

def test_record_not_in_corpus_is_dropped():
    rec = {"code_sha": "unknown", "arm": "A", "repaired": True,
           "attempt_log": [{"code": "process = _;", "code_sha": "x"}]}
    assert fg.evaluate_record(rec, {}, {}) is None


def test_record_with_empty_attempt_log_is_dropped():
    by_sha = {"sha_pre": _CORPUS_ROW}
    rec = {"code_sha": "sha_pre", "arm": "A", "repaired": False, "attempt_log": []}
    assert fg.evaluate_record(rec, by_sha, {}) is None


# ── frozen-data oracle ───────────────────────────────────────────────────────

@pytest.mark.skipif(not FROZEN_3B.exists(), reason="frozen 3b result file absent")
def test_frozen_3b_shrink_oracle():
    report = fg.build_report(FROZEN_3B, CORPUS, PROMPTS)
    s = report["summary"]
    got = {arm: (s[arm]["shrank"], s[arm]["wins"]) for arm in s}
    assert got == {"A": (35, 151), "B": (35, 88), "C": (27, 86)}


@pytest.mark.skipif(not FROZEN_3B.exists(), reason="frozen 3b result file absent")
def test_frozen_3b_primitives_denominators_pinned():
    # regression guard on the effect-id join (183/202 distinct programs join;
    # per-arm win counts that land in the primitives-determinable set):
    report = fg.build_report(FROZEN_3B, CORPUS, PROMPTS)
    s = report["summary"]
    det = {arm: s[arm]["primitives_determinable"] for arm in s}
    lost = {arm: s[arm]["primitive_lost"] for arm in s}
    assert det == {"A": 140, "B": 82, "C": 80}
    assert lost == {"A": 20, "B": 7, "C": 5}
    assert report["meta"]["records_unjoined"] == 0


@pytest.mark.skipif(not FROZEN_7B.exists(), reason="frozen 7b result file absent")
def test_frozen_7b_shrink_oracle():
    report = fg.build_report(FROZEN_7B, CORPUS, PROMPTS)
    s = report["summary"]
    got = {arm: (s[arm]["shrank"], s[arm]["wins"]) for arm in s}
    assert got == {"A": (19, 87), "B": (17, 60), "C": (17, 57)}


@pytest.mark.skipif(not FROZEN_3B.exists(), reason="frozen 3b result file absent")
def test_cell_map_keys_are_unique_per_arm_and_sha():
    report = fg.build_report(FROZEN_3B, CORPUS, PROMPTS)
    # 606 records, all joinable, one cell each
    assert report["meta"]["cells"] == 606
    assert len(report["cells"]) == 606
