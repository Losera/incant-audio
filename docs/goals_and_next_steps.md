# PluginForge — Goals & Next Steps

**Status refresh 2026-07-16:** the "Day 2 — Core JIT Pipeline" table below is done
(all four rows implemented; see CLAUDE.md "Current status" for the live picture and
docs/next_steps.md for per-task annotations). Still open from "Days 3–5": the
`activeLabels` thread-safety decision, and surfacing Faust compile errors (not just
LLM-generation errors) in the UI. Left the rest of this doc as originally written.

## Project Goal

Ship a single VST3/AU plugin binary that accepts a natural-language description of a DSP effect, synthesizes Faust DSL via LLM, compiles it to native code via libfaust/LLVM JIT, and exposes the generated parameters to the host DAW — all without restarting the plugin.

---

## Day 2 — Core JIT Pipeline (Immediate)

These are the blocking stubs that prevent any audio from flowing through generated DSP.

| Task | File | What to do |
|------|------|-----------|
| Wire libfaust JIT | `host/Source/FaustEngine.cpp` | Replace stub with `createDSPFactoryFromString()`, launch background compile thread, atomic-swap the live `llvm_dsp*` |
| DSP param push | `host/Source/ParamPool.cpp` | Implement `pushToFaust()` — iterate active slots, call `engine.setParamValue(label, value)` each block |
| Editor → LLM call | `host/Source/PluginEditor.cpp` | Wire `generateButton` onClick to call Python layer (IPC pipe or embedded subprocess) and feed result to `loadFaustCode()` |
| Thread-safe DSP swap | `host/Source/FaustEngine.h` | Add `std::atomic<llvm_dsp*> liveDsp` + `llvm_dsp_factory*`; audio thread reads atomically, compile thread writes |

**Success criterion:** Load the plugin in a DAW, type "a low-pass filter with cutoff", click Generate, and hear filtered audio through the plugin within 5 seconds.

---

## Days 3–5 — Hardening the Pipeline

Once audio flows, focus on correctness and resilience.

- **Parameter mapping**: After compile, call `remap()` with the new `ParamList`; expose correct labels in DAW parameter list (workaround JUCE rename limitation by using display names).
- **Error UI**: Surface Faust compiler errors in `statusLabel` rather than silently failing.
- **Compile cancellation**: If user triggers a second generate before the first compile finishes, cancel or queue correctly.
- **Sample-rate / block-size changes**: Ensure `FaustEngine::prepare()` re-initialises the live DSP object if one exists.
- **Resource cleanup**: Delete old `llvm_dsp_factory` after swap to prevent LLVM memory leak.

---

## Week 2 — Developer Experience

- **pytest CI**: Add GitHub Actions workflow running `pytest tests/ -m "not integration"` on every push.
- **IPC contract**: Define the JSON schema for the C++ ↔ Python boundary (stdin/stdout pipe or local socket) and document it in `docs/`.
- **Logging**: Replace `juce::Logger::writeToLog` stubs with structured log levels (DEBUG/INFO/ERROR) written to a rotating file the user can tail.
- **Faust stdlib bundling**: Populate `host/Resources/faust_stdlib/` so the plugin is self-contained and does not depend on a system Faust install.

---

## Week 3–4 — Quality & Polish

- **Parameter smoothing**: Add per-slot `juce::SmoothedValue<float>` to eliminate zipper noise on parameter changes.
- **Preset system**: Serialize prompt + generated Faust code into plugin state so DAW recall works.
- **LLM prompt library**: Extend `llm/prompts/system_prompt.txt` with more few-shot examples (reverb, EQ, distortion, pitch-shift) to improve first-attempt success rate.
- **Offline fallback**: Cache last-valid Faust code so the plugin loads its previous DSP even without network access.
- **Performance baseline**: Measure CPU headroom with a complex generated patch at 44.1 kHz / 128 samples buffer; target < 5 % single-core load.

---

## Long-Term

- **AU support**: Test and validate Apple Audio Unit format in Logic Pro / GarageBand.
- **MIDI-to-param**: Allow the LLM to optionally emit MIDI-controllable parameters.
- **Multi-block Faust**: Explore Faust's `soundfile` primitive for LLM-generated wavetable oscillators.
- **Plugin manager UI**: Standalone app for browsing, naming, and re-generating saved patches.
- **Model selection**: Allow users to pick the LLM model (Opus / Sonnet / Haiku) with a latency/quality tradeoff UI.

---

## Known Constraints & Risks

| Risk | Mitigation |
|------|-----------|
| JUCE cannot rename parameters at runtime | Use slot display names only; DAW automation lanes keep stable IDs (`macro_0..63`) |
| libfaust LLVM JIT compile time (~1–3 s) | Show spinner in `statusLabel`; compile on background thread, never block audio thread |
| LLM may generate non-compiling Faust | Retry loop (3 attempts) with stderr fed back; surface final error in UI |
| Faust stdlib not bundled yet | Document `faust_stdlib/` population step in build instructions before shipping |
| API key required at runtime | Document `.env` setup; consider in-plugin key entry field as UX fallback |
