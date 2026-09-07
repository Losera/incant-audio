"""Contract and grading tests for the offline eval pilot."""
import json
from pathlib import Path

import pytest

from bench import run_eval


def test_committed_case_contract_is_valid():
    path = run_eval.BENCH / "cases.json"
    cases = run_eval.validate_case_set(json.loads(path.read_text()), path.parent)
    assert len(cases) == 12
    assert {case["expected_outcome"] for case in cases} == {"pass", "fail"}


def test_missing_fixture_is_a_contract_error(tmp_path):
    data = {"schema_version": 1, "cases": [{
        "id": "x", "kind": "effect", "mode": "new", "fixture": "missing.dsp",
        "expected_outcome": "pass", "expect": {"compile": True},
    }]}
    with pytest.raises(run_eval.ContractError, match="does not exist"):
        run_eval.validate_case_set(data, tmp_path)


def test_missing_compiler_is_absent_not_pass(monkeypatch):
    monkeypatch.setattr(run_eval.shutil, "which", lambda _: None)
    result = run_eval.compile_metadata("process = _;")
    assert result["status"] == "absent"
    assert result["failure_code"] == "INFRA.MISSING_TOOL"


def test_ui_flattening_retains_control_contract_fields():
    ui = [{"type": "vgroup", "items": [{
        "type": "hslider", "label": "Gain", "init": 0, "min": -60,
        "max": 12, "step": 0.1, "meta": [{"unit": "dB"}],
    }]}]
    assert run_eval._flatten_ui(ui) == [{
        "label": "Gain", "type": "hslider", "init": 0, "min": -60,
        "max": 12, "step": 0.1, "meta": [{"unit": "dB"}],
    }]


def test_silence_negative_control_requires_the_named_failure(monkeypatch, tmp_path):
    fixture = tmp_path / "silent.dsp"
    fixture.write_text("process = _*0, _*0;")
    case = {"id": "silent", "kind": "effect", "mode": "new",
            "fixture": "silent.dsp", "expected_outcome": "fail",
            "expected_failure": "DSP.SILENT",
            "expect": {"audio": [{"probe": "safe"}]}}
    monkeypatch.setattr(run_eval, "compile_metadata", lambda _: {
        "status": "pass", "inputs": 2, "outputs": 2, "controls": [], "duration_ms": 1})
    monkeypatch.setattr(run_eval.render_oracle, "analyse", lambda _: {
        "rendered": True,
        "measurement": {"ok": False, "codes": ["silent"]},
        "features": {}, "tail": {},
    })
    result = run_eval.run_case(case, tmp_path, tmp_path)
    assert result["status"] == "fail"
    assert result["failure_code"] == "DSP.SILENT"
    assert result["expected_met"] is True


def test_distortion_semantic_probe_has_a_red_control(monkeypatch):
    report = {"rendered": True, "measurement": {"ok": True, "codes": []},
              "features": {}, "tail": {}}
    monkeypatch.setattr(run_eval, "harmonic_distortion_measurement",
                        lambda _: {"increase_db": 0.0, "output_harmonics_dbc": -180.0})
    checks = []
    run_eval.grade_audio({"audio": [{"probe": "harmonic_increase", "min_db": 20,
                                      "output_min_dbc": -40}]},
                         report, checks, "process = _, _;")
    assert checks[-1]["status"] == "fail"
    assert checks[-1]["failure_code"] == "DSP.MISSING_HARMONIC_DISTORTION"


def test_tremolo_semantic_probe_has_a_red_control(monkeypatch):
    report = {"rendered": True, "measurement": {"ok": True, "codes": []},
              "features": {}, "tail": {}}
    monkeypatch.setattr(run_eval, "amplitude_modulation_measurement",
                        lambda _: {"rate_hz": 0.5, "depth": 0.0})
    checks = []
    spec = {"audio": [{"probe": "amplitude_modulation",
                        "rate_hz": [4.5, 5.5], "depth": [0.45, 0.55]}]}
    run_eval.grade_audio(spec, report, checks, "process = _, _;")
    assert checks[-1]["status"] == "fail"
    assert checks[-1]["failure_code"] == "DSP.WRONG_AMPLITUDE_MODULATION"


@pytest.mark.integration
def test_offline_pilot_end_to_end(tmp_path):
    """Expensive compiler/render/JIT path; tools/check.sh fast deselects it."""
    assert run_eval.main(["--out", str(tmp_path / "run")]) == 0
    report = json.loads((tmp_path / "run" / "results.json").read_text())
    assert report["summary"] == {
        "total": 12, "expected_met": 12, "unexpected": 0, "absent": 0}
