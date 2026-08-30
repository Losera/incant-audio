#!/usr/bin/env python3
"""Unit tests for bench/frs_check.py — the faust-rs diagnostic wrapper.

These run WITHOUT a faust-rs binary: the parser and renderer are exercised
against captured JSON fixtures (tests/fixtures/frs/*.json, real
`faust-rs 0.8.0 --check --error-format json` output), and `check()`'s
no-binary path is exercised directly. CI therefore gains no faust-rs
dependency — matching this repo's rule that faust-rs is a measurement tool,
never a project dependency (bench/frs_check.py module docstring).
"""
import json
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "bench"))
import frs_check  # noqa: E402

FIX = Path(__file__).resolve().parent / "fixtures" / "frs"


def _load(name: str) -> frs_check.FrsResult:
    payload = json.loads((FIX / f"{name}.json").read_text())
    return frs_check._parse_payload(payload)


# ── parsing ──────────────────────────────────────────────────────────────────

def test_success_payload_parses_as_ok():
    res = _load("success")
    assert res.ok
    assert res.status == "success"
    assert res.diagnostics == []
    assert res.primary is None
    assert res.version == "0.8.0"


def test_routing_arity_payload_flattens_code_and_location():
    res = _load("prop_0002")
    assert not res.ok
    assert res.codes == ["FRS-PROP-0002"]
    diag = res.primary
    assert diag.primary_line == 13          # C++ stderr gives NO line for this class
    assert diag.primary_col == 1
    assert "left outputs (2)" in diag.message and "right inputs (1)" in diag.message
    assert diag.help                        # faust-rs carries a remedy here


def test_undefined_symbol_payload_carries_help_and_suggestion():
    res = _load("eval_0002_undefined")
    assert res.codes == ["FRS-EVAL-0002"]
    assert res.primary.primary_line == 2
    assert any("define the symbol" in h for h in res.primary.help)


def test_multi_diagnostic_payload_keeps_all_error_codes():
    res = _load("parse_0001_multidef")
    assert len(res.diagnostics) >= 2
    assert all(c == "FRS-PARSE-0001" for c in res.codes)


# ── rendering ────────────────────────────────────────────────────────────────

def test_render_includes_code_message_and_help():
    out = frs_check.render(_load("prop_0002"))
    assert "[FRS-PROP-0002]" in out
    assert "at line 13" in out
    assert "help:" in out
    assert out.rstrip().endswith("re-emit the complete program.")


def test_render_splices_caret_line_when_source_given():
    payload = json.loads((FIX / "prop_0002.json").read_text())
    src = "\n".join(f"line{n}" for n in range(1, 13)) + "\nprocess = a : b;\n"
    out = frs_check.render(frs_check._parse_payload(payload), src)
    assert "13 | process = a : b;" in out
    assert "^" in out


def test_render_suppresses_box_expr_noise_notes():
    # prop_0002's notes include a huge `box_expr=BOXSEQ(...)` dump; it must not
    # reach the model — that noise is the whole reason for moving off C++ stderr.
    out = frs_check.render(_load("prop_0002"))
    assert "box_expr=" not in out
    assert "BOXSEQ" not in out


def test_render_trims_lr_parser_repair_sequence_list():
    out = frs_check.render(_load("parse_0001_multidef"))
    assert "Repair sequences found" not in out
    assert "Insert PAR" not in out


def test_render_on_success_result_is_harmless():
    assert "no actionable diagnostic" in frs_check.render(_load("success"))


# ── check() degradation ──────────────────────────────────────────────────────

def test_check_returns_none_when_no_binary(monkeypatch):
    monkeypatch.setattr(frs_check, "faust_rs_bin", lambda: None)
    assert frs_check.check("process = _;") is None


def test_faust_rs_bin_rejects_nonexistent_override(monkeypatch):
    monkeypatch.setenv("PLUGINFORGE_FAUST_RS_BIN", "/no/such/faust-rs")
    assert frs_check.faust_rs_bin() is None


@pytest.mark.skipif(frs_check.faust_rs_bin() is None,
                    reason="faust-rs binary not configured (PLUGINFORGE_FAUST_RS_BIN)")
def test_check_live_roundtrip_on_bad_program():
    res = frs_check.check("import(\"stdfaust.lib\");\nprocess = nope;\n")
    assert res is not None and not res.ok
    assert res.codes and res.codes[0].startswith("FRS-")
