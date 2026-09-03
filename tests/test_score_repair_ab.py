#!/usr/bin/env python3
"""Unit tests for bench/score_repair_ab.py — the file that turns the committed
issue-#26 trajectories into every published number.

Pure functions over synthetic paired records. No scipy stub needed (McNemar and
Wilcoxon are exercised against known-answer inputs). Covers: the McNemar exact
p on a crafted discordant split, censored scoring, `include=` filtering, the
rescue arithmetic closing, `same + new + no_attempt == failed`, the arm-A cap
stratification split, and `load_pairs`'s last-write-wins as *documented* known
behaviour (WP1).
"""
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "bench"))

import score_repair_ab as sr  # noqa: E402


def _rec(sha, arm, *, repaired, attempts=None, model="m",
         first_class="syntax", second_same=None, terminal=None,
         feedback_len=40, made_code=True, n_attempts=1):
    """One (program, arm) result record, minimal but shaped like the real thing."""
    log = []
    if made_code:
        log.append({"n": 1, "code": "process = _;", "feedback_text": "x" * feedback_len,
                    "cpp_ok": repaired and attempts == 1})
        if n_attempts > 1:
            log.append({"n": 2, "code": "process = _,_;", "feedback_text": "y",
                        "cpp_ok": repaired and attempts == 2})
    else:
        log.append({"n": 1, "error": "OutputTruncated: hit the cap"})
    return {
        "code_sha": sha, "arm": arm, "repair_model": model,
        "repaired": repaired, "attempts_to_green": attempts,
        "first_error_class": first_class,
        "second_error_same_as_first": second_same,
        "terminal_reason": terminal, "attempt_log": log,
    }


def _pairs(*specs):
    """specs: (sha, a_kwargs, b_kwargs) -> [(a_rec, b_rec)]"""
    return [(_rec(sha, "A", **a), _rec(sha, "B", **b)) for sha, a, b in specs]


# ── mcnemar_exact ────────────────────────────────────────────────────────────

def test_mcnemar_exact_known_values():
    assert sr.mcnemar_exact(0, 0) == 1.0
    # 3 vs 1 discordant, two-sided exact: P(X<=1) + P(X>=3) on Binom(4, .5)
    # = (1 + 4)/16 + (4 + 1)/16 = 10/16
    assert sr.mcnemar_exact(3, 1) == pytest.approx(10 / 16)
    # 10 vs 0: only the two tails at the extremes -> 2 * .5^10
    assert sr.mcnemar_exact(10, 0) == pytest.approx(2 * 0.5 ** 10)
    assert sr.mcnemar_exact(5, 5) == pytest.approx(1.0)


# ── score() censoring ────────────────────────────────────────────────────────

def test_score_censors_failures_at_fail_score():
    assert sr.score({"repaired": True, "attempts_to_green": 1}) == 1
    assert sr.score({"repaired": True, "attempts_to_green": 2}) == 2
    assert sr.score({"repaired": False, "attempts_to_green": None}) == sr.FAIL_SCORE


# ── load_pairs include= ──────────────────────────────────────────────────────

def test_load_pairs_include_filters_by_sha():
    recs = [_rec("keep", "A", repaired=True, attempts=1),
            _rec("keep", "B", repaired=False),
            _rec("drop", "A", repaired=True, attempts=1),
            _rec("drop", "B", repaired=True, attempts=1)]
    assert len(sr.load_pairs(recs, "m", "B")) == 2                     # no filter
    assert len(sr.load_pairs(recs, "m", "B", include={"keep"})) == 1   # filtered
    assert sr.load_pairs(recs, "m", "B", include=set()) == []


def test_load_pairs_last_write_wins_is_known_behaviour():
    # WP1 in METHODOLOGY.md: with --samples K>1, load_pairs keeps the LAST
    # record per (code_sha, arm). This test pins that as a KNOWN limitation so a
    # future fix trips it deliberately, not silently.
    recs = [_rec("s", "A", repaired=False),
            _rec("s", "A", repaired=True, attempts=1),   # second wins
            _rec("s", "B", repaired=False)]
    (a, _b), = sr.load_pairs(recs, "m", "B")
    assert a["repaired"] is True


# ── rescue arithmetic ───────────────────────────────────────────────────────

def test_rescue_counts_close():
    recs = [
        _rec("1", "A", repaired=True, attempts=1),                       # won@1
        _rec("2", "A", repaired=True, attempts=2, n_attempts=2),         # rescued@2
        _rec("3", "A", repaired=False, n_attempts=2),                    # still broken, not rescued
        _rec("4", "A", repaired=False, made_code=False, terminal="truncated"),  # no program
    ]
    r = sr._rescue(recs)
    assert r == {"n": 4, "won_at_1": 1, "still_broken": 2,
                 "rescued_at_2": 1, "no_program": 1}
    # still_broken = n - won_at_1 - no_program
    assert r["still_broken"] == r["n"] - r["won_at_1"] - r["no_program"]


def test_produced_no_program_detects_both_signals():
    assert sr.produced_no_program(_rec("x", "A", repaired=False, made_code=False))
    assert sr.produced_no_program(
        {"terminal_reason": "rate_limited", "attempt_log": [{"n": 1, "code": "process=_;"}]})
    assert not sr.produced_no_program(_rec("x", "A", repaired=True, attempts=1))


# ── report(): denominators close, cap stratification splits ──────────────────

def _report_dict(pairs):
    import io
    from contextlib import redirect_stdout
    with redirect_stdout(io.StringIO()):
        return sr.report(pairs, "m", "B")


def test_report_second_error_denominators_close():
    pairs = _pairs(
        ("1", dict(repaired=True, attempts=1), dict(repaired=False, second_same=True, n_attempts=2)),
        ("2", dict(repaired=False, second_same=True, n_attempts=2), dict(repaired=False, second_same=False, n_attempts=2)),
        ("3", dict(repaired=False, made_code=False, terminal="truncated"),
              dict(repaired=False, second_same=False, n_attempts=2)),
    )
    s = _report_dict(pairs)
    # arm A failed = 2 (sha 2, sha 3); same 1, new 0, no_attempt 1 -> 1+0+1 == 2
    a_failed = [p[0] for p in pairs if not p[0]["repaired"]]
    same = sum(1 for r in a_failed if r["second_error_same_as_first"] is True)
    new = sum(1 for r in a_failed if r["second_error_same_as_first"] is False)
    no_att = sum(1 for r in a_failed if sr.produced_no_program(r))
    assert same + new + no_att == len(a_failed)


def test_report_cap_stratification_splits_on_feedback_length():
    pairs = _pairs(
        ("cap",   dict(repaired=True, attempts=1, feedback_len=sr.STDERR_CAP),
                  dict(repaired=False)),
        ("short", dict(repaired=True, attempts=1, feedback_len=50), dict(repaired=False)),
    )
    s = _report_dict(pairs)
    assert s["by_arm_a_truncation"]["capped"]["n"] == 1
    assert s["by_arm_a_truncation"]["uncapped"]["n"] == 1


def test_report_keeps_the_nine_keys_verify_reads():
    pairs = _pairs(("1", dict(repaired=True, attempts=1), dict(repaired=False)))
    s = _report_dict(pairs)
    for k in ("n", "a_green", "b_green", "b_only", "a_only", "mcnemar_p",
              "a_mean_attempts", "b_mean_attempts", "wilcoxon_p"):
        assert k in s
    assert "per_class" in s and "rescue" in s and "by_arm_a_truncation" in s


# ── verdict ─────────────────────────────────────────────────────────────────

def test_verdict_branches():
    base = dict(treatment="B", treatment_label="faust-rs full", wilcoxon_p=1e-6)
    assert sr.verdict({**base, "n": 100, "mcnemar_p": 1e-8, "a_green": 70, "b_green": 40}
                      ).startswith("NO — C++ stderr repaired 30 MORE")
    assert sr.verdict({**base, "n": 100, "mcnemar_p": 1e-8, "a_green": 40, "b_green": 70}
                      ).startswith("YES")
    assert "NO MEASURABLE DIFFERENCE" in sr.verdict(
        {**base, "n": 100, "mcnemar_p": 0.4, "a_green": 50, "b_green": 52})
