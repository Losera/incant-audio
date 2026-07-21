# Prototype Test Plan — human-in-the-loop + automated artifacts

Two halves: **Part A** is the scripted human test you run yourself (P6, the prototype finish
line — needs your ears, your display, your API key). **Part B** is the automated artifact
pipeline Claude runs (no API spend): rendered sounds, artwork per sound, and UI screenshots —
tangible outputs that double as regression evidence and demo material.

---

## Part A — Human test protocol (you drive)

### Setup (once)

```bash
cd host && cmake --build build -- -j$(nproc)
python llm/providers.py --check all      # pre-flight: which provider is live?
./build/PluginForgeHost_artefacts/Debug/Standalone/"PluginForge Host"
```

**No API key needs exporting.** Since ADR-012 (2026-07-21) the provider is selected by
`PLUGINFORGE_PROVIDER` in `PluginForge/.env`, and `generate.py` loads that file by absolute
path at import — so the plugin inherits it through `juce::ChildProcess` regardless of the
working directory it was launched from. Only the *selected* provider's key is needed;
`ollama` needs none. This block previously told you to `export ANTHROPIC_API_KEY`, which is
now both unnecessary and wrong: anthropic is the paid provider and is refused unless
`PLUGINFORGE_ALLOW_PAID=1`.

Optional overrides: `PLUGINFORGE_LLM_SCRIPT=/path/to/generate.py`,
`PLUGINFORGE_PYTHON=/path/to/python3`.

**Free-tier budget — read this before planning a session.** The binding Gemini free-tier
limit is **requests per DAY per model**, not per minute, and it is small: measured
2026-07-21, `gemini-3.6-flash` reported `quotaValue: 20`. One Generate click can spend up to
3 (the retry loop), so a day's budget is roughly **7 clicks, worst case**.

When you hit it, the plugin shows `LLM error: ... resources exhausted` and the API replies
`RESOURCE_EXHAUSTED` with a `retryDelay` — **ignore that delay, it's boilerplate**; a daily
quota does not refill in 28 seconds. Check which quota actually tripped:

```bash
python llm/providers.py --check all
```

Two ways out, in order of preference:
1. **Switch provider to `groq`** — the quota is ~14,400 requests/day, enough for a whole
   battery plus retries, and it keeps one model across the run so results stay comparable.
2. **Switch model** — the quota is `PerProjectPerModel`, so another model has its own
   bucket (`PLUGINFORGE_MODEL=gemini-3.5-flash`). Verified working 2026-07-21:
   `gemini-3.5-flash`, `gemini-3.5-flash-lite`. Also exhausted: `gemini-2.0-flash`.
   **Caveat:** changing model mid-battery makes the run a mix of two generators, which
   confounds exactly what the battery measures. Prefer option 1 for a real session.

Feed the plugin audio: in the Standalone's options button (top-left), pick your input device —
or route a media player through it via `pw-link`/qpwgraph (PipeWire). The meter shows
*post-DSP* output; before any DSP is compiled, `processBlock` early-outs and the input buffer
passes through untouched, so the meter shows your dry input. **Meter moving = audio path
alive**, with or without a compiled patch.

### Test script — run these in order

| # | Action | Expect |
|---|--------|--------|
| 1 | Launch app | Status: "Ready." — no "generate.py not found" error |
| 2 | Play audio through the plugin | Level meter dances (dry passthrough) |
| 3 | Prompt: `a warm low-pass filter with a cutoff knob` → Generate | "Generating…" → "JIT compiling: …" → **"Ready — DSP live, N params mapped"** |
| 4 | Listen | Highs audibly rolled off vs step 2 |
| 5 | Wiggle the first mapped macro slider (DAW: automation lane `macro_0`; Standalone: none visible — skip or test in a DAW) | Cutoff sweeps audibly; **no** `setParamValue … not found` spam in the terminal |
| 6 | Prompt: `an aggressive distortion with drive and output level` → Generate | New "Ready — DSP live" with different param count; sound switches with **no** click/dropout longer than a block |
| 7 | Prompt: `nonsense that is not audio: recite a poem` → Generate | Either a valid (if odd) DSP, or a clean "LLM error:"/"Faust compile error:" label — never a hang or crash |
| 8 | Kill network (or unset key), Generate | Clean error label within 120s (subprocess timeout) — button re-enables |
| 9 | VST3 in a DAW (optional): load `build/…/VST3/PluginForge Host.vst3` | Same flow; 64 `macro_*` automation lanes visible; lane 0..N-1 control the live patch |

### Pass criteria

Steps 3–6 all green = the prototype works end-to-end. Log the run (date, prompts used, any
step that deviated) in `docs/collaboration_log.md` — that entry is the prototype's birth
certificate.

---

## Part B — Automated artifact pipeline (Claude runs; zero API spend)

Everything lands in `artifacts/` (gitignored size permitting; curate what to commit).

| Stage | Tool | Output |
|---|---|---|
| 1. Test signal | `tools/make_test_signal.py` (numpy) | `artifacts/audio/input_testsignal.wav` — 6s: sine sweep 60Hz→8kHz + rhythmic noise bursts, stereo, 44.1k/16-bit — chosen so filters, dynamics and modulation are all audible/visible |
| 2. Rendered sounds | `faust2sndfile examples/*.dsp` | `artifacts/audio/<name>_processed.wav` ×4 (chorus, compressor, gain, lowpass) — each example patch processing the test signal |
| 3. Artwork | `tools/make_artwork.py` (matplotlib) | `artifacts/images/<name>_wave.png` (waveform) + `<name>_spec.png` (spectrogram) per rendered wav — dark, brand-neutral styling; the spectrograms are the "visually appealing" money shots (the lowpass literally shaves the image's top off) |
| 4. UI screenshot | `tools/screenshot_ui.sh` (**you run it** — see note) | `artifacts/images/plugin_ui.png` — the editor with meter/status visible |

**Screenshot note:** `grim` captures raw screen pixels, so automating this on a live desktop
kept catching other windows. Run `tools/screenshot_ui.sh` yourself with the app open and on
top — ideally mid-test with audio playing, so the meter is lit in the shot.

Regression value: stage 2 re-run after any FaustEngine/prompt change and diffed (file size /
spectral stats) catches "compiles but sounds wrong" — the class of bug no current test sees.

### Audio-reactive UI (implemented with this plan)

`PluginForgeProcessor` publishes per-block output peak through `std::atomic<float>
outputLevel` (relaxed store in `processBlock` — RT-safe, no allocation); the editor polls it
on a 30Hz `juce::Timer` and paints a decaying peak meter. Future candidates once the
prototype is signed off: spectrum strip behind the prompt box, param-slot activity glow —
same atomic-publish pattern, listed here so they inherit it rather than invent their own.
