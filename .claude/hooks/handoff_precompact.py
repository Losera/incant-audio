#!/usr/bin/env python3
"""PreCompact hook: records machine state to .claude/handoff-state.json before a
compaction (auto or manual) discards the session's context. This is the safety net
for the case /handoff's skill did not fire in time -- see ADR-028.

DELIBERATELY NEVER TOUCHES .claude/HANDOFF.md. This hook is a shell command with no
access to the agent's reasoning: it can capture branch, HEAD, dirty-tree status and a
diffstat, but it cannot write OBJECTIVE, COMPLETED WORK, FAILED APPROACHES or any of
the other prose fields a real handoff needs. A machine-generated stub silently
overwriting a real handoff would be a strict downgrade -- so this hook writes only to
handoff-state.json, a separate file, every time, and handoff_injector.py is the one
that tells the next session state-only means "reconstruct this, don't trust it as a
summary."

CANNOT BLOCK compaction -- PreCompact has no decision control, and this hook always
exits 0 regardless of what happens internally; a failure here must never be the reason
compaction stalls. On any error it still tries to leave a note on stderr, then exits 0.

NOT COVERED: whether Claude Code actually dispatches this hook at runtime, and whether
handoff-state.json's snapshot is read before the next git operation changes it further.
"""
import json
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
STATE = ROOT / ".claude" / "handoff-state.json"


def _git(*args) -> str | None:
    try:
        out = subprocess.run(
            ["git", *args], cwd=str(ROOT), capture_output=True, text=True, timeout=10,
        )
        return out.stdout.strip() if out.returncode == 0 else None
    except Exception:
        return None


def main() -> int:
    payload = json.load(sys.stdin)
    if payload.get("hook_event_name") not in (None, "PreCompact"):
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
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception as exc:
        # Cannot block compaction and must not try to. This is cleanup, not a gate.
        print(f"handoff_precompact.py failed (non-blocking): {exc}", file=sys.stderr)
        sys.exit(0)
