# PluginForge — Status  (2026-07-30)

Rewritten each session per COLLABORATION.md §5. Single writer, no merge conflicts.
Narrative history lives in git.

**Start a session with `/orient`**, not by reading this file top to bottom. It injects live
repo state, **the CI line**, this file's open sections, and a staleness banner if it falls
behind HEAD.

---

## Works — and how we know

**This session measured the instrument instead of the thing, and the instrument was the
problem.** A three-prong health check (DSP / AI / UI) produced
`artifacts/health/health_20260730.md`. The headline is not any of the rates. It is that
**run-to-run instability is large enough that most per-class claims this project has made
were reading sampling noise** — see PF-031 below, which is the most consequential closure
here.

- **The cross-model comparison exists, and the small model located a real prompt gap.**
  *(PF-012 closed, pending commit.)* Two runs each: groq `gpt-oss-120b` 88%/84%, local
  `ollama/qwen2.5-coder:7b` **80%/80%**. A 7B CPU model lands within one prompt of a 120B
  cloud model. **The profiles are disjoint and the small one is diagnostic:** four of ollama's
  five failures are `sequential composition` arity errors, two on *trivial*-category prompts
  (a mute toggle, a polarity inverter).
  **A first reading of this — committed, then corrected the same evening — said the prompt
  teaches no routing algebra. It does** (`system_prompt.txt:37-47`, all five operators plus
  the `_,_ : E` warning); that claim was quoted from PF-024's July 27 description of a prompt
  `a4f942e` has since changed. Reading the generated code instead gives a better answer:
  `ba.bypass2(mute, _ , _)` and `ef.gate_stereo(t,a,h,r,_,_)` both write the **signal into the
  argument list** of a stdlib effect whose signal arrives by composition
  (`misceffects.lib:159`). **One root cause, two models, three failures** — including PF-032's
  last silent render — and genuinely untaught. **A prompt rule targeting it was then written
  and measured as NOT working** (ollama 80% → 80%, both target cases byte-unchanged): the
  model was told in the imperative, with a worked example, and did it anyway. That reads as a
  7B instruction-following limit rather than a prompt gap. Retained only pending a groq run.
- **The benchmark noise is provider-side, not inherent.** *(PF-031 amendment.)* The same two
  ollama runs were byte-identical in **20 of 25 generations**, with identical failure sets and
  classes; groq's two runs overlapped on 2 prompts. So "temperature 0 is not reproducible" is
  a fact about groq, not about LLMs. **Consequence:** prompt-regression testing need not be
  statistical — a local provider answers "did this edit change the output?" exactly, for free,
  by byte-diff. `check_prompt_regression.py` spends 9 groq generations to answer that badly.
- **The noise floor exists, and it retires several standing claims.** *(PF-031, pending
  commit.)* Two back-to-back 25-prompt runs, same prompt, same corpus, unchanged tree:
  **88%** and **84%**. Rate spread **4 points — one prompt**. Worse, of five prompts that
  failed at all, **exactly one failed both times with the same class**. So a per-class count
  read off a single run is mostly a draw, and `--compare` between two single runs compares
  two draws. The replacement rule: *a defect is evidenced when the same prompt fails across
  repeated runs with the same class.* On today's data that is **one** — Karplus-Strong's
  `recursion_cycle`, stable across four archives.
- **The `a4f942e` prompt edit is measured at last, and it half-worked.** *(PF-024/PF-032.)*
  The Tier-2 benchmark statement owed since 2026-07-29 is paid. **Fixed:** the Hz unit
  contract (PF-032's warm low-pass has not recurred; oracle over the fresh corpora reports
  18/1/3 and **17/0/4** against 07-28's 16/2/4), `select2` (the sidechain compressor no
  longer emits a ternary — it fails later and differently), and ping-pong, which compiled in
  both runs. **Not fixed:** the dB contract — the noise gate still renders at rms *exactly*
  0.0 — and Karplus-Strong.
- **PF-036 is confirmed end to end.** CI run `30577386079` is **green on an `AMD EPYC 9V74`**,
  the exact CPU whose masked AVX-512 caused the SIGILL. That was named in advance as the only
  acceptable proof, and it arrived unsought two pushes later. The mechanism had already been
  reproduced on the CI toolchain; this closes the other end.
- **96 assertions that ran nowhere now run.** `OutputGuardTest` (17), `ParamMapTest` (46) and
  `StatePersistenceTest` (33) had CMake targets and appeared in neither `check.sh` nor the
  workflow. This is PF-029 inverted — there CI ran what the ladder did not, here *nothing*
  ran them. All three are green, and `StatePersistenceTest` needed adding to the PF-036
  preload list because it JITs.
- **The health check is one command and emits an artifact.** `tools/health_report.py` runs
  three lanes and writes `artifacts/health/health_<date>.{json,md}`. Every C++ harness now
  prints one `PF_SUMMARY harness=… checks=… failures=…` line, which turns 289 existing
  boolean assertions into countable numbers. **A harness that did not run is recorded as
  ABSENT, never as zero** — `--strict` makes absence an error, because a silent zero reads as
  health and that is this project's signature defect.
- **Today's totals: 289 assertions, 0 failures.** DSP 211 (`OfflineRenderTest` 106,
  `ParamMapTest` 46, `StatePersistenceTest` 33, `OutputGuardTest` 17, `JitTargetTest` 9) plus
  TSan clean; UI 78 (`EditorSessionTest` 69 over 12 scenarios, `PromptPanelThreadingTest` 9).
- **PF-038 has a number instead of a log line.** The knob order the harness had been printing
  as an unasserted fact is now captured and flagged: `Bypass, Cutoff, Level, Trigger, Voices`,
  **lexicographic: true**.
- **The knobs say what they mean, and it exposed PF-040.** *(`9509963`.)* `ParamMap::formatZone`
  on each slider's `textFromValueFunction`. The new test then failed on 819 Hz:
  `AudioParameterFloat`'s bare min/max ctor hardcodes `interval 0.01`
  (`juce_AudioParameterFloat.cpp:76`), so every slot had **101 positions** for the project's
  whole life. Both fixed.
- **The audio path.** Editor generate thread owned and joined (PF-006); timeout cliff closed
  (PF-019); Fresh/Iterate modes (PF-020); source-of-record commits only on compile success
  (PF-022); stale errors clear on submit (PF-021); `prepare()` re-inits on rate change
  (PF-018); `process()` null guard (PF-023); RT-safety hook covers all four audio-thread
  functions (PF-015); state persistence (PF-002); params denormalize (PF-001); all 64 params
  reach the editor (PF-005); JIT swap TSan-clean; prompt grounded in the real stdlib.
- **Python suite: 450 passed, 12 deselected.** `tools/check.sh full` green.

---

## Broken — ranked

Registry with IDs, severity and discovery dates: `docs/BUGS.md`.

**1. One generation defect is actually evidenced; the rest is sampling.** *(PF-024/PF-032,
high.)* Karplus-Strong's `recursion_cycle` is the only failure reproducing prompt *and* class
across runs — four archives now. The sidechain compressor fails every run with a *different*
error each time, so it is evidenced as unreliable with no stable signature. **Fixing anything
else on the strength of one run is fixing noise.**

**2. The noise gate still renders silent, and the dB hypothesis is now disproved.**
*(PF-032's surviving half, high.)* rms exactly 0.0 in the fresh corpus — but today's gate
passes `threshold` raw in dB, which is **correct**; the prompt's unit rule worked and the
double-`ba.db2linear` bug is gone. The real cause is
`ef.gate_stereo(threshold, attack, hold, release, _, _)` — the signal written into the
argument list where `misceffects.lib:159` documents `_,_ : gate_stereo(t,a,h,r) : _,_`.
A prompt rule targeting exactly this was added and **measured as not working on ollama**
(80% → 80%, the two target cases unchanged); it is retained only pending a groq run, and
must be reverted if that shows nothing. See PF-024.

**3. The DAW still sees raw slots.** *(follow-up to PF-037, open, unfiled.)* The plugin's own
knobs read `800 Hz`; the host's automation lane still reads `Macro 7: 0.04`. Closing it needs
per-patch metadata reachable from a `stringFromValue` lambda the host may call from any
thread — a concurrency change next to the "parameters are declared once" invariant.

**4. Knob ordering and a dead widget arm.** *(PF-038 low, PF-039 low.)* Lexicographic ordering
now mechanically confirmed; the rotary fallback is unreachable while `docs/ui_design_plan.md`
still calls it the fallback widget.

**5. A per-call output budget cannot be expressed.** *(PF-035, low.)*

**6. The only fidelity instrument is not interpretable.** *(PF-041 high, PF-042 medium.)* The
judge grades L4 against its own generation prompt (10/10 byte-identical) and returns the
middle of its three-point scale once in 44. Both found by running it for the first time.

**7. The declared ollama model cannot hold its own prompt.** *(PF-043, medium.)* 3,614-token
system prompt + a 4,096 output floor against a 4,096-token context. This is **PF-035 acquiring
a real cost** — that entry was filed `low` on "costs nothing measurable today."

**8. `score_efficacy.py --judge` spends quota and takes no lock.** *(unfiled.)* It can
interleave with a live benchmark into the same TPM bucket — PF-030's hazard, in the scorer.
Now more pressing: the judge is no longer hypothetical.

---

## Assumed, never checked

**Two claims. The number moved from 3 to 2** — PF-012 closed, on real data with error bars.

- **The efficacy pilot generalizes to nothing.** *(PF-011)* The full grid is 25 effects × 5
  tiers = 125 generations ≈ 437k tokens ≈ **2.2 days** on groq. Needs sharding across days —
  **or ollama**, which is unmetered and, per PF-012, near-deterministic.
- **Semantic fidelity is unmeasured.** *(PF-013)* **Half of this closed 2026-07-30.** ollama
  was installed and the judge ran for the first time in the project's history — 44 records
  graded, 0 errors. It works. But the measurement is still absent, and the reason changed:
  it is **no longer blocked on quota, it is blocked on the instrument.** Running it exposed
  **PF-041** (L4 is graded against a ground truth byte-identical to its own generation prompt,
  10/10, so it scores 2.00/2.00 tautologically and the tier gradient is confounded) and
  **PF-042** (the 0/1/2 rubric returns `1` once in 44 — a boolean wearing a three-point
  costume). Producing fresh tier numbers before fixing those would ship authoritative-looking
  figures that are partly tautology.

## Next three things

1. **Fix Karplus-Strong's `recursion_cycle`.** It is the only generation defect with evidence
   of being real rather than sampled. `.claude/skills/faust-idioms/SKILL.md` already carries a
   compiled pattern for the recursion case and it has never been folded into the prompt.
2. **Fix the noise gate's dB contract** (PF-032's surviving half), and verify with the oracle
   over a fresh corpus rather than the compile rate — a silent render is a property of the
   patch, not of the draw, so the oracle is the firmer instrument here.
3. *(evidence)* **Fix the judge before using it — PF-041 first.** ollama is installed and the
   judge demonstrably runs, so the remaining obstacle is the rubric, not the budget. PF-041
   needs a ground truth per effect that is **not** any tier's prompt; PF-042 needs the middle
   score to become reachable. Only then is a fresh efficacy run worth its generations. This is
   still the cheapest route to the `assumed` number — it is just one step longer than it
   looked this morning.

## Waiting on you

1. **`bench/results/.prompt_baseline.json` is still untouched**, deliberately. It records
   `0.88` for the deleted pre-unification prompt. Now that the noise floor is known, replacing
   it should record a *spread*, not a point — a single number there is what made
   `0.84 → 0.88` readable as movement in the first place.

2. **A P6 listening pass.** `bench/p6_capture.py --i-authorize-spend` is built and dry-run;
   it costs 14 generations and hands over WAVs plus a scorecard with one empty column. Per
   COLLABORATION.md §1 this is the one judgement with no instrument: the oracle proves a patch
   is not broken and cannot tell you the filter is musical. Not fired today — the quota went
   to the noise floor.

3. **Two seconds on `artifacts/images/session_12_readout.png`.** The readout is asserted to
   report the right strings; whether the grid *looks* right is not measurable here.
