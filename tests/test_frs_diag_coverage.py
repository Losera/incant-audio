#!/usr/bin/env python3
"""Unit tests for bench/frs_diag_coverage.py's pure helpers.

No faust-rs binary, no `faust`, no network — this covers the corpus-selection
logic only (the thing that was actually wrong before: 0/306 C++-accepted
records ever had faust-rs run on them). The subprocess-calling functions
(false_reject_screen, code_coverage, build_report) need live binaries and are
exercised manually (see docs/records/faust-rs-diagnostic-coverage-2026-10-09.md),
not in CI.
"""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "bench"))
import frs_diag_coverage as fdc  # noqa: E402


def test_distinct_accepted_keeps_first_occurrence_only():
    recs = [
        {"code_sha": "a", "compiles": True, "code": "x"},
        {"code_sha": "a", "compiles": True, "code": "x2"},  # dup sha, dropped
        {"code_sha": "b", "compiles": False, "code": "y"},  # not accepted
        {"code_sha": "c", "compiles": True, "code": "z"},
    ]
    out = fdc._distinct_accepted(recs)
    assert {r["code_sha"] for r in out} == {"a", "c"}
    assert len(out) == 2


def test_distinct_accepted_excludes_rejected_and_falsy_compiles():
    recs = [
        {"code_sha": "a", "compiles": False, "code": "x"},
        {"code_sha": "b", "compiles": None, "code": "y"},
    ]
    assert fdc._distinct_accepted(recs) == []


def test_known_codes_default_matches_catchall_membership():
    # Sanity: the catch-all codes this module flags specially must be a
    # subset of the known registry, or "never_fired" bookkeeping would be
    # silently wrong for them.
    assert fdc.CATCHALL_CODES <= set(fdc.KNOWN_CODES_DEFAULT)
