---
name: pr-cleanup
description: Audit every open PR for real CI status (not local-green assumptions), diagnose failures, fix and push corrections to the right branch, and report a per-PR status table. Never merges — merging is a human semantic-diff review (AGENTS.md §7). Trigger on "clean up the PRs", "review open PRs", "PR cleanup", "audit the PRs", "/pr-cleanup".
---

# PR cleanup

A PR that is "pushed" is not "done." CLAUDE.md's own rule: *"done means pushed and
green, not committed."* Green locally is not evidence of green on CI — this project has
already been bitten by that gap twice (PF-027's SIGILL, and PR #105's `faust`-in-CI
failure found 2026-10-20). This skill treats `gh pr checks` as ground truth and local
`tools/check.sh` runs as a cheap pre-check, never a substitute.

**What this skill does:** reviews every open PR's real state (CI, mergeability, scope,
staleness) and fixes what's fixable by pushing corrective commits to the PR's own
branch.
**What it never does:** merge a PR, force-push, rewrite history, or close/delete a PR
without the human's say — all consult-trigger territory (AGENTS.md §4, §6, §7).

## Step 1 — Inventory

```bash
gh pr list --state open --json number,title,headRefName,isDraft,mergeable,updatedAt
```

For each PR, in parallel where independent:

```bash
gh pr checks <n>                                   # the real signal
gh pr view <n> --json mergeable,mergeStateStatus,baseRefName
```

Classify into one bucket per PR — do this before touching any code:

| Bucket | Meaning |
|---|---|
| **GREEN** | All checks pass, mergeable clean. Ready for human review, nothing to do. |
| **PENDING** | Checks still running. Re-poll later; don't act on a guess. |
| **RED** | A check failed. Diagnose before touching anything (Step 2). |
| **NO CHECKS** | CI never triggered. Investigate why before assuming it's fine (Step 3). |
| **STALE/CONFLICT** | `mergeable: CONFLICTING` or far behind base. Needs a rebase/merge from base (Step 4). |

Report this table to the human before fixing anything — a RED or NO-CHECKS PR might be
someone else's known, accepted state, not a defect for you to chase.

## Step 2 — Diagnose a RED check

Never guess from the PR diff alone — pull the actual failure:

```bash
gh run list --branch <branch> --limit 3
gh run view <run-id> --job <job-id> --log-failed
```

Common classes seen in this repo already:
- **Environment mismatch.** CI's `test` job runs "no API key or faust compiler
  required" — literally no `faust` binary. Code that calls the real `faust` CLI
  unconditionally (as opposed to only when a DSP/build step explicitly needs it) will
  pass on any dev machine with Faust installed and fail on CI. Check what the failing
  job's own name/comment claims about its environment before assuming the code is wrong.
- **A stale generated artifact.** `tools/gen_stdlib_block.py --write`,
  `tools/gen_presentation_prompt.py --write` — anything with a `--check`/`--write` pair
  can drift from its source and fail a staleness assertion.
- **A genuine regression** introduced by the PR's own diff.

State which class it is, with the actual log line, before writing a fix — "the test
failed" is not a diagnosis.

## Step 3 — No CI triggered

Confirm, don't assume:
```bash
gh run list --branch <branch> --limit 5
```
If truly empty, that's a workflow-trigger gap (e.g. the branch name doesn't match a
`on: push: branches` filter, or the PR was opened from an already-pushed branch and
GitHub didn't re-queue a run). Report it as a finding — do not silently re-push just to
force a run without understanding why it didn't fire the first time, and do not treat
"no checks" as equivalent to "green."

## Step 4 — Fix and push

1. `git worktree list` / `cd` to the PR's actual worktree — never mix two branches'
   fixes in one commit.
2. Fix the diagnosed cause. If it's a design tension (e.g. "should CI install `faust`,
   or should these tests skip/mock without it?") rather than a one-line bug, that is a
   build-system/CI-config decision — AGENTS.md §4 consult territory. Propose the
   options, do not pick one unilaterally and land it.
3. Re-verify **at the right fidelity**: re-run the specific failing test locally, and if
   the failure was environment-shaped (Step 2's first class), try to reproduce the CI
   environment's constraint directly (e.g. temporarily `PATH` without `faust`) rather
   than re-running the same green-locally command that already gave false confidence
   once.
4. Commit with **explicit paths**, never `git add -A`/`-a` (`check_bash_denylist.py`
   enforces this; shared-index repos make it a correctness issue, not just style).
5. `git push` to the **existing** branch — never force-push an already-open PR's
   branch without the human explicitly asking for it (AGENTS.md §6). A fix is a new
   commit.
6. `gh pr checks <n>` again — don't report "fixed" until CI actually re-ran green.
   Pending is not green.

## Step 5 — Stale/conflicting PRs

Rebase onto the current base rather than merging base-into-branch when the PR is a
clean stack (matches `gh stack rebase`/`gh stack sync` if the branch is part of a
tracked stack — check `gh stack view` first). Never resolve a conflict by discarding
either side silently; if a conflict touches content neither side obviously wins on
(e.g. two PRs editing the same registry row), surface it rather than picking one.

## Step 6 — Report, never merge

End with a table: PR #, title, bucket before, action taken, bucket after. For anything
left RED, PENDING, or a design-tension fix, say so explicitly rather than rounding up to
"done." Merging — even a fully GREEN PR — is the human's call (AGENTS.md §7): report
readiness, do not run `gh pr merge`.

## Guardrails this skill must not violate

- No `gh pr merge`, ever.
- No `--force` push, no history rewrite, no `-i`/interactive rebase.
- No `git add -A` / `git add .` / `git commit -a`.
- No deciding a `Proposed` ADR to unblock a fix — if the red check traces back to an
  undecided architectural question, say so and stop there.
- No spending quota (benchmarks, paid providers) to fix a CI failure unless the human
  has already authorized it for that specific PR's work.
- If two open PRs touch the same file in a way that could conflict, flag the pairing
  before fixing either — do not silently reorder someone's stack.
