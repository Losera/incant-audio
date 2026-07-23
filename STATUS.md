# PluginForge — Status  (2026-07-23)

Rewritten each session per COLLABORATION.md §5. **Now overseer-maintained** — a fleet of
parallel sessions is active (see `docs/FLEET.md`); individual sessions send 5-line change-reports
and the overseer consolidates here, so this file has one writer and never merge-conflicts.
Narrative history lives in git.

---

## Works — and how we know

- **Full build, all three targets.** `PluginForgeHost`, `_Standalone`, `_VST3` compile and
  link clean. Confirmed 2026-07-18.
- **JIT compile and live DSP swap.** ThreadSanitizer over
  `host/tests/ParamPoolConcurrencyTest.cpp`: PASS, zero races.
- **Parameters are denormalized into real units.** *(was Broken #1/#4; resolved by `efbb5a5`.)*
  New `host/Source/ParamMap.h` (180 lines) converts slot 0–1 ↔ Faust zone (Hz/dB/ms) with
  log/exp/linear curves and discrete/menu quantization; `ParamCapture`
  (`FaustEngine.cpp`) records `scale`/`unit`/`isMenu`/`min`/`max`/`step`/`zone` per param.
  Covered by `host/tests/ParamMapTest.cpp` (212 lines). **Not yet verified by ear** — see below.
- **The parameter path is RT-safe.** *(was Broken #4; folded into `efbb5a5`.)* `pushToFaust`
  writes cached `FAUSTFLOAT*` zone pointers directly — no `std::map` lookups, no `MapUI`, no
  `fprintf` on the audio thread. `check_rt_safety.py` still does not scope `pushToFaust` (see
  "Assumed").
- **Compile thread is owned and joined.** *(was Broken #3; resolved by `d10f59e`.)*
  `FaustEngine` runs a persistent worker (`workerLoop`/`shutdown`); `~PluginForgeProcessor`
  calls `faustEngine.shutdown()` first. No detached thread capturing `this` at teardown.
- **Output safety net.** *(new, `91a5a89`.)* `OutputGuard.{h,cpp}` catches NaN/Inf, DC-blocks,
  soft-limits, and mutes runaway output on the audio thread. Covered by `OutputGuardTest.cpp`.
- **End-to-end generation on a free provider.** `python llm/generate.py --prompt "..."` returns
  Faust the compiler accepts, via `gemini-3.6-flash` (first-try and stderr-retry paths).
  Evidence the *pipeline* works; predates the prompt rewrite, so not evidence of a success rate.
- **System prompt grounded in the real stdlib.** Every `ns.func` in
  `llm/prompts/system_prompt.txt` resolves against installed `/usr/share/faust/*.lib`; all five
  few-shot examples compile. Enforced by `tests/test_prompt_stdlib.py` + `check_prompt_invariants.py`.
- **One prompt, one measurement.** `generate.py`, `run_benchmark.py`, `run_efficacy_study.py`
  all load the same prompt file.
- **Provider registry.** Five providers, three adapters, no added dependencies.
- **Python test suite.** 239 pass, 10 integration-deselected, ~1.4s (2026-07-21).
- **State persistence.** *(was Broken #1; landed `c34bbb6`, Backend/S1.)* Versioned
  ValueTree→XML blob (schemaVersion=1: Faust source + prompt + 64 APVTS values + SlotLabels);
  setState restores values then recompiles; unknown/corrupt/foreign blobs ignored.
  `StatePersistenceTest` round-trips through two processors 13/13, ASan/UBSan clean; JUCE headers
  cited (`juce_AudioProcessorValueTreeState.h:375-395`, `juce_AudioProcessor.h:1306-1312`).
  **NOTE:** the *format* is a §2 trigger-3 contract; S1 reports it was signed off via its
  plan-mode approval. The overseer cannot independently verify that (separate session), so the
  human is asked to confirm — see "Waiting on you." Retained metadata is `metaMutex`-guarded,
  never touched on the audio thread.
- **PluginEditor split into panels.** *(Plugin-UX/S3, Task 0.)* `PromptPanel` / `CodeEditorPanel`
  (stub) / `ParamGridPanel` extracted; shell wires all three; CMake updated.
  **CAVEAT:** uncommitted working-tree state, build not yet overseer-verified.

---

## Broken — ranked

**1. The editor exposes only 8 of 64 parameters.** *(Plugin-UX/S3)*
`MAX_KNOBS = 8` (`PluginEditor.h:41`). The value-loss half is already fixed (all 64 slots push
to Faust), but patches with >8 controls have no on-screen control for the remainder, and
toggle-kind params render as rotaries. Deterministic auto-layout is specified in
`docs/ui_design_plan.md` §3; `FaustEngine.h:23-30` already exposes the per-param `Kind` enum.

---

## Assumed, never checked

- **No generated plugin has ever been listened to.** The denormalization fix (#1/#4 above) is
  verified by unit test and by construction, **not by ear**. The P6 audible battery
  (`docs/prototype_test_plan.md` Part A, `docs/p6_test_battery.md`) has never run. This is now
  the fastest way to find whatever the old denormalization bug was masking.
- **Every benchmark number on record is void.** Measured against the deleted
  `bench/prompts/system_faust.txt`, which taught three functions that do not exist.
  `bench/results/.prompt_baseline.json` (0.88) has **not** been overwritten (gated) but describes
  nothing that exists.
- **The prompt rewrite is unmeasured.** Verified *correct* (references resolve, examples
  compile), not verified *better*.
- **The efficacy pilot generalizes to nothing.** N=50, one model, the old prompt, two of five
  categories. The full 125-prompt run has never produced valid data.
- **No cross-model comparison exists.** ADR-008 "Under evaluation" since 2026-04-29.
- **Semantic fidelity is unmeasured.** Every metric is compile rate; the `--judge` rubric is off
  by default and has never run.
- **No real user prompt has ever been recorded.** `generate.py` logs nothing.
- **`check_rt_safety.py` scopes exactly two functions** and cannot follow a call graph;
  `ParamPool::pushToFaust` (now on the audio thread) is not covered.
- **CI has never run green with the new prompt steps.** The `build-host` job runs
  `tools/gen_stdlib_block.py --check` and the prompt tests and carries five pre-existing
  `TODO: VERIFY` items about Ubuntu Faust packaging, none checkable from the Arch dev box.

---

## Next three things

1. **Build-verify + commit the PluginEditor split** (S3 uncommitted; S4 to build-verify) so the
   fleet works from a clean recorded baseline. Then S2 fleshes out its panels (already unblocked).
2. **Run the P6 listening pass** now that params denormalize and plugins persist — 15 minutes
   with a filter patch and a delay patch. First real evidence a generated plugin sounds like its
   words. (human, script authored by S4; use `groq`, not Gemini's ~20/day quota)
3. **Re-establish a benchmark baseline** — the old one is void. Overwriting
   `.prompt_baseline.json` is a §2 trigger-1 act; needs human authorization. (S4 → overseer → human)

---

## Waiting on you

- **Confirm the persisted-state format** — a §2 trigger-3 contract landed in `c34bbb6`. S1
  reports it was approved via plan-mode; the overseer couldn't see that approval, so please
  confirm you knowingly signed off. Format: schemaVersion=1 ValueTree→XML, Faust source + prompt
  as attributes, verbatim `<STATE>`, `<SlotLabels>` hint. Versioned/forward-defensive, so
  amending it now while v1 is the only blob in the wild is cheap.
- **DECIDE: add a "bring-your-own-LLM" mode? (§2 trigger-2, architectural)** S7's landscape
  (`docs/competitive_research.md`) finds competitor **Amorph** ships our exact thesis (in-DAW
  compile + auto-UI) free today on Cmajor, with an *external* LLM — $0 inference while we fight
  free-tier quotas. Proposed optional paste-external-code path alongside the integrated
  generator. Trade-off: neutralizes our quota/cost exposure and works offline, but dilutes the
  closed-loop auto-retry moat. No lane scopes this until you decide. (FLEET request #6.)
- **A listening pass** — the P6 battery, script coming from the Testing session.
- **Authorize the benchmark re-run** (overwrites the void baseline; spends free-tier quota — use
  groq for the 25-prompt run).
