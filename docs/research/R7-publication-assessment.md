# R7 — Publication Assessment: PF-079 (compile success ≠ render-safety)

Status: research note, read-only investigation. No internal code was changed to produce this
document. All external claims below carry a URL and were checked on **2026-10-12**.

## 1. Prior-art verification

### 1.1 arXiv 2409.00856 — "Benchmarking LLM Code Generation for Audio Programming with Visual
Dataflow Languages" (Zhang, Leon, Xu, Cardenas, Wissink, Martin, Srikanth, Dorogi, Valadez, Perez,
Grijalva, Zhang, Santolucito; submitted 2024-09-01)

Sources: [abstract](https://arxiv.org/abs/2409.00856), [HTML full text](https://arxiv.org/html/2409.00856v1).
Accessed 2026-10-12.

**Languages evaluated — visual/node-based only, confirmed.** The paper evaluates exactly two
languages, both explicitly visual/node-based:
- **MaxMSP** — "a visual programming language, with each project represented in an interface of
  nodes and connections."
- **Wavir** — "a visual dataflow language of our own design that complements Web Audio,"
  i.e. an in-house language built for the study, not an existing text-based DSL.

Faust, Cmajor, SuperCollider, or any other text-based audio DSL is not mentioned as an evaluated
language. The claim that this paper covers only visual/node-based languages and not text-based
DSLs like Faust is **true**.

**Correctness method — manual human inspection, confirmed, and explicitly chosen over automated
testing.** Direct quotes from the paper:

> "A key difference in our use of pass@k compared to prior work is that we do not have unit tests
> to measure correctness."

> "For generated code samples of specific benchmarks, each individual evaluator judges whether the
> code and the sound it produces resembles the correct example of the benchmark."

**Audio DSP as hard-to-test — confirmed, stated explicitly as the justification for the manual
method**, not merely implied:

> "Not only are unit tests difficult to implement for audio DSP across languages, where sample
> rates and language construct implementations (e.g. low pass filter) may vary, but unit tests
> are too restrictive for a creative coding task."

> "...it is difficult to formalize a test for 'the sound of waves hitting the ocean'."

**Verdict: the framing given to me is accurate, not an overstatement.** This paper (a) does not
touch text-based DSLs at all, (b) uses listening-based human judgment rather than execution-based
automated testing, and (c) explicitly argues that audio-DSP unit testing is hard — and uses that
argument to justify not attempting it. PF-079's render oracle targets a narrower, more tractable
problem than "does this sound like the reference" (which is why 2409.00856 fell back to humans):
it checks a small set of *objective, reference-free* defect classes (NaN/Inf propagation, DC
offset, runaway gain, silence) that do not require a "correct" audio example to compare against.
That distinction is real and is the basis for a legitimate novelty claim — but it is a narrower
claim than "we solved automated audio-code correctness checking," and the paper draft below is
written to that narrower, defensible scope.

### 1.2 arXiv 2605.14016 — "Case Studies and Reflections on Agentic Software Engineering for Rapid
Development of Digital Music Instruments" (Yee-King; submitted 2026-05-13)

Source: [abstract](https://arxiv.org/abs/2605.14016). Accessed 2026-10-12.

This is an autoethnographic study of using agentic coding tools (prompt-driven AI coding agents)
to build JUCE/C++ plugins — a reimplementation of Laurie Spiegel's "Music Mouse," a port of
Pachet's "Continuator," and a 3D OpenGL tracker-sequencer UI. It is about the *software
engineering process* of using AI agents to build DMI software (developer experience, prompt logs,
longevity/interoperability of the resulting code), not about generating or evaluating DSP
synthesis code itself, and not about audio-level correctness testing. No "compile success ≠
correctness" claim appears in the abstract or the content fetched.

**Verdict: does not compete with or anticipate PF-079.** Different question entirely (agentic dev
workflow quality vs. audio-output correctness of generated DSP).

### 1.3 arXiv 2512.03289 — "DAWZY: A New Addition to AI powered 'Human in the Loop' Music
Co-creation" (submitted 2025-12-02)

Source: [abstract](https://arxiv.org/abs/2512.03289). Accessed 2026-10-12.

DAWZY is a voice/text-controlled assistant that translates natural-language requests into
reversible actions inside REAPER (a DAW) via MCP tools — state queries, parameter adjustment, beat
generation. It is HCI/co-creation work, evaluated by user ratings (Usability, Control, Learning,
Collaboration, Enjoyment), not a DSP-code-generation or audio-correctness-testing paper.

**Verdict: does not compete with or anticipate PF-079.** Different problem (DAW-automation UX),
no code-correctness or render-safety claim of any kind.

### 1.4 Search for a 2026 Faust/Cmajor/JUCE automated-audio-level-evaluation paper

Searched broadly (queries on LLM-generated Faust code benchmarks, automated audio-level
correctness evaluation in 2026, Faust-specific DSP benchmarking). No paper was found that (a)
generates Faust, Cmajor, or comparable text-based real-time-audio DSL code via LLM and (b)
evaluates it by actually rendering/executing the compiled output and checking for objective
defects (NaN/Inf, DC offset, runaway gain, silence), as opposed to either compile-success or
human listening. Adjacent but non-competing work turned up:
- ["Evaluating LLM-Generated Code: A Benchmark and Developer Study"](https://arxiv.org/pdf/2605.09059)
  — general-purpose code-gen evaluation, not audio-specific.
- ["ParaPairAudioBench"](https://arxiv.org/html/2606.24648v1) and
  ["JASTIN"](https://arxiv.org/pdf/2605.04505) — LLM/LALM-as-judge frameworks for audio
  *perceptual/paralinguistic quality*, not for DSP-code defect detection.

**No prior-art paper was found that kills the PF-079 novelty claim.** This should be treated as
provisional — the field is moving fast and a targeted search immediately before actual submission
is warranted — but as of 2026-10-12 nothing competing surfaced.

## 2. Venue survey (dates/deadlines verified 2026-10-12; anything not independently confirmed from
an official page is marked unverified)

| Venue | Next edition | Deadline status | Paper tiers | Topical fit |
|---|---|---|---|---|
| **DAFx 27** | Cremona, Italy, Aug 24–27 2027 (confirmed, [official site](https://dafx27.deib.polimi.it/)) | CFP/deadline **not yet published** as of 2026-10-12 fetch. Pattern from DAFx25 (deadline Mar 31 2025) and DAFx26 (deadline Mar 30 2026, confirmed from [DAFx26 CFP](https://dafx26.mit.edu/call-for-papers)) suggests **late March 2027**, but this is an extrapolation, **not an announced date** — treat as unverified. | **Full papers only**: "full-length paper, 8 pages maximum, for both oral and poster presentations" (DAFx26 CFP). No short/demo track found in any recent DAFx CFP I could locate. | **Best topical fit.** DAFx is specifically about digital audio effects/DSP; the audience already knows Faust and will immediately understand "compiles ≠ renders safely." Cost: full 8-page peer-reviewed paper is the only track — a high bar for a single-author side-project submission, see §4. |
| **NIME 2027** | Paris, France, 22–25 June 2027 (confirmed, [official site](https://www.nime2027.org/)) | Site states "The call for papers and participation will be published in October 2026" — **not yet live** at fetch time. NIME 2026's pattern (titles/abstracts due Feb 5 2026, final submission Feb 12 2026, for a conference held 24–26 June 2026 — [NIME 2026 CFP](https://nime2026.org/call/papers/)) suggests a **~February 2027** deadline, but this is pattern-based, not confirmed for 2027 — unverified. | Tiered by word count: "short" (2000 words, formerly called "demo"), "medium" (4000 words, formerly "short"), "long" (6000 words) — all eligible for oral or poster presentation ([NIME 2026 CFP](https://nime2026.org/call/papers/)). | **Good secondary fit.** The short (2000-word) tier is a genuinely low bar matching a single-author side-project's scope. NIME's core scope is new interfaces for musical expression rather than code-generation safety per se, so the topical match is weaker than DAFx, but NIME has historically been receptive to tool/evaluation papers touching generative and AI-assisted music software. |
| **ICMC 2027** | Taoyuan, Taiwan, Jul 2–8 2027, hosted by Kainan University/Taiwan Computer Music Association, theme "Bridging Tradition and Future" (per search results surfacing [computermusic.org](http://www.computermusic.org/icmc-conference-2026-2/); I could not independently load a primary CFP page with a deadline) | **No deadline found** — unverified. | Historically accepts papers, music, and installations (not independently re-confirmed for 2027). | Moderate fit — ICMC covers computer music broadly including AI/intelligent-music themes, but less DSP-specific than DAFx and deadline is currently unknown. |
| **ISMIR** (LBD) | ISMIR 2026 late-breaking-demo: submission deadline **2026-09-25** ([call](https://ismir2026.ismir.net/call-for-late-breaking-demo)), confirmed — **already past as of today, 2026-10-12.** | Deadline passed. ISMIR 2027 not surveyed (MIR scope is a poor match regardless). | Low-bar LBD poster track exists in principle. | **Weak fit regardless of timing** — ISMIR's scope is music information retrieval, not DSP code generation/safety. Not recommended even once a future deadline opens. |
| **Linux Audio Conference (LAC)** | LAC 2026 already held, Maynooth University, Ireland, 18–20 June 2026 ([lac26.mucs.club](https://lac26.mucs.club/)) — in the past relative to today. LAC 2027 **not yet announced** (no location/date/CFP found). | No open deadline exists right now. | Historically low-bar (short papers/talks common at LAC), though not re-verified for a future edition. | **Strong topical/community fit in principle** — LAC's audience is exactly the Faust/grame-cncm/open-source-audio-dev community that would appreciate the faust-rs issue work and a render-safety oracle — but there is currently nothing to submit to. Watch for the 2027 CFP. |
| **ADC (Audio Developer Conference)** | ADC Bristol 26, Bristol UK, 9–11 Nov 2026 ([audio.dev](https://audio.dev/adc-bristol-26/)) | Talk-submission deadline was **2026-06-29** — confirmed, and **already past** as of today. ADC 2027 not yet announced. | Industry talks, not peer-reviewed papers — no "paper" track at all. | Good *exposure* fit (JUCE/plugin-developer audience would find the JIT-Faust-into-JUCE engineering story compelling) but this is not a publication venue — it's a conference talk, and no submission window is currently open. |

**Bottom line on "best fit + open, verifiable deadline": none.** Every venue surveyed is in one of
two states: (a) the CFP for the next edition has a confirmed conference date but the deadline
itself has not yet been published (DAFx27, NIME 2027, ICMC 2027, LAC 2027 not even scheduled), or
(b) the deadline exists and has already passed (ISMIR 2026 LBD: 2026-09-25; ADC Bristol 26 talks:
2026-06-29). There is currently no venue with both a verified fit and an open, dated submission
window. The two to watch, in order of topical fit: **DAFx27** (expect a CFP with a deadline around
Q1 2027, unconfirmed) and **NIME 2027** (CFP explicitly promised "October 2026," short-paper tier
is the realistic entry point given a single-author project's scope).

## 3. Draft abstract (PF-079), using only the facts given — no invented data

> Large language models are increasingly used to generate Faust digital signal processing (DSP)
> code from natural-language prompts, and in current practice — including in our own system —
> successful compilation (`faust -lang cpp` exiting cleanly) is treated as the correctness signal
> that gates a generated program before it reaches a live audio path. We show this signal
> overstates render-safety by a wide margin. Using an automated render oracle and spectral judge
> that execute compiled Faust programs and inspect the resulting audio for NaN/Inf propagation, DC
> offset, runaway gain, and silence, rather than relying on compilation success or on manual
> listening, we find concrete cases in which code that compiles cleanly is nevertheless unsafe to
> render: an LLM-generated upward-expander effect whose makeup-gain stage has no ceiling, measured
> producing +715 dB of gain, and a brick-wall limiter whose `select2()` branch arms are inverted,
> measured producing 0.95–0.98 DC offset. We situate this against existing LLM-for-audio-code
> benchmarks, which evaluate only visual/node-based dataflow languages and explicitly rely on
> manual human listening for correctness, citing the difficulty of writing unit tests for audio
> DSP. Our oracle instead targets a small set of objective, reference-free render-safety defects.
> We also report a case where we withdrew our own causal claim about an upstream Faust-toolchain
> change after further investigation showed the apparent effect was a diagnostics-visibility
> artifact rather than a genuine fix — offered as a record of how the finding was checked, not as
> an additional result. We argue compile success is an insufficient correctness proxy for
> LLM-generated real-time audio DSP, and that automated render-safety auditing over real,
> compiler-rejected and compiler-accepted LLM output is both necessary and tractable.

(≈245 words.)

## 4. What's missing before this could be submitted

- **No human-perceptual corroboration.** The oracle's defect classes (NaN/Inf, DC offset, runaway
  gain, silence) are objectively measurable, but nothing here shows these correlate with what a
  listener would call "broken." For a reviewer this is the first question: is +715 dB actually
  inaudible/clipped-to-silence in practice, or does it do something perceptually milder? A short
  listening-panel or even single-author structured listening pass over a sample of flagged cases
  would materially strengthen the claim.
- **Single model/prompt family.** The 192-program corpus (per the brief: screened from 202) is
  described as coming from one LLM-generation pipeline. Without cross-model comparison (at least
  one other LLM family generating Faust from the same or similar prompts), the paper can only
  claim "this generator's output, this often" — not a claim about LLM-generated Faust code in
  general. This is the single biggest limitation for a reviewer at a DSP-literate venue like DAFx.
- **No control/baseline rate.** There's no comparison against a baseline incidence of the same
  defect classes in human-written Faust (e.g., from faust-stdlib examples or GitHub Faust repos).
  Without a baseline, "LLM-generated code has this failure mode" is unfalsifiable as an
  LLM-specific claim — it could be a property of the DSP idioms involved (expanders/limiters are
  notoriously easy to get wrong by hand too) rather than of LLM generation per se.
- **The "25–40 percentage point" figure needs a stated method.** As given, PF-079 states a range
  but not how it was computed (over what denominator — all 192? the compiling subset? what counts
  as "render-safe" vs not?) or with what confidence interval. A reviewer will ask for the exact
  computation; this needs to be nailed down and shown, not just asserted, before submission.
- **Oracle precision/recall is uncharacterized.** `render_oracle.py` and `spectral_judge.py` are
  described as automated checks, but nothing here states their own false-positive/false-negative
  rate against known-good and known-bad Faust patches. A tool whose job is "is this correct" needs
  at least a sanity check against a small hand-labeled set to establish its own reliability before
  its output can be used as ground truth for a benchmark claim.
- **No ground-truth "intended behavior" labels beyond the failure taxonomy.** The taxonomy
  (routing_arity, syntax, etc.) classifies *why compilation failed*, not what the program was
  supposed to do. The render-safety finding is about programs that *do* compile — there's no
  parallel labeling of what "correct" output should have looked like for those programs, so claims
  of defect severity rest on the oracle's absolute thresholds (dB ceiling, DC-offset threshold)
  rather than on deviation from an intended reference.
- **The faust-rs causal-withdrawal episode is currently informal.** It lives in a commit message
  ("the repair-loop headline was a visibility effect, not faust-rs") and in filed upstream issues
  (#20/#21/#22), not in a written methodology section. This is worth treating as a genuine asset,
  not a liability: a paper that volunteers "we made this causal claim, found it was wrong on
  further investigation, and corrected the public record" is a strong rigor signal in a field
  currently saturated with overclaiming AI-tool papers, and reviewers who are skeptical of
  AI-dev-tool hype will likely read it favorably. But it needs to be written up explicitly as a
  methodology/limitations subsection with the issue links, not left as an implicit backstory.
- **Reproducibility/licensing statement needed for camera-ready.** The corpus
  (`bench/corpora/repair_corpus_20260830.json`, CC-BY-4.0) and harness
  (`bench/repair_ab_repro/`, `bench/repair_ab_core.py`, MIT) are already open, which is good — but
  the paper needs an explicit "data and evaluation harness are open; the generation engine itself
  is proprietary" statement to preempt a reviewer's reproducibility objection, and to clarify
  exactly which artifacts a reader could rerun versus which they could only inspect.
- **Fit-to-page-budget gap.** At DAFx's 8-page full-paper-only bar, the current material (one
  finding, two diagnosed root causes, a 192-program corpus, no cross-model/control comparison)
  reads more like a focused short paper than a full DAFx paper with a complete related-work and
  evaluation section. NIME's 2000-word short-paper tier is a more realistic match to the current
  state of the evidence; expanding to DAFx-caliber would require at least the cross-model baseline
  and oracle-precision work above.

## 5. Sources (all accessed 2026-10-12)

- https://arxiv.org/abs/2409.00856 and https://arxiv.org/html/2409.00856v1
- https://arxiv.org/abs/2605.14016
- https://arxiv.org/abs/2512.03289
- https://arxiv.org/pdf/2605.09059 (adjacent, non-competing)
- https://arxiv.org/html/2606.24648v1 (adjacent, non-competing)
- https://arxiv.org/pdf/2605.04505 (adjacent, non-competing)
- https://dafx27.deib.polimi.it/
- https://dafx26.mit.edu/call-for-papers (deadline-pattern reference)
- https://www.nime2027.org/
- https://nime2026.org/call/papers/ (deadline-pattern reference)
- http://www.computermusic.org/icmc-conference-2026-2/ (ICMC 2027 date claim, via search; not
  independently loaded as a primary page)
- https://ismir2026.ismir.net/call-for-late-breaking-demo
- https://lac26.mucs.club/
- https://audio.dev/adc-bristol-26/
