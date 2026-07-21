# Collaboration Log

Entries appended per COLLABORATION.md §6 after non-trivial sessions.

---

### 2026-07-21 — Free-provider layer (billing block removed)

**Mode:** DELEGATE for all Python (new module, three call sites, 82 tests, READMEs).
One deliberate stop into HUMAN-OWNED: ADR-012 and the `prompt_efficacy_study.md` §4
amendment were drafted to `docs/ADR-012-free-provider-layer-DRAFT.md`, not applied.

**Task:** The attention report identified the Anthropic credit exhaustion as the single
blocker on the prototype's last 15%. User asked for free model alternatives under a hard
free-only rule, with a step-by-step human plan where intervention was needed and
implementation+testing otherwise.

**What landed:** `llm/providers.py` — one registry (5 providers, 3 adapters; groq,
openrouter and ollama all speak OpenAI-compatible so they share one httpx path), wired
into `generate.py`, `run_benchmark.py`, `run_efficacy_study.py` and `score_efficacy.py`'s
judge. Zero new dependencies; zero C++ changes (`.env` reaches the plugin through
`load_dotenv()` + `ChildProcess` inheritance). Free-only is enforced in code via
`assert_free()`, deliberately placed in each `__main__` rather than in `make_generator()`
— every money-spending path is a CLI invocation, so that is complete coverage while
leaving library functions drivable by mocked tests. **Generation works end to end again**
on `gemini-3.6-flash`. Baseline file went to schema v2 (per-provider keys) with Claude's
0.88 frozen verbatim. 231 tests pass, of which the 145 pre-existing ones were unmodified.

**Would do differently:** I planned the human runbook around acquiring a Gemini key, then
discovered during execution that a working `GOOGLE_API_KEY` had been sitting in `.env` the
whole time — my earlier audit had printed only the key *names* (`grep -o "^[A-Z_]*="`, to
avoid echoing secrets) and I read an unknown value as an empty one. Redacting a value is
not the same as knowing it is absent; the doctor CLI I wrote later answers that question
in one command, and should have existed before the plan did.

**Mode signal:** DELEGATE held. The three most valuable findings all came from running
the thing rather than reasoning about it — the 2.5-model family 404ing for new accounts,
the 5-requests-per-minute free cap, and reasoning tokens silently eating the 1024-token
output budget (981 thinking / 39 visible, truncated). None were visible from the code or
from my training data, which is the argument for the plan's "no hardcoded model ids"
rule. The suite hanging on a live API call after I wrote `PLUGINFORGE_PROVIDER` into
`.env` was the sharpest lesson: config that reaches the product also reaches the tests.

---

### 2026-07-21 — State review; version-control baseline; widget-kind; doc reconciliation

**Mode:** DELEGATE throughout, with two deliberate stops into HUMAN-OWNED (ADR-009
verdict drafted to a separate file, not applied) and PAIR (bench-harness model bump
halted before the edit).

**Task:** User asked for a full review of the codebase state, a series of prompts for
the path forward, and execution of whatever Claude could do.

**What landed:** Verified CLAUDE.md's implementation claims against the code — all
accurate, file:line confirmed. But the review's headline finding was absent from every
doc: **PluginForge had no version control at all**, 956M untracked inside the unrelated
CS310 course repo rooted at `/home/losera`. The 2026-07-19 attention report flagged "no
git repo" and it was recorded as fixed; it was not. Initialized with a baseline commit
after hardening `.gitignore` for ~400M of in-source CMake residue (`host/CMakeFiles`,
`host/JUCE`, `*_artefacts`) that the existing `build/` rule did not cover — dry-run
verified `.env` excluded and the tree at 1.1M before committing. No remote: that's the
human's call. Also landed P12a (widget-kind on `ParamInfo`; four targets build clean,
TSan still zero races), the opus-4-6 → opus-4-8 pin on `generate.py` (145 tests pass),
and the doc reconciliation (`ui_design_plan.md` §4 had described P10 as "deferred" for
three weeks after it shipped). README gained P13–P17.

**Would do differently:** I planned P15 as "a pure string change" on the strength of
`generate.py` alone, and only caught that the four bench call sites pass `temperature=0`
— which opus-4-7+ reject with a 400 — after loading the migration reference *during*
execution. Had I not, a "mechanical" model bump would have either broken the harnesses or
silently stripped a locked confound control. The lesson is narrow and worth keeping:
when a change is characterized as mechanical, grep the whole repo for the symbol before
believing it, not just the file that motivated the change.

**Mode signal:** DELEGATE held for the code and doc work. The two stops both proved
right on inspection rather than on principle — ADR-009 turned out to need a *factual*
correction beyond the verdict (it cites `llm/prompts/system_faust.txt`, which does not
exist), and the bench bump turned out to be an experiment-design change rather than an
edit. Neither was visible from the plan; both surfaced only once the files were open,
which is an argument for the stop conditions being checked continuously rather than at
classification time.

---

### 2026-07-20 — P10 ecosystem survey + tiered-prompt L0 phrasing fix

**Mode:** DELEGATE (research/documentation + a 3-line data-file wording fix). No
Anthropic API calls in this task — unaffected by the still-unresolved billing block.

**Task:** User asked to check progress, review the prior session's docs, and
execute the next queued item. Billing was confirmed still unresolved (so P9/judge
stayed deferred), and the user scoped this session to two items: fixing 3 tiered
prompts (`filters-01`/`filters-02`/`dynamics-02`, L0 tier) that a prior audit found
leaking mechanical vocabulary ("filter sweep", "low-cut", "maximizer") against the
L0 tier's own no-mechanical-description rule, and running P10 (the JUCE/Faust
ecosystem survey spec'd in `docs/ui_design_plan.md` §4).

**What landed:** the 3-prompt fix (verified: `tests/test_efficacy_unit.py` still
36/36). P10 ran as 3 parallel research agents (general JUCE, Faust ecosystem,
complexity-ladder anchors); one agent stalled on first attempt (a slow WebFetch)
and was relaunched with a lighter, fewer-calls-per-repo method that finished
cleanly. Merged 21 unique entries (1 exact duplicate repo found and folded) into
`docs/juce_plugin_survey.md`. Headline finding: **zero of 19 fixed-param entries
used bare `GenericAudioProcessorEditor`**, even at 1-2 params — contradicts the
naive assumption that trivial plugins default to it, and supports keeping
PluginForge's planned auto-layout as the floor rather than a GenericEditor
fallback. Also surfaced a 4th UI paradigm the taxonomy didn't name (declarative/
GUI-Magic, used by 2 Faust-adjacent entries) and found Effect-type plugins invest
in custom UI more than §2 predicted — flagged as likely survivorship bias, not
treated as settled fact.

**Would do differently:** should have given the anchor-repo agent (Group C) a
lighter, fewer-calls-per-repo task from the start — it stalled on the first,
more-thorough version and the retry only succeeded after cutting verification
depth. Worth defaulting research agents to a "return partial results and move on"
instruction up front rather than discovering the need to retry.

**Mode signal:** the human selected exactly these two items from a 4-option menu
(declining P9/judge as still-blocked and P11 as needing their presence) —
DELEGATE scoping via explicit menu choice worked cleanly, no ambiguity to resolve
mid-task.

---

### 2026-07-19/20 — Prompt-efficacy study + UI/UX planning + outstanding-item cleanup (multi-track session)

**Mode:** DELEGATE throughout (new bench scaffolding, new docs, mechanical fixes, a
benchmark re-run). PAIR/HUMAN-OWNED items (P8/ADR-008, P11 state persistence,
audio_thread_example.md checklist, PluginEditor SafePointer read-through) were
surfaced in docs/next_steps.md's refreshed priority queue and root README's P9–P12,
not executed.

**Task:** User asked for (1) a prompt-efficacy study spanning DSP-expert to
naive-musician phrasing, with agents gathering data and mapping what helps/hurts
generation; (2) outstanding items surfaced first; (3) a delegated UI/design-plan
track incl. a GitHub JUCE/Faust ecosystem survey spec; (4) a future-UX roadmap for
iterating on generated plugins up to an embedded code editor; (5) all of it queued
and structured in READMEs so any future session can pick up any track cold, with
efficient subagent use and explicit token-budget awareness.

**What landed:**
- Ran P5 (full 25-prompt Faust re-run): 22/25 (88%), up from the committed 84% but
  short of ADR-009's ≥96% prediction — closed with a documented negative result
  rather than silently re-flagged as open. Baseline file updated.
- Built the tiered-prompt dataset (bench/prompts/tiered_prompts.json, 125 prompts,
  25 effects × 5 knowledge tiers) by hand on the main thread — judgment-heavy
  authoring, not delegated.
- One coding subagent built bench/run_efficacy_study.py + bench/score_efficacy.py +
  36 new mocked tests (145/145 non-integration tests green); one doc-writer subagent
  wrote docs/ui_design_plan.md + docs/ux_roadmap.md; one fixer subagent closed
  pair_draft_editor_llm_bridge.md's point F/C (API-key precheck, exception-safe
  stdout JSON) with 10 new tests. All three ran in parallel, none touched a
  HUMAN-OWNED file.
- Ran the pilot efficacy study (filters + generative × 5 tiers) in the background.
- Refreshed docs/next_steps.md into a live cross-track priority queue; closed out
  B5/B6/B8's stale "still open" annotations; updated CLAUDE.md status lines
  (including a stale "99 unit tests" count a subagent caught — actual pre-existing
  baseline was 109, now 145 with today's additions) and README's P-series (P9–P12).

**Would do differently:** authored the 125-prompt tier dataset serially on the main
thread before the harness subagent had a schema to test against — the subagent
correctly built against the documented schema and mocked fixtures instead of
blocking, but doing the dataset and the harness in the same wave (rather than
dataset-first) would have let the harness agent validate against real data sooner.

**Mode signal:** all three subagents stayed exactly in their scoped file lists
(verified by mtime/grep audit afterward, not just trusted from their self-reports);
one caught and fixed a real bug (classify_error's keyword-vs-length check ordering)
and one caught a stale test-count claim in CLAUDE.md unprompted — DELEGATE was the
right call for all three.

**Pilot run + follow-up (same session, after the above landed):** ran the
filters+generative pilot (50 first-attempt generations) and scored it. Headline:
first-try compile rate is non-monotonic across tiers — 90% at L4/L3, dropping to 50%
at L1 (vibe/metaphor prompts), partially recovering to 60% at L0 (artist/song
reference) — L1 was harder than L0 in this pilot, the opposite of the naive
"vaguer is always worse" assumption. Scoring the first pass left 3 of L1's errors
UNCLASSIFIED; reading the raw Faust stderr showed they were an ADR-009
duplicate-`process` regression and two arity/parameter-range errors — extended
`ERROR_CLASS_RULES` in bench/score_efficacy.py with that vocabulary and confirmed
zero UNCLASSIFIED remained on rescoring (36/36 efficacy tests still green). Full
writeup: docs/prompt_efficacy_study.md §7.1.

**Would do differently (addendum):** should have grepped the pilot's raw error
strings against the taxonomy before calling the harness "done" — the gap was found
only because I read the UNCLASSIFIED bucket by hand rather than trusting a clean
test run to mean the classifier was complete; worth building a "taxonomy coverage"
assertion into the harness itself before the full 125-prompt run.

**P9 attempt (user-requested, next turn):** ran the full 125-prompt study on
request. Result: 0/125 across every tier and category, every failure INCOMPLETE,
`mean_attempts` flat at 1.00 everywhere — a uniform, zero-variance pattern that is
never a real model result. Read the raw error field before writing anything up:
every one of the 125 requests was rejected pre-generation with
`BadRequestError: Your credit balance is too low to access the Anthropic API`. Did
NOT write these numbers into docs/prompt_efficacy_study.md §7.2 as findings —
recorded the run as invalid, renamed the output/chart files with an
`_INVALID_insufficient_credits` suffix instead of deleting them, and added the
billing block as the top item in docs/next_steps.md's human-attention list. No
budget was actually spent (requests rejected before tokens were used).

**Mode signal:** this is the check that matters most in an autonomous session —
a clean-looking report (score_efficacy.py ran without error, produced tables) does
not mean the data is real. Verifying against the raw JSON before writing up results
caught it; a summary-only glance at the pretty-printed table would not have.

---

### 2026-07-19 — /attention-report audit + report file; broader "optimize everything" request declined per protocol

**Mode:** DELEGATE (read-only audit + one report doc + two-line log/doc bookkeeping). The
rest of the request (agent-driven subroutine optimization, ADR "optimization", prompt
rewrite/test loops) was **not executed** — classified HUMAN-OWNED / stop-condition and
reported back instead.

**Task:** Ran the attention-report procedure; wrote `docs/attention_report_2026-07-19.md`.
Headline findings: (1) PluginForge has no git repo of its own — it is an untracked directory
inside the unrelated /home/losera repo, so CI can never fire; (2) committed benchmark data
shows Claude/Faust at 84%, contradicting ADR-009's ≥96% expectation, with the Gemini rows
being 25×429-quota errors (ADR-008 evaluation effectively never ran); (3) next_steps.md B5/B6
annotations are stale (both actually done). **Verified this run:** 99/99 unit tests,
zero live TODO:VERIFY markers, ADR-011 present in decisions.md.

**Would do differently:** Nothing on the audit. The user's original prompt bundled five
different asks with unclear scope ("optimize subroutines", "across DAWs", "5 rounds of
prompt tests"); a shorter clarifying exchange up front would have been cheaper than
classifying each fragment against §1/§4 after the fact.

**Mode signal:** The protocol did its job — three of the five asks land squarely in
HUMAN-OWNED territory (prompts are product IP, ADRs are human-authored, audio-thread
optimization is a §4 stop condition), so refusing to auto-execute them is the designed
outcome, not an obstruction.

### 2026-07-19 — Prototype test plan + audio-reactive meter + artifact pipeline

**Mode:** DELEGATE (docs, tooling, artwork), with one stated-deviation RT touch: publishing
the output peak from processBlock (single relaxed atomic store — same direct-threading
convention as the earlier swap fix).

**Task:** Wrote docs/prototype_test_plan.md — Part A is the scripted human test (9 steps,
prompts to type, expected status transitions, pass criteria), Part B the automated artifact
pipeline. Made the plugin audio-reactive: PluginForgeProcessor::outputLevel (atomic,
post-DSP peak, fed on both the live and passthrough paths) + a 30Hz decaying gradient meter
in the editor — the visible "audio path alive / DSP live" signal for testing. Built the
pipeline: tools/make_test_signal.py (6s sweep+bursts reference), faust2sndfile renders of
all 4 examples/*.dsp, tools/make_artwork.py (waveform+spectrogram cards, single-hue teal on
the plugin's dark surface, fixed −90..0dB range for honest comparison). All artifacts
generated into artifacts/ (now gitignored). **Verified:** rebuild clean, TSan PASS with 0
races / 0 setParamValue errors (meter included), cards visually confirmed (lowpass shaves
the sweep, compressor squashes bursts).

**Would do differently:** Automated UI screenshots on the live desktop were a mistake —
three grim captures caught the human's own windows (terminal, browser); all were deleted
immediately and the approach abandoned. Replacement: tools/screenshot_ui.sh for the human
to run with the window on top. Lesson recorded: never screen-capture on a desktop the human
is actively using; window pixels are not ours.

**Mode signal:** DELEGATE call correct for docs/tooling; the meter's single-store RT touch
was small enough that PAIR would have been ceremony, and the RT-safety hook screened it.


**Mode:** DELEGATE (record reconciliation + UI/subprocess wiring; the ADR itself was ratified
by the human — ported into docs/decisions.md by them, per HUMAN-OWNED rules).

**Task:** With ADR-011 ratified (human ported it to docs/decisions.md), reconciled every
record (ADR file → Status: Accepted pointing at decisions.md as authoritative copy; index
table; decisions_reconstructed [011] → Decided; CLAUDE.md; root README P1 marked done) and
executed the two items still Open in the ADR's hardening table: (1) interpreter discovery —
`PLUGINFORGE_PYTHON` env override, default `python3`, failure message now names the actual
interpreter; (2) ready-state UX (pair_draft point E) — new
`PluginForgeProcessor::onFaustCompileSuccess(int numParams)` fires from the compile thread
after remap()/before ready=true, editor hops via the established SafePointer+callAsync
pattern; post-subprocess label now says "JIT compiling: …" and flips to "Ready — DSP live,
N params mapped" when the swap lands. **Verified:** all targets rebuild clean; TSan harness
PASS with 0 races and 0 setParamValue errors; pytest 99/99.

**Would do differently:** Nothing. One observation for later: onFaustCompileSuccess runs
inside compileMutex (like remap), so anything heavy hung off it would extend the lock — fine
for a callAsync post, worth remembering if it ever grows.

**Mode signal:** Correct — the decision stayed human-owned end to end (draft 07-19 morning,
human ratification, Claude only executed the ratified plan); the wiring itself was
squarely DELEGATE.

---

### 2026-07-19 — Fix pushToFaust label mismatch + activeUI TOCTOU; ADR-011 draft; path/timeout hardening

**Mode:** Stated deviation — threading fix executed directly on explicit human instruction
("Execute it"), matching the 2026-07-17 precedent and the standing feedback that correct
atomic/threading code be written directly with SUBTLE comments. ADR-011 remained draft-only
(HUMAN-OWNED, Status: Proposed). Everything else DELEGATE.

**Task:** Root-caused the ~1,100 `setParamValue not found` errors from the first TSan run: two
bugs in `FaustEngine::compile()`'s swap — (1) the callback that runs `ParamPool::remap()` fired
after `ready=true` *and* after multi-ms LLVM factory teardown, so old labels were pushed into
the new DSP for several blocks; (2) no drain of in-flight audio-thread calls before mutating
`activeUI` (the TOCTOU reported 2026-07-18). Fix: `audioBusy` drain guard
(enterAudio()/exitAudio() bracketing processBlock, seq_cst store→load handshake) + callback
reordered before `ready=true`; design doc at docs/fixplan_pushtofaust_swap.md. Also: ADR-011
draft (argv IPC, docs/architectural_decisions/) with index/status updates; PluginEditor
hardening (bounded upward path search replacing the never-correct getSiblingFile guess — both
TODO VERIFY markers resolved — plus a 120s subprocess timeout+kill); TSan harness assumption
comment updated to verified fact. **Verified:** rebuild clean; TSan re-run PASS with
`setParamValue not found` count 0 (was ~1,100; log 1,115 → 12 lines); pytest still 99/99;
Standalone launched 8s clean and the path search replicated on disk finds llm/generate.py at
depth 5. ADR-009's ≥96% claim deliberately left open (human declined API spend; still P5).

**Would do differently:** Nothing on the fix itself. Worth noting the drain guard makes the
old belt-and-braces `isReady()` checks inside `process()`/`pushToFaust()` redundant (kept as
defense); if a future cleanup removes them, the enterAudio() bracket becomes the only gate and
its SUBTLE comment the only documentation of why.

**Mode signal:** The deviation felt right-sized: the human named the exact defect and ordered
execution; the seq_cst handshake reasoning is documented in three places (header, cpp,
fixplan doc) for the human read-through that PAIR would otherwise have forced up front.

---

### 2026-07-18 — Agentic orchestration: attention-report skill, README prompt series, first TSan run

**Mode:** DELEGATE (docs, skill authoring, and test execution — all in COLLABORATION.md's
DELEGATE column; no HUMAN-OWNED file touched).

**Task:** Built the session-orchestration layer: created `.claude/skills/attention-report/`
(severity-ranked "what needs the human" audit, invocable as `/attention-report`); added an
"Agentic architecture" map and a P0–P8 prompt series to the root `README.md`; created
`host/README.md` and `llm/README.md` with area orientation plus their subset of the series.
Ran verification: 99/99 unit tests pass; full CMake build of all three plugin targets clean;
**first-ever execution of `ParamPoolTsanTest` — PASS, zero ThreadSanitizer race reports**,
closing the 07-17 log entry's open recommendation. Also removed a stale agent-debris `rm -f`
permission line from `.claude/settings.local.json`.

**Would do differently:** Nothing procedurally, but the TSan run produced a finding beyond its
race-checking remit: ~1,100 libfaust `ERROR : setParamValue 'gain'/'cutoff' not found` lines
during compile/swap transitions. Thread-safety holds, but `ParamPool::pushToFaust()` is pushing
labels the live DSP rejects around swaps — parameter values may silently fail to reach the DSP.
Root-causing it likely touches HUMAN-OWNED swap logic, so: reported here, not fixed. The
harness's known no-MessageManager JUCE assertions/leak reports also fired as predicted by its
own header comment.

**Mode signal:** Correct call — pure DELEGATE surface (docs/tests/tooling); the one finding
that borders HUMAN-OWNED code was reported rather than fixed, per §4 stop conditions.


**Mode:** DELEGATE (running an existing, documented script with explicit
human go-ahead on the API spend).

**Task:** The committed `bench/results/results.json` (2026-05-10, post-ADR-009)
showed 84%, not the ≥96% ADR-009 expected from a re-run that never appears to
have landed. Given a choice between the cheap 9-prompt recovery subset and the
full 25-prompt suite, human chose the cheap option. Ran
`python bench/run_benchmark.py --provider claude --prompts bench/prompts/recovery_prompts.json`:
**8/9 (89%)** on the previously-failed-prompt subset — one FAIL (`a tape-style
flanger with feedback`, time-based category).

**Would do differently:** Nothing procedurally, but worth naming clearly: this
run **overwrote** `bench/results/results.json` (documented, known behavior of
`run_benchmark.py` — see `bench/check_prompt_regression.py`'s own docstring
warning about this) with just these 9 records, replacing the previously
committed 25-prompt data there. The original 84% figure (21/25) is preserved in
`.prompt_baseline.json`'s `note` field, so it isn't lost, but `results.json`
itself no longer reflects a full-suite run.

Per `bench/README.md`'s own stated convention,
`.prompt_baseline.json`'s `recorded_faust_compile_rate` is intentionally
**left at 0.84**, not updated to 0.89 — that field is meant to move only after
the full 25-prompt suite is re-run and reviewed, and this was explicitly the
cheaper, partial smoke check, not that. 89% on the 9-prompt subset is a real,
positive signal (better than the 84% floor) but does not by itself confirm or
refute ADR-009's ≥96% full-suite bar — the full run remains the actual
open item if that number is needed with confidence.

**Mode signal:** Correct call — did not run either benchmark option without
the human's explicit choice, matching the established project norm of not
spending API budget unasked.

---

### 2026-07-18 — Draft ADR-011 (IPC mechanism) for human ratification

**Mode:** HUMAN-OWNED — drafted proposed text only, per COLLABORATION.md §1
("any new ADR entry... you may draft proposed text for the human to review, but
the human is the author"). Did not write to `docs/decisions.md`
(`protect_human_owned.py` blocks it regardless).

**Task:** `docs/decisions_reconstructed.md` Decision [011] has tracked the
Python↔C++ IPC mechanism as "Open" since before Day 2, even though
`PluginEditor.cpp` shipped an argv-based implementation 2026-07-16 without this
ADR ever being written first (flagged in that day's own log entry). Drafted
ADR-011 in `docs/decisions.md`'s exact Status/Context/Decision/Reasons/
Consequences template, ratifying the existing argv + `juce::ChildProcess` +
stdout-JSON implementation as-is, and presented it in-conversation rather than
writing it to the file.

**Would do differently:** Nothing — this is squarely a "draft only" task and
was treated as one from the start; no code or decisions.md changes were made.

**Mode signal:** Correct call — an ADR entry is explicitly HUMAN-OWNED
regardless of how mechanical the drafting felt (the underlying decision was
already made in practice; this was closer to transcription than judgment, but
the protocol doesn't carve out an exception for that).

---

### 2026-07-18 — B6: surface Faust compile errors in the UI (DELEGATE)

**Mode:** DELEGATE — pattern-following an already-reviewed mechanism
(generateButton's SafePointer + `MessageManager::callAsync` dispatch), no new
threading pattern introduced.

**Task:** A Faust compile failure (as opposed to an LLM-generation failure,
already surfaced) previously only reached `juce::Logger`. Added
`PluginForgeProcessor::onFaustCompileError` (a `std::function` the editor
assigns), invoked from `loadFaustCode()`'s existing error branch alongside the
log line. The editor's assignment hops to the message thread via
`SafePointer`/`callAsync`, exactly mirroring the existing generateButton
callbacks, since the callback fires on FaustEngine's detached compile thread
(confirmed while researching the activeUI finding above — FaustEngine.h's own
comment claiming "calls cb on message thread" does not match `compile()`'s
actual implementation, which calls `cb()` directly on its own thread).

**Would do differently:** Nothing — this is a direct application of a pattern
already reviewed and landed for the LLM-error path; no new verification needed.

**Mode signal:** Rebuilt both `PluginForgeHost_VST3` and
`PluginForgeHost_Standalone` clean; `pytest -m "not integration"` still 99/10.
Correct DELEGATE call per COLLABORATION.md's own example list.

---

### 2026-07-18 — ParamPool ThreadSanitizer harness (PAIR) + a separate FaustEngine finding

**Mode:** PAIR for the harness itself (host/tests/ParamPoolConcurrencyTest.cpp +
a new ParamPoolTsanTest CMake console-app target, -fsanitize=thread). The
finding described below is reported only, per HUMAN-OWNED (FaustEngine.cpp's
audio-thread/atomic-swap code) -- not something this session touched.

**Task:** Close the "ParamPool race fix shipped 07-17 with zero automated
verification" gap flagged in this morning's status review. Built a harness that
drives the real `PluginForgeProcessor` object graph: one thread fires 20
back-to-back `loadFaustCode()` calls (alternating two tiny Faust programs), a
second thread hammers `processBlock()` (-> `ParamPool::pushToFaust()`)
concurrently. Result on `ParamPool`'s own double-buffer: **clean** -- zero
ThreadSanitizer reports across the run.

While investigating a large volume of `ERROR : setParamValue '<x>' not found`
stderr noise from the harness, isolated it (via throwaway scratch probes, no
JUCE/ParamPool involved) down to `FaustEngine::activeUI`, not `ParamPool`: the
`activeUI = std::move(newUI)` swap in `FaustEngine::compile()` is guarded only
by the `ready` atomic, but `pushToFaust()` only checks `isReady()` once before
looping over all 64 slots -- a real TOCTOU window where a concurrent
`setParamValue()` call can land on `activeUI` mid-move-assignment. A raw
libfaust+MapUI reproduction (no JUCE) confirmed the "not found" symptom appears
under concurrent access even outside this codebase entirely. Notably,
ThreadSanitizer itself never flagged this as a formal data race in either the
JUCE harness or the raw reproduction -- the symptom is a functional
correctness gap (a parameter push can silently miss during/around a live
recompile), not a TSan-detectable memory-safety violation, which may be why it
survived FaustEngine's original SUBTLE-comment review.

**Would do differently:** Should have designed the harness's two alternating
Faust programs to make a wrong-vs-missing-parameter distinguishable from the
start (my first raw reproduction attempt queried both possible keys every
iteration, which manufactured roughly half of its own "not found" results
as an artifact of the test rather than the underlying issue -- had to redo the
isolation once to separate the artifact from the real signal).

**Mode signal:** Correct calls on both counts -- drafted the harness myself
(PAIR, explicitly approved) and stopped short of touching FaustEngine.cpp once
the investigation pointed there, reporting instead of patching a HUMAN-OWNED
atomic-swap file unasked.

---

### 2026-07-18 — Fix ParamPool/APVTS parameter-creation ordering (PAIR)

**Mode:** PAIR, per COLLABORATION.md's own listed example for ParamPool parameter
remapping. Confirmed with the human via AskUserQuestion before drafting, since this
is a structural change to how ParamPool owns/creates parameters.

**Task:** Fix a live `jassert(!state.isValid())` failure (juce_AudioProcessorValueTreeState.cpp:311)
firing 64x on every build — the first time this project's build has ever run far
enough (see the CMakeLists.txt fix entry above) to surface it. Root cause:
`PluginProcessor`'s `apvts` member is constructed with the ParameterLayout-taking
constructor, which makes the internal ValueTree `state` valid immediately, but
`ParamPool`'s constructor then called the legacy `createAndAddParameter()` for
all 64 slots afterward — exactly the failure mode `ParamPool.cpp`'s own
`TODO: VERIFY` comment predicted. Moved all 64 `AudioParameterFloat` definitions
into `PluginProcessor::createParameterLayout()`; `ParamPool`'s constructor now
looks slots up via `apvts.getParameter(slotId(i))` instead of creating them.
Promoted `slotId()` to a shared `public static` method on `ParamPool` so both
files use one ID definition instead of duplicating the `"macro_N"` scheme.

**Would do differently:** Nothing — the fix was verified against the actual
`ParameterLayout` iterator-range constructor in
`juce_AudioProcessorValueTreeState.h` (not guessed), and confirmed by an actual
rebuild showing zero assertion output across `PluginForgeHost`,
`PluginForgeHost_Standalone`, and `PluginForgeHost_VST3` (previously: 38
assertion-failure lines during `juce_vst3_helper`'s parameter probe). `pytest -m
"not integration"` re-run afterward, still 99 passed / 10 deselected.

**Mode signal:** Correct call — this is precisely COLLABORATION.md's own worked
PAIR example for this file, and the fix touched a "new pattern" (who owns
parameter creation) that the human should understand before it ships. No
ThreadSanitizer/execution-level test exists yet for this file — that's a separate,
already-planned follow-up.

---

### 2026-07-18 — Unblock the C++ build (three JUCE compile-definition fixes)

**Mode:** DELEGATE (CMakeLists.txt edits — explicitly listed as DELEGATE in
COLLABORATION.md §1).

**Task:** CLAUDE.md described the build as blocked on "a missing system GTK dev
package (gtk3, webkit2gtk-4.1)." Checked first (`pacman -Qi`, `pkg-config
--exists`) and found both already installed system-wide — the actual root cause
was JUCE's `JUCE_WEB_BROWSER`, `JUCE_USE_CURL`, and `JUCE_VST3_CAN_REPLACE_VST2`
macros all defaulting to `1` regardless of the `NEEDS_WEB_BROWSER FALSE` /
`NEEDS_CURL FALSE` plugin options (those only gate *linking*, not the
`#include`/symbol-usage guards in `juce_gui_extra.cpp` / `juce_core`'s curl code
/ the VST3 client's legacy VST2-compat header). Added all three as explicit
`target_compile_definitions` in `host/CMakeLists.txt`. Result:
`PluginForgeHost`, `PluginForgeHost_Standalone`, and `PluginForgeHost_VST3` all
build and link for the first time in this project's history.

**Would do differently:** Nothing — checking `pacman -Qi`/`pkg-config --exists`
before running any install command caught that CLAUDE.md's stated blocker was
already stale (the packages were present), which avoided proposing an
unnecessary system change.

**Mode signal:** Straightforward DELEGATE call; no threading/audio-thread code
touched, purely build configuration, verified by an actual successful build each
step. Discovering all three fixes required reading JUCE's own CMake/module
source (`JUCEUtils.cmake`, `juce_gui_extra.h`, `juce_core.h`,
`juce_audio_plugin_client.h`) rather than guessing at flag names — each one was
confirmed by grep before being added.

---

### 2026-07-18 — Full status review + agentic development plan for today

**Mode:** DELEGATE (review, doc fixes, planning — no HUMAN-OWNED or PAIR code produced)

**Task:** Review all development notes, verify actual vs. claimed state of the C++
host and test suite, report on subagent/hook/loop architecture, and produce a
sequenced plan for today covering the open threads (ADR-011, B6, B8, the
ParamPool test-coverage gap, the GTK build blocker).

**Would do differently:** Nothing significant — verifying claims against a live
build attempt and a real `pytest --collect-only` run (rather than trusting
CLAUDE.md/START_HERE.md at face value) caught two concrete staleness bugs in one
pass: CLAUDE.md still described the 07-17 `activeLabels` fix as an open
HUMAN-OWNED race, and START_HERE.md claimed `docs/decisions.md` spans
ADR-001–009 when it only actually holds 001–008. Both are fixed in this session.

**Mode signal:** Matches the DELEGATE bar — every claim here was independently
tool-verified (live build, `pytest --collect-only`, direct file reads via three
parallel Explore subagents) rather than asserted from memory of the docs.

---

### 2026-07-17 — Fix activeLabels data race (ParamPool)

**Mode:** HUMAN-OWNED, explicitly overridden by the human after I flagged the
tension (COLLABORATION.md classifies atomic synchronization patterns as
HUMAN-OWNED; sanctioned help there is explanation/docs/critique, not authoring the
code). Asked via AskUserQuestion which path they wanted; they chose "Claude writes
it directly."

**Task:** Replace the single `activeLabels` vector (read on the audio thread by
`pushToFaust()`, written on the compile thread by `remap()` with no synchronization)
with a double-buffer + atomic-index pattern, mirroring `FaustEngine`'s existing
`activeDSP`/`ready` swap.

**Would do differently:** Nothing — before designing, re-read `FaustEngine.cpp`'s
`compile()` specifically to confirm `compileMutex` stays held through the `cb()`
call, which let the design drop a second lock on the write side entirely (single
writer, no writer-vs-writer race to handle). Confirming that assumption before
writing code, rather than after, avoided a wasted design iteration.

**Mode signal:** The human first asked for a beginner-level explanation of what
`activeLabels` does and why it's racy before any implementation was discussed — a
good instance of the "confirmed mental model before a fix" stop condition working
as intended. Once they had that model, they explicitly chose to override
HUMAN-OWNED rather than have this become a reference-doc-only PAIR deliverable.
`ParamPool.cpp.o` compiled clean against real headers on first try; `pytest`
unaffected (99 passed / 10 deselected, no C++ coverage exists for this path). Real
verification of the fix itself (e.g. ThreadSanitizer) was recommended but not run —
flagged rather than skipped silently.

---

### 2026-07-16 — Agentic-engineering architecture build-out (hooks, skill, subagent, loop)

**Mode:** DELEGATE (hook scripts, settings.json wiring, subagent/skill scaffolding,
loop script, README/log docs) + PAIR (the RT-safety hook's banned-token list and
brace-matching scope logic — mechanical to write, but the human should read it
line-by-line before trusting it in place of manual RT-safety vigilance)

**Task:** Build synergy between hooks, CLAUDE.md/COLLABORATION.md, subagents,
loops, and context-engineering. Delivered: three PreToolUse hooks
(`check_rt_safety.py`, `protect_human_owned.py`, `check_bash_denylist.py`) wired
via a new `.claude/settings.json`; the `invariant-hook-writer` subagent that turns
a described invariant into a tested hook going forward; the
`architecture-planning` skill that routes future architectural decisions to a
hook/ADR/subagent/loop/doc-update; `bench/check_prompt_regression.py` as a
concrete, cost-aware loop use case; and a new COLLABORATION.md §8 "Where
Information Lives" convention.

**Would do differently:** Assumed the `.claude/settings.json` hook schema used a
`{"hooks": {...}}` wrapper (correct for a *plugin's* `hooks/hooks.json`, wrong for
a project's `settings.json`, which takes event names directly at the root) —
caught by reading the plugin-dev marketplace's own `hook-development` skill and
its schema validator script before wiring anything in, rather than after. Also
assumed the audio-thread class was named `PluginProcessor`; it's actually
`PluginForgeProcessor` — caught immediately by a red/green test that unexpectedly
came back green when it should have blocked, rather than by inspection, which is
exactly why the verification step existed. Separately, while seeding
`bench/check_prompt_regression.py`'s baseline, found that the committed
`bench/results/results.json` (timestamped 2026-05-10, after ADR-009 was accepted)
shows a Claude/Faust rate of 84%, not the ≥96% ADR-009's Consequences section says
should have been confirmed by a re-run — that re-run does not appear to have ever
landed in a committed results file. Seeded the baseline with the honest 84%
rather than the aspirational number; flagged for the human rather than resolved.

**Mode signal:** Hooks, settings.json, and the loop script were verified end-to-end
with synthetic stdin payloads and the plugin marketplace's own schema validator —
all red/green pairs passed, matching the DELEGATE bar of "an objective,
tool-verifiable answer." The subagent and skill definitions could only be
schema-validated (frontmatter parses, matches the reference `agent-creator`/skill
examples) — Claude Code does not hot-load new `.claude/agents/` or
`.claude/skills/` entries mid-session (confirmed by attempting to invoke both and
getting "not found"), so their actual behavior is unverified until a restart.
Deliberately did not trigger the paid Claude-API leg of the regression-check loop
during seeding, to avoid spending API budget unasked — the human should run that
once deliberately.

---

### 2026-07-16 — Finish PluginEditor.cpp stub (Editor → LLM bridge)

**Mode:** PAIR

**Task:** Wire `generateButton.onClick` in PluginEditor.cpp to spawn `generate.py
--prompt`, parse the JSON response, and call `processor.loadFaustCode()` —
finishing the outstanding PAIR draft in `docs/pair_draft_editor_llm_bridge.md` from
the 2026-05-20 session.

**Would do differently:** Nothing on the API-verification side — having a real
JUCE checkout at `/home/losera/JUCE/modules` this time let every `TODO VERIFY API`
marker from the prior draft get resolved against primary sources instead of left
open, and it also caught a wrong claim in the prior draft (SafePointer's null-check
was assumed atomic; it's actually a plain pointer, safe only by message-thread
confinement). One gap: `cmake --build` surfaced that `FaustEngine.cpp` currently
contains broken code (a `ParamPool::pushToFaust()` definition with an empty
`for ()`, in the wrong file) despite CLAUDE.md marking it "IMPLEMENTED" — should
have run a build earlier in a prior session to catch this drift between
CLAUDE.md's status claims and actual file contents.

**Mode signal:** Correct mode — matches COLLABORATION.md's own listed PAIR example
for this exact task. `PluginEditor.cpp.o` and `PluginProcessor.cpp.o` compiled
clean against real headers; the full link failed only on the pre-existing
`FaustEngine.cpp` breakage and a missing system GTK dev package (both out of scope,
both flagged rather than touched).

---

### 2026-05-20 — Session review: status audit + CLAUDE.md update

**Mode:** DELEGATE (documentation)

**Task:** Full session review — read all project files, verify what's done vs. outstanding, update CLAUDE.md status to reflect actual state, deliver project overview.

**Would do differently:** Should cross-reference the collaboration_log.md against the task list in next_steps.md to flag when a DELEGATE task was silently completed across sessions without a log entry — FaustEngine.cpp and ParamPool.cpp both landed in previous sessions but there was no log entry confirming the PAIR drafts were accepted as-written or rewritten. Knowing whether the human rewrote them would improve future mode calibration.

**Mode signal:** CLAUDE.md status was stale (still said "Day 1 stub"). No code changes pending at DELEGATE level; the outstanding work is one PAIR item (PluginEditor.cpp) and two USER-owned items (ADR-011 IPC ADR, activeLabels thread-safety). Mode classification was straightforward.

---

### 2026-05-07 — Editor → LLM bridge: PAIR draft + DELEGATE fixes

**Mode:** DELEGATE (test_faust_compile.py integration marker, generate.py --prompt mode) + PAIR (editor subprocess wiring draft)

**Task:** Post-audit session. Execute outstanding DELEGATE items, produce PAIR draft for the one remaining Day-2 blocker: wiring generateButton.onClick to call generate.py and feed Faust code to loadFaustCode().

**Would do differently:** Should have verified juce::JSON::parse field-access API from installed headers before writing the draft — left a TODO VERIFY instead of a confirmed call, which means the human still has to check one more thing that could have been resolved here.

**Mode signal:** DELEGATE tasks (integration marker, --prompt flag) landed cleanly. PAIR draft required four TODO VERIFY markers (ChildProcess::start StringArray form, readAllProcessOutput blocking, JSON::parse field access, SafePointer thread-safety) — correctly reflects that these API details need human verification against installed headers before committing.

---

### 2026-05-07 — Codebase audit and bug-fix session

**Mode:** DELEGATE (syntax fixes, CI, prompts, scaffolding) + PAIR (compile() draft, pushToFaust(), audio_thread_example.md, --prompts flag)

**Task:** Audit full codebase state, fix all compilation bugs, apply outstanding DELEGATE and PAIR tasks from docs/next_steps.md, produce the audio-thread reference doc planned in docs/handoff_audiothread_task.md.

**Would do differently:** Should have checked `UI::addSoundfile()` for pure-virtual status before writing ParamCapture — required an extra build-fix round-trip. Reading the full UI.h header first would have caught it in one pass.

**Mode signal:** DELEGATE tasks (CMake libfaust link, ADR-009 prompts, CI workflow, recovery_prompts.json, syntax fixes) landed without iteration. PAIR tasks (compile() draft, pushToFaust()) required the human to verify API and threading invariants; audio_thread_example.md captures those for the next session. The lambda-signature bug in PluginProcessor.cpp (CompileCallback arity) was a clear DELEGATE catch. The GTK system dependency for juce_gui_extra is pre-existing and outside scope.

---

### 2026-05-05 — Architecture interview and Day-2 plan

**Mode:** PAIR (decision-making session, no code produced)

**Task:** Full design-tree interview to resolve all open architectural decisions before Day-2 implementation begins. Walked branch-by-branch through: DSL choice, provider strategy, model-swappability, IPC mechanism, `compiler/` directory purpose, FaustEngine threading model, and ParamPool audio-thread contract.

**Would do differently:** The `compiler/Dockerfile` branch could have been resolved faster by reading the files first before asking the user — the content made the answer unambiguous (AOT `faust -lang cpp` vs in-process JIT). Asking the user about something I could have determined myself cost a round-trip.

**Mode signal:** Correct mode. No code was produced; the output was a locked decision list and a sequential task plan. The user answered quickly and with confidence on most branches, indicating the questions were well-scoped. The one hesitation (persistent server vs subprocess) resolved correctly once the implementation complexity trade-off was made explicit — suggesting the pre-task protocol of surfacing trade-offs before accepting an answer is working.

### 2026-07-19 — generation-stress-tester agent
**Mode:** DELEGATE
**Task:** Create a subagent that stress-tests the NL→Faust pipeline with oversized/adversarial prompts and classifies failures; diagnose why giant specs produce Faust syntax errors.
**Would do differently:** Nothing yet — agent is untested against a live run; first real invocation may reveal the failure-mode table needs a category.
**Mode signal:** Pending — human should read the agent's boundaries section (HUMAN-OWNED prompt files, API budget) before first use.
