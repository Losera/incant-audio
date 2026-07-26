# PluginForge — Status  (2026-07-23)

Rewritten each session per COLLABORATION.md §5. **Now overseer-maintained** — a fleet of
parallel sessions is active (see `docs/FLEET.md`); individual sessions send 5-line change-reports
and the overseer consolidates here, so this file has one writer and never merge-conflicts.
Narrative history lives in git.

---

## Works — and how we know

- **The enforcement hooks now actually run, and have been seen blocking.** *(2026-07-25.)*
  They never had. `.claude/settings.json` declared `PreToolUse` at the file root; Claude Code
  requires it under a top-level `"hooks"` key and ignores a wrongly-shaped file **silently**.
  All five hooks were dead while CLAUDE.md, this file and a published service review all
  described the prompt invariant as "hook-enforced". The scripts were correct throughout —
  `check_bash_denylist.py` returned exit 2 by hand; nothing invoked it.
  Proof is behavioural, not declarative: after the fix a real `Bash` call was blocked, twice,
  in the session that made it (hooks apply immediately — no restart, contrary to what
  `.claude/agents/invariant-hook-writer.md` used to claim). `tests/test_control_wiring.py`
  (15 tests) now guards both the shape and the teeth, and found the two retired hooks still
  sitting on disk. **This was the third instance of one pattern** — after PAIR mode and the
  ADR-009 sync hook — of mistaking a declared control for a running one.
- **The RT-safety hook now covers the whole audio path, with a red case per function.**
  *(Closes the scope half of PF-015.)* It scoped `FaustEngine::process` and `processBlock`
  only, while four functions actually run on the audio thread: `ParamPool::pushToFaust`
  moved there with the PF-004 fix (`efbb5a5`) and was never added, and
  `OutputGuard::process` has been there since `91a5a89` and was never added either. So the
  hook's coverage and the real audio path disagreed for weeks while everything in the repo
  described that path as guarded — the same defect as the hooks not running at all, one
  level down. `ANCHOR_RE` now matches all four, `WATCHED_FILE_RE` covers `ParamPool.cpp`
  and `OutputGuard.cpp`, and `tests/test_control_wiring.py` carries a parametrised red case
  per newly-scoped function.
  **Known limitation, not a bug:** it still cannot follow a call graph, so a *fifth*
  function arriving on the audio thread has to be added to that list by hand. The red
  tests catch a scoped function silently losing its teeth; they cannot catch an unscoped
  one appearing.
- **One entry point for every check.** `tools/check.sh fast | full | audio | quota`, cost-ordered
  and cumulative. Verified end to end 2026-07-25: `audio` green, including the four-target JUCE
  build and a clean ThreadSanitizer run. `quota` refuses to spend free-tier requests without
  `--i-authorize-spend`. `tools/check.sh assumed` prints the size of the list below — the one
  number this project should steer by, because writing documentation cannot improve it.
- **Generated DSP is measured as audio, not just as text.** *(Closes the objective half of
  PF-013.)* `bench/render_oracle.py` renders a compiled patch offline (numpy + scipy only, no
  network, no quota) and gates NaN/Inf, silence, DC offset, runaway gain. Over the 25-prompt
  benchmark corpus: **17 of 17 renderable patches produce usable audio.** Calibrated against
  known physics — `fi.resonlp(1000, .707)` measures −3.0 dB at its 1 kHz corner and −30 dB two
  octaves up; a band-pass, high-shelf and 60 Hz notch each show their correct signature.
  **Limit:** zero-input patches (synths — 5 of 25) cannot be rendered; `faust2sndfile` is
  input-file-driven and writes an empty WAV for them. The oracle reports these as
  *unsupported*, never as failures, because conflating "cannot measure" with "is broken" is
  exactly how five billing errors became compile failures (below).
- **CI has run green with the prompt-grounding steps.** *(Corrects PF-016, which claimed it
  never had.)* `gh run list`: green on 2026-07-22 (`29883145844`, the very commit that pointed
  the bench harnesses at the unified prompt) and 2026-07-23 (`30032243641`), both ~4.5 min,
  both including the `build-host` job. The five `TODO: VERIFY` items about Ubuntu Faust
  packaging in that workflow are therefore **answered by those runs**, not still open.
  What is true is that CI is *starved*: `main` ran 17 commits ahead of `origin` before this
  session pushed.
- **The P6 listening battery has run.** *(Corrects PF-008, which claimed no generated plugin had
  ever been listened to — contradicting this file's own P6 section.)* It ran 2026-07-24 with
  human ears: **4 clean, 3 flaky, 7 failures of 14.** That is a bad result, not a missing one,
  and it is recorded above. The open question is now a *second* pass after the reliability
  burst, which is a different claim and is listed under "Waiting on you".
- **Five API billing errors are no longer counted as Faust compile failures.** In the
  2026-07-20 pilot the prompt `generative-05` was refused by the provider in all five tiers —
  no credit on the account, not one token generated — and all five were scored as compile
  failures. `bench/score_efficacy.py::is_transport_error` now partitions them out and
  `print_excluded()` reports the drop unconditionally. Re-scoring reproduces the corrected
  figures: L4 100%, L3 100%, L2 89%, L1 56%, L0 67% (n=9), against the published
  90/90/80/50/60 (n=10). The refusal fell once per tier, so the *shape* of the headline
  non-monotonicity survived — which is precisely why nobody noticed the numbers were wrong.

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
  `fprintf` on the audio thread. `check_rt_safety.py` still does not scope `pushToFaust`
  (PF-015; see "Assumed").
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
- **Python test suite.** 254 passed (240 baseline + 14 new for the BYO-LLM exporter),
  independently re-run by S7 2026-07-23. Several commits have landed since (param-grid
  auto-layout, `onFaustCompileFailure`) without a fresh full-suite count recorded — treat as
  probably-still-green, not verified-green, until someone reruns it.
- **State persistence.** *(was Broken #1; landed `c34bbb6`, Backend/S1.)* Versioned
  ValueTree→XML blob (schemaVersion=1: Faust source + prompt + 64 APVTS values + SlotLabels);
  setState restores values then recompiles; unknown/corrupt/foreign blobs ignored.
  `StatePersistenceTest` round-trips through two processors 13/13, ASan/UBSan clean; JUCE headers
  cited (`juce_AudioProcessorValueTreeState.h:375-395`, `juce_AudioProcessor.h:1306-1312`).
  **NOTE:** the *format* is a §2 trigger-3 contract and is still awaiting human confirmation —
  the overseer's ruling on FLEET req #12 (2026-07-23) settled a roll-call/gate contradiction:
  the Gate table is authoritative, S1's "signed off via plan approval" language was corrected,
  and confirmation is still pending — see "Waiting on you." Retained metadata is
  `metaMutex`-guarded, never touched on the audio thread.
- **PluginEditor split into panels.** *(Plugin-UX/S3, Task 0.)* `PromptPanel` / `CodeEditorPanel`
  (stub) / `ParamGridPanel` extracted; shell wires all three; CMake updated; resizable shell
  (`setResizable(true,true)`, limits 480×360–1400×1200); all four editor-linking targets
  (`PluginForgeHost`, `_Standalone`, `_VST3`, both `ParamPoolTsanTest` + `StatePersistenceTest`)
  build+link clean. Committed `471d045`. Shell's `resized()` wires each panel's bounds.
- **The editor now exposes all 64 parameters, not 8.** *(was Broken #1 / PF-005; fixed
  `2e129cd`, Plugin-UX/S3.)* `ParamGridPanel`'s Wave-1 auto-layout replaces the fixed 8-slider
  array with a kind-aware, N-aware, scrollable grid (`cols≈sqrt(N)`, 2–6 columns) that caps at
  `ParamPool::POOL_SIZE` (64) instead of the old `MAX_KNOBS=8`; toggle-kind params get proper
  widgets instead of rotaries; window height grows dynamically. `MAX_KNOBS` is grep-clean across
  `host/Source/`; build green (`cmake --build host/build --target PluginForgeHost` →
  `ninja: no work to do`). **Not yet confirmed by eye/runtime** — no one has visually checked a
  live patch with >8 params (including a toggle) actually renders correctly. Reopen (PF-005) if
  it misbehaves.
- **BYO-LLM Phase 0 + first half of Phase 1 landed.** *(Backend/S1.)* `llm/export_prompt.py`
  (+112) + `tests/test_export_prompt.py` (+134) on main `0ba4b51`: exports compiled Faust DSP
  param metadata (name/min/max/step/unit/kind) as JSON, reusing `providers.strip_code_fences`
  (lazy import, no fork), verified under a scrubbed env (`env -i`). Phase 1 first half:
  `onFaustCompileFailure(const juce::String&)` landed (`a9c0122`), paired with
  `onFaustCompileSuccess`; the shell has now migrated its call site to it (`a2db6a5`) — the
  transitional `onFaustCompileError` alias S1 kept for build continuity can be dropped by S1
  whenever convenient. S2's error-surface UI (req #8) and S3's mode affordance (req #9) build
  against this callback next.

---

## P6 listening battery — FIRST RUN, 2026-07-24 (groq / gpt-oss-120b)
The first-ever audible validation ran. **4 clean passes, 3 flaky, 7 failures.** Not yet reliable
enough to audition. Full per-prompt table in the plan of record
(`~/.claude/plans/hello-claude-i-d-like-virtual-turtle.md`). Four failure themes drive the burst
below: (1) a 120s timeout cliff on consecutive prompts #11–14 (PF-019); (2) a transient segfault
under rapid failed generations (PF-006 live repro); (3) invalid-Faust generation for whole prompt
classes — ping-pong, stereo routing, unbounded delay, artist-reference (PF-024); (4) cross-
generation state contamination (PF-020). The 2026-07-24 burst attacks reliability + crash first.

## Broken — ranked
See `docs/BUGS.md` for the full registry (IDs, severity, discovery dates, closed commits).

**1. Generation timeout cliff — 120s frozen UI under sustained use.** *(PF-019, high, S1, unfixed)*
P6 #11–14 timed out consecutively. One stalled/`429`'d groq POST (httpx timeout 120s,
`providers.py:50`) eats the whole retry budget, which equals the C++ subprocess cap
(`PromptPanel.cpp:208`); groq rate-limit backoff then blows it. #14 (the never-hang robustness
test) hung. Fix: per-attempt budget, bounded backoff, typed `rate_limited`/`timeout` reason.

**2. Shutdown/rapid-click UAF on the editor's detached generate thread.** *(PF-006, high, S2,
unfixed — now with a live crash repro)* The generate thread (`PromptPanel.cpp:284,300`)
`.detach()`es and calls `loadFaustCode` through a raw `&proc`; unbounded detached threads pile up
with no supersede. Produced a `Segmentation fault (core dumped)` during the 2026-07-24 battery
(P6 #7). Fix: owned+joined worker + atomic abort + `child.kill()`, mirroring PF-003 (`d10f59e`).

**3. Cross-generation state contamination; no fresh/iterate mode.** *(PF-020, high, S1+S2,
unfixed)* Old APVTS values leak into new patches by slot index (`ParamPool.cpp:94-96`); values
reset only when the editor is open (`ParamGridPanel.cpp:31-41`). Fix: `LoadMode {Fresh, Iterate}`
on `loadFaustCode` — Fresh resets in the processor (S1), UI affordance (S2).

**4. Invalid-Faust generation for whole prompt classes.** *(PF-024, high, S1, unfixed)* Consistent
failures: ping-pong endless cycle (#2), stereo→mono / unbounded-delay (#6), syntax errors (#9,#10).
Fix: prompt grounding for stereo routing, bounded delays, ping-pong pattern.

**5. Source-of-record committed before compile success.** *(PF-022, high, S1, unfixed)* A failed
generate overwrites `currentFaustSource` (`PluginProcessor.cpp:114-118`) and can be persisted,
breaking restore. Fix: commit source only on success.

**6. Stale error persists across a new Generate.** *(PF-021, medium, S2, unfixed)* `errorBox` is
never cleared on submit (`PromptPanel.cpp:124,319`), so a prior failure shows as the current
result. Fix: clear/timestamp on submit.

**7. `FaustEngine::prepare()` doesn't re-init a live DSP on sample-rate change.** *(PF-018,
medium, S1 Backend, open)* `prepare(sampleRate, blockSize)` (`FaustEngine.cpp:154-158`) stores
the new `sr`/`block` members but never re-inits an already-live DSP — a host that changes sample
rate mid-session keeps the DSP running at the old rate (wrong pitch/timing) until the next
recompile. Pre-existing, found while building state persistence (S1, FLEET req #5). Fix shape:
re-init via Faust's `instanceInit`/`instanceConstants` (`faust/dsp/dsp.h`) or mark stale and
drive the existing async recompile path, off the audio thread.

---

## Assumed, never checked

- **Every benchmark number on record is void.** *(PF-009)* Measured against the deleted
  `bench/prompts/system_faust.txt`, which taught three functions that do not exist.
  `bench/results/.prompt_baseline.json` (0.88) has **not** been overwritten (gated) but describes
  nothing that exists. S1 has posted the exact re-run command (FLEET req #19); awaiting human
  authorization.
- **The prompt rewrite is unmeasured.** *(PF-010)* Verified *correct* (references resolve,
  examples compile), not verified *better*. Same closing condition as PF-009.
- **The efficacy pilot generalizes to nothing.** *(PF-011)* N=50, one model, the old prompt, two
  of five categories. The full 125-prompt run has never produced valid data.
- **No cross-model comparison exists.** *(PF-012)* ADR-008 "Under evaluation" since 2026-04-29.
- **Semantic fidelity is unmeasured.** *(PF-013)* **Narrowed 2026-07-25, not closed.** The
  *objective* half now exists and runs: `bench/render_oracle.py` renders a compiled patch
  offline and gates NaN/Inf, silence, DC runaway and runaway gain (see Works). What is still
  unmeasured is *fidelity to the prompt* — whether the patch does what the words asked. The
  band features needed for it are already computed, and `bench/prompts/tiered_prompts.json`
  already carries `target` + `expected_primitives` per effect; nothing turns them into an
  expected spectral signature yet. The `--judge` rubric remains off by default and has never
  run. Generators (5 of 25 prompts) cannot be rendered at all — see Works.
- **No real user prompt has ever been recorded.** *(PF-014)* `generate.py` logs nothing.

---

## Next three things

1. **Land S2's uncommitted PromptPanel Wave-1 rework** (multi-line prompt, session history,
   progress indicator, scrollable error region) — it folds in the PF-006 UAF fix as one Tier-2
   change (owned+joined worker, atomic abort, bounded join, mirroring PF-003/`d10f59e`).
   Build-verify all four editor-linking targets before treating it as landed.
2. **Run the P6 listening pass** now that params denormalize, all 64 are on-screen, and plugins
   persist — 15 minutes with a filter patch and a delay patch. First real evidence a generated
   plugin sounds like its words. (human, script authored by S4; use `groq`, not Gemini's ~20/day
   quota)
3. **Authorize the benchmark re-run.** Exact command is now posted (FLEET req #19):
   `python bench/run_benchmark.py --provider groq` — 25 prompts, first-try compile rate, $0 cost
   (groq free tier). Writes `bench/results/results.json` only; does **not** touch
   `.prompt_baseline.json` (that overwrite is a separate, later §2 trigger-1 act). (S1 →
   overseer → human)

---

## Waiting on you

Three human-gated decisions (all in COLLABORATION.md §2 territory):

1. **Confirm the persisted-state format (§2 trigger-3).** Code is in `c34bbb6` (state persistence
   fully implemented + verified: 13/13 tests, ASan/UBSan clean). The *design* — schemaVersion=1
   ValueTree→XML, Faust source + prompt as attributes, verbatim `<STATE>`, `<SlotLabels>` hint —
   is §2 trigger-3 (a contract between components). The overseer's ruling on FLEET req #12
   (2026-07-23) reaffirmed the Gate table is authoritative and this is still unconfirmed by a
   human. **Please confirm:** you knowingly approved this design, or propose amendments. (Cheap
   to amend now; v1 is the only blob in the wild.)

2. **Run the P6 listening pass (first audible validation ever).** Script is prepped in
   `docs/p6_human_run_script.md`, ready to copy-paste. ~15 minutes + your ears on a free provider
   (groq). No generated plugin has ever been listened to; this is the fastest way to find what the
   old bugs were masking. Run when you have time + ears.

3. **Authorize the benchmark re-run.** Command is posted (FLEET req #19):
   `python bench/run_benchmark.py --provider groq` (25 prompts, first-try rate, $0, groq free
   tier — far under quota). Overwriting `bench/results/.prompt_baseline.json` (0.88) afterward is
   a separate §2 trigger-1 act needing its own go-ahead. Current baseline is void (measured
   against the deleted old prompt). Say go, and S1 will spend the quota and record the new
   result.
