#!/usr/bin/env python3
"""A $0, local-model generation regression lane, over a fixed prompt subset.

WHY THIS EXISTS
---------------
Workflow audit, 2026-10-20. STATUS.md's own "Next three things" #1 ranks a
groq n>=3 efficacy re-run as the project's top evidence priority, but
llm/providers.py's own quota arithmetic (MAX_OUTPUT_TOKENS * GROQ_TPM_LIMIT
math, see _DEFAULT_GENERATION_BUDGET_S's neighbourhood) shows groq cannot
afford a 125-cell grid at n>=3 in a day -- it is correctly described there as
"a multi-day grind". Meanwhile bench/run_efficacy_study.py already supports
`--provider ollama`, has produced a real prior result
(bench/results/efficacy/efficacy_ollama_20260828.json), and this machine has
a GPU (CLAUDE.md) -- a 12-effect, n=3 run measured ~10s/generation here, so
the whole lane finishes in minutes, not days, for zero dollars.

This is deliberately NOT the groq study (PF-031) and does not claim to be --
it measures the LOCAL model this project could run in CI or every session,
not the SHIPPING model. It exists to catch "did a prompt edit break something
badly" quickly and cheaply; PF-031's groq grid is still the one that answers
"does this generalize to what actually ships."

WHAT THIS IS NOT
----------------
Not a pass/fail gate. A single local 7B model at n=3 over 12 prompts is a
noisy instrument -- COMPARISON_TOLERANCE_PP below is wide on purpose, and
every comparison prints a report, never raises on its own account. Wiring a
noisy measurement into check.sh's pass/fail would get it disabled the first
time it cried wolf (the project's own stated reasoning for why
bench/presentation_checker.py --report is report-only, and why
tools/kg.py asserts nothing -- see those files' own docstrings).

THE PROMPT SUBSET
------------------
12 of the 125-cell grid's 25 effects (bench/prompts/tiered_prompts.json), all
at tier L3 (the tier the frozen 0.88 claude baseline and the original 125-cell
groq grid both used, so a future human comparing numbers across providers has
a like-for-like tier). 2-3 per category, chosen to include a mix of easy and
known-hard effects rather than an arbitrary prefix:

  trivial:     trivial-01, trivial-02        (expected easy; a regression here
                                               would be a severe signal)
  filters:     filters-01, filters-02
  time-based:  time-based-01, time-based-02
  dynamics:    dynamics-01, dynamics-02, dynamics-03
               (dynamics-03 specifically: STATUS.md defect #1 names dynamics
               as the category with a routing_arity weakness at every tier)
  generative:  generative-01, generative-02, generative-03
               (STATUS.md defect #1 again: Karplus-Strong recursion_cycle is
               the OTHER named failure mode; also exercises the oracle's
               UnsupportedPatch path below, since 0-input generators cannot be
               render-scored -- this lane reports that as "n/a", not a fail)

USAGE
-----
    python tools/generation_lane.py --run --reps 3          # real generations
    python tools/generation_lane.py --compare FILE1 FILE2 FILE3   # score only
    python tools/generation_lane.py                          # compare against
                                                               # the last saved
                                                               # reps under
                                                               # bench/results/
                                                               # generation_lane/
"""
from __future__ import annotations

import argparse
import json
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BENCH = ROOT / "bench"
sys.path.insert(0, str(BENCH))
import render_oracle  # noqa: E402

RESULTS_DIR = BENCH / "results" / "generation_lane"
BASELINE_FILE = RESULTS_DIR / "baseline.json"

EFFECT_IDS = [
    "trivial-01", "trivial-02",
    "filters-01", "filters-02",
    "time-based-01", "time-based-02",
    "dynamics-01", "dynamics-02", "dynamics-03",
    "generative-01", "generative-02", "generative-03",
]
TIER = "L3"

# How many percentage points a category's compile/oracle-pass rate may move
# from the baseline before this lane calls it out as worth a human look. Wide
# because n=3 over a handful of prompts per category is genuinely noisy for a
# local 7B model -- this is a smoke alarm, not a gate (see module docstring).
COMPARISON_TOLERANCE_PP = 25


def run_reps(n: int, provider: str = "ollama", retries: int = 2,
             out_dir: Path = RESULTS_DIR, timeout: int = 1800) -> list[Path]:
    """Invoke bench/run_efficacy_study.py `n` times, once per rep, writing
    `rep{i}.json` under out_dir. Returns the paths written."""
    out_dir.mkdir(parents=True, exist_ok=True)
    paths = []
    for i in range(1, n + 1):
        out_path = out_dir / f"rep{i}.json"
        cmd = [
            sys.executable, str(BENCH / "run_efficacy_study.py"),
            "--provider", provider,
            "--effects", ",".join(EFFECT_IDS),
            "--tiers", TIER,
            "--retries", str(retries),
            "--out", str(out_path),
        ]
        print(f"[generation_lane] rep {i}/{n}: {' '.join(cmd)}", file=sys.stderr)
        p = subprocess.run(cmd, cwd=ROOT, timeout=timeout)
        if p.returncode != 0:
            print(f"[generation_lane] rep {i} exited {p.returncode} -- "
                  "scoring whatever it wrote before failing", file=sys.stderr)
        if out_path.exists():
            paths.append(out_path)
    return paths


def score_rep(path: Path) -> dict:
    """One rep's records -> per-category {compiled, oracle_pass, oracle_na, n}."""
    records = json.loads(path.read_text())
    by_cat: dict[str, dict] = {}
    for rec in records:
        cat = rec.get("category", "?")
        bucket = by_cat.setdefault(cat, {"n": 0, "compiled": 0, "oracle_pass": 0, "oracle_na": 0})
        bucket["n"] += 1
        compiled = bool(rec.get("first_try_compiles") or rec.get("retry_success"))
        if compiled:
            bucket["compiled"] += 1
            r = render_oracle.analyse_record(rec)
            if r.get("unsupported"):
                bucket["oracle_na"] += 1
            elif r["rendered"] and r["measurement"]["ok"]:
                bucket["oracle_pass"] += 1
    return by_cat


def aggregate(reps: list[dict]) -> dict:
    """Sum per-category counts across reps -> {cat: {n, compiled, oracle_pass,
    oracle_na, compile_rate, oracle_pass_rate_of_scorable}}."""
    totals: dict[str, dict] = {}
    for rep in reps:
        for cat, b in rep.items():
            t = totals.setdefault(cat, {"n": 0, "compiled": 0, "oracle_pass": 0, "oracle_na": 0})
            for k in ("n", "compiled", "oracle_pass", "oracle_na"):
                t[k] += b[k]
    for cat, t in totals.items():
        t["compile_rate"] = t["compiled"] / t["n"] if t["n"] else 0.0
        scorable = t["compiled"] - t["oracle_na"]
        t["oracle_pass_rate_of_scorable"] = (t["oracle_pass"] / scorable) if scorable else None
    return totals


def compare_to_baseline(current: dict, baseline: dict | None) -> list[str]:
    """Report lines. Never raises; this lane only reports (see module docstring)."""
    lines = []
    if baseline is None:
        lines.append("No baseline recorded yet -- this run IS the first baseline "
                      "candidate. Commit it with `--save-as-baseline` once reviewed.")
        return lines
    base_cats = baseline.get("categories", {})
    for cat, cur in sorted(current.items()):
        base = base_cats.get(cat)
        if base is None:
            lines.append(f"{cat}: no baseline entry (new category) -- "
                          f"compile {cur['compile_rate']:.0%}")
            continue
        delta_pp = (cur["compile_rate"] - base["compile_rate"]) * 100
        flag = " <-- outside tolerance" if abs(delta_pp) > COMPARISON_TOLERANCE_PP else ""
        lines.append(
            f"{cat:<12} compile {cur['compile_rate']:>5.0%} "
            f"(baseline {base['compile_rate']:>5.0%}, {delta_pp:+.0f}pp){flag}"
        )
        if cur["oracle_pass_rate_of_scorable"] is not None and base.get("oracle_pass_rate_of_scorable") is not None:
            odelta = (cur["oracle_pass_rate_of_scorable"] - base["oracle_pass_rate_of_scorable"]) * 100
            oflag = " <-- outside tolerance" if abs(odelta) > COMPARISON_TOLERANCE_PP else ""
            lines.append(
                f"{'':<12} oracle  {cur['oracle_pass_rate_of_scorable']:>5.0%} "
                f"(baseline {base['oracle_pass_rate_of_scorable']:>5.0%}, {odelta:+.0f}pp){oflag}"
            )
    return lines


def load_baseline() -> dict | None:
    if not BASELINE_FILE.exists():
        return None
    return json.loads(BASELINE_FILE.read_text())


def save_baseline(agg: dict, provider: str, model: str, n_reps: int) -> None:
    RESULTS_DIR.mkdir(parents=True, exist_ok=True)
    BASELINE_FILE.write_text(json.dumps({
        "schema_version": 1,
        "recorded_at": datetime.now(timezone.utc).isoformat(),
        "provider": provider,
        "model": model,
        "effect_ids": EFFECT_IDS,
        "tier": TIER,
        "n_reps": n_reps,
        "categories": agg,
        "note": ("Local-model smoke baseline, not a shipping-model claim. "
                 "See tools/generation_lane.py module docstring."),
    }, indent=2) + "\n")


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--run", action="store_true", help="run real generations (ollama)")
    ap.add_argument("--reps", type=int, default=3)
    ap.add_argument("--provider", default="ollama")
    ap.add_argument("--retries", type=int, default=2)
    ap.add_argument("--compare", nargs="+", metavar="FILE", default=None,
                     help="score these existing result files instead of running")
    ap.add_argument("--save-as-baseline", action="store_true",
                     help="write this run's aggregate as the new committed baseline")
    args = ap.parse_args(argv)

    if args.run:
        paths = run_reps(args.reps, provider=args.provider, retries=args.retries)
    elif args.compare:
        paths = [Path(p) for p in args.compare]
    else:
        paths = sorted(RESULTS_DIR.glob("rep*.json"))
        if not paths:
            print("No existing reps under bench/results/generation_lane/ and --run "
                  "not given. Nothing to compare. Use --run to generate some.")
            return 0

    if not paths:
        print("No rep files produced -- see stderr above for what failed.")
        return 0

    reps = [score_rep(p) for p in paths]
    agg = aggregate(reps)
    model = "?"
    try:
        model = json.loads(paths[0].read_text())[0].get("model", "?")
    except Exception:
        pass

    print(f"generation lane: {len(paths)} rep(s), {len(EFFECT_IDS)} effects, "
          f"tier {TIER}, model {model}")
    print()
    baseline = load_baseline()
    for line in compare_to_baseline(agg, baseline):
        print(line)

    if args.save_as_baseline:
        save_baseline(agg, provider=args.provider, model=model, n_reps=len(paths))
        print(f"\nSaved baseline -> {BASELINE_FILE.relative_to(ROOT)}")

    return 0  # report-only -- see module docstring


if __name__ == "__main__":
    sys.exit(main())
