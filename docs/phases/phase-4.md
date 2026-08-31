# Phase 4 — Installable & supportable builds

**Status:** Release-critical, blocked · **Last reviewed:** 2026-08-30 (main `55eda8c`)

Part of the six-phase plan — see [`README.md`](README.md). Defers to `STATUS.md` and
`docs/BUGS.md`.

## What this phase proves

Installation and support become repeatable on a clean machine. Today generation is broken
when the plugin runs as an installed VST3 from a launcher-started DAW.

## Where it stands

Packaging exists; release proof does not. The blocking defects are in runtime discovery of
`llm/generate.py` and the installed Python runtime's configuration. ADR-032 (accepted) is
the fix path for the config half.

## Evidence on record

- All-rights-reserved licensing notice in place; no open-source grant
  (`PLUGIN_HEALTH_PLAN.md` P0.1).
- CI now includes the four deterministic gates it was missing; the ladder-to-CI structural
  check requires every gate. Locally green; remote green is the completion bar
  (`PLUGIN_HEALTH_PLAN.md` P0.3).
- Distribution reality is documented and honest: Linux x86-64 VST3 and Standalone only; AU
  / Windows / signing / notarization are not implemented and not advertised
  (`docs/distribution.md`).

## Open work

- **PF-065** — generation fails as an installed VST3 ("generate.py not found"),
  re-confirmed in REAPER 2026-08-28: the env override is not inherited, the parent-dir
  walk starts at the `.so` under `~/.vst3`, and the XDG fallback misses
  (`host/Source/PromptPanel.cpp`).
- **PF-071** — the partial fix traded that for a stale 2026-08-15 runtime at
  `~/.local/share/pluginforge/llm/` that defaults to the paid provider with no `.env`.
  Reproduced in REAPER and Carla (`docs/BUGS.md`).
- **ADR-032 v1** implementation: `provider`/`model` in the request JSON (`INTERFACE.md`,
  Tier 2), an in-plugin picker for the five providers, a
  `~/.config/pluginforge/config.json` read before the XDG step. Closes the config half
  only (`STATUS.md`, "Next three").
- A real `install.sh` that writes a version-matched runtime — closes the install-layout
  half (stays with PF-065).
- Clean-checkout Release smoke; supported-platform matrix; toolchain-range pins; a staged
  release artifact with licenses / hashes / provenance; a defined release-acceptance gate
  (`PLUGIN_HEALTH_PLAN.md` P1.1–P1.5).
- The Codex `feat/recommendation-mvp` branch has independently started the ADR-032
  wire-contract piece — reconcile before either lands.

## Done when

A clean machine can build or install the documented artifact; generation works from the
packaged runtime path with at least one supported provider; every public compatibility
choice is documented and versioned.

## Next action

Implement ADR-032 v1 (`STATUS.md` "Next three" item 2) — it makes the plugin usable from a
launcher-started DAW and is the smallest unblocking change. Reconcile the wire contract
with the Codex branch first.
