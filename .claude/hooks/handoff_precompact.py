#!/usr/bin/env python3
"""Session-state capture hook: records machine state to .claude/handoff-state.json
before the session's context goes away -- on compaction (auto or manual) and at session
end. This is the safety net for the case /handoff's skill did not fire in time -- ADR-028.

WHY MORE THAN PreCompact (2026-10-28): for the first 66 days this hook was wired to
PreCompact alone, and PreCompact fired exactly ONCE in that window (73 SessionStart
events against 1 PreCompact in handoff-log.jsonl) because sessions here run long and
rarely compact. handoff-state.json sat frozen at 2026-08-26 pointing at
feat/dynamic-stdlib-retrieval, a branch that no longer exists, while handoff_injector.py
correctly reported "stale" on 61 of 73 session starts. The capture was bound to an event
that effectively does not occur. Stop and SessionEnd do occur, so they are accepted too.

The event filter below is what makes that work: adding this script to a Stop hook WITHOUT
widening the filter would exit 0 and capture nothing -- the same wired-but-not-firing
failure, reintroduced silently. The logged `event` field records the real hook event, not
a hardcoded "PreCompact", so handoff-log.jsonl stays usable as evidence of what fired.

DELIBERATELY NEVER TOUCHES .claude/HANDOFF.md. This hook is a shell command with no
access to the agent's reasoning: it can capture branch, HEAD, dirty-tree status and a
diffstat, but it cannot write OBJECTIVE, COMPLETED WORK, FAILED APPROACHES or any of
the other prose fields a real handoff needs. A machine-generated stub silently
overwriting a real handoff would be a strict downgrade -- so this hook writes only to
handoff-state.json, a separate file, every time, and handoff_injector.py is the one
that tells the next session state-only means "reconstruct this, don't trust it as a
summary."

CANNOT BLOCK anything -- PreCompact has no decision control, and this hook always exits 0
regardless of what happens internally; a failure here must never be the reason compaction
or session teardown stalls. On any error it still tries to leave a note on stderr, then
exits 0.

NOT COVERED: whether Claude Code actually dispatches this hook at runtime, and whether
handoff-state.json's snapshot is read before the next git operation changes it further.
Verify dispatch the only way that proves it -- that handoff-log.jsonl gains a Stop or
SessionEnd row from a real session. The event names are confirmed present in the installed
binary's hook enum (2.1.241), which proves they are accepted config, not that they fire.

Also appends one line per firing to .claude/handoff-log.jsonl -- purely observational,
gitignored, never read back by either hook. See handoff_injector.py's docstring for why.

PLUGINFORGE_HANDOFF_LOG_PATH overrides the log destination -- same reason as in
handoff_injector.py: a test run must not be the thing polluting the real-usage count.
"""
import json
import os
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
STATE = ROOT / ".claude" / "handoff-state.json"
LOG = Path(os.environ["PLUGINFORGE_HANDOFF_LOG_PATH"]) if os.environ.get(
    "PLUGINFORGE_HANDOFF_LOG_PATH") else ROOT / ".claude" / "handoff-log.jsonl"


def _log_event(record: dict) -> None:
    """Best-effort, append-only. Never raises -- telemetry, not a control."""
    try:
        record = {"ts": datetime.now(timezone.utc).isoformat(), **record}
        with LOG.open("a") as f:
            f.write(json.dumps(record) + "\n")
    except Exception:
        pass


def _git(*args) -> str | None:
    try:
        out = subprocess.run(
            ["git", *args], cwd=str(ROOT), capture_output=True, text=True, timeout=10,
        )
        return out.stdout.strip() if out.returncode == 0 else None
    except Exception:
        return None


CAPTURE_EVENTS = (None, "PreCompact", "Stop", "SessionEnd")


def main() -> int:
    payload = json.load(sys.stdin)
    event = payload.get("hook_event_name")
    if event not in CAPTURE_EVENTS:
        return 0

    state = {
        "captured_at": datetime.now(timezone.utc).isoformat(),
        "trigger": payload.get("trigger", "unknown"),
        "session_id": payload.get("session_id"),
        "branch": _git("rev-parse", "--abbrev-ref", "HEAD"),
        "head": _git("rev-parse", "HEAD"),
        "status_porcelain": _git("status", "--porcelain"),
        "diff_stat": _git("diff", "--stat"),
    }

    STATE.parent.mkdir(parents=True, exist_ok=True)
    STATE.write_text(json.dumps(state, indent=2) + "\n")

    _log_event({
        "event": event or "PreCompact",
        "session_id": payload.get("session_id"),
        "trigger": state["trigger"],
    })
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception as exc:
        # Cannot block compaction and must not try to. This is cleanup, not a gate.
        print(f"handoff_precompact.py failed (non-blocking): {exc}", file=sys.stderr)
        sys.exit(0)
