#!/usr/bin/env python3
r"""Shared parser for STATUS.md, in the same spirit as tools/id_graph.py.

WHY THIS EXISTS
---------------
STATUS.md has four independent readers — `tools/check.sh`'s `level_assumed()`,
`tools/status_digest.sh` (what `/orient` prints every session), and two call
sites in `tools/health_report.py` — and until this module existed, each one
carried its own copy of the "## HEADING ... to the next ##" extractor, and
three of them carried their own copy of "what counts as an Assumed claim."

That duplication was not cosmetic. From 2026-09-23 (when the "Assumed, never
checked" section started being written as bold PARAGRAPHS rather than bold
BULLETS) until this module replaced the old regexes, `tools/check.sh assumed`
and `tools/status_digest.sh` both matched on `^\s*[-*]\s+\*\*` — a leading
bullet — and silently reported **0 claims** every single session, against a
section that in fact held 3. CLAUDE.md calls this count "the one number":
"Every piece of work should move at least one claim out of STATUS.md's
'Assumed, never checked' list." A 0 that should have been a 3 does not just
fail to help scheduling — it actively asserts the project has no open
measurement debt when it has at least one `high, open` item (PF-079).

`tools/health_report.py`'s `_check_assumed_drift()` independently noticed the
counter disagreed with the prose (see `artifacts/health/lanes/product.json`'s
`assumed_drift` field) — but that only runs in `check.sh full`'s product lane
and writes to a JSON artifact nothing at session start reads. The bug was
detected and the detection went nowhere. One parser, used everywhere, is the
fix for the *class* of bug, not just this instance of it: see
`test_no_second_status_parser_exists` in tests/test_control_wiring.py, which
greps for a second implementation of either extractor and is the red case
this module exists to make permanently green.

WHAT COUNTS AS A CLAIM
-----------------------
STATUS.md's "Assumed, never checked" section is prose, not a list. Each real
claim is one paragraph (text between blank lines) whose first non-whitespace
token is a bolded span — `**like this**` — covering the whole opening clause.
A paragraph containing bold *emphasis* partway through (e.g. the word
**no-op** inside a sentence) is not a claim; only a paragraph that *opens*
bold is. This was verified against STATUS.md's real section at HEAD:

    1. **The compile gate was assumed to track real plugin success...**  claim
    2. PF-011 — "the efficacy tier gradient on the shipping model"...     prose
    3. **This section disagrees with `docs/BUGS.md`'s own registry...**  claim
    4. **Benchmark staleness (2026-09-08 -> partly closed 2026-09-09):**  claim

— three claims, matching the section's own PF-079/BUGS-registry/benchmark
narrative, not the "6" a naive whole-section bold-span count would give (that
count would also catch **no-op** and **dynamics**, two inline emphases that
assert nothing).

Also ported here, verbatim, because it was already correct and only existed
in one place: `tools/status_digest.sh`'s "Broken" extractor (bold numbered
headline per item) and its trailing-`---`-strip on `section()`, which
`tools/check.sh`'s copy of `section()`-equivalent logic never had.

NOT COVERED: this module parses STATUS.md's *prose structure*. It does not
judge whether a claim is still true, whether "Broken" is complete, or
anything requiring reading code — that stays COLLABORATION.md §3's job, for
a human or an agent to do by hand. It also does not touch any other
status-bearing file (`docs/BUGS.md`, `docs/decisions.md`); those have their
own readers (`tools/id_graph.py` for ADR/PF resolution, `tools/health_report.py`
for the BUGS registry table).

Dependency-free by design: stdlib `re` / `pathlib` only, matching
`tools/id_graph.py`'s constraint.
"""
from __future__ import annotations

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
STATUS_MD = ROOT / "STATUS.md"

_SECTION_RE_TEMPLATE = r"^##+\s*{pattern}.*?$(.*?)(?=^##\s|\Z)"
_STAMP_RE = re.compile(r"^#\s*PluginForge\s*—\s*Status\s*\((\d{4}-\d{2}-\d{2})\)", re.M)
_BROKEN_ITEM_RE = re.compile(r"^\*\*(\d+\..+?)\*\*\s*(\*\(.+?\)\*)?", re.M | re.S)
# A paragraph "claims" iff, once collapsed to one line, it OPENS with a bold
# span. `\A` (not `^` + re.M) anchors to the start of the paragraph itself,
# so a bold phrase anywhere later in the same paragraph (inline emphasis, a
# "**Reconciled ...:**" sub-note two sentences in) does not count.
_LEADING_BOLD_RE = re.compile(r"\A\*\*(.+?)\*\*")


def _read(text: str | None) -> str:
    if text is not None:
        return text
    if not STATUS_MD.exists():
        raise FileNotFoundError(f"{STATUS_MD} does not exist")
    return STATUS_MD.read_text()


def section(pattern: str, text: str | None = None) -> str | None:
    """Body under the first `##`/`###`/... heading matching `pattern` (a regex
    fragment, e.g. r"Assumed"), up to the next top-level `##` heading or EOF.
    Returns None if no such heading exists. Strips a trailing `---` rule,
    which belongs to the next section's heading, not this body."""
    txt = _read(text)
    m = re.search(_SECTION_RE_TEMPLATE.format(pattern=pattern), txt, re.S | re.M)
    if not m:
        return None
    return re.sub(r"\n-{3,}\s*$", "", m.group(1).strip()).strip()


def section_title(pattern: str, text: str | None = None) -> str | None:
    """The full heading line matching `pattern` (e.g. "## Next three things"),
    with leading `#`s stripped. None if not found."""
    txt = _read(text)
    m = re.search(rf"^(##+\s*{pattern}.*?)$", txt, re.M)
    return m.group(1).lstrip("# ").strip() if m else None


def stamp(text: str | None = None) -> str | None:
    """The `YYYY-MM-DD` in STATUS.md's dated title line, as a string (callers
    needing a date object can `datetime.date.fromisoformat()` it), or None."""
    txt = _read(text)
    m = _STAMP_RE.search(txt)
    return m.group(1) if m else None


def assumed_claims(text: str | None = None) -> list[str]:
    """Titles of the real claims in '## Assumed, never checked' — one per
    paragraph that OPENS bold. See module docstring for why this is not a
    bullet count and not a whole-section bold-span count."""
    body = section(r"Assumed", text)
    if body is None:
        return []
    out = []
    for para in re.split(r"\n\s*\n", body):
        para = " ".join(para.split())  # collapse wrapping/whitespace
        m = _LEADING_BOLD_RE.match(para)
        if m:
            out.append(m.group(1))
    return out


def assumed_claim_texts(text: str | None = None) -> list[str]:
    """Full paragraph text of each claim in '## Assumed, never checked' — not
    just the bold opening clause `assumed_claims()` returns. A claim often
    cites a `PF-NNN` or a date well past its bold headline (e.g. "This
    section disagrees with `docs/BUGS.md`'s own registry..." names PF-011
    two sentences later), so a caller cross-referencing claim content
    against another source (see `tools/health_report.py`'s
    `_check_assumed_drift`) needs the whole paragraph, not the title.

    Deliberately returns each paragraph with its ORIGINAL line breaks, not
    collapsed to one line. `tools/id_graph.py`'s reference scanner skips a
    whole *line* that contains a historical-mention phrase ("was never",
    "superseded", "deleted", ...) so that one such phrase doesn't suppress a
    live citation on the next line. Collapsing a multi-sentence paragraph to
    a single line defeats that: a claim reading "...this claim was never
    updated" anywhere in it would blank out every `PF-NNN` in the whole
    paragraph, not just that one clause. A caller that needs a one-line
    display string should collapse whitespace itself, after any ID scan."""
    body = section(r"Assumed", text)
    if body is None:
        return []
    out = []
    for para in re.split(r"\n\s*\n", body):
        para = para.strip()
        if _LEADING_BOLD_RE.match(" ".join(para.split())):
            out.append(para)
    return out


def broken_items(text: str | None = None) -> list[tuple[str, str]]:
    """(headline, tag) pairs from '## Broken — ranked', one per `**N. ...**`
    numbered item, tag being the trailing `*(...)*` annotation if present."""
    body = section(r"Broken", text)
    if body is None:
        return []
    out = []
    for head, tag in _BROKEN_ITEM_RE.findall(body):
        out.append((" ".join(head.split()), " ".join(tag.split()) if tag else ""))
    return out


def next_three_body(text: str | None = None) -> str | None:
    """Raw body of '## Next three things', verbatim (callers needing the
    `*(evidence)*`-tagged item parse it further — see
    tests/test_control_wiring.py's evidence-slot assertion)."""
    return section(r"Next three", text)


def waiting_on_you_body(text: str | None = None) -> str | None:
    """Raw body of '## Waiting on you', verbatim."""
    return section(r"Waiting on you", text)


if __name__ == "__main__":  # pragma: no cover - manual sanity check
    claims = assumed_claims()
    print(f"Assumed, never checked: {len(claims)} claims")
    for c in claims:
        print(f"  - {c}")
    items = broken_items()
    print(f"Broken — ranked: {len(items)} items")
