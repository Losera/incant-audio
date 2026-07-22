# Handoff — Claude-Web architecture verification prompt

**Purpose:** paste the block below into Claude-Web (a fresh conversation, no repo access)
to obtain an independent adversarial review of the architectural findings in
`docs/architecture_review_2026-07-21.md`.

**Why a separate prompt rather than sharing the review:** the review was produced by an
agent with repo access, and an agent that can see the code tends to be believed. This
prompt is written to make the reviewer *attack* the conclusions, and it carries enough
primary evidence (actual code, actual numbers) that the reviewer can disagree on
substance rather than vibes.

**Notes for whoever runs it:**
- Everything between the rules is the prompt. Paste it verbatim.
- If Claude-Web supports file attachments, also attach
  `docs/architecture_review_2026-07-21.md`, `docs/prompt_efficacy_study.md`, and
  `docs/decisions.md`, and add the one-line addendum noted at the end.
- Expect a long response. It is designed to produce a decision, not an essay.

---

You are acting as an independent principal-level review board for a small audio-software
team. Your job is **adversarial verification**, not validation. An internal architecture
review has produced a set of findings and a recommended sequence; the team wants to know
where that review is **wrong, overconfident, or missing something** before they commit
engineering time. Agreement that is not earned is worse than useless to them — if a
finding is correct, say so in one line and move on; spend your effort on what is not.

You have no access to the repository. All evidence you need is below. Where the evidence
is insufficient to settle a question, **say so explicitly and state what single piece of
evidence would settle it** — do not fill the gap with plausible-sounding inference.

## 1. The project

**PluginForge** — LLM-guided program synthesis for real-time DSP audio plugins. A user
types a natural-language description ("a warm analog chorus"), and a VST3/AU plugin
generates, JIT-compiles, and runs the corresponding DSP live in a DAW.

```
natural language → LLM → Faust DSL → libfaust/LLVM JIT → live DSP in a VST3/AU plugin
```

Team size: one human engineer plus AI agents. Stage: working end-to-end prototype, not
shipped. Platform: Arch Linux, JUCE 7, C++17, Python 3.

**Settled decisions — treat as constraints, do not relitigate unless you find a
first-order technical error:**

- **ADR-001/002:** the LLM emits **Faust DSL** directly. Raw C++ and a custom JSON IR
  were both explicitly evaluated and rejected. Faust is an algebraic DSP language with a
  small grammar and a large standard library (`stdfaust.lib`).
- **ADR-003:** JUCE 7 for the plugin framework.
- **ADR-004:** because VST3/AU require a fixed parameter count at load time, the plugin
  pre-allocates **64 parameter slots** (`macro_0`…`macro_63`), and each generated patch's
  parameters are mapped into them.
- **ADR-005:** on Faust compile failure, the compiler's stderr is appended to the next
  LLM message and generation is retried, **up to 3 attempts total**.
- **ADR-006:** the primary success metric is **first-try compile rate**.
- **ADR-011:** the plugin invokes the Python LLM layer as a **one-shot argv subprocess**
  (`python3 generate.py --prompt "…"`), which returns exactly one JSON line on stdout.
- **Free-only providers.** The team's paid API credit ran out. The system now runs on
  free tiers only — Google Gemini, Groq, OpenRouter, or local Ollama — behind a provider
  registry. Anthropic remains in the registry but is gated behind an explicit opt-in
  environment variable. **Quota, not money, is the binding constraint.** Measured
  2026-07-21: Gemini free tier = 5 requests/minute and **20 requests/day, per model**;
  Groq ≈ 14,400 requests/day; OpenRouter ≈ 50/day; Ollama unlimited but local.

## 2. Architecture, input to output

```
[1] User types a prompt in the plugin UI (JUCE message thread)
[2] A detached std::thread spawns: python3 generate.py --prompt "<text>"
    (argv array — no shell interpretation; 120s timeout then kill)
[3] generate.py resolves provider from env, checks credentials, enforces free-only
[4] Retry loop, up to 3 attempts:
      LLM call  →  Faust code
      `faust -lang cpp <tmp>.dsp -o /dev/null`  →  compiles? / stderr
      on failure: stderr appended to the next user message, retry
[5] One JSON line to stdout: {success, faust_code, attempts, error}; always exit 0
[6] Plugin parses it, calls loadFaustCode()
[7] FaustEngine::compile() on a detached thread:
      createDSPFactoryFromString (LLVM JIT)
      capture parameter metadata (label, init, min, max, step, widget kind)
      build a MapUI (label → float* pointer into the DSP's memory)
      atomic swap of the live DSP under the audio thread
      callback → ParamPool::remap() publishes new labels
[8] processBlock(), every audio callback:
      enterAudio() guard
      ParamPool::pushToFaust()  — write current parameter values into the DSP
      dsp->compute()            — actual audio processing
      exitAudio()
```

Approximate sizes: `FaustEngine` 274 lines, `ParamPool` 119, `PluginProcessor` 172,
`PluginEditor` 421, `generate.py` 206, `providers.py` 574, benchmark/eval harnesses ≈1,200,
Python tests 1,236 (234 passing, verified by execution).

## 3. Findings to verify

Each finding below states what was **verified by reading code or executing tests**, versus
what is **inferred**. Attack the inferences hardest.

### Finding A — parameters are never denormalized (claimed severity: critical)

Verified by reading four locations:

1. All 64 slots are created with a fixed **0.0–1.0** range:
   ```cpp
   std::make_unique<juce::AudioParameterFloat>(
       ParamPool::slotId(i), "Macro " + juce::String(i + 1),
       0.0f, 1.0f, 0.0f);
   ```
2. The remap step receives full metadata but stores only the label, explicitly discarding
   the rest:
   ```cpp
   const auto& p = params[i];
   buffer.push_back(p.label);
   juce::ignoreUnused(p);   // min / max / step / kind dropped here
   ```
3. Each audio block pushes the raw slot value straight through:
   ```cpp
   auto* fp = dynamic_cast<juce::AudioParameterFloat*>(slots[i]);
   if (fp) engine.setParamValue(labels[i], fp->get());   // fp->get() ∈ [0,1]
   ```
4. Faust's `MapUI::setParamValue` (from the installed `/usr/include/faust/gui/MapUI.h`)
   performs **no range mapping and no clamping**:
   ```cpp
   const auto iter = fPathZoneMap.find(str);
   if (iter != fPathZoneMap.end()) { *iter->second = value; return; }
   // … falls through to a shortname map, then a label map, then:
   fprintf(stderr, "ERROR : setParamValue '%s' not found\n", str.c_str());
   ```

**Claim:** a generated `hslider("Cutoff [unit:Hz]", 1000, 20, 20000, 1)` receives a value
in [0,1], so the filter cutoff is set below 1 Hz regardless of knob position. Every patch
whose parameters are not natively 0–1 — every filter frequency, delay time, dB gain — is
inaudible or wrong. Only coincidentally-0–1 parameters (mix, depth, feedback) work.

**Supporting circumstantial evidence:** a *separate* code path in the editor normalizes
the Faust default *into* the 0–1 slot correctly, with a comment saying it is
"forward-compatible with a denormalising `pushToFaust`" — which was never written. And
the team's listening test has been repeatedly deferred as "needs the human's ears" and
has never been run.

**Verify:** Is the reasoning sound? Is there any mechanism in JUCE, Faust, or this flow
that would rescue it that the review missed? Is "critical, blocks all audible
functionality" the right severity? Is the proposed fix — carry the full metadata through
remap and compute `min + normalized * (max - min)`, with a 0.5 threshold for
button/checkbox widgets — correct and complete? What does it get wrong about
**logarithmic** parameters (audio frequency and gain controls are conventionally
log-scaled, and a linear map from a 0–1 slot to 20–20000 Hz puts 10 kHz at the halfway
point of the knob, which is musically unusable)? Should the slots have been declared with
`NormalisableRange` and a skew instead — and what does that cost, given the slot ranges
are fixed at plugin load but the patch is not known until later?

### Finding B — use-after-free on shutdown (claimed severity: high)

The JIT compile runs on a **detached** thread capturing `this`:

```cpp
void FaustEngine::compile(const juce::String& faustCode, CompileCallback cb)
{
    std::thread([this, code = faustCode.toStdString(), cb]() mutable {
        std::lock_guard<std::mutex> lock(compileMutex);
        // … LLVM JIT compile, then mutate this->activeDSP, this->activeUI,
        //   this->factory, this->ready, this->audioBusy; then invoke cb …
    }).detach();
}
```

The destructor does not wait for it:

```cpp
FaustEngine::~FaustEngine()
{
    delete activeDSP.load();
    if (factory) deleteDSPFactory(factory);
}
```

There is no join, no shutdown flag, and no latch. **Claim:** if the plugin is unloaded
while a compile is in flight (an LLVM JIT compile is tens to hundreds of milliseconds),
the detached thread subsequently touches freed memory, and `cb` holds a freed processor
pointer.

An analogous case exists on the editor side: a detached thread waits up to **120 seconds**
for the LLM subprocess, then calls `processor.loadFaustCode(...)` through a raw reference.
The editor itself is guarded by JUCE's `SafePointer`, and an in-code comment argues the
processor always outlives the editor — which is true — but the review claims the *detached
thread* can outlive the *processor*, because the DAW can tear down the entire plugin
during that 120-second window.

**Verify:** Are both races real? Under what concrete DAW behavior does each actually
fire, and how likely is that in practice — is this a "will crash in the wild" bug or a
"theoretically reachable" one? Is the second one (editor thread vs. processor) actually
reachable given JUCE's plugin-destruction ordering and the VST3/AU host contracts? What
is the correct fix pattern for a JIT-compiling audio plugin that must remain responsive?
Evaluate: (a) a `shared_ptr` control block the worker holds a `weak_ptr` to; (b) an atomic
`shuttingDown` flag plus a latch the destructor blocks on; (c) a persistent worker thread
with a job queue, joined in the destructor. Note that (b) makes plugin destruction block
for the duration of an LLVM compile, which may itself violate host expectations — is that
acceptable, and do real hosts tolerate it?

### Finding C — the benchmark does not measure the production prompt (claimed severity: high)

There are two system-prompt files: one used by the shipping product, one used by every
benchmark and study. They were intended to stay in sync. A `diff` shows they have not:
the **benchmark** prompt additionally contains a 16-line "AVAILABLE STDLIB HIGHLIGHTS"
section listing real Faust stdlib signatures (`fi.resonlp`, `ef.flanger`,
`co.compressor_stereo`, `ba.db2linear`, …), three extra stereo-wiring rules, and a
**different set of few-shot examples**.

A pre-commit-style automated hook exists specifically to keep them in sync. It checks that
one specific sentence is present and identical in both files, and that each file still
contains a line matching `process.*exactly once`. It is doing exactly what its
documentation says — but the team believes it protects "the two prompt files can't drift
apart," and they have in fact drifted substantially.

**Claim:** every benchmark number the team relies on — an 0.88 first-try compile baseline,
a closed architectural verdict, and the entire prompt-efficacy study below — was measured
on a **more grounded prompt than the product ships**, so those numbers do not transfer to
production, and likely *overstate* production robustness against invented function names.

**Verify:** Is that inference correct, and how large would you expect the effect to be?
The team must choose: (i) unify the prompts and strengthen the hook to full-file equality;
(ii) formally fork them as separate artifacts with separate baselines. Which, and why?
Note a real constraint: the product prompt is designated **human-authored product IP** that
AI agents are forbidden from editing, while the benchmark prompt is agent-editable — this
asymmetry is deliberate and is *why* they drifted. Does that change your answer? Separately:
this is an instance of a general failure mode — an automated check that verifies a *proxy*
for an invariant, creating more confidence than the invariant warrants. How should a team
systematically guard against that?

### Finding D — no state persistence (claimed severity: high)

```cpp
void getStateInformation(juce::MemoryBlock&) override {}
void setStateInformation(const void*, int) override {}
```

Both are empty stubs. **Claim:** saving and reopening a DAW project restores 64 default
macro slots with no DSP loaded and no way to recover the generated patch — the Faust source
exists nowhere but in the JIT-compiled factory and the user's memory. The review argues
this is data loss, not a missing feature, and that persisted state must include the Faust
source string and the originating prompt, not merely the parameter values.

**Verify:** severity, and the right state format. Specifically: should the plugin persist
the **Faust source** (deterministic to restore, but reopening the session triggers an LLVM
JIT compile on load — how long is acceptable, and on which thread?), the **original
prompt** (re-generating is non-deterministic and requires network and quota — clearly not
a restore mechanism, but valuable provenance), or **both**? How should version skew be
handled when a future libfaust rejects previously-valid stored source? Is there prior art
in JIT/scripting-based plugins (Reaktor, Max/MSP, Cmajor, ChucK-based hosts, JSFX) worth
copying here?

## 4. The proposal to assess

A systems lead has proposed: *a rating system for each individual user prompt, which then
routes that prompt into different pieces of the system — a different model, or certain
prompt-translation techniques — in order to scale efficacy, efficiency, and success.*

### 4.1 The relevant data

The team ran a **prompt-efficacy study**: 25 audio effects × 5 "knowledge tiers," where
each tier expresses the *same* target effect at a different level of DSP literacy.

- **L4** — DSP engineer: *"A stereo gain utility: single gain parameter in decibels, range
  −60 to +12 dB, default 0 dB, 0.1 dB steps, converted dB-to-linear and applied equally to
  both channels."*
- **L3** — informed producer: *"a simple stereo gain control with a dB slider"*
- **L2** — casual musician: *"a simple volume knob to make my track louder or quieter"*
- **L1** — pure sensory metaphor: *"let me push a sound forward or fade it back until it
  almost disappears"*
- **L0** — artist/cultural reference: *"just the channel volume fader from an SSL mixing
  console"*

**Pilot results** (2 of 5 effect categories, N=50 first attempts, one model, temperature 0,
same system prompt throughout):

| Tier | First-try compile | After retries | Mean attempts | Dominant first-attempt failure class |
|---|---|---|---|---|
| L4 | 90% | 90% | 1.00 | INCOMPLETE (empty output edge case) |
| L3 | 90% | 90% | 1.00 | INCOMPLETE (empty output edge case) |
| L2 | 80% | 90% | 1.10 | + SYNTAX |
| **L1** | **50%** | 80% | 1.50 | **SEMANTIC — 3 of 5 failures** |
| L0 | 60% | 90% | 1.40 | SYNTAX — 3 of 4 failures |

Three observations the review draws from this:

1. Compile rate is **not monotonic** in knowledge level. L1 (pure metaphor) is the floor —
   *harder than L0*, where a concrete cultural reference apparently gives the model
   something to anchor on.
2. **Failure class varies by tier, not just failure rate.** L1 fails semantically
   (circular definitions, signal-graph arity mismatches, duplicate symbol definitions);
   L0 fails syntactically.
3. **Retry rescues SYNTAX well and SEMANTIC poorly** (L0: 60→90%; L1: 50→80% at the
   highest attempt cost of any tier).

**Additional context, all verified:**

- One documented failure — the model inventing a nonexistent stdlib function
  `flanger_mono` for a flanger prompt — reproduced **identically across two months** on
  the same model. Failure modes are stable, not random.
- The full 125-prompt version of this study was attempted and **failed for billing
  reasons before any generation occurred** — zero data. It has not been re-run.
- No cross-model comparison exists on the current prompt and current models. The
  provider-comparison ADR has been "Under evaluation" for three months.
- Every metric in the project measures **whether the code compiles**, not whether it
  *sounds like what was asked for*. The only fidelity signals are a coarse
  expected-substring match and an optional LLM-judge rubric that is **off by default and
  has never been run**.
- **The production path logs nothing.** No record exists of any real user prompt, chosen
  provider, attempt count, error text, or latency. The only corpus is synthetic benchmark
  prompts.

### 4.2 The internal review's position (attack this)

The review argues **the instinct is right but the proposed mechanism is wrong for this
system**, on five grounds:

1. **PluginForge has a verifier; the systems that inspired this proposal do not.**
   RouteLLM, FrugalGPT, NotDiamond, Martian, and OpenRouter's auto-router all exist
   because you cannot tell at inference time whether a cheap model's answer is good
   enough, so you must *predict* it. Their learned scorers are a substitute for ground
   truth. PluginForge has ground truth: the Faust compiler returns in under a second and
   costs nothing. When a verifier is cheap relative to generation, prediction is dominated
   by **generate → verify → escalate**.
2. **There is no data to route on** — one N=50 pilot on a prompt the product does not use,
   no cross-model table, no valid full run.
3. **Free-tier quotas invert the economics.** Off-the-shelf routers optimize cost per unit
   quality; here the scarce resource is *requests against the provider that can do the
   job*. The right object is a **quota-aware scheduler**, not a cost optimizer.
4. **The objective function is wrong**, so a router would optimize the wrong thing. A
   rating system trained against compile rate would learn to favor whatever emits
   `process = _,_;` — a trivially compiling passthrough that does nothing and scores 100%.
5. **It creates a second sensitive prompt surface** (the rewriter's prompt), subject to
   the same human-ownership rules and the same version-coupling problem that Finding C
   shows this team has already lost once.

The review recommends instead, in order: **(①)** an offline *semantic* oracle — render the
compiled DSP against a test signal and assert it is finite, differs from its input, and
that sweeping each parameter changes the output; **(②)** a production generation ledger,
as the prerequisite for any rating system; **(③)** a **model-escalating retry cascade** —
attempts 1–2 on the cheap model with stderr feedback, attempt 3 on a *different model
family*, since the documented failures are stable across two months on one model and more
stderr will not break them (claimed: ~5 lines of code, no classifier, no training data);
**(④)** a **deterministic lexical** prompt classifier (does the prompt contain a known
effect noun, a parameter noun, a unit?) driving an L1/L0 → L3 rewrite, gated behind an A/B
on the existing tiered corpus; **(⑤)** dynamic retrieval of real `stdfaust.lib` signatures
into the generation context to attack the hallucination class at its root — placed in the
*context* rather than the human-owned prompt, deliberately.

It recommends **deferring** the learned rating system until a semantic metric exists, ~10³
labeled real generations have accumulated, and a valid cross-model comparison exists — and
predicts that even then the right answer is a **quota-aware cascade with an empirical
per-class provider ordering** (an auditable lookup table derived from the ledger) rather
than a learned model.

### 4.3 What to answer

1. **Is the verifier argument (4.2 ground 1) correct?** This is the load-bearing claim.
   Is "cheap verifier ⇒ escalation dominates prediction" right in general, and right here
   given that the verifier is *partial* — it proves syntactic validity, not semantic
   fidelity? Does the partiality break the argument, or merely bound it? Under what
   conditions would a difficulty classifier beat a cascade even with a verifier present?
2. **Is deferring the rating system correct, or is it premature pessimism?** Is there a
   cheap version worth building *now* — even just to collect the labels that would justify
   the full version later? Note the ordering trap: you cannot learn a router without
   routing data, and you cannot get routing data without routing.
3. **Grade the substitutes (① – ⑤).** Is the ranking right? Which is over- or
   under-valued? Is the "~5 lines" cascade claim credible, and what does it get wrong —
   e.g. does mixing model families mid-conversation invalidate the accumulated stderr
   context, and how should attempt 3's message be constructed given attempts 1–2 came from
   a different model?
4. **Is the deterministic lexical classifier (④) actually sufficient**, or does separating
   L1 from L2 fundamentally require a semantic model? What would a *good* trigger look
   like? Note the deployment cost asymmetry: an LLM-based classifier adds a network call
   and consumes the same scarce quota the generation needs.
5. **Is the semantic oracle (①) sound?** "Sweeping each parameter changes the output" is
   proposed as the automated test for the Finding-A class of bug. What does that miss, and
   what false positives does it produce (an LFO-driven effect changes its output with no
   parameter input at all; a `dry/wet` at 0 legitimately produces no change)? Propose a
   better property set for automatically validating generated DSP without human listening.
6. **What is missing entirely?** The review looked at correctness, measurement, and
   routing. What would a principal engineer flag that it did not — in security, in
   licensing (JUCE is GPL-or-commercial; the plugin embeds libfaust and LLVM and ships
   generated code of uncertain provenance), in distribution, in failure UX, or in the
   product concept itself?
7. **Steelman the systems lead.** Construct the strongest case *for* building the rating
   system now. If that case is stronger than the review's, say so plainly.

## 5. Output format

Produce:

1. **Verdict table** — one row per finding A–D and per recommendation ① – ⑤:
   `CONFIRMED` / `CONFIRMED, SEVERITY WRONG` / `DISPUTED` / `INSUFFICIENT EVIDENCE`, with
   a one-sentence reason. Be willing to populate every column.
2. **Deep dives** — only where you dispute, would re-rank, or found something new. Show
   the reasoning; a bare assertion is not useful to this team.
3. **Your own sequenced plan** — the ordered list *you* would give this team for the next
   four weeks, with a one-line justification per item. Explicitly note where it diverges
   from the review's ordering and why.
4. **The single highest-risk unexamined assumption** in this architecture — the thing most
   likely to invalidate a large amount of work if it turns out to be false. One paragraph.
5. **Evidence gaps** — what you could not settle from the material provided, and the
   specific artifact (a measurement, a file, an experiment) that would settle each.

## 6. Ground rules

- **Disagree by default.** You are the review board, not a reviewer of the review. Where
  you agree, one line is enough.
- **No hedging that avoids a decision.** "It depends" is only acceptable when followed by
  *what* it depends on and *how the team would find out*.
- **Distinguish what you know from what you infer.** You have no repo access; if a
  conclusion turns on code you have not seen, name the file and what you would need from it.
- **Respect the settled constraints** in §1 — Faust, JUCE, the 64-slot pool, free-only
  providers — unless you have found a first-order technical error in one, in which case
  say so loudly and early.
- **Weight practical impact over elegance.** This is a one-engineer project with a working
  prototype and no shipped users. Advice that assumes a team of ten is not useful.

---

*Addendum, if attaching the source documents:* "Attached are the full internal review, the
prompt-efficacy study, and the decision log referenced above. Treat the prompt above as
authoritative for scope and output format; use the attachments to check the review's
claims against its own evidence, including anything the prompt above summarized or omitted."
