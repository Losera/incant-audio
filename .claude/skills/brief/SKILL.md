---
name: brief
description: Full cold-re-entry briefing for PluginForge — five parallel-safe prompts (system, history, defects, vision) synthesized into three ranked next-task candidates, at a Tier-2 (file:line cited) evidence bar. Heavier than /orient: run this when picking the next dev task, re-onboarding cold after time away, or briefing someone else on the project. Trigger on "full briefing", "catch me up properly", "what should I work on", "/brief".
---

# Brief

`/orient` is the cheap daily read: live repo state plus the open half of `STATUS.md`, reported
without verification. This skill is the expensive one — run it when you need the *why*, the
architecture, and the roadmap reconciled against the code, not just the open items listed.

It never overlaps `/orient`'s trigger phrases ("where are we", "what's the status") — those
stay `/orient`'s. This is for "what should I work on" and "brief me from scratch."

---

## Preflight — run before dispatching any prompt

Read-only. Never merges, never touches the index, never stashes.

1. Run `${CLAUDE_PROJECT_DIR}/tools/status_digest.sh` for live repo state and the CI line.
2. `git fetch origin --quiet`, then `git rev-list --count HEAD..origin/main`. If non-zero:
   **do not brief off the working tree's `STATUS.md` or `docs/BUGS.md`.** Read them from
   `origin/main` instead — `git show origin/main:STATUS.md` and
   `git show origin/main:docs/BUGS.md` — and say explicitly how many commits behind HEAD is,
   and name them (`git log --oneline HEAD..origin/main`).
3. **CI sibling-run check.** If the digest's CI line reports anything other than a clean
   success, don't stop at that one run. Get the commit SHA it tested, then
   `gh run list --commit <sha>` to see every run against that same commit across triggers
   (`push`, `pull_request`). If a sibling run on the same SHA succeeded, report the failing run
   as what it actually is — e.g. "cancelled: exceeded the 40-minute `build-host` timeout on the
   PR trigger; the same commit passed on push in 17m49s" — not as a bare red banner. Only report
   true red if every run against that SHA failed or none exists.
4. Assemble a short **Ground Truth** block from the above (HEAD SHA, branch, commits-behind
   count and their one-line summaries, true CI status, dirty-tree paths) and pass it verbatim
   into every prompt below, so no sub-prompt re-derives repo state or contradicts another.

## Evidence contract — appended verbatim to every prompt below

> Every substantive claim is checked against the source and cited `path:line`, or written with
> an explicit `(unverified)` tag. `STATUS.md`, `docs/BUGS.md`, and `docs/decisions.md` are
> *reports*, not evidence (COLLABORATION.md §3) — read them, but verify anything load-bearing
> against the actual code before repeating it as fact. Where a doc and the code disagree, say so
> and cite both. Never write "works", "verified", or "tests pass" for a check you did not
> personally run this session.

## Execution

Dispatch P1–P4 as four parallel `Explore` agents (single message, four tool calls), each
receiving the Ground Truth block and the Evidence contract appended to its prompt body below.
Do not run P5 until all four have returned — do not fabricate or predict a missing report.

---

### P1 — SYSTEM
*What is this thing and what can it actually do today?*

Give a plain-language description of the pipeline: prompt → router (keyword-scored, no LLM
call) → LLM provider → Faust DSL → compiler validation (with up to 3 error-feedback retries) →
LLVM JIT → live audio-thread DSP swap. Then a concrete, user-facing feature list for both build
targets (PluginForge Host / effects, PluginForge Synth / instruments), sourced from
`host/Source/*` and `llm/*` — not from README marketing copy.

Explicitly separate **shipped** from **stubbed or read-only** (e.g. `/export` is a gated stub
that cannot produce a working plugin yet; `CodeEditorPanel` is read-only, no Compile button).

Name the three real-time invariants a newcomer can break in an afternoon, with file:line:
the `audioBusy` drain guard around `processBlock` (the swap-protocol TOCTOU fix), the fact that
parameters are declared exactly once in `PluginProcessor::createParameterLayout()` and
`ParamPool` must never create them, and that `pushToFaust()` runs on the audio thread with no
allocation, locks, logging, or map lookups permitted.

Budget: ~600 words plus one feature table. End with the Evidence contract.

---

### P2 — HISTORY
*What happened in the last four sessions, and why?*

Read `docs/sessions/*.md` for sessions 012–016, the last ~30 commits, and the newest `STATUS.md`
"Works — and how we know" block (use the `origin/main` copy per the Ground Truth block if the
checkout is behind).

Group findings by **motive**, not by date. This project's recent work splits into three
threads: product repair driven directly by user-reported failures (sessions 012–014), gathering
real-environment evidence such as pluginval/Carla validation (2026-08-16), and measurement
integrity work fixing the offline efficacy judge and making the efficacy harness resumable
(ADR-027 onward). For each entry, answer "what forced this to happen now" — a bug report, a
prior session's own finding, or a deliberate evidence-gap closure.

Budget: ~500 words. End with the Evidence contract.

---

### P3 — DEFECTS
*What's open, ranked by what it blocks.*

Build one table of every open `PF-###` defect from `docs/BUGS.md` (use the `origin/main` copy
per Ground Truth if behind) plus any unfiled `STATUS.md` "Broken" items, with columns: ID ·
one-line symptom · severity · **what it blocks** (a specific downstream capability, not a
vague "quality") · rough cost to fix.

Separate the table into two sections: **code defects** (something is broken) and **evidence
gaps** (something is unmeasured — this is the `assumed` metric). They are scheduled
differently: an urgency-ranked list never naturally schedules evidence work, which is exactly
why the project reserves a dedicated slot for it. State the current count from
`tools/check.sh assumed` (run it).

Budget: one table plus ~200 words. End with the Evidence contract.

---

### P4 — VISION
*Where is this going, and how far along is it?*

Lead with `bench/PLUGIN_PRODUCT_DEVELOPMENT_ARCHITECTURE.md`'s six development phases, its
7-point "Definition of release-ready", and its "Immediate critical path" diagram. For each
phase, check its self-declared status ("substantially complete", "partly complete", etc.)
against actual code and CI evidence, and flag any that overstates.

This document does not supersede three other roadmap sources still present in the repo —
reconcile against them and say which is live and which is superseded, rather than treating the
architecture doc as the only word:

- `docs/ux_roadmap.md` — the phased delivery roadmap, framed around "a one-shot generator is a
  demo, not an instrument-building tool."
- `PLUGIN_HEALTH_PLAN.md` — the release-readiness work queue ("it is not release-ready").
- `docs/competitive_landscape.md` — check its claim "we have never listened to a single
  generated plugin" against `STATUS.md`'s 2026-08-16 EFFECTS listening-pass sign-off; these
  appear to conflict and P4 should resolve which is current.

Surface the scheduling tension plainly: the architecture doc's critical path opens with
"finish the 125-cell efficacy evidence," while `STATUS.md`'s Next-three opens with "get it into
an interactive DAW session." Note that the architecture doc itself is untracked (no commit, no
ADR), and that the human's own vision notes (`UDHR.md`, `IDEAS.md`, named in `STATUS.md`) are
outside the repo and unreadable — so every roadmap statement available here is Claude-authored,
and the brief should say so rather than imply otherwise.

Budget: ~600 words plus a phase-status table. End with the Evidence contract.

---

### P5 — DECIDE (synthesis, main session, after P1–P4 return)

Consume all four reports. Emit **exactly three ranked candidate next tasks**. For each: what it
is, why now, what it unblocks, rough session cost, what evidence it would produce, and what it
would explicitly **not** prove (avoid manufacturing false confidence).

Constraints, all three mandatory:
- At least one candidate must move the `assumed` count — per `CLAUDE.md`, that number is the
  project's chosen metric precisely because it can't be improved by writing documentation.
- Flag any candidate touching architecture, schema, or the provider/paid-gate contract as
  **consult-first** (COLLABORATION.md §2) rather than something to just start on.
- If a candidate competes with an unfinished listening pass, say so — COLLABORATION.md §1: a
  listening pass outranks a diff read when both are on the table.

End with a single `YOUR MOVE` line naming the recommended pick and the reason in one sentence.

---

## Delivering the result

Default to a published Artifact (see the `Artifact` tool) unless the user asked for terminal
output only: sections for Ground Truth, System, History, Defects, Vision, and the three ranked
candidates, plus a short pipeline diagram and a critical-path diagram. Load the
`artifact-design` skill before writing the page. This is a one-off read for the human, not a
repo file — do not commit the rendered page or write it under `docs/`.
