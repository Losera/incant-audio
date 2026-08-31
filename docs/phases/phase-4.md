# Phase 4 — Installable & supportable builds

**Status:** Release-critical, blocked · **Last reviewed:** 2026-08-31 (main `3512f8e`)

Part of the six-phase plan — see [`README.md`](README.md). Defers to `STATUS.md` and
`docs/BUGS.md`.

## What this phase proves

Installation and support become repeatable on a clean machine. Today generation is broken
when the plugin runs as an installed VST3 from a launcher-started DAW.

## Where it stands

Packaging exists; release proof does not. The blocking defects are in runtime discovery of
`llm/generate.py` and the installed Python runtime's configuration. ADR-032 v1 is the fix
path for the config half: the **backend landed 2026-08-31 (PR #42)** — a plugin-read
`~/.config/pluginforge/config.json`, `generate_script_path` resolved before the XDG step,
`provider`/`model` in the request JSON. The **in-plugin picker + resolved-path surface**
(ADR-032 items 2 & 7) is in flight (PR #43). Neither PF-065 nor PF-071 is closed until a
human runs the repro in a launcher-started DAW.

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

- **PF-065 / PF-071 — verify in a real launcher-started DAW.** The resolution logic is
  tested (`PromptPanelPathResolutionTest`, `EditorSessionTest` scenarios 43/44), but no one
  has yet launched REAPER or Carla *from the desktop launcher* — no `PLUGINFORGE_*`, no
  `.env` — with a `~/.config/pluginforge/config.json` and confirmed generation succeeds.
  Only that closes the two defects. A human interaction pass; see `STATUS.md` "Waiting on
  you".
- **PF-065's install-layout half** — a real `install.sh` that writes a version-matched
  runtime + `.env` and has the plugin prefer it over a stale one. Out of scope for ADR-032;
  stays with PF-065.
- Clean-checkout Release smoke; supported-platform matrix; toolchain-range pins; a staged
  release artifact with licenses / hashes / provenance; a defined release-acceptance gate
  (`PLUGIN_HEALTH_PLAN.md` P1.1–P1.5).
- The Codex `feat/recommendation-mvp` branch edits the same `INTERFACE.md` wire contract —
  reconciliation is **ADR-033** (accepted with conditions 2026-08-31); the branch merges
  after the picker (PR #43) and a rebase.

## Done when

A clean machine can build or install the documented artifact; generation works from the
packaged runtime path with at least one supported provider; every public compatibility
choice is documented and versioned.

## Next action

Land the picker (PR #43), then a human runs the PF-071 launcher-DAW repro. In parallel: a
real `install.sh` for PF-065's install-layout half.
