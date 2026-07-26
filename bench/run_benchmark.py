#!/usr/bin/env python3
"""
PluginForge DSL Benchmark Harness
Measures first-try Faust compile success rate: Claude vs Gemini.

Usage:
    python run_benchmark.py                        # full run, both providers
    python run_benchmark.py --provider claude      # Claude only  (25 generations)
    python run_benchmark.py --provider gemini      # Gemini only  (25 generations)
    python run_benchmark.py --dry-run              # 1 generation to verify setup
"""
import argparse
import json
import os
import subprocess
import sys
import tempfile
from datetime import datetime, timezone
from pathlib import Path

from dotenv import load_dotenv

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "llm"))
import providers  # noqa: E402

load_dotenv(Path(__file__).parent.parent / ".env")

BENCH_DIR    = Path(__file__).parent
RESULTS_DIR  = BENCH_DIR / "results"
PROMPTS_FILE = BENCH_DIR / "prompts" / "prompts.json"

# "claude" is this harness's historical name for llm/providers.py's "anthropic"
# entry — every results.json and the committed baseline use it, so it stays.
PROVIDER_ALIASES = {"claude": "anthropic"}
ALL_PROVIDERS = ["claude"] + sorted(p for p in providers.PROVIDERS if p != "anthropic")

CLAUDE_MODEL = "claude-opus-4-6"
GEMINI_MODEL = providers.PROVIDERS["gemini"].default_model


def registry_name(provider: str) -> str:
    return PROVIDER_ALIASES.get(provider, provider)


# UNIFIED 2026-07-21: the benchmark now measures the SAME prompt the product ships.
# bench/prompts/system_faust.txt used to hold a divergent copy; it had drifted to carry
# a hand-written stdlib section, extra wiring rules, and different few-shot examples, so
# no benchmark number transferred to production. Worse, three of its stdlib entries
# (ef.chorus, ef.flanger, ef.ping_pong) named functions that do not exist -- see
# tools/gen_stdlib_block.py. One file, one measurement.
SYSTEM_PROMPT_FILE = BENCH_DIR.parent / "llm" / "prompts" / "system_prompt.txt"


def _load_prompt(path: Path) -> str:
    return path.read_text()


SYSTEM_PROMPTS = {
    "faust": _load_prompt(SYSTEM_PROMPT_FILE),
}


# ── Preflight ─────────────────────────────────────────────────────────────────

def preflight_check(providers_list: list[str]):
    errors = []

    for prov in providers_list:
        error = providers.check_credentials(registry_name(prov))
        if error:
            errors.append(error)

    try:
        subprocess.run(["faust", "--version"], capture_output=True, timeout=5)
    except FileNotFoundError:
        errors.append("'faust' not found on PATH.")

    if errors:
        print("Preflight check FAILED:")
        for e in errors:
            print(f"  ✗ {e}")
        sys.exit(1)

    print(f"Preflight passed — providers: {', '.join(providers_list)} | compiler: faust\n")


# ── Code generation ────────────────────────────────────────────────────────────

def model_for(provider: str) -> str:
    """Pinned model per provider — benchmark numbers are only comparable per model."""
    if provider == "claude":
        return CLAUDE_MODEL
    return providers.resolve_model(registry_name(provider))


def _make_generators(providers_list: list[str]) -> dict:
    """Returns {provider_name: callable(system_prompt, user_prompt) -> str}.

    temperature=0 is a locked confound control (docs/prompt_efficacy_study.md §4).
    Every free provider accepts it; claude-opus-4-7+ do not, which is why
    CLAUDE_MODEL stays pinned at opus-4-6 here.
    """
    generators = {}
    for prov in providers_list:
        model = model_for(prov)

        def make(sys_p, prov=prov, model=model):
            return providers.make_generator(
                registry_name(prov), system_prompt=sys_p, model=model,
                temperature=0.0, max_tokens=providers.MAX_OUTPUT_TOKENS,
            )

        # The system prompt is fixed per run but arrives per call, so build the
        # generator on first use and reuse the client for the rest of the run.
        def gen(sys_p, usr_p, _cache={}, make=make):  # noqa: B006 — per-generator cache
            if "g" not in _cache:
                _cache["g"] = make(sys_p)
            return _cache["g"](usr_p)

        generators[prov] = gen

    return generators


# ── Validators ────────────────────────────────────────────────────────────────

def validate_faust(code: str) -> tuple[bool, str]:
    # errors="replace": faust can emit invalid UTF-8 on stderr when the source
    # contains non-ASCII (it truncates the echoed line mid-character). Without this
    # the harness CRASHES on such a generation instead of recording it as a failed
    # attempt — which is exactly what a refusal reply ("I'm sorry, but I can't...")
    # produces, via its curly apostrophes. See llm/generate.py::validate_faust.
    with tempfile.NamedTemporaryFile(suffix=".dsp", mode="w", encoding="utf-8",
                                     delete=False) as f:
        f.write(code)
        tmp = f.name
    try:
        result = subprocess.run(
            ["faust", "-lang", "cpp", tmp, "-o", "/dev/null"],
            capture_output=True, text=True, timeout=30,
            encoding="utf-8", errors="replace",
        )
        return result.returncode == 0, result.stderr.strip()[:500]
    finally:
        os.unlink(tmp)


VALIDATORS = {"faust": validate_faust}


# ── Main run ──────────────────────────────────────────────────────────────────

def run(providers_list: list[str], dry_run: bool = False, prompts_file: Path = PROMPTS_FILE):
    preflight_check(providers_list)
    generators = _make_generators(providers_list)
    prompts_data: dict[str, list[str]] = json.loads(prompts_file.read_text())

    tasks = [
        (category, prompt, "faust", prov)
        # Keys starting with "_" are file-level comments, not categories --
        # JSON has no comment syntax, and a subset file that explains its own
        # purpose is worth more than the convention costs. Without this skip the
        # comment text gets sent to the LLM as a prompt.
        for category, prompts in prompts_data.items() if not category.startswith("_")
        for prompt in prompts
        for prov in providers_list
    ]

    if dry_run:
        tasks = tasks[:1]
        print("DRY RUN — 1 generation to verify setup.\n")
    else:
        print(f"Full benchmark: {len(tasks)} generations "
              f"(25 prompts × 1 DSL × {len(providers_list)} provider(s)).\n")

    results = []
    passes  = {p: 0 for p in providers_list}
    totals  = {p: 0 for p in providers_list}

    for i, (category, prompt, dsl, prov) in enumerate(tasks, 1):
        label = f"[{i:03d}/{len(tasks):03d}] {prov.upper():6s} {dsl.upper():7s} | {category:10s} | {prompt[:48]}"
        print(label, end="", flush=True)

        record = {
            "provider":            prov,
            "category":            category,
            "prompt":              prompt,
            "dsl":                 dsl,
            "code":                "",
            "first_try_compiles":  False,
            "error":               "",
            "timestamp":           datetime.now(timezone.utc).isoformat(),
        }

        try:
            code = generators[prov](SYSTEM_PROMPTS[dsl], prompt)
            record["code"] = code
            ok, err = VALIDATORS[dsl](code)
            record["first_try_compiles"] = ok
            record["error"] = err
            totals[prov] += 1
            if ok:
                passes[prov] += 1
            print(f"  → {'✓ PASS' if ok else '✗ FAIL'}")
        except Exception as e:
            record["error"] = f"{type(e).__name__}: {e}"[:500]
            totals[prov] += 1
            print(f"  → ERROR: {e}")

        results.append(record)

    # SUBTLE: a dry run must NOT land in results.json. It used to, and on 2026-07-21
    # two setup dry-runs silently overwrote the committed 25-record Claude run that
    # the ADR-009 verdict rests on (recovered from git). A 1-record smoke test has no
    # business replacing a full suite's evidence.
    out_file = RESULTS_DIR / ("results_dryrun.json" if dry_run else "results.json")
    out_file.write_text(json.dumps(results, indent=2))

    # Summary table
    print(f"\n{'─'*40}")
    print(f"  {'':12s} {'FAUST':>14s}")
    print(f"{'─'*40}")
    for prov in providers_list:
        n, p = totals[prov], passes[prov]
        pct = p / n * 100 if n else 0
        print(f"  {prov.upper():<12s}  {p}/{n} ({pct:3.0f}%)".rjust(28))
    print(f"{'─'*40}")
    print(f"\nResults saved to: {out_file}")

    if dry_run:
        r = results[0]
        print(f"\n── Generated code ({r['provider']} / {r['dsl']}) ──")
        print(r["code"])
        print(f"\nCompiled: {r['first_try_compiles']}")
        if r["error"]:
            print(f"Error: {r['error']}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="PluginForge DSL benchmark harness.")
    parser.add_argument(
        "--provider", choices=ALL_PROVIDERS + ["both"], default="gemini",
        help=f"Which LLM provider to use: {', '.join(ALL_PROVIDERS)}, or 'both' "
             f"(claude+gemini, the ADR-008 comparison). Default: gemini. "
             f"Anything involving 'claude' is a PAID provider and needs "
             f"PLUGINFORGE_ALLOW_PAID=1.",
    )
    parser.add_argument(
        "--dry-run", action="store_true",
        help="Run a single generation to verify setup, then exit.",
    )
    parser.add_argument(
        "--prompts", metavar="FILE", default=None,
        help="JSON prompts file to use instead of the default bench/prompts/prompts.json. "
             "Useful for running targeted subsets (e.g. recovery_prompts.json).",
    )
    args = parser.parse_args()
    selected = ["claude", "gemini"] if args.provider == "both" else [args.provider]
    try:  # free-only rule; see llm/providers.py
        for _prov in selected:
            providers.assert_free(registry_name(_prov))
    except providers.PaidProviderError as exc:
        print(f"Preflight check FAILED:\n  ✗ {exc}")
        sys.exit(1)
    prompts_file = Path(args.prompts) if args.prompts else PROMPTS_FILE
    run(providers_list=selected, dry_run=args.dry_run, prompts_file=prompts_file)
