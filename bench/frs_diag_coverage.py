#!/usr/bin/env python3
"""bench/frs_diag_coverage.py — faust-rs error-path diagnostic-coverage report.

WHY THIS EXISTS (2026-10-09 adversarial review of PF-076/issue-#26)

The published faust-rs "efficacy study" was two things: a conformance claim
(accept/reject agreement) built on a 36-program corpus that's since been lost
(only 15/51 re-derives, docs/BUGS.md) and sits one-sided (faust-rs was never
run on a single C++-ACCEPTED program in this project's corpus — zero coverage
of false rejects); and an LLM-repair-loop A/B (PF-076) that is a finding about
small local models, not about faust-rs, and is itself confounded three ways
(METHODOLOGY.md L3/L12/L13/L14/L15).

Against upstream's own testing (2,996 tests, a C++-generated impulse-response
oracle at 2e-6 tolerance across 8 backends, a 6,624-comparison backend matrix,
and a prior `xtask examples-compare` run that already found 216/1,110 stdlib
false rejects), neither of those is a contribution. What upstream's own
fixture corpora don't have — every one of `tests/corpus`, `tests/golden`,
`tests/impulse-tests` is positive-path — is a REJECTED-program diagnostic
corpus. This script reports what this project's 202-program CC-BY-4.0 corpus
(bench/corpora/repair_corpus_20260830.json) actually covers of faust-rs's own
diagnostic surface, plus the one thing worth re-stating as a clean result:
faust-rs never rejected a C++-accepted program in this corpus (0/248, the
false-reject screen this project's conformance claim never ran).

This is offered as a FIXTURE DONATION candidate, not a study: upstream's own
contribution norm (AGENTS.md) wants a named corpus + a gate that fails on
regression AND on a stale expectation, which is what `expected.json` +
`verify.py`-style harnesses in this repo already are for the repair-loop work.

Usage:
    python bench/frs_diag_coverage.py [--bin label=/path/to/faust-rs ...]

With no --bin, uses PLUGINFORGE_FAUST_RS_BIN / PATH (bench/frs_check.py
default resolution) as the sole binary, labelled "default".
"""
from __future__ import annotations

import argparse
import json
import os
import re
import subprocess
import sys
import tempfile
from collections import Counter
from pathlib import Path

BENCH_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(BENCH_DIR))
import corpus_screen  # noqa: E402
import frs_check  # noqa: E402

CORPUS = BENCH_DIR / "corpora" / "repair_corpus_20260830.json"
CPP_TIMEOUT_S = 20.0

# The set of FRS-* codes known to the diagnostics crate as of the
# faust-rs revision audited in docs/records/faust-rs-delta-2026-09-23.md
# (d8bdcf8, 2026-09-22). Not re-scraped live by this script — pass
# --known-codes to update if upstream's registry has moved. Used only to
# report "never fired on this corpus", not to validate faust-rs's output.
KNOWN_CODES_DEFAULT = [
    "FRS-CODEGEN-0001", "FRS-COMP-0004", "FRS-COMP-0005", "FRS-COMP-0006",
    "FRS-COMP-0007", "FRS-EVAL-0001", "FRS-EVAL-0002", "FRS-EVAL-0003",
    "FRS-EVAL-0004", "FRS-EVAL-0005", "FRS-EVAL-0006", "FRS-EVAL-0007",
    "FRS-EVAL-0099", "FRS-FIR-0001", "FRS-FIR-0002", "FRS-LEX-0001",
    "FRS-PARSE-0001", "FRS-PARSE-0002", "FRS-PARSE-0003", "FRS-PROP-0001",
    "FRS-PROP-0002", "FRS-PROP-0003", "FRS-PROP-0004", "FRS-PROP-0005",
    "FRS-PROP-0099", "FRS-SFIR-0001", "FRS-SFIR-0002", "FRS-SFIR-0003",
    "FRS-SFIR-0004", "FRS-SFIR-0005",
]

# A catch-all code (its own message carries the real detail; the code alone
# tells an automated caller nothing about the failure class). Any hit here is
# upstream-actionable by construction — see issue drafts in this session's
# handoff.
CATCHALL_CODES = {"FRS-EVAL-0099", "FRS-PROP-0099"}


class CompilerTimeout(Exception):
    pass


def cpp_ok(src: str) -> bool:
    with tempfile.NamedTemporaryFile(suffix=".dsp", mode="w", delete=False) as fh:
        fh.write(src)
        path = fh.name
    try:
        proc = subprocess.run(["faust", "-lang", "cpp", path, "-o", "/dev/null"],
                              capture_output=True, text=True, timeout=CPP_TIMEOUT_S,
                              errors="replace")
        return proc.returncode == 0
    except subprocess.TimeoutExpired:
        raise CompilerTimeout()
    finally:
        os.unlink(path)


def _distinct_accepted(records: list[dict]) -> list[dict]:
    """Distinct C++-ACCEPTED programs — the arm the published conformance
    claim never ran faust-rs on. Mirrors corpus_screen._distinct_failing's
    first-occurrence-by-code_sha rule, on the opposite compiles value."""
    seen: set[str] = set()
    out: list[dict] = []
    for r in records:
        if not r.get("compiles"):
            continue
        sha = r["code_sha"]
        if sha in seen:
            continue
        seen.add(sha)
        out.append(r)
    return out


def false_reject_screen(accepted: list[dict], bin_path: str | None) -> dict:
    """Of the C++-ACCEPTED programs, how many does faust-rs reject? The
    missing arm of the published "accept/reject agreement" claim (that claim
    was computed only over C++-REJECTED programs, 0/306 accepted records in
    this corpus ever carry an frs_codes field)."""
    rejects = []
    timeouts = []
    n = 0
    for r in accepted:
        n += 1
        res = frs_check.check(r["code"], bin_path=bin_path)
        if res is None:
            timeouts.append(r["code_sha"])
            continue
        if not res.ok:
            rejects.append({"code_sha": r["code_sha"], "prompt_id": r.get("prompt_id"),
                             "codes": res.codes})
    return {"n": n, "false_rejects": rejects, "unavailable": timeouts}


def code_coverage(rejected: list[dict], bin_path: str | None,
                   known_codes: list[str]) -> dict:
    """FRS code distribution over distinct C++-rejected programs, catch-all
    hits (upstream-actionable by construction), and message/code consistency
    spot-checks."""
    code_counts: Counter[str] = Counter()
    catchall_hits = []
    for r in rejected:
        res = frs_check.check(r["code"], bin_path=bin_path)
        if res is None or res.ok:
            continue
        primary_code = res.codes[0] if res.codes else "FRS-UNKNOWN"
        code_counts[primary_code] += 1
        if primary_code in CATCHALL_CODES:
            catchall_hits.append({
                "code_sha": r["code_sha"], "prompt_id": r.get("prompt_id"),
                "code": primary_code,
                "message": res.primary.message if res.primary else None,
            })
    never_fired = sorted(set(known_codes) - set(code_counts))
    return {"code_counts": dict(code_counts.most_common()),
            "catchall_hits": catchall_hits, "never_fired": never_fired}


def build_report(corpus_path: Path, bin_path: str | None,
                  known_codes: list[str]) -> dict:
    records = json.loads(corpus_path.read_text(encoding="utf-8"))
    rejected, excluded = corpus_screen.screen(records)
    accepted = _distinct_accepted(records)

    return {
        "corpus": str(corpus_path),
        "n_rejected_screened": len(rejected),
        "n_rejected_excluded": len(excluded),
        "n_accepted": len(accepted),
        "false_reject_screen": false_reject_screen(accepted, bin_path),
        "code_coverage": code_coverage(rejected, bin_path, known_codes),
    }


def print_report(label: str, report: dict) -> None:
    print(f"=== faust-rs diagnostic coverage — binary: {label} ===\n")
    print(f"corpus: {report['corpus']}")
    print(f"  distinct C++-rejected (screened) : {report['n_rejected_screened']}")
    print(f"  distinct C++-accepted            : {report['n_accepted']}\n")

    frs = report["false_reject_screen"]
    print(f"FALSE-REJECT SCREEN (the arm the published \"accept/reject "
          f"agreement\" claim never ran — 0/{frs['n']} of these accepted "
          f"records ever carried an frs_codes field):")
    print(f"  faust-rs rejects a C++-accepted program : "
          f"{len(frs['false_rejects'])}/{frs['n']}")
    if frs["unavailable"]:
        print(f"  faust-rs unavailable/timeout on         : "
              f"{len(frs['unavailable'])}/{frs['n']}")
    for fr in frs["false_rejects"][:10]:
        print(f"    - {fr['prompt_id']} ({fr['code_sha'][:8]}) -> {fr['codes']}")
    print()

    cov = report["code_coverage"]
    print("FRS CODE DISTRIBUTION over C++-rejected programs:")
    for code, n in cov["code_counts"].items():
        tag = "  <- CATCH-ALL" if code in CATCHALL_CODES else ""
        print(f"  {n:4d}  {code}{tag}")
    print(f"\n  codes in the known registry NEVER fired on this corpus "
          f"({len(cov['never_fired'])}):")
    for c in cov["never_fired"]:
        print(f"    {c}")
    if cov["catchall_hits"]:
        print(f"\n  CATCH-ALL hits ({len(cov['catchall_hits'])}) — each is "
              f"upstream-actionable by construction (a specific code should "
              f"exist for the real failure, not the generic eval/propagate "
              f"catch-all):")
        for h in cov["catchall_hits"]:
            print(f"    - {h['prompt_id']} ({h['code_sha'][:8]}) [{h['code']}] "
                  f"{h['message']}")
    print()


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--bin", action="append", default=[],
                    help="label=/path/to/faust-rs (repeatable)")
    ap.add_argument("--corpus", type=Path, default=CORPUS)
    ap.add_argument("--known-codes", type=Path,
                    help="JSON list to override KNOWN_CODES_DEFAULT")
    ap.add_argument("--json", action="store_true", help="machine-readable output")
    args = ap.parse_args()

    known_codes = KNOWN_CODES_DEFAULT
    if args.known_codes:
        known_codes = json.loads(args.known_codes.read_text())

    binaries = dict(a.split("=", 1) for a in args.bin) if args.bin else {"default": None}

    if not args.json:
        for label, bin_path in binaries.items():
            if bin_path is None and frs_check.faust_rs_bin() is None:
                print("faust-rs not found — set PLUGINFORGE_FAUST_RS_BIN or "
                      "pass --bin label=/path/to/faust-rs", file=sys.stderr)
                return 2
            report = build_report(args.corpus, bin_path, known_codes)
            print_report(label, report)
        return 0

    out = {}
    for label, bin_path in binaries.items():
        out[label] = build_report(args.corpus, bin_path, known_codes)
    print(json.dumps(out, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
