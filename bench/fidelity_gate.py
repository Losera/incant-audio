#!/usr/bin/env python3
"""bench/fidelity_gate.py — did the repair keep the program, or just make it compile?

The issue-#26 A/B (bench/repair_ab_core.py, bench/issue26/) measures
"repaired-within-2-attempts": success == the Faust compiler stopped complaining.
That number is silent about whether the repaired program still does what the
prompt asked. This module adds the cheap, compiler-free half of that question:

  shrink tier      nonblank(post) / nonblank(pre) < 0.60  — the model deleted
                   most of the program to buy the compile.
  primitives tier  an expected_primitives token the *broken* program already
                   had is gone from the repair.  Ground truth:
                   bench/prompts/tiered_prompts.json `expected_primitives`
                   (an any-of substring list), joined on effect id
                   (`prompt_id.split("/")[0]`).  183/202 of the frozen-3b
                   corpus joins; the rest report primitives_expected = null and
                   are excluded from the primitives denominator.

Neither tier needs faust or faust-rs.  The render tier (silence / NaN / DC /
runaway + acoustic compliance) is WP4 of the issue-#26 methodology plan and will
land here too; it needs faust2sndfile and is deliberately kept out of
bench/issue26/verify.py's scipy-only ~1 s closure.

    python bench/fidelity_gate.py bench/results/repair_ab/repair_ab_20260830.json
    python bench/fidelity_gate.py RESULT.json --out RESULT_fidelity.json

Emits `<result-stem>_fidelity.json` next to the result file (override with
--out).  Exit status is always 0 — this reports, it does not gate a build.

Independently-derived oracle (frozen data, see tests/test_fidelity_gate.py):
the shrink tier over repair_ab_20260830.json produces A 35/151, B 35/88,
C 27/86 shrunk wins.
"""
from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

_BENCH_DIR = Path(__file__).resolve().parent
if str(_BENCH_DIR) not in sys.path:
    sys.path.insert(0, str(_BENCH_DIR))

# reuse the exact any-of substring rule the efficacy scorer uses (score_efficacy.py:190)
from score_efficacy import matches_expected_primitives  # noqa: E402

DEFAULT_CORPUS = _BENCH_DIR / "corpora" / "repair_corpus_20260830.json"
DEFAULT_PROMPTS = _BENCH_DIR / "prompts" / "tiered_prompts.json"

# A repair whose non-blank line count fell below this fraction of the original
# "shrank": it bought the compile by deleting the program rather than fixing it.
SHRINK_THRESHOLD = 0.60


def nonblank(src: str | None) -> int:
    """Lines with any non-whitespace character."""
    return sum(1 for ln in (src or "").splitlines() if ln.strip())


def shrink_ratio(pre: str | None, post: str | None) -> float:
    """nonblank(post) / nonblank(pre).  1.0 when the original had no code lines
    (nothing to shrink) — a blank-only diff therefore also lands at 1.0."""
    p = nonblank(pre)
    if p == 0:
        return 1.0
    return nonblank(post) / p


def load_corpus_by_sha(path: Path) -> dict[str, dict]:
    """code_sha -> corpus row.  code_sha maps 1:1 to a source string in this
    corpus (verified: 0 collisions), so first-seen wins is safe."""
    by_sha: dict[str, dict] = {}
    for row in json.loads(Path(path).read_text()):
        by_sha.setdefault(row["code_sha"], row)
    return by_sha


def load_primitives(path: Path) -> dict[str, list[str]]:
    """effect_id -> expected_primitives (any-of substring list)."""
    data = json.loads(Path(path).read_text())
    return {e["effect_id"]: e.get("expected_primitives", []) for e in data["effects"]}


def effect_id_of(corpus_row: dict) -> str:
    return corpus_row["prompt_id"].split("/")[0]


def evaluate_record(rec: dict, by_sha: dict[str, dict],
                    primitives: dict[str, list[str]]) -> dict | None:
    """One A/B result record -> one fidelity cell, or None if it can't be joined
    to the corpus / has no attempts to judge."""
    origin_sha = rec["code_sha"]
    corpus_row = by_sha.get(origin_sha)
    if corpus_row is None:
        return None
    log = rec.get("attempt_log") or []
    if not log:
        return None

    pre = corpus_row["code"]
    last = log[-1]
    post = last.get("code")

    ratio = shrink_ratio(pre, post)
    shrank = ratio < SHRINK_THRESHOLD

    eid = effect_id_of(corpus_row)
    expected = primitives.get(eid)  # None => not joinable
    if expected:
        pre_has = matches_expected_primitives(pre, expected)
        post_has = matches_expected_primitives(post, expected)
        primitive_lost = pre_has and not post_has
    else:
        expected = None
        pre_has = post_has = primitive_lost = None

    return {
        "code_sha": origin_sha,
        "arm": rec["arm"],
        "repaired": bool(rec["repaired"]),
        "repaired_code_sha": last.get("code_sha"),
        "nonblank_pre": nonblank(pre),
        "nonblank_post": nonblank(post),
        "shrink_ratio": round(ratio, 4),
        "shrank": shrank,
        "effect_id": eid,
        "primitives_expected": expected,
        "pre_has_primitive": pre_has,
        "post_has_primitive": post_has,
        "primitive_lost": primitive_lost,
    }


def _blank_arm_summary() -> dict:
    return {
        "wins": 0,
        "shrank": 0,
        "primitives_determinable": 0,
        "primitive_lost": 0,
        "kept": 0,
    }


def summarise(cells: list[dict]) -> dict:
    """Per-arm counts, computed over wins only (repaired == True) — matches the
    published '~X% of arm A's wins shrink' framing."""
    arms: dict[str, dict] = {}
    for c in cells:
        if not c["repaired"]:
            continue
        s = arms.setdefault(c["arm"], _blank_arm_summary())
        s["wins"] += 1
        if c["shrank"]:
            s["shrank"] += 1
        determinable = c["primitives_expected"] is not None
        if determinable:
            s["primitives_determinable"] += 1
            if c["primitive_lost"]:
                s["primitive_lost"] += 1
        # "kept" = survived both cheap tiers; over primitives-determinable wins
        if determinable and not c["shrank"] and not c["primitive_lost"]:
            s["kept"] += 1

    for s in arms.values():
        s["shrink_rate"] = round(s["shrank"] / s["wins"], 4) if s["wins"] else None
        s["primitive_lost_rate"] = (
            round(s["primitive_lost"] / s["primitives_determinable"], 4)
            if s["primitives_determinable"] else None)
        s["kept_rate"] = (
            round(s["kept"] / s["primitives_determinable"], 4)
            if s["primitives_determinable"] else None)
    return {arm: arms[arm] for arm in sorted(arms)}


def build_report(result_path: Path, corpus_path: Path,
                 prompts_path: Path) -> dict:
    records = json.loads(Path(result_path).read_text())
    by_sha = load_corpus_by_sha(corpus_path)
    primitives = load_primitives(prompts_path)

    cells: list[dict] = []
    unjoined = 0
    for rec in records:
        cell = evaluate_record(rec, by_sha, primitives)
        if cell is None:
            unjoined += 1
            continue
        cells.append(cell)

    # key the cell map on (origin code_sha, arm): unique per cell, and — unlike
    # the repaired code_sha WP4's render map keys on — it never collides across
    # two different originals that happened to be repaired to the same text
    # (44 such collisions in the frozen 3b file).
    cell_map = {f"{c['code_sha']}::{c['arm']}": c for c in cells}

    return {
        "meta": {
            "tool": "bench/fidelity_gate.py",
            "schema": 1,
            "tiers": ["shrink", "primitives"],
            "result_file": str(Path(result_path).name),
            "corpus_file": str(Path(corpus_path).name),
            "prompts_file": str(Path(prompts_path).name),
            "shrink_threshold": SHRINK_THRESHOLD,
            "records_total": len(records),
            "cells": len(cells),
            "records_unjoined": unjoined,
        },
        "summary": summarise(cells),
        "cells": cell_map,
    }


def print_report(report: dict) -> None:
    m = report["meta"]
    print(f"fidelity gate — {m['result_file']}")
    print(f"  corpus {m['corpus_file']}  prompts {m['prompts_file']}  "
          f"shrink<{m['shrink_threshold']}")
    print(f"  {m['cells']} cells  ({m['records_unjoined']} records unjoined)")
    print()
    hdr = f"  {'arm':<4} {'wins':>5} {'shrank':>12} {'prim-lost':>16} {'kept':>14}"
    print(hdr)
    print("  " + "-" * (len(hdr) - 2))
    for arm, s in report["summary"].items():
        sr = "" if s["shrink_rate"] is None else f" ({s['shrink_rate']*100:.0f}%)"
        pd = s["primitives_determinable"]
        pl = "" if s["primitive_lost_rate"] is None else f" ({s['primitive_lost_rate']*100:.0f}%)"
        kp = "" if s["kept_rate"] is None else f" ({s['kept_rate']*100:.0f}%)"
        print(f"  {arm:<4} {s['wins']:>5} "
              f"{s['shrank']:>7}/{s['wins']:<3}{sr:>0}"
              f"{s['primitive_lost']:>9}/{pd:<3}{pl:>0}"
              f"{s['kept']:>7}/{pd:<3}{kp:>0}")
    print()
    print("  wins = repaired within 2 corrective attempts (compile only).")
    print("  kept = neither shrank nor lost an expected primitive, over the")
    print("         primitives-determinable wins.")


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("result", type=Path, help="a repair-A/B result JSON")
    ap.add_argument("--corpus", type=Path, default=DEFAULT_CORPUS)
    ap.add_argument("--prompts", type=Path, default=DEFAULT_PROMPTS)
    ap.add_argument("--out", type=Path, default=None,
                    help="sidecar path (default: <result-stem>_fidelity.json)")
    ap.add_argument("--quiet", action="store_true", help="don't print the table")
    args = ap.parse_args(argv)

    report = build_report(args.result, args.corpus, args.prompts)

    out = args.out or args.result.with_name(args.result.stem + "_fidelity.json")
    out.write_text(json.dumps(report, indent=2) + "\n")

    if not args.quiet:
        print_report(report)
        print(f"\n  wrote {out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
