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

**This session's finding is that the loop had a blind spot where it reports.** A workflow
audit — not any control — found CI red on four consecutive pushes since 2026-07-26 while
the previous rewrite of this file asserted *"CI is green. `ae5d213` passed 2026-07-26."*
That sentence has been deleted. PF-026 records the mechanism failure, PF-027 the underlying
build defect, PF-028 the same pattern in COLLABORATION.md §7.

Each closure below was verified by reading the cited code at HEAD, not by trusting a commit
message.

- **The digest reports CI, and cannot go quiet about it.** *(PF-026, 2026-07-28,
  uncommitted.)* `tools/status_digest.sh` prints a CI section immediately after repo state:
  newest completed run on the branch, its conclusion, consecutive-failure streak, and **how
  far behind HEAD the tested commit is** — a green run on an older commit is evidence about
  that commit only. Three states, three banners: red, green-but-behind, unreachable. Silence
  is the one forbidden output. **Mutation-tested 2026-07-28:** with the red banner disabled
  (the pre-fix behaviour) `test_red_ci_is_announced` fails and the other 14 still pass.
  15 tests in `TestDigestReportsCI`, all offline via the `PLUGINFORGE_CI_RUNS_JSON` seam.
- **Prose about mechanisms is mechanically checked.** *(PF-028, 2026-07-28, uncommitted.)*
  COLLABORATION.md §7's hook table listed two hooks retired in `cf1d8e8` and omitted
  `check_prompt_invariants.py`, which was running. `TestHookTableMatchesReality` now asserts
  the table names exactly what `settings.json` registers, that every hook on disk appears in
  it, and that every hook the prose calls retired is really gone. §7 states the general rule:
  a document describing a mechanism is either checked against it or dated and read-only.
- **`Next three things` reserves a slot for evidence.** *(2026-07-28, uncommitted.)* One of
  the three carries `*(evidence)*` and `TestStatusReservesAnEvidenceSlot` enforces it.
  Rationale in COLLABORATION.md §5: all 18 closed defects are code defects, most closed in
  one to three days; all six *evidence* defects were open five days later, and those six
  **are** the Assumed list. Two slots compete on urgency; the third cannot.
- **The prompt is measured again, and PF-024's partial fix has a directional result.**
  *(PF-009 + PF-010, 2026-07-28.)* 25 prompts, groq/`gpt-oss-120b`, $0: **22/25 = 88%**,
  archived at `bench/results/results_20260728_groq.json`. Against the 07-27 run archived by
  `e3019c0` (**20/25 = 80%**), which straddles `f3453c4` — the two runs bracket the only
  prompt change in between. Per class, which is the load-bearing view:
  `routing_arity` **2→0**, `unbound_variable` **1→0** — exactly what `f3453c4` targeted —
  while `syntax:EXTRA` and `syntax:FLOAT` each went 0→1. **The aggregate move is inside the
  noise** (+2 of 25 ≈ 1.1 SE); the per-class result is the evidence. Noise floor itself is
  unmeasured — PF-031.
- **Real user prompts are recorded.** *(PF-014, 2026-07-28.)* `log_user_prompt()` writes one
  JSONL record per generation from `_run_subprocess_mode` — the C++ host's path only, on both
  the success and the exception branch. Bench harnesses cannot contaminate it; fail-open so a
  logging failure never costs a generation. Gitignored. 19 tests in `tests/test_prompt_log.py`.
- **The editor's generate thread is owned and joined.** *(PF-006, `18e862e`.)* No `.detach()`
  remains in `PromptPanel.cpp`. One persistent worker, joined in the destructor (`:182-183`),
  started lazily (`:231-232`); an in-flight run is **superseded**, not stacked, and its
  subprocess killed (`:173`, `:238`). Threading contract stated in `PromptPanel.h:47-68`.
  Covered by `host/tests/PromptPanelThreadingTest.cpp` (263 lines).
- **The 120s timeout cliff is closed.** *(PF-019, `4bea5f3`.)* `providers.Budget`
  (`providers.py:143-160`) carries a total and a per-attempt cap; `generation_budget()`
  (`generate.py:76-85`) sizes one per generation so all attempts *plus* each faust compile
  fit inside the C++ subprocess cap. Backoff that would overrun the deadline is refused
  rather than slept through. `_HTTP_TIMEOUT` is now only a fallback (`:188`).
- **Fresh/Iterate load modes exist.** *(PF-020, `4a84c1c`.)* `LoadMode { Fresh, Iterate }`
  on `loadFaustCode`; Fresh resets mapped macros in the *processor*, so behavior no longer
  depends on whether the editor happens to be open.
- **Source of record is committed only on compile success.** *(PF-022, `4a84c1c`.)*
  `loadFaustCode` no longer touches `currentFaustSource`/`currentPrompt`; they are assigned
  only in the success branch (`PluginProcessor.cpp:180-181`), with a comment at `:148`
  recording that the omission is deliberate. A failed generate leaves the last good source.
- **Stale errors clear on submit.** *(PF-021, `18e862e`.)* `submitPrompt()` calls
  `clearError()` at `PromptPanel.cpp:200`, before the run starts.
- **`prepare()` re-inits a live DSP on sample-rate change.** *(PF-018, `be83d1e`.)*
- **`FaustEngine::process()` has an `activeDSP` null guard.** *(PF-023, `4a84c1c`.)*
- **The enforcement hooks run, and have been seen blocking.** *(2026-07-25, `a5e0275`.)*
  They never had: `.claude/settings.json` declared `PreToolUse` at the file root instead of
  under `"hooks"`, which Claude Code ignores **silently**. Proof is behavioural — a real
  `Bash` call was blocked, and `check_bash_denylist.py` blocked one again on 2026-07-27
  during unrelated work. `tests/test_control_wiring.py` guards both shape and teeth.
- **The RT-safety hook covers the whole audio path.** *(PF-015, `fed704e`.)* `ANCHOR_RE`
  matches all four functions that actually run there — `processBlock`, `FaustEngine::process`,
  `ParamPool::pushToFaust`, `OutputGuard::process` — with a parametrised red case each.
  **Known limitation:** the closure is enumerated by hand, so a *fifth* function arriving on
  the audio thread is not covered until someone adds it.
- **Project extensions can no longer reference deleted files.** *(2026-07-27, uncommitted.)*
  `attention-report` was deleted: it read CLAUDE.md's "Current status" section (removed
  2026-07-25) and `docs/collaboration_log.md` (retired, deleted), and reported the shortfall
  as nothing to report.
- **Generated DSP is measured as audio, over a CURRENT corpus.** `bench/render_oracle.py`
  renders a compiled patch offline (numpy + scipy, no network, no quota) and gates NaN/Inf,
  silence, DC offset, runaway gain. Calibrated against physics: `fi.resonlp(1000,.707)`
  measures −3.0 dB at its corner and −30 dB two octaves up.
  **Re-run 2026-07-28 against the fresh groq corpus — and it found something:
  16 passed, 2 FAILED, 4 unsupported.** The two failures compiled cleanly and render
  *silent* (PF-032), so the 88% compile rate overstates working output; of renderable
  patches the real rate is 16/18. The previous "17 of 17" was a true statement about the
  2026-07-19 `claude` corpus generated by the deleted prompt, and nobody had re-run the gate
  against current output until today.
- **One entry point for every check.** `tools/check.sh fast | full | audio | quota`,
  cost-ordered and cumulative. `quota` refuses to spend free-tier requests without
  `--i-authorize-spend`. `tools/check.sh assumed` prints the one number.
- **Python suite: 428 passed, 12 deselected**, run 2026-07-28 via
  `python -m pytest tests/ -m "not integration" -q`. The previous rewrite recorded "338
  passed, 12 deselected" for 2026-07-27; the gap exceeds this session's 39 new tests
  (15 CI digest + 5 hook-table/evidence-slot + 19 prompt log) and has not been accounted
  for, so treat the older figure as unreliable rather than reading the delta as one
  session's work.
- **State persistence.** *(PF-002, `c34bbb6`.)* Versioned ValueTree→XML blob
  (schemaVersion=1); `StatePersistenceTest` round-trips 33/33, ASan/UBSan clean. **The
  format is confirmed** (human, 2026-07-27). `<SlotLabels>` was dropped from v1 — written on
  every save and read by nothing. Old blobs containing the node still restore.
- **Parameters denormalize into real units** (PF-001, `ParamMap.h`, `ParamMapTest.cpp`);
  **the parameter path is RT-safe** (PF-004); **the compile thread is owned and joined**
  (PF-003, `d10f59e`); **OutputGuard** catches NaN/DC/runaway before the speakers;
  **all 64 params reach the editor** (PF-005, `2e129cd`); **JIT swap is TSan-clean**.
- **System prompt grounded in the real stdlib.** Every `ns.func` resolves against installed
  `/usr/share/faust/*.lib`; all five few-shot examples compile. Hook-enforced.
- **The benchmark harness cannot destroy its own evidence.** *(PF-025, uncommitted.)*
  `O_EXCL` lock naming the holding pid; every run writes a dated archive as well as
  `results.json`.

---

## P6 listening battery — one run, 2026-07-24 (groq / gpt-oss-120b)
**4 clean, 3 flaky, 7 failures of 14.** The 2026-07-21 review predicted it would fail on the
first patch; it did, repeatedly. That is a bad result, not a missing one — PF-008 asked
whether anyone had ever listened, and someone has, so it is discharged.

Everything that run exposed except the generation quality itself has since been fixed: the
timeout cliff (PF-019), the segfault (PF-006), and state contamination (PF-020) are all
closed and verified. **What remains is PF-024.**

---

## Broken — ranked

Registry with IDs, severity and discovery dates: `docs/BUGS.md`.

**1. Generation produces invalid Faust for whole prompt classes.** *(PF-024, high, open,
`llm/prompts/system_prompt.txt`, found 2026-07-24.)* Four recorded signatures from the P6
battery: **#2 ping-pong** → `endless evaluation cycle`; **#6 cold/glassy** →
`2 outputs must equal 1 input` or `invalid delay parameter range`; **#9** →
`syntax error, unexpected IDENT`; **#10 RE-201** → `syntax error, unexpected WITH`.
`bench/results/results.json` adds `undefined symbol : flanger_mono`.

Verified corrections for four of the five classes exist in
`.claude/skills/faust-idioms/SKILL.md`, each compiled against Faust 2.85.5 on 2026-07-27 —
but **none has been folded into the prompt**, so this entry is open on the evidence that
matters. Fix is Tier 2: it owes a re-run or an explicit statement that the baseline is stale.

**These read as five unrelated failures. They are largely one gap** (found 2026-07-27): the
prompt barely teaches Faust's routing algebra, and the one language construct it *does* teach
does not exist.
- `system_prompt.txt:21` instructs the model to *"use let bindings or with { } blocks."*
  **Faust has no `let`.** Verified against the installed compiler:
  `process = let g = 0.5; in _ * g, _ * g;` → `syntax error, unexpected IDENT` — the exact
  signature recorded for #9. **The prompt is teaching the failure.**
- The same line recommends `with { }`, which is real, but **none of the five few-shot examples
  contains one**.
- `<:` (split) and `:>` (merge) appear **nowhere** in the prompt; `~` (recursion) appears
  exactly once, buried inside the delay example's body at `:149`, never named or explained.
- Ping-pong is described in prose at `:26-27` and still fails: `endless evaluation cycle` is a
  recursion-topology error, which prose cannot convey and an example can.
- Nothing states that a delay's first argument is a compile-time-constant *maximum*.

**2. `OfflineRenderTest` aborts on a headless runner.** *(PF-027, high, open,
`host/tests/OfflineRenderTest.cpp:372`, found 2026-07-28.)* `main()` constructs
`PluginForgeProcessor`, whose APVTS ctor calls `startTimerHz(10)`
(`juce_AudioProcessorValueTreeState.cpp:265` → `juce_Timer.cpp:352` → `:336`). With no
MessageManager on the runner it hits the JUCE assertion and the job dies with **exit 132**.
This has failed every push since 2026-07-26: `30295123178`, `30296235090`, `30297455014`,
`30299041776`.

**It does not reproduce locally**, which is why it survived four pushes — a dev session has
what the runner lacks, so `tools/check.sh full` passes on the identical test. Not yet
diagnosed past the backtrace: whether the fix is a scoped `MessageManager` in the test's
`main`, a headless guard, or splitting the render harness off the processor is open. Note
`host/tests/OfflineRenderTest.cpp` has uncommitted working-tree changes; the working copy may
already differ from what CI ran.

---

## Assumed, never checked

**Three claims, down from six.** PF-009, PF-010 and PF-014 closed 2026-07-28 on measurement
and code, not on documentation. The three that remain all need generation runs the free tier
throttled today.

- **The efficacy pilot generalizes to nothing.** *(PF-011)* The pilot is 50 records covering
  `filters` + `generative` only — 2 of 5 categories — all on the **paid** `claude` provider,
  dated 2026-07-20, i.e. the deleted prompt. The full grid is 25 effects × 5 tiers = **125
  generations**, free on groq. Not run today: the same rate limit that truncated PF-012, and
  PF-030 means it cannot safely overlap another harness.
- **No cross-model comparison exists.** *(PF-012)* **Partially answered.** Over the 20
  prompts both models completed: `gpt-oss-120b` 18/20, `llama-3.3-70b` 17/20 — and three
  disagreements running in *both* directions, so the models differ by failure profile, not by
  a scalar. The llama arm was truncated at 21/25 by throttling. Detail in `docs/BUGS.md`.
- **Semantic fidelity is unmeasured.** *(PF-013)* The objective half runs (render oracle).
  Fidelity *to the prompt* still does not: `--judge` has never executed, and judging the
  existing pilot would grade the deleted prompt. It needs a fresh efficacy run to judge, so
  it is downstream of PF-011.

## Next three things

1. **Find the real cause of the CI SIGILL (PF-027).** Still red. The Timer assertion that
   four separate readings blamed was the gdb post-mortem's own breakpoint; the bare run dies
   at the *tremolo* patch, and the first three pass. `ScopedJuceInitialiser_GUI` has landed,
   which removes 19 benign assertions and should let the next post-mortem reach the actual
   faulting frame. Push and read it.
2. **Close PF-024's remaining three classes.** Today's run puts the two classes `f3453c4`
   targeted at zero (`routing_arity` 2→0, `unbound_variable` 1→0). What remains:
   `syntax:FLOAT` (ping-pong), `syntax:EXTRA` (sidechain compressor), `recursion_cycle`
   (Karplus-Strong). `.claude/skills/faust-idioms/SKILL.md` has a compiled pattern for the
   recursion case.
3. *(evidence)* **Run the full efficacy grid, then judge it.** 125 generations on groq, $0,
   closing PF-011; then `score_efficacy.py --judge` with a second groq model as grader,
   closing PF-013. Sequential, not parallel — PF-030. Do it in a fresh rate-limit window.

## Waiting on you

**Two items**, and the second is the one that matters.

1. **Overwriting `bench/results/.prompt_baseline.json`** once the new numbers exist. The
   run itself is no longer gated — §2 trigger 1 now names the artifact, not the activity —
   but replacing the stored 0.88 destroys the only record of the pre-unification prompt. I
   will ask again with the figures in hand.

2. **A second P6 listening pass.** Promoted out of *"optional, not blocking"*, where it sat
   while every reliability defect it exposed got fixed around it. Per COLLABORATION.md §1,
   this is the one judgment in the project with no instrument: the render oracle proves a
   patch is not broken — no NaN, no silence, no DC, no runaway gain — and cannot tell you the
   filter is musical or that the fuzz is the fuzz the prompt described. Hooks, the ladder,
   the oracle and now the CI line cover everything else. **This is the part that is
   structurally yours**, and it is worth more after PF-024 is fixed than before.
