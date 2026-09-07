#!/usr/bin/env python3
"""bench/score_repair_ab.py — turn a run_repair_ab.py result file into the
answer to: does faust-rs feedback shorten the repair loop?

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
import re
import sys
from collections import Counter, defaultdict
from pathlib import Path

CORRECTIVE_ATTEMPTS = 2
FAIL_SCORE = CORRECTIVE_ATTEMPTS + 1     # censored value for "never green"

# arm A's feedback is `faust -lang cpp` stderr .strip()[:500] — the product's
# real cap (bench/run_benchmark.py:246). A record whose attempt-1 arm-A feedback
# is exactly this long was truncated; see bench/repair_ab_repro/METHODOLOGY.md L6.
STDERR_CAP = 500


ARM_LABEL = {"A": "C++ stderr", "B": "faust-rs full", "C": "faust-rs core"}


def load_records(path: Path) -> list[dict]:
    return json.loads(path.read_text())


def combos(records: list[dict]) -> list[tuple[str, str]]:
    """(repair_model, treatment_arm) pairs present in the file, arm != A."""
    seen = {(r["repair_model"], r["arm"]) for r in records}
    models = sorted({m for m, _ in seen})
    return [(m, a) for m in models for a in ("B", "C") if (m, a) in seen and (m, "A") in seen]


def load_pairs(records: list[dict], model: str, treatment: str,
               include: set[str] | None = None) -> list[tuple[dict, dict]]:
    """Paired (arm-A, treatment) records by code_sha for one repair model.

    `include`, if given, restricts to those code_shas — used to apply the
    corpus program screen (bench/corpus_screen.py) without touching the result
    file. `include=None` is the historical behaviour, byte for byte.
    """
    by_sha: dict[str, dict[str, dict]] = defaultdict(dict)
    for r in records:
        if r["repair_model"] == model and (include is None or r["code_sha"] in include):
            by_sha[r["code_sha"]][r["arm"]] = r
    return [(v["A"], v[treatment])
            for v in by_sha.values() if "A" in v and treatment in v]


# ── per-record helpers (work on both pre- and post-terminal_reason records) ───

def produced_no_program(rec: dict) -> bool:
    """The generator raised before any repaired program was produced — a
    transport / truncation abort, not a repair failure. On the committed local
    runs these are all OutputTruncated; on a hosted run they include rate
    limits. See bench/repair_ab_core.py and METHODOLOGY.md L2."""
    if rec.get("terminal_reason"):
        return True
    log = rec.get("attempt_log") or []
    return not any("code" in a for a in log)


def arm_a_feedback_truncated(a_rec: dict) -> bool:
    """arm A's attempt-1 feedback hit the product's 500-char stderr cap."""
    log = a_rec.get("attempt_log") or []
    return bool(log) and len(log[0].get("feedback_text", "")) >= STDERR_CAP


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


def _cell_counts(sub: list[tuple[dict, dict]]) -> dict:
    """green/discordant counts + exact McNemar for a subset of pairs."""
    ag = sum(1 for a, _ in sub if a["repaired"])
    bg = sum(1 for _, b in sub if b["repaired"])
    bo = sum(1 for a, b in sub if b["repaired"] and not a["repaired"])
    ao = sum(1 for a, b in sub if a["repaired"] and not b["repaired"])
    return {"n": len(sub), "a_green": ag, "b_green": bg,
            "b_only": bo, "a_only": ao, "mcnemar_p": mcnemar_exact(bo, ao)}


def _rescue(recs: list[dict]) -> dict:
    """First-shot vs second-shot outcomes for one arm's records.

      won_at_1     repaired on the first corrective attempt
      still_broken produced a program on attempt 1 that did not compile
                   (so attempt 2 got a shot)   = n - won_at_1 - no_program
      rescued_at_2 repaired on the second attempt
    """
    n = len(recs)
    won_at_1 = sum(1 for r in recs if r["repaired"] and r["attempts_to_green"] == 1)
    no_program = sum(1 for r in recs if produced_no_program(r))
    rescued_at_2 = sum(1 for r in recs if r["repaired"] and r["attempts_to_green"] == 2)
    return {"n": n, "won_at_1": won_at_1,
            "still_broken": n - won_at_1 - no_program,
            "rescued_at_2": rescued_at_2, "no_program": no_program}


# ── caret-line preservation ─────────────────────────────────────────────────
# faust-rs quotes the offending source line under its caret; frs_check._source_caret
# emits it as "  {line:>4} | {text}". The mechanism question issue #26 raised: does
# a precise caret make a small model edit AT that line (and re-break it), or edit
# around it? Measured here, paired, from the committed attempt_log.

_CARET_LINE_RE = re.compile(r"^ {2,}\d+ \| (.*)$", re.M)


def quoted_source_line(feedback_text: str) -> str | None:
    """The source line faust-rs spliced under its caret, stripped. None for arm A
    (raw C++ stderr has no such line) or a render that carried no caret. A
    trivial line (< 3 chars, or no alphanumeric) is treated as no line —
    "preserving" `};` would say nothing about anchoring."""
    m = _CARET_LINE_RE.search(feedback_text or "")
    if not m:
        return None
    line = m.group(1).strip()
    if len(line) < 3 or not any(ch.isalnum() for ch in line):
        return None
    return line


def _attempt1(rec: dict) -> dict | None:
    """attempt_log[0] iff it holds a produced program (not a transport abort)."""
    log = rec.get("attempt_log") or []
    return log[0] if log and "code" in log[0] else None


def _line_survives(code: str, line: str) -> bool:
    """`line` (already stripped) reappears verbatim as a stripped line of `code`."""
    return any(ln.strip() == line for ln in code.splitlines())


def _caret_preservation(pairs: list[tuple[dict, dict]], treatment: str) -> dict:
    """Paired: of programs where the treatment arm's attempt-1 feedback quoted a
    source line under its caret, how often did that exact line survive, verbatim,
    into each arm's attempt-1 rewrite?

    The reference line is the treatment feedback's (arm A never sees it) — for
    arm A the question is whether its broader rewrite happened to keep the
    construct faust-rs would have flagged. Denominator: pairs where the treatment
    feedback has a caret line AND both arms produced an attempt-1 program.
    """
    n = a_keep = b_keep = b_only = a_only = 0
    for a, b in pairs:
        a1, b1 = _attempt1(a), _attempt1(b)
        if a1 is None or b1 is None:
            continue
        line = quoted_source_line(b1.get("feedback_text", ""))
        if line is None:
            continue
        n += 1
        a_has = _line_survives(a1["code"], line)
        b_has = _line_survives(b1["code"], line)
        a_keep += a_has
        b_keep += b_has
        b_only += b_has and not a_has
        a_only += a_has and not b_has
    return {"n": n, "a_preserved": a_keep, "b_preserved": b_keep,
            "b_only": b_only, "a_only": a_only,
            "mcnemar_p": mcnemar_exact(b_only, a_only)}


def report(pairs: list[tuple[dict, dict]], model: str, treatment: str) -> dict:
    tl = f"arm {treatment} ({ARM_LABEL[treatment]})"
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

    print(f"\n{'='*72}\n{model}   arm A vs {tl}   —   {n} paired programs\n{'='*72}")
    print("REPAIRED WITHIN 2 CORRECTIVE ATTEMPTS")
    print(f"  arm A (C++ stderr) : {a_green}/{n}  ({a_green/n:.0%})")
    print(f"  {tl:20s}: {b_green}/{n}  ({b_green/n:.0%})")
    print(f"  discordant pairs   : {treatment}-only {b_only}, A-only {a_only}")
    print(f"  McNemar exact p    : {mc_p:.3e}")
    print()
    print("ATTEMPTS TO GREEN  (never-green scored as %d)" % FAIL_SCORE)
    print(f"  arm A mean : {sum(a_scores)/n:.2f}")
    print(f"  arm {treatment} mean : {sum(b_scores)/n:.2f}")
    print(f"  Wilcoxon signed-rank p : {w_p:.3e}  (stat={w_stat:.1f}) — not independent"
          f" of McNemar (same discordant pairs, 3-valued censored score); for reference only")
    print(f"  both green: {treatment} fewer attempts {faster_b}, A fewer {faster_a}, "
          f"tied {len(both) - faster_a - faster_b}")
    print()

    # ── first vs second corrective attempt ──────────────────────────────────
    rescue = {"A": _rescue([a for a, _ in pairs]),
              treatment: _rescue([b for _, b in pairs])}
    print("FIRST vs SECOND CORRECTIVE ATTEMPT")
    for arm in ("A", treatment):
        r = rescue[arm]
        rate = f"{r['rescued_at_2']}/{r['still_broken']}" if r["still_broken"] else "0/0"
        pct = f" ({r['rescued_at_2']/r['still_broken']:.0%})" if r["still_broken"] else ""
        print(f"  arm {arm}: won on attempt 1 {r['won_at_1']}; "
              f"still broken after 1 {r['still_broken']}; rescued on attempt 2 {rate}{pct}"
              + (f"; no program produced {r['no_program']}" if r["no_program"] else ""))
    print()

    print(f"BY FIRST-ERROR CLASS  (n | A green | {treatment} green | {treatment}-only | A-only | McNemar p)")
    cls = Counter(a["first_error_class"] for a, _ in pairs)
    per_class = {}
    for c, total in cls.most_common():
        sub = [(a, b) for a, b in pairs if a["first_error_class"] == c]
        cc = _cell_counts(sub)
        per_class[c] = cc
        print(f"  {c:20s} {cc['n']:3d} | {cc['a_green']:3d} | {cc['b_green']:3d} | "
              f"{cc['b_only']:2d} | {cc['a_only']:2d} | {cc['mcnemar_p']:.2e}")
    print()

    # ── arm-A 500-char stderr cap: stratified robustness check ──────────────
    capped = [(a, b) for a, b in pairs if arm_a_feedback_truncated(a)]
    uncapped = [(a, b) for a, b in pairs if not arm_a_feedback_truncated(a)]
    by_trunc = {"capped": _cell_counts(capped), "uncapped": _cell_counts(uncapped)}
    print(f"ARM-A STDERR CAP ({STDERR_CAP} chars) — stratified")
    for k in ("uncapped", "capped"):
        d = by_trunc[k]
        if d["n"]:
            print(f"  {k:9s} n={d['n']:3d}  A {d['a_green']}/{d['n']} ({d['a_green']/d['n']:.0%})"
                  f"  {treatment} {d['b_green']}/{d['n']} ({d['b_green']/d['n']:.0%})"
                  f"  McNemar p={d['mcnemar_p']:.2e}")
    print()

    print("SECOND-ERROR IDENTITY  (of repairs that FAILED, was attempt 1's error"
          " the same class as the start?)")
    terminal = {}
    second_error = {}
    for arm_idx, arm in ((0, "A"), (1, treatment)):
        failed = [p[arm_idx] for p in pairs if not p[arm_idx]["repaired"]]
        same = sum(1 for r in failed if r.get("second_error_same_as_first") is True)
        new = sum(1 for r in failed if r.get("second_error_same_as_first") is False)
        no_attempt = sum(1 for r in failed if produced_no_program(r))
        second_error[arm] = {"failed": len(failed), "same_class": same,
                             "new_class": new, "no_attempt": no_attempt}
        # explicit terminal_reason (post-P2 records) — absent on the frozen data
        tr = Counter(r.get("terminal_reason") for r in failed if r.get("terminal_reason"))
        terminal[arm] = dict(tr)
        print(f"  arm {arm}: {len(failed)} failed — same class {same}, new class {new}, "
              f"no corrective attempt {no_attempt}"
              + (f"  [terminal: {dict(tr)}]" if tr else ""))
    transport = sum(v for arm in terminal.values() for k, v in arm.items()
                    if k not in ("compile_failed",))
    if transport and transport / max(2 * n, 1) > 0.05:
        print(f"\n  !! {transport} pairs ended on a transport/truncation failure "
              f"(>5%) — the arm comparison may be contaminated; see --drop-transport",
              file=sys.stderr)

    # ── caret-line preservation: does the caret anchor the model to the line? ─
    caret = _caret_preservation(pairs, treatment)
    print("\nCARET-LINE PRESERVATION  (faust-rs quotes the offending source line;"
          " did it survive verbatim into attempt 1?)")
    if caret["n"]:
        for arm, k in (("A", "a_preserved"), (treatment, "b_preserved")):
            print(f"  arm {arm}: {caret[k]}/{caret['n']} ({caret[k]/caret['n']:.0%})")
        print(f"  discordant: {treatment}-kept/A-edited {caret['b_only']}, "
              f"A-kept/{treatment}-edited {caret['a_only']}  McNemar p={caret['mcnemar_p']:.2e}")
    else:
        print("  (no treatment-arm feedback quoted a source line)")

    s = {
        "model": model, "treatment": treatment, "treatment_label": ARM_LABEL[treatment],
        "n": n, "a_green": a_green, "b_green": b_green,
        "b_only": b_only, "a_only": a_only, "mcnemar_p": mc_p,
        "a_mean_attempts": sum(a_scores) / n, "b_mean_attempts": sum(b_scores) / n,
        "wilcoxon_p": w_p, "per_class": per_class,
        "rescue": rescue, "by_arm_a_truncation": by_trunc, "terminal": terminal,
        "second_error": second_error, "caret_preservation": caret,
    }
    s["verdict"] = verdict(s)
    print(f"\n>>> {s['verdict']}\n")
    return s


def verdict(s: dict) -> str:
    n, mc_p = s["n"], s["mcnemar_p"]
    t = f"arm {s['treatment']} ({s['treatment_label']})"
    delta = s["b_green"] - s["a_green"]
    if mc_p < 0.05 and delta > 0:
        return f"YES — {t} repaired {delta} more of {n} programs (McNemar p={mc_p:.2e})."
    if mc_p < 0.05 and delta < 0:
        return (f"NO — C++ stderr repaired {-delta} MORE than {t} "
                f"(McNemar p={mc_p:.2e}).")
    return (f"NO MEASURABLE DIFFERENCE at n={n} between arm A and {t} "
            f"(repaired {s['a_green']} vs {s['b_green']}, McNemar p={mc_p:.2e}).")


def make_chart(s: dict, chart_file: Path) -> None:
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
        d = s if c == "ALL" else s["per_class"][c]
        base = s["n"] if c == "ALL" else d["n"]
        a_pct.append(d["a_green"] / base * 100)
        b_pct.append(d["b_green"] / base * 100)

    x = range(len(classes))
    width = 0.38
    fig, ax = plt.subplots(figsize=(max(8, len(classes) * 1.3), 6))
    ba = ax.bar([i - width / 2 for i in x], a_pct, width, label="arm A — C++ stderr",
                color="#4e9af1", zorder=3)
    bb = ax.bar([i + width / 2 for i in x], b_pct, width,
                label=f"arm {s['treatment']} — {s['treatment_label']}",
                color="#f4a261", zorder=3)
    ax.set_xticks(list(x))
    ax.set_xticklabels(classes, rotation=30, ha="right")
    ax.set_ylim(0, 115)
    ax.set_ylabel("Repaired within 2 corrective attempts (%)")
    ax.set_title(f"faust-rs repair-loop A/B — {s['model']}, temp=0  (n={s['n']})")
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
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("result", type=Path)
    ap.add_argument("--json-out", type=Path)
    ap.add_argument("--screen", type=Path, metavar="CORPUS",
                    help="apply the corpus program screen (bench/corpus_screen.py): "
                         "restrict to code_shas that pass 'is this a Faust program?'")
    ap.add_argument("--drop-transport", action="store_true",
                    help="also drop pairs where either arm's generator never "
                         "produced a program (transport / truncation abort). "
                         "Default off — keeping them is conservative for arm A.")
    args = ap.parse_args()

    include: set[str] | None = None
    if args.screen:
        sys.path.insert(0, str(Path(__file__).resolve().parent))
        from corpus_screen import included_shas
        include = included_shas(args.screen)
        print(f"screen {args.screen.name}: {len(include)} programs pass", file=sys.stderr)

    records = load_records(args.result)
    todo = combos(records)
    if not todo:
        print("no (model, treatment-arm) combo with a matching arm A in the file",
              file=sys.stderr)
        return 1

    all_summaries = []
    for model, treatment in todo:
        pairs = load_pairs(records, model, treatment, include=include)
        if args.drop_transport:
            pairs = [(a, b) for a, b in pairs
                     if not produced_no_program(a) and not produced_no_program(b)]
        if not pairs:
            continue
        s = report(pairs, model, treatment)
        all_summaries.append(s)
        slug = f"{model.replace(':', '_').replace('.', '')}_{treatment}"
        make_chart(s, args.result.with_name(f"{args.result.stem}_{slug}_chart.png"))

    if args.json_out:
        args.json_out.write_text(json.dumps(all_summaries, indent=2) + "\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
