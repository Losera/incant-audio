#!/usr/bin/env python3
"""Runtime-neutral session checks and machine-state snapshots for PluginForge."""

from __future__ import annotations

import argparse
import json
import subprocess
from datetime import datetime, timezone
from pathlib import Path


def git(root: Path, *args: str) -> str | None:
    try:
        result = subprocess.run(
            ["git", *args], cwd=root, capture_output=True, text=True, timeout=10
        )
    except (OSError, subprocess.TimeoutExpired):
        return None
    return result.stdout.rstrip("\n") if result.returncode == 0 else None


def repo_root() -> Path:
    result = subprocess.run(
        ["git", "rev-parse", "--show-toplevel"],
        capture_output=True,
        text=True,
        timeout=10,
    )
    if result.returncode != 0:
        raise SystemExit("agent_session.py: current directory is not in a Git checkout")
    return Path(result.stdout.strip()).resolve()


def state(root: Path) -> dict[str, object]:
    branch = git(root, "branch", "--show-current") or "(detached)"
    return {
        "captured_at": datetime.now(timezone.utc).isoformat(),
        "root": str(root),
        "branch": branch,
        "head": git(root, "rev-parse", "HEAD"),
        "status_porcelain": git(root, "status", "--porcelain") or "",
        "diff_stat": git(root, "diff", "--stat") or "",
    }


def check(root: Path) -> int:
    current = state(root)
    dirty = str(current["status_porcelain"]).splitlines()
    common = git(root, "rev-parse", "--git-common-dir")
    is_primary = common == ".git"

    print("### Agent session check")
    print(f"root      {current['root']}")
    print(f"branch    {current['branch']}")
    print(f"head      {str(current['head'])[:12]}")
    print(f"dirty     {len(dirty)} path(s)")
    for path in dirty[:15]:
        print(f"          {path}")
    if len(dirty) > 15:
        print(f"          ... and {len(dirty) - 15} more")

    if current["branch"] == "(detached)":
        print("WARNING   detached HEAD; create or select a task branch before editing")
    if is_primary:
        print("WARNING   primary checkout; use a dedicated worktree when sessions overlap")
    else:
        print("worktree  dedicated checkout")

    neutral = root / ".agent" / "HANDOFF.md"
    legacy = root / ".claude" / "HANDOFF.md"
    if neutral.exists():
        print("handoff   .agent/HANDOFF.md")
    elif legacy.exists():
        print("handoff   .claude/HANDOFF.md (legacy fallback)")
    else:
        print("handoff   none on disk")
    return 0


def snapshot(root: Path, trigger: str) -> int:
    current = state(root)
    current["trigger"] = trigger
    destination = root / ".agent" / "handoff-state.json"
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_text(json.dumps(current, indent=2) + "\n")
    print(destination)
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    commands.add_parser("check", help="report checkout safety and handoff state")
    snapshot_parser = commands.add_parser(
        "snapshot", help="write a machine-only pre-compaction snapshot"
    )
    snapshot_parser.add_argument("--trigger", default="manual")
    args = parser.parse_args()

    root = repo_root()
    if args.command == "check":
        return check(root)
    return snapshot(root, args.trigger)


if __name__ == "__main__":
    raise SystemExit(main())
