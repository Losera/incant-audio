#!/usr/bin/env python3
"""bench/score_repair_ab.py — turn a run_repair_ab.py result file into the
issue-#26 answer: does faust-rs feedback shorten the repair loop?

Reads bench/results/repair_ab/repair_ab_<date>.json (one record per
(program, arm)), pairs by code_sha, and reports:

  * repaired-within-2 rate per arm + exact McNemar test on the discordant pairs
  * attempts_to_green per arm (failed = CORRECTIVE_ATTEMPTS + 1) + Wilcoxon
    signed-rank on the paired scores
  * per-first-error-class breakdown (where does the effect, if any, live)
  * second-error identity: when a repair fails, same class again vs a new class

Also writes a grouped-bar chart, styled like bench/score_efficacy.py.

No LLM calls, no faust-rs — pure analysis. Safe to re-run.
"""
import argparse
import json
import sys
from collections import Counter, defaultdict
from pathlib import Path

CORRECTIVE_ATTEMPTS = 2
FAIL_SCORE = CORRECTIVE_ATTEMPTS + 1     # censored value for "never green"


def load_pairs(path: Path) -> list[tuple[dict, dict]]:
    by_sha: dict[str, dict[str, dict]] = defaultdict(dict)
    for r in json.loads(path.read_text()):
        by_sha[r["code_sha"]][r["arm"]] = r
    return [(v["A"], v["B"]) for v in by_sha.values() if "A" in v and "B" in v]


def score(rec: dict) -> int:
    return rec["attempts_to_green"] if rec["repaired"] else FAIL_SCORE


def mcnemar_exact(b_only: int, a_only: int) -> float:
    """Two-sided exact McNemar p-value (binomial on the discordant pairs)."""
    from scipy.stats import binomtest
    n = b_only + a_only
    if n == 0:
        return 1.0
    return binomtest(min(b_only, a_only), n, 0.5, alternative="two-sided").pvalue


def wilcoxon(a_scores: list[int], b_scores: list[int]) -> tuple[float, float]:
    """(statistic, p) for paired A-vs-B attempts; (nan, 1.0) if all-tied."""
    from scipy.stats import wilcoxon as w
    diffs = [a - b for a, b in zip(a_scores, b_scores)]
    if not any(diffs):
        return float("nan"), 1.0
    res = w(a_scores, b_scores, zero_method="wilcox", correction=True)
    return float(res.statistic), float(res.pvalue)


def report(pairs: list[tuple[dict, dict]]) -> dict:
    n = len(pairs)
    a_green = sum(1 for a, _ in pairs if a["repaired"])
    b_green = sum(1 for _, b in pairs if b["repaired"])
    b_only = sum(1 for a, b in pairs if b["repaired"] and not a["repaired"])
    a_only = sum(1 for a, b in pairs if a["repaired"] and not b["repaired"])

    a_scores = [score(a) for a, _ in pairs]
    b_scores = [score(b) for _, b in pairs]
    w_stat, w_p = wilcoxon(a_scores, b_scores)
    mc_p = mcnemar_exact(b_only, a_only)

    both = [(a, b) for a, b in pairs if a["repaired"] and b["repaired"]]
    faster_b = sum(1 for a, b in both if b["attempts_to_green"] < a["attempts_to_green"])
    faster_a = sum(1 for a, b in both if a["attempts_to_green"] < b["attempts_to_green"])

    print(f"paired programs: {n}\n")
    print("REPAIRED WITHIN 2 CORRECTIVE ATTEMPTS")
    print(f"  arm A (C++ stderr) : {a_green}/{n}  ({a_green/n:.0%})")
    print(f"  arm B (faust-rs)   : {b_green}/{n}  ({b_green/n:.0%})")
    print(f"  discordant pairs   : B-only {b_only}, A-only {a_only}")
    print(f"  McNemar exact p    : {mc_p:.4f}")
    print()
    print("ATTEMPTS TO GREEN  (never-green scored as %d)" % FAIL_SCORE)
    print(f"  arm A mean : {sum(a_scores)/n:.2f}")
    print(f"  arm B mean : {sum(b_scores)/n:.2f}")
    print(f"  Wilcoxon signed-rank p : {w_p:.4f}  (stat={w_stat:.1f})")
    print(f"  both green: B fewer attempts {faster_b}, A fewer {faster_a}, "
          f"tied {len(both) - faster_a - faster_b}")
    print()

    print("BY FIRST-ERROR CLASS  (n | A green | B green | B-only | A-only)")
    cls = Counter(a["first_error_class"] for a, _ in pairs)
    per_class = {}
    for c, total in cls.most_common():
        sub = [(a, b) for a, b in pairs if a["first_error_class"] == c]
        ag = sum(1 for a, _ in sub if a["repaired"])
        bg = sum(1 for _, b in sub if b["repaired"])
        bo = sum(1 for a, b in sub if b["repaired"] and not a["repaired"])
        ao = sum(1 for a, b in sub if a["repaired"] and not b["repaired"])
        per_class[c] = {"n": total, "a_green": ag, "b_green": bg,
                        "b_only": bo, "a_only": ao}
        print(f"  {c:20s} {total:3d} | {ag:3d} | {bg:3d} | {bo:2d} | {ao:2d}")
    print()

    print("SECOND-ERROR IDENTITY  (of repairs that FAILED, was attempt 1's error"
          " the same class as the start?)")
    for arm_idx, arm in ((0, "A"), (1, "B")):
        failed = [p[arm_idx] for p in pairs if not p[arm_idx]["repaired"]]
        same = sum(1 for r in failed if r.get("second_error_same_as_first") is True)
        new = sum(1 for r in failed if r.get("second_error_same_as_first") is False)
        print(f"  arm {arm}: {len(failed)} failed — same class {same}, new class {new}")
    print()

    return {
        "n": n, "a_green": a_green, "b_green": b_green,
        "b_only": b_only, "a_only": a_only, "mcnemar_p": mc_p,
        "a_mean_attempts": sum(a_scores) / n, "b_mean_attempts": sum(b_scores) / n,
        "wilcoxon_p": w_p, "per_class": per_class,
    }


def verdict(s: dict) -> str:
    n, mc_p, w_p = s["n"], s["mcnemar_p"], s["wilcoxon_p"]
    delta = s["b_green"] - s["a_green"]
    if mc_p < 0.05 and delta > 0:
        return (f"YES — faust-rs feedback repaired {delta} more of {n} programs "
                f"(McNemar p={mc_p:.3f}); attempts p={w_p:.3f}.")
    if mc_p < 0.05 and delta < 0:
        return (f"NO — C++ stderr repaired {-delta} MORE (McNemar p={mc_p:.3f}). "
                f"Richer feedback hurt on this corpus.")
    return (f"NO MEASURABLE DIFFERENCE at n={n} "
            f"(repaired {s['a_green']} vs {s['b_green']}, McNemar p={mc_p:.3f}, "
            f"attempts Wilcoxon p={w_p:.3f}).")


def make_chart(pairs, s: dict, chart_file: Path) -> None:
    try:
        import matplotlib
        matplotlib.use("Agg")
        import matplotlib.pyplot as plt
    except ImportError:
        print("matplotlib not available — skipping chart.", file=sys.stderr)
        return

    classes = ["ALL"] + list(s["per_class"].keys())
    a_pct, b_pct = [], []
    for c in classes:
        if c == "ALL":
            a_pct.append(s["a_green"] / s["n"] * 100)
            b_pct.append(s["b_green"] / s["n"] * 100)
        else:
            d = s["per_class"][c]
            a_pct.append(d["a_green"] / d["n"] * 100)
            b_pct.append(d["b_green"] / d["n"] * 100)

    x = range(len(classes))
    width = 0.38
    fig, ax = plt.subplots(figsize=(max(8, len(classes) * 1.3), 6))
    ba = ax.bar([i - width / 2 for i in x], a_pct, width, label="arm A — C++ stderr",
                color="#4e9af1", zorder=3)
    bb = ax.bar([i + width / 2 for i in x], b_pct, width, label="arm B — faust-rs",
                color="#f4a261", zorder=3)
    ax.set_xticks(list(x))
    ax.set_xticklabels(classes, rotation=30, ha="right")
    ax.set_ylim(0, 115)
    ax.set_ylabel("Repaired within 2 corrective attempts (%)")
    ax.set_title(f"issue #26 — repair-loop A/B  (n={s['n']} paired failing programs, "
                 f"qwen2.5-coder:7b, temp=0)")
    ax.legend()
    ax.grid(axis="y", linestyle="--", alpha=0.5, zorder=0)
    for bars, pcts in [(ba, a_pct), (bb, b_pct)]:
        for bar, h in zip(bars, pcts):
            ax.text(bar.get_x() + bar.get_width() / 2, h + 1, f"{h:.0f}",
                    ha="center", va="bottom", fontsize=8)
    plt.tight_layout()
    plt.savefig(chart_file, dpi=150, bbox_inches="tight")
    print(f"chart: {chart_file}", file=sys.stderr)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("result", type=Path)
    ap.add_argument("--chart", type=Path)
    ap.add_argument("--json-out", type=Path)
    args = ap.parse_args()

    pairs = load_pairs(args.result)
    if not pairs:
        print("no complete (A,B) pairs in the result file", file=sys.stderr)
        return 1
    s = report(pairs)
    line = verdict(s)
    print("=" * 72)
    print(line)
    print("=" * 72)

    chart = args.chart or args.result.with_name(args.result.stem + "_chart.png")
    make_chart(pairs, s, chart)
    if args.json_out:
        args.json_out.write_text(json.dumps({**s, "verdict": line}, indent=2))
    return 0


if __name__ == "__main__":
    sys.exit(main())
