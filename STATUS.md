# PluginForge — Status  (2026-07-21)

Rewritten each session per COLLABORATION.md §5. Narrative history lives in git.

---

## Works — and how we know

- **Full build, all three targets.** `PluginForgeHost`, `_Standalone`, `_VST3` compile and
  link clean. Confirmed 2026-07-18 (first time in project history).
- **JIT compile and live DSP swap.** Verified by a ThreadSanitizer run over
  `host/tests/ParamPoolConcurrencyTest.cpp`: PASS, zero races, after the 2026-07-19 fix
  that moved the compile callback ahead of `ready=true`.
- **End-to-end generation on a free provider.** `python llm/generate.py --prompt "..."`
  returns Faust the compiler accepts, via `gemini-3.6-flash`. Verified 2026-07-21 on both
  the first-try and stderr-retry paths — but note this predates the prompt rewrite below,
  so it is evidence the *pipeline* works, not evidence of the current success rate.
- **The system prompt is grounded in the real stdlib.** Every `ns.func` reference in
  `llm/prompts/system_prompt.txt` resolves against the installed
  `/usr/share/faust/*.lib`, and all five few-shot examples compile. Enforced by
  `tests/test_prompt_stdlib.py` (5 tests) and `.claude/hooks/check_prompt_invariants.py`,
  both verified to fail on the pre-fix content.
- **One prompt, one measurement.** `generate.py`, `run_benchmark.py`, and
  `run_efficacy_study.py` all load the same file; confirmed by importing all three and
  comparing.
- **Provider registry.** Five providers, three adapters, no added dependencies.
- **Python test suite.** 239 pass, 10 integration-deselected, 1.38s. Executed 2026-07-21.

---

## Broken — ranked

**1. Parameter values are never denormalized. Generated plugins do not work.** *(next up)*
All 64 APVTS slots are hardcoded 0.0–1.0 (`PluginProcessor.cpp:28`). `ParamPool::remap()`
stores only the label and discards min/max/step (`ParamPool.cpp:36-39`). `pushToFaust()`
pushes the raw 0–1 value (`ParamPool.cpp:74`), and `MapUI::setParamValue` does
`*zone = value` with no clamping and no range mapping (`/usr/include/faust/gui/MapUI.h:150`).
A generated 20–20000 Hz cutoff is therefore set below 1 Hz regardless of knob position.
Only coincidentally-0–1 params (mix, depth, feedback) work.

**2. No state persistence. Saving a DAW session discards the plugin.** *(format decided,
not yet implemented)*
`getStateInformation`/`setStateInformation` are empty stubs (`PluginProcessor.h:30-31`).
Agreed format: Faust source + originating prompt + APVTS values, async JIT on load.

**3. Use-after-free on shutdown.** *(approach decided, not yet implemented)*
`FaustEngine::compile()` detaches a thread capturing `this` (`FaustEngine.cpp:93`);
`~FaustEngine()` has no join, no flag, no latch. Agreed fix: atomic abort flag plus a
bounded join, thread becomes a joinable member.

**4. RT-safety hazard in the parameter path.**
`MapUI::setParamValue` calls `fprintf(stderr, …)` on a label miss (`MapUI.h:170`) — a lock
and syscall inside `processBlock`. `pushToFaust` also does up to 64
`std::map<std::string>` lookups per block. Caching resolved `FAUSTFLOAT*` zone pointers in
`remap()` fixes both; it also folds naturally into the fix for #1.

**5. The editor exposes 8 of 64 parameters.** `MAX_KNOBS = 8`; patches with more controls
silently lose UI access to the remainder.

---

## Assumed, never checked

- **No generated plugin has ever been listened to.** The P6 audible battery has been
  deferred repeatedly. Given defect #1 it would have failed on the first patch.
- **Every benchmark number on record is now void.** Two independent reasons, either
  sufficient: the numbers were measured against the deleted `bench/prompts/system_faust.txt`,
  never the production prompt; and that prompt taught three functions that do not exist.
  `bench/results/.prompt_baseline.json` (0.88) has **not** been overwritten — overwriting a
  measurement is gated — but it no longer describes anything that exists.
- **The prompt rewrite is unmeasured.** It is verified *correct* (references resolve,
  examples compile) but not verified *better*. The claim that grounding improves first-try
  compile rate is a hypothesis until a run says so.
- **The efficacy pilot generalizes to nothing.** N=50, one model, the old prompt, two of
  five categories. The full 125-prompt run has never produced valid data.
- **No cross-model comparison exists.** ADR-008 has been "Under evaluation" since 2026-04-29.
- **Semantic fidelity is unmeasured.** Every metric is compile rate. The `--judge` rubric
  is off by default and has never been run.
- **No real user prompt has ever been recorded.** `generate.py` logs nothing.
- **`check_rt_safety.py` scopes exactly two functions** and cannot follow a call graph.
  `ParamPool::pushToFaust` runs on the audio thread and is not covered.
- **CI has never run green with the new prompt steps.** The `build-host` job now runs
  `tools/gen_stdlib_block.py --check` and the prompt tests; that job also carries five
  pre-existing `TODO: VERIFY` items about the Ubuntu Faust packaging, none checkable from
  the Arch dev box. Whether Ubuntu's Faust exports the same 59 curated names as Arch's is
  genuinely unknown — a mismatch will fail the new check, correctly.
- **Anthropic fence-stripping is off** (`providers.py:150`) to preserve a baseline that the
  bullet above just voided. The reason for keeping it off is gone.

---

## Next three things

1. **Denormalize parameters** (broken #1), folding in the zone-pointer cache (#4). Tier 2:
   needs a cited read of the JUCE parameter API and a test that a swept slot moves the
   Faust zone. Unblocks every audible test in the project.
2. **Implement state persistence** (broken #2) — format agreed, no blockers.
3. **Fix the compile-thread shutdown race** (broken #3) — approach agreed, no blockers.

---

## Waiting on you

- **A listening pass, once #1 lands.** Fifteen minutes with a filter patch and a delay
  patch is the fastest way to find whatever defect #1 has been masking.
- **Authorize a benchmark re-run to establish a new baseline.** The old one is void. This
  spends free-tier quota (Gemini is 20 requests/day; use groq for the 25-prompt run), and
  overwriting `.prompt_baseline.json` is a §2 trigger-1 act, so I have not touched it.
  This is also the first real measurement of whether the prompt fix helped.
