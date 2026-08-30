#!/usr/bin/env python3
"""bench/build_repair_corpus.py — assemble a corpus of failing Faust programs
for the issue-#26 repair-loop A/B (bench/run_repair_ab.py).

WHY A CORPUS, NOT A GRID RE-RUN
    The ollama repair step is deterministic at temperature=0 (measured
    2026-08-30: 3/3 byte-identical), so each (failing program, feedback arm)
    yields exactly one repair trajectory — repeats carry no information and all
    statistical power comes from the NUMBER of distinct failing programs. One
    attempt-1 pass over the 125-cell grid only fails ~40 times, which is thin.
    So this script generates first-attempt programs across several
    (model, temperature) configs and keeps every one the C++ compiler rejects.
    The A/B then repairs each program identically under both arms; how the
    program was first generated does not bias that comparison.

WHAT IT WRITES
    bench/corpora/repair_corpus_<date>.json — a flat list of attempt records:

        {source, prompt_id, category, tier, prompt, config, model, temperature,
         compiles: bool, code, code_sha, cpp_stderr, cpp_error_class,
         frs_codes, frs_feedback, timestamp}

    Records with compiles == false are the corpus. `compiles == true` records
    are kept too (they make the per-config first-try rate auditable and let
    --resume skip completed work).

    Seed entries from prior archived runs are ingested with
    source == "archive:<name>" and config == "archive".

Locked confound controls, same as bench/run_efficacy_study.py:
  * ONE system prompt, llm/prompts/system_prompt.txt via run_benchmark.SYSTEM_PROMPTS.
  * A per-generation Budget matching the product envelope (PF-069 env override).
faust-rs classification is advisory metadata only; a missing binary is fine.
"""
import argparse
import json
import sys
import time
from datetime import datetime, timezone
from pathlib import Path

BENCH_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(BENCH_DIR))
sys.path.insert(0, str(BENCH_DIR.parent / "llm"))

import providers  # noqa: E402
from run_benchmark import (  # noqa: E402  (PF-025/PF-030 — one lock, every harness)
    BenchmarkLockHeld, acquire_lock, release_lock,
)
from run_efficacy_study import (  # noqa: E402
    SYSTEM_PROMPT, make_generation_budget, validate_faust,
)
import error_classes  # noqa: E402
import frs_check  # noqa: E402
from frs_check import sha  # noqa: E402

TIERED_PROMPTS = BENCH_DIR / "prompts" / "tiered_prompts.json"
FLAT_PROMPTS = BENCH_DIR / "prompts" / "prompts.json"
EFFICACY_ARCHIVE = BENCH_DIR / "results" / "efficacy" / "efficacy_ollama_20260828.json"
LADDER_CORPUS = BENCH_DIR / "ladder_corpus.json"
DEFAULT_OUT_DIR = BENCH_DIR / "corpora"

# model@temp. 7b at temp0 reproduces the published baseline config; the two
# extra models and the temp-0.8 pass widen the distinct-failure count without
# touching the repair step (which stays temp=0 in the A/B).
DEFAULT_CONFIGS = [
    "qwen2.5-coder:7b@0.0",
    "qwen2.5-coder:7b-16k@0.0",
    "qwen3-coder@0.0",
    "qwen2.5-coder:7b@0.8",
]


# ── prompt sources ───────────────────────────────────────────────────────────

def load_prompts() -> list[dict]:
    """Every (prompt_id, category, tier, text) from both corpora, flattened."""
    out: list[dict] = []
    tiered = json.loads(TIERED_PROMPTS.read_text())
    for effect in tiered["effects"]:
        for tier, text in effect["tiers"].items():
            out.append({
                "source": "tiered",
                "prompt_id": f'{effect["effect_id"]}/{tier}',
                "category": effect["category"],
                "tier": tier,
                "prompt": text,
            })
    flat = json.loads(FLAT_PROMPTS.read_text())
    for category, prompts in flat.items():
        if category.startswith("_"):
            continue
        for idx, text in enumerate(prompts):
            out.append({
                "source": "benchmark25",
                "prompt_id": f"{category}-{idx}",
                "category": category,
                "tier": None,
                "prompt": text,
            })
    return out


def seed_entries() -> list[dict]:
    """Failing programs already on disk in prior archived runs."""
    entries: list[dict] = []
    if EFFICACY_ARCHIVE.exists():
        for r in json.loads(EFFICACY_ARCHIVE.read_text()):
            if r["first_try_compiles"] or r["retry_success"] or not r.get("code"):
                continue
            entries.append(_seed_record(
                "archive:efficacy_ollama_20260828",
                f'{r["effect_id"]}/{r["tier"]}', r["category"], r["tier"],
                r["prompt"], r["code"], r["model"],
                (r["errors"] or [""])[0]))
    if LADDER_CORPUS.exists():
        for r in json.loads(LADDER_CORPUS.read_text()):
            if r.get("first_try_compiles") or not r.get("code") or not r.get("error"):
                continue
            entries.append(_seed_record(
                "archive:ladder_corpus", f'ladder-{sha(r["prompt"])[:8]}',
                r.get("category", "unknown"), None, r["prompt"], r["code"],
                r.get("model", "unknown"), r["error"]))
    return entries


def _seed_record(source, prompt_id, category, tier, prompt, code, model, cpp_stderr):
    rec = {
        "source": source, "prompt_id": prompt_id, "category": category,
        "tier": tier, "prompt": prompt, "config": "archive", "model": model,
        "temperature": None, "compiles": False, "code": code, "code_sha": sha(code),
        "cpp_stderr": cpp_stderr,
        "cpp_error_class": error_classes.classify_error(cpp_stderr),
        "timestamp": datetime.now(timezone.utc).isoformat(),
    }
    _attach_frs(rec)
    return rec


# ── faust-rs metadata ────────────────────────────────────────────────────────

def _attach_frs(rec: dict) -> None:
    """Advisory: FRS codes + the rendered arm-B feedback string, or nulls."""
    res = frs_check.check(rec["code"]) if not rec["compiles"] else None
    if res is None or res.ok:
        rec["frs_codes"] = None
        rec["frs_feedback"] = None
    else:
        rec["frs_codes"] = res.codes
        rec["frs_feedback"] = frs_check.render(res, rec["code"])


# ── generation ───────────────────────────────────────────────────────────────

def parse_config(spec: str) -> tuple[str, float]:
    model, _, temp = spec.partition("@")
    return model, float(temp or 0.0)


def run(configs: list[str], out_file: Path, limit: int | None,
        resume: bool, dry_run: bool) -> None:
    prompts = load_prompts()
    if limit:
        prompts = prompts[:limit]

    records: list[dict] = []
    done: set[tuple[str, str]] = set()
    if resume and out_file.exists():
        records = json.loads(out_file.read_text())
        done = {(r["config"], r["prompt_id"]) for r in records if r["config"] != "archive"}
        print(f"resume: {len(records)} records on disk, {len(done)} (config,prompt) done",
              file=sys.stderr)
    if not any(r["config"] == "archive" for r in records):
        seeds = seed_entries()
        records = seeds + records
        print(f"seeded {len(seeds)} archived failing programs", file=sys.stderr)

    tasks = [(c, p) for c in configs for p in prompts
             if (c, p["prompt_id"]) not in done]
    if dry_run:
        tasks = tasks[:2]
    print(f"{len(tasks)} generations pending "
          f"({len(configs)} configs × {len(prompts)} prompts)", file=sys.stderr)

    gen_cache: dict[str, object] = {}
    for i, (config, p) in enumerate(tasks, 1):
        model, temp = parse_config(config)
        started = time.monotonic()
        try:
            generate = providers.make_generator(
                "ollama", system_prompt=SYSTEM_PROMPT, model=model,
                temperature=temp, max_tokens=providers.MAX_OUTPUT_TOKENS,
                budget=make_generation_budget())
            code = generate(p["prompt"])
        except Exception as exc:  # noqa: BLE001 — a transport failure is not a corpus entry
            print(f"[{i:04d}/{len(tasks)}] {config:24s} {p['prompt_id']:22s} "
                  f"→ SKIP ({type(exc).__name__})", file=sys.stderr)
            continue
        ok, err = validate_faust(code)
        rec = {
            "source": p["source"], "prompt_id": p["prompt_id"],
            "category": p["category"], "tier": p["tier"], "prompt": p["prompt"],
            "config": config, "model": model, "temperature": temp,
            "compiles": ok, "code": code, "code_sha": sha(code),
            "cpp_stderr": "" if ok else err,
            "cpp_error_class": None if ok else error_classes.classify_error(err),
            "timestamp": datetime.now(timezone.utc).isoformat(),
        }
        _attach_frs(rec)
        records.append(rec)
        write(out_file, records)
        tag = "ok " if ok else f"FAIL {rec['cpp_error_class']}"
        print(f"[{i:04d}/{len(tasks)}] {config:24s} {p['prompt_id']:22s} "
              f"→ {tag:22s} {time.monotonic()-started:5.1f}s", file=sys.stderr)

    summarise(records)


def write(out_file: Path, records: list[dict]) -> None:
    out_file.parent.mkdir(parents=True, exist_ok=True)
    out_file.write_text(json.dumps(records, indent=2))


def summarise(records: list[dict]) -> None:
    fails = [r for r in records if not r["compiles"]]
    distinct = {r["code_sha"] for r in fails}
    print("\n── corpus summary ──", file=sys.stderr)
    print(f"records: {len(records)}   failing: {len(fails)}   "
          f"distinct failing programs: {len(distinct)}", file=sys.stderr)
    from collections import Counter
    by_class = Counter(r["cpp_error_class"] for r in fails)
    print(f"by C++ error class: {dict(by_class)}", file=sys.stderr)
    frs_ok = sum(1 for r in fails if r["frs_feedback"])
    print(f"faust-rs feedback attached: {frs_ok}/{len(fails)}", file=sys.stderr)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--configs", nargs="+", default=DEFAULT_CONFIGS,
                    help="model@temp specs (default: %(default)s)")
    ap.add_argument("--out", type=Path,
                    default=DEFAULT_OUT_DIR / f"repair_corpus_{datetime.now():%Y%m%d}.json")
    ap.add_argument("--limit", type=int, help="first N prompts only (smoke test)")
    ap.add_argument("--resume", action="store_true")
    ap.add_argument("--dry-run", action="store_true", help="2 generations only")
    args = ap.parse_args()

    try:
        acquire_lock()
    except BenchmarkLockHeld as exc:
        print(f"[!] {exc}", file=sys.stderr)
        return 2
    try:
        run(args.configs, args.out, args.limit, args.resume, args.dry_run)
    finally:
        release_lock()
    print(f"\nwrote {args.out}", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
