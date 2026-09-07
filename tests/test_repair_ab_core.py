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
