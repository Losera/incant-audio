#!/usr/bin/env python3
"""PreToolUse hook (Write): blocks NEW documents whose FILENAME carries a date.

WHY. A date in a filename is metadata that goes stale the moment the document
outlives the day it was named for, and this repository has the receipts:
`docs/session_plan_2026-08-02.md` was written on 08-01, planned for 08-02, and
executed across 08-02 and 08-03 -- so its name was wrong for two of the three
days it mattered. `docs/test_plan_today_2026-07-21.md` (deleted 2026-08-19, see
docs/records/doc-purge-2026-08-19.md) said "today" in a filename, which was wrong
by construction on every day but one. A reader scanning `ls docs/`
cannot tell a stale document from a current one, and this project's recurring
defect is precisely a document that outlived the thing it described (CLAUDE.md:
three hooks that never ran, a benchmark measuring a deleted prompt).

Living documents get a SESSION NUMBER instead; see COLLABORATION.md. A number is
stable, sorts correctly, and never claims to be about a day it is not.

⚠️ POINT-IN-TIME ARTIFACTS ARE EXEMPT, and the distinction is the whole design.
A record of something that HAPPENED on a date -- a review conducted that day, a
benchmark run, an archived result -- is correctly dated, because the date is its
content, not its packaging. Those live under an exempt directory (see
EXEMPT_DIRS). The test is not "does this contain a date" but "is this document
still going to be edited": a plan is, a record is not.

SCOPE, deliberately narrow to avoid false positives:
  - Write only. Edit/MultiEdit cannot create a file, and firing there would block
    ordinary edits to the dated documents that already exist and are staying.
  - Files that do NOT already exist. Overwriting an existing dated file is an
    update to an artifact that is already named, not the creation of a new one.
  - Text documents only (see CHECKED_SUFFIXES). Dated data files -- an archived
    benchmark JSON, a log -- are records, not living documents.

Add future filename rules by extending DATE_PATTERNS or EXEMPT_DIRS only; do not
touch the scan logic (the contract invariant-hook-writer relies on).
"""
import json
import os
import re
import sys

# Suffixes that denote a living, human-edited document. A .json/.csv/.log with a
# date in the name is almost always an archived RUN, which is the exempt case.
CHECKED_SUFFIXES = (".md", ".txt", ".rst")

# Directories where a dated name is CORRECT, because the file records an event
# rather than tracking ongoing work. Matched against the repo-relative path.
EXEMPT_DIRS = (
    "docs/records/",       # point-in-time: reviews, audits, snapshots
    "bench/results/",      # archived benchmark runs
    "logs/",
    "artifacts/",
)

DATE_PATTERNS = [
    # 2026-08-02, 2026_08_02, 20260802 (8 digits starting with a plausible year)
    (re.compile(r"(?<!\d)(19|20)\d{2}[-_]?\d{2}[-_]?\d{2}(?!\d)"),
     "a full date"),
    # 2026-07 / 2026_07 -- a year-month is just as stale-prone as a full date
    (re.compile(r"(?<!\d)(19|20)\d{2}[-_]\d{2}(?!\d)"),
     "a year and month"),
    # "today", "yesterday", "tonight" in a filename are wrong by construction on
    # every day but one. docs/test_plan_today_2026-07-21.md (deleted) managed both
    # at once.
    (re.compile(r"(?:^|[-_])(today|yesterday|tomorrow|tonight)(?:[-_]|$)", re.I),
     "a relative day word"),
]

GUIDANCE = (
    "Name it for what it IS, and let a session number carry the sequence:\n"
    "    docs/sessions/003-playground-shell.md\n"
    "    docs/sessions/004-polyphony.md\n"
    "The next number is one above the highest already in docs/sessions/ -- no date\n"
    "arithmetic, nothing to recompute, and it stays true forever.\n"
    "\n"
    "If this really is a POINT-IN-TIME RECORD -- a review run on a given day, an\n"
    "archived result -- then the date is its content and it belongs in an exempt\n"
    "directory instead:\n"
    "    " + ", ".join(EXEMPT_DIRS) + "\n"
    "\n"
    "Rule and rationale: COLLABORATION.md, \"Naming documents\"."
)


def main() -> int:
    payload = json.load(sys.stdin)
    if payload.get("tool_name") != "Write":
        return 0

    file_path = payload.get("tool_input", {}).get("file_path", "")
    if not isinstance(file_path, str) or not file_path:
        return 0

    if not file_path.endswith(CHECKED_SUFFIXES):
        return 0

    # Only NEW files. An existing dated document is already named; rewriting it is
    # not the act this hook exists to prevent, and blocking it would make the
    # historical record un-editable.
    if os.path.exists(file_path):
        return 0

    cwd = payload.get("cwd") or os.getcwd()
    try:
        rel = os.path.relpath(file_path, cwd)
    except ValueError:          # different drive on Windows; fall back to the raw path
        rel = file_path
    rel = rel.replace(os.sep, "/")

    if any(rel.startswith(d) for d in EXEMPT_DIRS):
        return 0

    basename = os.path.basename(rel)

    # Match against the STEM, not the full basename. The day-word pattern anchors
    # on a separator or end-of-string, and ".md" is neither -- so "test_plan_today.md"
    # slipped through while "test_plan_today_2026" was caught. Found by running the
    # red cases rather than by reading the regex, which is the entire argument for
    # running them.
    stem = os.path.splitext(basename)[0]

    for pattern, what in DATE_PATTERNS:
        if pattern.search(stem):
            print(
                f"BLOCKED (document naming): '{basename}' has {what} in its filename.\n"
                "\n"
                "A date in a name goes stale the moment the document outlives the day it\n"
                "was named for. docs/session_plan_2026-08-02.md was written on 08-01 and\n"
                "worked through 08-03 -- wrong for two of the three days it mattered.\n"
                "\n" + GUIDANCE,
                file=sys.stderr,
            )
            return 2

    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception as exc:
        print(f"BLOCKED (hook error, failing closed): {exc}", file=sys.stderr)
        sys.exit(2)
