# Phase 5 — Iterative creative workspace

**Status:** Foundations only · **Last reviewed:** 2026-08-30 (main `55eda8c`)

Part of the six-phase plan — see [`README.md`](README.md). Defers to `STATUS.md` and
`docs/BUGS.md`.

## What this phase proves

Move from "generate a result" to "develop a reusable sound" — refine, compare versions,
snapshot, and (for advanced users) open the hood on the Faust source.

## Where it stands

The enabling step (state persistence) shipped, and with it New / Add / Redo plus prompt
history. The higher-level authoring loop is unbuilt. One large **unreviewed** addition
sits in a Codex worktree.

## Evidence on record

- New / Add / Redo generation modes from the real UI; prompt history stored and recallable
  (ADR-011 amendment, closed 2026-08-06; `docs/decisions.md`).
- Read-only Faust code panel with copy support and compiler-error line highlighting; the
  no-code workflow remains the default.
- State persistence — the roadmap's enabling step — is done; everything downstream was
  blocked on it (`docs/ux_roadmap.md`).

## Open work

- A/B switching between the two most recent accepted patches.
- Named snapshots containing source, prompt, mappings, and values.
- Editable Faust source with a Compile action through the same validation / JIT path.
- Preset browser and versioned `.pforge` import / export.
- Provider / model selection UI — **ADR-032 accepted 2026-08-29, implementation
  unstarted** (`docs/decisions.md`).
- **Unreviewed:** the Codex `feat/recommendation-mvp` branch adds a pre-generation
  editable *design plan* step (+1,379 LOC, 44 Python tests green in the worktree, C++
  unbuilt). No ADR, empty commit bodies, and it overlaps ADR-032's wire-contract piece — a
  product-direction call. Worktree: `.worktrees/codex-recommendation-mvp`.
- Deliberately parked: a piano-roll editor — needs both a note grid and a clock, and there
  is no host transport in Standalone.

## Done when

A user can compare, snapshot, and reopen developed sounds, and (as an advanced path) edit
and recompile the Faust source through the shipping validation / JIT path.

## Next action

Decide the Codex recommendation branch — accept the direction (and give it an ADR plus
reconcile with ADR-032), fold in a subset, or shelve it. Then sequence A/B switching and
snapshots per `docs/ux_roadmap.md`.
