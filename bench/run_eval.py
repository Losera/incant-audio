#!/usr/bin/env python3
"""Run the versioned, offline Incant Audio evaluation pilot.

The runner coordinates existing Faust metadata, render-oracle, and shipping-JIT
capture paths. It never invokes a model provider. Results distinguish PASS,
FAIL, ERROR, and ABSENT so missing evidence cannot become a green result.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
import os
import platform
import re
import shutil
import subprocess
import sys
import tempfile
import time
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

import numpy as np

ROOT = Path(__file__).resolve().parent.parent
BENCH = ROOT / "bench"
sys.path.insert(0, str(BENCH))

import p6_capture  # noqa: E402
import render_oracle  # noqa: E402

SCHEMA_VERSION = 1
GRADER_VERSION = "offline-pilot-v1"
VALID_KINDS = {"effect", "instrument"}
VALID_MODES = {"new", "add", "redo"}
STATUSES = {"pass", "fail", "error", "absent"}


class ContractError(ValueError):
    pass


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def file_hash(path: Path) -> str:
    return sha256_bytes(path.read_bytes())


def json_safe(value: Any) -> Any:
    """Replace non-finite measurements with null; strict JSON has no infinities."""
    if isinstance(value, float) and not math.isfinite(value):
        return None
    if isinstance(value, dict):
        return {key: json_safe(item) for key, item in value.items()}
    if isinstance(value, (list, tuple)):
        return [json_safe(item) for item in value]
    return value


def validate_case_set(data: dict[str, Any], base: Path) -> list[dict[str, Any]]:
    if data.get("schema_version") != SCHEMA_VERSION:
        raise ContractError(f"case-set schema_version must be {SCHEMA_VERSION}")
    cases = data.get("cases")
    if not isinstance(cases, list) or not cases:
        raise ContractError("cases must be a non-empty list")
    seen: set[str] = set()
    for i, case in enumerate(cases):
        where = f"cases[{i}]"
        required = ("id", "kind", "mode", "fixture", "expected_outcome", "expect")
        missing = [key for key in required if key not in case]
        if missing:
            raise ContractError(f"{where} missing {', '.join(missing)}")
        if case["id"] in seen:
            raise ContractError(f"duplicate case id {case['id']}")
        seen.add(case["id"])
        if case["kind"] not in VALID_KINDS or case["mode"] not in VALID_MODES:
            raise ContractError(f"{where} has invalid kind or mode")
        if case["expected_outcome"] not in {"pass", "fail"}:
            raise ContractError(f"{where} expected_outcome must be pass or fail")
        if case["expected_outcome"] == "fail" and not case.get("expected_failure"):
            raise ContractError(f"{where} negative control needs expected_failure")
        for key in ("fixture", "starting_fixture"):
            if key in case and not (base / case[key]).is_file():
                raise ContractError(f"{where} {key} does not exist: {case[key]}")
    return cases


def _tool_output(command: list[str]) -> str | None:
    if shutil.which(command[0]) is None:
        return None
    try:
        proc = subprocess.run(command, capture_output=True, text=True, timeout=10,
                              encoding="utf-8", errors="replace")
    except (OSError, subprocess.TimeoutExpired):
        return None
    text = (proc.stdout or proc.stderr).strip()
    return text.splitlines()[0] if text else None


def build_manifest(case_file: Path, started: str) -> dict[str, Any]:
    git_commit = _tool_output(["git", "rev-parse", "HEAD"])
    dirty = None
    if shutil.which("git"):
        proc = subprocess.run(["git", "status", "--porcelain"], cwd=ROOT,
                              capture_output=True, text=True, timeout=10)
        dirty = bool(proc.stdout.strip()) if proc.returncode == 0 else None
    prompt_hashes = {}
    for name in ("system_prompt.txt", "instrument_prompt.txt"):
        path = ROOT / "llm" / "prompts" / name
        prompt_hashes[name] = file_hash(path) if path.exists() else None
    return {
        "schema_version": SCHEMA_VERSION,
        "runner_version": GRADER_VERSION,
        "git_commit": git_commit,
        "dirty": dirty,
        "case_set_sha256": file_hash(case_file),
        "prompt_sha256": prompt_hashes,
        "provider": None,
        "model": None,
        "python": platform.python_version(),
        "faust": _tool_output(["faust", "--version"]),
        "llvm": _tool_output(["llvm-config", "--version"]),
        "compiler": _tool_output(["c++", "--version"]),
        "juce": "7.0.9 (repository-supported external dependency; not probed)",
        "os": platform.platform(),
        "architecture": platform.machine(),
        "cpu": platform.processor() or None,
        "sample_rate_hz": render_oracle.SR,
        "block_size": None,
        "started_at": started,
        "ended_at": None,
    }


def _flatten_ui(items: list[dict[str, Any]]) -> list[dict[str, Any]]:
    controls: list[dict[str, Any]] = []
    for item in items:
        if isinstance(item.get("items"), list):
            controls.extend(_flatten_ui(item["items"]))
        elif item.get("type") not in {"vbargraph", "hbargraph"}:
            controls.append({key: item.get(key) for key in
                             ("label", "type", "init", "min", "max", "step", "meta")})
    return controls


def compile_metadata(source: str) -> dict[str, Any]:
    if shutil.which("faust") is None:
        return {"status": "absent", "failure_stage": "INFRA",
                "failure_code": "INFRA.MISSING_TOOL", "detail": "faust not on PATH"}
    started = time.perf_counter()
    with tempfile.TemporaryDirectory() as td:
        work = Path(td)
        (work / "patch.dsp").write_text(source, encoding="utf-8")
        try:
            proc = subprocess.run(["faust", "-json", "patch.dsp", "-o", "/dev/null"],
                                  cwd=work, capture_output=True, text=True, timeout=60,
                                  encoding="utf-8", errors="replace")
        except subprocess.TimeoutExpired:
            return {"status": "error", "failure_stage": "COMPILE",
                    "failure_code": "COMPILE.TIMEOUT", "detail": "faust exceeded 60s"}
        meta_path = work / "patch.dsp.json"
        if proc.returncode or not meta_path.exists():
            detail = (proc.stderr or proc.stdout or "no compiler output").strip()[-1200:]
            return {"status": "fail", "failure_stage": "COMPILE",
                    "failure_code": "COMPILE.FAUST_ERROR", "detail": detail,
                    "duration_ms": round((time.perf_counter()-started)*1000, 3)}
        meta = json.loads(meta_path.read_text(encoding="utf-8"))
    return {"status": "pass", "inputs": int(meta.get("inputs", 0)),
            "outputs": int(meta.get("outputs", 0)), "controls": _flatten_ui(meta.get("ui", [])),
            "duration_ms": round((time.perf_counter()-started)*1000, 3)}


def _control_by_label(controls: list[dict[str, Any]], label: str) -> dict[str, Any] | None:
    wanted = label.casefold()
    return next((c for c in controls if str(c.get("label", "")).casefold() == wanted), None)


def _unit(control: dict[str, Any]) -> str | None:
    for item in control.get("meta") or []:
        if isinstance(item, dict) and "unit" in item:
            return str(item["unit"])
    match = re.search(r"\[unit:([^\]]+)\]", str(control.get("label", "")))
    return match.group(1) if match else None


def _add(checks: list[dict[str, Any]], layer: str, name: str, passed: bool,
         failure_code: str, observed: Any = None) -> None:
    checks.append({"layer": layer, "check": name, "status": "pass" if passed else "fail",
                   "failure_code": None if passed else failure_code, "observed": observed})


def failure_stage_for(code: str | None) -> str | None:
    if not code:
        return None
    prefix = code.split(".", 1)[0]
    return {"VALIDATION": "VALIDATION", "COMPILE": "COMPILE", "REFINEMENT": "REFINEMENT",
            "JIT": "JIT", "DSP": "DSP", "INFRA": "INFRA"}.get(prefix, prefix)


def grade_metadata(expect: dict[str, Any], meta: dict[str, Any], checks: list[dict[str, Any]]) -> None:
    io = expect.get("io", {})
    if io:
        _add(checks, "L1", "input_arity", meta["inputs"] in io.get("inputs", []),
             "VALIDATION.WRONG_ARITY", meta["inputs"])
        _add(checks, "L1", "output_arity", meta["outputs"] in io.get("outputs", []),
             "VALIDATION.WRONG_ARITY", meta["outputs"])
    for wanted in expect.get("controls", []):
        control = _control_by_label(meta["controls"], wanted["label"])
        _add(checks, "L2", f"control:{wanted['label']}", control is not None,
             "VALIDATION.MISSING_CONTROL", control)
        if control is not None and "unit" in wanted:
            _add(checks, "L2", f"unit:{wanted['label']}", _unit(control) == wanted["unit"],
                 "VALIDATION.WRONG_CONTROL_UNIT", _unit(control))


def grade_refinement(case: dict[str, Any], candidate: dict[str, Any], checks: list[dict[str, Any]],
                     fixture_base: Path) -> None:
    start_source = (fixture_base / case["starting_fixture"]).read_text(encoding="utf-8")
    start = compile_metadata(start_source)
    if start["status"] != "pass":
        checks.append({"layer": "L2", "check": "starting_fixture", "status": start["status"],
                       "failure_code": start.get("failure_code"), "observed": start.get("detail")})
        return
    for label in case["expect"].get("preserve", []):
        before = _control_by_label(start["controls"], label)
        after = _control_by_label(candidate["controls"], label)
        keys = ("type", "init", "min", "max", "step")
        same = before is not None and after is not None and all(before.get(k) == after.get(k) for k in keys)
        _add(checks, "L2", f"preserve:{label}", same, "REFINEMENT.CONTROL_LOSS",
             {"before": before, "after": after})
    change = case["expect"].get("change_default")
    if change:
        before = _control_by_label(start["controls"], change["label"])
        after = _control_by_label(candidate["controls"], change["label"])
        changed = (before is not None and after is not None
                   and before.get("init") == change["from"] and after.get("init") == change["to"])
        _add(checks, "L2", f"requested_change:{change['label']}", changed,
             "REFINEMENT.REQUESTED_CHANGE_MISSING", {"before": before, "after": after})


def harmonic_distortion_measurement(source: str, frequency_hz: float = 997.0) -> dict[str, float]:
    """Measure added harmonic energy from a low-distortion sine probe."""
    sr, duration = render_oracle.SR, render_oracle.DUR
    samples = np.arange(int(sr * duration), dtype=np.float64)
    mono = 0.2 * np.sin(2 * np.pi * frequency_hz * samples / sr)
    signal = np.repeat(mono[:, None], 2, axis=1).astype(np.float32)
    x, y = render_oracle.render(source, signal)

    def ratio_db(values: np.ndarray) -> float:
        channel = values[:, 0] * np.hanning(values.shape[0])
        spectrum = np.abs(np.fft.rfft(channel)) ** 2
        frequencies = np.fft.rfftfreq(channel.size, 1 / sr)
        fundamental = int(np.argmin(np.abs(frequencies - frequency_hz)))
        fundamental_energy = spectrum[max(0, fundamental-1):fundamental+2].sum()
        harmonic_energy = 0.0
        for multiple in range(2, 9):
            target = multiple * frequency_hz
            if target >= sr / 2:
                break
            index = int(np.argmin(np.abs(frequencies - target)))
            harmonic_energy += spectrum[index-1:index+2].sum()
        return float(10 * np.log10((harmonic_energy + 1e-30) / (fundamental_energy + 1e-30)))

    input_db, output_db = ratio_db(x), ratio_db(y)
    return {"input_harmonics_dbc": input_db, "output_harmonics_dbc": output_db,
            "increase_db": output_db - input_db, "frequency_hz": frequency_hz}


def amplitude_modulation_measurement(source: str) -> dict[str, float]:
    """Measure the dominant envelope rate and normalized peak-to-peak depth."""
    sr, duration = render_oracle.SR, render_oracle.DUR
    signal = np.full((int(sr * duration), 2), 0.25, dtype=np.float32)
    _, y = render_oracle.render(source, signal)
    envelope = np.abs(y[:, 0])
    lo, hi = float(envelope.min()), float(envelope.max())
    depth = (hi - lo) / hi if hi > 0 else 0.0
    centered = envelope - envelope.mean()
    spectrum = np.abs(np.fft.rfft(centered * np.hanning(centered.size)))
    frequencies = np.fft.rfftfreq(centered.size, 1 / sr)
    band = (frequencies >= 0.1) & (frequencies <= 20.0)
    rate = float(frequencies[np.flatnonzero(band)[int(np.argmax(spectrum[band]))]])
    return {"rate_hz": rate, "depth": float(depth), "minimum": lo, "maximum": hi}


def grade_audio(expect: dict[str, Any], report: dict[str, Any], checks: list[dict[str, Any]],
                source: str) -> None:
    if not report.get("rendered"):
        status = "absent" if report.get("unsupported") else "error"
        checks.append({"layer": "L5", "check": "render", "status": status,
                       "failure_code": "INFRA.UNSUPPORTED_RENDER" if status == "absent" else "DSP.RENDER_ERROR",
                       "observed": report.get("error")})
        return
    measurement, feats = report["measurement"], report["features"]
    for probe in expect.get("audio", []):
        kind = probe["probe"]
        if kind == "safe":
            code = "DSP.SILENT" if "silent" in measurement["codes"] else (
                "DSP.NEVER_DECAYS" if "never_decays" in measurement["codes"] else "DSP.UNSAFE_OUTPUT")
            _add(checks, "L5", kind, measurement["ok"], code, measurement)
        elif kind == "band_below":
            value = feats["band_gain_db"][probe["band"]]
            _add(checks, "L5", f"band_below:{probe['band']}", value < probe["max_db"],
                 "DSP.WRONG_FREQUENCY_RESPONSE", value)
        elif kind == "band_range":
            values = {band: feats["band_gain_db"][band] for band in probe["bands"]}
            passed = all(probe["min_db"] < value < probe["max_db"] for value in values.values())
            _add(checks, "L5", "band_range", passed, "DSP.WRONG_FREQUENCY_RESPONSE", values)
        elif kind == "tail_at_least":
            tail = report["tail"]
            passed = tail["evaluated"] and tail["tail_ms"] >= probe["value_ms"]
            _add(checks, "L5", "tail_at_least", passed, "DSP.MISSING_TAIL", tail)
        elif kind == "harmonic_increase":
            try:
                value = harmonic_distortion_measurement(source)
            except render_oracle.RenderError as exc:
                checks.append({"layer": "L5", "check": kind, "status": "error",
                               "failure_code": "DSP.RENDER_ERROR", "observed": str(exc)})
            else:
                passed = (value["increase_db"] >= probe["min_db"]
                          and value["output_harmonics_dbc"] >= probe["output_min_dbc"])
                _add(checks, "L5", kind, passed,
                     "DSP.MISSING_HARMONIC_DISTORTION", value)
        elif kind == "amplitude_modulation":
            try:
                value = amplitude_modulation_measurement(source)
            except render_oracle.RenderError as exc:
                checks.append({"layer": "L5", "check": kind, "status": "error",
                               "failure_code": "DSP.RENDER_ERROR", "observed": str(exc)})
            else:
                rate_ok = probe["rate_hz"][0] <= value["rate_hz"] <= probe["rate_hz"][1]
                depth_ok = probe["depth"][0] <= value["depth"] <= probe["depth"][1]
                _add(checks, "L5", kind, rate_ok and depth_ok,
                     "DSP.WRONG_AMPLITUDE_MODULATION", value)
        else:
            raise ContractError(f"unknown audio probe {kind}")


def grade_jit(case: dict[str, Any], source_path: Path, checks: list[dict[str, Any]], artifact_dir: Path) -> None:
    spec = case["expect"].get("jit")
    if not spec:
        return
    binary = p6_capture.find_capture_binary()
    if binary is None:
        checks.append({"layer": "L3", "check": "jit_capture", "status": "absent",
                       "failure_code": "INFRA.MISSING_TOOL", "observed": "OfflineRenderTest not built"})
        return
    wav_path = artifact_dir / f"{case['id']}.wav"
    # Codex/CI debuggers may execute this ASan binary under ptrace, where
    # LeakSanitizer cannot operate. Leak detection is outside this capture's
    # pitch claim; ASan abort and UBSan halt remain enabled.
    env = {**os.environ, "ASAN_OPTIONS": "abort_on_error=1:detect_leaks=0",
           "UBSAN_OPTIONS": "print_stacktrace=1:halt_on_error=1"}
    command = [str(binary), "--capture", str(source_path), str(wav_path),
               "--note", str(spec["pitch_midi"])]
    try:
        proc = subprocess.run(command, capture_output=True, text=True, timeout=180, env=env)
    except subprocess.TimeoutExpired:
        capture = {"ok": False, "why": "JIT capture exceeded 180s"}
    else:
        capture = {"ok": False, "why": (proc.stdout + proc.stderr).strip()[-1200:]}
        for raw in proc.stdout.splitlines():
            if raw.startswith("CAPTURE_OK"):
                capture = {"ok": True, **dict(token.split("=", 1) for token in raw.split()[1:])}
                capture["wav"] = wav_path.name
                break
    if not capture.get("ok"):
        checks.append({"layer": "L3", "check": "jit_capture", "status": "fail",
                       "failure_code": "JIT.CAPTURE_FAILED", "observed": capture})
        return
    pitch = render_oracle.pitch_of_wav(wav_path, spec["pitch_midi"], int(capture.get("held_end", 0)))
    _add(checks, "L3", "jit_capture", True, "JIT.CAPTURE_FAILED", capture)
    _add(checks, "L5", "pitch", pitch.evaluated and pitch.in_tune,
         "DSP.WRONG_PITCH", vars(pitch))


def run_case(case: dict[str, Any], artifact_dir: Path, fixture_base: Path = BENCH) -> dict[str, Any]:
    started = time.perf_counter()
    source_path = fixture_base / case["fixture"]
    source = source_path.read_text(encoding="utf-8")
    checks: list[dict[str, Any]] = []
    meta = compile_metadata(source)
    if meta["status"] == "pass":
        _add(checks, "L1", "compile", True, "COMPILE.FAUST_ERROR", meta["duration_ms"])
        grade_metadata(case["expect"], meta, checks)
        if case.get("starting_fixture"):
            grade_refinement(case, meta, checks, fixture_base)
        if case["expect"].get("audio"):
            grade_audio(case["expect"], render_oracle.analyse(source), checks, source)
        grade_jit(case, source_path, checks, artifact_dir)
    else:
        checks.append({"layer": "L1", "check": "compile", "status": meta["status"],
                       "failure_code": meta.get("failure_code"), "observed": meta.get("detail")})
    statuses = {check["status"] for check in checks}
    status = "error" if "error" in statuses else "absent" if "absent" in statuses else (
        "fail" if "fail" in statuses else "pass")
    failures = [check["failure_code"] for check in checks if check["status"] == "fail"]
    expected_met = (status == case["expected_outcome"] and
                    (status == "pass" or case.get("expected_failure") in failures))
    first_failure = next((c for c in checks if c["status"] != "pass"), None)
    return {"schema_version": SCHEMA_VERSION, "case_id": case["id"], "kind": case["kind"],
            "mode": case["mode"], "status": status, "expected_outcome": case["expected_outcome"],
            "expected_failure": case.get("expected_failure"), "expected_met": expected_met,
            "source_sha256": sha256_bytes(source.encode()), "source_archive": case["fixture"],
            "grader_version": GRADER_VERSION, "duration_ms": round((time.perf_counter()-started)*1000, 3),
            "failure_stage": failure_stage_for(first_failure["failure_code"]) if first_failure else None,
            "failure_code": next((c["failure_code"] for c in checks if c["status"] != "pass"), None),
            "checks": checks}


def markdown_report(run: dict[str, Any]) -> str:
    manifest = run["manifest"]
    lines = ["# Incant Audio offline eval", "", f"- Run: `{run['run_id']}`",
             f"- Commit: `{manifest['git_commit']}` (dirty: `{manifest['dirty']}`)",
             f"- Cases: {run['summary']['total']}; expected outcomes met: {run['summary']['expected_met']}",
             f"- Unexpected: {run['summary']['unexpected']}; absent: {run['summary']['absent']}", "",
             "| Case | Actual | Expected | Contract | Failure |", "|---|---:|---:|---:|---|" ]
    for result in run["results"]:
        lines.append(f"| `{result['case_id']}` | {result['status']} | {result['expected_outcome']} | "
                     f"{'met' if result['expected_met'] else 'NOT MET'} | {result['failure_code'] or ''} |")
    lines += ["", "## Next evidence", "", "Reproduce with:", "",
              "```bash", "python bench/run_eval.py", "```", ""]
    return "\n".join(lines)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cases", type=Path, default=BENCH / "cases.json")
    parser.add_argument("--out", type=Path, default=None,
                        help="output directory (default: artifacts/eval_<UTC timestamp>)")
    args = parser.parse_args(argv)
    case_file = args.cases.resolve()
    started_dt = datetime.now(timezone.utc)
    started = started_dt.isoformat()
    try:
        case_data = json.loads(case_file.read_text(encoding="utf-8"))
        cases = validate_case_set(case_data, case_file.parent)
    except (OSError, json.JSONDecodeError, ContractError) as exc:
        print(f"case contract error: {exc}", file=sys.stderr)
        return 2
    out = args.out or ROOT / "artifacts" / f"eval_{started_dt.strftime('%Y%m%dT%H%M%SZ')}"
    out.mkdir(parents=True, exist_ok=False)
    manifest = build_manifest(case_file, started)
    results = []
    for case in cases:
        result = run_case(case, out, case_file.parent)
        results.append(result)
        print(f"{result['status'].upper():7} {case['id']} "
              f"(expected {case['expected_outcome']}, {'met' if result['expected_met'] else 'UNEXPECTED'})")
    manifest["ended_at"] = datetime.now(timezone.utc).isoformat()
    run = {"schema_version": SCHEMA_VERSION, "run_id": out.name, "manifest": manifest,
           "summary": {"total": len(results), "expected_met": sum(r["expected_met"] for r in results),
                       "unexpected": sum(not r["expected_met"] for r in results),
                       "absent": sum(r["status"] == "absent" for r in results)},
           "results": results}
    (out / "results.json").write_text(
        json.dumps(json_safe(run), indent=2, allow_nan=False) + "\n", encoding="utf-8")
    (out / "report.md").write_text(markdown_report(run), encoding="utf-8")
    print(f"\n{run['summary']['expected_met']}/{len(results)} expected outcomes met; reports in {out}")
    return 1 if run["summary"]["unexpected"] else 0


if __name__ == "__main__":
    raise SystemExit(main())
