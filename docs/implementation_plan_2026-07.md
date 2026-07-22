# PluginForge Implementation Plan — 2026-07

**Status:** approved 2026-07-21. Not yet started — no source, prompt, or hook file was
modified in the session that produced this plan.
**Verified against:** working tree at commit `cc342c2`.

---

## Context

An internal architecture review (`docs/architecture_review_2026-07-21.md`) was adversarially
verified by a reviewer with no repo access. This plan implements the merged result, after
re-verifying every finding against real code.

The prototype reached end-to-end generation on a free provider on 2026-07-21, and the first
generated patch was heard the same day (offline render). That moves the project from "does it
work at all" to "is it correct, safe, and measurable." This plan covers that transition.

**Structural note:** the brief's week-by-week schedule has been dropped per instruction.
Work is ordered by **priority tier** and by **dependency**, which is the part of scheduling
that carries information here. Calendar dates would be fiction for a one-engineer project.

---

## Phase 0 — Verification findings

Every item below was checked against source. Citations are `file:line`.

### Confirmed as reported

**Finding A — parameter denormalization. All four sites confirmed.**

| # | Site | Evidence |
|---|---|---|
| 1 | Slots constructed 0–1 | `host/Source/PluginProcessor.cpp:28-32` — `AudioParameterFloat(slotId(i), "Macro N", 0.0f, 1.0f, 0.0f)` |
| 2 | Metadata discarded at remap | `host/Source/ParamPool.cpp:34-40` — only `p.label` is stored; `init/min/max/step/kind` dropped via `juce::ignoreUnused(p)` |
| 3 | Raw `fp->get()` pushed | `host/Source/ParamPool.cpp:72-74` — `engine.setParamValue(labels[i], fp->get())` |
| 4 | No mapping in MapUI | `/usr/include/faust/gui/MapUI.h` `setParamValue` — `*iter->second = value`, no scaling, **no clamping** |

The bug is live and total: a slider at knob midpoint sends `0.5` into a zone whose Faust range
is `20..20000 Hz`, yielding 0.5 Hz.

**Editor normalization path (asked for explicitly): `host/Source/PluginEditor.cpp:295-301`,
in `refreshParamKnobs`. It is linear** — `norm = (p.defaultValue - p.min) / (p.max - p.min)`.
Its own comment (`:292-294`) anticipates "a denormalising pushToFaust." This is the second
consumer that must share the mapping pair.

**Finding B — lifetime. Confirmed on both sides.**
- Detached compile thread capturing `this`: `FaustEngine.cpp:93` (`std::thread([this, ...]`)
  → `:177` (`.detach()`).
- Non-joining destructor: `FaustEngine.cpp:52-57` — deletes `activeDSP` and `factory` with no
  join and no in-flight check. A compile landing after destruction writes through freed
  members and locks a destroyed `compileMutex`.
- Editor subprocess thread: `PluginEditor.cpp:93` captures `&proc` **by raw reference**
  (bound at `:91`), calls `proc.loadFaustCode(faustCode)` at `:190`, `.detach()` at `:204`,
  with a 120 s wait at `:124`. The in-code justification (`:87-90`) argues only that the
  *processor outlives the editor* — true, and irrelevant. Neither outlives a **detached
  thread** that can still be waiting 120 s after the host tore the plugin down.
- Additional: `juce::ChildProcess child` is thread-local (`:95`); on teardown nothing kills
  it, orphaning a `python3` process.

**Finding C — prompt drift. Confirmed, and materially larger than "drift."**
`llm/prompts/system_prompt.txt` is 40 lines; `bench/prompts/system_faust.txt` is 58. The bench
file contains a **16-line `AVAILABLE STDLIB HIGHLIGHTS` block** (real signatures for
`fi.resonlp`, `ef.ping_pong`, `co.compressor_stereo`, …) that the **product prompt does not
have at all**, plus stereo-wiring rules and different few-shot examples.

**Finding D — state stubs empty.** `host/Source/PluginProcessor.h:30-31` — both
`getStateInformation` and `setStateInformation` are `{}`.

### Contradictions and corrections — read before planning work

**① Old-DSP reclamation is NOT a race. Do not open a workstream for it.**
The swap path is correctly fenced. `processBlock` brackets *all* engine use in
`enterAudio()/exitAudio()` (`PluginProcessor.cpp:57-68`); `enterAudio` is a proper seq_cst
Dekker handshake (`FaustEngine.h:69-78`); `compile()` sets `ready=false` seq_cst then drains
`audioBusy` to zero (`FaustEngine.cpp:137-145`) *before* swapping (`:149-154`) and deleting
(`:174-176`). `delete old` occurs after `ready=true`, but `old` is unreachable by then. This
is sound. The genuine lifetime defect is the **destructor**, which is Finding B — reclamation
folds in there as scope, not as a new race.

**② FTZ/DAZ is already done.** `PluginProcessor.cpp:51` — `juce::ScopedNoDenormals` covers
the whole block including `compute()`. **A2 reduces to limiter + DC blocker + NaN/pin
watchdog.** Planning FTZ/DAZ again would be duplicated work.

**③ Finding C is a validity problem, not only a hygiene problem — it changes priority.**
Every published number in this repo — the 0.88 baseline, the ADR-009 verdict, the efficacy
pilot — was produced with the **bench** prompt, which is stdlib-grounded. The product ships
the **ungrounded** prompt. `bench/run_efficacy_study.py:11-13` states this as a deliberate
confound control: it avoids `llm/generate.py` precisely so the tier variable isn't confounded.
The consequence was not drawn: *no measurement in this project describes the product.*

This is directly corroborated by live evidence gathered 2026-07-21 on the product path:
`llama-3.3-70b-versatile` hallucinated `ba.log2linear`/`ba.linear2log`, and `gpt-oss-120b`
hallucinated `smoothclip` — both **undefined-symbol** failures, exactly the class a stdlib
block suppresses. **C is promoted to P1.**

**④ The sync hook is honestly scoped, not falsely assuring — but insufficient.**
`.claude/hooks/check_adr009_prompt_sync.py:8-12` states plainly that it checks only that the
ADR-009 *rule text* stays present and identical. It does that correctly, and it currently
**passes** on two files differing by 18 lines including the entire stdlib block. The false
assurance is at project level ("prompt sync is hooked"), not in the hook's own claims. Frame
any replacement as *widening scope*, not *fixing a lie*.

### New findings the review missed

**⑤ `pushToFaust` is worse than per-block string lookups.** `ParamPool.cpp:72` does a
`dynamic_cast` per param per block; `:69` a `std::string` compare; then `MapUI::setParamValue`
does **up to three `std::map<std::string,…>` lookups** (path → shortname → label) and, on
miss, calls **`fprintf(stderr, …)` — an I/O syscall on the audio thread**. That fprintf is the
mechanism behind the historical ~1,100-error episode in `CLAUDE.md`. Pointer caching removes
the allocation-free-but-unbounded lookup *and* makes audio-thread I/O structurally impossible.

**⑥ Slots 8–63 never receive their patch defaults.** `PluginEditor.h:36` sets `MAX_KNOBS = 8`;
`refreshParamKnobs` (`PluginEditor.cpp:277`) loops only to `MAX_KNOBS`. `POOL_SIZE` is 64
(`ParamPool.h:14`). A 12-param patch leaves slots 8–11 at 0.0. Under today's raw push that is
merely wrong; under a *denormalizing* push, 0.0 maps to each zone's **minimum** — a cutoff
pinned at 20 Hz, i.e. silence. **This makes ⑥ a release-blocker for A, not a nicety.**

**⑦ Two duplicate retry loops.** `generate.py:92-100` (`generate_with_retry`) and `:116-123`
(`generate_json`) implement the ADR-005 loop separately. Any cascade work touches both or
unifies them first.

**⑧ RT-safety hook cannot see helpers.** `.claude/hooks/check_rt_safety.py:30-40` scopes only
`FaustEngine::process` and `*::processBlock` bodies, and documents that helper functions are
invisible. A limiter or mapping function in a helper **evades the guard entirely**.

**⑨ Stale hook docstring.** `check_rt_safety.py:7-9` justifies its anchoring by "a stray
`ParamPool::pushToFaust()` definition in the same file." That fragment was removed 2026-07-16;
`FaustEngine.cpp` (178 lines) no longer contains it.

**⑩ `generate_faust` already sends a fresh single turn.** `generate.py:66-69` composes
`user_prompt + error` into one message with no history replay. ③'s "never replay chat history"
requirement is *already* the architecture; only provider escalation and including the *last
failed code* are new.

---

## Hard constraints (restated, not relitigated)

1. **ADR-001…011 are settled**: Faust DSL output, JUCE 7, 64 macro slots, 3-attempt retry,
   argv one-shot subprocess, free-only providers. No task reopens these.
2. ~~**`llm/prompts/system_prompt.txt` is human-authored IP.** No task in this plan assigns
   any agent to edit it; improvements reach the model through context injection at retry
   time, never through the prompt file.~~
   **SUPERSEDED 2026-07-21, same day this plan was written.** COLLABORATION.md revision 2
   §1 removed authorship gating entirely — "Claude has write access to every file in this
   repository, including the real-time audio path and the generation prompts" — and
   `protect_human_owned.py` was unregistered from `.claude/settings.json` and retired.
   The prompt is directly editable.

   The replacement guarantee is stronger, and is about content rather than authorship:
   `check_prompt_invariants.py` (registered) and `tests/test_prompt_stdlib.py` require
   every `ns.func` in the prompt to resolve against the installed stdlib and every
   few-shot example to compile. The old gate protected the file from being edited without
   protecting it from being wrong — the product prompt carried no stdlib grounding at all
   while the bench prompt did, which is a direct cause of the `smoothclip` /
   `ba.log2linear` / `flanger_mono` hallucination class.

   **What still applies:** the prompt's failure mode is silent and statistical — a bad
   edit does not fail a test, it costs a few points of success rate discovered weeks
   later. So prompt changes carry the §3 Tier 2 evidence bar (before/after measurement),
   and the binding constraint on that is quota, not permission. This invalidates the
   premise of workstream ⑤ below, which was designed around the edit ban; see the note
   there.
3. **Quota is binding.** Gemini 5 rpm / **20 req/day per model** (measured, `quotaValue: 20`);
   Groq ≈14,400/day with an 8000 TPM cap; OpenRouter ≈50/day; Ollama local. No task spends
   Gemini casually. Bulk runs go to Groq.
4. **One engineer + agents, working prototype, no shipped users.** Distribution-blocking
   concerns are listed as decisions, not built.
5. **RT rules hold**: no allocation, lock, I/O, or unbounded work on the audio thread; the
   libfaust acquire/release swap ordering in `FaustEngine::compile` is not to be re-derived.

---

## P0 — Correctness and safety

Nothing downstream is trustworthy until these land. A and A2 are independent of each other
and can proceed in parallel; both are independent of B.

### A. Parameter denormalization

**Touchpoints**
- **New** `host/Source/ParamMap.h` — header-only, no JUCE-audio deps so it is unit-testable:
  - `float mapSlotToZone(float norm01, const FaustEngine::ParamInfo&)`
  - `float mapZoneToSlot(float zoneValue, const FaustEngine::ParamInfo&)`
  - `enum class Curve { Linear, Log, Exp }` + `Curve curveFor(const ParamInfo&)`
- `ParamPool.h/.cpp` — `remap()` must **retain the full `ParamInfo`**, not just the label
  (fixes finding-A site 2). Extend the double-buffer to hold `std::vector<ParamInfo>`; the
  existing release/acquire index protocol (`ParamPool.cpp:26,50,65`) is correct and is
  preserved verbatim.
- `ParamPool::pushToFaust` — consume the cached zone pointers (see A3) and call
  `mapSlotToZone` before writing.
- `PluginEditor.cpp:295-301` `refreshParamKnobs` — replace the inline linear normalization
  with `mapZoneToSlot`. **Same function, both directions** — that is the point.
- `PluginEditor.cpp:277` — seed **all** `POOL_SIZE` slots, not `MAX_KNOBS` (finding ⑥).
  Knob *visibility* stays capped at 8; *value seeding* must not be.

**Rules** (from the brief, unchanged): linear base `min + n*(max−min)`; parse
`[scale:log]` / `[scale:exp]` from the label; default `[unit:Hz]` sliders with `min > 0` to
log; dB stays linear; quantize integer-step and `[style:menu]`; `≥0.5` threshold for
`Button`/`CheckButton`. **Host-facing slots stay 0–1** — no `NormalisableRange` skew on slots;
the macro-slot pattern is ADR-settled.

**Size:** ~250 lines + tests. **Depends on:** nothing.

**Tests** — extend the existing console-app target `ParamPoolTsanTest`
(`host/CMakeLists.txt:75-93`) or add a sibling `ParamMapTest` target the same way:
- round-trip `mapZoneToSlot(mapSlotToZone(n)) ≈ n` across all curves, ranges, and both
  endpoints;
- log curve rejects `min <= 0` and falls back to linear without asserting;
- integer-step quantization lands exactly on steps;
- button threshold at 0.499/0.501;
- a `[unit:dB]` param with `min = -60` stays linear.

**Acceptance criteria (observable):**
- A patch declaring `hslider("Cutoff [unit:Hz]", 1000, 20, 20000, 1)` with its slot at knob
  midpoint drives the zone to **632 Hz ± 5 %** (log midpoint of 20…20000), not 0.5.
- On a fresh compile of that patch, the knob rests at the position corresponding to **1000 Hz**
  and the zone reads 1000 Hz ± 0.5 %.
- A 12-param patch leaves **no slot at its zone minimum** unless the patch's own default is the
  minimum.
- Sweeping the knob full-range produces a monotonic, audible cutoff sweep with no zipper noise.

### A2. Output safety net — non-negotiable

This runs unvetted machine-generated DSP into speakers and ears. **Scope reduced to three
items** — FTZ/DAZ is already covered (correction ②).

**Touchpoints**
- **New** `host/Source/OutputGuard.h/.cpp` — DC blocker (one-pole highpass ~5 Hz), hard
  limiter (ceiling ≈ −0.3 dBFS), NaN/Inf detector, and a sustained-0-dBFS watchdog that mutes
  and latches a flag.
- `PluginProcessor.cpp:67` — insert immediately after `faustEngine.process(buffer)`, **inside**
  the `enterAudio()/exitAudio()` bracket and before the `outputLevel` store.
- `PluginProcessor.h` — `std::atomic<bool> outputMuted` published for the editor.
- `PluginEditor` — surface the latched state ("DSP muted — unstable output"), cleared on next
  successful compile.

**⚠️ Hook interaction (finding ⑧):** `check_rt_safety.py` scopes only `FaustEngine::process`
and `*::processBlock` bodies. `OutputGuard` methods called from `processBlock` are **invisible
to it**. Add a task to widen `WATCHED_FILE_RE`/anchors to cover `OutputGuard.*`, or the guard
becomes an unpoliced RT hole. This is a hook change — **human-owned, not agent work.**

**Size:** ~180 lines + tests. **Depends on:** nothing. Can land before A.

**Tests:** feed the guard a buffer of NaN → output is finite and muted flag set; +6 dBFS square
→ output ≤ −0.3 dBFS with no discontinuity; DC offset of 0.5 → decays below 0.01 within 200 ms;
a legitimate hot-but-valid signal is **not** muted (false-positive guard).

**Acceptance criteria (observable):** generating a patch that self-oscillates to full scale
produces an audible limit and a muted-state label — **never** a speaker-damaging transient, and
never a crash. Silence in still yields silence out.

### B. Lifetime and shutdown

**Touchpoints**
- `FaustEngine.h/.cpp`:
  - Replace the per-compile `std::thread(...).detach()` (`:93,:177`) with **one persistent
    worker thread + job queue** (`std::thread` + `std::mutex` + `std::condition_variable` +
    a single-slot pending-job that newer requests overwrite → free cancellation of superseded
    compiles).
  - `~FaustEngine`: signal stop, **join**, *then* delete DSP/factory (`:52-57`).
  - Keep the swap protocol at `:137-176` **byte-identical**; it is verified correct
    (correction ①). Only the thread that runs it changes.
- `PluginEditor.h/.cpp`:
  - Hold the subprocess thread as a member `std::thread` plus a
    `std::shared_ptr<std::atomic<bool>> cancelToken`.
  - Retain the child **PID**; on editor destruction set the token, `kill()` the child, join.
  - Gate `proc.loadFaustCode(faustCode)` (`:190`) behind the token.
  - Deliver results on the message thread under the same guard, replacing the raw `&proc`
    capture (`:91`).

**Size:** ~200 lines. **Depends on:** nothing. **Enables:** D's restore-time recompile.

**Tests:** TSan run of the existing concurrency harness must stay at zero races. Add a unit
test that destroys a `FaustEngine` with a compile in flight and asserts clean teardown
(ASan/TSan, no leaks).

**Manual DAW repro script (human task — needs a DAW; none is installed today):**
1. Open project, type a prompt, hit Generate, and **close the project within 2 s** — before
   the subprocess returns. Expect: no crash, no orphan `python3` in `ps`.
2. Same, but close during **JIT compile** (after "JIT compiling", before "Ready").
3. Remove the plugin from the track mid-generation.
4. Rapid-fire Generate 5× without waiting. Expect: superseded compiles cancelled, last one
   wins, no crash.
5. Close the **host** entirely mid-generation.
Run all five in **Reaper** and **Bitwig** (different teardown orders). Record pass/fail per
step in `docs/collaboration_log.md`.

**Acceptance criteria (observable):** after any of the five steps, `ps aux | grep generate.py`
is empty and no crash log is produced. Rapid-fire produces exactly one live DSP matching the
last prompt.

---

## P1 — Prompt validity, persistence, measurement substrate

### C. Product-vs-bench prompt experiment (promoted — see correction ③)

**This is measurement, not policy.** It settles C's magnitude and doubles as the A/B for
stdlib grounding (⑤).

**Design:** one run, same day, same model, `temperature=0`, on **Groq** (quota rule 3).
Existing tiered corpus (`bench/prompts/tiered_prompts.json`, 25 effects × 5 tiers) × two arms:
- **Arm P** — `llm/prompts/system_prompt.txt`, **read-only** (the product's real behavior).
- **Arm B** — `bench/prompts/system_faust.txt` (the stdlib-grounded, agent-editable arm).

**Touchpoints:** `bench/run_efficacy_study.py` gains a `--system-prompt {product,bench}` flag
and records the arm in every record. The confound-control docstring at `:11-13` must be
rewritten to say the arm is now an explicit variable rather than a fixed control.
`docs/prompt_efficacy_study.md` §4 gains a **prompt-arm boundary** note alongside the existing
provider-era boundary.

**Hook replacement:** the current proxy check must not survive as the only guard. Two options —
**recommend the second**: (a) full-file equality on a unified pair, which the differing few-shot
examples make wrong; (b) a **loud CI diff-report job** that prints the full diff and fails only
when the *bench* file loses a rule the *product* file has. Hook/CI change = human-owned.

**Size:** ~120 lines harness + a doc pass. **Depends on:** nothing (measurement only).
**Feeds:** ⑤.

**Acceptance criteria (observable):** a single results file reports first-try compile rate for
both arms across all 5 tiers, with the undefined-symbol error class broken out. The question
*"how much of the 0.88 baseline is the prompt the product doesn't ship?"* is answered with a
number and a CI.

### D. State persistence

**Touchpoints:** `PluginProcessor.h:30-31` → real implementations in `PluginProcessor.cpp`.
Persist: Faust source, originating prompt, all 64 slot values, **schema version**, and
`faust`/libfaust version. Restore = enqueue a recompile on **B's worker thread**;
silent-bypass until ready. **Never block `setStateInformation`, never block the audio thread.**

Version-skew path: stored source fails to compile under a newer libfaust → surface the error in
the UI, keep the source **exportable** so work is never silently lost.

**Size:** ~200 lines. **Depends on: B** (needs the worker thread and cancellation).

**Tests:** round-trip a state blob through `get`/`setStateInformation` and assert every slot
value and the source survive; a deliberately corrupt/older-schema blob loads without crashing
and reports cleanly.

**Acceptance criteria (observable):** save a project with a live patch, reopen it, and the same
DSP is audible with knobs at their saved positions, with no audible artifact during the
restore-time compile. A blob whose source no longer compiles shows an error and still offers
the source for export.

### ②. Generation ledger

Prerequisite for every later routing, rating, or oracle decision — build it before anything
that would want its data.

**Touchpoints:** `llm/generate.py` — one append-only writer, called from both retry loops
(finding ⑦; unify them first). Local JSONL at `artifacts/ledger/generations.jsonl`.
Record per attempt: timestamp, prompt, provider, model, attempt index, outcome, compiler
stderr **class** (reuse `bench/score_efficacy.py`'s `ERROR_CLASS_RULES` rather than inventing
a taxonomy), latency, token usage when available.

**Size:** ~80 lines. **Depends on:** nothing. **Feeds:** ③, ①, and the oracle calibration set.

**⚠️ Privacy:** prompts are user text and go to free-tier providers *and* to disk. Listed as a
human decision below. Ledger must be gitignored by default.

**Acceptance criteria (observable):** after any 10 generations, the JSONL has 10+ rows whose
attempt counts and error classes match what the UI showed, and `docs/` can answer "what
fraction of failures are undefined-symbol" with one command.

---

## P2 — Retry quality

Depends on ② (ledger) for evidence and benefits from C's result.

### ③ + ④. Escalating cascade with deterministic rewrite

- **Attempts 1–2:** cheap and abundant (Groq).
- **Attempt 2** additionally triggers ④'s rewrite when applicable.
- **Attempt 3:** escalate to a **different model family** (Gemini — note per-model daily quota
  means several Gemini model IDs multiply the budget; enumerate them in `providers.py`).
- **Attempt-3 message = fresh single turn**: original prompt + **last failed code** + compiler
  stderr as evidence. Per finding ⑩ the single-turn shape already exists at
  `generate.py:66-69`; adding the failed code is the change. **Never replay chat history across
  model families.**

**④ has no upfront classifier and costs zero quota.** A deterministic anchor check — known
effect noun / parameter noun / unit token / a small gear-reference gazetteer — runs locally. On
**first failure of an anchor-less prompt**, rewrite then retry. No added latency on success.

**Touchpoints:** `llm/generate.py` (unified retry loop), `llm/providers.py` (model-family
escalation list). **Size:** ~200 lines. **Depends on:** ② (to measure whether it helps).

**Tests:** anchor detector unit tests over the L0–L4 corpus — L4 prompts must be anchor-rich,
L1 metaphor prompts anchor-less; cascade tests with a mocked provider assert attempt 3 targets
a different family and carries the failed code.

**Acceptance criteria (observable):** on the L1 tier, first-try-plus-cascade success exceeds
the pre-change ledger baseline; a prompt that succeeds on attempt 1 makes **exactly one** API
call and shows no added latency.

### ⑤. Reactive stdlib retrieval

**Trigger:** stderr of the **undefined-symbol** class only. Extract the offending symbol, find
nearest real signatures in `stdfaust.lib`, inject them into the **retry context**.

> **⚠️ PREMISE INVALIDATED, 2026-07-21 (same day).** This workstream existed because
> constraint 2 forbade editing the prompt, so stdlib knowledge had to arrive at *retry*
> time. Two things killed that premise within hours: the constraint was retired (see
> constraint 2 above), and the human then put a **generated** stdlib reference directly
> into `llm/prompts/system_prompt.txt` via `tools/gen_stdlib_block.py`, which is the
> simpler solution — grounding every first attempt instead of repairing failed ones.
>
> ⑤ is therefore **descoped to the residual case**: symbols the generated block omits,
> found only after a real failure. That residual may be empty. **Do not build this until
> the ledger (②) shows undefined-symbol failures still occurring at a rate worth
> spending code on.** Independently, `ParamCapture`'s zone-pointer caching (landed in
> `efbb5a5`) removed the MapUI lookup path this workstream also cited.

**Touchpoints:** new `llm/stdlib_index.py` (parse the installed `.lib` files once, cache to
JSON); hook into the retry composer. **Size:** ~150 lines. **Depends on:** C (which quantifies
the win) and ② (which proves it).

**Acceptance criteria (observable):** a prompt that fails with `undefined symbol : smoothclip`
retries with real `stdfaust.lib` signatures in context and succeeds at a measurably higher rate
than the same prompt without injection, per the ledger.

---

## P3 — Semantic oracle v1

**Depends on A** (correctness) **and ②** (the calibration corpus). Building it before A would
be actively misleading — see the last property below.

**Properties:**
1. Finite output — no NaN, no denormals.
2. Output ≠ input under a deterministic chirp + noise at **multiple input levels**.
3. **Per-parameter sensitivity**: min/mid/max renders at fixed seed and phase, compared
   **pairwise (param-vs-param, never output-vs-input)** — this is what kills LFO and
   dry/wet-at-zero false positives.
4. **Unit-aware quantitative checks**: dB → RMS delta of correct sign and rough magnitude;
   Hz → spectral centroid moves monotonically; delay-ms → cross-correlation lag moves.
5. Silence in → silence out.

**Critical design constraint:** the oracle **must drive the full plugin parameter path
(`ParamPool` → `MapUI`)**, not Faust zones directly. Driving zones directly would have passed
green while Finding A was live — that is the entire justification for the harness shape.

**Touchpoints:** new console-app target `OracleHarness` in `host/CMakeLists.txt`, built the
same way as `ParamPoolTsanTest` (`:75-93`), linking `ParamPool.cpp` + `FaustEngine.cpp` +
`ParamMap.h`. Offline render loop with a fixed-seed signal; Python scorer reusing the spectral
approach already proven on 2026-07-21 (band-energy deltas).

**Size:** ~400 lines (largest single item). **Depends on:** A, ②.

**Acceptance criteria (observable):** the oracle **fails** a deliberately broken build with
denormalization reverted, and **passes** the same patches once A is restored. A known-good
low-pass reports centroid moving monotonically with cutoff; a dry/wet-at-0 patch is **not**
flagged as dead.

**Human task — one-hour listening calibration.** ~10 real patches drawn from the ledger, rated
by ear, compared against oracle verdicts. Purpose: measure false-positive and false-negative
rates before anyone trusts a green check. **This cannot be delegated** — it is the ground truth
the oracle is calibrated against.

---

## Human decisions (not tasks)

1. **libfaust/JUCE license posture.** libfaust is GPL-family and JUCE 7 is dual-licensed;
   both must be settled *before any distribution*, and the answer may constrain architecture.
   Not urgent for a prototype; blocking for a release.
2. **Privacy disclosure.** User prompts go to free-tier providers *and* are written to the
   ledger on disk. Decide what is disclosed and what is retained before anyone else runs this.
3. **ADR-011 Python-subprocess distribution debt.** An installed bundle has no repo above it
   and no guaranteed interpreter. Decide whether the exit ADR is written **now** or deferred —
   deferring is defensible for a prototype, but the debt should be named.
4. **Hook changes** (widen RT-safety scope per ⑧; replace the prompt-sync proxy per C). Hooks
   are enforcement policy — human-owned.
5. **A DAW must be installed** for B's manual repro script. None is present
   (verified 2026-07-21); Carla is the lightest sufficient host.

---

## Risk register — top 5

1. **The prompt A/B (C) invalidates existing baselines.** If Arm P scores far below 0.88, the
   ADR-009 verdict and the efficacy pilot describe a prompt the product never shipped, and
   conclusions drawn from them need revisiting.
2. **Denormalization changes every patch's sound.** A is a correctness fix that will make
   existing saved states and rendered artifacts sound different. Landing it after D would
   silently break saved projects — **land A before D**, as ordered here.
3. **The oracle is the largest item and the easiest to over-build.** Property 3 (pairwise
   sensitivity) is where the subtlety lives; v1 should ship narrow rather than complete.
4. **Free-tier quota throttles measurement.** C plus the ⑤ A/B are hundreds of calls. Groq's
   8000 TPM cap (~1.7 req/min at 4096 max_tokens) makes a full corpus run take hours, and
   Gemini's 20/day cannot absorb overflow.
5. **B is the highest-risk change to working code.** The swap protocol at `FaustEngine.cpp`
   `:137-176` is correct and hard-won; the worker-thread refactor must preserve it byte-for-byte
   or reintroduce races that took two prior sessions to close.

---

## Verification (end to end, after P0)

```bash
# 1 — C++ unit + concurrency
cd host && cmake --build build -- -j$(nproc)
./build/ParamMapTest_artefacts/ParamMapTest            # new (A)
./build/ParamPoolTsanTest_artefacts/ParamPoolTsanTest  # existing, must stay zero-race

# 2 — Python
python -m pytest tests/ -m "not integration" -q        # 234 baseline, must not drop

# 3 — generation path (Groq; do not burn Gemini)
python llm/providers.py --check all
python llm/generate.py --prompt "a warm low-pass filter with a cutoff knob"

# 4 — offline audible check (quota-free, reuses saved .dsp)
faust2sndfile patch.dsp && ./patch artifacts/audio/input_testsignal.wav wet.wav
```

Then the plugin itself: launch Standalone, generate a `[unit:Hz]` cutoff patch, confirm the
midpoint-≈632 Hz criterion by ear and by spectral check, and run B's five-step teardown script
in a DAW.
