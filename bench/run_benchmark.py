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
import anthropic

load_dotenv(Path(__file__).parent.parent / ".env")

BENCH_DIR    = Path(__file__).parent
RESULTS_DIR  = BENCH_DIR / "results"
PROMPTS_FILE = BENCH_DIR / "prompts" / "prompts.json"

CLAUDE_MODEL = "claude-opus-4-6"
GEMINI_MODEL = "gemini-2.0-flash-lite"


def _load_prompt(filename: str) -> str:
    return (BENCH_DIR / "prompts" / filename).read_text()


SYSTEM_PROMPTS = {
    "faust": _load_prompt("system_faust.txt"),
}


# ── Preflight ─────────────────────────────────────────────────────────────────

def preflight_check(providers: list[str]):
    errors = []

    if "claude" in providers and not os.environ.get("ANTHROPIC_API_KEY"):
        errors.append("ANTHROPIC_API_KEY not set — add it to PluginForge/.env")

    if "gemini" in providers and not os.environ.get("GOOGLE_API_KEY"):
        errors.append("GOOGLE_API_KEY not set — add it to PluginForge/.env")

    try:
        subprocess.run(["faust", "--version"], capture_output=True, timeout=5)
    except FileNotFoundError:
        errors.append("'faust' not found on PATH.")

    if errors:
        print("Preflight check FAILED:")
        for e in errors:
            print(f"  ✗ {e}")
        sys.exit(1)

    print(f"Preflight passed — providers: {', '.join(providers)} | compiler: faust\n")


# ── Code generation ────────────────────────────────────────────────────────────

def _make_generators(providers: list[str]) -> dict:
    """Returns {provider_name: callable(system_prompt, user_prompt) -> str}."""
    generators = {}

    if "claude" in providers:
        client = anthropic.Anthropic()
        def gen_claude(sys_p, usr_p):
            r = client.messages.create(
                model=CLAUDE_MODEL,
                max_tokens=1024,
                temperature=0,
                system=sys_p,
                messages=[{"role": "user", "content": usr_p}],
            )
            return r.content[0].text.strip()
        generators["claude"] = gen_claude

    if "gemini" in providers:
        from google import genai as gai
        from google.genai import types as gai_types
        gclient = gai.Client(api_key=os.environ["GOOGLE_API_KEY"])
        _cache: dict = {}
        def gen_gemini(sys_p, usr_p, cache=_cache, gc=gclient):
            k = sys_p[:40]
            if k not in cache:
                cache[k] = sys_p
            r = gc.models.generate_content(
                model=GEMINI_MODEL,
                contents=usr_p,
                config=gai_types.GenerateContentConfig(
                    system_instruction=sys_p,
                    temperature=0.0,
                    max_output_tokens=1024,
                ),
            )
            return r.text.strip()
        generators["gemini"] = gen_gemini

    return generators


# ── Validators ────────────────────────────────────────────────────────────────

def validate_faust(code: str) -> tuple[bool, str]:
    with tempfile.NamedTemporaryFile(suffix=".dsp", mode="w", delete=False) as f:
        f.write(code)
        tmp = f.name
    try:
        result = subprocess.run(
            ["faust", "-lang", "cpp", tmp, "-o", "/dev/null"],
            capture_output=True, text=True, timeout=30,
        )
        return result.returncode == 0, result.stderr.strip()[:500]
    finally:
        os.unlink(tmp)


VALIDATORS = {"faust": validate_faust}


# ── Main run ──────────────────────────────────────────────────────────────────

def run(providers: list[str], dry_run: bool = False, prompts_file: Path = PROMPTS_FILE):
    preflight_check(providers)
    generators = _make_generators(providers)
    prompts_data: dict[str, list[str]] = json.loads(prompts_file.read_text())

    tasks = [
        (category, prompt, "faust", prov)
        for category, prompts in prompts_data.items()
        for prompt in prompts
        for prov in providers
    ]

    if dry_run:
        tasks = tasks[:1]
        print("DRY RUN — 1 generation to verify setup.\n")
    else:
        print(f"Full benchmark: {len(tasks)} generations "
              f"(25 prompts × 1 DSL × {len(providers)} provider(s)).\n")

    results = []
    passes  = {p: 0 for p in providers}
    totals  = {p: 0 for p in providers}

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

    out_file = RESULTS_DIR / "results.json"
    out_file.write_text(json.dumps(results, indent=2))

    # Summary table
    print(f"\n{'─'*40}")
    print(f"  {'':12s} {'FAUST':>14s}")
    print(f"{'─'*40}")
    for prov in providers:
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
        "--provider", choices=["claude", "gemini", "both"], default="both",
        help="Which LLM provider to use (default: both)",
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
    providers = ["claude", "gemini"] if args.provider == "both" else [args.provider]
    prompts_file = Path(args.prompts) if args.prompts else PROMPTS_FILE
    run(providers=providers, dry_run=args.dry_run, prompts_file=prompts_file)
