#!/usr/bin/env python3
"""SessionStart hook: re-injects .claude/HANDOFF.md so a context clear, a compaction,
or a resumed session sees it without being told to look. This is the guard half of
ADR-028 -- it makes a handoff that EXISTS impossible to miss. It cannot make one get
WRITTEN; nothing in Claude Code can force a skill to run. See /handoff and
handoff_precompact.py, its narrower sibling for the case nobody wrote one.

CANNOT BLOCK a session from starting -- SessionStart has no decision control, and this
hook never tries to exit nonzero for that reason. "Failing closed" here means printing
a loud, honest banner instead of silently emitting nothing, borrowing
tools/status_digest.sh's rule: silence is the one forbidden output. Every reachable
state below prints something distinguishable from every other state:

  handoff present, fresh            -> the document
  handoff present, HEAD moved / >24h old -> the document + a staleness banner
  no handoff, but a PreCompact ran   -> machine state only, marked unverified
  neither exists                    -> an explicit "NO HANDOFF ON DISK" line

NOT COVERED: whether the injected text is read. additionalContext lands as a system
reminder; nothing in-process can prove the next turn actually used it.

Also appends one line per firing to .claude/handoff-log.jsonl -- purely observational,
gitignored, never read by this hook or any control. It exists so "is this protocol
actually being used" can be answered by counting lines instead of by memory, per the
2026-08-21 decision to hold ADR-028 unpushed for a real-usage evaluation window before
landing it. Logging failures never affect the hook's return value -- see _log_event.

PLUGINFORGE_HANDOFF_LOG_PATH overrides the log destination. Exists so
tests/test_control_wiring.py can invoke this hook for real without a single pytest run
outweighing weeks of actual usage in the same file -- the measurement this log exists
to take must not be the thing polluting it.
"""
import json
import os
import subprocess
import sys
import time
from datetime import datetime, timezone
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
HANDOFF = ROOT / ".claude" / "HANDOFF.md"
STATE = ROOT / ".claude" / "handoff-state.json"
LOG = Path(os.environ["PLUGINFORGE_HANDOFF_LOG_PATH"]) if os.environ.get(
    "PLUGINFORGE_HANDOFF_LOG_PATH") else ROOT / ".claude" / "handoff-log.jsonl"

MAX_LINES = 400
STALE_AGE_HOURS = 24


def _log_event(record: dict) -> None:
    """Best-effort, append-only. Never raises -- this is telemetry, not a control,
    and a broken log must never be the reason a session fails to start."""
    try:
        record = {"ts": datetime.now(timezone.utc).isoformat(), **record}
        with LOG.open("a") as f:
            f.write(json.dumps(record) + "\n")
    except Exception:
        pass

_META_RE = __import__("re").compile(
    r"<!--\s*handoff-meta:\s*head=(\S+)\s+written=(\S+)(?:\s+branch=(\S+))?\s*-->"
)


def _git(*args) -> str | None:
    try:
        out = subprocess.run(
            ["git", *args], cwd=str(ROOT), capture_output=True, text=True, timeout=10,
        )
        return out.stdout.strip() if out.returncode == 0 else None
    except Exception:
        return None


def _handoff_context() -> tuple[str, bool]:
    """Returns (context, was_flagged_stale)."""
    text = HANDOFF.read_text()
    meta = _META_RE.search(text[:2000])
    stale_reasons = []

    if meta:
        recorded_head = meta.group(1)
        current_head = _git("rev-parse", "HEAD")
        if current_head and recorded_head and recorded_head != current_head:
            stale_reasons.append(
                f"recorded HEAD {recorded_head[:12]} != current HEAD {current_head[:12]}"
            )
    else:
        stale_reasons.append(
            "no handoff-meta comment found -- staleness cannot be checked; the "
            "writer skipped the load-bearing head= line the skill asks for"
        )

    try:
        age_hours = (time.time() - HANDOFF.stat().st_mtime) / 3600
        if age_hours > STALE_AGE_HOURS:
            stale_reasons.append(f"written {age_hours:.0f}h ago (>{STALE_AGE_HOURS}h)")
    except OSError:
        pass

    lines = text.splitlines()
    truncated = len(lines) > MAX_LINES
    if truncated:
        body = "\n".join(lines[:MAX_LINES])
        body += (
            f"\n\n...[truncated at {MAX_LINES} lines, {len(lines) - MAX_LINES} more "
            "-- read .claude/HANDOFF.md directly for the rest]"
        )
    else:
        body = text

    banner = ""
    if stale_reasons:
        banner = (
            "\n⚠ HANDOFF STALENESS WARNING: " + "; ".join(stale_reasons) + ". "
            "Treat CURRENT STATE below as a starting point to verify, not settled fact.\n"
        )

    context = "=== .claude/HANDOFF.md (previous session's handoff) ===\n" + banner + "\n" + body
    return context, bool(stale_reasons)


def _state_only_context() -> str:
    try:
        state = json.loads(STATE.read_text())
        rendered = json.dumps(state, indent=2)
    except Exception as exc:
        rendered = f"(unreadable: {exc})\n{STATE.read_text()}"
    return (
        "=== NO HANDOFF ON DISK -- a compaction ran and no handoff was written ===\n"
        "Treat everything below as raw machine state, not a summary of intent or of "
        "what changed:\n" + rendered
    )


def _nothing_context() -> str:
    return (
        "NO HANDOFF ON DISK. Expected on a genuinely fresh start. If this follows a "
        "/clear or a compaction, the previous session did not write one -- "
        "reconstruct context from `git log` and STATUS.md, or run /orient."
    )


def main() -> int:
    payload = json.load(sys.stdin)
    if payload.get("hook_event_name") not in (None, "SessionStart"):
        return 0

    if HANDOFF.exists():
        context, stale = _handoff_context()
        state = "stale" if stale else "fresh"
    elif STATE.exists():
        context = _state_only_context()
        state = "state_only"
    else:
        context = _nothing_context()
        state = "none"

    _log_event({
        "event": "SessionStart",
        "session_id": payload.get("session_id"),
        "source": payload.get("source"),
        "state": state,
    })

    print(json.dumps({
        "hookSpecificOutput": {
            "hookEventName": "SessionStart",
            "additionalContext": context,
        }
    }))
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception as exc:
        # SessionStart cannot block; the honest failure mode is a loud banner, not
        # silence and not a nonzero exit that might be read as "session denied".
        print(json.dumps({
            "hookSpecificOutput": {
                "hookEventName": "SessionStart",
                "additionalContext": (
                    f"NO HANDOFF ON DISK -- handoff_injector.py failed: {exc}"
                ),
            }
        }))
        sys.exit(0)
