#!/usr/bin/env python3
"""bench/issue26/verify.py — reproduce the published issue-#26 numbers from the
committed data, with no model and no faust-rs.

What it re-derives, from bench/corpora/ and bench/results/repair_ab/ :

  * the corpus shape           — 202 distinct C++-rejected programs, class mix,
                                 and that every stored cpp_error_class still
                                 matches llm/error_classes.classify_error()
  * the program screen         — bench/corpus_screen.py excludes exactly the 10
                                 non-program rows; 192 pass
  * each headline A/B cell      — repaired-within-2 counts, exact McNemar,
                                 mean attempts-to-green, per-class McNemar,
                                 the first-vs-second-attempt (rescue) split,
                                 the arm-A 500-char stderr-cap strata, the
                                 second-error identity (same class vs new), and
                                 caret-line preservation (does faust-rs's quoted
                                 source line survive verbatim into attempt 1)
  * the fidelity claim          — recomputed from the *_fidelity.json `cells`
                                 dict under the screen (a derivation, not a
                                 checksum), incl. the paired "both A and B
                                 repaired" figures

against bench/issue26/expected.json.  Integer counts must match exactly;
p-values must sit under a recorded bound (or above 0.05 where the claim is
"not significant").  The run asserts it made exactly `checks_expected` checks —
a stale verify.py against a newer expected.json (or vice versa) fails loudly
instead of printing REPRODUCED over a subset.

    python3 bench/issue26/verify.py            # verify (exit 0 == reproduced)
    python3 bench/issue26/verify.py --freeze   # re-emit expected.json from a run

Only dependency: scipy.  Runs in ~1 s.
"""
from __future__ import annotations

import argparse
import json
import math
import sys
from collections import Counter
from contextlib import redirect_stdout
from pathlib import Path

_HERE = Path(__file__).resolve().parent
_ROOT = _HERE.parent.parent
for p in (_ROOT / "bench", _ROOT / "llm"):
    sys.path.insert(0, str(p))

import corpus_screen  # noqa: E402
import error_classes  # noqa: E402
import repair_ab_core  # noqa: E402
import score_repair_ab  # noqa: E402

EXPECTED_PATH = _HERE / "expected.json"
CORPUS = "bench/corpora/repair_corpus_20260830.json"
RESULT_3B = "bench/results/repair_ab/repair_ab_20260830.json"
RESULT_7B = "bench/results/repair_ab/repair_ab_20260830_7b.json"
FIDELITY_3B = "bench/results/repair_ab/repair_ab_20260830_fidelity.json"
FIDELITY_7B = "bench/results/repair_ab/repair_ab_20260830_7b_fidelity.json"

PASS, FAIL = "  ok  ", " FAIL "
_fails: list[str] = []
_checks = 0


def _check(label: str, got, want) -> None:
    global _checks
    _checks += 1
    ok = got == want
    print(f"[{PASS if ok else FAIL}] {label}: got {got!r}"
          + ("" if ok else f", want {want!r}"))
    if not ok:
        _fails.append(label)


def _check_p(label: str, p: float, bound: dict) -> None:
    """bound is {"le": x} (assert p <= x) or {"ge": x} (assert p >= x)."""
    global _checks
    _checks += 1
    if "le" in bound:
        ok, txt = p <= bound["le"], f"{p:.2e} <= {bound['le']:g}"
    else:
        ok, txt = p >= bound["ge"], f"{p:.2e} >= {bound['ge']:g}"
    print(f"[{PASS if ok else FAIL}] {label}: {txt}")
    if not ok:
        _fails.append(label)


def _p_bound(p: float) -> dict:
    """A recorded bound for a fresh p-value: one decade of headroom below 0.05,
    else 'must stay >= 0.05' for a claim that is deliberately not significant."""
    if p < 0.05:
        return {"le": 10.0 ** (math.ceil(math.log10(p)) + 1)}
    return {"ge": 0.05}


# ── observation: compute everything from the committed data ──────────────────

def _screened_fidelity_summary(fidelity_file: str, keep: set[str]) -> dict:
    """Recompute the per-arm shrink / primitive-loss summary from the sidecar's
    `cells` dict, restricted to the screened programs.  A derivation of the
    published fidelity numbers, not a read of the stored `summary`."""
    cells = json.loads((_ROOT / fidelity_file).read_text())["cells"]
    out: dict[str, dict] = {}
    for c in cells.values():
        if c["code_sha"] not in keep or not c["repaired"]:
            continue
        s = out.setdefault(c["arm"], {"wins": 0, "shrank": 0,
                                      "primitives_determinable": 0, "primitive_lost": 0})
        s["wins"] += 1
        s["shrank"] += int(c["shrank"])
        if c["primitives_expected"] is not None:
            s["primitives_determinable"] += 1
            s["primitive_lost"] += int(bool(c["primitive_lost"]))
    return {arm: out[arm] for arm in sorted(out)}


def _paired_both_won(result_file: str, fidelity_file: str, keep: set[str],
                     ctrl: str, treat: str) -> dict:
    records = json.loads((_ROOT / result_file).read_text())
    cells = json.loads((_ROOT / fidelity_file).read_text())["cells"]
    by_sha: dict[str, dict] = {}
    for r in records:
        by_sha.setdefault(r["code_sha"], {})[r["arm"]] = r
    both = [v for v in by_sha.values()
            if ctrl in v and treat in v and v[ctrl]["code_sha"] in keep
            and v[ctrl]["repaired"] and v[treat]["repaired"]]

    def cell(sha, arm):
        return cells[f"{sha}::{arm}"]

    g = {"n": len(both)}
    for tag, arm in ((ctrl.lower(), ctrl), (treat.lower(), treat)):
        g[f"{tag}_shrank"] = sum(cell(v[arm]["code_sha"], arm)["shrank"] for v in both)
        g[f"{tag}_primitive_lost"] = sum(
            1 for v in both if cell(v[arm]["code_sha"], arm)["primitive_lost"])
        g[f"{tag}_primitives_determinable"] = sum(
            1 for v in both if cell(v[arm]["code_sha"], arm)["primitives_expected"] is not None)
    return g


def _cell_observation(result_file: str, model: str, treatment: str,
                      keep: set[str]) -> dict:
    records = json.loads((_ROOT / result_file).read_text())
    pairs = score_repair_ab.load_pairs(records, model, treatment, include=keep)
    import io
    with redirect_stdout(io.StringIO()):          # report() is chatty
        s = score_repair_ab.report(pairs, model, treatment)

    per_class = {c: {k: d[k] for k in ("n", "a_green", "b_green", "b_only", "a_only")}
                 | {"mcnemar_p": d["mcnemar_p"]}
                 for c, d in s["per_class"].items()}
    return {
        "result_file": result_file, "model": model, "treatment": treatment,
        "n": s["n"], "a_green": s["a_green"], "b_green": s["b_green"],
        "b_only": s["b_only"], "a_only": s["a_only"],
        "mcnemar_p": s["mcnemar_p"], "wilcoxon_p": s["wilcoxon_p"],
        "a_mean_attempts": round(s["a_mean_attempts"], 6),
        "b_mean_attempts": round(s["b_mean_attempts"], 6),
        "per_class": per_class,
        "rescue": s["rescue"],
        "by_arm_a_truncation": {
            k: {kk: s["by_arm_a_truncation"][k][kk]
                for kk in ("n", "a_green", "b_green", "mcnemar_p")}
            for k in ("uncapped", "capped")},
        "second_error": s["second_error"],
        "caret_preservation": s["caret_preservation"],
    }


def observe() -> dict:
    raw = json.loads((_ROOT / CORPUS).read_text())
    entries = repair_ab_core.load_corpus(_ROOT / CORPUS)
    hist = Counter(error_classes.classify_error(e["cpp_stderr"]) for e in entries)
    stored_ok = all(
        r["cpp_error_class"] == error_classes.classify_error(r["cpp_stderr"])
        for r in raw if not r["compiles"])

    _, excluded = corpus_screen.screen(raw)
    keep = {r["code_sha"] for r in entries} & corpus_screen.included_shas(_ROOT / CORPUS)

    cells = [
        _cell_observation(RESULT_3B, "qwen2.5-coder:3b", "B", keep),
        _cell_observation(RESULT_3B, "qwen2.5-coder:3b", "C", keep),
        _cell_observation(RESULT_7B, "qwen2.5-coder:7b-instruct-q3_K_S", "B", keep),
        _cell_observation(RESULT_7B, "qwen2.5-coder:7b-instruct-q3_K_S", "C", keep),
    ]
    return {
        "corpus": {
            "file": CORPUS,
            "total_records": len(raw),
            "failing_records": sum(1 for r in raw if not r["compiles"]),
            "distinct_failing_programs": len(entries),
            "by_first_error_class": dict(sorted(hist.items())),
            "stored_cpp_error_class_matches_classifier": stored_ok,
        },
        "screen": {
            "kept": len(keep),
            "excluded": len(excluded),
            "shas": {r["code_sha"]: r["screen_reason"] for r in excluded},
        },
        "cells": cells,
        "fidelity": {
            "sidecars": [
                {"file": FIDELITY_3B, "model": "qwen2.5-coder:3b",
                 "summary_screened": _screened_fidelity_summary(FIDELITY_3B, keep)},
                {"file": FIDELITY_7B, "model": "qwen2.5-coder:7b-instruct-q3_K_S",
                 "summary_screened": _screened_fidelity_summary(FIDELITY_7B, keep)},
            ],
            "paired_both_won": _paired_both_won(
                RESULT_3B, FIDELITY_3B, keep, "A", "B"),
        },
    }


# ── freeze: observation -> expected.json ────────────────────────────────────

def freeze(obs: dict) -> dict:
    exp = json.loads(json.dumps(obs))          # deep copy
    for cell in exp["cells"]:
        cell["mcnemar_p"] = _p_bound(cell["mcnemar_p"])
        cell["wilcoxon_p"] = _p_bound(cell["wilcoxon_p"])
        for d in cell["per_class"].values():
            d["mcnemar_p"] = _p_bound(d["mcnemar_p"])
        for d in cell["by_arm_a_truncation"].values():
            d["mcnemar_p"] = _p_bound(d["mcnemar_p"])
        cell["caret_preservation"]["mcnemar_p"] = _p_bound(
            cell["caret_preservation"]["mcnemar_p"])
    exp["_schema"] = 2
    exp["_comment"] = (
        "Frozen expectations for bench/issue26/verify.py. Regenerate with "
        "`verify.py --freeze` ONLY when the committed corpus / result JSONs are "
        "deliberately changed, and say why in the commit. p-value fields are "
        "{le: x} (p must stay <= x) or {ge: 0.05} (claim is deliberately NOT "
        "significant).")
    exp["headline"] = (
        "faust-rs feedback fed verbatim into the repair loop LOWERS "
        "repaired-within-2. Program screen (bench/corpus_screen.py) -> "
        f"3B {exp['cells'][0]['a_green']}/{exp['cells'][0]['n']} (A) -> "
        f"{exp['cells'][0]['b_green']} (B) / {exp['cells'][1]['b_green']} (C); "
        f"7B-Q3 {exp['cells'][2]['a_green']}/{exp['cells'][2]['n']} (A) -> "
        f"{exp['cells'][2]['b_green']} (B) / {exp['cells'][3]['b_green']} (C). "
        "McNemar exact p < 1e-8 every headline cell; arm A also fewer "
        "attempts-to-green. On the programs where arm A's stderr was never "
        "truncated the gap is the same; see METHODOLOGY.md for the full "
        "limitations list.")
    exp["checks_expected"] = _count_checks(obs, exp)
    return exp


def _count_checks(obs: dict, exp: dict) -> int:
    """Dry compare pass — how many _check / _check_p calls a verify run makes."""
    import io
    global _checks, _fails
    saved_c, saved_f = _checks, _fails
    _checks, _fails = 0, []
    with redirect_stdout(io.StringIO()):
        _compare(obs, exp)
    n = _checks
    _checks, _fails = saved_c, saved_f
    return n


# ── compare: observation vs expected.json ──────────────────────────────────

def _compare(obs: dict, exp: dict) -> None:
    c, e = obs["corpus"], exp["corpus"]
    print("\n=== corpus ===")
    for k in ("total_records", "failing_records", "distinct_failing_programs"):
        _check(f"corpus {k}", c[k], e[k])
    _check("corpus class histogram", c["by_first_error_class"], e["by_first_error_class"])
    _check("stored cpp_error_class matches classifier",
           c["stored_cpp_error_class_matches_classifier"],
           e["stored_cpp_error_class_matches_classifier"])

    print("\n=== program screen ===")
    _check("screen kept", obs["screen"]["kept"], exp["screen"]["kept"])
    _check("screen excluded", obs["screen"]["excluded"], exp["screen"]["excluded"])
    _check("screen shas", obs["screen"]["shas"], exp["screen"]["shas"])

    for og, eg in zip(obs["cells"], exp["cells"]):
        tag = f"{eg['model']} A-vs-{eg['treatment']}"
        print(f"\n=== {tag} ===")
        for k in ("n", "a_green", "b_green", "b_only", "a_only",
                  "a_mean_attempts", "b_mean_attempts"):
            _check(f"{tag} {k}", og[k], eg[k])
        _check(f"{tag} arm A repaired more", og["a_green"] > og["b_green"], True)
        _check_p(f"{tag} McNemar p", og["mcnemar_p"], eg["mcnemar_p"])
        _check_p(f"{tag} Wilcoxon p", og["wilcoxon_p"], eg["wilcoxon_p"])
        for cls, ed in eg["per_class"].items():
            od = og["per_class"][cls]
            for k in ("n", "a_green", "b_green", "b_only", "a_only"):
                _check(f"{tag} [{cls}] {k}", od[k], ed[k])
            _check_p(f"{tag} [{cls}] McNemar p", od["mcnemar_p"], ed["mcnemar_p"])
        for arm in ("A", eg["treatment"]):
            for k in ("won_at_1", "still_broken", "rescued_at_2", "no_program"):
                _check(f"{tag} rescue[{arm}] {k}", og["rescue"][arm][k],
                       eg["rescue"][arm][k])
        for arm in ("A", eg["treatment"]):
            for k in ("failed", "same_class", "new_class", "no_attempt"):
                _check(f"{tag} second_error[{arm}] {k}",
                       og["second_error"][arm][k], eg["second_error"][arm][k])
        cp_o, cp_e = og["caret_preservation"], eg["caret_preservation"]
        for k in ("n", "a_preserved", "b_preserved", "b_only", "a_only"):
            _check(f"{tag} caret_preservation {k}", cp_o[k], cp_e[k])
        _check_p(f"{tag} caret_preservation McNemar p",
                 cp_o["mcnemar_p"], cp_e["mcnemar_p"])
        for strat in ("uncapped", "capped"):
            od, ed = og["by_arm_a_truncation"][strat], eg["by_arm_a_truncation"][strat]
            for k in ("n", "a_green", "b_green"):
                _check(f"{tag} cap[{strat}] {k}", od[k], ed[k])
            _check_p(f"{tag} cap[{strat}] McNemar p", od["mcnemar_p"], ed["mcnemar_p"])

    print("\n=== fidelity (recomputed from *_fidelity.json cells, screened) ===")
    for osc, esc in zip(obs["fidelity"]["sidecars"], exp["fidelity"]["sidecars"]):
        name = Path(esc["file"]).name
        for arm, want in esc["summary_screened"].items():
            for k, wv in want.items():
                _check(f"{name} {arm} {k}", osc["summary_screened"][arm][k], wv)
    op, ep = obs["fidelity"]["paired_both_won"], exp["fidelity"]["paired_both_won"]
    for k, wv in ep.items():
        _check(f"paired A-vs-B {k}", op[k], wv)


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--freeze", action="store_true",
                    help="re-emit expected.json from a fresh observation")
    args = ap.parse_args(argv)

    obs = observe()

    if args.freeze:
        exp = freeze(obs)
        EXPECTED_PATH.write_text(json.dumps(exp, indent=2) + "\n")
        print(f"froze {EXPECTED_PATH} — {exp['checks_expected']} checks", file=sys.stderr)
        return 0

    exp = json.loads(EXPECTED_PATH.read_text())
    print("issue #26 — reproducing the published repair-loop A/B from committed data")
    print(exp.get("headline", ""))
    _compare(obs, exp)

    want = exp.get("checks_expected")
    ran = _checks
    if want is not None and ran != want:
        _fails.append(f"checks run ({ran}) != checks_expected ({want}) — "
                      "verify.py and expected.json are out of sync; re-freeze")
        print(f"[{FAIL}] checks run == checks_expected: {ran} != {want}")

    print("\n" + "=" * 64)
    if _fails:
        print(f"REPRODUCTION FAILED — {len(_fails)} check(s): {'; '.join(_fails)}")
        return 1
    print(f"REPRODUCED — {ran} checks, every count and p-value bound holds.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
