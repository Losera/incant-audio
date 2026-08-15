"""Self-consistency check for docs/BUGS.md.

docs/BUGS.md is two documents wearing one file: a registry table near the top (the
durable, IDed source of truth) and a set of prose "detail sections" further down, many
of which restate a status in their own header. The file's own preamble (lines ~14-20 at
the time this test was written) admits the two halves have drifted out of sync TWICE
before and instructs a reader to "believe neither, read the code at HEAD" when they
disagree.

This test is deliberately narrow: it only checks BUGS.md against itself (registry row
status vs. detail-section header status). It does NOT reach into STATUS.md or any other
file — a registry-vs-STATUS.md cross-file drift (e.g. PF-041/PF-042, see
docs/sessions or STATUS.md's "Broken - ranked") is a different, harder-to-automate
question and is out of scope here on purpose.

Status vocabulary and the bucket mapping
-----------------------------------------
The registry's `Status` column uses four values: open, in-progress, fixed, wontfix
(docs/BUGS.md "Conventions"). Detail-section headers are much less disciplined: some
carry no status at all, some use an italic parenthetical at the end of the header line
("*(fixed 2026-07-28)*", "*(open, found 2026-07-28)*", "*(diagnosed 2026-07-28, not
fixed)*"), and at least one (PF-012's second detail section) states its status inline
in the header prose itself ("### PF-012 -- CLOSED 2026-07-30. ...") with no parenthetical
at all.

Rather than requiring an exact vocabulary match (which would make "in-progress" fight
every detail section that still says "open" from before a partial fix landed -- see
PF-032, where the registry is `in-progress` and one detail header says "open, found
2026-07-28" from before the low-pass half was fixed), this test collapses both sides
into two buckets:

    resolved   = {fixed, wontfix}
    unresolved = {open, in-progress}

and asserts the buckets agree. This is a deliberate judgment call: it means a registry
row that flips from `open` to `in-progress` (or vice versa) without a detail-header
update will NOT be flagged, only a fixed/unresolved split will. That is the right
trade-off for this test's job, which is catching "the row says fixed and the prose
still says open" -- exactly the PF-029/PF-031 case this test was written to catch --
not litigating open-vs-in-progress terminology drift.

Evidence this test actually catches something (required by CLAUDE.md's "a control
counts only once it has been seen failing"): run against docs/BUGS.md as of this
writing, this test fails with two mismatches:

    PF-029: registry=fixed  vs detail-section bucket=unresolved (header said
            "*(open, found 2026-07-28)*")
    PF-031: registry=fixed  vs detail-section bucket=unresolved (header said
            "*(open, found 2026-07-28)*", despite the section's own body later saying
            "CLOSED 2026-07-30" -- the header was never updated)

PF-032 (registry `in-progress`) has two detail sections, one headed "*(open, found
2026-07-28)*" and one headed "*(diagnosed 2026-07-28, not fixed)*". Both bucket to
`unresolved`, which matches `in-progress`'s bucket, so PF-032 does NOT fail under this
mapping -- consistent with the substance (the registry row itself says the fix is
half-done), even though the header vocabulary ("open") is stale relative to
"in-progress". That staleness is a real (smaller) inconsistency this test chooses not
to flag; see the module docstring above for why.
"""
import re
from pathlib import Path

import pytest

REPO = Path(__file__).resolve().parent.parent
BUGS_MD = REPO / "docs" / "BUGS.md"

REGISTRY_STATUSES = {"open", "in-progress", "fixed", "wontfix"}

RESOLVED = "resolved"
UNRESOLVED = "unresolved"

_BUCKET_OF_STATUS = {
    "fixed": RESOLVED,
    "wontfix": RESOLVED,
    "open": UNRESOLVED,
    "in-progress": UNRESOLVED,
}

# Registry table rows: "| PF-041 | title text | high | open | S4 Testing | ... |"
_REGISTRY_ROW_RE = re.compile(
    r"^\|\s*(PF-\d{3,})\s*\|(?P<rest>.*)\|\s*$"
)

# Detail-section headers: "### PF-029 -- the local ladder ... *(open, found 2026-07-28)*"
# May cite more than one ID ("### PF-037 / PF-038 / PF-039 -- ...").
_DETAIL_HEADER_RE = re.compile(r"^###\s+(?P<body>.*PF-\d{3,}.*)$")
_PF_ID_RE = re.compile(r"PF-\d{3,}")

# Trailing italic parenthetical, e.g. "*(fixed 2026-07-28)*" or
# "*(diagnosed 2026-07-28, not fixed)*". Tolerant: allows trailing whitespace only.
_TRAILING_PAREN_RE = re.compile(r"\*\(([^()]*)\)\*\s*$")

# Inline "-- CLOSED <date>" / "-- FIXED <date>" right after the em dash, used by
# PF-012's second detail section instead of a parenthetical.
_INLINE_STATUS_RE = re.compile(
    r"[—–-]\s*(CLOSED|FIXED|OPEN)\b", re.IGNORECASE
)


def parse_registry(text: str) -> dict[str, str]:
    """Return {PF-ID: status} for every row of the registry table.

    Tolerant of extra whitespace and markdown emphasis around the status token, but
    only accepts one of the four documented registry statuses -- a row this loose
    match cannot classify is skipped rather than guessed at.
    """
    statuses: dict[str, str] = {}
    for line in text.splitlines():
        m = _REGISTRY_ROW_RE.match(line.strip())
        if not m:
            continue
        pf_id = m.group(1)
        cols = [c.strip() for c in m.group("rest").split("|")]
        # cols after split of "rest": [Title, Sev, Status, Lane, File:line,
        # Discovered, Closed] -- Status is index 2, not 1 (ID's own pipes are
        # already consumed by the regex, so cols[0] is Title, not ID).
        if len(cols) < 3:
            continue
        status_raw = cols[2].strip().strip("*`").lower()
        if status_raw not in REGISTRY_STATUSES:
            continue
        statuses[pf_id] = status_raw
    return statuses


def classify_detail_status(header_text: str) -> str | None:
    """Classify a detail-section header's stated status into a bucket, or None.

    Returns "resolved", "unresolved", or None if the header does not clearly state a
    status (per the task's instruction: skip rather than guess).
    """
    m = _TRAILING_PAREN_RE.search(header_text)
    status_text = None
    if m:
        status_text = m.group(1)
    else:
        m2 = _INLINE_STATUS_RE.search(header_text)
        if m2:
            status_text = m2.group(1)
    if status_text is None:
        return None

    t = status_text.lower()

    if "superseded" in t:
        # Explicitly points elsewhere ("see above") rather than stating a status of
        # its own -- not classifiable, skip.
        return None
    if "not fixed" in t:
        return UNRESOLVED
    if "wontfix" in t or "won't fix" in t:
        return RESOLVED
    if "fixed" in t or "closed" in t:
        return RESOLVED
    if "in-progress" in t or "in progress" in t:
        return UNRESOLVED
    if "open" in t:
        return UNRESOLVED
    return None


def parse_detail_sections(text: str) -> list[tuple[str, str, str | None]]:
    """Return [(PF-ID, header_line, bucket_or_None), ...] for every detail section.

    A header naming multiple IDs (e.g. "PF-037 / PF-038 / PF-039") yields one tuple
    per ID, all sharing that header's classification.
    """
    out = []
    for line in text.splitlines():
        m = _DETAIL_HEADER_RE.match(line.strip())
        if not m:
            continue
        header = line.strip()
        ids = _PF_ID_RE.findall(header)
        bucket = classify_detail_status(header)
        for pf_id in ids:
            out.append((pf_id, header, bucket))
    return out


@pytest.fixture(scope="module")
def bugs_text() -> str:
    assert BUGS_MD.exists(), f"expected {BUGS_MD} to exist"
    return BUGS_MD.read_text(encoding="utf-8")


@pytest.fixture(scope="module")
def registry(bugs_text: str) -> dict[str, str]:
    reg = parse_registry(bugs_text)
    # Sanity: the registry table is the whole point of the file. If parsing finds
    # implausibly few rows, the regex has drifted from the table's real shape and a
    # silently-empty registry would make every assertion below vacuously pass.
    assert len(reg) > 40, (
        f"only parsed {len(reg)} registry rows -- the table format probably changed; "
        "re-check _REGISTRY_ROW_RE against docs/BUGS.md's current table"
    )
    return reg


@pytest.fixture(scope="module")
def details(bugs_text: str) -> list[tuple[str, str, str | None]]:
    d = parse_detail_sections(bugs_text)
    assert len(d) > 10, (
        f"only parsed {len(d)} detail-section headers -- _DETAIL_HEADER_RE probably "
        "no longer matches docs/BUGS.md's '### PF-NNN' section headers"
    )
    return d


class TestRegistryTableIsParseable:
    def test_known_ids_present(self, registry):
        for pf_id in ("PF-001", "PF-029", "PF-031", "PF-032", "PF-041"):
            assert pf_id in registry, f"{pf_id} missing from parsed registry"

    def test_known_statuses(self, registry):
        assert registry["PF-029"] == "fixed"
        assert registry["PF-031"] == "fixed"
        assert registry["PF-032"] == "in-progress"


class TestDetailHeaderClassifier:
    """Direct unit tests of the classifier against verbatim header strings recorded
    from docs/BUGS.md, independent of the file's current content."""

    def test_open_found_is_unresolved(self):
        header = (
            "### PF-029 — the local ladder does not run the tests CI runs. "
            "*(open, found 2026-07-28)*"
        )
        assert classify_detail_status(header) == UNRESOLVED

    def test_fixed_with_date_is_resolved(self):
        header = "### PF-009 / PF-010 — the prompt is measured again. *(fixed 2026-07-28)*"
        assert classify_detail_status(header) == RESOLVED

    def test_fixed_with_commit_hash_is_resolved(self):
        header = (
            "### PF-005 — Editor exposes only 8 of 64 parameters. "
            "*(fixed `2e129cd`, 2026-07-23)*"
        )
        assert classify_detail_status(header) == RESOLVED

    def test_diagnosed_not_fixed_is_unresolved(self):
        header = "### PF-032 — Two compiling patches render silent. *(diagnosed 2026-07-28, not fixed)*"
        assert classify_detail_status(header) == UNRESOLVED

    def test_inline_closed_is_resolved(self):
        header = "### PF-012 — CLOSED 2026-07-30. The comparison exists, and it found a prompt defect."
        assert classify_detail_status(header) == RESOLVED

    def test_superseded_is_unclassifiable(self):
        header = (
            "### PF-012 — a cross-model comparison was attempted and got 80% "
            "of the way. *(superseded, see above)*"
        )
        assert classify_detail_status(header) is None

    def test_no_status_at_all_is_unclassifiable(self):
        header = "### PF-013 — Semantic fidelity is unmeasured."
        assert classify_detail_status(header) is None


class TestRegistryAgreesWithDetailSections:
    """The actual self-consistency check.

    RED EVIDENCE (recorded 2026-08-11, before the docs/BUGS.md header fix landed):
    running this test against docs/BUGS.md as-is produced exactly two failures,
    naming PF-029 and PF-031 -- see the module docstring for the full mismatch
    detail. PF-032 did not fail, because its "open"/"not fixed" detail headers both
    bucket to `unresolved`, which matches its registry status of `in-progress`.
    """

    def test_every_detail_section_agrees_with_its_registry_row(self, registry, details):
        mismatches = []
        for pf_id, header, bucket in details:
            if bucket is None:
                continue  # header doesn't clearly state a status -- not our job to guess
            reg_status = registry.get(pf_id)
            if reg_status is None:
                continue  # detail section for an ID with no registry row -- not this test's concern
            reg_bucket = _BUCKET_OF_STATUS[reg_status]
            if reg_bucket != bucket:
                mismatches.append(
                    f"{pf_id}: registry status={reg_status!r} ({reg_bucket}) but "
                    f"detail header says {bucket} -- header: {header!r}"
                )

        assert not mismatches, (
            "docs/BUGS.md registry table and detail-section headers disagree for "
            f"{len(mismatches)} ID(s):\n" + "\n".join(mismatches)
        )
