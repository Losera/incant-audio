#!/usr/bin/env python3
"""
PluginForge Benchmark Scorer
Reads bench/results/results.json and prints a comparison table + error analysis.
Saves a PNG chart to bench/results/benchmark_chart.png.

Usage:
    python score_results.py
    python score_results.py --no-chart
"""
import argparse
import json
import sys
from collections import Counter
from pathlib import Path

RESULTS_FILE = Path(__file__).parent / "results" / "results.json"
CHART_FILE   = Path(__file__).parent / "results" / "benchmark_chart.png"

CATEGORIES = ["trivial", "filters", "time-based", "dynamics", "generative"]
DSLS       = ["faust", "cmajor"]


def load_results() -> list[dict]:
    if not RESULTS_FILE.exists():
        print(f"ERROR: {RESULTS_FILE} not found. Run run_benchmark.py first.")
        sys.exit(1)
    records = json.loads(RESULTS_FILE.read_text())
    # Back-compat: records without a provider field are assumed to be claude
    for r in records:
        r.setdefault("provider", "claude")
    return records


def get_providers(records: list[dict]) -> list[str]:
    seen = []
    for r in records:
        if r["provider"] not in seen:
            seen.append(r["provider"])
    return seen


def compute_scores(records: list[dict], provider: str) -> dict:
    """Returns scores[dsl][category] = [passes, total]."""
    scores = {dsl: {cat: [0, 0] for cat in CATEGORIES} for dsl in DSLS}
    for r in records:
        if r["provider"] != provider:
            continue
        dsl, cat = r["dsl"], r["category"]
        if dsl in scores and cat in scores[dsl]:
            scores[dsl][cat][1] += 1
            if r["first_try_compiles"]:
                scores[dsl][cat][0] += 1
    return scores


def print_table(scores: dict, provider: str):
    col = 14
    print(f"  Provider: {provider.upper()}")
    header = f"{'':18s}" + "".join(f"{'Faust':>{col}}{'Cmajor':>{col}}")
    print(header)
    print("─" * (18 + col * 2))

    total_passes = {dsl: 0 for dsl in DSLS}
    total_count  = {dsl: 0 for dsl in DSLS}

    for cat in CATEGORIES:
        row = f"{cat:<18}"
        for dsl in DSLS:
            p, n = scores[dsl][cat]
            pct = int(p / n * 100) if n else 0
            row += f"{p}/{n} ({pct:3d}%)".rjust(col)
            total_passes[dsl] += p
            total_count[dsl]  += n
        print(row)

    print("─" * (18 + col * 2))
    row = f"{'TOTAL':<18}"
    for dsl in DSLS:
        p, n = total_passes[dsl], total_count[dsl]
        pct = int(p / n * 100) if n else 0
        row += f"{p}/{n} ({pct:3d}%)".rjust(col)
    print(row)
    print()


def print_errors(records: list[dict], provider: str, top_n: int = 5):
    for dsl in DSLS:
        failures = [r for r in records if r["provider"] == provider
                    and r["dsl"] == dsl and not r["first_try_compiles"]]
        print(f"  {dsl.upper()} — {len(failures)} failures, top error snippets:")
        if not failures:
            print("    (none)")
        else:
            fragments: Counter = Counter()
            for r in failures:
                for line in r.get("error", "").splitlines():
                    line = line.strip()
                    if "error" in line.lower() and len(line) > 10:
                        fragments[line[:80]] += 1
                        break
                else:
                    fragments["(no error text captured)"] += 1
            for msg, count in fragments.most_common(top_n):
                print(f"    {count:>2}×  {msg}")
        print()


def make_chart(all_scores: dict[str, dict], providers: list[str]):
    try:
        import matplotlib
        matplotlib.use("Agg")
        import matplotlib.pyplot as plt
        import numpy as np
    except ImportError:
        print("matplotlib not available — skipping chart.")
        return

    n_providers = len(providers)
    fig, axes = plt.subplots(1, n_providers + 1, figsize=(7 * (n_providers + 1), 6))
    if n_providers == 1:
        axes = [axes, plt.subplot()]   # pad for consistent indexing
        fig.set_size_inches(14, 6)

    fig.suptitle("PluginForge — Faust vs Cmajor First-Try Compile Rate", fontsize=14, fontweight="bold")

    colors = {"faust": "#4e9af1", "cmajor": "#f4a261"}
    x      = np.arange(len(CATEGORIES))
    width  = 0.35

    provider_totals: dict[str, dict[str, tuple[int, int]]] = {}

    for idx, prov in enumerate(providers):
        ax = axes[idx]
        scores = all_scores[prov]
        faust_pct  = [scores["faust"][c][0]  / scores["faust"][c][1]  * 100
                      if scores["faust"][c][1]  else 0 for c in CATEGORIES]
        cmajor_pct = [scores["cmajor"][c][0] / scores["cmajor"][c][1] * 100
                      if scores["cmajor"][c][1] else 0 for c in CATEGORIES]

        bars_f = ax.bar(x - width / 2, faust_pct,  width, label="Faust",  color=colors["faust"],  zorder=3)
        bars_c = ax.bar(x + width / 2, cmajor_pct, width, label="Cmajor", color=colors["cmajor"], zorder=3)
        ax.set_xticks(x)
        ax.set_xticklabels([c.replace("-", "-\n") for c in CATEGORIES], fontsize=9)
        ax.set_ylim(0, 115)
        ax.set_ylabel("First-try compile rate (%)")
        ax.set_title(f"By Category — {prov.capitalize()}")
        ax.legend()
        ax.grid(axis="y", linestyle="--", alpha=0.5, zorder=0)
        ax.axhline(100, color="gray", linestyle=":", linewidth=0.8)

        for bars, pcts in [(bars_f, faust_pct), (bars_c, cmajor_pct)]:
            for bar, h in zip(bars, pcts):
                ax.text(bar.get_x() + bar.get_width() / 2, h + 1, f"{h:.0f}%",
                        ha="center", va="bottom", fontsize=8)

        provider_totals[prov] = {
            dsl: (sum(scores[dsl][c][0] for c in CATEGORIES),
                  sum(scores[dsl][c][1] for c in CATEGORIES))
            for dsl in DSLS
        }

    # Rightmost panel: overall comparison across all providers × DSLs
    ax_ov = axes[n_providers]
    labels, heights, bar_colors = [], [], []
    for prov in providers:
        for dsl in DSLS:
            p, n = provider_totals[prov][dsl]
            labels.append(f"{prov.capitalize()}\n{dsl.capitalize()}")
            heights.append(p / n * 100 if n else 0)
            bar_colors.append(colors[dsl])

    bars = ax_ov.bar(range(len(labels)), heights, color=bar_colors, zorder=3)
    for bar, h in zip(bars, heights):
        ax_ov.text(bar.get_x() + bar.get_width() / 2, h + 1, f"{h:.0f}%",
                   ha="center", va="bottom", fontsize=10, fontweight="bold")
    ax_ov.set_xticks(range(len(labels)))
    ax_ov.set_xticklabels(labels, fontsize=10)
    ax_ov.set_ylim(0, 115)
    ax_ov.set_ylabel("Compile rate (%)")
    ax_ov.set_title("Overall Comparison")
    ax_ov.grid(axis="y", linestyle="--", alpha=0.5, zorder=0)

    plt.tight_layout()
    plt.savefig(CHART_FILE, dpi=150, bbox_inches="tight")
    print(f"Chart saved to: {CHART_FILE}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--no-chart", action="store_true", help="Skip PNG chart")
    args = parser.parse_args()

    records   = load_results()
    providers = get_providers(records)

    print(f"\n{'═'*46}")
    print("  PluginForge DSL Benchmark — Compile Rates")
    print(f"{'═'*46}\n")

    all_scores = {}
    for prov in providers:
        scores = compute_scores(records, prov)
        all_scores[prov] = scores
        print_table(scores, prov)
        print_errors(records, prov)

    if not args.no_chart:
        make_chart(all_scores, providers)
