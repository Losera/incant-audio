# PF-074 repro — resonance/cutoff sweep followed by permanent silence

Captured 2026-10-12 from `~/PluginForge/logs/prompts.jsonl`, a live REAPER session. First
captured repro for `docs/BUGS.md` PF-074 ("a generated instrument patch produced NaN/Inf
during normal play"), open since 2026-08-28. **Result: every internal hypothesis this repro
was tested against was falsified.** See the summary at the bottom before re-deriving any of
this — the obvious-looking root cause (a `Q=0` divide-by-zero) does NOT reproduce through
the real host path, and this took building and running the actual production code to find
out, not reading the source.

Provider: `groq`, model `openai/gpt-oss-120b`. Both generations reported `success: true` in
the log — that field is `llm/generate.py`'s Python-side Faust validation, not the libfaust
JIT compile inside the host; the two can disagree, and it was checked directly (see below).

## Timeline

| time (UTC) | prompt | file | outcome in REAPER |
|---|---|---|---|
| 19:48:30 | "create a tu tu neurotic style bass synth by the help" | `neuro-bass-worked.dsp` | played correctly, then user raised Resonance and moved Cutoff/Attack → went permanently silent, unrecoverable by restoring the knobs |
| 19:50:54 | same prompt, regenerated with **New** mode | `neuro-bass-regen-silent.dsp` | silent from the start, in the same session |

Both patches declare `Resonance` with a **minimum of 0** (`fi.resonlp`'s `a1 = 1/Q`,
`/usr/share/faust/filters.lib:2185-2193`, an unprotected direct-form biquad via `tf2s`/`tf2`
— `Q == 0` is an immediate NaN). That is the mechanism this repro was built to test.

## Reproducing

```sh
# 1. Bare DSP, no smoothing, no host — isolates the raw Faust math.
faust -lang cpp -cn mydsp -o worked.cpp neuro-bass-worked.dsp
g++ -std=c++17 -O2 -I/usr/include/faust -DDSP_SOURCE_FILE='"worked.cpp"' \
    sweep_driver.cpp -o sweep_worked
./sweep_worked --param Resonance --from 0 --blocks 5 --blocksize 256   # instant NaN

# 2. Real host path. Needs an existing `build/` configured per CLAUDE.md
# ("JUCE 7.0.9 at $HOME/JUCE") with `ninja OfflineRenderTest` already run once,
# so its .o files exist to link against -- host_repro.cpp does not add a CMake
# target, it links straight against the build's existing object files. See the
# compile/link commands this session used (captured via
# `ninja -t commands OfflineRenderTest` against the real target) -- copy its
# flags, swap `tests/OfflineRenderTest.cpp.o` for `host_repro.cpp.o`.
./host_repro neuro-bass-worked.dsp neuro-bass-regen-silent.dsp
```

## Results — every hypothesis tested, falsified

| Hypothesis | Bare DSP (no smoothing) | Real host (`apvts` → `pushToFaust` → `si.smoo`) |
|---|---|---|
| **H2** — `Resonance = 0` is an instant NaN | **Confirmed.** Non-finite on sample 0, every time, both patches. | **Falsified.** A single jump to 0, a 1.6s *sustained dwell* at 0, and a realistic ~2s continuous drag through 0 — none produced a non-finite sample. `si.smoo`'s smoother never lets the raw zero reach the filter on any timescale tried. |
| **H1** — fast Cutoff/Resonance sweep at high Q diverges on its own | Falsified. 10-block and 2-block (single-sample) sweeps across the full declared range, resonance at max, stayed finite. | Falsified. The user's actual gesture (Resonance 0.7→5, Cutoff swept both directions, ~2s) stayed finite throughout. `OutputGuard`'s limiter did engage (peak pinned at exactly `kCeiling=0.966051`) — loud, bounded, correctly in `Report` policy for an instrument (ADR-020) — never muted. |
| **H3/H4** — "New" regeneration fails to reset the guard or mis-seeds a slot | n/a (gen4's own DSP confirmed healthy at its declared defaults, no NaN/runaway) | **Falsified.** Reproduced the exact sequence — load A, (fail to) break it, press New into B — and B compiled, `isInstrument()==true`, unmuted, peak 0.9655. |

**What this means:** the one latent defect (`Resonance` reaching a literal, undamped `0.0f`)
is real and worth closing defensively regardless, but it is not reachable through normal
knob automation at any speed this repro could produce, and it does not explain this
session's silence. Every hypothesis that placed the fault inside PluginForge's own
DSP/parameter/guard path is now ruled out against the *exact* captured patches and the
*exact* reported gesture — using the real production code, not a reimplementation.

**Leading remaining explanation:** something outside this codebase — REAPER session-local
state (track arm/monitor routing, audio device). `docs/sessions/017-phase2-interactive-host.md:366`
already recorded a precedent of exactly this shape: *"Synth on a dedicated track: no sound
— not a plugin bug — REAPER track arm/monitor config."*

**Not yet done, and the actual blocker:** no `juce::FileLogger` is installed anywhere in the
plugin, so the live session that produced this left no trace of what the host itself saw —
offline replay is now exhausted as a technique for this incident. Next step is live capture:
install a `FileLogger`, arm `PLUGINFORGE_COCKPIT_STATE` (`PluginEditor.cpp:228-230`, already
built, never switched on), and reproduce live. Full narrative:
`~/.claude/plans/i-was-trying-out-steady-graham.md` (Phase 1).

## Files here

- `neuro-bass-worked.dsp` / `neuro-bass-regen-silent.dsp` — the two captured patches.
- `sweep_driver.cpp` — bare-DSP driver (MapUI, no host). Compiles against whatever
  `-DDSP_SOURCE_FILE` points at; see command above.
- `host_repro.cpp` — real-host driver, linked against `PluginForgeProcessor` itself via the
  build's existing `OfflineRenderTest` object files. Not a CMake target on purpose — this is
  diagnosis scaffolding, not a standing test; promote it (or fold its sweep assertion into
  `OfflineRenderTest.cpp`/`bench/render_oracle.py`) only if the project decides the knob-sweep
  gate from the diagnosis plan's Phase 5 is worth keeping.
