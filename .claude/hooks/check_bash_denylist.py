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

DENYLIST = [
    (
        re.compile(r"sudo\b[^&;|\n]*\bnpm\s+install\b"),
        'CLAUDE.md "Do not" list forbids `sudo npm install`. Fix directory '
        "permissions and run npm install without sudo, or ask the human directly "
        "if elevation is genuinely required.",
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
