# PluginForge — Status  (2026-07-30)

Rewritten each session per COLLABORATION.md §5. Single writer, no merge conflicts.
Narrative history lives in git.

**Start a session with `/orient`**, not by reading this file top to bottom. It injects live
repo state, **the CI line**, this file's open sections, and a staleness banner if it falls
behind HEAD.

---

## Works — and how we know

**This session's finding is that a cautious conclusion was flattened into a confident wrong
one, and the summary outlived the investigation that contradicted it.** CI was red at HEAD
with a SIGILL that PF-027 had closed two days earlier as "never the CPU." It was the CPU.
PF-027's own detail entry in `docs/BUGS.md` had said so — it declined to exonerate the
runner, named the missing evidence, and said to wait for the next red run's post-mortem.
That post-mortem arrived and nobody had read it. See PF-036; the correction is worth more
than the fix.

Each closure below was verified by reading the cited code at HEAD, or by a named artifact.

- **CI's SIGILL is diagnosed, fixed, and the diagnosis is reproducible on this box.**
  *(PF-036, pending commit.)* libfaust's LLVM picks its target CPU **by name** and enables
  that name's default feature set without rechecking CPUID. Azure's EPYC 9V74 reports as
  `znver4` — whose defaults include AVX-512 — while the hypervisor masks AVX-512 out of the
  guest, so the JIT emits `kmovd`/EVEX and dies. Runner CPU predicts outcome perfectly over
  the last twelve runs: both failures on 9V74, every success on EPYC 7763 or Xeon 8573C. A
  ~1-in-5 draw, which is why one green run read as a fix.
  **The documented fix does not work**: `createDSPFactoryFromString`'s `target` parameter is
  inert on the JIT path — asking for `:i486` still emitted 28 VEX-prefixed AVX instructions,
  and `totally-bogus-triple:nonexistent-cpu` was accepted without error. The working lever is
  an `LD_PRELOAD` shim over `llvm::sys::getHostCPUName()`, which libfaust leaves undefined and
  resolves from libLLVM. **CI-only**; the product path stays host-native deliberately.
- **The shim is proven to have teeth, without waiting on a lucky runner draw.**
  `host/tests/JitTargetTest.cpp` disassembles the JIT'd pages under two CPU names: `x86-64`
  emits zero AVX-512 (and zero AVX) across the corpus; `znver4` emits it, which is what stops
  the green arm being vacuous. Run without the preload it fails on its first assertion rather
  than passing empty — checked, exit 1. `tremolo` is the patch that was crashing: 1 EVEX + 2
  opmask refs under `znver4`, and `os.osc`'s phasor wrap is the `vroundss $0x9` sitting right
  after the faulting `kmovd` in the CI backtrace.
- **The CI wiring cannot silently rot.** `TestJitTargetIsPinnedInCI` asserts every JIT-ing
  step still carries the preload and that the sanitizer runtime leads it (an ASan binary
  aborts outright if anything precedes libasan — found the hard way). Mutation-tested against
  four breakages; each caught by the intended assertion, green again after restore.
- **The three UI observations are now defects with IDs.** *(PF-037/038/039.)* They had been
  sitting in this file's prose with no ID, one rewrite away from vanishing.
- **The editor is driven by something, at last.** *(`81fc75b`.)*
  `host/tests/EditorSessionTest.cpp` — 61 checks over 11 scenarios against the real
  `PluginForgeProcessor` + `PluginForgeEditor`. Each writes a PNG via `createComponentSnapshot`
  to `artifacts/images/`; CI uploads them. Nothing had ever constructed this class.
- **Reopening a saved project no longer wipes every knob.** *(PF-033, `81fc75b`.)* A patch
  saved with slots at 0.95 and 0.05 came back at **0.250 and 0.750** — exactly those slots'
  declared defaults. `StatePersistenceTest` round-trips 33/33 and cannot see this, because it
  never constructs an editor.
- **The harness's own timing race is closed, and CI is what found it.** *(PF-034, `10c27e2`.)*
- **Fresh vs Refine is reachable from the UI.** *(PF-020's residual, `3106cd9`.)*
- **The generated Faust is visible.** *(ux_roadmap Phase 3a, `7e8de50`.)* `CodeEditorPanel` was
  a 13-line empty stub; it now shows the live source read-only behind a "Show code" disclosure.
- **A listening pass is now one command plus ears.** *(`5430dcc`, built and dry-run, NOT
  fired.)* `bench/p6_capture.py` runs the 14-prompt battery and emits WAVs plus a scorecard
  with one empty column, rendered through the **shipping** path.
- **The digest reports CI and cannot go quiet.** *(PF-026, `ff74d5c`.)*
- **The ladder runs what CI runs.** *(PF-029, `558ac96`.)* Now including `JitTargetTest`, which
  the workflow-parsing guard demanded automatically the moment CI started building it — the
  relation did its job without anyone updating a list.
- **The benchmark harness cannot destroy its own evidence.** *(PF-025, `ff74d5c`.)*
- **The prompt is measured — but see the caveat below.** *(PF-009 + PF-010.)* 25 prompts,
  groq/`gpt-oss-120b`: **22/25 = 88%**, archived at `bench/results/results_20260728_groq.json`.
  Per class: `routing_arity` 2→0, `unbound_variable` 1→0, while `syntax:EXTRA` and
  `syntax:FLOAT` each went 0→1. **The aggregate move is inside the noise**; the per-class
  result is the evidence. Noise floor unmeasured — PF-031.
- **Generated DSP is measured as audio.** `bench/render_oracle.py`, calibrated against physics.
  Last run 2026-07-28 against the 07-28 corpus: **16 passed, 2 FAILED, 4 unsupported.**
- **The audio path itself.** Editor generate thread owned and joined (PF-006); the 120s timeout
  cliff closed (PF-019); Fresh/Iterate load modes (PF-020); source-of-record commits only on
  compile success (PF-022); stale errors clear on submit (PF-021); `prepare()` re-inits a live
  DSP on rate change (PF-018); `process()` null guard (PF-023); the RT-safety hook covers all
  four audio-thread functions (PF-015); state persistence (PF-002, format human-confirmed);
  params denormalize into real units (PF-001); all 64 params reach the editor (PF-005);
  JIT swap is TSan-clean; the system prompt is grounded in the real stdlib.
- **Python suite: 445 passed, 12 deselected.** `tools/check.sh full` all green, including all
  four behavioural harnesses.

---

## Broken — ranked

Registry with IDs, severity and discovery dates: `docs/BUGS.md`.

**1. Generation quality is unmeasured since the prompt changed.** *(PF-024 + PF-032, high,
in-progress.)* `a4f942e` rewrote the constructs both defects blame — stereo dry/wet across a
multi-output block, `select2`, and the two unit contracts (frequencies in Hz unless the doc
says normalised; never pre-convert dB for a function documenting a dB parameter). **No
generation has run against it.** Every number in this file describes the *previous* prompt.
The Tier-2 benchmark statement is unpaid, and per PF-031 the honest form is per-class, not
aggregate. `check.sh audio` will stay red regardless — it renders the stored 07-28 corpus,
which a prompt edit cannot retroactively change.

**2. Every parameter displays as a raw 0–1 slot number.** *(PF-037, medium, open.)* A cutoff
of 800 Hz reads `0.04`. The DSP is correct — `ParamMap` denormalizes into it — and nothing
denormalizes for the display, so no user can read what any knob is set to. Previously
declined as "a design question, not a bug"; that was wrong.

**3. The benchmark's noise floor is unmeasured.** *(PF-031, medium, open.)* It has never been
run twice on an unchanged prompt, so no delta can be called significant — including
80%→88%.

**4. Knob ordering and a dead widget arm.** *(PF-038 low, PF-039 low, open.)* Knobs sort
lexicographically (`P0, P1, P10, P11 … P2`); the rotary fallback is unreachable while
`docs/ui_design_plan.md` still describes it as the fallback widget.

**5. A per-call output budget cannot be expressed.** *(PF-035, low, open.)*

---

## Assumed, never checked

**Three claims, unchanged.** This session did not move the number. Every closure above is a
code or infrastructure defect; all three remaining claims need generation runs, and the
blocking item is authorization, not work.

- **The efficacy pilot generalizes to nothing.** *(PF-011)* 50 records, 2 of 5 categories, on
  the paid `claude` provider, dated 2026-07-20 — i.e. the deleted prompt. The full grid is
  25 effects × 5 tiers = **125 generations**, free on groq.
- **No cross-model comparison exists.** *(PF-012)* **Partially answered.** Over the 20 prompts
  both models completed: `gpt-oss-120b` 18/20, `llama-3.3-70b` 17/20, with three disagreements
  running in *both* directions. The llama arm was truncated at 21/25 by throttling.
- **Semantic fidelity is unmeasured.** *(PF-013)* `--judge` has never executed.

## Next three things

1. **Push, and read the CI result correctly.** The fix is local-green but unpushed. **A green
   run is not confirmation** — at a 1-in-5 failure rate, 80% of runs were already green with
   the bug live. That inference is what closed PF-027 early. What confirms it is a green run
   whose `lscpu` line says `AMD EPYC 9V74`; the workflow still prints that line.
2. *(evidence)* **Fire `bench/p6_capture.py` live, then the efficacy grid.** 14 generations on
   groq closes the objective half and hands over WAVs; then 125 more closes PF-011, then
   `score_efficacy.py --judge` closes PF-013. Sequential, not parallel — PF-030. This is also
   what pays PF-024/PF-032's outstanding benchmark statement.
3. **Fix PF-037.** The knob readout is the most visible defect in the product and the cheapest
   of the three UI items — `ParamMap` already has the denormalization; nothing calls it for
   display.

## Waiting on you

**Three items. The third is still the one that matters.**

1. **Overwriting `bench/results/.prompt_baseline.json`** once new numbers exist. Replacing the
   stored 0.88 destroys the only record of the pre-unification prompt. Not yet asked.

2. **Authorizing the live capture run.** `python bench/p6_capture.py --i-authorize-spend`
   spends 14 free-tier generations ($0 on groq) and produces `artifacts/p6_<date>/` — 14 WAVs
   and a scorecard. Built and dry-run, deliberately not fired. Worth doing **after** item 1.

3. **A second P6 listening pass.** Per COLLABORATION.md §1 this is the one judgement in the
   project with no instrument: the oracle proves a patch is not broken and cannot tell you the
   filter is musical, or that the fuzz is the fuzz the prompt described. The ask is now "play
   fourteen files and fill in one column," not "drive the Standalone for forty minutes."
   Everything around the listening is machine work and the machine does it. The listening is
   yours, and is not delegable to a hook, a harness or a model.
