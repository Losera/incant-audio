#!/usr/bin/env python3
"""PreToolUse hook (Write/Edit/MultiEdit): keeps the ADR-009 duplicate-symbol
RULE TEXT in sync between llm/prompts/system_prompt.txt and
bench/prompts/system_faust.txt, closing the gap protect_human_owned.py
deliberately leaves open (bench/prompts/* is Claude-editable, so the two copies
of the rule could silently drift apart).

IMPORTANT scoping distinction: the rule ADR-009 protects (no duplicate Faust
symbols in generated programs) is NOT hookable -- that would require parsing an
arbitrary Faust program's full symbol table. What IS hookable is the lexical
property that the rule's *text* stays present and consistent across the two
prompt files. That is all this hook checks.

What it enforces, and why it is scoped this way:
  1. The verbatim-shared ADR-009 sentence (anchored on "Never define the same
     variable or function name", extended to end of line) must be present in
     BOTH files after the edit, and must be character-identical between them.
     Exact equality is checkable here because the sentence is verbatim-shared
     today.
  2. Each file must still contain a "process ... exactly once" line. Presence
     only, not equality: the two files already word this clause differently
     ("Every Faust program must define process exactly once." vs "process must
     be defined exactly once, and never re-defined"), so equality would flag
     the current, human-ratified state as a violation. Divergence of this
     clause's *wording* is therefore not mechanically detectable; losing the
     clause entirely is.

This hook only fires when the edited file is one of the two prompt files. The
counterpart file is READ from disk, never modified -- llm/prompts/* is
HUMAN-OWNED and protect_human_owned.py independently blocks writes to it; this
hook complements that one. Sync direction is bench <- llm: if the human rewords
the rule in llm/prompts/system_prompt.txt (outside Claude's tools, so no hook
fires), a later Claude edit syncing bench/prompts/system_faust.txt passes
because the shared sentence is compared against the llm file's current on-disk
text, not a hardcoded copy. If the human ever rewords the anchor prefix itself,
this hook fails closed with a message saying its anchor is stale -- update the
ANCHOR regexes below to match the new human-chosen wording.

For Write, tool_input.content is the full new file. For Edit/MultiEdit, the
post-edit content is reconstructed from the file on disk; if old_string is not
found, this fails closed (exit 2) rather than allowing an unverifiable edit.
Fails closed on any unexpected error.
"""
import json
import re
import sys
from pathlib import Path

LLM_PROMPT_RE = re.compile(r"(^|/)llm/prompts/system_prompt\.txt$")
BENCH_PROMPT_RE = re.compile(r"(^|/)bench/prompts/system_faust\.txt$")

LLM_REL = "llm/prompts/system_prompt.txt"
BENCH_REL = "bench/prompts/system_faust.txt"

# Anchor for the verbatim-shared ADR-009 sentence; the match extends to end of
# line so any trailing rewording of the sentence is caught as divergence.
SHARED_SENTENCE_RE = re.compile(r"Never define the same variable or function name[^\n]*")

# Presence-only check for the process-uniqueness clause (worded differently in
# each file today -- see module docstring for why equality is not enforced).
PROCESS_ONCE_RE = re.compile(r"process[^\n]*exactly once")

WATCHED_TOOLS = {"Write", "Edit", "MultiEdit"}


def resolved_posix_path(file_path: str, cwd: str) -> str:
    p = Path(file_path)
    if not p.is_absolute():
        p = Path(cwd) / p
    return p.resolve(strict=False).as_posix()


def get_effective_content(tool_name: str, tool_input: dict, abs_path: Path) -> str:
    if tool_name == "Write":
        return tool_input.get("content", "")

    if not abs_path.exists():
        raise RuntimeError(
            f"cannot verify ADR-009 sync: {abs_path} not found on disk"
        )
    content = abs_path.read_text()

    if tool_name == "Edit":
        edits = [
            {
                "old_string": tool_input.get("old_string", ""),
                "new_string": tool_input.get("new_string", ""),
                "replace_all": tool_input.get("replace_all", False),
            }
        ]
    else:  # MultiEdit
        edits = tool_input.get("edits", [])

    for edit in edits:
        old = edit.get("old_string", "")
        new = edit.get("new_string", "")
        if old not in content:
            raise RuntimeError(
                "cannot verify ADR-009 sync: old_string not found in current "
                "file content (stale read or concurrent modification) -- "
                "refusing to guess, failing closed"
            )
        if edit.get("replace_all"):
            content = content.replace(old, new)
        else:
            content = content.replace(old, new, 1)

    return content


def block(message: str) -> int:
    print(
        "BLOCKED (ADR-009 prompt sync): " + message + "\n"
        "The ADR-009 duplicate-symbol rule text must stay present and in sync "
        f"in BOTH {LLM_REL} and {BENCH_REL}. Note {LLM_REL} is HUMAN-OWNED "
        "(COLLABORATION.md §1): if the rule's wording needs to change, the "
        "human edits the llm copy first, then the bench copy is synced to match "
        "it. If you believe this hook's anchor text is stale, stop and ask the "
        "human rather than retrying (see .claude/hooks/check_adr009_prompt_sync.py).",
        file=sys.stderr,
    )
    return 2


def main() -> int:
    payload = json.load(sys.stdin)
    tool_name = payload.get("tool_name")
    if tool_name not in WATCHED_TOOLS:
        return 0

    tool_input = payload.get("tool_input", {})
    file_path = tool_input.get("file_path", "")
    if not file_path:
        return 0

    cwd = payload.get("cwd", ".")
    path = resolved_posix_path(file_path, cwd)

    llm_match = LLM_PROMPT_RE.search(path)
    bench_match = BENCH_PROMPT_RE.search(path)
    if not llm_match and not bench_match:
        return 0

    # Derive the project root from the edited file's own path (cwd is not
    # guaranteed to be the project root), then locate the counterpart file.
    if llm_match:
        root = path[: llm_match.start()]
        edited_rel, other_rel = LLM_REL, BENCH_REL
    else:
        root = path[: bench_match.start()]
        edited_rel, other_rel = BENCH_REL, LLM_REL
    other_path = Path((root + "/" if root else "") + other_rel)

    edited_content = get_effective_content(tool_name, tool_input, Path(path))
    if not other_path.exists():
        raise RuntimeError(
            f"cannot verify ADR-009 sync: counterpart file {other_path} not "
            "found on disk"
        )
    other_content = other_path.read_text()

    # Check 1: verbatim-shared sentence present in both, identical across both.
    edited_sentences = SHARED_SENTENCE_RE.findall(edited_content)
    other_sentences = SHARED_SENTENCE_RE.findall(other_content)
    if not edited_sentences:
        return block(
            f"this edit would leave {edited_rel} without the shared ADR-009 "
            "sentence (anchor: \"Never define the same variable or function "
            "name...\"). Removing or rewording it in one file only makes the "
            "two prompt copies drift apart."
        )
    if not other_sentences:
        return block(
            f"the counterpart file {other_rel} does not currently contain the "
            "shared ADR-009 sentence this hook anchors on -- either it drifted "
            "or the hook's anchor is stale. Failing closed until a human "
            "reconciles the two files (and this hook's anchor, if rewording "
            "was intentional)."
        )
    variants = set(edited_sentences) | set(other_sentences)
    if len(variants) != 1:
        listing = "\n".join(f"  - {v!r}" for v in sorted(variants))
        return block(
            "this edit would make the shared ADR-009 sentence diverge between "
            "the two prompt files. Variants that would exist after the edit:\n"
            + listing
        )

    # Check 2: neither file may lose its process-uniqueness clause. Presence
    # only -- the two files legitimately word this clause differently today.
    if not PROCESS_ONCE_RE.search(edited_content):
        return block(
            f"this edit would leave {edited_rel} without a \"process ... "
            "exactly once\" rule line, dropping half of the ADR-009 rule text."
        )
    if not PROCESS_ONCE_RE.search(other_content):
        return block(
            f"the counterpart file {other_rel} does not currently contain a "
            "\"process ... exactly once\" rule line -- the copies have already "
            "drifted. Failing closed until a human reconciles them."
        )

    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception as exc:
        print(f"BLOCKED (hook error, failing closed): {exc}", file=sys.stderr)
        sys.exit(2)
