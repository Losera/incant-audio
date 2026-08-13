#!/usr/bin/env python3
"""Classify Faust generation failures into named, actionable classes.

WHY THIS EXISTS, GIVEN score_efficacy.classify_error ALREADY DOES SOMETHING SIMILAR.
That function answers "what kind of thing went wrong" for the efficacy study, in five
coarse buckets (SYNTAX / HALLUCINATION / SEMANTIC / INCOMPLETE / UNCLASSIFIED). It is
right for what it does and is not replaced here.

It cannot answer the question PF-024 asks, for two specific reasons:

  1. `SEMANTIC` merges `endless evaluation cycle`, `N outputs must equal M input`, and
     `invalid delay parameter range` into one bucket. Those are three *different* prompt
     defects with three different fixes — a missing recursion example, a missing
     split/merge section, and a missing bounded-delay rule. Merged, a prompt change that
     fixes one and breaks another shows up as no change at all.
  2. `SYNTAX` discards the unexpected token. `syntax error, unexpected IDENT` and
     `syntax error, unexpected WITH` are the recorded signatures for two different
     failures (P6 #9 and #10), and the first is what the Faust compiler says when a
     program uses `let` — a construct the system prompt recommends and Faust does not
     have. Without the token, that finding is invisible.

So: same input, finer partition, and the token kept. Transport detection is NOT
reimplemented — `score_efficacy.is_transport_error` is imported, because it encodes the
2026-07-20 billing incident where five refused requests were scored as compile failures,
and a second copy of that rule would drift from the first.

Usage:
    python bench/classify_failures.py bench/results/results.json
    python bench/classify_failures.py --compare before.json after.json
"""
from __future__ import annotations

import argparse
import json
import re
import sys
from collections import Counter, defaultdict
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from score_efficacy import is_transport_error  # noqa: E402  (reused, not reimplemented)

# The taxonomy (class names, FIX_HINT, the regex rule table) moved to
# llm/error_classes.py on 2026-08-13 so generate.py's production retry loop
# and this benchmark harness classify errors with the SAME rules rather than
# two copies that can drift — see that module's docstring for the full
# rationale (it makes the same argument this file already made against
# reimplementing is_transport_error, applied to itself).
sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "llm"))
from error_classes import (  # noqa: E402
    OK, TRANSPORT, TRUNCATED, HALLUCINATED_SYMBOL, UNBOUND_VARIABLE,
    ROUTING_ARITY, DELAY_RANGE, RECURSION_CYCLE, DUPLICATE_SYMBOL, SYNTAX,
    TYPE_MISMATCH, INCOMPLETE, UNCLASSIFIED, FIX_HINT, _RULES,
)

# The token in `syntax error, unexpected WITH` / `unexpected IDENT`. Kept because it is
# the whole diagnostic value of a syntax failure.
_TOKEN_RE = re.compile(r"unexpected\s+([A-Z_]{2,}|[a-z_]+)", re.I)

_MIN_PROGRAM_LEN = 20  # matches score_efficacy.INCOMPLETE_MIN_LEN

_UNDEF_SYM_RE = re.compile(r"undefined symbol\s*:\s*([A-Za-z_]\w*)")


def _split_undefined_symbol(error: str, code: str) -> str:
    """`undefined symbol : X` covers two different defects with two different fixes.

    Faust says the same thing whether the model called a stdlib function that does not
    exist, or used a name it never bound. Both were in the corpus, and merging them
    would point at the wrong fix half the time:

      ef.flanger_mono(...)          -> there is no flanger_mono in ef. (it is pf.)
                                       fix: the namespace rule
      delayCh = x * (1-mix) + ...   -> `x` is never bound; the model copied the
                                       prompt's own echoCh(x) example and dropped the
                                       parameter list
                                       fix: show that a function taking an argument
                                       must declare it

    The distinguisher is whether the failing symbol is called through a namespace in the
    generated program. `ns.name` means the model reached for the standard library;
    a bare name it never defined means it lost track of its own scope.
    """
    m = _UNDEF_SYM_RE.search(error)
    if not m or not (code or "").strip():
        # The split is a property of the PROGRAM, not the error message. Without the
        # program there is nothing to inspect, so fall back to the historical class
        # rather than inventing a distinction the evidence does not support.
        return HALLUCINATED_SYMBOL
    sym = m.group(1)
    if re.search(rf"\b[A-Za-z_]\w*\.{re.escape(sym)}\b", code or ""):
        return HALLUCINATED_SYMBOL
    # Defined nowhere in the program, and not reached through a namespace.
    if re.search(rf"^\s*{re.escape(sym)}\s*[=(]", code or "", re.M):
        return HALLUCINATED_SYMBOL      # it IS defined; something subtler is wrong
    return UNBOUND_VARIABLE


class Failure:
    """One classified record."""

    __slots__ = ("klass", "detail", "category", "prompt", "error")

    def __init__(self, klass, detail="", category="", prompt="", error=""):
        self.klass = klass
        self.detail = detail
        self.category = category
        self.prompt = prompt
        self.error = error

    @property
    def label(self) -> str:
        """Class plus its distinguishing detail, e.g. `syntax:IDENT`."""
        return f"{self.klass}:{self.detail}" if self.detail else self.klass

    def __repr__(self):
        return f"<Failure {self.label}>"


def classify(error: str, code: str = "", record: dict | None = None) -> Failure:
    """Classify one failure. `record` is optional and only used for transport detection.

    Transport is checked first and deliberately: a refused request is not a compile
    outcome, and counting one as a compile failure is a mistake this project has already
    made once, at a cost of five records across five tiers.
    """
    if record is not None and is_transport_error(record):
        return Failure(TRANSPORT)

    err = (error or "").strip()
    if not err:
        return Failure(UNCLASSIFIED)

    low = err.lower()
    if "cut off at the output limit" in low or "truncat" in low:
        return Failure(TRUNCATED)

    for klass, pattern in _RULES:
        if pattern.search(err):
            detail = ""
            if klass == SYNTAX:
                m = _TOKEN_RE.search(err)
                if m:
                    detail = m.group(1).upper()
            elif klass == HALLUCINATED_SYMBOL:
                klass = _split_undefined_symbol(err, code)
            return Failure(klass, detail)

    stripped = (code or "").strip()
    if not stripped or len(stripped) < _MIN_PROGRAM_LEN:
        return Failure(INCOMPLETE)

    return Failure(UNCLASSIFIED)


# ── Record shapes ─────────────────────────────────────────────────────────────
#
# run_benchmark.py writes {first_try_compiles, error, code, category, prompt}.
# run_efficacy_study.py writes an `errors` LIST. Both are read here rather than
# normalising one into the other, because normalising loses the first-error
# distinction that is_transport_error depends on.

def _record_error(rec: dict) -> str:
    if rec.get("error"):
        return rec["error"]
    errors = rec.get("errors") or []
    return errors[0] if errors else ""


def _compiled(rec: dict) -> bool:
    if "first_try_compiles" in rec:
        return bool(rec["first_try_compiles"])
    return not (rec.get("errors") or [])


def classify_records(records: list[dict]) -> list[Failure]:
    """Classify every record, successes included (as class `ok`)."""
    out = []
    for rec in records:
        if _compiled(rec):
            out.append(Failure(OK, category=rec.get("category", ""),
                               prompt=rec.get("prompt", "")))
            continue
        err = _record_error(rec)
        f = classify(err, rec.get("code", ""), rec)
        f.category = rec.get("category", "")
        f.prompt = rec.get("prompt", "")
        f.error = err
        out.append(f)
    return out


# ── Reporting ─────────────────────────────────────────────────────────────────

def summarise(failures: list[Failure]) -> dict:
    total = len(failures)
    ok = sum(1 for f in failures if f.klass == OK)
    transport = sum(1 for f in failures if f.klass == TRANSPORT)
    # Compile rate excludes transport from the DENOMINATOR — see is_transport_error.
    denom = total - transport
    return {
        "total": total,
        "ok": ok,
        "transport_excluded": transport,
        "denominator": denom,
        "compile_rate": (ok / denom) if denom else 0.0,
        "by_class": Counter(f.label for f in failures if f.klass != OK),
    }


def print_report(failures: list[Failure], title: str = "") -> None:
    s = summarise(failures)
    if title:
        print(f"\n{title}")
        print("=" * len(title))
    print(f"first-try compile: {s['ok']}/{s['denominator']} = {s['compile_rate']:.0%}"
          + (f"   ({s['transport_excluded']} transport excluded)"
             if s["transport_excluded"] else ""))

    if not s["by_class"]:
        print("no failures")
        return

    print("\nfailure classes")
    width = max(len(lbl) for lbl in s["by_class"])
    for label, n in s["by_class"].most_common():
        klass = label.split(":")[0]
        print(f"  {label:<{width}}  {n:>2}   {FIX_HINT.get(klass, '')}")

    # per-class x per-category, so a fix that helps filters and hurts delays is visible
    grid = defaultdict(Counter)
    for f in failures:
        if f.klass != OK:
            grid[f.category][f.label] += 1
    if len(grid) > 1:
        print("\nby category")
        for cat in sorted(grid):
            items = ", ".join(f"{lbl}×{n}" for lbl, n in grid[cat].most_common())
            print(f"  {cat:<12} {items}")

    print("\nfailing prompts")
    for f in failures:
        if f.klass != OK:
            print(f"  [{f.label}] {f.prompt[:64]}")
            print(f"      {f.error.strip().splitlines()[0][:96] if f.error.strip() else ''}")


def compare(before: list[Failure], after: list[Failure]) -> None:
    """Per-class delta. The aggregate rate is reported but is NOT the headline.

    "88%" hid three unrelated failure modes in this project for months; a single number
    cannot show that a change fixed two classes and introduced a third.
    """
    b, a = summarise(before), summarise(after)
    print("\nper-class delta")
    print("=" * 14)
    print(f"compile rate  {b['compile_rate']:.0%} → {a['compile_rate']:.0%}"
          f"   ({b['ok']}/{b['denominator']} → {a['ok']}/{a['denominator']})")

    classes = sorted(set(b["by_class"]) | set(a["by_class"]))
    if not classes:
        print("no failures on either side")
        return
    width = max(len(c) for c in classes)
    print()
    for c in classes:
        nb, na = b["by_class"].get(c, 0), a["by_class"].get(c, 0)
        arrow = "→"
        if na < nb:
            verdict = f"fixed {nb - na}"
        elif na > nb:
            verdict = f"WORSE +{na - nb}"
        else:
            verdict = "unchanged"
        print(f"  {c:<{width}}  {nb:>2} {arrow} {na:<2}  {verdict}")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("results", nargs="?", default="bench/results/results.json")
    ap.add_argument("--compare", nargs=2, metavar=("BEFORE", "AFTER"),
                    help="per-class delta between two results files")
    args = ap.parse_args()

    if args.compare:
        before = classify_records(json.loads(Path(args.compare[0]).read_text()))
        after = classify_records(json.loads(Path(args.compare[1]).read_text()))
        print_report(before, f"BEFORE — {args.compare[0]}")
        print_report(after, f"AFTER — {args.compare[1]}")
        compare(before, after)
        return 0

    path = Path(args.results)
    if not path.exists():
        print(f"ERROR: {path} not found", file=sys.stderr)
        return 1
    print_report(classify_records(json.loads(path.read_text())), str(path))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
