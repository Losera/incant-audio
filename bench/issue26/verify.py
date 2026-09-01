#!/usr/bin/env python3
"""bench/issue26/verify.py — reproduce the published issue-#26 numbers from the
committed data, with no model and no faust-rs.

Re-derives, from bench/corpora/ and bench/results/repair_ab/ :

  * the corpus shape          — 202 distinct C++-rejected programs, class mix
  * each headline A/B cell     — repaired-within-2 counts, McNemar / Wilcoxon p,
                                mean attempts-to-green

and diffs them against bench/issue26/expected.json. Integer counts must match
exactly; p-values must sit under the recorded bound. Exit 0 = reproduced.

Only dependency: scipy (already in bench/requirements.txt). Runs in ~1 s.

    python bench/issue26/verify.py
"""
from __future__ import annotations

import io
import json
import sys
from collections import Counter
from contextlib import redirect_stdout
from pathlib import Path

_HERE = Path(__file__).resolve().parent
_ROOT = _HERE.parent.parent
for p in (_ROOT / "bench", _ROOT / "llm"):
    sys.path.insert(0, str(p))

import error_classes  # noqa: E402
import repair_ab_core  # noqa: E402
import score_repair_ab  # noqa: E402

EXPECTED = json.loads((_HERE / "expected.json").read_text())
PASS, FAIL = "  ok  ", " FAIL "
_fails: list[str] = []


def _check(label: str, got, want) -> None:
    ok = got == want
    print(f"[{PASS if ok else FAIL}] {label}: got {got!r}" + ("" if ok else f", want {want!r}"))
    if not ok:
        _fails.append(label)


def _check_le(label: str, got: float, bound: float) -> None:
    ok = got <= bound
    print(f"[{PASS if ok else FAIL}] {label}: {got:.2e} <= {bound:g}")
    if not ok:
        _fails.append(label)


def verify_corpus() -> None:
    c = EXPECTED["corpus"]
    path = _ROOT / c["file"]
    raw = json.loads(path.read_text())
    print(f"\n=== corpus: {c['file']} ===")
    _check("total records", len(raw), c["total_records"])
    _check("failing records", sum(1 for r in raw if not r["compiles"]), c["failing_records"])

    entries = repair_ab_core.load_corpus(path)
    _check("distinct failing programs", len(entries), c["distinct_failing_programs"])
    hist = Counter(error_classes.classify_error(e["cpp_stderr"]) for e in entries)
    _check("by first-error class", dict(sorted(hist.items())),
           dict(sorted(c["by_first_error_class"].items())))


def verify_cell(cell: dict) -> None:
    path = _ROOT / cell["result_file"]
    records = json.loads(path.read_text())
    pairs = score_repair_ab.load_pairs(records, cell["model"], cell["treatment"])
    with redirect_stdout(io.StringIO()):          # report() is chatty; we want its dict
        s = score_repair_ab.report(pairs, cell["model"], cell["treatment"])

    tag = f"{cell['model']} A-vs-{cell['treatment']}"
    print(f"\n=== {tag}   ({Path(cell['result_file']).name}) ===")
    for k in ("n", "a_green", "b_green", "b_only", "a_only"):
        _check(f"{tag} {k}", s[k], cell[k])
    _check_le(f"{tag} McNemar p", s["mcnemar_p"], cell["mcnemar_p_max"])
    _check_le(f"{tag} Wilcoxon p", s["wilcoxon_p"], cell["wilcoxon_p_max"])
    for k in ("a_mean_attempts", "b_mean_attempts"):
        got = round(s[k], 6)
        want = round(cell[k], 6)
        _check(f"{tag} {k}", got, want)
    # the direction of the finding, not just the magnitude
    _check(f"{tag} arm A repaired more", s["a_green"] > s["b_green"], True)


def main() -> int:
    print("issue #26 — reproducing the published repair-loop A/B from committed data")
    print(EXPECTED["headline"])
    verify_corpus()
    for cell in EXPECTED["cells"]:
        verify_cell(cell)

    print("\n" + "=" * 64)
    if _fails:
        print(f"REPRODUCTION FAILED — {len(_fails)} check(s): {', '.join(_fails)}")
        return 1
    print("REPRODUCED — every committed count and p-value bound holds.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
