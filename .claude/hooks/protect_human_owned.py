#!/usr/bin/env python3
"""RETIRED 2026-07-21 by COLLABORATION.md revision 2 --- UNREGISTERED from
.claude/settings.json, kept only for reference. This hook gated *authorship* by file
category, which revision 2 no longer does: llm/prompts/* and docs/decisions.md are now
Claude-writable, under the Tier 2 evidence bar (COLLABORATION.md §3) and the consult
gate for architectural direction (§2). Do not re-register it without reading §9.

Original docstring follows.

PreToolUse hook (Write/Edit/MultiEdit): blocks direct Claude writes to files
COLLABORATION.md §1 marks as HUMAN-OWNED *in their entirety* --- llm/prompts/*
(product IP, wording-sensitive) and docs/decisions.md (human is author of record).

Scoped to whole-file HUMAN-OWNED paths only. Real-time audio code (FaustEngine.cpp,
PluginProcessor.cpp) is HUMAN-OWNED only within specific function bodies, not the
whole file -- that's handled by check_rt_safety.py's function-level scoping instead
of a blanket block here, since e.g. FaustEngine::compile() in the same file is
Claude-editable.

Deliberately does NOT cover bench/prompts/system_faust.txt or system_cmajor.txt --
those are not in COLLABORATION.md's HUMAN-OWNED list today, even though ADR-009's
rule text must be kept in sync with llm/prompts/system_prompt.txt by hand. That's a
policy question for a human decision (e.g. via the architecture-planning skill),
not one this hook resolves unilaterally.

Fails closed (exit 2) on any unexpected error.
"""
import json
import re
import sys
from pathlib import Path

PROMPTS_DIR_RE = re.compile(r"(^|/)llm/prompts/")
DECISIONS_FILE_RE = re.compile(r"(^|/)docs/decisions\.md$")

WATCHED_TOOLS = {"Write", "Edit", "MultiEdit"}


def resolved_posix_path(file_path: str, cwd: str) -> str:
    p = Path(file_path)
    if not p.is_absolute():
        p = Path(cwd) / p
    return p.resolve(strict=False).as_posix()


def main() -> int:
    payload = json.load(sys.stdin)
    if payload.get("tool_name") not in WATCHED_TOOLS:
        return 0

    file_path = payload.get("tool_input", {}).get("file_path", "")
    if not file_path:
        return 0

    cwd = payload.get("cwd", ".")
    path = resolved_posix_path(file_path, cwd)

    if PROMPTS_DIR_RE.search(path):
        print(
            "BLOCKED (HUMAN-OWNED, COLLABORATION.md §1): llm/prompts/* is product IP "
            "-- generation quality is sensitive to exact wording. You may suggest the "
            "rule/wording change in your response; the human edits this file directly.",
            file=sys.stderr,
        )
        return 2

    if DECISIONS_FILE_RE.search(path):
        print(
            "BLOCKED (HUMAN-OWNED, COLLABORATION.md §1): docs/decisions.md ADR entries "
            "are authored by the human. Draft the proposed ADR text in your response "
            "instead of writing it directly (see the architecture-planning skill).",
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
