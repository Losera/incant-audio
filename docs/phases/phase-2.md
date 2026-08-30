# Phase 2 — Real-host proof

**Status:** Mostly closed · **Last reviewed:** 2026-08-30 (main `55eda8c`)

Part of the six-phase plan — see [`README.md`](README.md). Defers to `STATUS.md` and
`docs/BUGS.md`.

## What this phase proves

Confidence that the advertised workflow holds inside a real DAW, not only in the project's
(extensive) test harnesses.

## Where it stands

The largest gap — "has never been generated or played inside an interactive DAW" — closed
2026-08-28 in session 017 WP6 (REAPER). One hosted-in-a-modular-rack pass and a human
listening pass remain.

## Evidence on record

- REAPER, both plugins in one project: the effect target audibly filtered and reverbed a
  looping signal; the synth target generated instrument patches
  (`docs/sessions/017-phase2-interactive-host.md`, §9).
- A human pressed real QWERTY keys and heard notes — the OS→JUCE keypress hop end to end,
  which had only ever had a static-contract test.
- `pluginval --strictness 5` → SUCCESS for both plugins across seeds.
- Carla `carla-discovery-native` → SUCCESS for both — independent of JUCE's own tooling.
- Known-provenance VST3 builds; a Standalone effects listening pass on record.

## Open work

- Interactive Carla Rack / Patchbay pass: generate while hosted, play MIDI, process real
  audio, move controls, automate, save/reopen, close editor and host. Needs JACK
  (`pipewire-jack`) on the machine (`PLUGIN_HEALTH_PLAN.md` P0.4).
- A human listening pass on the WP6 patches — whether they *sound like what was asked for*
  is not delegable to the render oracle (`STATUS.md`, "Waiting on you").
- Complete the synthetic-keypress round trip on a supported display environment; keep the
  static contract test as the cheaper layer (`PLUGIN_HEALTH_PLAN.md` P1.7).

## Done when

Both targets load interactively in a supported host and survive generate / refine /
automate / save-reopen / teardown with audio intact, and a listening pass is recorded.

## Next action

Install `pipewire-jack`, load both plugins in a Carla rack, run the hosted
generate-play-automate-save-reopen sequence, and do the listening pass on the resulting
patches.
