## B1 — Producer / Sound Designer

**Method note:** Phase 1 below is built entirely from quotes independently fetched from the
cited URL (forum post, review article, or product review page) — not invented personas.
Where a search engine's own summary was the only thing available (not a direct page fetch),
that is flagged explicitly rather than presented as a verbatim quote. Gearspace is 403-blocked
to this session's fetcher (consistent with `docs/competitive_landscape.md`'s own note on this),
so Gearspace content below is search-summary only, clearly marked.

### What real users actually said about competitors

**Amorph (Artists in DSP) — the closest comparator, and the one with the most real reaction:**

- KVR review, reviewer "Dexter" (US), **July 11, 2026**, v0.99/Windows, 5 stars — the only
  review on KVR's Amorph product page: *"I wrote a couple of vst's in under an hour it was
  amazing."* The same review (per KVR's listing) notes the auto-generated graphics needed more
  effort to customize than the DSP code itself — a UI complaint, not a sound complaint.
  [kvraudio.com/product/amorph-by-artists-in-dsp/reviews](https://www.kvraudio.com/product/amorph-by-artists-in-dsp/reviews)
- weraveyou.com review by Joe Palmer, **March 5, 2026** — the most substantive hands-on
  account found, with real friction specifically on sound quality and iteration cost:
  - *"Without the Pro version of chat bots, the results can be filled with errors, making it
    a much slower 'trial and error' process."*
  - *"Using Amorph with less efficient AI can be a pain, as I found out when using the free
    tier of Gemini; without Pro mode it became a loop of errors"* — i.e. the free-tier-model
    experience (the same tier PluginForge defaults to) was reported as materially worse.
  - *"Everything I made sounded pretty digital, and creating an accurate sampler or
    acoustic-sounding instrument would probably require some very advanced code"* — a direct,
    first-person sound-quality verdict, not a UX complaint.
  - *"While the actual processing can be highly advanced, the interface is static, limiting
    the amount that it can be automated or played with to discover new sounds."*
  [weraveyou.com/2026/03/amorph-review-build-your-own-plugins-with-ai](https://weraveyou.com/2026/03/amorph-review-build-your-own-plugins-with-ai/)

**"Vibe-coded plugins" as a category — the reaction extends past any one tool:**

- KVR Audio, "Anyone else noticed the increase of Vibe coded plugins flooding the market?"
  (Machine Learning and AI for Music Creation forum, thread fetched directly at page 12,
  `t=627485&start=165`):
  - **jinxtigr**, Sun Jul 12 2026, 9:07pm: *"I think talented coders get suckered into going
    on a tear when the AI seems to be 'writing code for them, but imperfectly'"*
  - **Zeisner**, Mon Jul 13 2026, 7:09pm: *"The GUI and the webpage gives it away before the
    sound does. Nothing looks as if any effort...has been put into it"*
  - **Funky40**, Thu Jul 16 2026, 12:19am (quoted verbatim, including the poster's own typos):
    *"played with my fisrt own plugin...works ! soundet like crap ! cause: i forgot to
    specifie that i want to have a tape-delay"* — this is the single most load-bearing quote
    in this research: a real person got a plugin that **compiled successfully and ran** but
    sounded wrong because their prompt under-specified what they wanted, not because the tool
    was broken.
  - **MJACau**, Thu Jul 16 2026, 2:57am: *"I just don't trust the security, unless the LLMs
    are running solely on my machine."*
  [kvraudio.com/forum/viewtopic.php?t=627485&start=165](https://www.kvraudio.com/forum/viewtopic.php?t=627485&start=165)
- Bedroom Producers Blog, "Vibe-coded plugins are giving me SynthEdit déjà vu," **April 23-24,
  2026** (article text fetched directly; no reader comments were present in the fetched
  content):
  - *"Vibe coding hands you a wall of code that whoever 'made' the plugin probably can't read,
    much less debug."*
  - *"A plugin that drops a buffer or mishandles memory crashes a session...AI-generated code
    has a habit of producing things that look right and then fail in ways that are hard to
    track down."*
  - *"'SynthEdit plugin' became shorthand for 'amateur, probably buggy, probably not worth
    installing.'"* — the article explicitly frames vibe-coded plugins as at risk of inheriting
    that same reputation.
  - Author's own stance, which is a useful calibration for how skeptical even a sympathetic
    reviewer is: *"I'm half worried and half curious."*
  [bedroomproducersblog.com/2026/04/23/vibe-coding-synthedit](https://bedroomproducersblog.com/2026/04/23/vibe-coding-synthedit/)
- A second, related KVR thread literally titled "Vibe coded plugins" was identified by search
  (not independently fetched verbatim) where, per the search engine's own summary, the original
  poster says these plugins are "popping up everywhere," that they "don't trust them, and
  won't use them," with replies "mostly agreeing." **This one is search-summary-sourced, not a
  direct-fetch quote — flagged, not presented as verbatim.**

**ConjureDSP — could NOT find real user reaction.** The only content reachable in its KVR beta
thread (`t=629365`, fetched directly) is the original poster "Awdio" (dated April 13, 2026)
soliciting testers — *"So far I'm the only person who has tried it, and it'd be great to get
real world feedback"* — with **zero reply posts** in the fetched thread content. No Gearspace,
Reddit, or review-site hands-on account with sound-quality commentary was found for ConjureDSP
specifically. Do not read anything below about "vibe-coded plugins" generally as evidence about
ConjureDSP in particular — it is evidence about the category, and ConjureDSP has no individual
voice in it from what this search could surface.

**pluginmaker.ai — could NOT find real user reaction.** No Reddit thread, forum post, or
comment-section discussion with first-person sound-quality or workflow commentary was found.
The closest thing located was a review-site advisory, not a user: *"A generated plugin does
not automatically mean good DSP design, and sound quality, CPU use, latency, and stability need
DAW testing"* (cyber-ivy.com tool-check writeup) — this is the reviewer's own caution to
readers, not a reported experience of actually trying it and being disappointed.

**r/edmproduction / r/WeAreTheMusicMakers:** no threads naming Amorph, ConjureDSP, or
pluginmaker.ai were found by search. Reporting this as a gap rather than silently dropping it.

### Top 5 reasons a producer would abandon Incant inside 10 minutes

1. **The README's "polyphonic synthesizers" claim breaks on the first chord.**
   `README.md:34` markets *"Audio Effects & Polyphonic Synths: ... ADSR polyphonic
   synthesizers."* The engine is **monophonic by design** — `FaustEngine.cpp:519-524`
   (`noteOn`) reads a single `voiceFreq`/`voiceGain`/`voiceGate` atomic pointer each, not a
   per-voice array, and `STATUS.md:561` states this plainly: *"Monophonic by design
   (`FaustEngine.cpp:519-524`, deliberate)."* `STATUS.md:542-544` (PF-075, open) documents the
   predictable consequence: a tester played a chord and it *"sounded like ~5 voices from a
   strictly-mono engine"* — almost certainly overlapping release tails and reverb creating an
   illusion of polyphony, not real polyphony. A producer who read "polyphonic synthesizers" on
   the README and plays a chord in the first ten minutes gets voice-stealing/retriggering
   behavior, not the chords they were promised. This is a documented, self-acknowledged
   overclaim, not speculation.

2. **Single-shot generation with no clarification step reproduces the exact failure a real
   user already reported on a competitor.** `PromptPanel.cpp`'s `submitPrompt()` (~line
   656-664) routes New/Add/Redo straight to `queueRequest("generate")` — one LLM call, no
   back-and-forth. "Plan" (`refineSelector` id 4) is the *only* mode with a design-review
   round-trip, and it is opt-in, not the default users land on. This is structurally the same
   shape that produced **Funky40's** real KVR complaint (Jul 16, 2026): *"played with my fisrt
   own plugin...works! soundet like crap! cause: i forgot to specifie that i want to have a
   tape-delay."* Incant's render oracle (per `CLAUDE.md`) checks for NaN/DC/runaway gain —
   it has no mechanism to catch "technically valid, not what I meant." The same failure mode a
   real person hit on a competitor remains fully available here.

3. **No MIDI CC mapping — reaching for a mod wheel finds nothing to map it to.**
   `ParamIdentity.h:43` lists MIDI CC mapping as future work ("(later) MIDI CC mapping"), and
   `STATUS.md:563` confirms it as an open, triaged gap: *"no MIDI CC mapping
   (`PluginProcessor.cpp:288-317`)."* For a tool whose README leads with "polyphonic
   synthesizers," mapping a mod wheel or expression pedal to a parameter is a baseline
   expectation, not an edge case — and it does not exist yet.

4. **The auto-generated UI reads as unfinished before the sound is even judged.**
   `STATUS.md:546` (PF-038, open): *"Knob ordering is Faust's own"* — unsectioned, not
   designed. `docs/competitive_landscape.md` independently documents an Amorph reviewer calling
   the auto-generated graphics *harder* to get right than the audio code, and real-user
   **Zeisner** on KVR (Jul 13, 2026) made the same point about the category generally: *"The
   GUI and the webpage gives it away before the sound does. Nothing looks as if any
   effort...has been put into it."* Incant's param grid is functional, not designed — the
   exact first impression a real forum poster already named as a dealbreaker-adjacent signal
   for this whole category.

5. **The very next move after a first success — refining it — can click.**
   `STATUS.md:538-540` (PF-073, open, needs a captured repro): *"Audible discontinuity on the
   2nd-generation DSP swap in a host... 'Not a very smooth transition.' No `setParamValue`
   flood in the log. A brief recompile gap is expected; a click is not."* A producer's natural
   second action after generating something promising is to use Add/Redo to refine it — and
   the documented, open behavior on that exact path is an audible artifact on the swap. This
   directly undercuts the "RT-safety, won't blow up your monitors" differentiation
   `docs/competitive_landscape.md`'s Moat #3 is built on (contrasted there against Amorph's own
   "use a master limiter when compiling new code" warning) — the competitor's documented risk
   and PluginForge's own open defect are, in a user's ears, the same experience.

### A correction to the brief's premise, found while verifying it

The brief asked me to confirm "the 1-3 second JIT compile wait mentioned in STATUS.md or
docs/sessions/." **I could not find that specific claim, and what I did find contradicts the
"1-3 second JIT" framing specifically** — the JIT compile itself is documented as fast:
`docs/architecture_review_2026-07-21.md:152` describes the compile window as *"tens to hundreds
of milliseconds for LLVM."* The actual user-visible wait is **LLM inference time**, which is
materially longer and far less predictable than "1-3 seconds": `docs/decisions.md:2972` cites
an existing *"host-side cap (180 s hard kill, `llm/generate.py`'s cited host cap)"* on the whole
generate-and-retry round trip, and `bench/CAPTURE_SESSION_CHECKLIST.md:21` separately notes an
optional capture mode that *"adds a few seconds to every generation."* Net: the real friction
point is not a snappy sub-3-second JIT wait — it's an LLM-call-bound wait that can legitimately
run into the tens of seconds (up to a 180s worst case across up to 3 retries), which is a larger
first-ten-minutes risk than the brief's framing implied, not a smaller one.

---

## B3 — Educator & Research-User

**Verdict up front: real but narrow wedge, and the nearest adjacent research (general CS-ed
studies of LLM-explained compiler errors) is a warning sign, not encouragement — effect sizes
there are weak. This is worth a near-zero-cost probe, not a curriculum investment.**

### 1. Is Faust's pedagogical landscape actually under-served?

No — Faust is already a first-class citizen of audio-programming education, and the gap is
narrower than "nobody teaches this."

- Stanford CCRMA runs Faust/JUCE instruction directly: **Music 320C** ("Advanced Topics in
  Signal Processing: Developing Audio Plugins") uses Faust alongside C++/JUCE for plugin
  projects ([ccrma.stanford.edu/course-overviews/music-320c](https://ccrma.stanford.edu/course-overviews/music-320c),
  accessed 2026-10-14), and CCRMA runs a dedicated recurring summer workshop, **"Audio Plugins
  with Faust and JUCE"** ([ccrma.stanford.edu/workshops/faust-juce-21/](https://ccrma.stanford.edu/workshops/faust-juce-21/),
  accessed 2026-10-14).
- GRAME co-produced a free online course with Stanford: **"Real-Time Audio Signal Processing
  in Faust"** (Yann Orlarey and Romain Michon, 2018), hosted on Kadenze and mirrored via
  Stanford Online ([online.stanford.edu/courses/sohs-ymusic0006-real-time-audio-signal-processing-faust](https://online.stanford.edu/courses/sohs-ymusic0006-real-time-audio-signal-processing-faust),
  [kadenze.com/courses/real-time-audio-signal-processing-in-faust/info](https://www.kadenze.com/courses/real-time-audio-signal-processing-in-faust/info),
  both accessed 2026-10-14).
- Faust teaching extends beyond Stanford/GRAME: **TU Berlin's Audio Communication Group**
  teaches a "Building Instruments in Faust" sound-synthesis class (Henrik von Coler), and
  **Université Paris 8** runs a 24-hour required Faust introduction inside its "Programming
  Languages in Computer Music 1" course for 3rd-year undergrads (found via web search,
  accessed 2026-10-14 — unable to independently verify against each university's own primary
  syllabus page, so treat the TU Berlin/Paris 8 specifics as Tier-3/secondary-sourced, not
  confirmed primary).
- Tooling for classroom use already exists and is already designed for exactly this context:
  the **Faust IDE** (faustide.grame.fr, `github.com/grame-cncm/faustide`) runs entirely
  client-side in WebAssembly specifically so it works "in situations with many simultaneous
  users such as workshops and classrooms" (GitHub README, accessed 2026-10-14). **Faust
  Playground** is a separate, simplified graphical front end built explicitly "to enable
  children to learn basic audio programming" and has been used in secondary schools (search
  summary of GRAME materials, accessed 2026-10-14).
- The one admission of real difficulty comes from GRAME's own side: coverage of Faust
  pedagogy states plainly that "teaching Faust to musicology students is described as a
  difficult task" and separately that Faust's error messages are "long and not very
  meaningful," with "expressions merged and simplified before errors are triggered, making it
  difficult to correlate the error output with source code" (search summaries citing
  Faust community/HN discussion and GRAME's own debugging docs, accessed 2026-10-14). This is
  the real crack: **Faust teaching exists and is mature, but Faust's own error messages are a
  widely acknowledged weak point** — independent of any LLM being involved at all. Confirmed
  directly: GRAME's own reference, **`faustdoc.grame.fr/manual/errors/`**, is a static,
  by-category technical reference (syntax errors, box-connection/arity errors, pattern-matching
  errors, signal errors, etc. — fetched directly, accessed 2026-10-14) with worked examples,
  but it is generic and not personalized to *your* mistake, and it says nothing about
  LLM-generated code anywhere on the page (confirmed by direct fetch).
- Direct evidence this friction is live and unresolved today, independent of any LLM: forum
  threads like "Faust error (DSP)" and "Extremely frustrated. Simple FX Plugin, constant
  failure" on forum.hise.audio (found via search, accessed 2026-10-14) show working musicians
  hitting exactly this wall — cryptic Faust compiler output they can't map back to their code.

**So: Faust pedagogy itself is well-served at the curriculum level (CCRMA, GRAME/Kadenze, TU
Berlin, Paris 8, FaustIDE, FaustPlayground). The gap is narrower and more specific — Faust's
own error output is acknowledged as hard to read, full stop, LLM or not.** Incant's
`error_classes.py` taxonomy is aimed exactly at that specific, real, narrow gap — not at a
missing Faust curriculum.

### 2. Has anyone built an "explainable LLM-DSP-codegen" pedagogical layer before?

**Not Faust-specific, and nothing found that targets a classroom/learning use case the way
this task frames it** — but the general pattern ("take a raw compiler error, have an LLM turn
it into a plain-English explanation for a learner") is an active, multi-year CS-education
research line outside of audio, and the results there are a genuine caution:

- **`dcc --help`** (UNSW) wires an LLM into the DCC teaching compiler to generate
  novice-focused C error explanations from source + error + stack frame. It was deployed at
  scale: **2,565 students, 64,000+ uses over 10 weeks** in CS1/CS2
  ([arxiv.org/abs/2308.11873](https://arxiv.org/abs/2308.11873), accessed 2026-10-14).
  Explanations were conceptually accurate in 90% of compile-time cases but **often broke the
  "don't give the answer" instruction and gave code anyway** (same source).
- **"Not the Silver Bullet: LLM-enhanced Programming Error Messages are Ineffective in
  Practice"** ([arxiv.org/abs/2409.18661](https://arxiv.org/abs/2409.18661), accessed
  2026-10-14): in a study with **106 students across 6 tasks**, GPT-4-generated error
  explanations beat standard compiler error messages in only **1 of 6 tasks**, and
  **expert-handwritten explanations still outperformed both** the LLM explanations and the raw
  compiler output. This is the single most important counter-finding for this wedge: the
  closest available empirical analog to "explain the compiler error in plain English" has a
  **weak, mostly-null effect size**, not a slam dunk.
- A pilot study from the Raspberry Pi Foundation's Computing Education Research Centre on
  teachers' views of LLM-explained error messages similarly frames this as "a useful
  supplement, not a replacement for instructor support" — teachers, not just students, are the
  audience that needs convincing (search summary, accessed 2026-10-14, not independently
  fetched from primary source).
- Within Faust/DSP specifically: GRAME's **own official page on using LLMs with Faust**,
  `faustdoc.grame.fr/manual/llm/` (fetched directly, accessed 2026-10-14), already names almost
  the exact failure taxonomy Incant encodes — verbatim: *"LLMs often produce plausible-looking
  Faust code that still needs correction. Common issues include: missing
  `import("stdfaust.lib");`; invented library names or wrong function arity..."* — i.e. GRAME
  has independently identified hallucinated-symbol and arity-mismatch as the characteristic
  LLM-on-Faust failure modes, the same two classes `error_classes.py`'s `HALLUCINATED_SYMBOL`
  and `ROUTING_ARITY` encode. The difference: GRAME's version is **developer-facing prose
  advice** ("always run the compiler, inspect the SVG diagram") for someone prompting an LLM by
  hand — not an automated classifier wired into a live retry loop with a plain-English
  explanation surfaced to an end user inside a product. That same GRAME page documents **nine
  production systems** already combining LLMs with Faust, including **Chaos Audio's "AI FX
  Builder"** (a generally-available feature building Faust effects from NL prompts, confirmed
  live via [chaosaudio.com](https://chaosaudio.com/blogs/whats-new/introducing-ai-fx-builder-create-any-effect-you-can-imagine),
  accessed 2026-10-14 — **not previously tracked in `docs/competitive_landscape.md`, flagged
  here for that doc's maintainers to triage separately**), plus **pluginmaker.ai** (already in
  the landscape doc) and "Radium V8.0" (unverified, not independently checked). None of the
  coverage found for any of these nine systems describes an end-user-facing, named,
  plain-English error-class explanation UI — they are generation tools, not teaching tools, as
  the landscape doc's "black box" framing already asserts.

**Conclusion on novelty: the concept of classifying and explaining LLM-on-DSL failure modes is
not novel in the abstract (GRAME names the same two failure classes in prose; general CS-ed
research has built and deployed this pattern at scale for C). What is still apparently unbuilt
is the specific combination — automated classification wired to an existing retry loop,
surfaced as a live, in-product, plain-English "why your generated DSP broke" explanation, in
Faust, inside a plugin host.** That is a real but thin novelty claim, and the general-CS-ed
evidence above says the pedagogical payoff of the "plain English error explanation" move itself
is uncertain even when well-executed.

### 3. Is NIME (or an adjacent community) asking for this?

No direct hits for "AI-assisted but explainable DSP education" as a named topic. Adjacent but
distinct work exists:

- NIME 2026 has at least one LLM-for-accessible-creation paper (Michael Clemens, "Accessible
  Musical ALife Through LLM Co-Creation") that explicitly found *"structured prompt engineering
  is necessary for reliable synthesis, while extensive documentation alone does not help"*
  ([nime.org/proceedings/2026/nime2026_170.pdf](https://nime.org/proceedings/2026/nime2026_170.pdf),
  accessed 2026-10-14) — relevant context (documentation alone doesn't close the gap a
  structured tool could), but about musical ALife, not Faust/DSP error pedagogy.
  specifically.
- A separate thread exists on **explainable AI (XAI) for NIME**, but it targets making opaque
  *neural audio models* legible to artists via "material agency," not explaining *compiler
  errors in generated DSL code* to learners — a different problem wearing a similar name
  (search summary, accessed 2026-10-14, not independently fetched).
- LLM "teachable agents" for music education exist in the literature (e.g. music-theory
  teachable agents showing improved knowledge construction), but target music theory
  pedagogy, not DSP/code pedagogy.

**No one is currently publishing on, or visibly asking for, "explainable AI-assisted DSP
coding education" as a named research gap.** That cuts both ways: open territory, but also no
evidence of an already-organized, waiting audience the way there would be if NIME had a
standing workshop or working group on it.

### 4. Who would actually adopt this — specifically

Ranked by plausibility, each tied to something found above rather than a generic persona:

1. **Most plausible: self-taught Faust users already hitting this exact wall on forums.**
   The forum.hise.audio threads found above ("Faust error (DSP)", "Extremely frustrated...")
   are not hypothetical personas — they are people, today, stuck on cryptic Faust compiler
   output with no plain-English bridge back to their own code. This population doesn't need to
   be recruited or imagined; it's visible in public forum search results right now. They are
   not necessarily generating via LLM (many write Faust by hand in HISE), but Incant's
   explain-the-error surface would land on the identical frustration regardless of whether the
   broken code came from a human or an LLM.
2. **Plausible, narrower: a CCRMA/TU-Berlin-style instructor running a Faust+JUCE plugin
   workshop**, specifically one that already has students writing/compiling real Faust (CCRMA
   320C, the CCRMA Faust+JUCE summer workshop, TU Berlin's "Building Instruments in Faust").
   These instructors already have FaustIDE and GRAME's manual as classroom tools; the pitch to
   them is narrow — Incant's error panel could be a scaffolded on-ramp exercise ("prompt an
   effect, watch it fail a specific named way, read why, fix it") rather than a curriculum
   replacement. This is a tool pitch to one or two specific existing courses, not a market.
3. **Least plausible as framed: "self-taught programmers learning DSP in general."** Incant's
   taxonomy (`ROUTING_ARITY`, `DELAY_RANGE`, `RECURSION_CYCLE`) is Faust's own composition
   algebra, not general DSP theory. It teaches "how Faust's `<:`/`:>`/`~` operators work," which
   is valuable only to someone already committed to learning Faust specifically, not a generic
   "I want to understand DSP" audience who might just as well end up in C/Python/SuperCollider.

### 5. Minimum build to test the wedge

**Nearly free, and most of the content already exists.** `llm/error_classes.py`'s
`RETRY_HINT` dict (lines 155–179) already contains hand-written, plain-English, pedagogically
framed explanations for `ROUTING_ARITY` and `DELAY_RANGE` — e.g. the `ROUTING_ARITY` hint
explicitly teaches the rule *"A stdlib effect takes ONLY its control arguments — the audio
arrives by composition"* with a worked right/wrong example. Today this text is **sent only to
the LLM** as retry feedback; it is never shown to the human. The minimum probe is:

- Add a UI surface (one of `host/Source/*Panel.*`) that, on a classified compile failure, shows
  the human the same `classify_error()` result and `RETRY_HINT`/`FIX_HINT` text already
  computed server-side — reframed as "what went wrong" rather than "what to tell the model."
  This requires **zero new taxonomy work, zero new LLM calls, zero curriculum design** — it is
  wiring an existing string to an existing error-display path.
- Then show it to the two populations identified in §4 (an active Faust forum poster, and/or
  one CCRMA/TU-Berlin-style instructor) and ask directly: did this explanation teach you
  something the raw Faust compiler error didn't?

**Do not build** a lesson-plan/curriculum/assessment layer, worked-exercise sets, or outreach
to institutions until that probe returns a clear "yes." That is the expensive, hard-to-reverse
investment this section is explicitly not recommending yet.

### Kill criterion (explicit, falsifiable)

**If, when shown Incant's classified plain-English explanation for a real Faust compile
failure, a sample of real users (forum posters and/or an instructor from §4) cannot state in
their own words why the code failed any better than when shown only the raw Faust compiler
stderr (the same null-result pattern "Not the Silver Bullet," arXiv:2409.18661, found at scale
for C error messages) — then the "education/research wedge" is dead.** The error-explanation
feature may still ship as a plain user-experience nicety (it reduces frustration during
iteration regardless), but stop investing in the pedagogy framing, stop pursuing instructor
outreach, and do not build the lesson-plan/curriculum layer.

### Sources (all accessed 2026-10-14 unless noted)

- CCRMA Music 320C — https://ccrma.stanford.edu/course-overviews/music-320c
- CCRMA Faust+JUCE workshop — https://ccrma.stanford.edu/workshops/faust-juce-21/
- Stanford Online Faust course — https://online.stanford.edu/courses/sohs-ymusic0006-real-time-audio-signal-processing-faust
- Kadenze Faust course — https://www.kadenze.com/courses/real-time-audio-signal-processing-in-faust/info
- Faust IDE GitHub README — https://github.com/grame-cncm/faustide
- GRAME Faust error-message reference (fetched directly) — https://faustdoc.grame.fr/manual/errors/
- GRAME Faust LLM guidance (fetched directly) — https://faustdoc.grame.fr/manual/llm/
- Chaos Audio AI FX Builder — https://chaosaudio.com/blogs/whats-new/introducing-ai-fx-builder-create-any-effect-you-can-imagine
- `dcc --help` (UNSW) — https://arxiv.org/abs/2308.11873
- "Not the Silver Bullet: LLM-enhanced Programming Error Messages are Ineffective in Practice" — https://arxiv.org/abs/2409.18661
- NIME 2026, Clemens, "Accessible Musical ALife Through LLM Co-Creation" — https://nime.org/proceedings/2026/nime2026_170.pdf
- forum.hise.audio Faust-error threads (existence of real user frustration, not independently archived) — https://forum.hise.audio/topic/10743/faust-error-dsp
- TU Berlin / Université Paris 8 Faust teaching mentions: found via search summary only, **not independently verified against each institution's own primary syllabus page — treat as secondary-sourced, unconfirmed.**
- Raspberry Pi Foundation teacher-views pilot: found via search summary only, **not independently fetched from primary source.**

---

## B2 — Linux & Open-Source Audio

**Scope note:** read-only research draft. Does not touch `R9-audience-findings.md` (other
agents' file) — a human merges this in separately. All web claims below carry an access date;
anything I could not independently verify is marked **UNVERIFIED**. "Today" for this draft is
2026-10-14, per the task brief.

---

### 1. CLAP adoption reality

**Hard numbers are stale.** The only concrete CLAP ecosystem count I could find — "15 hosts, 93
developers, ~394 CLAP plugins" — traces to Libre Arts' "CLAP API two years later" retrospective
(https://librearts.org/2024/11/clap-api-two-years-later/, accessed 2026-10-14), which is dated
**November 2024**, i.e. ~2 years stale relative to today. A couple of search syntheses restated
this as "as of October 2026" without a new source behind it — I'm flagging that restatement as
**UNVERIFIED**, not treating it as a fresh count. `clapdb.tech` (https://clapdb.tech, accessed
2026-10-14) is the canonical live tracker and almost certainly has a current total, but my fetch
only surfaced its "recent updates" feed, not the aggregate count — so treat "~400 CLAP plugins"
as a **2024 floor**, not a 2026 figure.

**Per-DAW status, independently checked:**
- **Bitwig Studio** — co-created CLAP with u-he; full, first-class CLAP support; runs natively
  on Linux/macOS/Windows (Wikipedia "Bitwig Studio," accessed 2026-10-14; spectral-colors.com
  "CLAP vs VST3 (2026)," accessed 2026-10-14). Bitwig is the single DAW where shipping CLAP buys
  the most credibility, because CLAP is *their* format.
- **REAPER** — full CLAP support since v7, confirmed cross-platform including Linux (KVR news on
  v6.71 adding CLAP, https://www.kvraudio.com/news/cockos-updates-reaper-to-v6-71---clap-plugin-support-56611,
  accessed 2026-10-14; reapertips.com v6.71 writeup, accessed 2026-10-14). REAPER also fully
  supports LV2 on Linux.
- **Ardour** — does **not** support CLAP, and there's an explicit, quotable reason from the
  project lead: on the Ardour discourse thread "New plugin standard"
  (https://discourse.ardour.org/t/new-plugin-standard/107337, accessed 2026-10-14), Paul Davis is
  reported as saying **"Since we don't do modulation, CLAP offers nothing in an Ardour context
  that is not offered by LV2."** Ardour would accept a well-implemented CLAP PR but isn't
  prioritizing it. Ardour added VST3 support in 6.5 (ardour.org/news/6.5.html, accessed
  2026-10-14) and treats **LV2 as its first-class native format** — LV2 predates CLAP by over a
  decade and is the format most tied to Ardour/JACK-era Linux audio culture specifically.

**So: CLAP vs. LV2 "more Linux-native-expected" is not a clean answer — it's audience-split.**
Bitwig and REAPER (both popular with serious Linux musicians, see §6) are CLAP-forward. Ardour —
arguably the most *culturally* Linux-native FOSS DAW, closest to the KXStudio/LAD lineage in §4 —
has no CLAP support and treats LV2 as the native format CLAP is redundant with. A Linux-first
tool optimizing for the broadest *serious-Linux-user* reach should read this as "ship CLAP for
Bitwig/REAPER reach, but don't expect it to register at all with the Ardour/LV2-tradition crowd."

**JUCE/CLAP feasibility for this specific codebase:** `host/CMakeLists.txt` targets JUCE 7.0.9
(per CLAUDE.md). Official JUCE-native CLAP support is JUCE 9-only (JUCE forum FR thread,
https://forum.juce.com/t/fr-support-clap-for-plugins-host-client/51860, accessed 2026-10-14). But
**`clap-juce-extensions`** (https://github.com/free-audio/clap-juce-extensions, MIT, accessed
2026-10-14) is an unofficial bolt-on that works with JUCE 6/7/8 and is already production-proven
— it's what Surge, several ChowDSP plugins, B-Step, Monique, and Dexed ship CLAP builds with, and
it's also literally what Audio Plugin Coder's beta CLAP support (per `competitive_landscape.md`)
uses. **This means CLAP support does not require a JUCE 9 upgrade** — it's a medium-effort,
well-trodden dependency add on the current JUCE 7 target, not an architecture change.

---

### 2. PipeWire / JACK — confirmed non-issue, do not chase

PipeWire and JACK are **audio-server/transport layers**: they route audio and MIDI between
applications (what connects to what, buffer/sample-rate negotiation). PipeWire ships a
JACK-API-compatible implementation, so JACK clients work unmodified under PipeWire
(pipewire.org, ArchWiki "PipeWire," both accessed 2026-10-14). **Neither has any bearing on
plugin binary format** (VST3/CLAP/LV2) — a plugin loads inside a *host* (Bitwig, REAPER, Ardour,
or Incant's own Standalone target), and the host talks to PipeWire/JACK for I/O underneath that,
entirely independent of which plugin format the host loaded. Confirmed by cross-referencing the
PipeWire project docs against JUCE/Carla plugin-hosting docs — no interaction surface exists
between "which plugin API a DAW supports" and "which audio server moves the samples."

**For the final report: PipeWire/JACK is correctly out of scope for a plugin-format or
packaging decision.** It would only matter if Incant's own Standalone JUCE target had audio-I/O
complaints on Linux (a real but separate concern, outside this brief), not for VST3/CLAP/LV2
positioning. Flag it as addressed-and-dismissed so nobody re-opens it as a format question later.

---

### 3. Distro packaging norms — effort vs. reach

**AUR (Arch User Repository) — lowest effort, and already the dev's home turf.**
Single-maintainer VST3 plugins are a completely normal AUR pattern — found live examples:
`zl-splitter-vst3`, `maim-vst3`, `neuralnote-vst3`, `fire-vst3-bin`, `formula-vst3`, `aeolus-vst3-bin`
(aur.archlinux.org, accessed 2026-10-14), each maintained by one person, ranging from
source-built to binary (`-bin`) packages. A `-bin` PKGBUILD for a prebuilt VST3 `.so`/bundle is
genuinely a single file, no sandboxing, no symbol-conflict concerns — it just copies files to
`~/.vst3/`. **This is the dev's own distro** (Arch/Omarchy per CLAUDE.md), so this is "package
what you already run," not new infrastructure.

**Flatpak/Flathub — real friction specifically for a single plugin, not for a whole app.**
Flathub *does* have plugin-format infrastructure
(`org.freedesktop.LinuxAudio.BaseExtension`/`...Plugins`, github.com/flathub, accessed
2026-10-14) and a years-long discourse thread working through it
(https://discourse.flathub.org/t/proposal-for-audio-plugins/399, accessed 2026-10-14, thread
dates from April–July 2020). But the documented obstacles are real and plugin-specific: "plugins
must be self-contained (statically linked)" because GTK2/3/4 and Qt4/5 can't coexist in one
sandboxed process space (same thread, 2020-05-03 post). A companion community writeup
("Proprietary Audio Plugins for Linux, but the right way,"
https://gist.github.com/abique/4c1b9b40f3413f0df1591d2a7c760db4, accessed 2026-10-14) proposes
running each plugin out-of-process over IPC specifically to dodge this — i.e., the ecosystem's
own experts treat single-plugin Flatpak distribution as a hard-enough problem to warrant a
bespoke IPC architecture, not a solved one. **Flathub packaging in practice is for whole
host apps that themselves load plugins** (Ardour: `github.com/flathub/org.ardour.Ardour`;
Zrythm: `github.com/flathub/org.zrythm.Zrythm`, both accessed 2026-10-14) — not for shipping one
standalone VST3/CLAP binary. Telling data point: **even MAGDA**, a project built Linux-first from
day one, only ships a **sideloadable `.flatpak` bundle with "Flathub submission pending"**
(search synthesis of MAGDA's own distribution docs, accessed 2026-10-14) — no completed Flathub
listing, and I found **no AUR package for MAGDA either**. If the best-positioned comparable
project hasn't finished a Flathub submission, that's corroborating evidence Flatpak is not the
cheap path for a tool in this category.

**Nix/nixpkgs — moderate effort, smaller reach, best values-fit.** The official NixOS wiki
documents a plain convention for this exact case — install plugin files under
`$out/lib/vst3`, `$out/lib/clap`, etc., as an ordinary `stdenv.mkDerivation`
(wiki.nixos.org/wiki/Audio_production, accessed 2026-10-14). Community repos
(`9Prestidigitator/maxpkgs`, `jee-mj/foss-plugins` on GitHub, accessed 2026-10-14) exist
specifically for audio plugins not yet in mainline `nixpkgs`, including a pattern of symlinking
built plugins into each user's `~/.vst3`/`~/.clap`. Reach is far smaller than AUR (NixOS/nix-darwin
users are a minority even within Linux audio), but the audience that *is* there strongly
self-selects for reproducible-build, source-available tooling — a strong values match for
Incant's actual story (open pipeline, local JIT), just not a volume play.

**Ranking (lowest effort → highest):** AUR (`-bin` PKGBUILD) < Nix (derivation + NixOS wiki
convention) < Flathub (plugin-specific sandboxing problems the ecosystem itself hasn't fully
solved, and the closest comparable project hasn't finished it either).

---

### 4. Linux audio community norms — what it rejects

**Documented, not assumed, evidence of a real preference against closed/telemetry/cloud tooling:**

- **LSP (Linux Studio Plugins) project** — LGPLv3, states its reason for existing as filling
  "the lack of good and useful plugins under the GNU/Linux platform," funded by
  donations/bounty-subscriptions rather than license keys or paid enterprise support
  (lsp-plug.in, sourceforge.net/projects/lsp-plugins, both accessed 2026-10-14). The explicit
  absence of license-key gating is itself a norm statement in this space — "openly licensed,
  donation-funded" is the default shape a serious Linux-audio plugin project is expected to take.
- **KXStudio's own "and Free Software" manual page**
  (https://kx.studio/Documentation:Manual:kxstudio_and_free_software, accessed 2026-10-14) states
  "all of the custom KXStudio tools are FLOSS, as is the vast majority of other included
  software," and explicitly **segregates the few non-free inclusions (e.g. REAPER) into a
  separate, user-removable repository** rather than bundling them by default. That's a project
  institutionalizing "free by default, proprietary opt-in and clearly marked" as policy, not
  incidental.
- **Audacity telemetry backlash (2021)** is the sharpest real-world data point: when Muse Group
  added optional telemetry (reporting to Google/Yandex), the reaction produced **50+ community
  forks**, the most notable explicitly named and framed as a statement of principle —
  "**Audiocity**: an Audacity fork without any non-free software or telemetry, as all GPL
  software should [have]**" (search synthesis citing the Audiocity project description, accessed
  2026-10-14). This shows the backlash threshold is low even for *opt-in* telemetry from an
  otherwise-beloved open project.
- **Even Ardour treats "phone home" as something to justify and let users kill.** Ardour's own
  FAQ (https://ardour.org/faq.html, accessed 2026-10-14) documents that release builds check
  ardour.org at startup for update notices, and that this is a **compile-time-optional feature
  specifically so people who object to it can build without it.** A mainstream, trusted FOSS DAW
  still felt obligated to document and offer an opt-out for a mild phone-home check — that is the
  community's baseline expectation, not an edge case.

**Implication for Incant specifically (not asked to fix, just mapped):** Incant's generation path
calls out to an LLM provider over the network (free-tier Gemini/Groq/OpenRouter, or local Ollama,
or paid Anthropic — per `llm/providers.py` and CLAUDE.md). That network dependency for the core
generate action is the kind of thing this audience audits for and reacts to if undisclosed. The
mitigating fact already true in the repo: **a fully local/offline path already exists (Ollama
provider)** — making that visible and first-class in any Linux-audience-facing messaging is a
*messaging* fix, not a build fix, and costs nothing new to build.

---

### 5. MAGDA as a Linux-audio-community adoption case study

**Could not find a dedicated r/linuxaudio thread or a Linux Audio Developers (LAD)/Linux Audio
Announce (LAA) mailing-list discussion of MAGDA** despite several targeted searches (`MAGDA DAW
reddit linuxaudio`, `site:reddit.com r/linuxaudio MAGDA`, `MAGDA Linux Audio Developers mailing
list`, all accessed 2026-10-14) — this is a genuine gap, not a negative finding; it may simply not
be indexed, or the discussion may be happening in venues search doesn't surface well (Discord,
Matrix). **Marking this UNVERIFIED / not found, explicitly, rather than inferring reception from
absence.**

**What I did find, from the KVR Audio forum thread for MAGDA**
(https://www.kvraudio.com/forum/viewtopic.php?t=628692, accessed 2026-10-14): reception reads as
**cautiously positive, low-volume**. Representative comments: a user reports a bug (missing
Windows settings menu) while adding "this looks pretty interesting"; another: "used this for
about 10 minutes and i kinda like it." The thread documents rapid iteration (v0.1.0 → v0.4.0 across
AI-chat, local-offline-llama.cpp support, multi-provider AI, DSL console) with the maintainer
(`ConceptualMachines`) actively responding. No hostility, no "vibe-coding slop" pushback of the
kind `competitive_landscape.md`'s Hügelton section documents elsewhere in this market — but also
no evidence of the kind of groundswell that would indicate deep Linux-audio-community penetration
yet.

**Distro packaging reality check on MAGDA itself, relevant to §3 above:** search results describe
MAGDA's Linux distribution as a **sideloadable `.flatpak` bundle with Flathub submission still
pending** (accessed 2026-10-14) — no completed Flathub listing found, and no AUR package found
either. For a project this explicitly Linux-committed, that's a useful real-world data point:
**packaging lag is the norm even for well-positioned, actively-developed tools in this space**,
not a sign Incant is unusually behind.

---

### 6. Bitwig and REAPER on Linux — what "well-supported" actually requires

Both run natively on Linux and are genuinely popular with serious Linux musicians (as opposed to
Ableton/Logic/Pro Tools, none of which run natively on Linux at all).

- **Discovery mechanism for both is the same and is not a gatekept listing.** Bitwig and REAPER
  both scan standard plugin install directories (e.g. `~/.vst3`, `~/.clap`) at startup; there is
  **no submission/approval process to be "listed"** inside either DAW — if the binary is a valid
  VST3 or CLAP build sitting in the right directory, it shows up. I found no "verified plugins"
  gate for either DAW (searched directly, accessed 2026-10-14) — the previously-reported
  assumption of a formal listing process is **not substantiated**; discoverability is really about
  (a) shipping formats the DAW scans for and (b) being findable *outside* the DAW (KVR, Bedroom
  Producers Blog, rekkerd.org — the same outlets `competitive_landscape.md` already tracks for
  this whole competitive category) so a Bitwig/REAPER-on-Linux user hears about it at all.
- **Bitwig specifically rewards CLAP** — it's the format Bitwig co-created and leads on
  (Wikipedia/spectral-colors.com, accessed 2026-10-14). Shipping only VST3 (current state) means
  Incant is invisible to the part of the Bitwig community that filters by "CLAP-native" as a
  quality signal, even though VST3 itself loads fine in Bitwig.
- **REAPER already fully covers both VST3 and CLAP on Linux**, so Incant's current VST3-only
  output already works there technically — the REAPER-specific gap is purely discoverability
  (Cockos forums, REAPER's own plugin-recommendation threads), not a format gap.

**Net:** "well-supported on Bitwig and REAPER" = ship a CLAP build (medium effort, §1) + show up
in the handful of press/forum outlets this market already funnels through. There is no
DAW-specific certification step to budget for.

---

### Adoption checklist — ranked by effort-to-credibility ratio

| # | Action | Effort | Credibility gain | Ratio |
|---|---|---|---|---|
| 1 | **AUR `-bin` package** (PKGBUILD wrapping the built VST3, installs to `~/.vst3`) | **Low** (single file, no sandboxing, dev's own distro) | **Medium** (Arch-using Linux-audio overlap is real but is one distro's users) | **Best ratio in this set** |
| 2 | **Disclose/foreground the existing local-LLM (Ollama) path** for this audience specifically | **Low** (code already exists, per `llm/providers.py`; this is a messaging/README change) | **Medium-high** (directly answers the §4 telemetry/cloud-skepticism norm with something already true) | **Second-best ratio** |
| 3 | **Ship a CLAP build via `clap-juce-extensions`** | **Medium** (proven JUCE-7-compatible dependency, not an architecture change — real integration work, not research) | **High** (closes the gap with Bitwig and the AI-DSP-competitor set — APC already has it per `competitive_landscape.md`) | Good ratio, but real engineering hours |
| 4 | **Nix packaging (nixpkgs-style derivation)** | **Medium** (clear convention exists; still new infra to learn/maintain) | **Low-medium** (small but values-aligned audience) | Weaker ratio |
| 5 | **Flathub listing for the plugin itself** | **High** (the ecosystem's own unresolved sandboxing/static-linking problem, §3; even MAGDA hasn't finished this) | **Low for a single plugin** (Flathub plugin distribution is really an app-hosting-plugins pattern) | **Worst ratio — likely not worth doing for a bare plugin** |
| 6 | **LV2 build for the Ardour/KXStudio-lineage crowd** | **High** (separate wrapper/architecture from VST3/CLAP; Ardour itself says CLAP buys nothing LV2 doesn't) | **Low-medium** (reaches a real but smaller, more purist-FOSS segment than Bitwig/REAPER) | Weak ratio unless specifically chasing the Ardour/LAD lineage |

---

### THE recommendation — single cheapest, highest-leverage move

**Publish an AUR `-bin` package.** It is a one-file PKGBUILD on the maintainer's own daily-driver
distro (Arch/Omarchy, per CLAUDE.md), requires zero new build infrastructure, has direct
precedent from other single-maintainer VST3 plugins (`zl-splitter-vst3`, `maim-vst3`,
`neuralnote-vst3`, accessed 2026-10-14), and is the one packaging channel where Incant would
visibly be doing something **zero comparators in `competitive_landscape.md` do today** — none of
Amorph, pluginmaker.ai, ConjureDSP, APC, MAGDA, or Sonorie have an AUR package (confirmed absent
for MAGDA specifically, §5; not independently re-checked for the others, so treat that half as
**UNVERIFIED** rather than confirmed-absent). Given it's also the dev's own distro, this is "ship
what you already have, where you already live," not a new project.

A close second, bundled for free: making the already-existing local-LLM (Ollama) path visible in
Linux-facing messaging directly answers §4's documented telemetry/cloud skepticism with something
that costs no new engineering.

---

### Sources (all accessed 2026-10-14 unless noted)
- Libre Arts — [CLAP API two years later](https://librearts.org/2024/11/clap-api-two-years-later/) (dated Nov 2024 — flagged as the stale source behind the "15 hosts/93 devs/~394 plugins" figure)
- [clapdb.tech](https://clapdb.tech) — canonical live CLAP tracker; fetch only surfaced recent-updates feed, not an aggregate total
- Wikipedia — [Bitwig Studio](https://en.wikipedia.org/wiki/Bitwig_Studio) · [CLever Audio Plug-in](https://en.wikipedia.org/wiki/CLever_Audio_Plug-in)
- Spectral Colors — [CLAP vs VST3 (2026)](https://spectral-colors.com/news/clap-vs-vst3-2026/)
- KVR — [Cockos updates REAPER to v6.71 — CLAP Plugin Support](https://www.kvraudio.com/news/cockos-updates-reaper-to-v6-71---clap-plugin-support-56611)
- reapertips.com — [REAPER v6.71](https://reapertips.com/post/reaper-v6-71)
- Ardour discourse — [New plugin standard](https://discourse.ardour.org/t/new-plugin-standard/107337) (Paul Davis CLAP/LV2 quote) · [Current state of VST support on Linux](https://discourse.ardour.org/t/current-state-of-vst-support-on-linux/105468)
- Ardour — [6.5 release notes](https://ardour.org/news/6.5.html) · [FAQ](https://ardour.org/faq.html) (phone-home disclosure)
- JUCE forum — [FR: Support CLAP for plugins](https://forum.juce.com/t/fr-support-clap-for-plugins-host-client/51860)
- GitHub — [free-audio/clap-juce-extensions](https://github.com/free-audio/clap-juce-extensions) · [free-audio/clap](https://github.com/free-audio/clap) · [flathub/org.ardour.Ardour](https://github.com/flathub/org.ardour.Ardour) · [flathub/org.zrythm.Zrythm](https://github.com/flathub/org.zrythm.Zrythm) · [flathub/org.freedesktop.LinuxAudio.BaseExtension](https://github.com/flathub/org.freedesktop.LinuxAudio.BaseExtension) · [9Prestidigitator/maxpkgs](https://github.com/9Prestidigitator/maxpkgs) · [jee-mj/foss-plugins](https://github.com/jee-mj/foss-plugins)
- pipewire.org · ArchWiki — [PipeWire](https://wiki.archlinux.org/title/PipeWire)
- AUR — [zl-splitter-vst3](https://aur.archlinux.org/packages/zl-splitter-vst3) · [maim-vst3](https://aur.archlinux.org/packages/maim-vst3) · [neuralnote-vst3](https://aur.archlinux.org/packages/neuralnote-vst3) · [fire-vst3-bin](https://aur.archlinux.org/packages/fire-vst3-bin) · [formula-vst3](https://aur.archlinux.org/packages/formula-vst3) · [aeolus-vst3-bin](https://aur.archlinux.org/packages/aeolus-vst3-bin)
- NixOS Wiki — [Audio production](https://wiki.nixos.org/wiki/Audio_production)
- Flathub Discourse — [Proposal for audio plugins](https://discourse.flathub.org/t/proposal-for-audio-plugins/399) (thread dated Apr–Jul 2020)
- Gist (abique) — [Proprietary Audio Plugins for Linux, but the right way](https://gist.github.com/abique/4c1b9b40f3413f0df1591d2a7c760db4)
- lsp-plug.in — [Linux Studio Plugins Project](https://lsp-plug.in/) · SourceForge project page
- KXStudio — [and Free Software (manual page)](https://kx.studio/Documentation:Manual:kxstudio_and_free_software) · [kx.studio](https://kx.studio/) · DistroWatch KXStudio entry (distro discontinued; repos still receiving updates in 2026 per repo timestamps)
- Audacity/Audiocity telemetry backlash — search-synthesized from coverage of the 2021 Muse Group telemetry controversy and the Audiocity fork's stated rationale (**not independently fetched from a single primary source — treat the exact Audiocity quote as UNVERIFIED wording, the backlash and fork count as well-corroborated**)
- KVR — [MAGDA forum thread](https://www.kvraudio.com/forum/viewtopic.php?t=628692)
- MAGDA — [manual](https://conceptual-machines.github.io/magda-core/) (Flatpak sideload / Flathub-pending distribution note via search synthesis — **UNVERIFIED against MAGDA's own docs directly**; not re-fetched from primary source in this pass)
- `/home/losera/PluginForge/docs/competitive_landscape.md` — internal source for MAGDA/APC/Amorph competitive facts referenced throughout

---

## B4 — Agent / AI-Developer Ergonomics

**Scope note:** read-only research draft. Does not touch `docs/research/R9-audience-findings.md`
(other agents are writing there); a human merges this in separately.

### The question
Is "Incant as a backend/tool an AI agent reaches for" an open lane, or already occupied? Short
answer up front: **mostly occupied for the headline capability (agent-callable, MCP-native DSP
tooling), with one narrow and currently-real opening (render-check as an agent-callable safety
gate) that nobody else is documented to have.** See verdict at the end — this is not a forced
positive conclusion.

### Occupied-vs-open table

| Capability | ConjureDSP | APC | Incant today |
|---|---|---|---|
| Generate DSP from NL | **Yes** — "describe the effect you want, the agent then codes the effect" via BYO coding assistant (conjuredsp.com, via search summary, accessed 2026-10-14) | **Yes** — agent-driven, against a JUCE 9 skeleton + 5-phase workflow (Dream→Plan→Design→Implement→Ship); no bundled model (`github.com/Noizefield/audio-plugin-coder`, per `docs/competitive_landscape.md:137-155`) | **Yes** — `process_json_request()` dispatches `generate_json()` (`llm/generate.py:649-653`), with a built-in LLM call, not BYO-agent |
| Self-verify / render-check generated code before claiming success | **Not documented.** No search result (this session, 2026-10-14) describes a ConjureDSP tool that renders audio and checks for defects; `get_audio_state` exists but its content wasn't confirmed as a pass/fail safety gate | **Not documented** in the GitHub README/CHANGELOG surveyed — APC's own "auto-capture" system (below) is about *compile* errors, not audio-level defects | **Yes, and unused as a tool today.** `bench/render_oracle.py` renders compiled Faust offline and measures NaN/Inf, DC offset, silence, runaway gain (`bench/render_oracle.py:1-41`) — but it is a dev-side bench script, not exposed to any agent/caller as a tool |
| Iterative repair on compiler error | **Yes** — `compile_and_run` / `get_error` loop, roughly a dozen MCP tools (`docs/competitive_landscape.md:114-122`); whether `get_error` returns raw stderr or a typed/classified error is **unverifiable** — a direct fetch of `conjuredsp.com/docs/` returned no usable content this session, and no search result described the return format | **Yes, but knowledge-base-driven, not per-call typed.** Per GitHub search summary (accessed 2026-10-14): on an error, APC searches a "known issues" database, applies a documented fix if known, else attempts resolution and tracks attempts, auto-creating an issue entry after 3 failed attempts | **Yes, with a closed, named error taxonomy** — `reason` is one of `ok \| invalid_faust \| truncated \| timeout \| rate_limited \| error` (`llm/generate.py:494`, `docs/decisions.md:393` ADR-011), plus action-specific additions (`target_mismatch`, `invalid_recommendation`, `invalid_face`). This is a classified signal an agent could branch on, not raw stderr text alone — though stderr is still folded into the retry prompt, not surfaced itself as structured diagnostic data |
| Export a standalone, buildable artifact | N/A — ConjureDSP's output runs inside its own AUv3 host; no separate "export" step is its model | **Yes — this is APC's actual product.** Output is a real, user-buildable JUCE project (CMake, VST3/AU/CLAP beta) | **No — broken.** `tools/export_repo.py`'s `processBlock` is "Placeholder: passthrough" that never loads or invokes the generated `Patch.dsp` (`tools/export_repo.py:100`, confirmed live in this session; tracked as PF-053, `docs/BUGS.md:91`, open) |
| Expose itself as an MCP server/tool for external agents | **Yes — shipped since ~2026-04/07.** Embedded in-process MCP server, ~12-19 tools depending on source (`docs/competitive_landscape.md:113-122`) | **No** — APC is a repo-skeleton + agent-instruction framework, not an MCP server; the agent drives it directly via its existing coding-agent tool access, no MCP layer of its own | **No.** `INTERFACE.md` documents a one-shot `argv`/`--request-file` subprocess contract between the JUCE host and `llm/generate.py` (ADR-011) — no MCP server exists anywhere in the repo (`grep -rn -i "\bmcp\b"` across `.md`/`.py` returns only unrelated hits — a different `claude_design` MCP mentioned once in session notes, and a citation of someone else's DAW-MCP paper in `docs/research/R7-publication-assessment.md:77`) |
| Multi-harness support with no bundled LLM | **Yes** — "you select your preferred coding assistant, and ConjureDSP plugs into it" (`docs/competitive_landscape.md:110-111`) | **Yes, by design** — explicitly agent-agnostic (Claude Code, Cursor, Antigravity, Kilo, Codex, OpenCode), bundles no model at all (`docs/competitive_landscape.md:141-144`) | **No, opposite default.** Incant bundles its own LLM call (`llm/providers.py`, `CLAUDE.md` Stack section); a BYO-LLM hedge is **authorized but not built** (`docs/byo_llm_plan.md`, referenced in `docs/competitive_landscape.md:417-421`) |

**Addendum (not in the original brief, but materially changes the picture):** `docs/competitive_landscape.md:70-81` records that as of 2026-09-22, **Amorph also now ships MCP connections** for Claude Code / Cursor / VS Code / Claude Desktop (`Artists-in-DSP/amorph-for-agents`, an MCP hub at `127.0.0.1:7330/mcp` exposing `get_host_status`/`edit_lines`/`task_complete`/`apply_draft`). That makes **two** shipped MCP-native competitors (ConjureDSP, Amorph), not one, as of this research date — the "occupied" side of the ledger is stronger than the task brief's framing assumed.

### What's actually still open — honest assessment of the three candidates

**(a) Typed error classification vs. raw stderr.** Confirmed: Incant's `reason` field is a closed,
named enum (`llm/generate.py:494`, `docs/decisions.md:393`), not raw compiler text handed back
uninterpreted. This is real and verifiable on Incant's side. But the comparison can't be completed
honestly: ConjureDSP's `get_error` tool's actual return shape is **unverifiable** this session (the
docs page would not return usable content via fetch; no search result described its structure), so
"Incant's typed taxonomy is more structured than ConjureDSP's" **cannot be claimed** — it may or
may not be true. What's verifiable is that APC's error-handling is a *different kind of structure*
entirely — a persistent known-issues knowledge base keyed by attempt count, not a per-response
typed classification (search summary of APC's README, accessed 2026-10-14) — so Incant's approach
is distinct in mechanism from APC's, even though "is it better than ConjureDSP's" stays open.

**(b) Render oracle as an agent-callable safety check before claiming success.** This is the
strongest finding. `docs/research/R7-publication-assessment.md:84-99` already ran a targeted
search (accessed 2026-10-12) for any paper or tool that generates Faust/Cmajor/JUCE-style DSP via
LLM *and* evaluates it by actually rendering and checking for objective defects (NaN/Inf, DC
offset, runaway gain, silence) — and found none. This session's own searches (2026-10-14) for
ConjureDSP- or APC-specific audio-safety-check tooling corroborate that independently: nothing
found describing either project rendering output and gating on NaN/DC/runaway-gain/silence before
telling the calling agent "this is safe." **This capability is real on Incant's side today**
(`bench/render_oracle.py:17-22`'s `measure()`) **and genuinely undocumented anywhere else** — but
it is currently a bench/CI tool, not callable by an external agent. The gap between "we have the
logic" and "an agent can call it" is the whole opportunity here.

**(c) Separate, real demand for an audio-DSP MCP tool, distinct from what ConjureDSP already
provides.** Weak support, honestly reported. Searches this session for an audio-DSP-generation MCP
server turned up only generic audio-analysis/playback servers (spectral/harmonic/rhythm analysis
via librosa or similar, audio I/O control) — nothing resembling "generate + compile + verify DSP
code via MCP" as a distinct ask separate from ConjureDSP's own niche. The one adjacent academic
signal is DAWZY (arXiv 2512.03289, cited in `docs/research/R7-publication-assessment.md:71-82`) —
an MCP-based NL-to-REAPER-automation assistant — which shows HCI-research interest in DAW+MCP
generally, but it is about DAW *control*, not DSP *code generation*, and offers no evidence of
unmet demand specifically for a second DSP-generation MCP server alongside ConjureDSP's. **No
distinct demand signal was found.** This candidate should be treated as unconfirmed, not confirmed
open.

### Smallest-surface-change proposal

Wrap the existing, already-working headless entry point as an MCP server exposing a small, honest
tool set — nothing new is invented, nothing in the JUCE host or the ADR-011 wire contract changes:

1. **`generate(prompt, kind?, provider?, model?)`** — thin wrapper around
   `process_json_request()` / `generate_json()` (`llm/generate.py:649-653`), already callable
   headlessly and independent of the JUCE host.
2. **`render_check(faust_code)`** — thin wrapper around `bench/render_oracle.py`'s `measure()` /
   `features()` / `tail()` (`bench/render_oracle.py:17-37`), newly exposed as a callable tool
   rather than a dev/CI script. This is the one piece that is not duplicated by either competitor
   per the research above.
3. **`error_reason(response)`** — not new code, just documenting the existing closed `reason` enum
   (`llm/generate.py:494`) as a typed field in the tool's output schema, so a calling agent can
   branch on `invalid_faust` vs. `timeout` vs. `rate_limited` instead of pattern-matching stderr
   text.

This is deliberately **not** "wrap export, too" — `tools/export_repo.py` is broken today (PF-053),
and shipping a broken export tool to an external agent would be worse than not shipping one. Export
should stay out of the MCP surface until PF-053 closes.

This proposal is additive and does not require resolving the BYO-LLM architectural question
(`docs/byo_llm_plan.md`) — the MCP server would still call Incant's own bundled LLM for `generate`,
same economics as today, just reachable by an external agent instead of only the JUCE host.
Whether to *also* let the MCP caller supply its own already-generated Faust code and skip straight
to `render_check` (bypassing Incant's LLM call entirely) is a natural follow-on and is exactly the
kind of architectural fork (does Incant's value live in generation, or in verification?) that
belongs in front of the user per AGENTS.md §4, not decided here.

### Verdict

**Occupied-but-narrow-opening-exists.** The headline capability — "agent reaches for an MCP tool
that writes, compiles, and iterates on real-time DSP code inside a DAW context" — is shipped twice
over (ConjureDSP since ~April 2026; Amorph since 2026-09-22) and approximated a third way by APC's
agent-agnostic skeleton. Treating this as wide open would be dishonest. The one piece that
research in this session and in `docs/research/R7-publication-assessment.md` could not find
anywhere else — an agent-callable render-and-measure safety gate (NaN/Inf/DC/runaway-gain/silence)
sitting between "compiles" and "I'm telling you this is safe" — is real, already built
(`bench/render_oracle.py`), and currently inert (no caller outside the dev/CI path). The smallest
honest move is exposing exactly that, plus the existing typed error enum, as 2-3 MCP tools around
the already-working headless entry point — not chasing ConjureDSP's or Amorph's broader
"full agentic control of a running patch" surface, which is both already occupied and would
require architecture Incant does not have today (Incant's wire contract is one-shot request/
response, ADR-011; it has no notion of a "running patch" an agent edits live).
