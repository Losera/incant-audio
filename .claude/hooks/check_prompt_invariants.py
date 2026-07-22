#!/usr/bin/env python3
"""PreToolUse hook (Write/Edit/MultiEdit): guards llm/prompts/system_prompt.txt.

REPLACES check_adr009_prompt_sync.py, which is now retired. That hook kept the
ADR-009 rule text in sync across two prompt files. As of 2026-07-21 there is only
one prompt file — bench/prompts/system_faust.txt was deleted and the benchmark now
loads the production prompt — so a *sync* check has nothing to compare against and
would fail closed on every edit.

What replaced it is a stronger check on the one remaining file. The sync hook's
real-world failure is worth recording: it verified one sentence and one regex, and
the team believed it guaranteed the two files could not drift. They had in fact
drifted substantially, and — worse — BOTH files taught Faust functions that do not
exist (`ef.ping_pong`, `ef.chorus`, `ef.flanger`). Two of the four few-shot examples
in the production prompt did not compile. A check on a proxy created confidence
proportional to the invariant rather than to the proxy.

WHAT THIS ENFORCES (all mechanically decidable):
  1. The ADR-009 duplicate-symbol sentence is still present.
  2. A "process ... exactly once" clause is still present.
  3. The generated-stdlib-block markers are intact, so tools/gen_stdlib_block.py
     can still splice, and the block cannot be silently hand-edited away.
  4. Every `ns.func` token in the file resolves against the INSTALLED Faust
     library — the anti-fabrication check, and the one that would have caught the
     2026-07-21 defect on the day it was introduced.

WHAT THIS DOES NOT CATCH (stated per COLLABORATION.md §7):
  - Whether a few-shot example COMPILES. That needs the faust binary and is too
    slow for a PreToolUse hook; tests/test_prompt_stdlib.py covers it in CI.
  - Whether a rule or description is *correct*, only that names exist. A function
    can exist and still be described wrongly or used with the wrong arity.
  - Prompt QUALITY. Nothing mechanical can tell you a prompt generates worse Faust;
    that is what bench/check_prompt_regression.py and the efficacy study are for.

Skips silently (exit 0) when the Faust library is not installed — the check would
be meaningless, and a machine without Faust must still be able to edit docs.
Fails closed (exit 2) on any unexpected error.
"""
import json
import re
import sys
from pathlib import Path

PROMPT_RE = re.compile(r"(^|/)llm/prompts/system_prompt\.txt$")
PROMPT_REL = "llm/prompts/system_prompt.txt"

FAUST_STDLIB = Path("/usr/share/faust/stdfaust.lib")

SHARED_SENTENCE_RE = re.compile(r"Never define the same variable or function name[^\n]*")
PROCESS_ONCE_RE = re.compile(r"process[^\n]*exactly once")
BEGIN_MARKER = "# BEGIN GENERATED STDLIB REFERENCE"
END_MARKER = "# END GENERATED STDLIB REFERENCE"

WATCHED_TOOLS = {"Write", "Edit", "MultiEdit"}


def resolved_posix_path(file_path: str, cwd: str) -> str:
    p = Path(file_path)
    if not p.is_absolute():
        p = Path(cwd) / p
    return p.resolve(strict=False).as_posix()


def get_effective_content(tool_name: str, tool_input: dict, abs_path: Path) -> str:
    """Post-edit content. For Edit/MultiEdit this is reconstructed from disk;
    an old_string that is not found fails closed rather than guessing."""
    if tool_name == "Write":
        return tool_input.get("content", "")

    if not abs_path.exists():
        raise RuntimeError(f"cannot verify prompt invariants: {abs_path} not on disk")
    content = abs_path.read_text()

    if tool_name == "Edit":
        edits = [{
            "old_string": tool_input.get("old_string", ""),
            "new_string": tool_input.get("new_string", ""),
            "replace_all": tool_input.get("replace_all", False),
        }]
    else:
        edits = tool_input.get("edits", [])

    for edit in edits:
        old = edit.get("old_string", "")
        new = edit.get("new_string", "")
        if old not in content:
            raise RuntimeError(
                "cannot verify prompt invariants: old_string not found in current "
                "file content (stale read or concurrent modification) -- refusing "
                "to guess, failing closed"
            )
        count = -1 if edit.get("replace_all") else 1
        content = content.replace(old, new, count) if count > 0 else content.replace(old, new)

    return content


def block(message: str) -> int:
    print(
        "BLOCKED (prompt invariants): " + message + "\n\n"
        f"{PROMPT_REL} is the single system prompt — the product and the benchmark "
        "both load it, so an error here changes generated audio AND invalidates "
        "every measurement. If a stdlib name was rejected, check it against the "
        "installed library rather than memory:\n"
        "    python tools/gen_stdlib_block.py --verify-prompt\n"
        "    python tools/gen_stdlib_block.py --write   # regenerate the block\n"
        "See .claude/hooks/check_prompt_invariants.py for what this does NOT catch.",
        file=sys.stderr,
    )
    return 2


def main() -> int:
    payload = json.load(sys.stdin)
    if payload.get("tool_name") not in WATCHED_TOOLS:
        return 0

    tool_input = payload.get("tool_input", {})
    file_path = tool_input.get("file_path", "")
    if not file_path:
        return 0

    path = resolved_posix_path(file_path, payload.get("cwd", "."))
    match = PROMPT_RE.search(path)
    if not match:
        return 0

    content = get_effective_content(payload["tool_name"], tool_input, Path(path))

    if not SHARED_SENTENCE_RE.search(content):
        return block(
            "this edit would remove the ADR-009 duplicate-symbol sentence "
            '(anchor: "Never define the same variable or function name..."). '
            "That rule exists because duplicate-`process` regressions were a "
            "measured failure class."
        )

    if not PROCESS_ONCE_RE.search(content):
        return block(
            'this edit would remove the "process ... exactly once" clause, '
            "dropping half of the ADR-009 rule text."
        )

    if BEGIN_MARKER not in content or END_MARKER not in content:
        return block(
            "this edit would remove the generated-stdlib-block markers "
            f"({BEGIN_MARKER!r} / {END_MARKER!r}). The block is generated from the "
            "installed Faust library by tools/gen_stdlib_block.py; without the "
            "markers it cannot be regenerated and will silently rot."
        )

    # Anti-fabrication check. Requires the installed library; skip if absent.
    if not FAUST_STDLIB.exists():
        return 0

    root = path[: match.start()]
    tools_dir = Path((root + "/" if root else "") + "tools")
    sys.path.insert(0, str(tools_dir))
    try:
        from gen_stdlib_block import verify_prompt_references
    except ImportError as exc:
        raise RuntimeError(
            f"cannot verify stdlib references: {tools_dir}/gen_stdlib_block.py "
            f"not importable ({exc})"
        ) from exc

    problems = verify_prompt_references(content)
    if problems:
        return block(
            "this edit would introduce Faust functions that DO NOT EXIST in the "
            "installed standard library:\n  " + "\n  ".join(problems)
            + "\n\nThis is the exact defect found 2026-07-21. A few-shot example is "
              "the strongest signal in a prompt: teaching a fabricated name licenses "
              "the model to invent more."
        )

    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception as exc:
        print(f"BLOCKED (hook error, failing closed): {exc}", file=sys.stderr)
        sys.exit(2)
