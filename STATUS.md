# PluginForge — Status  (2026-07-31)

Rewritten each session per COLLABORATION.md §5. Single writer, no merge conflicts.
Narrative history lives in git.

**Start a session with `/orient`**, not by reading this file top to bottom. It injects live
repo state, **the CI line**, this file's open sections, and a staleness banner if it falls
behind HEAD.

---

## Works — and how we know

**This session finished verifying work that was written but never run.** The tree arrived
carrying ~1,400 uncommitted lines across three threads — a regenerated prompt, a rewritten
render oracle, and a UI thread — all of it green on `check.sh full`, and one of its central
tests had never executed. That is the headline: **the gates were green because the test was
not called** (PF-047).

- **The control-style selector is real and now proven.** *(PF-047 fixed.)* The editor cycles
  Knobs: auto → rotary → sliders; the style is a *view* property that never reaches the DSP
  (`PluginProcessor.h:116-126` — deliberately not an `AudioParameterChoice`, and `setUiStyle`
  holds no handle on `FaustEngine`, so a style change is structurally incapable of touching
  audio). `scenario13_styleSwitchDoesNotThrash` asserts the load-bearing claim — a flip
  restyles widgets *in place*, so every `SliderAttachment` survives and no parameter moves —
  and it had been defined at `EditorSessionTest.cpp:759` and **never called**. Wired in: 32
  checks, green. `EditorSessionTest` went **68 → 107 checks over 14 scenarios**.
- **Faust group structure is captured, and asserted.** `ParamCapture`'s four box callbacks
  were empty bodies; they now maintain a group stack (`FaustEngine.cpp:15-42`) and stamp
  `ParamInfo::group` (`:105`). `scenario14_groupCapture` pins it against values read off
  `faust -lang cpp` for the exact patch rather than guessed: the compiler's filename wrapper
  is dropped, `vgroup("Amp")` → `"Amp"`, nesting joins outermost-first → `"Osc/Tune"`, and a
  param outside every group reports `""`. **Seen failing:** stubbing `info.group` to `""`
  turns it red on exactly the two grouped cases and leaves both fallback cases green.
  Nothing lays out by this field yet — it is the input a sectioned surface needs.
- **The control style survives a project reload, and the panel is what proves it.**
  `uiStyle` rides the state blob as a v1 amendment (`PluginProcessor.cpp:307`); an old blob
  lacks the attribute and defaults to `faithful`, which is what those blobs were saved under.
  `scenario10_stateRoundTrip` now asserts the reopened **panel** is in the restored style, not
  merely that the processor kept the string — the restore fires `onUiStyleChanged` *before*
  the recompile so rebuilt widgets are styled on their first frame, and only a panel-level
  assertion distinguishes that from a value that persisted and never applied.
- **The UI design loop exists as one command.** `tools/ui_iterate.sh` builds `UiDesignGallery`,
  renders the real editor headless against five fixtures × three styles, and writes
  `artifacts/ui_gallery/{*.png,index.html,manifest.json}` plus a layout diff against a
  committed reference. **15 rendered, 0 broken, `no change`.** It exits 0 on layout drift by
  design — drift is the point when you are iterating, and a loop that goes red on every moved
  control is a loop that gets commented out.
- **The instrument stopped mislabelling its own evidence.** *(PF-048 fixed.)* `groups:` was
  printed before its own record's header (which cannot print until the snapshot supplies
  pixel dimensions), so every group line attached to the **previous** record — the console
  said `03__horizontal` had 04's sections and `04__horizontal` had none, while `manifest.json`
  from the same run had all fifteen correct. Fix confirmed print-only: layout diff still
  `no change`.
- **The render oracle can see time.** A `tail()` layer renders a second **burst** probe —
  drive for 250 ms, then silence, and *the silence is the measurement* — because every gate
  before it was computed from a continuous probe and nothing decays while the input is still
  going. One gate comes out of it (`never_decays`); "has a tail" is deliberately **not** a
  gate, because a gain plugin correctly has none, and when no expectation is stated the report
  **omits the key** rather than reporting a pass. Self-test green including two red cases that
  are green on every pre-existing gate: `dead_delay` (compiles, not silent, unity gain, no
  tail) and `never_decays` (unity loop gain).
- **The corpus driver exists once instead of twice.** `check.sh` and `health_report.py` each
  inlined their own loop over `analyse()`, recomputing the same gate logic in two places free
  to drift; both now call `render_oracle.analyse_corpus`.
- **The prompt was regenerated under the Faust the box actually has.** The stdlib block is
  stamped 2.85.9, not the stale 2.85.5 CLAUDE.md flagged. Thirteen entries were trimmed **on
  evidence, not taste** — zero appearances across ~134k characters of generated code in every
  archive, *and* each has a sibling still listed showing the same call shape. That bought the
  headroom for a `~` feedback arity rule. Slack **124 → 483 tokens**; block 5737 → 4505 chars.
- **The prompt's benchmark statement is paid, and the answer is again "no movement".**
  ollama `qwen2.5-coder:7b-16k`, 25 prompts: **80%**, the same as the two runs before it, with
  the same failure profile — three `sequential composition` arity errors, one timeout, one new.
  Two consecutive prompt rules aimed at this model have now measurably done nothing. That
  reads as a 7B instruction-following limit, consistent with PF-012.
- **The audio path.** Editor generate thread owned and joined (PF-006); timeout cliff closed
  (PF-019); Fresh/Iterate modes (PF-020); source-of-record commits only on compile success
  (PF-022); stale errors clear on submit (PF-021); `prepare()` re-inits on rate change
  (PF-018); `process()` null guard (PF-023); RT-safety hook covers all four audio-thread
  functions (PF-015); state persistence (PF-002); params denormalize (PF-001); all 64 params
  reach the editor (PF-005); JIT swap TSan-clean.
- **Carried forward from 2026-07-30, unchanged:** the noise floor is real and provider-side
  (PF-031 — two ollama runs byte-identical in 20 of 25 generations; groq's two overlapped on
  2, so "temperature 0 is not reproducible" is a fact about groq); the cross-model comparison
  exists (PF-012 — a 7B CPU model lands within one prompt of a 120B cloud model); PF-036 is
  confirmed green on the exact `AMD EPYC 9V74` whose masked AVX-512 caused the SIGILL; 96
  assertions that ran nowhere now run; pluginval passes at strictness 10.
- **Suite: 459 passed, 12 deselected.** `tools/check.sh full` green — run three times today,
  including twice after the test wiring changed. `tools/check.sh audio` **red**, see below.

---

## Broken — ranked

Registry with IDs, severity and discovery dates: `docs/BUGS.md`.

**1. `check.sh audio` is red, and measures the wrong thing.** *(PF-045 medium, PF-046
medium — both new today.)* The level gates on `bench/results/results.json`, which every
benchmark run overwrites, so its verdict is a property of the last draw. Same command, two
corpora, measured today: HEAD's corpus **3 failed** (runaway gain, two silent); the working
tree's **1 failed**. Both red, disjoint failure sets, same harness and same code.
The surviving failure is **PF-045**, a genuine unit-contract error — a generated ADSR passes
`releaseTime * ma.SR / 1000.0` to `en.adsr`, whose times are in **seconds**, making a
declared 1000 ms release into 48,000 s; the envelope holds sustain forever as DC. Same class
as PF-032, and found by the new tail gate on its first corpus run. **Do not read a green
`full` as covering the audio path.**

**2. One generation defect is actually evidenced; the rest is sampling.** *(PF-024/PF-032,
high.)* Karplus-Strong's `recursion_cycle` is the only failure reproducing prompt *and* class
across runs — four archives. The sidechain compressor fails every run with a *different*
error, so it is evidenced as unreliable with no stable signature. **Fixing anything else on
the strength of one run is fixing noise.**

**3. The noise gate still renders silent, and the dB hypothesis is disproved.** *(PF-032's
surviving half, high.)* The cause is `ef.gate_stereo(threshold, attack, hold, release, _, _)`
— the signal written into the argument list where `misceffects.lib:159` documents
`_,_ : gate_stereo(t,a,h,r) : _,_`. The prompt rule targeting exactly this was measured as
not working on ollama and is retained only pending a groq run.

**4. The DAW still sees raw slots.** *(follow-up to PF-037, open, unfiled.)* The plugin's own
knobs read `800 Hz`; the host's automation lane still reads `Macro 7: 0.04`. Closing it needs
per-patch metadata reachable from a `stringFromValue` lambda the host may call from any
thread — a concurrency change next to the "parameters are declared once" invariant.

**5. Knob ordering.** *(PF-038 low.)* Now understood rather than merely observed: the order is
**Faust's own**, emitted alphabetically per group and per widget within a group
(`FaustEngine.h`, `ParamInfo::group` ordering note) — `ParamGridPanel` does not sort. PF-039
is closed by the same reading; the rotary arm is no longer dead code, since `ControlStyle::Rotary`
now reaches it deliberately.

**6. A per-call output budget cannot be expressed.** *(PF-035, low.)*

**7. The only fidelity instrument is not interpretable.** *(PF-041 high, PF-042 medium.)* The
judge grades L4 against its own generation prompt (10/10 byte-identical) and returns the
middle of its three-point scale once in 44.

**8. The declared ollama model cannot hold its own prompt.** *(PF-043, medium.)* Eased but not
closed: the prompt shed 1,200 characters today, so the squeeze against a 4,096-token context
is looser. The arithmetic still does not fit with the 4,096 output floor.

**9. `score_efficacy.py --judge` spends quota and takes no lock.** *(unfiled.)* It can
interleave with a live benchmark into the same TPM bucket — PF-030's hazard, in the scorer.

---

## Assumed, never checked

**Two claims. The number did not move today** — this session bought verification of code that
already existed, not new evidence about generation quality. Said plainly because the metric
exists precisely so that a productive session cannot disguise itself as a measured one.

- **The efficacy pilot generalizes to nothing.** *(PF-011)* The full grid is 25 effects × 5
  tiers = 125 generations ≈ 437k tokens ≈ **2.2 days** on groq. Needs sharding across days —
  **or ollama**, which is unmetered and, per PF-012, near-deterministic.
- **Semantic fidelity is unmeasured.** *(PF-013)* Half closed 2026-07-30: the judge ran, 44
  records graded, 0 errors. Still blocked — **on the instrument, not on quota** — by PF-041
  (L4 graded against a ground truth byte-identical to its own generation prompt) and PF-042
  (the 0/1/2 rubric returns `1` once in 44).

## Next three things

1. **Decide what `check.sh audio` should gate on (PF-046), then fix PF-045 or don't.** The
   level is red on every corpus anyone has drawn, which is how a rung stops being run. The
   cheap version is a small committed corpus for the ladder and the mutable `results.json` for
   reporting; the expensive version is a prompt rule for `en.*` time units, which costs
   headroom and which the last two rules' results argue against.
2. **Fix Karplus-Strong's `recursion_cycle`.** Still the only generation defect with evidence
   of being real rather than sampled. `.claude/skills/faust-idioms/SKILL.md` already carries a
   compiled pattern for the recursion case and it has never been folded into the prompt.
3. *(evidence)* **Fix the judge before using it — PF-041 first.** ollama is installed and the
   judge demonstrably runs, so the obstacle is the rubric, not the budget. PF-041 needs a
   ground truth per effect that is **not** any tier's prompt; PF-042 needs the middle score to
   become reachable. Only then is a fresh efficacy run worth its generations. Still the
   cheapest route to the `assumed` number.

## Waiting on you

1. **Nothing is pushed.** Three commits are staged in history and `origin/main` is still at
   `a451350`. Per CLAUDE.md *done means pushed and green* — and note that `check.sh audio` is
   red for the PF-046 reason, so "green" needs your ruling before this counts as done.

2. **Look at the gallery.** `artifacts/ui_gallery/index.html` — five fixtures × three control
   styles, the first time the editor's layout alternatives have been visible side by side.
   This is the one judgement with no instrument (COLLABORATION.md §1): the manifest proves the
   controls are present, correct and correctly grouped, and cannot tell you which style a
   musician would rather use. **The `horizontal` style at 40 params (480×920) is the case I'd
   look at first** — it stacks one full-width row per control and the window hits its height
   cap.

3. **`bench/results/.prompt_baseline.json` is still untouched**, deliberately. It records
   `0.88` for the deleted pre-unification prompt. Now that the noise floor is known, replacing
   it should record a *spread*, not a point.

4. **A P6 listening pass.** `bench/p6_capture.py --i-authorize-spend` is built and dry-run; it
   costs 14 generations and hands over WAVs plus a scorecard with one empty column. The oracle
   proves a patch is not broken and cannot tell you the filter is musical.

5. **`UDHR.md` is a 1-byte stub** and `IDEAS.md` is your idea dump — both untracked, both left
   alone. Your seventh idea says "consult the universal declaration of human rights"; the file
   is empty, so I did not act on it.
