#!/usr/bin/env python3
"""PreToolUse hook (Bash): blocks shell-command patterns forbidden by CLAUDE.md's
"Do not" list. Fails closed (exit 2) on any unexpected error so a bug here can
never silently degrade into an allow.

Add future Bash-checkable CLAUDE.md rules to DENYLIST below — each entry is
(compiled regex, human-readable reason). Do not touch the scan logic to add a
rule; only add list entries (this is the contract invariant-hook-writer relies on).
"""
import json
import re
import sys

_GIT_SCOPE_REASON = (
    "This repository is edited by the human and by Claude AT THE SAME TIME, sharing "
    "one working tree, one index, and one HEAD. On 2026-07-21 a human-staged deletion "
    "of bench/prompts/system_faust.txt was sitting in the index while Claude was "
    "committing unrelated C++ work; a bare `git add -A` would have swept it into "
    "Claude's commit. Whole-tree git commands cannot tell whose work they are moving.\n"
    "Use explicit paths instead:\n"
    "  git add path/one path/two\n"
    "  git commit -F <msgfile> --only path/one path/two\n"
    "If you genuinely need a whole-tree operation, stop and ask the human — they may "
    "have uncommitted work you cannot see."
)

DENYLIST = [
    (
        re.compile(r"sudo\b[^&;|\n]*\bnpm\s+install\b"),
        'CLAUDE.md "Do not" list forbids `sudo npm install`. Fix directory '
        "permissions and run npm install without sudo, or ask the human directly "
        "if elevation is genuinely required.",
    ),
    # ── Shared-working-tree safety (COLLABORATION.md §11) ───────────────────
    # These block WHOLE-TREE git operations only. Every path-scoped form stays
    # allowed, which is the whole point: name what you are touching.
    (
        re.compile(r"\bgit\s+add\s+(-A\b|--all\b|-u\b|\.(\s|$))"),
        "`git add` over the whole tree stages the human's in-flight work too.\n"
        + _GIT_SCOPE_REASON,
    ),
    (
        re.compile(r"\bgit\s+commit\b[^\n]*\s-[a-zA-Z]*a[a-zA-Z]*(\s|$)"),
        "`git commit -a` commits every modified tracked file, including the "
        "human's.\n" + _GIT_SCOPE_REASON,
    ),
    (
        re.compile(r"\bgit\s+commit\b[^\n]*\s--all\b"),
        "`git commit --all` commits every modified tracked file, including the "
        "human's.\n" + _GIT_SCOPE_REASON,
    ),
    (
        # `git stash list` / `show` are read-only and are how you discover that
        # someone else stashed something; only the mutating forms are blocked.
        re.compile(r"\bgit\s+stash\b(?!\s+(list|show)\b)"),
        "`git stash` moves work out from under whoever else is editing. Claude ran "
        "`git stash push -- host/` on 2026-07-21 to A/B a test result; had the human "
        "been mid-edit in host/, their work would have vanished with no warning.\n"
        "To compare against committed state, use `git show HEAD:<path>`, "
        "`git diff`, or a scratch copy outside the repo.",
    ),
    (
        re.compile(r"\bgit\s+reset\s+--hard\b"),
        "`git reset --hard` destroys uncommitted work across the whole tree, "
        "including the human's.\n" + _GIT_SCOPE_REASON,
    ),
    (
        re.compile(r"\bgit\s+(checkout|restore)\s+(--\s+)?\.(\s|$)"),
        "Restoring the whole tree discards the human's uncommitted edits.\n"
        + _GIT_SCOPE_REASON,
    ),
    (
        re.compile(r"\bgit\s+clean\b[^\n]*\s-[a-zA-Z]*f"),
        "`git clean -f` deletes untracked files — which is where the human's "
        "new, not-yet-added work lives (STATUS.md, new hooks, new tools were all "
        "untracked on 2026-07-21).\n" + _GIT_SCOPE_REASON,
    ),
]

SEGMENT_SPLIT = re.compile(r"&&|\|\||[;|\n]")


def main() -> int:
    payload = json.load(sys.stdin)
    if payload.get("tool_name") != "Bash":
        return 0

    command = payload.get("tool_input", {}).get("command", "")
    if not isinstance(command, str) or not command:
        return 0

    for segment in SEGMENT_SPLIT.split(command):
        for pattern, reason in DENYLIST:
            if pattern.search(segment):
                print(f"BLOCKED (CLAUDE.md Do-Not list): {reason}", file=sys.stderr)
                return 2

    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception as exc:
        print(f"BLOCKED (hook error, failing closed): {exc}", file=sys.stderr)
        sys.exit(2)
