#!/usr/bin/env python3
"""
PluginForge Prompt-Efficacy Scorer

Reads an efficacy-study results file (from bench/run_efficacy_study.py) and
the tiered_prompts.json dataset it was run against, and prints:

  1. Tier x category matrices: first-try compile rate, retry-corrected rate,
     mean attempts.
  2. Per-tier aggregate rates with counts (n=...).
  3. Error-class tagging of each failed FIRST attempt, using the taxonomy in
     docs/benchmark_analysis.md (SYNTAX / HALLUCINATION / SEMANTIC /
     INCOMPLETE / UNCLASSIFIED). Prints counts per tier and lists
     UNCLASSIFIED fragments for manual review.
  4. Heuristic semantic pass rate: expected_primitives any-of substring match
     against compiled-successful code, per tier.
  5. compute_efficacy_scores(records, dataset) — programmatic aggregation,
     reusable by other tooling (mirrors score_results.compute_scores style).

Optional --chart FILE.png: grouped bar chart (tiers on x, first-try vs
retry-corrected rates), matplotlib, styled like score_results.py.

Optional --judge: runs a separate judge model over each compiled record, scoring
0-2 fidelity against an independently-authored acceptance spec per effect (PF-041 fix
-- see bench/prompts/acceptance_specs.json; NOT any tier's own generation prompt).
Writes judge_score into a copy of the results file (suffix _judged.json). Default OFF.

Usage:
    python bench/score_efficacy.py --results bench/results/efficacy/efficacy_XXXX.json
    python bench/score_efficacy.py --results ... --prompts bench/prompts/tiered_prompts.json --chart out.png
    python bench/score_efficacy.py --results ... --judge
"""
import argparse
import json
import os
import sys
from collections import Counter, defaultdict
from pathlib import Path

BENCH_DIR = Path(__file__).resolve().parent
DEFAULT_PROMPTS_FILE = BENCH_DIR / "prompts" / "tiered_prompts.json"
# PF-041 fix (ADR-027 §2): the judge's ground truth used to be effect["tiers"]["L4"] --
# byte-identical to the L4 generation prompt for 10/10 effects, so L4 scored 2.00/2.00
# tautologically. This file is independently authored per-effect acceptance criteria,
# never derived from any tier's prompt text (see its own module docstring).
DEFAULT_ACCEPTANCE_SPECS_FILE = BENCH_DIR / "prompts" / "acceptance_specs.json"

# PF-025/PF-030 — one lock, every harness that spends provider quota. Imported
# lazily-but-at-module-scope the same way run_efficacy_study.py:56-60 does it, so
# there is exactly one implementation of the lock in the tree. Only --judge takes
# it; the offline report below must stay runnable during a live benchmark.
sys.path.insert(0, str(BENCH_DIR))
from run_benchmark import (  # noqa: E402
    BenchmarkLockHeld, acquire_lock, release_lock,
)

# ── Error-class taxonomy ───────────────────────────────────────────────────────
# Keyword rules mapped to docs/benchmark_analysis.md's taxonomy. Order matters:
# first matching class wins. Examples the wording is drawn from:
#   HALLUCINATION — "undefined symbol : flanger_mono" (docs/benchmark_analysis.md
#       Faust Failure 2); "Cannot find symbol 'highpassOut'" (Cmajor Failure 1)
#   SEMANTIC — "after 400 evaluation steps ... endless evaluation cycle"
#       (Faust Failure 1, circular with{} deps); "Cannot implicitly convert
#       'float64' to 'float32'" (type mismatch class); "multiple definitions of
#       symbol 'process'" and "number of outputs ... must be equal to the number
#       of inputs" (real Faust compiler wording observed in the 2026-07-20 pilot —
#       ADR-009 duplicate-symbol regressions and signal-graph arity mismatches;
#       both are the same "algebraic vs sequential" misunderstanding as the
#       ping-pong-delay SEMANTIC failure documented above)
#   SYNTAX — "Found "input" when expecting identifier" (reserved keyword);
#       "Unrecognised suffix on literal" (C-style literal syntax)

ERROR_CLASS_RULES = [
    ("SYNTAX", ["syntax error", "parse error", "unexpected token",
                "expecting identifier", "unrecognised suffix", "unrecognized suffix"]),
    ("HALLUCINATION", ["undefined symbol", "unknown", "cannot find symbol",
                        "no such", "not defined"]),
    ("SEMANTIC", ["evaluation cycle", "evaluation step", "type mismatch",
                  "cannot convert", "cannot implicitly convert",
                  "i/o mismatch", "io mismatch", "incompatible types",
                  "wrong number of", "arity", "multiple definitions of symbol",
                  "number of outputs", "number of inputs",
                  "invalid delay parameter range", "invalid delay",
                  "must be between", "must be equal to"]),
]

INCOMPLETE_MIN_LEN = 20  # shorter than this (after stripping) counts as INCOMPLETE

# Failures where NO generation happened: the request was refused or never reached
# the model. These are not compile outcomes and must not sit in a compile-rate
# denominator.
#
# WHY. The 2026-07-20 pilot recorded `generative-05` as a first-try compile failure
# in all five tiers. It was not: the Anthropic account had no credit and the
# request was rejected before a token was generated. Every published tier figure in
# docs/prompt_efficacy_study.md §7.1 is therefore a compile rate containing one
# non-compile event, with a denominator one too large. Discovered 2026-07-25 while
# checking whether the study's headline result survives scrutiny; see
# docs/research/truncation-confound-HANDOFF-S1.md §2.1.
#
# `truncated` belongs here too, for a different reason: a cut-off program never
# reached the compiler either, so scoring it as invalid Faust blames the model for
# an output-budget defect. The pipeline now reports it as its own reason
# (llm/generate.py), and it is excluded rather than counted against generation.
TRANSPORT_ERROR_KEYWORDS = [
    "credit balance",          # anthropic: insufficient funds
    "invalid_request_error",
    "authentication",
    "api key",
    "permission",
    "quota",
    "rate_limit",
    "rate limited",
    "rate limit reached",     # groq HTTP 429 wording
    "connection",
    "timed out",
    "timeout",
    "badrequesterror",
    "was cut off at the output limit",   # OutputTruncated
]


def is_transport_error(record: dict) -> bool:
    """True when the record represents a request that never produced a program.

    Deliberately checks only the FIRST error: a record whose first attempt was
    refused for billing but which later generated real code is still a transport
    casualty for first-try purposes.
    """
    errors = record.get("errors") or []
    if not errors:
        return False
    first = (errors[0] or "").lower()
    # Real compiler output always names the compiler's own failure; if `faust`
    # spoke at all, this was a generation, not a transport failure.
    if "error :" in first or "syntax error" in first:
        return False
    return any(kw in first for kw in TRANSPORT_ERROR_KEYWORDS)


def classify_error(code: str, error: str) -> str:
    """Classify one failed FIRST attempt into the benchmark_analysis.md taxonomy.

    Checked in the order given in the task spec: SYNTAX, HALLUCINATION,
    SEMANTIC keyword rules first (a specific compiler error is diagnostic
    even if the code we happened to log alongside it is short/empty), then
    INCOMPLETE for empty/truncated code, then UNCLASSIFIED.
    """
    err_lower = (error or "").lower()
    for label, keywords in ERROR_CLASS_RULES:
        if any(kw in err_lower for kw in keywords):
            return label

    stripped_code = (code or "").strip()
    if not stripped_code or len(stripped_code) < INCOMPLETE_MIN_LEN:
        return "INCOMPLETE"

    return "UNCLASSIFIED"


# ── Loading ────────────────────────────────────────────────────────────────────

def load_results(path: Path) -> list:
    if not Path(path).exists():
        print(f"ERROR: {path} not found. Run run_efficacy_study.py first.", file=sys.stderr)
        sys.exit(1)
    return json.loads(Path(path).read_text())


def load_dataset(path: Path) -> dict:
    return json.loads(Path(path).read_text())


def load_acceptance_specs(path: Path) -> dict:
    """PF-041: independently-authored per-effect ground truth for --judge.

    Raises KeyError-shaped errors loudly (missing file, missing "specs" key)
    rather than falling back to any tier's prompt text -- a silent fallback
    here is exactly how the tautology could recur.
    """
    return json.loads(Path(path).read_text())["specs"]


def _effects_by_id(dataset: dict) -> dict:
    return {e["effect_id"]: e for e in dataset["effects"]}


# ── Heuristic semantic pass ────────────────────────────────────────────────────

def matches_expected_primitives(code: str, expected_primitives: list) -> bool:
    """Any-of substring match — see tiered_prompts.json schema docstring."""
    if not expected_primitives:
        return False
    return any(p in (code or "") for p in expected_primitives)


# ── compute_efficacy_scores ────────────────────────────────────────────────────

def compute_efficacy_scores(records: list, dataset: dict) -> dict:
    """Nested-dict aggregation, mirroring score_results.compute_scores style.

    Returns:
      {
        "tier_category": {tier: {category: {"first_try": [p, n], "retry": [p, n],
                                              "attempts_sum": int, "attempts_n": int}}},
        "tier_agg": {tier: {"first_try": [p, n], "retry": [p, n], "mean_attempts": float}},
        "error_classes": {tier: Counter({class: count})},
        "unclassified_fragments": {tier: [str, ...]},
        "semantic_pass": {tier: [p, n]},  # n = compiled-successful records for that tier
      }
    """
    effects = _effects_by_id(dataset)
    tiers = dataset["tiers"]
    categories = sorted({e["category"] for e in dataset["effects"]})

    tier_category = {
        t: {c: {"first_try": [0, 0], "retry": [0, 0], "attempts_sum": 0, "attempts_n": 0}
            for c in categories}
        for t in tiers
    }
    tier_agg = {t: {"first_try": [0, 0], "retry": [0, 0], "attempts_sum": 0, "attempts_n": 0}
                for t in tiers}
    error_classes = {t: Counter() for t in tiers}
    unclassified_fragments = {t: [] for t in tiers}
    semantic_pass = {t: [0, 0] for t in tiers}

    excluded = {t: 0 for t in tiers}

    for r in records:
        tier = r["tier"]
        cat = r["category"]
        if tier not in tier_category or cat not in tier_category[tier]:
            continue  # defensive: dataset/results mismatch

        # A request that never produced a program is not a compile outcome. Counting
        # it as one is what put five billing errors into the pilot's compile rate.
        if not r["first_try_compiles"] and is_transport_error(r):
            excluded[tier] += 1
            continue

        compiled_ok = r["first_try_compiles"] or r["retry_success"]

        cell = tier_category[tier][cat]
        cell["first_try"][1] += 1
        cell["retry"][1] += 1
        cell["attempts_sum"] += r["attempts"]
        cell["attempts_n"] += 1
        if r["first_try_compiles"]:
            cell["first_try"][0] += 1
        if compiled_ok:
            cell["retry"][0] += 1

        agg = tier_agg[tier]
        agg["first_try"][1] += 1
        agg["retry"][1] += 1
        agg["attempts_sum"] += r["attempts"]
        agg["attempts_n"] += 1
        if r["first_try_compiles"]:
            agg["first_try"][0] += 1
        if compiled_ok:
            agg["retry"][0] += 1

        # Error-class tagging of the FIRST attempt only.
        if not r["first_try_compiles"] and r.get("errors"):
            first_error = r["errors"][0]
            label = classify_error(r.get("code", ""), first_error)
            error_classes[tier][label] += 1
            if label == "UNCLASSIFIED":
                unclassified_fragments[tier].append(
                    f"{r['effect_id']}: {first_error[:200]}"
                )

        # Heuristic semantic pass rate over compiled-successful code.
        if compiled_ok:
            semantic_pass[tier][1] += 1
            effect = effects.get(r["effect_id"])
            if effect and matches_expected_primitives(r.get("code", ""),
                                                       effect.get("expected_primitives", [])):
                semantic_pass[tier][0] += 1

    return {
        "tier_category": tier_category,
        "tier_agg": tier_agg,
        "error_classes": error_classes,
        "unclassified_fragments": unclassified_fragments,
        "semantic_pass": semantic_pass,
        "excluded_transport": excluded,
        "tiers": tiers,
        "categories": categories,
    }


def print_excluded(scores: dict) -> None:
    """Report what was dropped from the denominators, and why.

    Printed unconditionally, including when the count is zero: a silent exclusion
    is how you get a second generation of numbers nobody can reproduce.
    """
    excluded = scores.get("excluded_transport", {})
    total = sum(excluded.values())
    print(f"\n{'═' * 60}")
    print("EXCLUDED FROM COMPILE RATES (no program was generated)")
    print(f"{'═' * 60}")
    if not total:
        print("  none — every record represents a real generation attempt")
        return
    print(f"  {total} record(s) dropped: request refused or cut off before a")
    print("  program existed, so they are not compile outcomes.")
    for tier, n in excluded.items():
        if n:
            print(f"    {tier}: {n}")
    print("  Denominators below are correspondingly smaller. See is_transport_error().")


# ── Printing ───────────────────────────────────────────────────────────────────

def _pct(p: int, n: int) -> str:
    # round, not int(): truncation systematically under-reported every inexact
    # rate by up to a point (8/9 printed as 88%, not 89%). Harmless in a console
    # dump, not harmless in a figure someone quotes.
    return f"{p}/{n} ({round(p / n * 100) if n else 0:3d}%)"


def print_tier_category_matrices(scores: dict) -> None:
    tiers, categories = scores["tiers"], scores["categories"]
    col = 16
    print(f"\n{'═' * 60}")
    print("  Tier × Category — first-try compile rate")
    print(f"{'═' * 60}")
    header = f"{'':10s}" + "".join(f"{c:>{col}}" for c in categories)
    print(header)
    for t in tiers:
        row = f"{t:<10s}"
        for c in categories:
            p, n = scores["tier_category"][t][c]["first_try"]
            row += _pct(p, n).rjust(col)
        print(row)

    print(f"\n{'═' * 60}")
    print("  Tier × Category — retry-corrected compile rate")
    print(f"{'═' * 60}")
    print(header)
    for t in tiers:
        row = f"{t:<10s}"
        for c in categories:
            p, n = scores["tier_category"][t][c]["retry"]
            row += _pct(p, n).rjust(col)
        print(row)

    print(f"\n{'═' * 60}")
    print("  Tier × Category — mean attempts")
    print(f"{'═' * 60}")
    print(header)
    for t in tiers:
        row = f"{t:<10s}"
        for c in categories:
            cell = scores["tier_category"][t][c]
            mean = cell["attempts_sum"] / cell["attempts_n"] if cell["attempts_n"] else 0.0
            row += f"{mean:.2f}".rjust(col)
        print(row)


def print_tier_aggregate(scores: dict) -> None:
    print(f"\n{'─' * 60}")
    print("  Per-tier aggregate (n=count visible)")
    print(f"{'─' * 60}")
    for t in scores["tiers"]:
        agg = scores["tier_agg"][t]
        ft_p, ft_n = agg["first_try"]
        rc_p, rc_n = agg["retry"]
        mean = agg["attempts_sum"] / agg["attempts_n"] if agg["attempts_n"] else 0.0
        print(f"  {t:<4s}  first-try {_pct(ft_p, ft_n):<14s}  "
              f"retry-corrected {_pct(rc_p, rc_n):<14s}  mean attempts {mean:.2f}  (n={ft_n})")


def print_error_classes(scores: dict) -> None:
    print(f"\n{'─' * 60}")
    print("  Error-class tagging (of failed FIRST attempts)")
    print(f"{'─' * 60}")
    for t in scores["tiers"]:
        counts = scores["error_classes"][t]
        if not counts:
            print(f"  {t}: (no first-attempt failures)")
            continue
        summary = ", ".join(f"{label}={n}" for label, n in
                             sorted(counts.items(), key=lambda kv: -kv[1]))
        print(f"  {t}: {summary}")

    any_unclassified = any(scores["unclassified_fragments"][t] for t in scores["tiers"])
    if any_unclassified:
        print("\n  UNCLASSIFIED fragments (manual review):")
        for t in scores["tiers"]:
            for frag in scores["unclassified_fragments"][t]:
                print(f"    [{t}] {frag}")


def print_semantic_pass(scores: dict) -> None:
    print(f"\n{'─' * 60}")
    print("  Heuristic semantic pass rate (expected_primitives any-of match,")
    print("  over compiled-successful code)")
    print(f"{'─' * 60}")
    for t in scores["tiers"]:
        p, n = scores["semantic_pass"][t]
        print(f"  {t:<4s}  {_pct(p, n)}")


# ── Chart ──────────────────────────────────────────────────────────────────────

def make_chart(scores: dict, chart_file: Path) -> None:
    try:
        import matplotlib
        matplotlib.use("Agg")
        import matplotlib.pyplot as plt
        import numpy as np
    except ImportError:
        print("matplotlib not available — skipping chart.", file=sys.stderr)
        return

    tiers = scores["tiers"]
    first_try_pct = []
    retry_pct = []
    for t in tiers:
        agg = scores["tier_agg"][t]
        ft_p, ft_n = agg["first_try"]
        rc_p, rc_n = agg["retry"]
        first_try_pct.append(ft_p / ft_n * 100 if ft_n else 0)
        retry_pct.append(rc_p / rc_n * 100 if rc_n else 0)

    x = range(len(tiers))
    width = 0.35
    fig, ax = plt.subplots(figsize=(8, 6))
    bars_f = ax.bar([i - width / 2 for i in x], first_try_pct, width,
                     label="First-try", color="#4e9af1", zorder=3)
    bars_r = ax.bar([i + width / 2 for i in x], retry_pct, width,
                     label="Retry-corrected", color="#f4a261", zorder=3)
    ax.set_xticks(list(x))
    ax.set_xticklabels(tiers)
    ax.set_ylim(0, 115)
    ax.set_ylabel("Compile rate (%)")
    ax.set_title("PluginForge — Prompt-Efficacy Study: Compile Rate by Tier")
    ax.legend()
    ax.grid(axis="y", linestyle="--", alpha=0.5, zorder=0)
    ax.axhline(100, color="gray", linestyle=":", linewidth=0.8)

    for bars, pcts in [(bars_f, first_try_pct), (bars_r, retry_pct)]:
        for bar, h in zip(bars, pcts):
            ax.text(bar.get_x() + bar.get_width() / 2, h + 1, f"{h:.0f}%",
                    ha="center", va="bottom", fontsize=8)

    plt.tight_layout()
    plt.savefig(chart_file, dpi=150, bbox_inches="tight")
    print(f"Chart saved to: {chart_file}", file=sys.stderr)


# ── Judge (optional) ───────────────────────────────────────────────────────────

JUDGE_PROVIDER = os.environ.get("PLUGINFORGE_JUDGE_PROVIDER", "gemini")
JUDGE_MODEL = os.environ.get("PLUGINFORGE_JUDGE_MODEL") or None  # None → provider default

# PF-042 fix (ADR-027 §2): the old rubric asked for a single holistic 0/1/2 judgment
# ("partially implements... missing key behavior or parameters") with no operational
# definition of "partial" — over 44 gradings against the local 7B judge model, score 1
# was returned once (2.3%), so the instrument returned an effective boolean while
# claiming three levels. Investigated rubric-vs-model-bluntness per the ADR: rewritten
# here to turn "partial" into a mechanical rule over the now-numbered acceptance
# criteria (bench/prompts/acceptance_specs.json, PF-041's fix) instead of leaving
# "partial" to the judge's own unguided sense of the word — a checklist-count is a much
# easier task for a small model than an unstructured holistic call. Re-measured against
# the same 44-record set as a regression check (docs/BUGS.md PF-042, session results).
JUDGE_RUBRIC = """You are grading Faust DSP code against a numbered checklist of acceptance
criteria for one audio effect. Evaluate each numbered criterion in the checklist below
against the code, then score using this rule:

  2 = every criterion is clearly met, or all but one is met and the miss is minor
  1 = roughly half the criteria are met, OR the effect's basic structure is right but at
      least one criterion is clearly NOT met (e.g. a missing control, wrong signal shape)
  0 = few or none of the criteria are met, the code does not compile meaningfully, or the
      effect is unrelated to the checklist

Acceptance criteria (independent of any prompt given to the code's author):
{ground_truth}

Generated Faust code to grade:
{code}

Think through each numbered criterion briefly, then end your response with a line
containing ONLY a single digit: 0, 1, or 2."""


def _parse_judge_digit(text: str) -> int | None:
    """Last 0/1/2 character in the response, not the first.

    The old rubric asked for ONLY a digit, so first-found was safe. The new
    rubric (PF-042) asks the judge to reason through numbered criteria first,
    so "(1)", "(2)" etc. inside that reasoning would be misread as the score
    by a first-found scan — this is why the rubric asks for a final digit-only
    LINE, and why parsing reads from the end of the response instead.
    """
    for ch in reversed(text):
        if ch in "012":
            return int(ch)
    return None


def run_judge(records: list, acceptance_specs: dict) -> list:
    """Runs the judge model over each compiled record; adds a judge_score key.
    Returns a NEW list (copy) — does not mutate the caller's records.

    SUBTLE: the judge must not be the same model that generated the code — a
    model grading its own output is not an independent measurement. Keep
    PLUGINFORGE_JUDGE_PROVIDER different from the run's --provider.

    acceptance_specs: effect_id -> ground-truth text (PF-041 fix — see
    load_acceptance_specs and bench/prompts/acceptance_specs.json). Deliberately
    a required parameter, not a default-loaded one: a caller must decide where
    ground truth comes from rather than silently falling back to a tier prompt.
    """
    import sys as _sys
    from pathlib import Path as _Path
    _sys.path.insert(0, str(_Path(__file__).resolve().parent.parent / "llm"))
    import providers

    providers.assert_free(providers.resolve_provider(JUDGE_PROVIDER))
    judge = providers.make_generator(
        JUDGE_PROVIDER, system_prompt="", model=JUDGE_MODEL,
        temperature=0.0, max_tokens=600,
    )

    judged = []
    for r in records:
        rec = dict(r)
        compiled_ok = r["first_try_compiles"] or r["retry_success"]
        if not compiled_ok:
            rec["judge_score"] = None
            judged.append(rec)
            continue

        ground_truth = acceptance_specs.get(r["effect_id"], "")
        prompt = JUDGE_RUBRIC.format(ground_truth=ground_truth, code=r.get("code", ""))
        try:
            text = judge(prompt)
            rec["judge_score"] = _parse_judge_digit(text)
        except Exception as e:
            rec["judge_score"] = None
            rec["judge_error"] = f"{type(e).__name__}: {e}"[:300]

        judged.append(rec)
        print(f"  judged {r['effect_id']}/{r['tier']} -> {rec['judge_score']}", file=sys.stderr)

    return judged


# ── Main ───────────────────────────────────────────────────────────────────────

def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description="PluginForge prompt-efficacy scorer.")
    parser.add_argument("--results", required=True, metavar="FILE",
                        help="Efficacy results JSON (from run_efficacy_study.py)")
    parser.add_argument("--prompts", default=str(DEFAULT_PROMPTS_FILE), metavar="FILE",
                        help="Tiered prompts dataset (default: bench/prompts/tiered_prompts.json)")
    parser.add_argument("--chart", metavar="FILE.png", default=None,
                        help="Optional PNG chart path")
    parser.add_argument("--judge", action="store_true",
                        help="Run the fidelity judge (extra API calls; default off). Judge model "
                             "comes from PLUGINFORGE_JUDGE_PROVIDER/_MODEL.")
    parser.add_argument("--acceptance-specs", default=str(DEFAULT_ACCEPTANCE_SPECS_FILE),
                        metavar="FILE",
                        help="PF-041 ground truth for --judge (default: "
                             "bench/prompts/acceptance_specs.json), independently authored "
                             "per-effect -- never a tier prompt.")
    args = parser.parse_args(argv)

    results_path = Path(args.results)
    records = load_results(results_path)
    dataset = load_dataset(Path(args.prompts))

    scores = compute_efficacy_scores(records, dataset)

    print(f"\n{'═' * 60}")
    print("  PluginForge Prompt-Efficacy Study — Report")
    print(f"{'═' * 60}")

    print_excluded(scores)
    print_tier_category_matrices(scores)
    print_tier_aggregate(scores)
    print_error_classes(scores)
    print_semantic_pass(scores)

    if args.chart:
        make_chart(scores, Path(args.chart))

    if args.judge:
        # ── PF-025 lock, added 2026-07-30 ────────────────────────────────────
        # Everything above this line is offline arithmetic over a JSON file.
        # --judge is not: it makes one provider call per compiled record, up to
        # 125 on a full grid, and until now it took no lock at all. It could
        # therefore interleave with a live run_benchmark or run_efficacy_study
        # into the SAME per-minute token bucket -- which is PF-030's hazard
        # exactly, reappearing in the scorer six days after PF-030 closed it in
        # the study. The failure mode is the worst kind: the collided run's 429s
        # are filed by classify_failures as `transport`, i.e. a corrupted
        # measurement that looks like data.
        #
        # Held only around the judge, not the whole program, because the offline
        # report must stay runnable while a benchmark is in flight -- that is the
        # normal way anyone reads yesterday's numbers.
        #
        # Exit 2 on conflict matches run_benchmark.py:372 and
        # run_efficacy_study.py:313 so a caller can distinguish "someone else is
        # running" from "it failed" without parsing text.
        try:
            acquire_lock()
        except BenchmarkLockHeld as exc:
            print(f"Refusing to judge:\n  ✗ {exc}", file=sys.stderr)
            return 2
        try:
            acceptance_specs = load_acceptance_specs(Path(args.acceptance_specs))
            print(f"\nRunning fidelity judge ({JUDGE_PROVIDER})...", file=sys.stderr)
            judged_records = run_judge(records, acceptance_specs)
            judged_path = results_path.with_name(results_path.stem + "_judged.json")
            judged_path.write_text(json.dumps(judged_records, indent=2))
            print(f"Judged results written to: {judged_path}", file=sys.stderr)
        finally:
            release_lock()

    return 0


if __name__ == "__main__":
    sys.exit(main())
