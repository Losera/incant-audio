#!/usr/bin/env python3
"""bench/corpus_screen.py — a mechanical "is this actually a Faust program?" screen
for the faust-rs repair corpus.

WHY THIS EXISTS
    A weak generator, prompted with an artist/gear reference and nothing else
    (the L0 tier), sometimes answers with English instead of code:

        "I'm sorry, but as an AI language model, I do not have access to the
         specific sound of the band ..."

    and once with a program whose tail the model truncated mid-token, leaving a
    literal "..." in the source.  Ten of the 202 distinct C++-rejected rows in
    `repair_corpus_20260830.json` are one of those two things.  They are real
    generation *failures*, but they are not Faust programs, and a diagnostic
    comparison ("which compiler localises the error better") is meaningless on an
    essay.

    Rather than hand-pick a list of shas after seeing the A/B outcome — which is
    exactly the post-hoc filtering this whole exercise is meant to avoid — this
    module applies two outcome-blind syntactic rules and hands you the result as
    a function you can re-run:

      no_process_definition   no top-level `process = ...` / `process(...)`
      truncated_ellipsis      the source contains a literal `...`

    The rules are deliberately conservative.  `42c2cb9dbd09f972` has unbalanced
    parentheses (15 open, 14 close) — a real bug a syntax-repair corpus *should*
    contain — and is kept; paren balance is NOT a screen clause.

USAGE
    # write the exclusion sidecar the docs point at
    python3 bench/corpus_screen.py bench/corpora/repair_corpus_20260830.json \
        --out bench/corpora/repair_corpus_20260830_excluded.json

    # from Python — the include set the scorer / verify.py use
    from corpus_screen import included_shas
    keep = included_shas("bench/corpora/repair_corpus_20260830.json")
"""
from __future__ import annotations

import argparse
import json
import re
from pathlib import Path

# A top-level `process` binding: `process` at the start of a line (after optional
# whitespace) followed by `=` or `(`.  Faust requires exactly one; a file without
# it is not a program the compiler could ever accept.
_PROCESS_RE = re.compile(r"(?m)^\s*process\s*[(=]")

# The two screen clauses, in the order they are reported.  Each maps a reason
# string to a predicate over the raw `code` field.
_CLAUSES: tuple[tuple[str, "callable"], ...] = (
    ("truncated_ellipsis", lambda code: "..." in code),
    ("no_process_definition", lambda code: not _PROCESS_RE.search(code)),
)


def is_faust_program(code: str) -> tuple[bool, str | None]:
    """(True, None) if `code` passes every screen clause, else (False, reason)
    naming the first clause it failed."""
    for reason, fails in _CLAUSES:
        if fails(code or ""):
            return False, reason
    return True, None


def _distinct_failing(records: list[dict]) -> list[dict]:
    """First occurrence of each code_sha among the C++-rejected rows — the same
    dedup `repair_ab_core.load_corpus` does, kept here so this module needs
    nothing else."""
    seen: set[str] = set()
    out: list[dict] = []
    for r in records:
        if r["compiles"] or r["code_sha"] in seen:
            continue
        seen.add(r["code_sha"])
        out.append(r)
    return out


def screen(records: list[dict]) -> tuple[list[dict], list[dict]]:
    """(kept, excluded) over the distinct failing programs.  Each excluded row
    gains a `screen_reason` key."""
    kept: list[dict] = []
    excluded: list[dict] = []
    for r in _distinct_failing(records):
        ok, reason = is_faust_program(r["code"])
        if ok:
            kept.append(r)
        else:
            excluded.append({**r, "screen_reason": reason})
    return kept, excluded


def included_shas(corpus_path: str | Path) -> set[str]:
    """The code_shas that pass the screen — the `include` set for
    score_repair_ab.load_pairs and bench/repair_ab_repro/verify.py."""
    records = json.loads(Path(corpus_path).read_text())
    kept, _ = screen(records)
    return {r["code_sha"] for r in kept}


def excluded_rows(corpus_path: str | Path) -> list[dict]:
    """Compact records for the committed `_excluded.json` sidecar."""
    records = json.loads(Path(corpus_path).read_text())
    _, excluded = screen(records)
    return [
        {
            "code_sha": r["code_sha"],
            "prompt_id": r["prompt_id"],
            "tier": r["tier"],
            "cpp_error_class": r["cpp_error_class"],
            "screen_reason": r["screen_reason"],
            "code_head": (r["code"] or "")[:120],
        }
        for r in excluded
    ]


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("corpus", type=Path, help="repair_corpus_*.json")
    ap.add_argument("--out", type=Path, default=None,
                    help="write the exclusion sidecar here (default: print a summary)")
    args = ap.parse_args(argv)

    records = json.loads(args.corpus.read_text())
    kept, excluded = screen(records)
    rows = excluded_rows(args.corpus)

    print(f"{args.corpus.name}: {len(kept) + len(excluded)} distinct failing programs "
          f"-> {len(kept)} kept, {len(excluded)} excluded")
    from collections import Counter
    for reason, n in Counter(r["screen_reason"] for r in rows).most_common():
        print(f"  {reason:22s} {n}")
    for r in rows:
        print(f"  - {r['code_sha']}  {r['prompt_id']:18s} {r['screen_reason']}")

    if args.out:
        args.out.write_text(json.dumps(rows, indent=2) + "\n")
        print(f"\nwrote {args.out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
