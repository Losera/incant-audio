#!/usr/bin/env python3
"""bench/repair_ab_repro/run_wp3.py — run the pre-registered WP3 factorial.

Protocol: bench/repair_ab_repro/WP3_PROTOCOL.md (committed BEFORE this script
existed — read that file first, this is just the runner). Five arms, one
shared matched wrapper, crossing faust-rs content (full/minimal) x source
visibility (shown/withheld), plus an A2 reference arm that also serves as a
determinism check against the original committed arm-A run.

Modeled directly on bench/run_repair_ab.py (same lock, same incremental
write-per-record, same --resume semantics) rather than reimplementing it —
this project's own stated rule (repair_ab_core.py's docstring) is one
implementation of the loop, not a second copy that can drift.

Output: bench/results/repair_ab/wp3_<date>.json
"""
import argparse
import json
import re
import sys
from datetime import datetime
from pathlib import Path

BENCH_DIR = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(BENCH_DIR))
sys.path.insert(0, str(BENCH_DIR.parent / "llm"))

import providers  # noqa: E402
from run_benchmark import (  # noqa: E402
    BenchmarkLockHeld, acquire_lock, release_lock,
)
from run_efficacy_study import (  # noqa: E402
    SYSTEM_PROMPT, make_generation_budget, validate_faust,
)
import frs_check  # noqa: E402
from repair_ab_core import (  # noqa: E402
    WP3_ARMS, load_corpus, repair_loop as _repair_loop, stratified_sample,
)

DEFAULT_OUT_DIR = BENCH_DIR / "results" / "repair_ab"
DEFAULT_REPAIR_MODEL = "qwen2.5-coder:3b"

# The same detection used to quantify L14 (METHODOLOGY.md) — kept here rather
# than imported from a one-off script so this file is the single source for
# both the WP3_PROTOCOL.md numbers and the leak_score covariate computed on
# THIS run's draw.
_WIDGET_RE = re.compile(
    r'\b(?:hslider|vslider|nentry|button|checkbox|vgroup|hgroup|tgroup)\s*\(\s*"')
_BOX_NOTATION_RE = re.compile(r'\\\(x|\)\.\(')
_FULL_LINE_RE = re.compile(r'\b\w+\s*=\s*[^;{}]{5,};')


def leak_score(cpp_stderr: str) -> int:
    """0 = no literal widget text, 1 = fragment inside a box-expression dump,
    2 = a full ';'-terminated definition line reproduced verbatim. See
    METHODOLOGY.md L14 for the methodology this mirrors."""
    if not _WIDGET_RE.search(cpp_stderr):
        return 0
    if _FULL_LINE_RE.search(cpp_stderr):
        return 2
    return 1


def repair_loop(entry: dict, arm: str, generate, repair_model: str) -> dict:
    rec = _repair_loop(entry, arm, generate, repair_model, validate_faust)
    if arm == "A2":
        rec["leak_score"] = leak_score(entry["cpp_stderr"])
    return rec


def run(corpus_path: Path, out_file: Path, limit: int | None, sample: int | None,
        arms: tuple[str, ...], repair_model: str, resume: bool, dry_run: bool) -> None:
    entries = load_corpus(corpus_path)
    if sample:
        entries = stratified_sample(entries, sample)
    elif limit:
        entries = entries[:limit]
    print(f"WP3: corpus {len(entries)} distinct failing programs | arms {arms} | "
          f"repair model {repair_model}", file=sys.stderr)

    records: list[dict] = []
    done: set[tuple[str, str, str]] = set()
    if resume and out_file.exists():
        records = json.loads(out_file.read_text())
        done = {(r["code_sha"], r["arm"], r["repair_model"]) for r in records}
        print(f"resume: {len(records)} records, {len(done)} (program,arm,model) done",
              file=sys.stderr)

    tasks = [(e, arm) for e in entries for arm in arms
             if (e["code_sha"], arm, repair_model) not in done]
    if dry_run:
        tasks = tasks[:len(arms) * 2]
    print(f"{len(tasks)} (program, arm) repairs pending", file=sys.stderr)

    for i, (entry, arm) in enumerate(tasks, 1):
        generate = providers.make_generator(
            "ollama", system_prompt=SYSTEM_PROMPT, model=repair_model,
            temperature=0.0, max_tokens=providers.MAX_OUTPUT_TOKENS,
            budget=make_generation_budget())
        rec = repair_loop(entry, arm, generate, repair_model)
        records.append(rec)
        write(out_file, records)
        g = "GREEN@%d" % rec["attempts_to_green"] if rec["repaired"] else "no-fix"
        print(f"[{i:04d}/{len(tasks)}] {rec['prompt_id']:22s} arm {arm:4s} "
              f"{rec['first_error_class']:18s} → {g}", file=sys.stderr)

    print(f"\nWP3 run complete: {len(records)} total records in {out_file}",
          file=sys.stderr)


def write(out_file: Path, records: list[dict]) -> None:
    out_file.parent.mkdir(parents=True, exist_ok=True)
    out_file.write_text(json.dumps(records, indent=2))


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--corpus", type=Path, required=True)
    ap.add_argument("--out", type=Path,
                    default=DEFAULT_OUT_DIR / f"wp3_{datetime.now():%Y%m%d}.json")
    ap.add_argument("--arms", default=",".join(WP3_ARMS),
                    help=f"comma list from {WP3_ARMS} (default: all five)")
    ap.add_argument("--repair-model", default=DEFAULT_REPAIR_MODEL)
    ap.add_argument("--limit", type=int, help="first N distinct programs only")
    ap.add_argument("--sample", type=int, help="N distinct programs, class-proportional")
    ap.add_argument("--resume", action="store_true")
    ap.add_argument("--dry-run", action="store_true", help="a few repairs only")
    args = ap.parse_args()

    arms = tuple(a.strip() for a in args.arms.split(","))
    if any(a not in WP3_ARMS for a in arms):
        print(f"[!] --arms must be from {WP3_ARMS}", file=sys.stderr)
        return 2
    if frs_check.faust_rs_bin() is None:
        print("[!] faust-rs not found — B2/B2n/C2/C2n would collapse to arm A. "
              "Set PLUGINFORGE_FAUST_RS_BIN or put faust-rs on PATH.", file=sys.stderr)
        return 2
    try:
        acquire_lock()
    except BenchmarkLockHeld as exc:
        print(f"[!] {exc}", file=sys.stderr)
        return 2
    try:
        run(args.corpus, args.out, args.limit, args.sample, arms,
            args.repair_model, args.resume, args.dry_run)
    finally:
        release_lock()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
