#!/usr/bin/env python3
"""
Live verification that the model honours either refine preamble (STATUS.md
"Assumed, never checked" #2 / "Next three things" #1, the item this script closes).

The 3-mode Add/Redo refine architecture landed in 3fafea7, but no real model run
has ever confirmed the output respects the mode contract:
  - Add (refine_mode="surgical")  -> _SURGICAL_PREAMBLE: minimal, surgical change;
    existing structure and control names preserved EXACTLY.
  - Redo (refine_mode="context")  -> _CONTEXT_PREAMBLE: prior source is reference
    only; free to rewrite from scratch.

This script runs ONE call per mode through the exact product path — the
llm/generate.py --request-file subprocess the C++ host invokes (PromptPanel.cpp) —
on the SAME starting patch, differing only in refine_mode and prompt. The contrast
is the evidence: same provider, same kind, same prior_source; only the preamble
differs.

THE DISCRIMINATOR (the starting patch):
    import("stdfaust.lib");
    z = hslider("ZzyzxLive",0.5,0,1,0.01);
    core = _ * z;
    process = core, core;
It carries a marker control ("ZzyzxLive") and a named helper ("core") so "preserve
exactly" is mechanically checkable: a surgical (Add) run that honours its preamble
still contains both; a free rewrite (Redo) has no natural home for a marker this
oddly named, so a rewrite that honours its preamble drops it. Short, so it fits
groq's 8k admission window after generate_json's dynamic-stdlib headroom
optimization.

MECHANICS (per llm/CONTRACT.md):
  - Free-only. Provider is pinned to "groq" by default and asserted to be one of
    the free-tier entries in llm/providers.py. The subprocess also runs through
    generate.py's __main__, where assert_free fires again — belt and suspenders.
  - Takes bench/run_benchmark.py's quota lock for the duration, the same lock
    every quota-spending harness shares (PF-025/PF-030).
  - PLUGINFORGE_PROMPT_LOG is pointed at the evidence directory so the subprocess
    exercises log_user_prompt (PF-014) without contaminating logs/prompts.jsonl —
    that log is reserved for real user generations (PF-014 docstring).
  - Writes ONLY one committed artifact: bench/results/refine_preamble_live_*.json,
    containing both request dicts verbatim, both ADR-011 responses, both faust
    outputs, the verdicts, and the explicit unverified remainder.

Usage:
    python bench/check_refine_preamble_live.py               # full run (2 calls)
    python bench/check_refine_preamble_live.py --dry-run     # 1 call (surgical)
    python bench/check_refine_preamble_live.py --provider groq
"""
import argparse
import json
import os
import subprocess
import sys
import tempfile
from datetime import datetime, timezone
from pathlib import Path

BENCH_DIR = Path(__file__).resolve().parent
REPO_ROOT = BENCH_DIR.parent
LLM_DIR = REPO_ROOT / "llm"
RESULTS_DIR = BENCH_DIR / "results"

# Same lock every quota-spending harness in this tree shares (PF-025/PF-030).
sys.path.insert(0, str(BENCH_DIR))
from run_benchmark import (  # noqa: E402
    BenchmarkLockHeld, acquire_lock, release_lock,
)

sys.path.insert(0, str(LLM_DIR))
import providers  # noqa: E402
import router  # noqa: E402

# ── Free-only guard (ADR-012) ────────────────────────────────────────────────
# generate.py's __main__ enforces this for the subprocess; this script also
# enforces it before launching, so a misconfigured --provider fails fast without
# spending anything.
FREE_PROVIDERS = {name for name, spec in providers.PROVIDERS.items() if spec.free}
DEFAULT_PROVIDER = "groq"


def assert_free(provider: str) -> None:
    if provider not in FREE_PROVIDERS:
        raise SystemExit(
            f"Refusing provider {provider!r}: not in the free-tier set "
            f"{sorted(FREE_PROVIDERS)} (ADR-012). This check is $0 by design; "
            f"PLUGINFORGE_ALLOW_PAID is never read here."
        )


# ── The discriminator starting patch ─────────────────────────────────────────
PRIOR_SOURCE = (
    'import("stdfaust.lib");\n'
    'z = hslider("ZzyzxLive",0.5,0,1,0.01);\n'
    'core = _ * z;\n'
    'process = core, core;\n'
)

RUNS = [
    {
        "name": "add_surgical",
        "refine_mode": "surgical",
        "prompt": "add a low-pass filter before the output",
        # Surgical preamble says preserve structure/control-names EXACTLY, so the
        # marker control and the named helper must both survive.
        "must_keep": ("ZzyzxLive", "core"),
    },
    {
        "name": "redo_context",
        "refine_mode": "context",
        "prompt": ("rewrite this from scratch as a stereo chorus with rate and "
                   "depth controls"),
        # Context preamble frees the model to rewrite; the marker has no natural
        # home in a from-scratch stereo chorus, so keeping it reads as the model
        # treating the prior as more load-bearing than the preamble claims.
        "must_drop": ("ZzyzxLive",),
    },
]


def verdict_for(run: dict, response: dict) -> str:
    if response.get("prior_source_refused"):
        # Surgical preflight hard-fail (A6): prior too large to fit in one message
        # in Add mode. A refused request short-circuits before any model call, so
        # it is recorded but is NOT evidence about preamble behaviour.
        return "prior_source_refused"
    if response.get("success") is not True:
        return f"no_valid_generation (reason={response.get('reason')})"
    code = response.get("faust_code") or ""
    missing = [t for t in run.get("must_keep", ()) if t not in code]
    if missing:
        return f"not_honoured (missing {missing})"
    kept = [t for t in run.get("must_drop", ()) if t in code]
    if kept:
        return f"inconclusive_non_preservation (kept {kept})"
    return "honoured"


def run_one(run: dict, provider: str, out_dir: Path) -> dict:
    # Faithful to what the C++ host sends (PromptPanel.cpp): prompt, prior_source,
    # kind, refine_mode — no provider/model/max_retries, which resolve from .env
    # and defaults exactly as the host's subprocess does.
    request = {
        "prompt": run["prompt"],
        "prior_source": PRIOR_SOURCE,
        "kind": router.EFFECT,
        "refine_mode": run["refine_mode"],
    }

    with tempfile.NamedTemporaryFile("w", suffix=".json", dir=str(out_dir),
                                     delete=False, encoding="utf-8") as fh:
        fh.write(json.dumps(request))
        req_path = Path(fh.name)

    env = dict(os.environ)
    env["PLUGINFORGE_PROVIDER"] = provider
    # PF-014: logs/prompts.jsonl is reserved for real user generations; bench runs
    # exercise the same log_user_prompt code path but land in the evidence dir.
    env["PLUGINFORGE_PROMPT_LOG"] = str(out_dir / f"prompts_{run['name']}.jsonl")

    proc = subprocess.run(
        [sys.executable, "generate.py", "--request-file", str(req_path)],
        cwd=str(LLM_DIR), env=env, capture_output=True, text=True, timeout=300,
    )
    stderr_tail = proc.stderr[-500:]
    response = None
    for line in proc.stdout.splitlines():
        try:
            response = json.loads(line)
            break
        except json.JSONDecodeError:
            continue
    if response is None:
        return {
            "run": run["name"], "request": request, "response": None,
            "verdict": "no_parseable_response",
            "detail": f"stdout={proc.stdout[:300]!r} stderr={stderr_tail!r}",
        }
    return {
        "run": run["name"], "request": request, "response": response,
        "verdict": verdict_for(run, response),
        "detail": (f"attempts={response.get('attempts')} "
                   f"reason={response.get('reason')} "
                   f"prior_source_dropped={response.get('prior_source_dropped')} "
                   f"prior_source_refused={response.get('prior_source_refused')}"),
    }


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--provider", default=DEFAULT_PROVIDER,
                    help=f"free-tier provider name (default: {DEFAULT_PROVIDER})")
    ap.add_argument("--dry-run", action="store_true",
                    help="run only the first (surgical) call")
    args = ap.parse_args()

    assert_free(args.provider)
    # For the record only — the request itself omits model, resolving from .env
    # and provider defaults exactly as the host's subprocess does.
    model = providers.resolve_model(args.provider, None)

    err = providers.check_credentials(args.provider)
    if err:
        raise SystemExit(f"Preflight failed: {err}")

    runs = RUNS[:1] if args.dry_run else RUNS

    try:
        acquire_lock()
    except BenchmarkLockHeld as exc:
        raise SystemExit(str(exc)) from None

    try:
        stamp = datetime.now(timezone.utc).strftime("%Y%m%d_%H%M%S")
        suffix = "_dryrun" if args.dry_run else ""
        out_path = RESULTS_DIR / f"refine_preamble_live_{stamp}{suffix}.json"
        RESULTS_DIR.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(dir=str(RESULTS_DIR)) as tmp:
            tmpdir = Path(tmp)
            records = [run_one(run, args.provider, tmpdir) for run in runs]
            result = {
                "brief": ("STATUS.md 'Assumed, never checked' #2 / 'Next three "
                          "things' #1: live refine-preamble verification"),
                "claim_under_test": (
                    "the model honours the refine preamble for its mode: "
                    "surgical (Add) keeps existing structure and control names "
                    "exactly; context (Redo) is free to rewrite from scratch"
                ),
                "provider": args.provider,
                "model": model,
                "kind_pinned": router.EFFECT,
                "generated_at": datetime.now(timezone.utc).isoformat(),
                "prior_source": PRIOR_SOURCE,
                "verdicts": {r["run"]: r["verdict"] for r in records},
                "records": records,
                "unverified_remainder": (
                    "One run per mode is a single stochastic draw, not a rate. "
                    "Covered: groq/openai-gpt-oss-120b, the effects prompt, one "
                    "prompt per mode, one specific prior patch. NOT covered: "
                    "instruments (kind=INSTRUMENT), gemini/ollama/anthropic, "
                    "other phrasings, the legacy refine_mode=None path, longer "
                    "prior patches. 'Preserve exactly' is judged by marker/helper "
                    "survival plus a successful compile, not a semantic diff. Redo "
                    "non-preservation is only demonstrated when the model actually "
                    "drops the marker."
                ),
            }
    finally:
        release_lock()

    out_path.write_text(json.dumps(result, indent=2))

    for r in records:
        print(f"[{r['run']}] verdict={r['verdict']}", file=sys.stderr)
    print(f"Written to {out_path}", file=sys.stderr)


if __name__ == "__main__":
    main()
