# 021 — Workflow and human guide

Written out of the 2026-10-20 workflow audit, which found that PluginForge had already
built nearly every instrument a review like this would otherwise recommend —
`tools/health_report.py`'s product lane, the ADR log, the control-wiring test suite —
wired into a lane that is rarely read, and absent from `/orient`, the one read every
session is supposed to start with. The fixes landed alongside this file
(`tools/integration_report.py`, the decision-queue section in `tools/status_digest.sh`,
`tools/check.sh assumed`'s cross-check, the `/audit` skill). This document is the human
half: what changed about the session shape, and which decisions are yours and no agent's,
stated once so it does not have to be re-derived from STATUS.md prose every time.

A living document (COLLABORATION.md §7): edit this in place as the workflow changes,
rather than writing a dated record that goes stale.

---

## Session shape

```
/orient  →  one worktree per session  →  work  →  /change-report  →  push + open a PR  →  /handoff
```

The invariant this audit found broken: **a session used to end with a `STATUS.md`
paragraph instead of a PR.** At audit time, nine branches held finished, clean,
0-commits-behind work — including a landed bugfix (PF-083) with its own regression test —
and none had been pushed. `/orient` now surfaces exactly this
(`### Integration state`, from `tools/integration_report.py`), so a session ending without
a PR is visible at the *next* session's start, not discovered a month later during an
audit.

If `/orient`'s integration table shows a branch that is clean, zero-behind, and has no PR:
that is finished work waiting on nothing but the open-PR step. Pushing and opening the PR
is routine (COLLABORATION.md §3 Tier 1) — the agent should do it without asking, the same
way it commits a Tier-1 change without asking.

## Your lane — what genuinely cannot be delegated

Everything else in this project is written to minimize how often you're needed. These
seven are not a matter of agent capability; per COLLABORATION.md §1–§2, they are
structurally yours:

1. **The listening pass.** COLLABORATION.md §1: "whether a generated plugin sounds like
   what was asked for" has no instrument. The render oracle proves a patch is not
   broken — no NaN, no silence, no DC, no runaway gain. It cannot tell you the filter is
   musical. Nothing in this audit changes that; it is the one judgment every `YOUR MOVE`
   line is written assuming outranks a diff read.
2. **ADR decisions.** `/orient`'s new `### Decision queue` lists every ADR still
   `Proposed`, oldest first, plus any using a status word outside the template
   (`Proposed`/`Accepted`/`Rejected`/`Superseded`) — those are undecided too, just not
   spelled that way. An agent drafts ADR text and proposes it; only you accept, reject, or
   amend it (COLLABORATION.md §2 trigger 2).
3. **Spend authorization.** `tools/check.sh quota` and anything hitting a paid or
   quota-metered provider refuses outright without `--i-authorize-spend`.
4. **Posting to third parties.** GitHub issue comments, upstream bug reports — an agent
   drafts, you post. `gh issue comment` from an agent is a protocol violation regardless
   of how correct the draft is.
5. **Destructive git.** Force-push, history rewrite, deleting a remote branch, merging a
   worktree with no evidence it's redundant — `tools/integration_report.py` names the
   candidates; a human decides and runs the command.
6. **The reserved evidence slot.** One of `STATUS.md`'s "Next three things" is tagged
   `*(evidence)*` and enforced by `tests/test_control_wiring.py`. It exists because a list
   ranked by urgency will never schedule work whose defining property is that it is not
   urgent — your attention is what keeps that slot from quietly being spent on something
   easier instead.
7. **Architectural direction.** New component, new dependency, new persisted schema —
   AGENTS.md §4, COLLABORATION.md §2 trigger 2. An agent implements; you decide what gets
   built.

## The two-minute session end

`STATUS.md`'s "Waiting on you" #6 used to carry a hand-maintained worktree inventory —
the single most wrong section in the file by the time this audit found it, naming eight
worktrees already gone and omitting all eight that existed. That inventory is retired.
What a session-end now needs:

1. `/change-report` for anything landed this session (COLLABORATION.md §4's five lines:
   CHANGED / WHY / VERIFIED / RISK / YOUR MOVE).
2. Push and open a PR if `tools/integration_report.py` shows the branch ready — see
   "Session shape" above.
3. `/handoff` if the next session won't be a continuation — it writes
   `.claude/HANDOFF.md` once, replacing whatever was there.

`STATUS.md` itself only needs rewriting (COLLABORATION.md §5) when something changed that
the computed sections (`/orient`'s digest, `tools/health_report.py --lane product`) can't
derive on their own — a new defect, a closed one, a changed "Next three." The worktree/PR
inventory is no longer part of that file at all; it is computed every time, not recorded.

## The diff-reading trap

**Always `git diff origin/main...HEAD` (three dots). Never `..` (two dots), when judging
what a branch contributes.**

A two-dot diff compares a branch's tip against `main`'s *current* tip. If `main` has
moved since the branch split, main's own later additions render as if the branch deleted
them. This audit reproduced it exactly — three branches that looked like they'd gut the
repo were small:

| branch | two-dot (`..`) | three-dot (`...`), the branch's real contribution |
|---|---|---|
| `codex-llm-generation-professional` | −50,249 lines | **+1,058** |
| `shell-command-bar` | −49,393 lines | **+76** |
| `f2-f3-face-ladder` | −43,104 lines | **+569** |

One of these misreadings froze a branch from both deletion and merge for a month
(`docs/decisions.md` "would regress by 391 lines," per the stale `STATUS.md` entry this
audit corrected) on a two-dot number that was never the branch's own contribution.
`tools/integration_report.py` only ever computes three-dot, specifically so this cannot
recur through that tool — but a raw `git diff` typed by hand has no such guard. Three
dots, every time.

## Concurrency

This repo is worked by more than one session at once, routinely. `session_collision_guard.py`
fires at session start when: another session's `.claude/HANDOFF.md` names a different
branch than yours, tracked files are already dirty, or another transcript was written in
the last 8 minutes. None of these block you — they are a prompt to check before assuming
the tree is yours alone.

**One worktree per session, as the rule, not the exception.** `git worktree add
.worktrees/<name> -b <branch> origin/main`, work there, never on the shared primary
checkout. This audit's own session found the primary checkout 9 commits ahead of
`origin/main` with no upstream and a file another session had already modified — exactly
the shared-index hazard the guard exists to flag. A worktree that switches which branch it
has checked out mid-session (observed live during this audit) is a sign another session is
using it right now; `tools/integration_report.py`'s output is a live read and will reflect
that correctly on the next run — do not treat a worktree's branch as fixed.
