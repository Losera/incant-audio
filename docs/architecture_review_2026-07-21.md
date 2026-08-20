# PluginForge — Architecture Review & Prompt-Routing Proposal Assessment

**Frozen point-in-time record, 2026-07-21. Not maintained — read for what was known that day,
verify anything load-bearing against current code.** (Kept on disk: cited as a live-file fixture
by `tests/test_control_wiring.py:364`.)

**Date:** 2026-07-21
**Author:** Lead systems architect (Claude Code session)
**Scope:** Full pipeline review, input → output; assessment of the proposed per-prompt
rating / routing system; recommended sequencing before further feature work.
**Method:** Read of every source file in `host/Source/`, `llm/`, `bench/`, `tests/`,
`.claude/hooks/`, plus `/usr/include/faust/gui/MapUI.h`. Test suite executed
(234 pass / 10 deselected). Claims in `CLAUDE.md` were verified against code, not
assumed.

---

## 0. Executive summary

The architecture is sound in its **bones** and has one genuinely excellent component
(the `FaustEngine` compile/swap protocol). The decision record is unusually
disciplined. But the system is **not currently delivering working plugins**, for a
reason no document records: parameter values are pushed into Faust without
denormalization, so almost every generated patch runs with its controls pinned near
zero. Two further defects (no state persistence, a shutdown use-after-free on the
detached compile thread) mean the artifact is not yet a DAW-safe plugin.

On the systems lead's proposal: **the instinct is right, the mechanism proposed is
wrong for this system.** A learned per-prompt *rating* driving *model selection* is
the standard answer for systems that cannot check their own output. PluginForge can
check its own output — it has a free, fast, deterministic compiler oracle in the loop.
That changes the optimal design from "predict difficulty, pick a model" to
"generate cheaply, verify, escalate." The routing win here is a **cascade**, not a
classifier, and it is roughly five lines of code. The much larger win hiding inside
the same proposal is the *prompt translation* half — for which this project already
has unusually strong evidence — and the prerequisite for all of it is a generation
ledger that does not currently exist.

**Recommended order: fix the three defects → build the objective function → cascade →
then, and only then, consider a rating system.**

---

## 1. The pipeline, end to end

### 1.1 Control flow

```
 [1] User types prompt          PluginEditor.cpp:48  generateButton.onClick
        │                       (message thread; disables button, sets status)
        ▼
 [2] Detached std::thread       PluginEditor.cpp:93
        │                       juce::ChildProcess::start(StringArray{python3,
        │                       generate.py, --prompt, text})   ← ADR-011, argv, no shell
        │                       waitForProcessToFinish(120s) + kill() on expiry
        ▼
 [3] generate.py --prompt       llm/generate.py:199 → _run_subprocess_mode
        │                       providers.assert_free()   ← free-only gate
        │                       providers.check_credentials()
        ▼
 [4] generate_json()            llm/generate.py:103
        │  ┌── loop, ≤3 attempts (ADR-005) ─────────────────┐
        │  │  generate_faust()   → providers.make_generator()│
        │  │       └─ adapter: anthropic | gemini | openai_compat
        │  │          + _pace() + backoff + fence-stripping  │
        │  │  validate_faust()  → `faust -lang cpp … -o /dev/null`
        │  │       └─ on failure: stderr appended to next user message
        │  └────────────────────────────────────────────────┘
        ▼
 [5] One JSON line to stdout    {"success","faust_code","attempts","error"}  ← ADR-011
        │                       always exit 0; diagnostics to stderr only
        ▼
 [6] Editor parses last "{" line, calls proc.loadFaustCode()   PluginEditor.cpp:190
        ▼
 [7] FaustEngine::compile()     FaustEngine.cpp:91 — detached thread, compileMutex
        │                       createDSPFactoryFromString (LLVM JIT)
        │                       ParamCapture  → label/min/max/init/step/kind
        │                       MapUI newUI   → label → FAUSTFLOAT* zone
        │                       ── swap protocol (7 steps, see §2.1) ──
        │                       cb() → ParamPool::remap() BEFORE ready=true
        ▼
 [8] onFaustCompileSuccess      → callAsync → status label + refreshParamKnobs()
        ▼
 [9] processBlock()             PluginProcessor.cpp:48 — every audio callback
                                enterAudio() → paramPool.pushToFaust()
                                            → faustEngine.process() → exitAudio()
```

### 1.2 File-by-file assessment

| File | LOC | Verdict |
|---|---|---|
| `host/Source/FaustEngine.h/.cpp` | 96 / 178 | **Strong.** The Dekker-style `audioBusy`/`ready` handshake is correct and correctly *explained*. The seq_cst justification in `enterAudio()` is the kind of comment that survives a maintainer. One real defect (§2.2). |
| `host/Source/ParamPool.h/.cpp` | 43 / 76 | **Broken contract.** Double-buffer publication is right. But `remap()` receives full `ParamInfo` and stores only `label`, discarding the min/max/step needed downstream. Root cause of the P0 defect (§2.1). |
| `host/Source/PluginProcessor.h/.cpp` | 71 / 101 | Correct as far as it goes. `getStateInformation`/`setStateInformation` are empty stubs — see §2.3. `outputLevel` relaxed store is fine. |
| `host/Source/PluginEditor.h/.cpp` | 52 / 369 | Careful thread reasoning, well documented. Carries UI, IPC, path discovery, and JSON parsing in one file — the seam to split when the UX roadmap lands. `MAX_KNOBS=8` vs `POOL_SIZE=64` (§2.5). |
| `llm/generate.py` | 206 | Clean. Retry loop is the ADR-005 contract, faithfully. Model-agnostic since the provider refactor. No telemetry (§4.1). |
| `llm/providers.py` | 574 | **Best-in-class for its size.** Five providers, three adapters, no new deps. The `min_max_tokens` and `no_temperature_models` fields encode hard-won empirical facts at the exact place they apply. The `_is_daily_quota` early-out is a genuinely subtle correctness win. |
| `llm/faust_validator.py` | 19 | Duplicates `generate.validate_faust` (different signature, same subprocess call). Harmless, but two definitions of the oracle is one too many when the oracle is about to become load-bearing. |
| `bench/run_benchmark.py` | 246 | Fine. `CLAUDE_MODEL` pinned to `opus-4-6` while production is `opus-4-8` — deliberate and documented, but a comparability landmine (§4.2). |
| `bench/run_efficacy_study.py` | 294 | Well-designed experiment. Confound controls are stated and enforced. Incremental writes after each record is the right call for a quota-limited run. |
| `bench/score_efficacy.py` | 455 | Good taxonomy work. The `--judge` path is the only fidelity signal in the project and it is **off by default** (§3.4). |
| `.claude/hooks/*` | 49–211 | Genuinely good idea, honestly scoped. `check_adr009_prompt_sync.py`'s docstring correctly distinguishes "the rule" (unhookable) from "the rule's text" (hookable). But see §2.4 — what it *doesn't* check has already happened. |
| `tests/` | 1,236 | 234 pass, 0.87s. `conftest.py`'s `PLUGINFORGE_PROVIDER=anthropic` pin is the right fix for a real incident. Coverage is Python-only; the C++ RT path has one TSan test and no unit tests. |

---

## 2. Defects found (ranked)

### 2.1 P0 — Parameter values are never denormalized. Generated plugins do not work.

**The chain:**

- `PluginProcessor.cpp:28` — all 64 slots are created as
  `AudioParameterFloat(id, name, 0.0f, 1.0f, 0.0f)`. Range is **0–1, always**.
- `ParamPool.cpp:36` — `remap()` stores `p.label` and calls `juce::ignoreUnused(p)`.
  **`min`, `max`, `step`, and `kind` are discarded.**
- `ParamPool.cpp:74` — `engine.setParamValue(labels[i], fp->get())`. `fp->get()`
  returns the slot's raw value, i.e. **0–1**.
- `/usr/include/faust/gui/MapUI.h:150-171` — `setParamValue` does
  `*iter->second = value` with **no clamping and no range mapping**.

**Consequence:** a generated `hslider("Cutoff [unit:Hz]", 1000, 20, 20000, 1)` receives
a value between 0.0 and 1.0. The filter cutoff is set to **under 1 Hz**, regardless of
knob position. Every patch whose parameters are not natively 0–1 (i.e. every filter,
every delay time, every dB gain, every frequency) is inaudible or wrong. Only
0–1-ranged params (mix, depth, feedback) happen to work by coincidence.

**The intent was clearly there and got half-implemented.** `PluginEditor.cpp:295-301`
*normalizes* the Faust default into the 0–1 slot correctly, with a comment saying it is
"forward-compatible with a denormalising `pushToFaust`". That denormalizing
`pushToFaust` was never written, and `remap()` throws away the data it would need.

**This is almost certainly why "the audible half of P6 is still unrun" has stayed
unrun.** It will fail on the first patch.

**Fix shape (HUMAN-OWNED — audio path):** change `labelBuffers` from
`std::vector<std::string>` to `std::vector<ParamInfo>`; in `pushToFaust`, push
`p.min + fp->get() * (p.max - p.min)`. Handle `Button`/`CheckButton` as a threshold at
0.5. Roughly 15 lines, no new synchronization — the existing double-buffer already
publishes whatever the buffer holds.

**Do this first. No measurement of this system means anything until it is true.**

### 2.2 P1 — Shutdown use-after-free on the detached compile thread.

`FaustEngine::compile()` (FaustEngine.cpp:93) launches `std::thread(...).detach()` with
`this` captured. `~FaustEngine()` (FaustEngine.cpp:52) deletes `activeDSP` and the
factory. There is **no join, no shutdown flag, and no wait**.

If the plugin is unloaded (DAW closes the project, user removes the plugin) while a JIT
compile is in flight — a window of tens to hundreds of milliseconds for LLVM — the
detached thread subsequently touches `this->compileMutex`, `this->activeDSP`,
`this->factory`, `this->ready`, and `this->audioBusy` on freed memory, and calls `cb`,
which is a lambda holding a freed `PluginForgeProcessor*`.

The elaborate drain protocol protects the *audio thread* from the compile thread. Nothing
protects the compile thread from *destruction*.

The same class of bug exists on the editor's generate thread (`PluginEditor.cpp:93`):
`SafePointer` correctly guards the editor, but `proc.loadFaustCode(faustCode)` at line
190 uses a raw reference. The comment argues the processor outlives the editor — true —
but the *detached thread* can outlive the processor, because it may be parked in a 120-second
`waitForProcessToFinish` when the DAW tears the whole plugin down.

**Fix shape (HUMAN-OWNED):** a `std::shared_ptr` control block or an
`std::atomic<bool> shuttingDown` + latch that the destructor waits on. This is exactly
the "confident-sounding wrong code causes hard-to-debug user-facing failures" category
COLLABORATION.md reserves for the human — reported, not fixed.

### 2.3 P1 — No state persistence. Saving a DAW session discards the plugin.

`PluginProcessor.h:30-31`:

```cpp
void getStateInformation(juce::MemoryBlock&) override {}
void setStateInformation(const void*, int) override {}
```

Reopening a saved project restores 64 macro slots to defaults with **no DSP loaded and
no way to recover the generated patch** — the Faust source exists nowhere but in the
JIT'd factory and the user's memory. For a plugin whose entire value is a generated
artifact, this is a data-loss bug, not a missing feature. `docs/ux_roadmap.md` already
names it the Phase 1 blocker; this review concurs and raises its severity: it belongs
with the P0/P1 defects, not in a UX phase.

Persisted state must include the **Faust source string and the originating prompt**, not
just APVTS values.

### 2.4 P1 — The benchmark does not measure the production prompt.

`llm/prompts/system_prompt.txt` and `bench/prompts/system_faust.txt` have diverged
substantially. `diff` shows the bench prompt carries:

- a 16-line **"AVAILABLE STDLIB HIGHLIGHTS"** section (`fi.resonlp`, `ef.flanger`,
  `co.compressor_stereo`, …) that the production prompt does not have;
- three additional stereo-wiring rules;
- a **different example set** (the production prompt's ping-pong and chorus examples are
  replaced by a hand-written stereo delay with `with{}`-free wiring).

`check_adr009_prompt_sync.py` verifies exactly one sentence and one regex. It is doing
precisely what its docstring says — but the property the team believes it protects
("the two prompt files can't drift apart") is **not** the property it enforces, and the
files have in fact drifted.

**This invalidates the transfer of every benchmark number to production.** The 0.88
baseline, the ADR-009 verdict, and the entire efficacy study were measured on a prompt
that is materially more grounded than the one the plugin ships. The stdlib-highlights
block in particular is a direct mitigation for the HALLUCINATION failure class — so the
benchmark is likely *overstating* production hallucination resistance.

Two legitimate resolutions, and the team must pick one explicitly:
- **Unify** — production adopts the bench prompt (a HUMAN-OWNED edit), and the hook is
  strengthened to a full-file equality or a shared-include check; or
- **Fork deliberately** — declare the two prompts as separate artifacts with separate
  baselines, and stop citing bench numbers as production predictions.

Silently continuing is the only unacceptable option.

### 2.5 P2 — Smaller items

- **`fprintf` on the audio thread.** `MapUI::setParamValue` (MapUI.h:170) calls
  `fprintf(stderr, ...)` on a label miss. That is a lock + syscall inside `processBlock`.
  The 2026-07-19 swap-ordering fix removed the *cause* of the ~1,100-error storm, but the
  *hazard* is structural: any future label mismatch reintroduces an unbounded RT
  violation. **Fix and optimization in one:** resolve and cache the `FAUSTFLOAT*` zone
  pointers in `remap()` (via `MapUI::getParamZone`), and have `pushToFaust` write through
  cached pointers. This eliminates 64 `std::map<std::string>` lookups per block, removes
  all string work from the audio thread, and makes the `fprintf` path unreachable.
- **Editor shows 8 of 64 params.** `MAX_KNOBS = 8`; patches with more controls silently
  lose UI access to the remainder. `refreshParamKnobs` clamps with `jmin` and moves on.
- **No generation caching.** An identical prompt costs a fresh API call. Against Gemini's
  measured **20 requests/day**, a content-addressed cache keyed on
  `sha256(prompt + system_prompt + model)` is a large practical win for a trivial amount
  of code.
- **Two definitions of the compile oracle** — `llm/faust_validator.py:6` and
  `generate.py:72`. Converge before the oracle becomes load-bearing (§3.3).
- **`strip_fences=False` for anthropic** is a deliberate measurement-comparability
  choice that is also a live correctness gap — Anthropic models *can* emit fences. The
  comment justifies it for the baseline; it should not survive into production.
- **Env-var code execution surface.** `PLUGINFORGE_PYTHON` and `PLUGINFORGE_LLM_SCRIPT`
  let anything in the plugin's environment select the interpreter and script. Acceptable
  on a dev box; needs an ADR before distribution.

---

## 3. The proposal: per-prompt rating and routing

> *"…whether a rating system for each individual user prompt being pipelined into
> different pieces of the project (such as a different model or using certain prompt
> translation techniques) in order to scale the efficacy, efficiency and success of
> our system."*

### 3.1 What the project's own data already says

The efficacy pilot (`docs/prompt_efficacy_study.md` §7.1, N=50 first attempts) is the
most decision-relevant artifact in the repo:

| Tier | First-try | Retry-corrected | Mean attempts | Dominant failure class |
|---|---|---|---|---|
| L4 DSP engineer | 90% | 90% | 1.00 | INCOMPLETE (edge case) |
| L3 informed producer | 90% | 90% | 1.00 | INCOMPLETE (edge case) |
| L2 casual musician | 80% | 90% | 1.10 | + SYNTAX |
| **L1 vibe/metaphor** | **50%** | 80% | 1.50 | **SEMANTIC (3 of 5)** |
| L0 artist reference | 60% | 90% | 1.40 | SYNTAX (3 of 4) |

Three findings drive everything below:

1. **A 40-point first-try spread exists between phrasings of the same request.** This is
   a far larger effect than any plausible model-swap effect on the same prompt.
2. **Failure class varies by tier, not just failure rate.** L1 fails semantically; L0
   fails syntactically. These need *different* interventions — which means the routing
   signal must be a **class**, not a scalar rating.
3. **Retry rescues SYNTAX well and SEMANTIC poorly** (H4, partially supported). The
   existing retry loop is therefore differentially effective, and knows nothing about it.

### 3.2 Why a learned per-prompt rating is the wrong first move here

**(a) PluginForge has a verifier. Almost none of the systems that inspired this proposal
do.**

RouteLLM, FrugalGPT, NotDiamond, Martian, and OpenRouter's auto-router all exist to solve
one problem: *you cannot tell, at inference time, whether the cheap model's answer is
good enough, so you must predict it in advance.* Their entire apparatus — preference
data, learned scorers, difficulty classifiers — is a substitute for ground truth.

PluginForge has ground truth: `faust -lang cpp` returns in well under a second, costs
nothing, and is deterministic. When a verifier is cheap relative to generation, prediction
is strictly dominated by **generate → verify → escalate**. You do not need to guess which
prompts are hard; you find out for free, on the ones that actually are.

This is the single most important asymmetry in the assessment, and it inverts the standard
conclusion.

**(b) There is no data to route on.** Routing requires a per-model competence table. The
project has:
- one valid efficacy run (N=50, `opus-4-6`, on the *bench* prompt — see §2.4);
- one 25-prompt baseline (0.88, same caveats);
- an INVALID 125-prompt run (billing failure, 0 tokens, 0 data);
- ADR-008 (Claude vs Gemini) still marked **"Under evaluation"** after three months.

No cross-model comparison on the current prompt, current models, or a common corpus
exists. A router fitted on this would be a coin flip with added latency.

**(c) The free-only constraint inverts the economics.** Every off-the-shelf router
optimizes **cost per unit quality**. PluginForge's providers are free with **quota**
limits: Gemini 5 RPM / **20 RPD per model**, Groq ~14,400 RPD, OpenRouter ~50/day,
Ollama unlimited-but-local. The scarce resource is not dollars, it is *requests against
the provider that can actually do the job*. The right object here is a
**quota-aware scheduler with fallback ordering**, not a cost optimizer — a different
objective function that no off-the-shelf router models. `providers.py` already holds all
the quota knowledge, in prose, in `notes`. Promoting it to structured fields
(`rpd`, `rpm`, `tpm`) is the honest version of "routing" for this project.

**(d) The objective function is wrong, so a router would optimize the wrong thing.**
Every headline metric — ADR-006's first-try compile rate, the 0.88 baseline, the entire
efficacy study — measures **whether the code compiles**, not **whether it sounds like
what was asked for**. ADR-006 concedes this explicitly. A rating system trained against
compile rate will happily learn to favor whatever emits `process = _,_;` — a trivially
compiling passthrough that scores 100% and does nothing. The only fidelity signals in the
repo are `expected_primitives` substring matching (coarse; the study itself calls it
"weak resolution") and the `--judge` LLM rubric, which is **off by default and has never
been run**.

**Building a router before building the metric it optimizes is the highest-risk move
available.**

**(e) It adds a second HUMAN-OWNED prompt surface.** COLLABORATION.md §1 makes
`llm/prompts/*` human-authored product IP because "generation quality is sensitive to
exact wording." A prompt-translation stage introduces a *rewriter* prompt with exactly
the same sensitivity and the same ownership burden, plus a versioning coupling: the
rewriter and the generator prompt must be versioned together or no benchmark number is
comparable across time. §2.4 shows this project has already lost that battle once, with
a hook actively watching.

### 3.3 What to build instead — ranked by evidence-per-unit-effort

**① Build the objective function: an offline semantic oracle.** *(Prerequisite for
everything else.)*

Extend validation past "does it compile" to "does it behave." After a successful compile,
render the DSP offline (`faust2…`, or libfaust in a headless harness) against a short test
signal and assert cheap, robust properties:

- output is finite (no NaN/Inf) and stable over a few seconds;
- output actually differs from input (catches the passthrough degenerate case);
- sweeping each declared parameter across its range **changes the output** — the direct
  automated test for the §2.1 denormalization class of bug, and for dead controls generally;
- category-specific probes where cheap (a filter attenuates a swept sine above cutoff; a
  delay shows autocorrelation at the delay time).

This converts P6's "needs the human's ears" from a blocker into a regression test, and it
is the metric every subsequent decision — routing included — must be scored against.
`llm/faust_validator.py` is the natural home; converge `generate.validate_faust` into it
while you are there.

**② Instrument production: a generation ledger.** *(Prerequisite for any rating system.)*

`generate.py` records **nothing**. There is no log of real user prompts, chosen provider,
attempt count, error text, error class, or latency. The efficacy corpus is entirely
synthetic bench prompts. **You cannot build, train, or validate a per-prompt rating system
without this data, and it does not exist.**

Append one JSONL record per generation: prompt (or its hash, if privacy is a concern),
prompt length, provider, model, attempts, per-attempt error text, final classification via
`score_efficacy.classify_error`, wall-clock latency, success. This is ~20 lines, is
DELEGATE-mode work, and is the single highest-value change in this document after the
defect fixes. Six months of it is what makes §3.4 answerable with evidence instead of
argument.

**③ Model-escalating retry — the actual routing win.**

The retry loop escalates *context* (compiler stderr) but never *model*. Attempt 3 is the
same model that has now failed twice, and the documented failure modes are **persistent
across two months on one model** — `flanger_mono` hallucinated identically in 2026-05 and
2026-07-19. That is precisely the failure a different model breaks, and precisely the
failure more stderr will not.

Change `generate_with_retry` / `generate_json` to walk a provider ladder:

```
attempt 1 → primary        (fast/cheap: groq gpt-oss-120b, or local ollama)
attempt 2 → primary + stderr
attempt 3 → secondary + stderr    ← different model family
```

This is a **cascade**, the pattern FrugalGPT and AlphaCodium-style self-repair loops
converge on when a verifier is available. It captures most of the value the systems lead
is after, requires no classifier, no training data, and no new prompt surface, and it
composes naturally with the quota-aware fallback ordering in §3.2(c) — a provider that
has burned its daily quota simply drops down the ladder. Roughly five lines plus a
provider list.

**④ Prompt normalization — the defensible half of "prompt translation."**

This is where the systems lead's instinct is strongest and the data most supportive. The
L1→L4 gap is 40 points. A cheap pre-pass that rewrites a vague prompt into an L3/L4-style
specification before generation should recover most of it.

But note what the data actually says about *how* to trigger it. Because failure class
varies by tier and rescue-ability varies by class, the useful classifier output is a
**small discrete label** — roughly "has effect-category and parameter vocabulary" vs
"pure sensory metaphor" vs "cultural/artist reference" — not a continuous difficulty
score. That label can very likely be produced by a **deterministic lexical check**
(does the prompt contain a known effect noun, a parameter noun, a unit?) rather than a
model call. Start there: it is free, testable, has no prompt surface of its own, and is
directly checkable against the 125-prompt tiered corpus you already have.

Gate the rewriter behind an A/B on that corpus. If it does not beat the null hypothesis
by a clear margin on the §3.1 numbers, it does not ship.

**⑤ Dynamic stdlib grounding — the targeted fix for HALLUCINATION.**

The persistent `flanger_mono` failure is not a difficulty problem; it is a **grounding**
problem. The model invents a function because it has no authoritative list of what exists.

The bench prompt's hand-maintained "AVAILABLE STDLIB HIGHLIGHTS" block is already a
static, manually-curated retrieval step — and it is a plausible contributor to the
bench/production gap in §2.4. Make it dynamic and correct: extract the real signature
list from the *installed* `stdfaust.lib`, select the entries relevant to the prompt, and
inject them as grounding context. This is standard retrieval practice, attacks the
hallucination class directly and mechanically, and — importantly — lives in the *context*,
not in the HUMAN-OWNED prompt text, so it sidesteps the ownership and versioning problem
in §3.2(e) entirely.

**⑥ Content-addressed generation cache.** See §2.5. Trivial, and material against a
20-requests-per-day quota.

### 3.4 When to revisit the rating system

Reopen the learned-router question when **all** of the following hold:

- §3.3① exists — decisions are scored on behavior, not compile success;
- §3.3② has accumulated on the order of **10³ labeled real generations**, not 50 synthetic ones;
- a valid cross-model efficacy run exists on a **common prompt** (i.e. §2.4 resolved), giving
  a real per-model competence table;
- the cascade (§3.3③) is in place and measured, establishing the baseline a router must beat.

At that point the right shape is very likely still not a learned rating model. It is a
**quota-aware cascade with an empirical per-class provider ordering** — a lookup table
derived from the ledger, versioned alongside the prompts, and re-derived when either
changes. That is auditable, debuggable, cheap, and it is what the evidence in this
repository supports. RouteLLM's actual contribution, adapted honestly to this project, is
the observation that the *labels* matter more than the model that consumes them; here the
compiler and the semantic oracle produce those labels for free.

---

## 4. Cross-cutting observations

### 4.1 The project measures its benchmark far better than it measures itself

`bench/` is rigorous: confound controls stated and enforced, incremental writes, an error
taxonomy refined against real compiler output, an INVALID run correctly preserved and
labeled rather than deleted. `llm/generate.py` in production logs nothing at all. The
observability asymmetry between the experiment and the product is the largest structural
gap in the architecture, and it is what makes §3 a matter of argument rather than data.

### 4.2 Version pinning is documented but fragile

`bench/run_benchmark.py:37` pins `CLAUDE_MODEL = "claude-opus-4-6"` while
`providers.py:147` defaults anthropic to `claude-opus-4-8`. Both choices are deliberate
and commented. But the baseline file `bench/results/.prompt_baseline.json` carries a bare
number (0.88) whose validity depends on a model, a prompt file, a fence-stripping flag,
and a provider — none of which are recorded alongside it. **Make the baseline a record,
not a scalar:** `{score, model, prompt_sha256, provider, strip_fences, date}`, and have
`check_prompt_regression.py` refuse to compare across mismatched provenance. This is
cheap insurance against silently comparing incomparable numbers, which §2.4 shows is
already happening.

### 4.3 The collaboration apparatus is a genuine asset — and slightly over-trusted

The three-mode framework, the fail-closed PreToolUse hooks, and the ADR discipline are
better than most teams of this size manage. The failure mode to watch is the one §2.4
demonstrates: a hook that checks a *proxy* for an invariant can create more confidence
than the invariant warrants. Recommend an explicit convention — each hook's docstring
already states what it checks; add a one-line **"what this does NOT catch"** clause, and
surface those clauses in `COLLABORATION.md` so the gaps are as visible as the guarantees.

### 4.4 What is genuinely good, and should not be touched

- The `FaustEngine` swap protocol. The seq_cst Dekker handshake, the drain, the
  callback-before-`ready` ordering, and the off-audio-thread teardown are all correct and
  correctly reasoned. The comments explain *why*, including what the two previous bugs
  were. Leave it alone except for §2.2.
- `providers.py`'s registry shape. Adding a provider is a dataclass entry.
- ADR-011's argv one-shot subprocess. Stateless, crash-isolated, debuggable, and the
  latency argument is correct. The "revisit trigger" clause is exemplary ADR practice.
- Faust as the target (ADR-001/002). Nothing in this review disturbs it; the compiler
  oracle it provides is precisely what makes §3.2(a) true and is the project's single
  biggest architectural advantage over a raw-C++ or JSON-IR design.

---

## 5. Recommended sequence

| # | Action | Mode | Blocks |
|---|---|---|---|
| 1 | Denormalize params in `pushToFaust`; carry `ParamInfo` through `remap` (§2.1) | HUMAN-OWNED | Everything audible |
| 2 | Implement `get`/`setStateInformation` incl. Faust source + prompt (§2.3) | PAIR | Any real DAW use |
| 3 | Fix compile-thread shutdown UAF (§2.2) | HUMAN-OWNED | Distribution |
| 4 | Resolve bench/production prompt divergence — unify or fork, explicitly (§2.4) | HUMAN-OWNED | All measurement |
| 5 | Cache zone pointers in `remap`; remove string lookups + `fprintf` from RT path (§2.5) | HUMAN-OWNED | — |
| 6 | Offline semantic oracle (§3.3①) | PAIR | The objective function |
| 7 | Generation ledger in `generate.py` (§3.3②) | DELEGATE | Any rating system |
| 8 | Model-escalating retry cascade (§3.3③) | PAIR | — |
| 9 | Structured quota fields in `ProviderSpec`; quota-aware fallback (§3.2c) | DELEGATE | — |
| 10 | Baseline provenance record (§4.2) | DELEGATE | — |
| 11 | Re-run P9 (125-prompt) on groq, post-fix, post-prompt-decision | DELEGATE | §3.4 |
| 12 | Deterministic prompt classifier + normalization A/B (§3.3④) | PAIR | — |
| 13 | Dynamic stdlib grounding (§3.3⑤) | PAIR | — |
| — | *Learned per-prompt rating / model router* | — | **Deferred — see §3.4** |

Items 1–5 are defect repair and should land before any new capability. Items 6–7 are the
measurement substrate. Item 8 is the cheapest large win. Items 12–13 are the parts of the
original proposal worth building, once 6–7 can tell you whether they worked.

---

## 6. New ADRs this review implies

| Proposed | Subject |
|---|---|
| ADR-013 | Parameter normalization contract between APVTS slots and Faust zones (§2.1) |
| ADR-014 | Plugin state persistence format — what is saved, and versioning (§2.3) |
| ADR-015 | Prompt-surface policy: one prompt or two, and how the benchmark relates to production (§2.4) |
| ADR-016 | Success metric v2: semantic oracle alongside compile rate; supersedes/extends ADR-006 (§3.3①) |
| ADR-017 | Retry cascade and quota-aware provider ordering; extends ADR-005 (§3.3③) |
| ADR-018 | Generation telemetry — what is recorded, retention, privacy posture (§3.3②) |

ADR-008 (Claude vs Gemini) should also be closed or formally withdrawn — it has been
"Under evaluation" since 2026-04-29 and the free-only pivot has overtaken its premise.
