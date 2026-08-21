---
name: handoff
description: Write a context-clear handoff to .claude/HANDOFF.md so the next agent (or you, after /clear or compaction) does not have to reconstruct this session from raw logs. Use at a planned clear-point — a change landed and the next task is unrelated, or a check.sh level just went green and committed — or when .claude/RESUME.md shows a near-limit trigger. Trigger on "handoff", "write a handoff", "I'm about to clear", "/handoff", "wrap up this session".
---

# Handoff (AGENTS.md §11, implemented)

One file, one fixed path: `.claude/HANDOFF.md`. Writing it **replaces** whatever was
there — there is never a delete step, never more than one handoff on disk, and never a
window where zero exist. Untracked (`.gitignore`); this is working state, not project
content, the same reasoning `.claude/plans/` already uses.

`SessionStart` re-injects this file automatically after `/clear`, after compaction, and
on resume — see `.claude/hooks/handoff_injector.py`. That means **the next agent sees
this without being told to look for it.** It does not mean this skill is guaranteed to
run: nothing in Claude Code can force a skill to fire, so writing it is still on you, at
the right moment, not something a hook enforces. `.claude/hooks/handoff_precompact.py`
is a separate, narrower safety net for the case where you *don't* get the chance — see
"If you run out of runway" below.

## When to write one

**Planned** — a phase actually closed:
- a change landed (pushed, CI green — COLLABORATION.md's own definition of "landed")
  and the next task is a different STATUS.md item;
- `tools/check.sh full` went green and got committed, and the next step is a different
  subsystem.

**Unplanned** — you may be almost out of room:
- `.claude/RESUME.md` exists with `Trigger: near-limit` from this session;
- `.claude/handoff-state.json` shows a compaction already fired this session and you
  have not written a handoff since.

Do not write one for a mid-task pause with nothing closed — an empty or restating
handoff is worse than none, because the next reader trusts it.

## The document

```
# Handoff

<!-- handoff-meta: head=<git rev-parse HEAD> written=<ISO 8601 UTC> branch=<name> -->

OBJECTIVE                 <what this session was trying to do, one or two sentences>
BRANCH                    <name — must match the meta comment above>
CURRENT STATE              <what's true right now: working tree, what's mid-flight>
COMPLETED WORK             <what actually landed this session>
CHANGED FILES               <paths, +added/-removed — from a real `git diff --stat`>
VERIFICATION PERFORMED      <what was run, with results — file:line for anything cited>
VERIFICATION STILL NEEDED   <what a competent reader would still want run>
FAILED APPROACHES / DISCOVERIES  <what didn't work and why, so it isn't retried>
UNRESOLVED RISKS             <what could still be wrong>
NEXT RECOMMENDED ACTION      <the single next step, not a wish list>

--- PluginForge additions ---
ASSUMED COUNT              <output of `tools/check.sh assumed`, as a number>
CI STATE                   <branch's CI status, from `gh` or /orient's digest>
CHANGE REPORT OWED          <yes/no — was §4's five-line report written for the last landed change?>
TIER 2 CITATIONS            <file:line for every Tier 2 claim made above, or "none made">
```

The first ten fields are AGENTS.md §11 verbatim, in that order, so a handoff written
here reads the same as one written for any other project under that policy. The four
PluginForge fields close the gap between "generic handoff" and "the next agent can run
`/orient` and trust what it prints."

**The `handoff-meta` comment line is load-bearing, not decoration.** The injector hook
parses `head=` out of it to detect staleness — if the recorded HEAD no longer matches
`git rev-parse HEAD`, someone (you, another session, a push) moved the branch since this
was written, and the reader needs to know that before trusting `CURRENT STATE`. Get it
right: `git rev-parse HEAD`, not a remembered short sha.

**400-line hard cap.** The injector truncates past it. If the document does not fit,
it is doing STATUS.md's job — cut it down and point at STATUS.md or a `docs/sessions/`
plan for the rest, the way `/orient` points at the full STATUS.md rather than inlining
it.

## The fields most likely to get faked

Same discipline as `/change-report`, because the failure mode is identical:

- **VERIFICATION PERFORMED needs artifacts, not adjectives.** "Tests pass" without a
  command and a result is not evidence — COLLABORATION.md §3's rule applies here
  exactly as it does to a change report.
- **UNRESOLVED RISKS is load-bearing.** An empty or "none" risk line means you have not
  thought about what could be wrong, not that nothing is.
- **FAILED APPROACHES is the field a rushed handoff skips first**, and it is the one
  that saves the most time — the next agent re-discovering a dead end you already found
  costs a full round trip.

## If you run out of runway

If compaction fires before you get to write this — `.claude/hooks/handoff_precompact.py`
runs automatically on `PreCompact` and records machine state only (branch, HEAD sha,
`git status --porcelain`, `git diff --stat`, the compaction trigger) to
`.claude/handoff-state.json`. **It never touches `HANDOFF.md`** — a script has no access
to your reasoning, so it cannot write OBJECTIVE, COMPLETED WORK, or the other prose
fields, and a machine-generated stub silently overwriting a real handoff would be a
strict downgrade. If you see `handoff-state.json` at session start with no matching
`HANDOFF.md`, that is the signal this happened: reconstruct a real handoff from that
state plus `git log`, rather than trusting the state file alone.

## Before writing it

1. **Get the real diff**, same as `/change-report`: `git diff --stat` for CHANGED
   FILES, do not estimate from memory.
2. **Run `tools/check.sh assumed`** for the ASSUMED COUNT field rather than recalling
   last session's number — it may have moved.
3. **Check whether a `/change-report` is owed** for the most recent landed change. If
   one hasn't been written yet, write it first — the change report and the handoff
   answer different questions and neither substitutes for the other.
