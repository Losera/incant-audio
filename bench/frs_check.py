#!/usr/bin/env python3
"""bench/frs_check.py — faust-rs (`--check --error-format json`) as a diagnostic oracle.

WHY THIS EXISTS
    Issue #26 (Losera/incant-audio) asks whether faust-rs's richer compile
    diagnostics shorten PluginForge's generate -> compile -> correct loop. The
    2026-08-28 measurement that seeded the question used a throwaway
    `scratchpad/frs_annotate.py` which was never committed and is gone, leaving
    `docs/BUGS.md`'s faust-rs figures unreproducible from a clean checkout. This
    module is the committed replacement: a stable wrapper the repair-loop A/B
    (`bench/run_repair_ab.py`) and any re-derivation of the #26 numbers both call.

NOT A PROJECT DEPENDENCY
    faust-rs is a Rust workspace built out-of-tree, not on crates.io and not in
    `requirements.txt`. Point `PLUGINFORGE_FAUST_RS_BIN` at the built binary
    (`target/release/faust-rs`); with it unset this module looks for `faust-rs`
    on PATH. `check()` returns None when no binary is found, so callers degrade
    to C++-only rather than crashing.

WHAT faust-rs GIVES (measured 2026-08-30, faust-rs 0.8.0, over the 15 archived
never-compiled cells in bench/results/efficacy/efficacy_ollama_20260828.json):
    - a stable `FRS-*` code on 15/15   (C++ stderr: 0/15)
    - a source line:col on 15/15       (C++ stderr: 8/15)
    - a `help` remedy array on 10/15   (the 5 without are FRS-PARSE-0001, which
                                        instead carry LR-parser repair sequences)
    - the offending source TEXT: 0/15. faust-rs does not embed source text in the
      JSON for file input and exposes no CLI flag to change that
      (`SourceTextPolicy` is code-only). `render()` therefore splices the
      offending line from the caller's own copy of the source, using the
      `compatibility_span` line/col.
"""
from __future__ import annotations

import hashlib
import json
import os
import shutil
import subprocess
import tempfile
from dataclasses import dataclass, field
from pathlib import Path


def sha(text: str) -> str:
    """Short content hash — the identity key for a Faust program across the
    corpus and A/B harnesses."""
    return hashlib.sha256(text.encode("utf-8")).hexdigest()[:16]

# Notes carrying compiler-internal box dumps — as noisy for an LLM as the C++
# box-expression dump this whole exercise is trying to get away from. Dropped by
# `render()`; the prose notes (cause/rule/computed/arity) are kept.
_NOISE_NOTE_MARKERS = ("box_expr=", "expr=", "node_id=", "(seq left)", "(seq right)",
                       "= BOX", "binding_trace=", "scope.local=", "scope.visible=",
                       "scope.top_level=")

_CHECK_TIMEOUT_S = 20.0


def faust_rs_bin() -> str | None:
    """The faust-rs binary to use, or None if none is configured/installed."""
    override = os.environ.get("PLUGINFORGE_FAUST_RS_BIN")
    if override:
        return override if Path(override).is_file() else None
    return shutil.which("faust-rs")


@dataclass
class FrsLabel:
    role: str
    line: int | None
    col: int | None
    message: str


@dataclass
class FrsDiagnostic:
    """One faust-rs error diagnostic, flattened to what the repair loop needs."""
    code: str                         # e.g. "FRS-PROP-0002"
    detail_code: str | None
    stage: str | None
    message: str
    labels: list[FrsLabel] = field(default_factory=list)
    help: list[str] = field(default_factory=list)
    notes: list[str] = field(default_factory=list)
    raw: dict = field(default_factory=dict)

    @property
    def primary_line(self) -> int | None:
        for lbl in self.labels:
            if lbl.line:
                return lbl.line
        return None

    @property
    def primary_col(self) -> int | None:
        for lbl in self.labels:
            if lbl.line:
                return lbl.col
        return None


@dataclass
class FrsResult:
    status: str                       # "success" | "failed"
    diagnostics: list[FrsDiagnostic]
    version: str | None
    raw: dict = field(default_factory=dict)

    @property
    def ok(self) -> bool:
        return self.status == "success"

    @property
    def primary(self) -> FrsDiagnostic | None:
        return self.diagnostics[0] if self.diagnostics else None

    @property
    def codes(self) -> list[str]:
        return [d.code for d in self.diagnostics]


def _parse_payload(payload: dict) -> FrsResult:
    diags: list[FrsDiagnostic] = []
    for d in payload.get("diagnostics", []):
        if d.get("severity") != "error":
            continue
        labels = []
        for lbl in d.get("labels", []):
            span = lbl.get("compatibility_span") or {}
            labels.append(FrsLabel(
                role=lbl.get("role", ""),
                line=span.get("line"),
                col=span.get("col"),
                message=lbl.get("message", ""),
            ))
        diags.append(FrsDiagnostic(
            code=d.get("code") or "FRS-UNKNOWN",
            detail_code=d.get("detail_code"),
            stage=d.get("stage"),
            message=d.get("message", ""),
            labels=labels,
            help=list(d.get("help") or []),
            notes=list(d.get("notes") or []),
            raw=d,
        ))
    return FrsResult(
        status=payload.get("status", "failed"),
        diagnostics=diags,
        version=(payload.get("compiler") or {}).get("version"),
        raw=payload,
    )


def check(dsp_source: str, *, bin_path: str | None = None) -> FrsResult | None:
    """Run `faust-rs --check --error-format json` on a Faust source string.

    Returns None when no faust-rs binary is available or it did not emit JSON —
    the caller should then fall back to the C++ verdict alone. A compile failure
    is a normal FrsResult with status == "failed", not None.
    """
    binary = bin_path or faust_rs_bin()
    if not binary:
        return None
    with tempfile.NamedTemporaryFile(suffix=".dsp", mode="w", encoding="utf-8",
                                     delete=False) as fh:
        fh.write(dsp_source)
        src = fh.name
    try:
        proc = subprocess.run(
            [binary, "--check", "--error-format", "json", src],
            capture_output=True, text=True, timeout=_CHECK_TIMEOUT_S,
            encoding="utf-8", errors="replace",
        )
    except (OSError, subprocess.TimeoutExpired):
        return None
    finally:
        os.unlink(src)
    try:
        payload = json.loads(proc.stdout)
    except json.JSONDecodeError:
        return None
    return _parse_payload(payload)


def _clean_message(msg: str) -> str:
    """Trim faust-rs's noisiest message tail — the FRS-PARSE-0001 LR-parser
    'Repair sequences found' token-insertion list, which runs 20+ lines and is
    parser internals, not a repair the model can act on."""
    for marker in ("Repair sequences found", "No repair sequences found"):
        idx = msg.find(marker)
        if idx != -1:
            return msg[:idx].rstrip().rstrip(".")
    return msg


def _source_caret(source: str, line: int | None, col: int | None) -> list[str]:
    if not line:
        return []
    lines = source.splitlines()
    if line < 1 or line > len(lines):
        return []
    text = lines[line - 1]
    out = [f"  {line:>4} | {text}"]
    if col and col >= 1:
        out.append(f"       | {' ' * (col - 1)}^")
    return out


def render(result: FrsResult, source: str | None = None, *, max_notes: int = 4) -> str:
    """The arm-B feedback string: what the model sees INSTEAD of raw C++ stderr.

    Human-readable, not raw JSON — dumping JSON at a 7B would test our
    formatting, not faust-rs. Keeps: the stable code, the concrete message
    (arities included), the source line:col, a spliced caret line when `source`
    is given, the prose notes, and the `help` remedy lines. Drops: the
    box-expression dumps and the LR-parser repair-sequence list.
    """
    diag = result.primary
    if diag is None:
        return "faust-rs reported no actionable diagnostic."

    lines = [f"The Faust compiler rejected your program. "
             f"[{diag.code}] {_clean_message(diag.message)}"]

    loc = diag.primary_line
    if loc:
        col = diag.primary_col
        lines.append(f"  at line {loc}" + (f", column {col}" if col else ""))
        if source:
            lines.extend(_source_caret(source, loc, col))

    prose_notes = [n for n in diag.notes
                   if not any(m in n for m in _NOISE_NOTE_MARKERS)]
    for note in prose_notes[:max_notes]:
        lines.append(f"  note: {note}")

    for hint in diag.help:
        lines.append(f"  help: {hint}")

    if len(result.diagnostics) > 1:
        others = ", ".join(d.code for d in result.diagnostics[1:])
        lines.append(f"  (also reported: {others})")

    lines.append("Fix this and re-emit the complete program.")
    return "\n".join(lines)


if __name__ == "__main__":
    import sys
    if len(sys.argv) < 2:
        print("usage: python bench/frs_check.py <file.dsp>", file=sys.stderr)
        sys.exit(2)
    src_text = Path(sys.argv[1]).read_text(encoding="utf-8")
    res = check(src_text)
    if res is None:
        print("faust-rs unavailable (set PLUGINFORGE_FAUST_RS_BIN) or emitted no JSON.",
              file=sys.stderr)
        sys.exit(2)
    print(f"# faust-rs {res.version}  status={res.status}  codes={res.codes}\n")
    print(render(res, src_text))
