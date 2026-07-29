# PluginForge — Status  (2026-07-28)

Rewritten each session per COLLABORATION.md §5. Single writer, no merge conflicts.
Narrative history lives in git.

**The fleet is over.** `docs/FLEET.md` and `docs/.fleet/` are deleted; there is no overseer
and no cross-lane request log. Lane names (S1–S7) survive only inside `docs/BUGS.md` as a
record of who did what. Read this file and `docs/BUGS.md`; there is nothing else to sync.

**Start a session with `/orient`**, not by reading this file top to bottom. It injects live
repo state, **the CI line**, this file's open sections, and a staleness banner if it falls
behind HEAD.

---

## Works — and how we know

**This session's finding is that the answer had been sitting uncommitted for three days.**
CI had been red on four consecutive pushes with a SIGILL in `OfflineRenderTest`, and three
separate readings had produced three different wrong causes. The fix was already in the
working tree, unpushed, behind sixteen dirty paths. Pushing it alone took one commit and one
run. See PF-027 below — the correction is worth more than the fix.

Each closure below was verified by reading the cited code at HEAD, or by a named artifact.

- **CI is green, and the SIGILL was never the CPU.** *(PF-027, `144e023`, run
  `30409357504`.)* `juce::ScopedJuceInitialiser_GUI` at the top of `OfflineRenderTest::main()`.
  The APVTS ctor calls `startTimerHz(10)` and `Timer::startTimer` asserts a MessageManager
  exists (`juce_Timer.cpp:336`); without one the timer machinery proceeds into undefined
  behaviour that survives locally and does not on the runner. Pushed **alone**, ahead of the
  rest of the tree, so the result would be attributable — and it was. The `AMD EPYC` /
  libfaust-LLVM / instruction-set hypothesis was never tested and was never evidence.
- **The ladder runs what CI runs.** *(PF-029, `558ac96`.)* `tools/check.sh full` built four
  targets and ran one; CI additionally built and ran two behavioural harnesses. That gap is
  how four red pushes read as green locally. `full` now builds and runs `OfflineRenderTest`,
  `PromptPanelThreadingTest` and `EditorSessionTest`, under CI's sanitizer options, skipping
  the display-dependent ones by name when there is no display. Guarded by
  `TestLadderRunsWhatCIRuns`, which parses the harness list out of the **workflow** so it
  cannot go stale when a third harness is added. Mutation-tested: 3 of its 4 tests fail
  against the pre-fix ladder.
- **The editor is driven by something, at last.** *(`81fc75b`.)*
  `host/tests/EditorSessionTest.cpp` — 61 checks over 11 scenarios against the real
  `PluginForgeProcessor` + `PluginForgeEditor`: generate, widget kinds, a 40-param overflow,
  error surfacing, PF-021, PF-022, Fresh vs Refine, the output-guard mute, rapid-fire
  supersede, teardown mid-flight, save/reopen, and the code view. Each writes a PNG via
  `createComponentSnapshot` (software renderer, no compositor) to `artifacts/images/`; CI
  uploads them. Nothing had ever constructed this class.
- **Reopening a saved project no longer wipes every knob.** *(PF-033, `81fc75b`, found by the
  harness on its first green run.)* `ParamGridPanel::refreshParamKnobs` seeded every mapped
  slot from patch defaults unconditionally — including on the restore recompile, which
  `LoadMode::Iterate` exists specifically to protect. Measured: a patch saved with slots at
  0.95 and 0.05 came back at **0.250 and 0.750**, exactly those slots' declared defaults.
  Fixed by deleting the seeding; the processor's `resetMappedSlotsToDefaults` already does it
  properly and in the swap protocol's safe window. `StatePersistenceTest` round-trips 33/33
  and cannot see this, because it never constructs an editor.
- **The harness's own timing race is closed, and CI is what found it.** *(PF-034, `10c27e2`.)*
  On its first pushed run `EditorSessionTest` failed 1 of 61 on the runner and passed every
  time locally: `loadAndSettle` waited on the source of record, which is assigned on the
  **compile** thread, while the widgets are rebuilt later on the **message** thread. With two
  consecutive patches of equal parameter count the wait discriminated nothing. Replaced with a
  refresh counter that must **advance**, which no prior state can satisfy. Third instance in
  one day of the dev box and the runner disagreeing — PF-027, PF-029, PF-034 — and in all
  three the dev box gave the more flattering answer.
- **Fresh vs Refine is reachable from the UI.** *(PF-020's residual, `3106cd9`.)* A Refine
  toggle, off by default. The mode is read on the message thread at submit and published into
  the job slot under `jobMutex` with the prompt and the stamp — never re-read on the worker,
  which would be both a data race on a Component and a retroactive change to a run already
  going. Each mode is the other's red case in scenario 6.
- **The generated Faust is visible.** *(ux_roadmap Phase 3a, `7e8de50`.)* `CodeEditorPanel`
  was a 13-line empty stub; it now shows the live source read-only behind a "Show code"
  disclosure, off by default. Monochrome — JUCE ships C++/Lua/XML tokenisers and no Faust one,
  and the C++ one would confidently colour the wrong things.
  `currentSourceForTest()` is promoted to `currentSource()`, a product accessor.
- **A listening pass is now one command plus ears.** *(`5430dcc`, built and dry-run, NOT
  fired.)* `bench/p6_capture.py` runs the 14-prompt battery and emits WAVs plus a scorecard
  with one empty column. Rendered through `OfflineRenderTest --capture`, i.e. the **shipping**
  path (JIT, swap protocol, ParamPool denormalisation, OutputGuard), not `faust2sndfile`.
  Holds the PF-025 lock for the whole run. Prompts transcribed verbatim and the transcription
  is **checked** against the document — mutation-tested by deleting one comma.
- **The digest reports CI and cannot go quiet.** *(PF-026, `ff74d5c`.)* Red, green-but-behind
  and unreachable each get a banner; silence is the one forbidden output. 15 tests, mutation-
  tested via the `PLUGINFORGE_CI_RUNS_JSON` seam.
- **Prose about mechanisms is mechanically checked.** *(PF-028, `ff74d5c`.)*
  `TestHookTableMatchesReality` asserts COLLABORATION.md §7 names exactly what
  `settings.json` registers.
- **Real user prompts are recorded.** *(PF-014, `cfd9569`.)* `log_user_prompt()`, product path
  only, fail-open, gitignored. 19 tests. **Nothing has accumulated in it yet.**
- **The benchmark harness cannot destroy its own evidence.** *(PF-025, `ff74d5c`.)* `O_EXCL`
  lock naming the holding pid; every run writes a dated archive.
- **The prompt is measured.** *(PF-009 + PF-010.)* 25 prompts, groq/`gpt-oss-120b`:
  **22/25 = 88%**, archived at `bench/results/results_20260728_groq.json`, against 20/25 = 80%
  on 07-27. Per class: `routing_arity` 2→0, `unbound_variable` 1→0 — what `f3453c4` targeted —
  while `syntax:EXTRA` and `syntax:FLOAT` each went 0→1. **The aggregate move is inside the
  noise**; the per-class result is the evidence. Noise floor unmeasured — PF-031.
- **Generated DSP is measured as audio.** `bench/render_oracle.py`, calibrated against
  physics. Re-run 2026-07-28 against the fresh corpus: **16 passed, 2 FAILED, 4 unsupported.**
  The two failures compile and render *silent* (PF-032).
- **The editor's generate thread is owned and joined** (PF-006, `18e862e`); **the 120s timeout
  cliff is closed** (PF-019, `4bea5f3`); **Fresh/Iterate load modes exist** (PF-020,
  `4a84c1c`); **source of record commits only on compile success** (PF-022); **stale errors
  clear on submit** (PF-021); **`prepare()` re-inits a live DSP on rate change** (PF-018);
  **`process()` has a null guard** (PF-023); **the hooks run and have been seen blocking**
  (`a5e0275`) — `check_bash_denylist.py` blocked a `git stash` during this session;
  **the RT-safety hook covers all four audio-thread functions** (PF-015); **state persistence**
  (PF-002, format human-confirmed); **params denormalize into real units** (PF-001);
  **all 64 params reach the editor** (PF-005 — and the grid is now confirmed at runtime);
  **JIT swap is TSan-clean**; **the system prompt is grounded in the real stdlib.**
- **Python suite: 434 passed, 12 deselected.** `tools/check.sh full` all green, including all
  three behavioural harnesses.

---

## Two things the harness measured that were nobody's claim either way

Recorded because a reader deserves them, and because both are visible in
`artifacts/images/session_*.png`:

1. **Knobs appear alphabetically, not in declaration order.** A 40-param patch lists
   `P0, P1, P10, P11 … P2`. Lexicographic, so `P10` precedes `P2`.
2. **No generated plugin has ever shown a rotary.** `FaustEngine::Kind` has five values and
   `refreshParamKnobs` handles all five explicitly, so the `default:` rotary arm is
   unreachable. `docs/ui_design_plan.md` describes it as the fallback widget. It is dead code.

A third, from reading the snapshots: **every value displays as a raw 0–1 slot number.** A
cutoff of 800 Hz reads `0.04`; a voice count of 2 reads `0.14`. `ParamMap` denormalizes into
the DSP but nothing denormalizes for the display. Not filed as a defect yet because it is a
design question, not a bug.

---

## Broken — ranked

Registry with IDs, severity and discovery dates: `docs/BUGS.md`.

**1. Generation produces invalid Faust for whole prompt classes.** *(PF-024, high, open,
`llm/prompts/system_prompt.txt`, found 2026-07-24.)* Three signatures remain in the 07-28 run:
`syntax:FLOAT` (ping-pong), `syntax:EXTRA` (sidechain compressor), `recursion_cycle`
(Karplus-Strong). Verified corrections for the recursion case exist in
`.claude/skills/faust-idioms/SKILL.md`, compiled against Faust 2.85.5, **and none has been
folded into the prompt.** This is the single biggest thing standing between here and a
listening pass that is worth the human's time — ping-pong is prompt #2 of 14.

**2. Two compiling patches render silent.** *(PF-032, high, **diagnosed 2026-07-28, not
fixed**.)* Both are **unit-contract errors on stdlib arguments**, and both are one line from
working — verified by rendering the generated code against a one-argument variant:

- The warm low-pass writes `cutoff : *(1.0/ma.SR)`, but `vaeffects.lib:71` documents
  `moog_vcf(res, fr)` as taking **Hz**. It passes 0.0208 Hz. rms 2.5e-08 → **0.0114** when the
  Hz goes through unscaled.
- The noise gate pre-converts with `ba.db2linear`, but `misceffects.lib:164` documents `thresh`
  in **dB** and `:188` does the conversion itself. Double conversion makes the threshold ~1.0
  linear, so the gate never opens and the output is *identically* 0.0 → **0.0995** when the dB
  goes through raw.

Both are valid Faust that JITs cleanly, loads in a DAW, and produces nothing — which is
precisely what a compile-rate metric cannot see. `check_prompt_invariants.py` guarantees every
`ns.func` exists and says nothing about what its arguments **mean**; the stdlib block carries
names and arities, and units live in the `//` doc blocks it does not read. **Fixing it is a
prompt edit, i.e. item 1 below and its evidence bar.** Full working in `docs/BUGS.md`.

**3. The benchmark's noise floor is unmeasured.** *(PF-031, medium, open.)* It has never been
run twice on an unchanged prompt, so no delta can be called significant — including this
week's 80%→88%.

**4. `run_efficacy_study.py` takes no PF-025 lock.** *(PF-030, medium, open.)* It can run
concurrently with `run_benchmark.py` and share one free-tier rate limit. `p6_capture.py` takes
the lock and `TestCaptureHarnessTakesTheLock` guards new harnesses; the efficacy study is the
one still outside.

---

## Assumed, never checked

**Three claims, unchanged.** This session did not move the number, and that is worth stating
rather than glossing: every closure above is a code defect, and all three remaining claims
need generation runs the user deliberately scoped out of this session. What did change is the
cost — PF-013's successor is now one command instead of a 40-minute manual session.

- **The efficacy pilot generalizes to nothing.** *(PF-011)* 50 records, 2 of 5 categories, on
  the paid `claude` provider, dated 2026-07-20 — i.e. the deleted prompt. The full grid is
  25 effects × 5 tiers = **125 generations**, free on groq.
- **No cross-model comparison exists.** *(PF-012)* **Partially answered.** Over the 20 prompts
  both models completed: `gpt-oss-120b` 18/20, `llama-3.3-70b` 17/20, with three disagreements
  running in *both* directions — the models differ by failure profile, not by a scalar. The
  llama arm was truncated at 21/25 by throttling.
- **Semantic fidelity is unmeasured.** *(PF-013)* `--judge` has never executed. Judging the
  existing pilot would grade the deleted prompt, so it is downstream of PF-011.

## Next three things

1. **Close PF-024's three classes and PF-032's two, in one prompt edit.** `syntax:FLOAT`,
   `syntax:EXTRA`, `recursion_cycle`, plus the two unit contracts PF-032 just pinned down
   (frequencies in Hz unless the doc says normalised; never pre-convert dB for a function that
   documents a dB parameter). The faust-idioms skill has a compiled pattern for the recursion
   case. A prompt edit owes a benchmark statement per `.claude/rules/tier2-evidence.md` — and
   PF-031 means the honest statement is per-class, not aggregate.
2. **Decide whether the stdlib block should carry argument units.** PF-032's root cause is
   that the generated block teaches names and arities while the units sit in doc-block prose
   the generator does not read. Extending it would close the class rather than the two
   instances. New enforcement surface → route through `/architecture-planning`.
3. *(evidence)* **Fire `bench/p6_capture.py` live, then the efficacy grid.** 14 generations on
   groq closes the objective half and hands over WAVs; then 125 more closes PF-011, then
   `score_efficacy.py --judge` closes PF-013. Sequential, not parallel — PF-030. Fresh
   rate-limit window.

## Waiting on you

**Three items. The third is the one that matters, and it is now much cheaper than it was.**

1. **Overwriting `bench/results/.prompt_baseline.json`** once new numbers exist. Replacing the
   stored 0.88 destroys the only record of the pre-unification prompt. Not yet asked.

2. **Authorizing the live capture run.** `python bench/p6_capture.py --i-authorize-spend`
   spends 14 free-tier generations ($0 on groq) and produces
   `artifacts/p6_<date>/` — 14 WAVs and a scorecard. Built and dry-run this session,
   deliberately not fired. **Worth doing after item 1 above, not before**, or several of the
   fourteen rows will be generation failures already on record — ping-pong is prompt #2.

3. **A second P6 listening pass.** Per COLLABORATION.md §1 this is the one judgement in the
   project with no instrument: the oracle proves a patch is not broken and cannot tell you the
   filter is musical or that the fuzz is the fuzz the prompt described. **What changed this
   session is the shape of the ask.** It is no longer "drive the Standalone for forty
   minutes"; it is "play fourteen files and fill in one column." Everything around the
   listening is now machine work, and the machine does it. The listening is still yours, and
   is not delegable to a hook, a harness or a model.
