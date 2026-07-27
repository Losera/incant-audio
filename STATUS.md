# PluginForge — Status  (2026-07-27)

Rewritten each session per COLLABORATION.md §5. Single writer, no merge conflicts.
Narrative history lives in git.

**The fleet is over.** `docs/FLEET.md` and `docs/.fleet/` are deleted; there is no overseer
and no cross-lane request log. Lane names (S1–S7) survive only inside `docs/BUGS.md` as a
record of who did what. Read this file and `docs/BUGS.md`; there is nothing else to sync.

**Start a session with `/orient`**, not by reading this file top to bottom. It injects live
repo state plus the four sections below that are still open, and prints a staleness banner
if this file falls behind HEAD.

---

## Works — and how we know

**The Broken list went from seven entries to one.** That is the headline of the last two
days and it was not a burst of fixes — the fixes had already landed and the registry
hadn't noticed. `1fc1092` (2026-07-27) re-verified ten entries against the code at HEAD:
PF-006, PF-008, PF-015, PF-016, PF-018, PF-019, PF-020, PF-021, PF-022 and PF-023 all said
`open` while every fix was live in the tree. **This is the declared-vs-actual pattern
running backwards** — the registry declared broken what was fixed. Cheaper than the usual
direction, since nobody shipped a dead control, but it still misrouted a day of work.

Each closure below was verified by reading the cited code at HEAD, not by trusting a commit
message.

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
  `clearError()` at `PromptPanel.cpp:200`, before the run starts. An error survives a later
  success within the same run but never survives the next submit.
- **`prepare()` re-inits a live DSP on sample-rate change.** *(PF-018, `be83d1e`.)* It
  computes `rateChanged` before storing members and drives a real re-init when the rate
  moved and a DSP is live.
- **`FaustEngine::process()` has an `activeDSP` null guard.** *(PF-023, `4a84c1c`.)*
- **The enforcement hooks run, and have been seen blocking.** *(2026-07-25, `a5e0275`.)*
  They never had: `.claude/settings.json` declared `PreToolUse` at the file root instead of
  under `"hooks"`, which Claude Code ignores **silently**. Proof is behavioural — a real
  `Bash` call was blocked, and `check_bash_denylist.py` blocked one again on 2026-07-27
  during unrelated work. `tests/test_control_wiring.py` guards both shape and teeth.
- **The RT-safety hook covers the whole audio path.** *(PF-015, `fed704e`.)* `ANCHOR_RE`
  matches all four functions that actually run there — `processBlock`, `FaustEngine::process`,
  `ParamPool::pushToFaust`, `OutputGuard::process` — with a parametrised red case each.
  **Known limitation:** it cannot follow a call graph, so a *fifth* function arriving on the
  audio thread must be added by hand. `.claude/rules/tier2-evidence.md` states this where
  someone editing that code will see it.
- **Project extensions can no longer reference deleted files.** *(2026-07-27, uncommitted.)*
  `attention-report` was deleted: it read CLAUDE.md's "Current status" section (removed
  2026-07-25) and `docs/collaboration_log.md` (retired, deleted), reported the shortfall as
  nothing to report, and tagged findings with the DELEGATE/PAIR/HUMAN-OWNED taxonomy §9
  retired on 2026-07-21. Both agents carried the same retired names, and
  `invariant-hook-writer` proposed a hook for `bench/prompts/system_faust.txt`, deleted six
  days earlier. `tests/test_control_wiring.py` now has two tests for this class, both
  red-cased; the suite is 23 tests.
- **Session start is a computed digest, not a full read.** *(2026-07-27, uncommitted.)*
  `/orient` injects `tools/status_digest.sh` output at load time: repo state, this file's
  open sections, and a staleness banner comparing its date to HEAD. 629 words against this
  file's ~2,000. It **fails loud** — a heading it cannot find prints `MISSING` and exits 1,
  because a digest that silently shrinks reads like good news.
- **Generated DSP is measured as audio.** *(closes the objective half of PF-013.)*
  `bench/render_oracle.py` renders a compiled patch offline (numpy + scipy, no network, no
  quota) and gates NaN/Inf, silence, DC offset, runaway gain. **17 of 17 renderable patches
  produce usable audio.** Calibrated against physics: `fi.resonlp(1000,.707)` measures
  −3.0 dB at its corner and −30 dB two octaves up. **Limit:** zero-input patches (synths,
  5 of 25) cannot be rendered — reported as *unsupported*, never as failures.
  **Read the 17-of-17 carefully (noted 2026-07-27).** The gate renders every compiling patch in
  `bench/results/results.json`, and that file is **25 records, all provider `claude`, all
  timestamped 2026-07-19** — generated by the since-deleted prompt. So the number is a true
  statement about the *render oracle* and says **nothing** about what the current prompt
  produces on a free provider. The gate is real; what it gates is not what the sentence
  implies. Today's baseline run replaces that corpus, after which it begins to mean what it
  claims.
- **One entry point for every check.** `tools/check.sh fast | full | audio | quota`,
  cost-ordered and cumulative. `quota` refuses to spend free-tier requests without
  `--i-authorize-spend`. `tools/check.sh assumed` prints the one number.
- **CI is green.** `ae5d213` passed 2026-07-26, including `build-host`. The five
  `TODO: VERIFY` items about Ubuntu Faust packaging are answered by those runs (PF-016).
- **Python suite: 338 passed, 12 deselected**, re-run 2026-07-27 (was 254 at the last
  rewrite).
- **State persistence.** *(PF-002, `c34bbb6`.)* Versioned ValueTree→XML blob
  (schemaVersion=1); `StatePersistenceTest` round-trips 13/13, ASan/UBSan clean. **The
  format itself is still awaiting human confirmation** — see "Waiting on you."
- **Parameters denormalize into real units** (PF-001, `ParamMap.h`, `ParamMapTest.cpp`);
  **the parameter path is RT-safe** (PF-004); **the compile thread is owned and joined**
  (PF-003, `d10f59e`); **OutputGuard** catches NaN/DC/runaway before the speakers;
  **all 64 params reach the editor** (PF-005, `2e129cd`); **JIT swap is TSan-clean**.
- **System prompt grounded in the real stdlib.** Every `ns.func` resolves against installed
  `/usr/share/faust/*.lib`; all five few-shot examples compile. Hook-enforced.

---

## P6 listening battery — one run, 2026-07-24 (groq / gpt-oss-120b)
**4 clean, 3 flaky, 7 failures of 14.** The 2026-07-21 review predicted it would fail on the
first patch; it did, repeatedly. That is a bad result, not a missing one — PF-008 asked
whether anyone had ever listened, and someone has, so it is discharged.

Everything that run exposed except the generation quality itself has since been fixed: the
timeout cliff (PF-019), the segfault (PF-006), and state contamination (PF-020) are all
closed and verified. **What remains is PF-024.** A second pass is now worth running, but it
is a new question, not the discharged one.

---

## Broken — ranked

Registry with IDs, severity and discovery dates: `docs/BUGS.md`.

**1. Generation produces invalid Faust for whole prompt classes.** *(PF-024, high, open,
`llm/prompts/system_prompt.txt`, found 2026-07-24.)* The only real open defect in the
project. Four recorded signatures from the P6 battery: **#2 ping-pong** →
`endless evaluation cycle`; **#6 cold/glassy** → `2 outputs must equal 1 input` or
`invalid delay parameter range`; **#9** → `syntax error, unexpected IDENT`; **#10 RE-201** →
`syntax error, unexpected WITH`. `bench/results/results.json` adds
`undefined symbol : flanger_mono`.

Verified corrections for four of the five classes now exist in
`.claude/skills/faust-idioms/SKILL.md`, each compiled against Faust 2.85.5 on 2026-07-27 —
but **none of them has been folded into the prompt**, so this entry is open on the evidence
that matters. Fix is Tier 2: it owes a re-run or an explicit statement that the baseline is
stale.

**These read as five unrelated failures. They are largely one gap** (found 2026-07-27): the
prompt barely teaches Faust's routing algebra, and the one language construct it *does* teach
does not exist.
- `system_prompt.txt:21` instructs the model to *"use let bindings or with { } blocks."*
  **Faust has no `let`.** Verified against the installed compiler:
  `process = let g = 0.5; in _ * g, _ * g;` → `syntax error, unexpected IDENT` — the exact
  signature recorded for #9. **The prompt is teaching the failure.**
- The same line recommends `with { }`, which is real, but **none of the five few-shot examples
  contains one**. Being told to use a construct one is never shown is a plausible route to
  #10's `unexpected WITH`.
- `<:` (split) and `:>` (merge) appear **nowhere** in the 173-line prompt; `~` (recursion)
  appears exactly once, buried inside the delay example's body at `:149`, never named or
  explained. `2 outputs must equal 1 input` and `unexpected ARROW` are both that gap.
- Ping-pong is described in prose at `:26-27` and still fails: `endless evaluation cycle` is a
  recursion-topology error, which prose cannot convey and an example can.
- Nothing states that a delay's first argument is a compile-time-constant *maximum* —
  `invalid delay parameter range: interval(0,2.1e9,0)`.

**Why the grounding hook did not catch this.** `tools/gen_stdlib_block.py --verify-prompt`
validates that every `ns.func` reference resolves. It cannot see a hallucinated **language
construct** in prose, so `let` was never in scope. That is a real coverage gap in a control the
project describes as making prompt hallucination impossible — a control that compiled every
construct the prompt recommends would have caught it the day it was written. Worth its own
`/architecture-planning` pass; it is a new enforcement mechanism, not a code change.

---

## Assumed, never checked

Six claims, unchanged from the last rewrite. All six are measurement debt (PF-009–PF-014),
and five of them close on one authorized command.

- **Every benchmark number on record is void.** *(PF-009)* Measured against the deleted
  `bench/prompts/system_faust.txt`, which taught three functions that do not exist.
  `bench/results/.prompt_baseline.json` (0.88) describes nothing that exists.
- **The prompt rewrite is unmeasured.** *(PF-010)* Verified *correct* — references resolve,
  examples compile — not verified *better*.
- **The efficacy pilot generalizes to nothing.** *(PF-011)* N=50, one model, the old prompt,
  two of five categories.
- **No cross-model comparison exists.** *(PF-012)* ADR-008 "Under evaluation" since 2026-04-29.
- **Semantic fidelity is unmeasured.** *(PF-013)* Narrowed, not closed. The objective half
  runs (render oracle, above). Fidelity *to the prompt* does not: `tiered_prompts.json`
  already carries `target` + `expected_primitives`, and nothing turns them into an expected
  spectral signature. The `--judge` rubric has never run.
- **No real user prompt has ever been recorded.** *(PF-014)* `generate.py` logs nothing.

---

## Next three things

1. **Fix PF-024 by grounding the prompt in the four verified patterns.** The compiled
   failing/working pairs are in `.claude/skills/faust-idioms/SKILL.md`; the work is folding
   them into `llm/prompts/system_prompt.txt` as rules or few-shots, keeping every `ns.func`
   resolving. Tier 2 — cite file:line, re-run `tools/check.sh audio`, and either re-run the
   benchmark or declare the baseline stale.
2. **Commit and push this session's extension work.** `1fc1092` is unpushed and roughly a
   dozen paths are uncommitted (`/orient`, `/change-report`, `/faust-idioms`, the Tier-2
   rule, `tools/status_digest.sh`, the two new control-wiring tests, settings, CLAUDE.md).
   CI has not seen any of it.
3. **Run the benchmark twice — authorized 2026-07-27.** `python bench/run_benchmark.py
   --provider groq`, 25 prompts, $0. Once *before* the PF-024 fix and once after, so the
   prompt change is measured rather than asserted. Archive `results.json` first
   (`run_benchmark.py:216` overwrites it unconditionally, and the 2026-07-19 file is the only
   record of the pre-unification prompt). Closes PF-009 and PF-010. Report **per failure
   class**, not as one aggregate — "88%" hid three unrelated failure modes for months.

---

## Waiting on you

Two items, and one of them is smaller than it was. The P6 listening pass that sat here for
weeks is discharged — it ran. The benchmark re-run was authorized 2026-07-27; only the
baseline *overwrite* is still gated.

1. **Confirm the persisted-state format (§2 trigger-3).** Code is in `c34bbb6`, fully
   implemented and verified (13/13, ASan/UBSan clean). The *design* — schemaVersion=1
   ValueTree→XML, Faust source + prompt as attributes, verbatim `<STATE>`, `<SlotLabels>`
   hint — is a contract between components, so it needs your knowing approval, not a
   plan-mode sign-off (`docs/BUGS.md:492-494`). Cheap to amend now; v1 is the only blob in
   the wild.
2. **Overwriting `bench/results/.prompt_baseline.json`** once today's numbers exist. The
   benchmark re-run itself was **authorized 2026-07-27** (twice: a baseline, then a delta run
   after the PF-024 fix), so PF-009/PF-010 are no longer waiting on you. Replacing the stored
   0.88 is the **separate** §2 trigger-1 act, and it still is. I will ask again with the new
   figures in hand.

*Optional, not blocking:* a second P6 listening pass. Every reliability defect the first run
exposed is now closed, so a second pass would measure generation quality rather than crashes
— but it is worth more after PF-024 is fixed than before.
