# Phase 6 — Later expansion

**Status:** Deferred by design · **Last reviewed:** 2026-08-30 (main `55eda8c`)

Part of the six-phase plan — see [`README.md`](README.md). Defers to `STATUS.md` and
`docs/BUGS.md`.

## What this phase proves

Nothing yet. This phase opens only after an initial supported release exists and holds.

## Where it stands

Correctly parked. None of the work below precedes reliable audio, real-host behaviour,
session compatibility, and distribution evidence — i.e. Phases 2–4 first.

## Evidence on record

- The deferral itself is on record and reasoned (the product-architecture draft under
  bench/, Phase 6).
- MIDI-fidelity gaps triaged 2026-08-16 as pre-existing and by-design, not regressions: a
  monophonic engine, block-granularity MIDI (~10.7 ms jitter, documented in-code), a
  hardcoded 2.0 s tail, no MIDI CC mapping (`STATUS.md`, "Broken — ranked").

## Open work

- AU / Windows / macOS builds — each needs one Release CI build and one host-validation
  obligation before being advertised (`PLUGIN_HEALTH_PLAN.md` P1.2).
- Deeper polyphony and per-voice cloning (the engine is deliberately mono today —
  `host/Source/FaustEngine.cpp`).
- Richer meter widgets (PF-052) and exported standalone plugin projects (PF-053) — kept as
  separate deliverables, export gated until it yields a validated project
  (`PLUGIN_HEALTH_PLAN.md` P1.10).
- Sharing / community workflows.

## Done when

Not applicable yet.

## Next action

Nothing. Revisit after Phase 4 produces a supported release.
