#!/usr/bin/env python3
"""
PluginForge Prompt-Efficacy Study Harness

Measures how prompt knowledge-level (tier L4 expert ... L0 artist-reference)
affects Faust generation success, per effect and category, with the SAME
product-style retry loop (compiler stderr fed back, up to --retries extra
attempts).

Confound controls (locked decisions — do not change casually):
  * ONE system prompt for ALL calls, including retry attempts:
    llm/prompts/system_prompt.txt, loaded via run_benchmark.SYSTEM_PROMPTS.
    Until 2026-07-21 this was a separate bench copy, on the theory that calling
    llm/generate.py would confound the tier variable with a prompt variable. The
    copy then drifted, which confounded the study against PRODUCTION instead —
    the worse of the two failures, since it silently invalidated the transfer of
    every measured rate. There is now one file; the study measures what ships.
    We still do not call llm/generate.py, to keep the retry loop under this
    harness's own control (attempt accounting, per-attempt error capture).
  * Provider/model/params match bench/run_benchmark.py exactly:
    claude (claude-opus-4-6), temperature=0, max_tokens=providers.MAX_OUTPUT_TOKENS.

    NOTE ON THE 2026-07-20 PILOT: it ran at max_tokens=1024, which was hardcoded
    here and truncated the longest generations mid-program. Those were scored as
    compile failures, so the pilot's per-tier rates are NOT comparable to any run
    made after 2026-07-25. See docs/research/truncation-confound-HANDOFF-S1.md.

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
import time
from datetime import datetime, timezone
from pathlib import Path

BENCH_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(BENCH_DIR))

# Importing run_benchmark as a module is safe: its only module-level side
# effects are load_dotenv() and reading llm/prompts/system_prompt.txt
# (verified 2026-07-19 by importing it with a bare env). The anthropic client
# is constructed lazily inside _make_generators(), not at import time, so no
# API key is required just to import. check_prompt_regression.py invokes
# run_benchmark.py via subprocess (it imports score_results, not
# run_benchmark), so no import cycle is introduced.
import run_benchmark  # noqa: E402
from run_benchmark import CLAUDE_MODEL, registry_name, validate_faust  # noqa: E402,F401
from run_benchmark import (  # noqa: E402  (PF-025/PF-030 — one lock, every harness)
    BenchmarkLockHeld, acquire_lock, release_lock,
)

sys.path.insert(0, str(BENCH_DIR.parent / "llm"))
import providers  # noqa: E402

# Locked confound control: same system prompt as the benchmark for every call.
SYSTEM_PROMPT = run_benchmark.SYSTEM_PROMPTS["faust"]

DEFAULT_PROMPTS_FILE = BENCH_DIR / "prompts" / "tiered_prompts.json"
DEFAULT_OUT_DIR = BENCH_DIR / "results" / "efficacy"

# Match the product path's per-generation wall-clock envelope.  Benchmark
# requests used to pass budget=None, so a provider's daily-quota Retry-After
# could suspend the whole study for hours and prevent the incremental writer
# from reaching a recoverable checkpoint.
GENERATION_BUDGET_S = 140.0
FAUST_VALIDATE_TIMEOUT_S = 15.0

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


def make_generation_budget() -> providers.Budget:
    per_attempt = max(10.0, (GENERATION_BUDGET_S / 3.0)
                      - FAUST_VALIDATE_TIMEOUT_S / 3.0)
    return providers.Budget(total=GENERATION_BUDGET_S,
                            per_attempt_cap=per_attempt)


def make_generator(provider: str = "claude", model: str | None = None,
                   budget: providers.Budget | None = None):
    """Callable(user_message) -> code string. Matches run_benchmark._make_generators:
    temperature=0, max_tokens=providers.MAX_OUTPUT_TOKENS, pinned model per provider."""
    return providers.make_generator(
        registry_name(provider),
        system_prompt=SYSTEM_PROMPT,
        model=model or run_benchmark.model_for(provider),
        temperature=0.0,
        max_tokens=providers.MAX_OUTPUT_TOKENS,
        budget=budget,
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
        "terminal_reason": "",
        "retry_after_s": None,
        "provider_wall_s": 0.0,
        "validation_wall_s": 0.0,
        "cell_wall_s": 0.0,
        "estimated_prompt_tokens": 0 if provider == "groq" else None,
        "estimated_output_tokens": 0 if provider == "groq" else None,
        "token_estimate_method": "groq_char_calibration_v1" if provider == "groq" else None,
    }

    cell_started = time.monotonic()
    error_ctx = ""
    for attempt in range(1, retries + 2):  # retries=2 → up to 3 total attempts
        record["attempts"] = attempt
        user_message = build_user_message(prompt, error_ctx)
        if provider == "groq":
            record["estimated_prompt_tokens"] += providers.estimate_tokens(
                SYSTEM_PROMPT + user_message)
        provider_started = time.monotonic()
        try:
            code = generator(user_message)
        except Exception as e:  # API failure: record and stop this cell
            record["provider_wall_s"] += time.monotonic() - provider_started
            record["errors"].append(f"{type(e).__name__}: {e}"[:500])
            record["retry_after_s"] = getattr(e, "retry_after", None)
            if isinstance(e, providers.RateLimited):
                record["terminal_reason"] = "rate_limited"
            elif isinstance(e, providers.BudgetExhausted):
                record["terminal_reason"] = "timeout"
            else:
                record["terminal_reason"] = "transport_error"
            break
        record["provider_wall_s"] += time.monotonic() - provider_started
        record["code"] = code
        if provider == "groq":
            record["estimated_output_tokens"] += providers.estimate_tokens(code)
        validation_started = time.monotonic()
        ok, err = validate_faust(code)
        record["validation_wall_s"] += time.monotonic() - validation_started
        if ok:
            if attempt == 1:
                record["first_try_compiles"] = True
            else:
                record["retry_success"] = True
            record["terminal_reason"] = "compiled"
            break
        record["errors"].append(err)
        error_ctx = err

    if not record["terminal_reason"]:
        record["terminal_reason"] = "compile_failed"
    record["provider_wall_s"] = round(record["provider_wall_s"], 6)
    record["validation_wall_s"] = round(record["validation_wall_s"], 6)
    record["cell_wall_s"] = round(time.monotonic() - cell_started, 6)
    return record


# ── Main run ──────────────────────────────────────────────────────────────────

def write_results(out_file: Path, records: list) -> None:
    out_file.parent.mkdir(parents=True, exist_ok=True)
    out_file.write_text(json.dumps(records, indent=2))


def run_study(tasks: list, retries: int, out_file: Path,
              generator=None, dry_run: bool = False,
              provider: str = "claude", model: str | None = None,
              initial_records: list | None = None) -> list:
    if dry_run:
        tasks = tasks[:1]
        print("DRY RUN — 1 generation to verify setup.", file=sys.stderr)
    else:
        print(f"Efficacy study: {len(tasks)} generations "
              f"(up to {retries + 1} attempts each).", file=sys.stderr)

    records = list(initial_records or [])
    for i, (effect, tier, prompt) in enumerate(tasks, 1):
        # One budget per cell, shared by that cell's corrective attempts, just
        # like one product generation.  Reusing one Budget for the whole study
        # would expire every later cell after the first 140 seconds.
        cell_generator = generator or make_generator(
            provider, model, budget=make_generation_budget())
        record = run_effect_tier(cell_generator, effect, tier, prompt, retries,
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

        # Stop at a durable transport checkpoint instead of spending the same
        # exhausted quota on every remaining cell.  --resume will retry it later.
        if record["terminal_reason"] in {"rate_limited", "timeout", "transport_error"}:
            print("Transport checkpoint written; stopping so --resume can retry "
                  "without burning the remaining grid.", file=sys.stderr)
            break

    return records


def prepare_resume(tasks: list, existing: list, provider: str, model: str) -> tuple[list, list]:
    """Return (kept records, pending tasks) for a safely resumable run.

    A record with no generated code is a transport failure, not completed work,
    and is retried. Provenance and prompt text must match so --resume cannot
    silently combine different experiments.
    """
    task_by_key = {(effect["effect_id"], tier): (effect, tier, prompt)
                   for effect, tier, prompt in tasks}
    kept_by_key = {}
    seen_keys = set()
    for record in existing:
        key = (record.get("effect_id"), record.get("tier"))
        if key not in task_by_key:
            raise ValueError(f"existing record {key!r} is outside the selected task grid")
        if key in seen_keys:
            raise ValueError(f"duplicate existing record for {key!r}")
        seen_keys.add(key)
        _, _, prompt = task_by_key[key]
        if (record.get("provider"), record.get("model"), record.get("prompt")) != \
                (provider, model, prompt):
            raise ValueError(f"existing record {key!r} has incompatible provenance or prompt")
        if record.get("code"):
            kept_by_key[key] = record

    kept = [kept_by_key[(effect["effect_id"], tier)]
            for effect, tier, _ in tasks
            if (effect["effect_id"], tier) in kept_by_key]
    pending = [task for task in tasks
               if (task[0]["effect_id"], task[1]) not in kept_by_key]
    return kept, pending


def print_summary(records: list, tiers: list) -> None:
    print("\nPer-tier summary (first-try / retry-corrected / n):", file=sys.stderr)
    for tier in tiers:
        rs = [r for r in records if r["tier"] == tier]
        if not rs:
            continue
        ft = sum(r["first_try_compiles"] for r in rs)
        rc = sum(r["first_try_compiles"] or r["retry_success"] for r in rs)
        print(f"  {tier}: {ft:2d} / {rc:2d} / {len(rs)}", file=sys.stderr)


def print_efficiency_summary(records: list) -> None:
    """Report operational efficiency separately from model efficacy.

    Older records remain valid efficacy evidence but have no timing fields, so
    they are excluded rather than silently treated as zero-duration work.
    """
    measured = [r for r in records if "cell_wall_s" in r]
    if not measured:
        return
    compiled = sum(r.get("terminal_reason") == "compiled" for r in measured)
    provider_s = sum(float(r.get("provider_wall_s", 0.0)) for r in measured)
    validation_s = sum(float(r.get("validation_wall_s", 0.0)) for r in measured)
    wall_s = sum(float(r.get("cell_wall_s", 0.0)) for r in measured)
    estimates = [r for r in measured if r.get("estimated_prompt_tokens") is not None]
    estimated_tokens = sum(
        int(r.get("estimated_prompt_tokens", 0)) + int(r.get("estimated_output_tokens", 0))
        for r in estimates
    )
    per_provider_min = (compiled / (provider_s / 60.0)) if provider_s > 0 else 0.0
    per_1k_tokens = (compiled / (estimated_tokens / 1000.0)) if estimated_tokens else 0.0
    print("\nOperational efficiency (instrumented cells only):", file=sys.stderr)
    print(f"  compiled / measured: {compiled} / {len(measured)}", file=sys.stderr)
    print(f"  wall / provider / validation seconds: "
          f"{wall_s:.1f} / {provider_s:.1f} / {validation_s:.1f}", file=sys.stderr)
    print(f"  compiled cells per provider-minute: {per_provider_min:.2f}", file=sys.stderr)
    if estimates:
        print(f"  estimated visible tokens ({estimates[0]['token_estimate_method']}): "
              f"{estimated_tokens}; compiled cells per 1k estimated tokens: "
              f"{per_1k_tokens:.2f}", file=sys.stderr)


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
    parser.add_argument("--resume", action="store_true",
                        help="Resume --out, preserving generated records and retrying cells "
                             "whose request failed before producing code.")
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

    initial_records = []
    if args.resume:
        if not args.out:
            parser.error("--resume requires --out EXISTING.json")
        if not out_file.exists():
            parser.error(f"--resume file does not exist: {out_file}")
        existing = json.loads(out_file.read_text())
        resolved_model = args.model or run_benchmark.model_for(args.provider)
        try:
            initial_records, tasks = prepare_resume(
                tasks, existing, args.provider, resolved_model)
        except ValueError as exc:
            parser.error(str(exc))
        print(f"Resume: preserving {len(initial_records)} generated records; "
              f"running {len(tasks)} pending cells.", file=sys.stderr)
        if not tasks:
            print("Resume complete — no pending cells.", file=sys.stderr)
            return 0

    # PF-030. This harness spends the same free-tier quota as run_benchmark.py and
    # took no lock at all, so the two could run concurrently and interleave their
    # requests against one rate limit. That is the more dangerous half of PF-025:
    # a measurement corrupted by a collision does not look corrupted, it looks like
    # data. A lock only one participant respects is not a lock.
    #
    # Held for the WHOLE run, and taken on --dry-run too: the hazard is a property
    # of the run, and a guard skipped when it is inconvenient is not a guard
    # (bench/p6_capture.py:202-206 makes the same argument).
    #
    # Exit 2, not 1, so a caller can tell "someone else is running" from "preflight
    # failed" without parsing text — matching run_benchmark.py:372. preflight_check()
    # runs inside the lock, and the finally releases it either way, including on a
    # sys.exit from preflight (SystemExit unwinds through finally).
    try:
        acquire_lock()
    except BenchmarkLockHeld as exc:
        print(f"Refusing to start:\n  ✗ {exc}", file=sys.stderr)
        return 2
    try:
        preflight_check(args.provider)
        records = run_study(tasks, retries=args.retries, out_file=out_file,
                            dry_run=args.dry_run, provider=args.provider, model=args.model,
                            initial_records=initial_records)
    finally:
        release_lock()

    print_summary(records, dataset["tiers"])
    print_efficiency_summary(records)
    print(f"\nResults saved to: {out_file}", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
