#!/usr/bin/env python3
"""Unit tests for bench/repair_ab_repro/score_wp3.py's pairing logic.

No live data, no scipy call beyond what score_repair_ab already exercises —
this covers the generalization of load_pairs to arbitrary arm names, which is
the only new logic in this module (mcnemar_exact/_aggregate_cell are reused,
already tested in test_score_repair_ab.py).
"""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "bench"))
sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "bench" / "repair_ab_repro"))
import score_wp3  # noqa: E402


def _rec(sha, arm, repaired, attempts=1):
    return {"code_sha": sha, "arm": arm, "repair_model": "qwen2.5-coder:3b",
            "repaired": repaired, "attempts_to_green": attempts if repaired else None,
            "attempt_log": []}


def test_pair_by_sha_matches_two_named_arms():
    records = [
        _rec("a", "A2", True), _rec("a", "B2n", False),
        _rec("b", "A2", False), _rec("b", "B2n", True),
        _rec("c", "A2", True), _rec("c", "B2n", True),
    ]
    pairs = score_wp3.pair_by_sha(records, "A2", "B2n")
    assert len(pairs) == 3
    shas = {p[0]["code_sha"] for p in pairs}
    assert shas == {"a", "b", "c"}


def test_pair_by_sha_drops_sha_missing_either_arm():
    records = [
        _rec("a", "A2", True), _rec("a", "B2n", False),
        _rec("b", "A2", True),  # no B2n record for b
    ]
    pairs = score_wp3.pair_by_sha(records, "A2", "B2n")
    assert len(pairs) == 1
    assert pairs[0][0]["code_sha"] == "a"


def test_pair_by_sha_filters_by_model():
    records = [
        _rec("a", "A2", True), _rec("a", "B2n", False),
    ]
    records[1]["repair_model"] = "qwen2.5-coder:7b"
    pairs = score_wp3.pair_by_sha(records, "A2", "B2n", model="qwen2.5-coder:3b")
    assert pairs == []


def test_mcnemar_report_discordant_counts():
    pairs = [
        (_rec("a", "x", True), _rec("a", "y", False)),   # x_only
        (_rec("b", "x", False), _rec("b", "y", True)),   # y_only
        (_rec("c", "x", True), _rec("c", "y", True)),    # concordant green
        (_rec("d", "x", False), _rec("d", "y", False)),  # concordant fail
    ]
    out = score_wp3.mcnemar_report("test", pairs)
    assert out["n"] == 4
    assert out["x_green"] == 2
    assert out["y_green"] == 2
    assert out["x_only"] == 1
    assert out["y_only"] == 1
    assert out["mcnemar_p"] == 1.0
