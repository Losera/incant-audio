#!/usr/bin/env python3
"""bench/repair_ab_repro/score_wp3.py — score the WP3 pre-registered factorial.

Runs the four primary endpoints and the one exploratory analysis named in
WP3_PROTOCOL.md's "Pre-registered analysis plan" and applies the "Pre-
registered decision rule" verbatim (stated before this run, not fit to it).

Reuses score_repair_ab.py's own primitives (_aggregate_cell, mcnemar_exact,
score, produced_no_program) rather than reimplementing them — PF-076's own
adversarial-review lesson about second-implementation drift applies here too.

Usage:
    python bench/repair_ab_repro/score_wp3.py [--wp3 PATH] [--original PATH]
"""
import argparse
import json
import sys
from collections import defaultdict
from pathlib import Path

BENCH_DIR = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(BENCH_DIR))
import score_repair_ab as sra  # noqa: E402

DEFAULT_WP3 = BENCH_DIR / "results" / "repair_ab" / "wp3_20261009.json"
DEFAULT_ORIGINAL = BENCH_DIR / "results" / "repair_ab" / "repair_ab_20260830.json"
MODEL = "qwen2.5-coder:3b"


def pair_by_sha(records: list[dict], arm_x: str, arm_y: str,
                 model: str = MODEL) -> list[tuple[dict, dict]]:
    """Generalizes score_repair_ab.load_pairs (hardcoded to arm 'A') to any
    two named arms — needed because WP3's arms are A2/B2/B2n/C2/C2n, not A/B/C."""
    by_cell: dict[tuple[str, str], list[dict]] = defaultdict(list)
    for r in records:
        if r["repair_model"] == model:
            by_cell[(r["code_sha"], r["arm"])].append(r)
    by_sha: dict[str, dict[str, dict]] = defaultdict(dict)
    for (sha, arm), cell in by_cell.items():
        by_sha[sha][arm] = sra._aggregate_cell(cell)
    return [(v[arm_x], v[arm_y]) for v in by_sha.values()
            if arm_x in v and arm_y in v]


def mcnemar_report(label: str, pairs: list[tuple[dict, dict]]) -> dict:
    x_green = sum(1 for x, _ in pairs if x["repaired"])
    y_green = sum(1 for _, y in pairs if y["repaired"])
    x_only = sum(1 for x, y in pairs if x["repaired"] and not y["repaired"])
    y_only = sum(1 for x, y in pairs if y["repaired"] and not x["repaired"])
    p = sra.mcnemar_exact(y_only, x_only)
    out = {"label": label, "n": len(pairs), "x_green": x_green, "y_green": y_green,
           "x_only": x_only, "y_only": y_only, "mcnemar_p": p}
    print(f"{label}: n={out['n']}  x={x_green}  y={y_green}  "
          f"discordant x_only={x_only} y_only={y_only}  McNemar p={p:.3g}")
    return out


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--wp3", type=Path, default=DEFAULT_WP3)
    ap.add_argument("--original", type=Path, default=DEFAULT_ORIGINAL)
    args = ap.parse_args()

    wp3 = json.loads(args.wp3.read_text())
    original = json.loads(args.original.read_text())

    print("=== WP3 primary endpoints (pre-registered, WP3_PROTOCOL.md) ===\n")

    r1 = mcnemar_report("1. B2n vs A2 (fair content test)", pair_by_sha(wp3, "A2", "B2n"))
    r2 = mcnemar_report("2. B2 vs B2n (visibility factor, full)", pair_by_sha(wp3, "B2n", "B2"))
    r3 = mcnemar_report("3. C2 vs C2n (visibility factor, minimal)", pair_by_sha(wp3, "C2n", "C2"))

    # 4. A (original committed arm-A, product wrapper) vs A2 (matched wrapper) —
    # sanity/determinism check. The ORIGINAL file's arm is "A"; pair by sha
    # against WP3's "A2" records.
    orig_by_sha = {r["code_sha"]: r for r in original
                   if r["arm"] == "A" and r["repair_model"] == MODEL}
    a2_by_sha = {}
    for r in wp3:
        if r["arm"] == "A2" and r["repair_model"] == MODEL:
            a2_by_sha.setdefault(r["code_sha"], []).append(r)
    a2_agg = {sha: sra._aggregate_cell(recs) for sha, recs in a2_by_sha.items()}
    pairs4 = [(orig_by_sha[sha], a2_agg[sha]) for sha in orig_by_sha if sha in a2_agg]
    r4 = mcnemar_report("4. A (original) vs A2 (matched wrapper) — determinism sanity",
                         pairs4)

    print("\n=== Decision rule (stated in WP3_PROTOCOL.md before this run) ===\n")
    p1 = r1["mcnemar_p"]
    if p1 < 1e-3:
        print(f"B2n vs A2: p={p1:.3g} < 1e-3 -> CONFIRMED UNDER FAIR CONDITIONS.")
        print("The original finding (faust-rs's diagnostic CONTENT, not its wrapper or")
        print("visible line, makes this weak model's repair loop worse) holds.")
    else:
        p2 = r2["mcnemar_p"]
        if p2 < 1e-3:
            print(f"B2n vs A2: p={p1:.3g} (not significant); B2 vs B2n: p={p2:.3g} "
                  "(significant)")
            print("-> SUBSTANTIALLY A VISIBILITY CONFOUND. The headline does not survive")
            print("a fair comparison as originally stated; visibility, not diagnostic")
            print("richness, is the operative variable.")
        else:
            print(f"Neither B2n vs A2 (p={p1:.3g}) nor B2 vs B2n (p={p2:.3g}) clears "
                  "significance.")
            print("-> UNDERPOWERED NULL at this corpus size. Report as such; claim "
                  "neither direction.")

    print(f"\nSanity check (4): A vs A2 p={r4['mcnemar_p']:.3g} "
          f"({'OK — no wrapper-identity effect' if r4['mcnemar_p'] >= 0.05 else 'FLAG — unexpected difference, check determinism (L5) before trusting 1-3'})")

    # 5. Exploratory: leak_score distribution within A2, by cpp_error_class —
    # descriptive only, not confirmatory (leak_score is ~deterministic given
    # error class, so this cannot separate "visibility helps" from "class is
    # just easier").
    print("\n=== 5. Exploratory: A2 repaired-within-2 by leak_score, stratified "
          "by error class (descriptive only) ===\n")
    a2_records = [r for r in wp3 if r["arm"] == "A2" and r["repair_model"] == MODEL]
    by_class: dict[str, dict[int, list[bool]]] = defaultdict(lambda: defaultdict(list))
    for r in a2_records:
        by_class[r["first_error_class"]][r.get("leak_score", 0)].append(r["repaired"])
    for cls in sorted(by_class):
        print(f"  {cls}:")
        for ls in sorted(by_class[cls]):
            vals = by_class[cls][ls]
            rate = sum(vals) / len(vals) if vals else float("nan")
            print(f"    leak_score={ls}: n={len(vals)}  repaired={sum(vals)}  "
                  f"rate={rate:.0%}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
