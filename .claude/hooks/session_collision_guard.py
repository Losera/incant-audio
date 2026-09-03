#!/usr/bin/env python3
"""SessionStart hook: warn when this session is starting in a checkout another
session may already be working in -- so two agents don't fight over one working
tree, one git index, and one .claude/HANDOFF.md.

AGENTS.md 6 ("Use a separate Git worktree when multiple agents work concurrently").
This hook does not enforce that -- SessionStart has no decision control and this
script never exits nonzero to block. Like handoff_injector.py, "teeth" here means
an honest banner in EVERY reachable state, never silence (tools/status_digest.sh's
rule). The states, any of which may combine:

  clean, in a dedicated worktree      -> a single OK line naming the worktree
  clean, in the shared primary        -> OK + a one-line worktree reminder
  another transcript recently written -> CONCURRENT SESSION block
  working tree already dirty          -> SHARED INDEX block
  HANDOFF.md's branch != current      -> FOREIGN HANDOFF block

Every check is cheap (git + stat, well under a second) and best-effort: a check
that raises is reported as "could not verify <x>", never crashes the session.
Nothing here writes any file.

NOT COVERED: an IDLE concurrent session (no recent transcript write) is invisible
to the CONCURRENT SESSION check, and a crashed one can linger as a false positive
for RECENT_MINUTES. The check says so in its own output rather than implying
certainty.
"""
import json
import os
import re
import subprocess
import sys
import time
from pathlib import Path

# PLUGINFORGE_SCG_ROOT overrides the checkout this guard inspects. Exists so
# tests/test_control_wiring.py can point the branch/handoff checks at a
# throwaway repo with a known state, rather than depending on whatever branch
# and HANDOFF.md the real tree happens to carry (which would make the red case
# skip on a detached-HEAD CI checkout). Same "a path seam for tests only"
# precedent as handoff_injector.py's PLUGINFORGE_HANDOFF_LOG_PATH.
ROOT = Path(os.environ["PLUGINFORGE_SCG_ROOT"]).resolve() if os.environ.get(
    "PLUGINFORGE_SCG_ROOT") else Path(__file__).resolve().parents[2]
HANDOFF = ROOT / ".claude" / "HANDOFF.md"

RECENT_MINUTES = 8          # a transcript touched more recently than this = "maybe live"
MAX_LISTED_PATHS = 15       # cap the dirty-file list so the banner stays readable

_META_RE = re.compile(
    r"<!--\s*handoff-meta:\s*head=(\S+)\s+written=(\S+)(?:\s+branch=(\S+))?\s*-->"
)


def _git(*args, raw=False):
    """stdout on success, None on any failure. Never raises. raw=True keeps the
    output verbatim -- porcelain status needs its leading XY columns, which a
    .strip() on the first line would eat."""
    try:
        out = subprocess.run(
            ["git", *args], cwd=str(ROOT), capture_output=True, text=True, timeout=10
        )
        if out.returncode != 0:
            return None
        return out.stdout if raw else out.stdout.strip()
    except Exception:
        return None


def _current_branch():
    b = _git("rev-parse", "--abbrev-ref", "HEAD")
    return b if b and b != "HEAD" else None      # None = detached or unknown


# ---------------------------------------------------------------------------
# check 1 -- FOREIGN HANDOFF: the handoff on disk describes a different branch
# ---------------------------------------------------------------------------
def _check_foreign_handoff(branch):
    if not HANDOFF.exists():
        return None
    try:
        head = HANDOFF.read_text()[:2000]
    except OSError as exc:
        return f"could not verify the handoff branch: {exc}"

    meta = _META_RE.search(head)
    if not meta:
        return None                              # handoff_injector.py owns the no-meta case
    recorded_branch = meta.group(3)
    if not recorded_branch or not branch or recorded_branch == branch:
        return None

    return (
        "FOREIGN HANDOFF\n"
        f"  .claude/HANDOFF.md describes work on branch '{recorded_branch}', "
        f"you are on '{branch}'.\n"
        "  It is almost certainly another effort's handoff. /handoff REPLACES that\n"
        "  one file -- writing yours drops whatever it still owes. Before running\n"
        "  /handoff: copy it aside, or move its owed items into STATUS.md's\n"
        "  'Waiting on you' / 'Next three'."
    )


# ---------------------------------------------------------------------------
# check 2 -- SHARED INDEX: the tree is already dirty before this session acted
# ---------------------------------------------------------------------------
def _check_shared_index(source):
    lock = ROOT / ".git" / "index.lock"
    porcelain = _git("status", "--porcelain", raw=True)
    if porcelain is None:
        return "could not verify working-tree state (git status failed)"

    lines = [ln for ln in porcelain.splitlines() if ln.strip()]
    tracked = [ln for ln in lines if not ln.startswith("??")]
    if not tracked and not lock.exists():
        return None

    parts = []
    if lock.exists():
        parts.append(
            "  .git/index.lock is present -- a git operation is running in this\n"
            "  checkout RIGHT NOW (another session mid-commit?)."
        )
    if tracked:
        staged = [ln for ln in tracked if ln[0] not in " ?"]
        shown = tracked[:MAX_LISTED_PATHS]
        more = len(tracked) - len(shown)
        listing = "\n".join("    " + ln for ln in shown)
        if more > 0:
            listing += f"\n    ... {more} more"
        note = ""
        if source in ("resume", "compact"):
            note = ("  (source={}: some of this may be THIS session's own work from "
                    "before the break.)\n".format(source))
        parts.append(
            f"  {len(tracked)} tracked file(s) already modified"
            + (f", {len(staged)} of them staged" if staged else "")
            + " before this session did anything:\n"
            + listing + "\n"
            + note
            + "  If another session owns these you share one index: commit with\n"
            + "  EXPLICIT paths only (never `git add -A` / `git commit -a`), and\n"
            + "  consider `git worktree add .worktrees/<name> -b <branch>`."
        )
    return "SHARED INDEX\n" + "\n".join(parts)


# ---------------------------------------------------------------------------
# check 3 -- CONCURRENT SESSION: another transcript in this project was just written
# ---------------------------------------------------------------------------
def _project_transcript_dir(payload):
    tp = payload.get("transcript_path")
    if tp:
        p = Path(tp).expanduser()
        if p.parent.is_dir():
            return p.parent
    cwd = payload.get("cwd") or str(ROOT)
    slug = cwd.replace("/", "-").replace(".", "-")
    guess = Path.home() / ".claude" / "projects" / slug
    return guess if guess.is_dir() else None


def _check_concurrent_session(payload):
    tdir = _project_transcript_dir(payload)
    if tdir is None:
        return "could not verify concurrent sessions (no transcript dir found)"

    own = payload.get("session_id")
    own_path = payload.get("transcript_path")
    cutoff = time.time() - RECENT_MINUTES * 60
    hits = []
    try:
        for jf in tdir.glob("*.jsonl"):
            if own and jf.stem == own:
                continue
            if own_path and str(jf) == str(Path(own_path).expanduser()):
                continue
            try:
                mtime = jf.stat().st_mtime
            except OSError:
                continue
            if mtime >= cutoff:
                hits.append((jf.stem, (time.time() - mtime) / 60))
    except OSError as exc:
        return f"could not verify concurrent sessions: {exc}"

    if not hits:
        return None

    hits.sort(key=lambda h: h[1])
    freshest_id, freshest_age = hits[0]
    return (
        "CONCURRENT SESSION\n"
        f"  {len(hits)} other transcript(s) in this project were written in the last "
        f"{RECENT_MINUTES} min\n"
        f"  (freshest: {freshest_id[:8]}, {freshest_age:.1f} min ago). Another agent\n"
        "  session is probably live in this same checkout. Give this session its\n"
        "  own worktree before doing substantive work:\n"
        "    git worktree add .worktrees/<name> -b <branch> && cd .worktrees/<name>\n"
        "  (mtime-based: an idle session won't show here; a crashed one may linger.)"
    )


# ---------------------------------------------------------------------------
# worktree context -- always one line, even on the all-clear path
# ---------------------------------------------------------------------------
def _worktree_line():
    listing = _git("worktree", "list", "--porcelain")
    if listing is None:
        return "worktree layout: could not determine"
    roots = [ln[len("worktree "):] for ln in listing.splitlines()
             if ln.startswith("worktree ")]
    here = str(ROOT)
    in_dedicated = "/.worktrees/" in here or "/worktrees/" in here
    n_other = max(0, len(roots) - 1)
    if in_dedicated:
        return f"running in a dedicated worktree ({Path(here).name}); {n_other} other(s) exist"
    if n_other:
        return (f"this is the SHARED primary checkout and {n_other} worktree(s) exist -- "
                "concurrent efforts here should each `git worktree add` their own")
    return "single checkout, no other worktrees"


# ---------------------------------------------------------------------------
def _build_context(payload):
    source = payload.get("source")
    branch = _current_branch()

    blocks = [b for b in (
        _check_foreign_handoff(branch),
        _check_shared_index(source),
        _check_concurrent_session(payload),
    ) if b]

    wt = _worktree_line()
    warnings = [b for b in blocks if not b.startswith("could not verify")]
    soft = [b for b in blocks if b.startswith("could not verify")]

    if warnings:
        header = "SESSION COLLISION GUARD -- possible collision with another session:"
        body = "\n\n".join(warnings)
        tail = f"\nworktree: {wt}"
        if soft:
            tail += "\n" + "\n".join(soft)
        return f"=== {header} ===\n\n{body}\n{tail}"

    ok = f"=== session collision guard: no concurrent-session signals ===\nworktree: {wt}"
    if soft:
        ok += "\n" + "\n".join(soft)
    return ok


def main():
    payload = json.load(sys.stdin)
    if payload.get("hook_event_name") not in (None, "SessionStart"):
        return 0
    print(json.dumps({
        "hookSpecificOutput": {
            "hookEventName": "SessionStart",
            "additionalContext": _build_context(payload),
        }
    }))
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception as exc:
        # SessionStart cannot block; the honest failure mode is a loud line, not
        # silence and not a nonzero exit that might read as "session denied".
        print(json.dumps({
            "hookSpecificOutput": {
                "hookEventName": "SessionStart",
                "additionalContext": (
                    f"session collision guard failed to run ({exc}); "
                    "check for concurrent sessions manually before committing."
                ),
            }
        }))
        sys.exit(0)
