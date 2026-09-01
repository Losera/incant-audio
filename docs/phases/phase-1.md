# Phase 1 — Core loop

**Status:** Substantially done · **Last reviewed:** 2026-08-30 (main `55eda8c`)

Part of the six-phase plan — see [`README.md`](README.md). Defers to `STATUS.md` and
`docs/BUGS.md`.

## What this phase proves

A musician describes an effect or instrument; an LLM turns it into Faust DSP; the plugin
JIT-compiles and hot-swaps it with no restart; the DAW keeps its parameters and project
state.

## Where it stands

The end-to-end path works and has been exercised for weeks. Remaining refinements roll
into Phase 3 (defect hardening) and Phase 5 (authoring workflow), not back into this
phase.

## Evidence on record

- NL prompt → Faust → LLVM JIT → VST3 producing a working effect — verified by ear
  2026-07-22, many patches since (`STATUS.md`, "Works — and how we know").
- Render oracle proves no NaN / silence / DC / runaway over the corpus on every
  `tools/check.sh audio` (`bench/render_oracle.py`, $0).
- Separate effect and synth targets; VST3 bus layout and MIDI capability are fixed at
  plugin-scan time by design.
- Faust CLI validation plus a corrective retry loop, up to 3 attempts, compiler stderr
  fed back (`llm/generate.py`).
- Real-time-safe DSP replacement: off-thread compile, an `audioBusy` drain guard, and
  compile-callback-before-`ready` ordering (`host/Source/CONTRACT.md`,
  `host/Source/FaustEngine.cpp`).
- Dynamic control capture → 64 stable `macro_*` DAW slots; automation IDs survive a live
  swap (`host/Source/ParamPool.cpp`).
- State persistence: patch source, prompt, knob values, and mapping are saved, reopened,
  and recompiled (`host/Source/PluginProcessor.cpp`; `host/tests/EditorSessionTest.cpp`).
- New / Add / Redo modes selectable from the real UI — `EditorSessionTest` scenario 25 is
  the red-then-green proof.

## Open work

- Nothing phase-blocking. The `host/Source/FaustEngine.cpp` `prepare()`
  sample-rate-field-before-mutex inspection finding is unconfirmed and tracked under
  Phase 3 / `PLUGIN_HEALTH_PLAN.md` P0.5.

## Done when

A musician can create and iterate on a live effect or instrument without rebuilding C++ or
restarting the plugin. **Met.**

## Next action

None for this phase — effort belongs in Phases 2–4.
