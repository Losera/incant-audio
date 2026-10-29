# Phase 6 — Later expansion

**Status:** Deferred by design (two items pulled earlier — ADR-038) · **Last reviewed:** 2026-09-09

Part of the six-phase plan — see [`README.md`](README.md). Defers to `STATUS.md` and
`docs/BUGS.md`.

## What this phase proves

Nothing yet. This phase opens only after an initial supported release exists and holds.

## Where it stands

Correctly parked. None of the work below precedes reliable audio, real-host behaviour,
session compatibility, and distribution evidence — i.e. Phases 2–4 first.

**ADR-038 (accepted 2026-09-09)** pulls two of the "Open work" items — richer generated-face
rendering and the export path (PF-053) — into a track that runs **parallel to** Phases 3–4
rather than after a release, on the grounds that ADR-023's blocking prerequisite (a plugin
validated in a real DAW) is now met and ADR-035's face floor now exists to build on. The
critical path (PF-024, PF-032, the Phase-4 release) is unchanged, and the export feature is
not cut as a public release while those defects are open. See
`docs/sessions/020-generated-faces-v2.md` for the F1–F5 / E1–E4 ladders.

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
  (`PLUGIN_HEALTH_PLAN.md` P1.10). **ADR-038 (accepted 2026-09-09) starts both ahead of
  schedule** — see "Where it stands" above.
- Sharing / community workflows.

## Done when

Not applicable yet.

## Next action

Per ADR-038: F1 (bipolar-knob bugfix) and E1 (AOT Faust emit) next; the rest of Phase 6
still revisits after Phase 4 produces a supported release.
