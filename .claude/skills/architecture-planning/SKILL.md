---
name: architecture-planning
description: Use when a PluginForge architectural decision is being made or reconsidered — a new capability, a design trade-off, or reopening something in docs/decisions.md. Walks the COLLABORATION.md engagement-mode framework and decides whether the decision needs a new hook, ADR, subagent, loop, or CLAUDE.md/COLLABORATION.md update. Trigger on "should we...", "new architectural decision", "does this need an ADR", "/architecture-planning".
---

# Architecture Planning

Routes a PluginForge architectural decision to the right place: a mechanical hook,
an ADR draft, a subagent, a loop, a doc update, or a memory entry. This skill is a
**router**, not a source of truth — it never contains project facts itself, only
the procedure for deciding where a fact or rule belongs. See the "Where Information
Lives" table in COLLABORATION.md §8 for the underlying convention this walks.

## Step 1 — Reload context

Read `CLAUDE.md` and `COLLABORATION.md` in full before reasoning about the
decision. Do not rely on memory of their contents from earlier in the session —
both files can change, and COLLABORATION.md's own preamble already requires
reloading both at the start of any non-trivial session.

## Step 2 — Classify the consequence, not the file

State which of COLLABORATION.md §2's four consult-first triggers this decision hits,
with one sentence of reasoning:

1. irreversible or outward-facing · 2. architectural direction ·
3. a contract between components · 4. build, dependency, or distribution

Trigger 2 is the one that usually fires here: writing a new ADR, reversing an
accepted one, or foreclosing an option an open ADR is still holding open. **The
drafting is not gated — the decision is.** So this skill drafts proposed ADR text
and proposes it; the human agrees; then it is committed.

If nothing fires, say so in one sentence and proceed. Do not stall a session on a
classification question — COLLABORATION.md §2's closing paragraph is explicit that
an ambiguous call should be made, done, and reported.

## Step 3 — Walk this checklist in order

Work through each item; skip only the ones that plainly don't apply, and say so
briefly rather than silently omitting them.

**(a) Does this need a hook?**
Ask: does this decision establish or change a project invariant that a future
Claude edit could silently violate? If yes, classify it the same way
`invariant-hook-writer` does:
- **Hookable** — a local, lexical property of one file, one function's body
  (locatable by a fully-qualified anchor + brace-matching, like
  `check_rt_safety.py`), a set of paths, or a command string.
- **Not hookable** — requires whole-program semantic reasoning. The two
  established examples: ADR-009's "no duplicate symbol anywhere in the generated
  Faust program" (needs a Faust-grammar-aware parse) and RT-thread-reachability
  beyond `FaustEngine::process`/`PluginForgeProcessor::processBlock` (needs a call
  graph a regex can't build).

If hookable, hand off to the `invariant-hook-writer` subagent rather than writing
the hook script inline in this conversation — it owns the settings.json
registration safety and the red/green self-test discipline.

**(b) Does this need an ADR?**
If the decision belongs in `docs/decisions.md`, draft it using the existing
Status/Context/Decision/Reasons/Consequences template at the bottom of that file.
Present the draft in your response for the human to paste in — do not write it to
the file yourself (see Step 2's hard constraint).

**(c) Does this need a new subagent?**
If the decision calls for a repeatable, specialized procedure analogous to
`invariant-hook-writer`, sketch its name/directory/tools/instructions in the same
shape rather than solving the problem ad hoc inside this conversation.

**(d) Does this need a loop?**
If the decision introduces a recurring verification need (something that should be
re-checked every time a related file changes), point at the concrete pattern
already established in `bench/check_prompt_regression.py` — hash-gated, cheap
early-exit, cost-aware — as the template rather than inventing a new mechanism.

**(e) Does this need a CLAUDE.md or COLLABORATION.md update?**
Apply the table in COLLABORATION.md §8: durable facts/status/file-map/"Do not"
items go in CLAUDE.md; process/modes/protocol go in COLLABORATION.md. A CLAUDE.md
status-line update is ordinarily ungated Tier 1. COLLABORATION.md's own rules are
§10 territory: the protocol has no inertia, but a rule Claude thinks is wrong gets
argued with in the report rather than silently routed around.

**(f) Does this need a memory entry instead?**
If it's a cross-session heuristic that's useful but not yet stable or general
enough to check into a project file, note it as a memory candidate rather than
writing it into CLAUDE.md/COLLABORATION.md prematurely. If something already in
memory has since proven stable, say so — it should be promoted into a checked-in
doc and the memory entry retired, not left duplicating a now-authoritative source.

## Step 4 — Report it

End with the COLLABORATION.md §4 change report — CHANGED / WHY / VERIFIED / RISK /
YOUR MOVE — naming which checklist items (a)–(f) fired and why. `RISK` is the
load-bearing line.

There is no separate log to append to. `docs/collaboration_log.md` was retired in
COLLABORATION.md §5 and deleted on 2026-07-25; its `Mode signal` field is the
cautionary example of a self-graded rubric that never returned a failure (§9).
Narrative history lives in git.

## Hard prohibitions (restated, not optional)

This skill must never, itself, Write or Edit: `docs/decisions.md`, anything under
`llm/prompts/`, or the RT-scoped bodies of `FaustEngine::process()` /
`PluginForgeProcessor::processBlock()`. It drafts text or hook specs for a human or
the `invariant-hook-writer` subagent to land — it is not the author of record for
any of these.
