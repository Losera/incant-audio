# Session 008 — Vision Architecture Push

**Date:** 2026-08-06
**Status:** Active

## Decisions Made

| Decision | Choice | ADR |
|----------|--------|-----|
| Browser model | Dev-cockpit mirror (localhost web app mirrors live screenshots + state) | ADR-025 |
| Visual iteration | UI IR + LLM visual spec (reopen ADR-022 narrowly) | ADR-024 (proposed) |
| Export | Repo-first, then stripped binary | ADR-023 (proposed) |
| Agent fleet | Lane-parallel, overseer-led (one session default) | No ADR change |
| Plugin types | Instruments + effects now; samplers/drums deferred | ADR-026 (future) |
| Sample audition | Real-time dropdown through live DSP | New feature |

## Phases Executed

### Phase 0 — Dev-Cockpit ✅
- `dev-cockpit/server.py` — localhost HTTP server (file polling, ~200 LOC)
- `dev-cockpit/static/index.html` — iterate surface with live screenshot + state panels
- `host/Source/PluginEditor.{h,cpp}` — `writeCockpitState()` at ~10 Hz from 30 Hz timer
- `.claude/skills/cockpit/SKILL.md` — `/cockpit` skill
- ADR-025 added to `docs/decisions.md`
- Screenshot fallback: returns SVG placeholder when Hyprland window not found

### Phase 1 — UI IR v1 ✅
- `host/Source/UiIr.h` — renderer-agnostic schema (parse/toVar, schema 1)
- `host/Source/ParamGridPanel.{h,cpp}` — `applyUiIr()` + `layoutSectioned()`
- Controls not in IR appended to trailing "Parameters" section
- Section headings, column spans, archetype/tokens fields
- No prompt changes (Phase 1a: hand-authored IR only)

### Phase 2 — Export Repo-First ⛔ gated (PF-053)
- `tools/export_repo.py` — generates CMake + JUCE + Faust stub project
- `tools/export/CMakeLists.txt.j2` — Jinja2 template
- `.claude/skills/export/SKILL.md` — `/export` skill
- ADR-023 added to `docs/decisions.md`

### Phase 3 — Sample Audition ✅
- `artifacts/samples/` — reference WAVs (sine_sweep, drum_loop, guitar_chord)
- `host/Source/PluginProcessor.{h,cpp}` — `loadAuditionSample()`, `setAuditionActive()`, audition buffer in `processBlock`
- `host/Source/PluginEditor.{h,cpp}` — `auditionSelector` ComboBox with sample discovery
- Samples injected into input bus at audio-thread rate, looping with atomic position

### Phase 4 — Samplers/Drums Exploration
- Faust `soundfiles.lib` has `so.player`, `so.loop`, `so.loop_speed`, `so.play_interp`
- These are genuine sample-playback primitives but require the soundfile at compile time
- For a sampler: Option C (recommended) — pre-built C++ sample layer + Faust for filtering/effects
- ADR-026 not yet drafted (exploration only, implementation deferred)

## Verification Status

| Phase | Build | check.sh fast | Manual |
|-------|-------|--------------|--------|
| 0 | ✅ | ✅ | Pending (cockpit needs running Standalone) |
| 1 | ✅ | ✅ | Pending (need to hand-author reference IR) |
| 2 | ✅ | ✅ | Pending — PF-053 (generated project does not compile; skill-gated) |
| 3 | ✅ | ✅ | Pending (need to test audition in Standalone) |

## iPlug2 Discussion

Question raised: should iPlug2 be considered for UI elements?

Answer: No, stay with JUCE (ADR-003). Reasons:
1. Switching would be a 2-3 month rewrite — entire codebase is JUCE-coupled
2. pluginmaker.ai is likely browser-first (HTML/CSS in webview), not iPlug2 native
3. The UI quality gap is about layout/token systems (ADR-021/022), not the framework
4. Phase 1 UI IR addresses the visual limitation without a framework switch
5. One real iPlug2 advantage: CLAP support — but JUCE's clap-wrapper works fine

## Unverified Claims

- The dev-cockpit screenshot works end-to-end with the Standalone running (needs manual test)
- The audition sample feeds through effects correctly in real time (needs listening pass)
- The UI IR sectioned layout renders correctly with hand-authored IRs (needs visual verification)
- The exported plugin builds and loads in a DAW — **superseded by PF-053**: the generated project does not compile, so this is not merely unverified, it is currently false.
