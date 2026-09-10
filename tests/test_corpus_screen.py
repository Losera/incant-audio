#!/usr/bin/env python3
"""Unit tests for bench/corpus_screen.py — the mechanical "is this a Faust
program?" screen for the faust-rs repair corpus.

No faust, no network. Pure-function checks of the two clauses, plus a frozen
oracle: the screen over the *committed* corpus must exclude exactly these ten
code_shas and keep `42c2cb9dbd09f972` (a real program with a real paren bug).
If the screen drifts — widened to catch a real program, or narrowed to miss a
non-program — this breaks.
"""
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "bench"))

import corpus_screen as cs  # noqa: E402

CORPUS = ROOT / "bench" / "corpora" / "repair_corpus_20260830.json"

# The ten, derived outcome-blind and frozen here on purpose.
EXCLUDED = {
    "18143791cbe9ae50": "truncated_ellipsis",
    "a6246d365f065b7f": "no_process_definition",
    "b2034ebb445c6b22": "no_process_definition",
    "7014f6f5c4c25d40": "no_process_definition",
    "7d47498712538cc1": "no_process_definition",
    "305fb7da3bd041b1": "no_process_definition",
    "24144fc67fdcb390": "no_process_definition",
    "2263b138d48f9f90": "no_process_definition",
    "dbcdb04b4bbea4db": "no_process_definition",
    "317de3f3aae959cf": "no_process_definition",
}


# ── the two clauses, in isolation ────────────────────────────────────────────

def test_a_real_program_passes():
    ok, reason = cs.is_faust_program('import("stdfaust.lib");\nprocess = _ : *(0.5);')
    assert ok and reason is None


def test_process_with_paren_form_passes():
    ok, _ = cs.is_faust_program('process(x) = x + 1;\n')
    assert ok


def test_prose_fails_no_process():
    ok, reason = cs.is_faust_program(
        "I'm sorry, but as an AI language model, I do not have access to ...")
    # "..." is checked first, so this row would report truncated_ellipsis; use a
    # prose sample without an ellipsis to exercise the process clause directly:
    ok2, reason2 = cs.is_faust_program(
        "In a stereo session, a Y-split cable divides the mono bass signal into two")
    assert not ok2 and reason2 == "no_process_definition"


def test_truncated_ellipsis_fails_and_wins_precedence():
    # a program that also lacks `process` — the ellipsis clause reports first
    ok, reason = cs.is_faust_program('gain = hslider("g", 1, 0, 1, 0.01) : si.\n... ;')
    assert not ok and reason == "truncated_ellipsis"


def test_commented_process_does_not_count():
    ok, reason = cs.is_faust_program('// process = _;\nimport("stdfaust.lib");')
    assert not ok and reason == "no_process_definition"


# ── frozen oracle over the committed corpus ──────────────────────────────────

def test_frozen_corpus_excludes_exactly_these_ten():
    records = json.loads(CORPUS.read_text())
    _, excluded = cs.screen(records)
    got = {r["code_sha"]: r["screen_reason"] for r in excluded}
    assert got == EXCLUDED


def test_frozen_corpus_keeps_192():
    assert len(cs.included_shas(CORPUS)) == 192


def test_real_program_with_a_real_paren_bug_is_kept():
    # 42c2cb9dbd09f972 has 15 '(' vs 14 ')' — the exact row a syntax-repair
    # corpus should contain. A naive paren-balance rule would wrongly drop it.
    assert "42c2cb9dbd09f972" in cs.included_shas(CORPUS)


def test_excluded_rows_sidecar_shape():
    rows = cs.excluded_rows(CORPUS)
    assert len(rows) == 10
    for r in rows:
        assert set(r) == {"code_sha", "prompt_id", "tier",
                          "cpp_error_class", "screen_reason", "code_head"}
        assert len(r["code_head"]) <= 120


def test_committed_excluded_sidecar_matches_the_screen():
    sidecar = ROOT / "bench" / "corpora" / "repair_corpus_20260830_excluded.json"
    on_disk = {r["code_sha"]: r["screen_reason"] for r in json.loads(sidecar.read_text())}
    assert on_disk == EXCLUDED
