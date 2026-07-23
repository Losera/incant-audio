# Fleet Session Retrospective & Revitalization Plan — 2026-07-23

Overseer-authored at graceful shutdown. Two parts: (1) how to revitalize the fleet on resume,
(2) an honest review of the 6-manager architecture vs. 1–2 sessions, measured against this
project's own history.

---

## Part 1 — What the fleet produced today (the evidence base)

| Lane | Output | Committed as |
|---|---|---|
| S1 Backend | State persistence (versioned blob, test 13/13); `loadFaustCode(prompt)`; found PF-018 SR bug | `c34bbb6` |
| S3 Plugin UX | `PluginEditor` split into 3 panels; PF-006 UAF honest marker; Wave-1 plan | `471d045`,`dc3d423`,`137c2bf` |
| S5 Bug tracking | `docs/BUGS.md` registry PF-001..018, each verified vs code | `c47668c` |
| S7 Competitive | `competitive_landscape.md` + `byo_llm_plan.md`; **BYO-LLM authorized (req #6)**; Phase 0 code | `c47668c`, worktree `ca9b241` |
| S4 Testing | `p6_human_run_script.md`; test edits (unverified) | `c47668c`, `7944c8b` |
| S2 Prompting UX | Wave-0 design proposal (artifact); reqs #1–3; unblocked for Wave 1 | (design artifact) |
| S6 Overseer | FLEET board, STATUS reconcile, rulings #1–6, S2 auto-resume relay, RESTART runbook | `c47668c` |

Wall-clock: all of the above advanced inside one working block. A Codex handoff for the S1 lane
(`docs/handoff_s1_codex.md`) was also prepared.

## Part 2 — Revitalization plan (do these on resume)

**Immediate (overseer, first 5 min):**
1. Follow `docs/.fleet/RESTART.md`. Relaunch overseer, then lanes.
2. Apply S5's pending STATUS-sync proposal to STATUS "Broken" (BUGS.md is ahead of it).
3. Re-arm only still-open gate relays (the S2 relay is *done* — its signal file exists).

**Wave 2 per lane:**
- **S2** → Wave 1 code: PromptPanel (indeterminate progress, ruling #2a) + CodeEditorPanel; adopt `loadFaustCode(prompt)` (req #4).
- **S3** → ParamGridPanel auto-layout, lift the 8-knob cap, dynamic window; fix PF-006 UAF.
- **S1 (or its Codex successor)** → PF-018 SR re-init bug; layout-hint `generate.py` mode when S3 specs it; benchmark-re-run prep.
- **S4** → build-verify the split + full suite; drive the P6 human listening pass (needs human ears, groq).
- **S7** → BYO-LLM Phase 1 (reqs #7/#8/#9) — now human-authorized; reconcile the worktree onto main first.
- **S5** → keep the registry synced; triage new bugs.

**Cost discipline (the lever, not a tool switch):**
- Assign models per lane stakes: **Opus** only on audio-path lanes (S1, S3 wiring); **Haiku/Sonnet** on S5/S7/S4/docs. Set with `/model` per session.
- Run **2–3 lanes hot** on genuinely parallel tracks; idle the rest via background watchers (idle = ~0 tokens). Don't run all 6 hot unless the work is truly parallel.
- Prefer background-Bash watchers over `/loop` (a `/loop` bills the model every tick; a watcher bills once, on fire).

**Human-gated, still open (in STATUS "Waiting on you"):**
- Confirm the persisted-state format (S1 says plan-approved).
- Authorize the benchmark-baseline overwrite.
- BYO-LLM was authorized (req #6) → Phase 1 may proceed.

---

## Part 3 — Retrospective: 6 agentic managers vs. 1–2 sessions

**Method.** The honest baseline is this project's own past: *everything before today* was built with
1–2 sessions (git history, CLAUDE.md's per-file narrative). Today ran 6–7 concurrent lanes + an
overseer. Comparison below is against that lived baseline, not a hypothetical.

### Where the fleet clearly won
- **Parallel wall-clock throughput.** Persistence, the editor split, the bug registry, the
  competitive study, and BYO-LLM authorization all advanced in one block. Serially these are days
  of single-session work; the tracks are genuinely independent, so parallelism was real, not nominal.
- **Governance catches a lone session tends to miss.** The overseer caught: a *stale* STATUS.md
  (3 "broken" items already fixed), a §2 format-gate question, its own wrong stderr-merge ruling
  (fixed by reading the header), a worktree auto-clean risk that would have silently lost S7's
  Phase 0, and `.claude/worktrees/` not being gitignored. A single session heads-down on features
  rarely audits its own process like this.
- **Collision avoidance by construction.** Three lanes wanted `PluginEditor.cpp`; the panel split
  resolved it. `loadFaustCode`'s signature change (S1) vs. its call site (S2/S3) surfaced in the
  request log and was solved with a backward-compatible optional arg *before* it broke anything.
  Single-writer STATUS.md neutralized the COLLABORATION.md §5 rewrite-collision hazard.
- **Tool-agnostic coordination — an unexpected strength.** Because the board is just files
  (`FLEET.md`, `STATUS.md`, `docs/BUGS.md`, git), a lane can be handed to Codex (`handoff_s1_codex.md`)
  and still participate. The architecture is not Claude-specific; that widens the cost/model options.

### Where it cost more than it returned
- **~6–7× active token burn.** The dominant cost. Justified only when the lanes are truly parallel.
- **Coordination overhead is real.** `FLEET.md` became a hot multi-writer file (constant
  "modified on disk"); managed via overseer-only sections, but it's friction. Seven prompts to
  launch, a board to maintain, an overseer that must stay alive for the relays.
- **Visibility gaps.** S4 never signed the roll call; its activity had to be *inferred* (pytest
  cache, then the P6 script it left). In a fleet, a silent lane is nearly invisible.
- **Uncommitted-work sprawl.** Multiple lanes left WIP uncommitted; it took this shutdown to
  preserve it (and the worktree Phase 0 was one auto-clean away from loss).

### Verdict — a decision framework, not a winner
- **Use 6+ managers when:** work is **lane-separable and parallelizable** (backend vs. UI vs. docs
  vs. research), there's an **overseer** holding the single-writer + governance invariants, and the
  wall-clock win is worth ~N× spend. Today qualified.
- **Use 1–2 sessions when:** work is **tightly coupled** (everything edits one subsystem),
  **cost-sensitive**, or **exploratory** (high shared-context need). Most days on this project are
  this shape — the fleet's overhead would not pay off.
- **Recommended steady state:** overseer + **2–3 hot lanes** on parallel tracks, rest idle via
  watchers, cheap models on low-stakes lanes, and one lane optionally on Codex to test the
  tool-agnostic path and cut cost. Reserve the full 6–7 for genuine parallel pushes like today.

### One-line takeaway
The 6-manager model bought real parallel throughput and caught process/governance defects a solo
session misses — but it is a **burst tool**, not a default: spin it up for separable parallel work
with an overseer, and collapse back to 1–2 sessions (with cheap models) for coupled or quiet work.
