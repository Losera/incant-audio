#!/usr/bin/env python3
"""Unit tests for bench/repair_ab_core.py — the shared repair loop.

The point of the module is that bench/run_repair_ab.py (the canonical harness
that produced the committed numbers) and bench/repair_ab_repro/repair_ab_standalone.py
(the external-reproduction harness) run the SAME loop. These tests pin the
record shape, the stop-at-green behaviour, and the one piece the standalone had
to vendor rather than import (strip_code_fences).

No network, no faust, no faust-rs: `generate` and `validate_faust` are fakes.
"""
import sys
from pathlib import Path

import pytest

BENCH = Path(__file__).resolve().parent.parent / "bench"
sys.path.insert(0, str(BENCH))
sys.path.insert(0, str(BENCH / "repair_ab_repro"))

import repair_ab_core  # noqa: E402


def _entry(**over):
    e = {
        "prompt_id": "t/L1", "category": "trivial", "tier": "L1",
        "prompt": "a gentle lowpass", "config": "archive",
        "code": 'import("stdfaust.lib");\nprocess = _, _ <: _;',
        "code_sha": "deadbeefdeadbeef",
        "cpp_stderr": "ERROR : the number of outputs [2] must be equal to the number of inputs [1]",
    }
    e.update(over)
    return e


def _ok(_code):        # validate_faust fake: always compiles
    return True, ""


def _never(_code):     # validate_faust fake: never compiles
    return False, "ERROR : syntax error, unexpected foo"


def test_arm_a_uses_cpp_stderr_verbatim():
    seen = []
    repair_ab_core.repair_loop(_entry(), "A", lambda m: seen.append(m) or _entry()["code"],
                               "fake", _never)
    assert "the number of outputs [2]" in seen[0]
    assert seen[0].startswith("a gentle lowpass")


def test_stops_at_first_green():
    calls = []
    rec = repair_ab_core.repair_loop(
        _entry(), "A", lambda m: calls.append(m) or "x", "fake", _ok)
    assert rec["repaired"] is True
    assert rec["attempts_to_green"] == 1
    assert rec["attempts_used"] == 1
    assert len(calls) == 1                       # did not burn the 2nd attempt


def test_never_green_runs_full_budget():
    calls = []
    rec = repair_ab_core.repair_loop(
        _entry(), "A", lambda m: calls.append(m) or "x", "fake", _never)
    assert rec["repaired"] is False
    assert rec["attempts_to_green"] is None
    assert rec["attempts_used"] == repair_ab_core.CORRECTIVE_ATTEMPTS
    assert len(calls) == repair_ab_core.CORRECTIVE_ATTEMPTS


def test_record_has_the_schema_score_repair_ab_needs():
    rec = repair_ab_core.repair_loop(_entry(), "A", lambda m: "x", "fake", _never)
    for k in ("prompt_id", "category", "tier", "corpus_config", "code_sha",
              "first_error_class", "arm", "repair_model", "repaired",
              "attempts_to_green", "attempts_used", "second_error_class",
              "second_error_same_as_first", "attempt_log", "timestamp"):
        assert k in rec, k
    assert rec["first_error_class"] == "routing_arity"


def test_generator_exception_is_recorded_not_raised():
    def boom(_m):
        raise RuntimeError("transport died")
    rec = repair_ab_core.repair_loop(_entry(), "A", boom, "fake", _ok)
    assert rec["repaired"] is False
    assert "RuntimeError" in rec["attempt_log"][0]["error"]


def test_run_repair_ab_delegates_to_core():
    """The canonical harness must not carry its own copy of the loop."""
    import importlib
    import inspect
    mod = importlib.import_module("run_repair_ab")
    src = inspect.getsource(mod.repair_loop)
    assert "_repair_loop(" in src            # thin wrapper around the shared impl
    assert mod.load_corpus is repair_ab_core.load_corpus


@pytest.mark.parametrize("text", [
    "process = _;",
    "```faust\nprocess = _;\n```",
    "```\nprocess = _;\n```",
    "here you go:\n```faust\nprocess = _;\n```\nenjoy",
    "```faust\nprocess = _;",                 # missing closing fence
    "no fences at all\nprocess = _;",
    "```\n```",                               # degenerate
])
def test_strip_fences_parity_with_providers(text):
    """repair_ab_standalone vendors strip_code_fences from llm/providers.py.
    Diff the two on a shared input set so the vendored copy cannot drift."""
    import repair_ab_standalone
    import providers
    assert repair_ab_standalone.strip_code_fences(text) == providers.strip_code_fences(text)


def test_validate_faust_parity(monkeypatch):
    """arm A's feedback IS the C++ stderr. run_repair_ab injects
    run_benchmark.validate_faust; repair_ab_standalone vendors its own copy.
    If they truncate or shape the verdict differently, arm A diverges between
    the canonical harness and the reproduction one. Diff them without faust by
    faking subprocess.run — same as test_strip_fences_parity does for the other
    vendored piece."""
    import subprocess
    import repair_ab_standalone
    import run_benchmark

    # a rejecting compile with an over-long stderr -> exercises the [:500] cut
    long_err = "ERROR : " + "x" * 900
    monkeypatch.setattr(
        subprocess, "run",
        lambda *a, **k: subprocess.CompletedProcess(a, 1, stdout="", stderr=long_err))
    std = repair_ab_standalone.validate_faust("process = _;")
    canon = run_benchmark.validate_faust("process = _;")
    assert std == canon
    assert std[0] is False and len(std[1]) == 500      # identical truncation

    # the compiler wedges -> both must return a non-crash verdict, same text
    def boom(*a, **k):
        raise subprocess.TimeoutExpired(cmd="faust", timeout=30)
    monkeypatch.setattr(subprocess, "run", boom)
    assert repair_ab_standalone.validate_faust("x") == run_benchmark.validate_faust("x")


def _fake_frs_result():
    """A minimal FrsResult, independent of the real faust-rs binary -- WP3's
    feedback_for branches are exercised the same way arm A/B/C's already are
    (no network, no faust, no faust-rs), per this file's own stated rule."""
    import frs_check
    label = frs_check.FrsLabel(role="primary", line=3, col=12, message="here")
    diag = frs_check.FrsDiagnostic(
        code="FRS-PROP-0002", detail_code=None, stage="propagate",
        message="split composition mismatch", labels=[label],
        help=["try widening the input"], notes=["cause: divisibility rule"])
    return frs_check.FrsResult(status="failed", diagnostics=[diag], version="0.8.0")


def test_wp3_arms_cross_content_and_visibility(monkeypatch):
    """The five WP3 arms (WP3_PROTOCOL.md): A2 is untouched raw stderr; B2/C2
    show the source line, B2n/C2n withhold it; all four faust-rs arms drop
    render()'s own framing text (framing=False) since WP3_TEMPLATE supplies
    one shared wrapper instead."""
    import frs_check
    monkeypatch.setattr(frs_check, "check", lambda code: _fake_frs_result())

    code = 'import("stdfaust.lib");\n\nprocess = _, _ <: _, gain;'  # line 3 matches the fake label
    stderr = "ERROR : split composition A<:B"

    a2, a2_id = repair_ab_core.feedback_for("A2", code, stderr)
    assert a2 == stderr and a2_id is None          # untouched, same as arm A

    b2, b2_id = repair_ab_core.feedback_for("B2", code, stderr)
    b2n, b2n_id = repair_ab_core.feedback_for("B2n", code, stderr)
    c2, c2_id = repair_ab_core.feedback_for("C2", code, stderr)
    c2n, c2n_id = repair_ab_core.feedback_for("C2n", code, stderr)

    for fb in (b2, b2n, c2, c2n):
        assert "The Faust compiler rejected your program." not in fb  # framing=False
        assert "Fix this and re-emit the complete program." not in fb

    assert "process = _, _ <: _, gain;" in b2       # source shown
    assert "process = _, _ <: _, gain;" not in b2n  # source withheld
    assert "process = _, _ <: _, gain;" in c2
    assert "process = _, _ <: _, gain;" not in c2n

    assert "note:" in b2 and "help:" in b2          # full render keeps notes/help
    assert "note:" not in c2 and "help:" not in c2  # minimal render drops them

    assert b2_id == b2n_id == c2_id == c2n_id == "FRS-PROP-0002"


def test_wp3_template_is_shared_and_matches_arm_a(monkeypatch):
    """A2's wrapper must be byte-identical to arm A's, or A2 stops being a
    valid determinism check against the committed arm-A run
    (WP3_PROTOCOL.md's item 4)."""
    assert repair_ab_core.WP3_TEMPLATE == repair_ab_core.ARM_A_TEMPLATE


def test_wp3_leak_score_matches_l14_patterns():
    """run_wp3.py's leak_score(): 0/1/2 on the three real patterns L14 found
    (docs/BUGS.md PF-076 refinement, METHODOLOGY.md L14) -- a clean stderr,
    a widget fragment buried in a box-expression dump, and a full verbatim
    definition line."""
    sys.path.insert(0, str(BENCH / "repair_ab_repro"))
    import run_wp3

    clean = "ERROR : the number of outputs [2] must divide the number of inputs [1]"
    assert run_wp3.leak_score(clean) == 0

    fragment = (
        'while B = (_,(1,(hslider("Gain", 1.0f, 0.01f, 1e+01f, 0.01f) : '
        '\\(x13).(\\(x14).(((1.0f,(1,(44.1f,...')
    assert run_wp3.leak_score(fragment) == 1

    full_line = (
        "ERROR : multiple definitions of symbol 'buttonPress'\n"
        'buttonPress = hslider("Button Press [unit:Hz]", 100, 1, 1000, 1) : si.smoo;\n'
        "buttonPress = si.smoo(buttonPress);")
    assert run_wp3.leak_score(full_line) == 2
