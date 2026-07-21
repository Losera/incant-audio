---
name: attention-report
description: Generate a severity-ranked report of everything in PluginForge that currently needs the human's attention — open decisions, unverified assumptions, stale docs, failing checks — each item cited by file:line and tagged with the COLLABORATION.md engagement mode required to fix it. Run at the start of a session, before planning a day's work, or after landing a significant change. Trigger on "what needs my attention", "status report", "where are we", "/attention-report".
---

# Attention Report

You are auditing PluginForge on behalf of its human owner. Your job is to tell them, in one
scannable report, what requires **their** attention — not everything that exists, only what is
open, unverified, contradictory, or blocking. You are a skeptical auditor: documentation claims
are hypotheses to check against the code, not facts to repeat.

## Step 1 — Gather fresh state (do not answer from memory)

Run/read all of the following before writing a single line of the report:

1. `git status` and `git log --oneline -15` — uncommitted work and recent history.
2. `CLAUDE.md` "Current status" section — treat every claim as unverified.
3. `docs/collaboration_log.md` — the newest 5 entries (file is reverse-chronological).
4. `docs/next_steps.md` — items marked STILL OPEN or PARTIAL.
5. `docs/decisions_reconstructed.md` — every decision whose status is not "Decided".
6. `python -m pytest tests/ --collect-only -q 2>&1 | tail -2` and, if cheap, run
   `python -m pytest tests/ -m "not integration" -q`.
7. Live fail-loud markers in shipped code only:
   `grep -rn "TODO: VERIFY\|TODO VERIFY\|SUBTLE:" host/Source host/tests llm/*.py`
   (exclude `docs/` — those files narrate the convention, they aren't live defects).
8. `ls host/build/*_artefacts 2>/dev/null` or equivalent — has the C++ build ever been run here?

## Step 2 — Cross-check claims vs reality

For each CLAUDE.md/docs claim relevant to an open item, verify it against the code before
repeating it. Known example of drift: CLAUDE.md has claimed "no C++ test coverage at all" after
`host/tests/ParamPoolConcurrencyTest.cpp` already existed. Any claim you could not verify goes
in the report explicitly marked *(unverified)* — never silently upgraded to fact.

## Step 3 — Write the report

Severity-ranked sections, in this order. Every item: one-line statement, `file:line` citation,
and the engagement mode (DELEGATE / PAIR / HUMAN-OWNED) the fix requires per COLLABORATION.md §1.
Omit empty sections. Do not pad — five sharp items beat fifteen diluted ones.

1. **🔴 Blocking** — stops the prototype or corrupts state if ignored.
2. **🟠 Needs your decision** — open/tentative ADRs, HUMAN-OWNED changes awaiting ratification.
3. **🟡 Unverified assumptions** — live `TODO: VERIFY` markers, tests never run, claims
   estimated but not measured (e.g. a benchmark rate asserted from a prompt change).
4. **📄 Stale docs** — CLAUDE.md / docs statements contradicted by the code.
5. **🟢 Verified healthy** — max 5 bullets, only things you re-verified this run.

## Step 4 — Completion estimate

End with a **prototype-completion estimate**: a percentage plus its definition. "Working
prototype" means: a user types a natural-language prompt into the running standalone app (or a
DAW-hosted VST3), and audible, parameter-controllable DSP comes out. State which pipeline stages
are proven vs merely implemented, and name the single shortest path of remaining work.

## Constraints

- Read-only: this skill never edits files, never fixes what it finds.
- Never read or quote `llm/prompts/*` content (HUMAN-OWNED product IP) — refer to it by path.
- If you find a genuinely new architectural question, point at `/architecture-planning`
  rather than resolving it inline.
