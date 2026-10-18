# R14 — Recommendations & Planning Prompts

**Written 2026-10-14.** Synthesizes R6–R13 (the market-fit research session) plus two inputs
reviewed the same day: **"Incant Vision"** (a 7-frame product-vision canvas made with Claude's
Design tool) and a **live-performance hypothesis** the user raised mid-session ("like Strudel,
but AI builds the instrument while it plays"). This doc exists because R6–R13 are the right
shape for *evidence* — file:line cited, independently checkable — and the wrong shape for
*picking up work next session*. Everything actionable below ends in a paste-ready prompt.

Read-only research; no code changed to produce this doc. All internal citations verified
directly against the repo at write time, not inherited from a prior doc's summary of itself.

---

## 1. Review note

Cross-checked, not assumed, before writing anything below:

- PF-079's render-safety numbers (groq 56–68%, ollama 75–95% vs. 84–96%/similar compile rates)
  are consistent across R10, R11, and R12, and re-confirmed directly against `docs/BUGS.md:117`
  today — **still open**.
- PF-081 (the retry loop never shows the model the program it's repairing) re-confirmed open
  at `docs/BUGS.md:119` — matches R10's characterization exactly, including the falsified
  PF-076 caret-preservation mechanism it corrects.
- PF-053 (`/export` is a passthrough stub) re-confirmed open at `docs/BUGS.md:91`.
- MAGDA's "AI-Faust-panel" claim is consistently flagged unconfirmed (two secondary sources,
  one primary page not corroborating) everywhere it's cited — `competitive_landscape.md`, R9
  §B2/§B4, R11. No contradiction found.
- **Two loose threads from late in the session, not yet folded into anything:**
  1. R9 §B3 found an untracked competitor, **Chaos Audio's "AI FX Builder"** (via GRAME's own
     LLM-guidance page), which never made it into `docs/competitive_landscape.md` — B3 ran
     after A1's competitive-refresh pass had already closed.
  2. R12 found a live render-safety failure (`dynamics-01/L3`, a compressor patch recording
     `first_try_compiles: true` but rendering completely silent) not yet a line item in
     `docs/BUGS.md`'s PF-079 entry.

---

## 2. "Incant Vision" — adversarial assessment

A 7-frame canvas, "Incant in 2–3 years," decoded directly from
`~/Downloads/Incant Audio UI mockups (1).html` (a Claude Design export; the `/design/p/` and
`/artifact/` share-link formats the user first pasted aren't readable by this session's
Artifact tool — the downloaded bundle was). Full content read directly, not summarized
secondhand. Pipeline: **Intent → Signal Plan → Forge → Prove → Hot-swap → Refine → Own.**

### 2.1 Already substantially real

| Vision stage | Real mechanism |
|---|---|
| Intent | `llm/router.py`'s keyword-scoring `classify()` |
| Signal Plan | `llm/recommendation.py` (ADR-033's typed pre-generation plan) — the vision's "plan card" just isn't surfaced in the UI for approval yet |
| Forge | `generate.py` + the stdlib-grounding mechanism (`tools/gen_stdlib_block.py` / `check_prompt_invariants.py`) that already rejects invented `ns.func` calls |
| Prove | `validate_faust()` + `bench/render_oracle.py` + `OutputGuard` |
| Hot-swap | The documented zero-drop DSP-swap protocol (`CLAUDE.md`'s "DSP swap protocol" invariant) |

One precise correction worth making if this framing is ever repeated publicly: the vision
describes Forge as "Faust written against **retrieved** library docs" — the real mechanism is a
**deterministic generated-and-trimmed static block**, not RAG-style retrieval. Same spirit
(grounded, not hallucinated), different and more precise mechanism.

### 2.2 Directly collides with a known, documented limitation

**(a) Polyphony.** The instrument-mode mockup (F3, "Felt Pluck") depicts a fully polyphonic
8-voice UI with a per-voice allocation bar. The real engine is monophonic by design
(`FaustEngine.cpp:519-540`) — the same overclaim R10 already caught in README.md. True
polyphony is a real architecture change, not a UI task.

**(b) Targeted repair.** The "Prove" trust-moment scene (F5) depicts a one-line diff repairing
the *exact broken draft* — a routing-arity fix shown as a surgical edit with a plain-English
reason. PF-081 (confirmed open, §1 above) found the real retry loop **never shows the model the
program it's repairing** — it blindly regenerates from the original spec with only the error
text appended. The vision names the right feature; it isn't built.

**(c) Export formats.** Three frames (F1, F3, F6, F7) assume VST3/AU/CLAP export exists. Only
VST3 + Standalone ship today. AU is an explicit non-goal on record; CLAP is costed in H2 (R11).

### 2.3 Independent corroboration worth acting on

The positioning map (F7) plots **"Chaos Audio AI FX Builder"** as a T2 rival — the exact same
previously-untracked competitor R9 §B3 found independently via GRAME's own LLM-guidance page,
hours earlier in this same session. Two unrelated sources converging on the same gap reinforces
closing it. The map also names several products this session never verified — Radium Faust
Dev 2, nagi, FaustWave, BIAS X, sonible smart:, Output Co-Producer — and the canvas itself
caveats this as "our reading of each product's public description, not a benchmark." Worth a
quick verification pass, not blind inclusion.

### 2.4 Genuinely new, worth costing as its own track

Not yet built, not contradicted by anything known:

- **The "Ear" perceptual-refinement loop** (F4) — a CLAP-scored intent-match, checkpointed
  (c0–c6), scrubbable refinement UI ("less boxy, more air" → visible spectral before/after,
  parameter diff table). Distinct from `bench/spectral_judge.py`'s objective defect-checking
  role — this is semantic/perceptual, that's objective-safety.
- **A Stage/Graph/Code three-depth editor** (F2/F3) — touching a knob highlights its node in
  Graph and the exact source line in Code; one surface, three depths, no product-switching.
- **A provenance card attached at export** (F7) — naturally sequenced with fixing `/export`
  (PF-053), since there's nothing to attach a card to until export actually works.

### 2.5 Verdict

Useful as feature-ideation and positioning-narrative material, and as real confirmation that
5 of 7 pipeline stages already exist in some form. **Not** useful as a roadmap to adopt as-is —
its two most dramatic frames (polyphony, targeted repair) depict capabilities the codebase
doesn't have. Each genuinely-new idea gets a Tier 2 decision-framing prompt below, not a
Tier 0/1 executable one.

---

## 3. Live-performance instrument generation — "Strudel, but AI builds the instrument live"

The user's fourth hypothesis, raised mid-session: not editing patterns over a fixed synth
(Strudel/TidalCycles), but generating and hot-swapping actual *instruments* via NL or code,
live, without stopping playback.

**Strudel is not prior art for the AI-generation half.** Confirmed directly
(thoughtwax.com's first-person account): the AI assistance happens *before* a performance
(prompting Claude for Strudel code, debugging it offline); live performance is manual code
toggling. Strudel is the right reference for the live-coding performance *UX*, not evidence the
AI-generation half already exists.

**The closest real prior art** is a NIME 2026 paper — Gupta, "A Text-Steerable Instrument for
Sketching Procedural Soundscapes via Language Models" (arXiv 2607.00309, read directly from the
PDF). Real, open-source (`github.com/prabal-rje/latentscore`, demo at `latentscore.com`), and
it does exactly the "no stopping, no waiting for a GPU" live-crossfade pitch. **But it is
precisely not what the user described**: it does not generate DSP source code live. It resolves
text into a fixed, 34-field *categorical configuration* (tempo, brightness, bass/pad/melody
*style selectors* from a curated set) that a deterministic procedural engine renders —
explicitly chosen *over* code generation. Direct quote: "Where GPU-bound text-to-audio systems
synthesize monolithic waveforms... our instrument generates human-readable configurations over
a categorical schema." Its own benchmark table (Table 2): embedding-retrieval backend resolves
in ~1.2s end-to-end at 100% schema validity; routing through an external LLM (Gemini 3 Flash
Preview) takes ~5.5s and fails schema validation 11% of the time, falling back to the fast
backend to keep audio from dropping out.

**That design choice is a direct, evidence-based risk for the user's idea, not just a
citation.** Incant's own measured numbers point the same direction: PF-079 (compile success
overstating render-safety by 25–40pp, §1) and LLM-inference-bound generation latency (seconds to
tens of seconds per call, worse under retries, per R12) are exactly the failure modes a
fixed-categorical-config design sidesteps — and a live arbitrary-code-generation design would
inherit, *during a live set*, where a bad swap is far more costly than in a studio.

**Incant's differentiator survives the comparison, stated precisely**: Incant generates
arbitrary new Faust *topology* (real signal-graph synthesis), not a selection over a fixed set
of orchestration-layer presets — a categorically harder problem than the NIME paper solves.
Nothing found competes directly at the "live, no-interruption, arbitrary-DSP-generation"
intersection.

**This reframes H3, it doesn't duplicate it.** R11's H3 cost estimate (6–18+ months) assumed
full DAW infrastructure — tracks, timeline, mixer, clip launching — scoped against MAGDA's
"build your own DAW" framing. A live-performance mode needs none of that: it's the **existing,
already-real, already-TSan-verified hot-swap protocol** wired into a performance-oriented
workflow, not a new DAW shell. Plausibly much cheaper than R11's H3 number — but the
latency/reliability risk above means it needs its own honest cost pass, not a reuse of H3's.

**Verdict**: a real, distinct, costable idea — not yet explored by this session's research, not
already solved by Strudel, only partially anticipated by one NIME 2026 paper that chose a
different, safer, less general approach for reasons Incant's own measured defects corroborate.

---

## 4. Condensed recommendations (from R13 — not re-derived)

- **Ranked bet: H1 > H2 > H3.** PF-079 remediation first (needed regardless of positioning),
  then the license decision, then README/positioning rewrite + AUR package. Counterargument on
  record: every week on H1's cleanup is a week Amorph/MAGDA compound user bases Incant doesn't
  have — the bet is that H1 buys the cheapest *credible* claim, not that it's risk-free.
- **Visibility sequence**, cheapest first: fix README's polyphony claim → Hügelton-style
  disclosure line → AUR package → QWERTY demo video + Show HN → KVR/Gearspace listing →
  NIME 2027 short paper once its CFP opens.
- **Safe to say today**: NL→Faust→JIT→VST3/Standalone on Linux x86-64; the 3-attempt retry
  loop with classified error feedback exists and runs; `OutputGuard` checks NaN/Inf/DC/runaway
  (described factually, without the word "safe" as a blanket guarantee); the project has
  publicly corrected its own overclaims twice.
- **Do not say yet**: anything implying output "sounds good" (zero instrument exists for this —
  the single highest-risk gap); "safe"/"production-ready" unqualified (PF-079 open); "polyphonic
  synthesizers" (confirmed false); any specific compile/retry/render-safe percentage (stale,
  n=1 against the project's own n≥3 bar); "VST3/AU" (no AU target exists); anything about binary
  size/LLVM linkage from ADR-001 (describes a static-link build that isn't what ships).

Full detail, citations, and the complete claims ledger: R10, R11, R13.

---

## 5. Planning prompts

Paste-ready, addressed to a fresh Claude Code session. Each names concrete files/PF-IDs/ADRs/
R-docs so it's self-contained per `AGENTS.md §11`.

### Tier 0 — zero-dependency, do regardless of strategy

**Fix the README polyphony overclaim.**
> Fix the overclaim in README.md: it markets "polyphonic synthesizers" but `FaustEngine.cpp:519-540`
> shows the engine is monophonic by design (single `voiceFreq`/`voiceGain`/`voiceGate` atomics,
> last-note-priority release). See `docs/research/R10-claims-ledger.md` claim #5 and
> `docs/BUGS.md` PF-075 for the self-acknowledged context (a tester heard "~5 voices" from
> overlapping release tails, not real polyphony). Reword README.md to describe what the engine
> actually does; don't claim polyphony until it's real.

**Fix STATUS.md's lingering "VST3/AU" string.**
> STATUS.md:90 still says "VST3/AU" — there is no AU target (`host/CMakeLists.txt:70,149` is
> `FORMATS VST3 Standalone`). A sibling instance of this exact string was already corrected
> elsewhere in STATUS.md (PR #73, 2026-09-08) but this one was missed. Fix it.

**Correct ADR-001's binary-linking description.**
> `docs/decisions.md` ADR-001 states the plugin binary "must link libfaust + LLVM (~30MB added
> to binary size)" — this describes static linking. The actual build
> (`host/CMakeLists.txt:106-120`) dynamically links a system `libfaust.so` and never links LLVM
> directly. Correct ADR-001's Consequences section to match the real build. See
> `docs/research/R8-licensing-options.md` §2(c) and `R10-claims-ledger.md` claim #6 for the full
> evidence trail.

**Add Chaos Audio and spot-check the other unverified rivals in `competitive_landscape.md`.**
> `docs/competitive_landscape.md` doesn't yet track Chaos Audio's "AI FX Builder" — found
> independently twice in one session (R9 §B3 via GRAME's LLM-guidance page, and separately named
> in the "Incant Vision" positioning map as a T2 rival). Add it following the doc's existing
> field-table format. While there, spot-check the other names from the vision canvas's
> positioning map that this session never verified: Radium Faust Dev 2, nagi, FaustWave, BIAS X,
> sonible smart:, Output Co-Producer. The canvas itself caveats these as unverified; confirm or
> drop each before citing them anywhere.

**Triage the `dynamics-01/L3` silent-compressor finding.**
> While preparing the R12 listening-session kit, re-running `bench/render_oracle.py` against
> `efficacy_groq_20260831.json`'s `dynamics-01/L3` cell (prompt: "a compressor with threshold,
> ratio, attack, and release") found it renders completely silent today, despite the archive
> recording `first_try_compiles: true`. This is a live instance of the PF-079 pattern
> (`docs/BUGS.md:117`) not currently named in that entry — only the limiter and expander
> dynamics failures are. Decide whether this needs its own PF-ID or folds into PF-079; file
> accordingly. Full repro: `docs/research/R12-session-kit.md` §2.

### Tier 1 — scoped engineering

**Land the PF-079 dynamics fix.**
> `docs/BUGS.md:117` (PF-079, open, high) has both root causes already diagnosed from the Faust
> stdlib directly: an expander's unbounded makeup gain (no envelope floor before `ba.linear2db`,
> `basics.lib:163`/`maths.lib:276`) and a limiter's inverted `select2` arms (`basics.lib:1888`).
> The fix is owed as a `system_prompt.txt` change plus two `render_oracle.py --self-test` red
> cases — not yet landed. Land it, then re-run the efficacy study across both archives to
> confirm the gap actually closed. This is real correctness work the product needs regardless
> of any positioning decision — do it first.

**Resume the in-flight PF-031 n≥3 efficacy re-run.**
> STATUS.md's reserved evidence item is PF-031: re-running the groq efficacy grid at n≥3
> (current numbers are n=1). A checkpoint already exists, uncommitted, in
> `.worktrees/efficacy-pf031` at 38/125 cells. Resume it — don't start a fresh run.

**Write the Hügelton-style AI-disclosure line.**
> Add a disclosure sentence to the public README following the template
> `docs/research/R6-visibility-ledger.md` §1 names: state what the LLM does (writes Faust/C++)
> and what it explicitly does not do (make architecture or UI decisions — both human-gated per
> `COLLABORATION.md`). The facts are already true and already documented in `CLAUDE.md`; this is
> a publishing task, not an engineering one.

**Publish an AUR `-bin` package.**
> Package a PKGBUILD for the built VST3 bundle, installing to `~/.vst3`, following the pattern
> of existing single-maintainer AUR VST3 packages (`zl-splitter-vst3`, `maim-vst3`,
> `neuralnote-vst3` — see `docs/research/R9-audience-findings.md` §B2 for the precedent). No new
> build infrastructure needed; this is the dev's own daily-driver distro.

**Record the QWERTY demo video and post to Show HN.**
> Record a short demo of the already-logged QWERTY→heard-notes session
> (`docs/sessions/017-phase2-interactive-host.md`) and post it to Show HN with the
> self-correction story (PF-076's withdrawal) told plainly in the post text. Per
> `docs/research/R6-visibility-ledger.md` §3, real 2025–2026 Show HN cases show a working demo +
> evidence-backed, non-overclaiming prose beats both polish and open-source status there.

**Ship CLAP support via `clap-juce-extensions`.**
> Add CLAP output using `clap-juce-extensions` (MIT, works with JUCE 6/7/8, no JUCE 9 upgrade
> needed — already production-proven by Surge, ChowDSP, and Audio Plugin Coder's own CLAP beta).
> See `docs/research/R9-audience-findings.md` §B2 for the full feasibility note, including the
> caveat that Ardour specifically won't care (no CLAP support there) but Bitwig and REAPER will.

**Fix `/export` (PF-053).**
> `docs/BUGS.md:91` (PF-053, open, medium): `tools/export_repo.py`'s `processBlock` remains a
> passthrough stub that never loads or invokes `Patch.dsp` (`:98-103`). ADR-038's design (E1 AOT
> emission) has partly landed as `host/Source/AotEmit.h`; what remains is E2 (wire `processBlock`
> to the static DSP, fix the `PLUGIN_CODE` collision) and E3 (prove sound, un-gate `/export`).
> Finish it.

**Build the 3-tool MCP wrapper.**
> Wrap the existing headless entry point as a minimal MCP server: `generate()` (thin wrapper
> around `process_json_request()`/`generate_json()`, `llm/generate.py:649-653`), `render_check()`
> (wraps `bench/render_oracle.py`'s `measure()`/`features()`/`tail()`, currently a dev/CI script
> with no external caller — this is the one piece not duplicated by ConjureDSP or Amorph, per
> `docs/research/R9-audience-findings.md` §B4), and `error_reason()` (documents the existing
> closed `reason` enum, `llm/generate.py:494`, as a typed output field). Deliberately exclude
> export until PF-053 closes.

**Surface Faust's own control metadata into grouped, typed UI.**
> Faust metadata Incant already generates (`hgroup`, `[style:knob]`, `[scale:log]`, `[unit:Hz]`)
> isn't driving the UI today — every control renders as a flat slider. Wire the param grid to
> read this metadata and render grouped, typed controls (knobs, log-scaled ranges, unit labels)
> instead. This is the "Incant Vision" canvas's F2 note #2 ("current Incant gap") — no new
> architecture needed, just reading metadata already present in generated `.dsp` source.

### Tier 2 — human-gated decisions (these need your call, not a session's unilateral execution)

**License: GPLv3 vs. open-core.**
> Decide between full GPLv3 (clears Synth Anatomy's one named coverage lever unambiguously, zero
> fee under JUCE 7.0.9's GPLv3 escape hatch) and open-core (keep `host/Source/*` proprietary
> under JUCE's free Personal tier, open `llm/` — already precedented by the existing CC-BY/MIT
> corpus carve-outs in `LICENSE`, and legally viable since `llm/` never links JUCE). Full
> decision table: `docs/research/R8-licensing-options.md` §5. Not urgent — the obligation
> triggers at public distribution, which hasn't happened yet — but worth deciding before the
> next release, not after.

**Confirm or kill MAGDA's AI-Faust-panel claim.**
> `docs/competitive_landscape.md` flags MAGDA (github.com/Conceptual-Machines/magda-core) as
> possibly already combining NL DAW control and NL Faust-DSP generation in one free, open-source
> tool — based on two secondary sources; one primary doc page didn't corroborate it. Do a
> targeted primary-source check (MAGDA's own manual/changelog, not search-engine summaries)
> before spending any time on H3 in any form. If confirmed, H3's framing changes from "build a
> new category" to "catch up to an incumbent with 933 commits."

**Is true polyphony worth its own architecture track?**
> The "Incant Vision" canvas's instrument-mode mockup assumes 8-voice polyphony; the real engine
> is monophonic by design (`FaustEngine.cpp:519-540`). Decide whether real multi-voice allocation
> is worth costing as its own track — this is new engine work, not UI styling, and should get an
> ADR before any implementation starts, per `AGENTS.md §4`.

**Is the CLAP-"ear" perceptual-refinement loop worth an R&D track?**
> The vision canvas's F4 ("Ear refinement close-up") proposes a CLAP-scored, checkpointed,
> scrubbable perceptual-refinement UI — distinct from `bench/spectral_judge.py`'s existing
> objective defect-checking role. Nothing in the codebase does semantic/perceptual intent-match
> scoring today. Decide whether this is worth prototyping as a new track, and at what fidelity
> (a real CLAP model vs. a cheaper spectral-band heuristic to start).

**Is a Stage/Graph/Code three-depth editor worth committing to now?**
> The vision canvas's F2/F3 propose one surface with three depths (Stage/Graph/Code), where
> touching a knob highlights its node and its exact source line. Decide whether this UI
> redesign is worth committing to now, or after Phase 4 (installable builds) ships — Phase 4 is
> still release-critical and blocked per `docs/decisions.md`.

**Is a live-performance mode worth prototyping?**
> Full reasoning in §3 above. Decide whether to prototype a live-performance mode on top of the
> existing, already-TSan-verified hot-swap protocol (`CLAUDE.md`'s "DSP swap protocol"
> invariant) — and if so, what latency/reliability bar a live set actually requires before this
> is safe to put in front of an audience. Ground that bar against two real numbers: the
> `latentscore` NIME 2026 paper's own benchmark (its embedding-retrieval backend: ~1.2s
> end-to-end at 100% validity; its external-LLM backend: ~5.5s at 89% validity — Table 2,
> arXiv 2607.00309), and Incant's own measured generation latency + PF-079's render-safety gap.
> A live set is far less forgiving of a bad swap than a studio session.

### Tier 3 — longer horizon

**Prepare a NIME 2027 short-paper submission.**
> Once NIME 2027's CFP opens (promised October 2026 per `docs/research/R7-publication-assessment.md`),
> submit the PF-079 finding ("compile success is an insufficient correctness proxy for
> LLM-generated real-time audio DSP") at the 2000-word short-paper tier. Draft abstract already
> written: `docs/research/R7-publication-assessment.md` §3. Gap list before submission is
> possible: §4 of the same doc — no human-perceptual corroboration, single model/prompt family,
> no control/baseline rate, the "25-40pp" figure needs a stated computation method, oracle
> precision/recall uncharacterized.

**A provenance card attached at export.**
> The vision canvas's F7 pairs every export with a "provenance card" documenting how a patch was
> made. Sequence this naturally after `/export` (PF-053) actually works — there's nothing to
> attach a card to until then.

---

## 6. The one item no prompt can do

Per `COLLABORATION.md §1`: whether a generated plugin sounds like what was asked for has no
instrument, and never will by writing more documentation. R12's session kit is ready — 8
pre-screened candidates, a REAPER runbook, a scorecard that takes under a minute per patch.
Every "measured" claim in Tier 1 and Tier 2 above is downstream of this actually happening.

---

## Sources

Synthesizes `docs/competitive_landscape.md` (updated this session) and
`docs/research/R6`–`R13` without re-deriving them — see each for full citations. New sources
this doc adds: `~/Downloads/Incant Audio UI mockups (1).html` (the "Incant Vision" canvas,
decoded directly), thoughtwax.com's "Live-coding music with AI" (2025-06, fetched directly),
and arXiv 2607.00309 (Gupta, "A Text-Steerable Instrument for Sketching Procedural Soundscapes
via Language Models," NIME 2026, read directly from the PDF — not a secondhand summary).
