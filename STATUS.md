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
- **Python suite: 445 passed, 4 skipped, 12 deselected.** `tools/check.sh full` green.

---

## Broken — ranked

Registry with IDs, severity and discovery dates: `docs/BUGS.md`.

**1. One generation defect is actually evidenced; the rest is sampling.** *(PF-024/PF-032,
high.)* Karplus-Strong's `recursion_cycle` is the only failure reproducing prompt *and* class
across runs — four archives now. The sidechain compressor fails every run with a *different*
error each time, so it is evidenced as unreliable with no stable signature. **Fixing anything
else on the strength of one run is fixing noise.**

**2. The noise gate still renders silent.** *(PF-032's surviving half, high.)* rms exactly 0.0
in the fresh corpus. The dB unit contract — double `ba.db2linear` — is the standing
hypothesis; `a4f942e` closed the Hz contract and not this one.

**3. The DAW still sees raw slots.** *(follow-up to PF-037, open, unfiled.)* The plugin's own
knobs read `800 Hz`; the host's automation lane still reads `Macro 7: 0.04`. Closing it needs
per-patch metadata reachable from a `stringFromValue` lambda the host may call from any
thread — a concurrency change next to the "parameters are declared once" invariant.

**4. Knob ordering and a dead widget arm.** *(PF-038 low, PF-039 low.)* Lexicographic ordering
now mechanically confirmed; the rotary fallback is unreachable while `docs/ui_design_plan.md`
still calls it the fallback widget.

**5. A per-call output budget cannot be expressed.** *(PF-035, low.)*

**6. `score_efficacy.py --judge` spends quota and takes no lock.** *(unfiled.)* It can
interleave with a live benchmark into the same TPM bucket — PF-030's hazard, in the scorer.

---

## Assumed, never checked

**Three claims, and this session did not move the number.** Stating that plainly because it is
the project's one metric and the temptation is to count the health check as progress against
it. It is not: 289 assertions and a noise floor are evidence *about instruments*, and all
three claims below need generation volume that does not fit in one day's quota.

- **The efficacy pilot generalizes to nothing.** *(PF-011)* The full grid is 25 effects × 5
  tiers = 125 generations ≈ 437k tokens ≈ **2.2 days** on groq. Needs sharding across days.
- **No cross-model comparison exists.** *(PF-012)* **The partial answer on record is now known
  to be worthless.** It read `gpt-oss-120b` 18/20 against `llama-3.3-70b` 17/20 — a
  one-prompt difference, which today's measured noise floor says is exactly one resample.
- **Semantic fidelity is unmeasured.** *(PF-013)* `--judge` has never executed and **cannot
  today**: it must use a model independent of the generator, ollama is not installed, and
  gemini allows 20 requests/day. **Installing ollama is the unlock** — local, unmetered, and
  independent of groq.

## Next three things

1. **Fix Karplus-Strong's `recursion_cycle`.** It is the only generation defect with evidence
   of being real rather than sampled. `.claude/skills/faust-idioms/SKILL.md` already carries a
   compiled pattern for the recursion case and it has never been folded into the prompt.
2. **Fix the noise gate's dB contract** (PF-032's surviving half), and verify with the oracle
   over a fresh corpus rather than the compile rate — a silent render is a property of the
   patch, not of the draw, so the oracle is the firmer instrument here.
3. *(evidence)* **Install ollama and run `--judge`.** It is the single cheapest way to move
   the `assumed` number: it unblocks PF-013 outright and makes PF-011's grid affordable by
   removing the judge from the groq budget entirely.

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
