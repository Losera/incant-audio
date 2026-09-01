# Product phases

Incant Audio / PluginForge is built in six phases, from the core generate→compile→swap
loop to an initial supported release. This directory is the **per-phase rollup**:
`phase-1.md` … `phase-6.md`, one file each.

**This set defers to `STATUS.md` and `docs/BUGS.md`.** STATUS.md is the live weekly
picture (rewritten each session per `COLLABORATION.md` §5); docs/BUGS.md is the durable,
IDed defect registry. When a phase file disagrees with either, they win — fix the phase
file.

## Status at a glance — 2026-08-30, main `55eda8c`

| # | Phase | Status | File |
|---|---|---|---|
| 1 | Core loop | Substantially done | [`phase-1.md`](phase-1.md) |
| 2 | Real-host proof | Mostly closed | [`phase-2.md`](phase-2.md) |
| 3 | Audible & interaction defects | Active backlog | [`phase-3.md`](phase-3.md) |
| 4 | Installable & supportable builds | Release-critical, blocked | [`phase-4.md`](phase-4.md) |
| 5 | Iterative creative workspace | Foundations only | [`phase-5.md`](phase-5.md) |
| 6 | Later expansion | Deferred by design | [`phase-6.md`](phase-6.md) |

## Critical path

    groq 125-cell efficacy run  (blocked on quota)
      → interactive host validation  (Phase 2, Carla rack)
      → fix audible blockers  (Phase 3)
      → clean-checkout release rehearsal  (Phase 4)
      → initial supported release

Phase 5 (A/B switching, snapshots, editable code) runs parallel into the release, not
before it. Prompt efficacy is one input to the release decision, not the gate.

## Definition of release-ready

An initial supported release requires all seven, none yet fully evidenced:

1. A clean machine can build or install the documented artifact.
2. Both effect and synth pass strict plugin validation and load interactively in a
   supported host.
3. Generation works from the packaged runtime path with at least one supported provider.
4. Effect audio and synth MIDI are audible and stable across generation, refinement,
   automation, save/reopen, sample-rate changes, and teardown.
5. The offline audio corpus has no unexplained silence, NaN, runaway gain, arity, or tail
   failure.
6. Every public compatibility choice — platforms, parameter presentation, state schema,
   provider policy, dependencies — is documented and versioned.
7. Objective validation and human listening evidence are both recorded; neither
   substitutes for the other.

`PLUGIN_HEALTH_PLAN.md` carries the P0/P1/P2 work queue behind criteria 1–3 and 6.

## How this is maintained

Each `phase-N.md` carries a **Last reviewed** line. Refresh a phase file when its boundary
materially moves — a blocker closes, the next action changes, a phase flips status — not
on every session. These files are in `tests/test_control_wiring.py`'s `LIVE_DOC_FILES`, so
a dead repo-path citation or a retired-mode instruction fails CI.

## Origin

Distilled 2026-08-30 from the product-architecture draft at
bench/PLUGIN_PRODUCT_DEVELOPMENT_ARCHITECTURE.md (2026-08-17, untracked). That draft may be
retired once this set is trusted; its runtime-architecture and data-flow diagrams are a
synthesis of the canonical sources and are not lost — those live in `README.md`,
`INTERFACE.md`, `host/Source/CONTRACT.md`, and `docs/decisions.md`.
