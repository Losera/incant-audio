#!/usr/bin/env python3
"""
PluginForge Prompt-Efficacy Study Harness

Measures how prompt knowledge-level (tier L4 expert ... L0 artist-reference)
affects Faust generation success, per effect and category, with the SAME
product-style retry loop (compiler stderr fed back, up to --retries extra
attempts).

Confound controls (locked decisions — do not change casually):
  * The system prompt is bench/prompts/system_faust.txt for ALL calls,
    including retry attempts. We deliberately do NOT call llm/generate.py,
    because it would swap in llm/prompts/system_prompt.txt and confound the
    tier variable with a system-prompt variable.
  * Provider/model/params match bench/run_benchmark.py exactly:
    claude (claude-opus-4-6), temperature=0, max_tokens=1024.

Usage:
    python bench/run_efficacy_study.py                    # full run, all tiers/effects
    python bench/run_efficacy_study.py --tiers L4,L0      # tier subset
    python bench/run_efficacy_study.py --categories filters,dynamics
    python bench/run_efficacy_study.py --effects filters-01,time-03
    python bench/run_efficacy_study.py --dry-run          # 1 generation only

Output: one JSON list of records (schema below), written incrementally after
each record so a crashed run keeps partial results. Progress goes to stderr;
stdout stays clean.
"""
import argparse
import json
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path

BENCH_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(BENCH_DIR))

# Importing run_benchmark as a module is safe: its only module-level side
# effects are load_dotenv() and reading bench/prompts/system_faust.txt
# (verified 2026-07-19 by importing it with a bare env). The anthropic client
# is constructed lazily inside _make_generators(), not at import time, so no
# API key is required just to import. check_prompt_regression.py invokes
# run_benchmark.py via subprocess (it imports score_results, not
# run_benchmark), so no import cycle is introduced.
import run_benchmark  # noqa: E402
from run_benchmark import CLAUDE_MODEL, registry_name, validate_faust  # noqa: E402,F401

sys.path.insert(0, str(BENCH_DIR.parent / "llm"))
import providers  # noqa: E402

# Locked confound control: same system prompt as the benchmark for every call.
SYSTEM_PROMPT = run_benchmark.SYSTEM_PROMPTS["faust"]

DEFAULT_PROMPTS_FILE = BENCH_DIR / "prompts" / "tiered_prompts.json"
DEFAULT_OUT_DIR = BENCH_DIR / "results" / "efficacy"

# Replicates llm/generate.py's generate_faust() error_context pattern exactly
# (same wording, same em dash) so retry behavior is comparable to the product loop.
ERROR_CONTEXT_TEMPLATE = "\n\nYour previous output had this compiler error — fix it:\n{stderr}"


# ── Preflight ─────────────────────────────────────────────────────────────────

def preflight_check(provider: str = "claude") -> None:
    """Mirrors run_benchmark.preflight_check([provider]), but prints to stderr
    so stdout stays clean for piping."""
    errors = []

    credential_error = providers.check_credentials(registry_name(provider))
    if credential_error:
        errors.append(credential_error)

    try:
        subprocess.run(["faust", "--version"], capture_output=True, timeout=5)
    except FileNotFoundError:
        errors.append("'faust' not found on PATH.")

    if errors:
        print("Preflight check FAILED:", file=sys.stderr)
        for e in errors:
            print(f"  ✗ {e}", file=sys.stderr)
        sys.exit(1)

    print(f"Preflight passed — provider: {provider} | compiler: faust", file=sys.stderr)


# ── Dataset handling ──────────────────────────────────────────────────────────

def load_dataset(path: Path) -> dict:
    dataset = json.loads(Path(path).read_text())
    for key in ("tiers", "effects"):
        if key not in dataset:
            raise ValueError(f"Dataset {path} missing required key {key!r}")
    return dataset


def select_tasks(dataset: dict, tiers=None, categories=None, effects=None) -> list:
    """Returns [(effect_dict, tier, prompt_text), ...].

    Order: dataset effect order (outer), dataset tier order (inner) — the
    caller's filter lists select membership only, they never reorder.
    """
    tier_order = [t for t in dataset["tiers"] if tiers is None or t in tiers]
    tasks = []
    for effect in dataset["effects"]:
        if categories is not None and effect["category"] not in categories:
            continue
        if effects is not None and effect["effect_id"] not in effects:
            continue
        for tier in tier_order:
            tasks.append((effect, tier, effect["tiers"][tier]))
    return tasks


# ── Generation ────────────────────────────────────────────────────────────────

def build_user_message(prompt: str, error_context: str = "") -> str:
    """Same message-construction rule as llm/generate.py generate_faust()."""
    content = prompt
    if error_context:
        content += ERROR_CONTEXT_TEMPLATE.format(stderr=error_context)
    return content


def make_generator(provider: str = "claude", model: str | None = None):
    """Callable(user_message) -> code string. Matches run_benchmark._make_generators:
    temperature=0, max_tokens=1024, pinned model per provider."""
    return providers.make_generator(
        registry_name(provider),
        system_prompt=SYSTEM_PROMPT,
        model=model or run_benchmark.model_for(provider),
        temperature=0.0,
        max_tokens=1024,
    )


def run_effect_tier(generator, effect: dict, tier: str, prompt: str, retries: int,
                    provider: str = "claude", model: str | None = None) -> dict:
    """Run one (effect, tier) cell: initial attempt + up to `retries` corrective
    retries, feeding compiler stderr back exactly like the product loop."""
    record = {
        "provider": provider,
        "model": model or run_benchmark.model_for(provider),
        "effect_id": effect["effect_id"],
        "category": effect["category"],
        "tier": tier,
        "prompt": prompt,
        "dsl": "faust",
        "code": "",
        "first_try_compiles": False,
        "attempts": 0,
        "retry_success": False,
        "errors": [],
        "timestamp": datetime.now(timezone.utc).isoformat(),
    }

    error_ctx = ""
    for attempt in range(1, retries + 2):  # retries=2 → up to 3 total attempts
        record["attempts"] = attempt
        try:
            code = generator(build_user_message(prompt, error_ctx))
        except Exception as e:  # API failure: record and stop this cell
            record["errors"].append(f"{type(e).__name__}: {e}"[:500])
            break
        record["code"] = code
        ok, err = validate_faust(code)
        if ok:
            if attempt == 1:
                record["first_try_compiles"] = True
            else:
                record["retry_success"] = True
            break
        record["errors"].append(err)
        error_ctx = err

    return record


# ── Main run ──────────────────────────────────────────────────────────────────

def write_results(out_file: Path, records: list) -> None:
    out_file.parent.mkdir(parents=True, exist_ok=True)
    out_file.write_text(json.dumps(records, indent=2))


def run_study(tasks: list, retries: int, out_file: Path,
              generator=None, dry_run: bool = False,
              provider: str = "claude", model: str | None = None) -> list:
    if generator is None:
        generator = make_generator(provider, model)

    if dry_run:
        tasks = tasks[:1]
        print("DRY RUN — 1 generation to verify setup.", file=sys.stderr)
    else:
        print(f"Efficacy study: {len(tasks)} generations "
              f"(up to {retries + 1} attempts each).", file=sys.stderr)

    records = []
    for i, (effect, tier, prompt) in enumerate(tasks, 1):
        record = run_effect_tier(generator, effect, tier, prompt, retries,
                                 provider=provider, model=model)
        records.append(record)
        # Incremental write: a crashed run keeps everything up to this record.
        write_results(out_file, records)

        if record["first_try_compiles"]:
            status = "✓ PASS"
        elif record["retry_success"]:
            status = f"✓ PASS (retry, attempt {record['attempts']})"
        else:
            status = "✗ FAIL"
        print(f"[{i:03d}/{len(tasks):03d}] {record['effect_id']:14s} {tier} → {status}",
              file=sys.stderr)

    return records


def print_summary(records: list, tiers: list) -> None:
    print("\nPer-tier summary (first-try / retry-corrected / n):", file=sys.stderr)
    for tier in tiers:
        rs = [r for r in records if r["tier"] == tier]
        if not rs:
            continue
        ft = sum(r["first_try_compiles"] for r in rs)
        rc = sum(r["first_try_compiles"] or r["retry_success"] for r in rs)
        print(f"  {tier}: {ft:2d} / {rc:2d} / {len(rs)}", file=sys.stderr)


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description="PluginForge prompt-efficacy study harness.")
    parser.add_argument("--prompts", metavar="FILE", default=str(DEFAULT_PROMPTS_FILE),
                        help="Tiered prompts dataset (default: bench/prompts/tiered_prompts.json)")
    parser.add_argument("--tiers", metavar="L4,L3,...", default=None,
                        help="Comma-separated tier subset (default: all tiers in dataset order)")
    parser.add_argument("--categories", metavar="a,b", default=None,
                        help="Comma-separated category subset (default: all)")
    parser.add_argument("--effects", metavar="id1,id2", default=None,
                        help="Comma-separated effect_id subset (default: all)")
    parser.add_argument("--retries", type=int, default=2, metavar="N",
                        help="Corrective retries after a failed compile; 2 means up to "
                             "3 total attempts, like the product loop (default: 2)")
    parser.add_argument("--out", metavar="FILE", default=None,
                        help="Output JSON file (default: bench/results/efficacy/"
                             "efficacy_<UTCtimestamp>.json)")
    parser.add_argument("--dry-run", action="store_true",
                        help="Run a single generation to verify setup, then exit.")
    parser.add_argument("--provider", choices=run_benchmark.ALL_PROVIDERS, default="gemini",
                        help="LLM provider (default: gemini). 'claude' is PAID and needs "
                             "PLUGINFORGE_ALLOW_PAID=1. NOTE: a provider change is a "
                             "confound against the §4-locked study design — see "
                             "docs/prompt_efficacy_study.md.")
    parser.add_argument("--model", metavar="ID", default=None,
                        help="Override the provider's pinned model id.")
    args = parser.parse_args(argv)

    try:  # free-only rule; see llm/providers.py
        providers.assert_free(registry_name(args.provider))
    except providers.PaidProviderError as exc:
        print(f"Preflight check FAILED:\n  ✗ {exc}", file=sys.stderr)
        return 1

    dataset = load_dataset(Path(args.prompts))

    tiers = args.tiers.split(",") if args.tiers else None
    categories = args.categories.split(",") if args.categories else None
    effects = args.effects.split(",") if args.effects else None

    if tiers:
        unknown = [t for t in tiers if t not in dataset["tiers"]]
        if unknown:
            parser.error(f"unknown tier(s) {unknown}; dataset has {dataset['tiers']}")

    tasks = select_tasks(dataset, tiers=tiers, categories=categories, effects=effects)
    if not tasks:
        parser.error("filters selected zero (effect, tier) cells — nothing to run")

    if args.out:
        out_file = Path(args.out)
    else:
        stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
        out_file = DEFAULT_OUT_DIR / f"efficacy_{stamp}.json"

    preflight_check(args.provider)
    records = run_study(tasks, retries=args.retries, out_file=out_file,
                        dry_run=args.dry_run, provider=args.provider, model=args.model)
    print_summary(records, dataset["tiers"])
    print(f"\nResults saved to: {out_file}", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
