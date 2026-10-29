#!/usr/bin/env python3
"""Report which local branches/worktrees hold finished work that never reached a PR.

WHY THIS EXISTS
---------------
Workflow audit, 2026-10-20. Nothing in this repo computed branch/worktree/PR state
before this: STATUS.md's "Waiting on you" carried a hand-maintained worktree-triage
list instead, and it was wrong in both directions at once -- naming eight worktrees
that no longer existed and omitting all eight that did (see
docs/sessions/021-workflow-and-human-guide.md). A session doing real work had no
way to see, at a glance, that finished work was sitting unpushed.

The other finding this script exists to make impossible to repeat: STATUS.md once
froze a branch from both deletion and merge because a raw `git diff A..B` read as a
391-line regression in docs/decisions.md. That was a two-dot diff against a `main`
that had moved -- it shows main's OWN later additions as if B deleted them. This
script always uses three-dot (`A...B`, the merge-base diff) for "true contribution",
and never prints a two-dot number at all, so that misreading cannot happen again
through this tool.

WHAT THIS IS NOT
----------------
Not a CI gate. It makes no assertions and never exits non-zero on repo content it
finds (only on its own errors, or when asked to --fetch and the network is down).
Wiring a zero-assertion lane into tools/check.sh would report it as healthy -- the
UiDesignGallery precedent (see tools/kg.py, which states the same principle for
the same reason). It also pushes nothing and opens no PRs: COLLABORATION.md §2's
rule is "gate the write, never the run" -- this is a read.

USAGE
-----
    python tools/integration_report.py              # table, to stdout
    python tools/integration_report.py --json        # machine-readable
    python tools/integration_report.py --fetch       # refresh remote-tracking refs
                                                      # first (network; off by default
                                                      # so this stays a $0, offline,
                                                      # always-safe-to-run report)

Consumed by tools/status_digest.sh via subprocess, so `/orient` surfaces this at
session start instead of requiring a separate command.

TEST SEAMS
----------
PLUGINFORGE_PR_LIST_JSON  -- substitutes for `gh pr list --state all --json ...`,
mirroring tools/status_digest.sh's PLUGINFORGE_CI_RUNS_JSON. Production never sets it.
All other git state is read directly off the repo via `git -C <worktree>`, which a
test drives by constructing a real throwaway repo (see tests/test_integration_report.py)
rather than faking -- there is no seam for it because worktree/branch geometry is
exactly the thing under test.
"""
from __future__ import annotations

import argparse
import json
import os
import shutil
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def _git(root: Path, *args: str, timeout: float = 10) -> str | None:
    """Run `git -C root <args>`, returning stdout or None on any failure."""
    try:
        p = subprocess.run(
            ["git", "-C", str(root), *args],
            capture_output=True, text=True, timeout=timeout,
        )
    except Exception:
        return None
    return p.stdout.strip() if p.returncode == 0 else None


def parse_worktree_porcelain(text: str) -> list[dict]:
    """Pure parser: `git worktree list --porcelain` text -> [{path, head, branch}].

    `branch` is None for a detached HEAD. The primary checkout is included like
    any other entry -- callers decide what to do with it (build_report excludes
    it when its branch is `main` itself, since there is nothing to compare `main`
    against). Split out from list_worktrees() so the parsing logic is testable
    without a real git repo.
    """
    entries: list[dict] = []
    cur: dict = {}
    for line in text.splitlines():
        if line.startswith("worktree "):
            if cur:
                entries.append(cur)
            cur = {"path": line[len("worktree "):].strip(), "head": None, "branch": None}
        elif line.startswith("HEAD "):
            cur["head"] = line[len("HEAD "):].strip()
        elif line.startswith("branch "):
            ref = line[len("branch "):].strip()
            cur["branch"] = ref[len("refs/heads/"):] if ref.startswith("refs/heads/") else ref
    if cur:
        entries.append(cur)
    return entries


def list_worktrees(root: Path = ROOT) -> list[dict]:
    """`git worktree list --porcelain` against a real repo, parsed."""
    out = _git(root, "worktree", "list", "--porcelain")
    if out is None:
        return []
    return parse_worktree_porcelain(out)


def list_local_branches_without_worktree(root: Path = ROOT, worktrees: list[dict] | None = None) -> list[str]:
    """Local branches that exist but are not checked out in any worktree."""
    out = _git(root, "for-each-ref", "--format=%(refname:short)", "refs/heads")
    if out is None:
        return []
    all_branches = [b for b in out.splitlines() if b.strip()]
    if worktrees is None:
        worktrees = list_worktrees(root)
    checked_out = {w["branch"] for w in worktrees if w.get("branch")}
    return [b for b in all_branches if b not in checked_out]


def remote_branches(root: Path = ROOT, fetch: bool = False) -> set[str]:
    """Remote-tracking branch names under origin, from LOCAL refs by default.

    Deliberately does not fetch unless `fetch=True` -- this keeps the report a
    $0, offline, always-safe-to-run instrument. It can therefore be stale by
    however long it has been since the last `git fetch`; pass --fetch (or
    fetch=True) to refresh first. Staleness here means "a branch pushed by
    someone else in the last few minutes might not show as remote yet", not
    "the numbers are wrong" -- local git state (diffstat, dirty, behind) is
    always live regardless of this flag.
    """
    if fetch:
        subprocess.run(["git", "-C", str(root), "fetch", "origin", "--quiet"],
                        capture_output=True, timeout=30)
    out = _git(root, "for-each-ref", "--format=%(refname:short)", "refs/remotes/origin")
    if out is None:
        return set()
    names = set()
    for line in out.splitlines():
        line = line.strip()
        if line and line != "origin" and line != "origin/HEAD":
            names.add(line[len("origin/"):])
    return names


def pr_list(root: Path = ROOT) -> list[dict] | None:
    """[{number, headRefName, state, url}, ...] via `gh pr list --state all`, or
    None if `gh` is unavailable / the call fails -- callers must treat that as
    'unknown', never as 'no PRs exist', per the project's own 'unknown is loud,
    not silently absent' rule (tools/status_digest.sh's CI section)."""
    seam = os.environ.get("PLUGINFORGE_PR_LIST_JSON")
    if seam is not None:
        raw = seam
    else:
        if shutil.which("gh") is None:
            return None
        try:
            p = subprocess.run(
                ["gh", "pr", "list", "--state", "all", "--limit", "200",
                 "--json", "number,headRefName,state,url"],
                capture_output=True, text=True, timeout=20,
            )
        except Exception:
            return None
        if p.returncode != 0:
            return None
        raw = p.stdout
    try:
        data = json.loads(raw)
        if not isinstance(data, list):
            return None
        return data
    except Exception:
        return None


def parse_numstat(text: str) -> dict:
    """Pure parser: `git diff --numstat` text -> {files, insertions, deletions}.
    A binary file's `-\t-\tpath` counts as a touched file but contributes no
    line counts."""
    files = adds = dels = 0
    for line in text.splitlines():
        if not line.strip():
            continue
        parts = line.split("\t")
        if len(parts) != 3:
            continue
        files += 1
        a, d, _ = parts
        if a != "-":
            adds += int(a)
        if d != "-":
            dels += int(d)
    return {"files": files, "insertions": adds, "deletions": dels}


def _diffstat(root: Path, base: str, head: str) -> dict | None:
    """Three-dot (`base...head`) numstat totals -- the merge-base diff, which
    reports what `head` actually contributes, not what changed on `main` since
    the branch split. See module docstring for why two-dot is never used here."""
    out = _git(root, "diff", "--numstat", f"{base}...{head}")
    if out is None:
        return None
    return parse_numstat(out)


def _age_str(epoch_s: int | None, now: datetime | None = None) -> str:
    if epoch_s is None:
        return "?"
    now = now or datetime.now(timezone.utc)
    then = datetime.fromtimestamp(epoch_s, tz=timezone.utc)
    days = (now - then).days
    if days <= 0:
        return "today"
    if days == 1:
        return "1d"
    return f"{days}d"


def collect_branch_state(
    worktree_path: str | None, branch: str, root: Path = ROOT,
    base: str = "origin/main", now: datetime | None = None,
) -> dict:
    """Everything the report prints about one branch.

    `worktree_path` is None for a local branch with no checked-out worktree --
    diffstat/behind-count still work against the bare ref; dirty-path count is
    reported as "n/a" since there is no working tree to be dirty.
    """
    git_root = Path(worktree_path) if worktree_path else root
    ref = "HEAD" if worktree_path else branch

    head = _git(git_root, "rev-parse", ref)
    diff = _diffstat(git_root, base, ref)
    behind_out = _git(git_root, "rev-list", "--count", f"{ref}..{base}")
    behind = int(behind_out) if behind_out and behind_out.isdigit() else None

    if worktree_path:
        dirty_out = _git(git_root, "status", "--porcelain")
        dirty = len([ln for ln in (dirty_out or "").splitlines() if ln.strip()]) if dirty_out is not None else None
    else:
        dirty = None  # no working tree to be dirty

    commit_epoch = _git(git_root, "log", "-1", "--format=%ct", ref)

    return {
        "branch": branch,
        "worktree": worktree_path,
        "head": head,
        "contribution": diff,       # three-dot vs base; see module docstring
        "behind_main": behind,
        "dirty_paths": dirty,
        "age": _age_str(int(commit_epoch) if commit_epoch and commit_epoch.lstrip("-").isdigit() else None, now),
    }


def build_report(root: Path = ROOT, fetch: bool = False, base: str = "origin/main") -> dict:
    worktrees = list_worktrees(root)
    primary_branch = None
    rows: list[dict] = []

    for w in worktrees:
        branch = w.get("branch")
        if branch is None:
            continue  # detached HEAD worktree -- nothing to report against a branch
        if branch == "main":
            primary_branch = "main"
            continue  # nothing to compare main against itself
        row = collect_branch_state(w["path"], branch, root=root, base=base)
        rows.append(row)

    for branch in list_local_branches_without_worktree(root, worktrees):
        if branch == "main":
            continue
        row = collect_branch_state(None, branch, root=root, base=base)
        rows.append(row)

    remotes = remote_branches(root, fetch=fetch)
    prs = pr_list(root)  # None means "could not determine", not "no PRs"

    pr_by_branch: dict[str, list[dict]] = {}
    if prs is not None:
        for pr in prs:
            pr_by_branch.setdefault(pr.get("headRefName", ""), []).append(pr)

    for row in rows:
        row["has_remote"] = row["branch"] in remotes
        if prs is None:
            row["pr"] = None  # unknown -- gh unavailable; never reported as "no PR"
        else:
            matches = pr_by_branch.get(row["branch"], [])
            open_ = [p for p in matches if p.get("state") == "OPEN"]
            chosen = open_[0] if open_ else (matches[0] if matches else None)
            row["pr"] = chosen

    rows.sort(key=lambda r: (
        0 if (r["contribution"] is None) else -(r["contribution"]["insertions"] + r["contribution"]["deletions"]),
    ))

    return {
        "primary_branch_is_main": primary_branch == "main",
        "pr_lookup_available": prs is not None,
        "rows": rows,
    }


def _fmt_contribution(c: dict | None) -> str:
    if c is None:
        return "?"
    return f"+{c['insertions']}/-{c['deletions']} ({c['files']}f)"


def format_report(data: dict) -> str:
    rows = data["rows"]
    if not rows:
        return "No non-main branches or worktrees found -- nothing stranded."

    lines = []
    lines.append(f"{'branch':<42} {'contribution (three-dot)':<28} {'behind':>6} "
                  f"{'dirty':>5} {'remote':>7} {'PR':<22} age")
    lines.append("-" * 130)
    for r in rows:
        behind = "?" if r["behind_main"] is None else str(r["behind_main"])
        dirty = "n/a" if r["dirty_paths"] is None else str(r["dirty_paths"])
        remote = "yes" if r["has_remote"] else "NO"
        if not data["pr_lookup_available"]:
            pr_s = "unknown (gh unavailable)"
        elif r.get("pr") is None:
            pr_s = "none" if r["has_remote"] else "none, no remote"
        else:
            pr_s = f"#{r['pr']['number']} {r['pr']['state']}"
        lines.append(
            f"{r['branch']:<42} {_fmt_contribution(r['contribution']):<28} "
            f"{behind:>6} {dirty:>5} {remote:>7} {pr_s:<22} {r['age']}"
        )

    ready = [r for r in rows if r["dirty_paths"] == 0 and r["behind_main"] == 0
             and data["pr_lookup_available"] and r.get("pr") is None]
    if ready:
        lines.append("")
        lines.append("Clean, 0-behind, NO PR -- finished work not yet opened as a PR:")
        for r in ready:
            lines.append(f"  - {r['branch']}  ({_fmt_contribution(r['contribution'])})")

    if not data["pr_lookup_available"]:
        lines.append("")
        lines.append("NOTE: `gh` was unavailable -- PR column is unknown for every row, "
                      "not necessarily 'none'. Do not read blanks here as 'no PR exists'.")

    return "\n".join(lines)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--json", action="store_true", help="machine-readable output")
    ap.add_argument("--fetch", action="store_true",
                     help="refresh remote-tracking refs first (network)")
    args = ap.parse_args()

    data = build_report(ROOT, fetch=args.fetch)
    if args.json:
        print(json.dumps(data, indent=2))
    else:
        print(format_report(data))
    return 0


if __name__ == "__main__":
    sys.exit(main())
