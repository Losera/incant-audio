#!/usr/bin/env python3
"""tests/test_issue26_readme_numbers.py — pin bench/issue26/README.md's prose
numbers to what bench/score_repair_ab.py actually computes over the committed
trajectories.

Motivation: PR #55 hand-edited the README headline and mechanism numbers, and
one (`rescue 49/87` for `49/88`) drifted, because nothing checked the prose —
`verify.py` guards `expected.json`, and `tests/test_release_packaging.py` reads
only the *root* README. This closes that gap: the numbers a GitHub reader sees
must match the numbers `verify.py` re-derives.

Ships with a verified-red case: flip any pinned digit in the README and this
fails.
"""
import re
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "bench" / "issue26"))

import verify  # noqa: E402

README = (ROOT / "bench" / "issue26" / "README.md").read_text()


@pytest.fixture(scope="module")
def cells():
    obs = verify.observe()
    return {(c["model"], c["treatment"]): c for c in obs["cells"]}


def _assert_phrase(pattern: str):
    assert re.search(pattern, README), f"README.md missing / stale: /{pattern}/"


def test_headline_table_matches_scorer(cells):
    b3 = cells[("qwen2.5-coder:3b", "B")]
    c3 = cells[("qwen2.5-coder:3b", "C")]
    b7 = cells[("qwen2.5-coder:7b-instruct-q3_K_S", "B")]
    c7 = cells[("qwen2.5-coder:7b-instruct-q3_K_S", "C")]
    # `| 192 | 143 (74%) | 82 (43%) | 80 (42%) |`
    _assert_phrase(rf"\|\s*{b3['n']}\s*\|\s*\*?\*?{b3['a_green']}[^|]*\|\s*"
                   rf"{b3['b_green']}[^|]*\|\s*{c3['b_green']}\b")
    _assert_phrase(rf"\|\s*{b7['n']}\s*\|\s*\*?\*?{b7['a_green']}[^|]*\|\s*"
                   rf"{b7['b_green']}[^|]*\|\s*{c7['b_green']}\b")


def test_rescue_split_matches_scorer(cells):
    r = cells[("qwen2.5-coder:3b", "B")]["rescue"]
    rc = cells[("qwen2.5-coder:3b", "C")]["rescue"]
    a, b = r["A"], r["B"]
    _assert_phrase(rf"{a['won_at_1']}\s*/\s*{a['won_at_1'] + a['still_broken'] + a['no_program']}"
                   rf"[^)]*\bvs\b[^)]*{b['won_at_1']}")
    _assert_phrase(rf"\b{a['rescued_at_2']}\s*/\s*{a['still_broken']}\b")
    _assert_phrase(rf"\b{b['rescued_at_2']}\s*/\s*{b['still_broken']}\b")
    _assert_phrase(rf"\b{rc['C']['rescued_at_2']}\s*/\s*{rc['C']['still_broken']}\b")


def test_recidivism_matches_scorer(cells):
    for treat in ("B", "C"):
        se = cells[("qwen2.5-coder:3b", treat)]["second_error"]
        a, t = se["A"], se[treat]
        a_att = a["same_class"] + a["new_class"]
        t_att = t["same_class"] + t["new_class"]
        if treat == "B":
            _assert_phrase(rf"{a['same_class']}\s*\(\*?\*?54%[^)]*\b{a_att}\b")
        _assert_phrase(rf"{t['same_class']}\s*/\s*{t_att}\b")


def test_caret_preservation_matches_scorer(cells):
    cp = cells[("qwen2.5-coder:3b", "B")]["caret_preservation"]
    cpc = cells[("qwen2.5-coder:3b", "C")]["caret_preservation"]
    cp7 = cells[("qwen2.5-coder:7b-instruct-q3_K_S", "B")]["caret_preservation"]
    _assert_phrase(rf"\b{cp['b_preserved']}\s*/\s*{cp['n']}\b")
    _assert_phrase(rf"\b{cp['a_preserved']}\s*/\s*{cp['n']}\b")
    _assert_phrase(rf"\b{cp7['b_preserved']}\s*/\s*{cp7['n']}\b")
    _assert_phrase(rf"\b{cp7['a_preserved']}\s*/\s*{cp7['n']}\b")
    _assert_phrase(rf"\b{cpc['b_preserved']}\s*/\s*{cpc['n']}\b")


def test_cap_strata_match_scorer(cells):
    t = cells[("qwen2.5-coder:3b", "B")]["by_arm_a_truncation"]
    u, c = t["uncapped"], t["capped"]
    _assert_phrase(rf"never truncated\*?\*?\s*\|\s*{u['n']}\s*\|\s*{u['a_green']}[^|]*\|\s*{u['b_green']}\b")
    _assert_phrase(rf"truncated\*?\*?\s*\|\s*{c['n']}\s*\|\s*{c['a_green']}[^|]*\|\s*{c['b_green']}\b")
    _assert_phrase(rf"\b{c['n']} of the {u['n'] + c['n']}\b")
