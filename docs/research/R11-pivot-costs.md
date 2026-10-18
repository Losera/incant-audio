# R11 — Pivot Cost Assessment: Three Hypotheses, Adversarial Throughout

**Status: research only.** Read-only; no code changed to produce this. Per AGENTS.md §4,
architecture is human-gated — nothing below is a decision, and the ranked recommendation at
the end is a bet offered for the human to accept, modify, or reject, not a choice already
made. Written 2026-10-14, citing this session's prior research docs (R6-R9) and the repo as
of `main`/`cs375-a2-part-b` HEAD at write time.

---

## 0. Base rate — this project's own pace, so the estimates below aren't invented from nothing

Git history: first commit `23d16dc`, 2026-07-21; most recent commit at time of writing,
2026-10-11 — **82 calendar days, 333 commits**, ~55,700 lines across tracked
`.cpp`/`.h`/`.py` source (`git ls-files | grep -E '\.(cpp|h|py)$' | ... | wc -l`, measured
directly this session — close to, and corroborating, the task brief's "~58k lines" figure;
the gap is almost certainly build-artifact/vendored-path exclusion differences, not a real
disagreement), 40 ADRs in `docs/decisions.md` (`grep -c "^## ADR-"`, not the 5-file
`docs/architectural_decisions/` directory, which is a smaller/older parallel location), and
15 currently-open defects in `docs/BUGS.md` (`grep -c "| open |"` — matches the task brief's
"15 open defects per earlier repo mapping this session").

Phase timing, from `docs/phases/phase-1.md:3,16,23` and `phase-2.md:3,15`: Phase 1 (core
loop — NL prompt → Faust → JIT → VST3, hot-swap, state persistence) was "verified by ear"
**one day after the initial commit** (2026-07-22) and reached "substantially done" over the
following weeks. Phase 2 (real-host proof — actually loading and playing both plugin targets
inside REAPER) closed its largest gap on 2026-08-28, roughly **5.5 weeks** after Phase 1's
first working day.

**This is a single developer working with heavy AI-agent leverage (Claude Code), not a
traditional solo-human engineering pace** — the numbers above (working core loop in ~1 day;
~56k lines and 40 ADRs in ~12 weeks) are only a sane calibration point if every estimate below
is understood in the same terms. Where this document says "N person-weeks," it means
*at this project's own demonstrated agent-leveraged pace*, not a generic industry weeks-per-
feature heuristic. That cuts both ways: it is the reason the estimates below are not absurdly
long for a single person, and it is also why H3's full-DAW cost (§3) is explicitly flagged as
colder than H1/H2's — nothing in this history has built DAW-level infrastructure (transport,
timeline, clip engine) to calibrate against, even at this pace.

---

## 1. H1 (DEEP) — Best-in-class open-source NL→DSP

### What's actually required

**(a) License decision + execution.** Per `docs/research/R8-licensing-options.md` §5's
decision table: JUCE 7.0.9 (the pinned version, `CLAUDE.md` Stack section;
`/home/losera/JUCE/LICENSE.md`, read directly per R8 §1) already has a zero-fee GPLv3
escape hatch, and Faust/libfaust's own license (R8 §2) is not the limiting factor either
way — the architecture-file exception in `llvm-dsp.h` already lets the JUCE-linked host
binary be distributed "under terms of your choice." Two real options, not one:
- **GPLv3, whole repo.** Clears Synth Anatomy's one *named*, concrete lever — "open-source
  the code" (`docs/research/R6-visibility-ledger.md:46-49`, quoting the article directly) —
  unambiguously. Forecloses ever dual-licensing `host/Source/*` for a fee.
- **Open-core** (keep `host/Source/*` proprietary under the JUCE Personal free tier, open
  `llm/`). Already precedented by the existing CC-BY/MIT corpus carve-outs in `LICENSE`
  (R8 §5, "Source-available / open-core" row) and legally viable independent of JUCE's
  terms, because `llm/` never links JUCE at all. This is the layer R8 and
  `docs/research/R9-audience-findings.md` §B4 both independently flag as most interesting
  to the AI-agent-developer audience specifically.

**Adversarial note on which to recommend:** open-core is cheaper and more precedented, but
it is a real gamble against Synth Anatomy's bar specifically — the article's own language is
"open-source the code," not "open-source part of the code," and a partial open-core may or
may not read to a skeptical editor as clearing that bar. Full GPLv3 clears it unambiguously
at the cost of real optionality. **This document does not resolve that trade — it is
exactly the kind of licensing/business decision R6 itself flags as consult-trigger
territory (COLLABORATION.md §2.3/§4), not something this research note is authorized to
pick.** Execution cost either way (LICENSE rewrite, SPDX headers, verifying no accidental
boundary violations between `llm/` and `host/Source/*`): **0.5-1 person-week.**

**(b) README/positioning rewrite.** `docs/competitive_landscape.md:394-397` already drafted
the exact three-legged pitch ("self-correcting... safe... measured") this would execute —
the analysis is done, only the writing remains. **0.25-0.5 person-week.**

**(c) Closing the PF-079 gap — the expensive, real part.** `docs/BUGS.md:117` (PF-079,
open, high severity): a 2026-09-23 render-oracle pass over 325 compiled cells across two
independent generator models found render-safety **25-40 percentage points below compile
rate** (groq 56-68% render-safe vs. 84-96% compile; ollama 75-95% vs. similar), with two
root causes already diagnosed directly from the Faust stdlib source: an expander's makeup-
gain stage has no envelope floor before `ba.linear2db` (`basics.lib:163`, `maths.lib:276`),
producing up to +715dB measured; a limiter's `select2()` branch arms are inverted
(`basics.lib:1888`), producing 0.95-0.98 DC offset. Per `docs/BUGS.md:117`'s own final
clause: *"fix owed as a `system_prompt.txt` change plus two `render_oracle.py --self-test`
red cases, not yet landed."* Landing the fix, adding the regression cases, and re-running
the efficacy study across both archives to confirm the gap actually closed is realistically
**2-4 person-weeks** — with real open-ended risk the range doesn't fully capture: fixing
the two *known* root causes does not guarantee no other hidden render-safety failure modes
exist elsewhere in the `dynamics` category (already flagged as the worst-performing
category on both archives, `docs/BUGS.md:117`) or elsewhere in the corpus.

**(d) AUR `-bin` package.** Per `docs/research/R9-audience-findings.md` §B2 "Adoption
checklist," ranked #1 by effort-to-credibility ratio: "a single file, no sandboxing, dev's
own distro" — **0.25-0.5 person-week**, contingent on a release build actually existing
(Phase 2/4 release-readiness; see dependencies below).

**Total: ~3-6 person-weeks** (roughly 1-1.5 months at this project's own pace), with the
PF-079 remediation as the dominant cost and the one component carrying real schedule risk
if hidden failure modes surface.

### What must be true first

- A human decision on GPLv3-vs-open-core (AGENTS.md §4 — this document cannot make it).
- PF-079's fix must actually land and be re-measured *before* any "measured reliability"
  number is published — publishing an unverified number would repeat exactly the pattern
  the project has already been burned by once (README's "polyphonic synthesizers" overclaim
  against the monophonic engine, `docs/research/R9-audience-findings.md:92-103`) and that
  the project has a track record of *not* doing twice (the faust-rs causal-claim withdrawal,
  `docs/decisions.md:3421` region, cited favorably in `docs/research/R7-publication-
  assessment.md` §4 as a rigor signal worth preserving).
- A packageable release build for the AUR step — `docs/decisions.md:2488-2491` records
  Phase 4 ("installable release") as still *"Release-critical, blocked"* as of the ADR-038
  writeup; this should be re-checked at execution time, not assumed clear.

### Kill criteria (concrete, falsifiable)

**If, after landing the two known PF-079 fixes and re-running the efficacy study across
both archives, render-safety is still materially below compile-rate on at least one
provider (i.e., the fix doesn't close the gap it was aimed at) — stop publishing any
reliability number and do not lead the positioning on "measured" until it does.** The whole
H1 bet is that "guarantees, not concept" is defensible; a defensible-looking pitch built on
a number that turns out to still be bad is worse than no pitch, because being first to
publish — which `docs/competitive_landscape.md:259-266`'s "Decisive question" section
confirms nobody else has done — only helps if the number is good enough to want attention
on it.

### Strongest counterargument

This is largely "the status quo, cleaned up and documented," and it does not change
PluginForge's competitive position against competitors who are *shipping and compounding* —
`docs/competitive_landscape.md:272-275` (Threat #1) already names this exact risk: *"Every
day of internal architecture polish is a day Amorph compounds a user base and a patch
library."* A month spent on licensing language, README positioning, and one (real, needed)
bug-class fix is still a month not spent getting a single generated plugin in front of a
single new user. And the bet that "being measured first" pays off assumes the eventual
number is flattering — if it isn't (dynamics-category failures could run deeper than the
two diagnosed causes), publishing first could read worse than publishing nothing, which is
the same risk named in the kill criterion above, not a separate hypothetical.

---

## 2. H2 (MEDIUM) — Platform/SDK others build on

### What's actually required

**(a) CLAP support via `clap-juce-extensions`.** `docs/research/R9-audience-findings.md`
§B2 point 1 and the adoption checklist (#3): an MIT, JUCE-7-compatible bolt-on, "a medium-
effort, well-trodden dependency add... not an architecture change," already production-
proven (Surge, ChowDSP, and the exact mechanism Audio Plugin Coder's own CLAP beta uses per
`docs/competitive_landscape.md:145-147`). Needs CMake wiring, a build/test pass across both
VST3 and CLAP targets, and verification that `GeneratedFaceLookAndFeel`/`UiIr` render
correctly under both. **1-2 person-weeks.**

**(b) Fix `/export` (PF-053).** `docs/BUGS.md:91`: the invalid generated-C++ bool
expressions were already corrected 2026-08-12; `processBlock` remains *"a passthrough stub
that never loads or invokes `Patch.dsp`"* (`tools/export_repo.py:98-103`). The design work
is already done — `docs/decisions.md:2463-2471,2509` (ADR-038, citing ADR-023's amendment):
AOT emission via `libfaust`'s `generateAuxFilesFromString`, in-process, no new dependency,
is already specified; what remains is E2 ("wire `processBlock` to the static DSP + fix the
`PLUGIN_CODE` collision") and E3 ("prove-sound + un-gate `/export`"). Implementation-only
work against an already-specified design, at this project's own pace for similarly-scoped
defects: **1-2 person-weeks.**

**(c) MCP wrapper.** `docs/research/R9-audience-findings.md` §B4 "Smallest-surface-change
proposal": three thin tools — `generate()` (wraps `process_json_request()`/`generate_json()`,
`llm/generate.py:649-653`, already callable headlessly), `render_check()` (wraps
`bench/render_oracle.py:17-37`'s `measure()`/`features()`/`tail()`, currently a dev/CI
script with no external caller), and `error_reason()` (documents the existing closed `reason`
enum, `llm/generate.py:494`, as a typed output field — no new code). Explicitly scoped by
R9 as *"additive, not architecture-changing"* and deliberately **not** wrapping export until
PF-053 closes (R9 §B4: *"shipping a broken export tool to an external agent would be worse
than not shipping one"*). **~1 person-week**, including a real smoke-test with an actual
external agent (Claude Code or similar), not just unit coverage of the wrapper.

**(d) Publish the repair corpus + render oracle as a citable benchmark.** The artifacts
(`bench/corpora/repair_corpus_20260830.json`, CC-BY-4.0; `bench/repair_ab_repro/`, MIT) are
already open per `docs/research/R7-publication-assessment.md` §4. But R7 §4 lists real,
specific gaps before this is submission-ready: no human-perceptual corroboration of the
oracle's defect classes; single-model/prompt-family corpus with no cross-model baseline; no
control/baseline defect rate against human-written Faust; the "25-40pp" figure needs a
stated, defensible computation method; oracle precision/recall is uncharacterized against a
hand-labeled set; the faust-rs withdrawal episode needs formal methodology write-up, not just
a commit message. R7 §2 also found **no venue currently has both topical fit and an open,
dated deadline** — DAFx27/ICMC27/LAC27 CFPs are unannounced, NIME 2027's CFP is "promised
October 2026" (i.e., possibly opening imminently), ISMIR 2026 LBD and ADC Bristol 26 talks
have already passed. This is a genuine, separable research-track cost: cross-model
regeneration + a human-written-Faust baseline corpus + oracle precision/recall
characterization + actual paper writing against NIME's realistic 2000-word short-paper tier
(R7 §4's own conclusion — DAFx's 8-page full-paper-only bar is a mismatch for the current
evidence) is **3-6 person-weeks**, separable from (a)-(c) and not blocking them.

**Total engineering core (a-c): ~3-5 person-weeks** (~1 month). **Publication track (d):**
an additional, optional 3-6 weeks, not required for (a)-(c) to ship.

### What must be true first

- (a) and (c) have no hard dependency on each other or on PF-053.
- PF-053 must close before any export-via-MCP tool is added (R9 §B4 is explicit on this) —
  but the 3-tool wrapper proposed does not include export, so this is a *future* dependency,
  not a current blocker.
- The publication track (d) does **not** need PF-079 fixed first — unlike H1, where the
  marketing claim needs the bug actually gone, the paper's contribution *is* the
  methodology and the as-found defect (R7 §3's draft abstract is built entirely around
  PF-079 as a still-open finding). Fixing it first would remove, not strengthen, the
  paper's central result unless reframed as a before/after study.

### Kill criteria

If, after shipping the MCP wrapper and making it genuinely discoverable (posted, mentioned
to agent-tooling communities), no real external-agent usage materializes within roughly
4-6 weeks, the core bet — "an agent reaches for this" — is unconfirmed, and the publication
track especially should pause rather than compound investment in an unconfirmed lane.
Separately: if `clap-juce-extensions` integration surfaces blocking incompatibilities with
the current `UiIr`/`GeneratedFaceLookAndFeel` rendering path (not expected per R9's "no
plugin-code changes needed" framing, but unverified against this project's specific UI
substrate) — stop and reassess before sinking further time into CLAP specifically.

### Strongest counterargument

Per `docs/research/R9-audience-findings.md` §B4's own verdict: this lane is **"occupied-
but-narrow-opening-exists,"** not open. ConjureDSP (since ~April 2026) and Amorph (since
2026-09-22) both already ship MCP servers with materially broader capability — live,
tool-based editing of a *running* patch — than the 2-3-tool, one-shot wrapper proposed here
(Incant's wire contract is request/response, `docs/decisions.md` ADR-011; it has no notion
of a running patch an agent edits live, per R9 §B4's own closing line). The one genuinely
unduplicated piece — render-check as an agent-callable safety gate — is real but narrow.
Spending ~1 month of engineering plus an optional 1-1.5 months of publication work risks
building a platform for a market that, per the project's own prior research, is already
served by better-funded, more-capable competitors — and H2's "compatible with unchanged
architecture" virtue cuts both ways: it is low-risk precisely because it is low-commitment,
diffusing effort across four semi-independent work-items instead of finishing any one of
them as a decisive bet.

---

## 3. H3 (COLD ESTIMATE, adversarial) — "Linux/OS for music; build-your-own-DAW from NL"

### MAGDA may already be this

`docs/competitive_landscape.md:157-192` (Threat #9, Moat #4's caveat): MAGDA is free,
open-source (GPL-3), Linux-native, built on JUCE + Tracktion Engine, 933 commits / 234
stars / 26 forks (verified live 2026-10-12), with **confirmed** NL control of tracks/
clips/notes/plugin-assignment and **reportedly** (two independent secondary sources, one
primary doc page not corroborating) a Faust device whose AI panel vibe-codes effects from a
prompt. If that second claim holds, MAGDA already combines NL DAW control and NL Faust-DSP
generation — in Faust, Incant's own engine — in one free, open-source, Linux-native tool.
That falsifies the premise this hypothesis would otherwise rest on ("nobody combines NL DAW
control and NL plugin generation"), and it changes the cost/strategy question entirely: if
true, H3 is not "build a new category," it is **"catch up to an existing open-source
competitor with a real (if thin, per R9 §B2 §5's KVR-thread finding: "cautiously positive,
low-volume") community already in motion."**

### The user already tried this, and dropped it — what's known and what isn't

`/home/losera/PluginForge-backup-20260804-185851/IDEAS.md:4-10` (the user's own words, Aug
2026, read directly): six numbered ideas moving from "can an agent orchestrate compositions
on a DAW interface" through "a multi-modality... integrated system of musical feedback and
then plugin construction" to "an agent-oriented music design grammar... like sonic pi but
where its multi-modality is created through an integrated system of musical feedback and
then plugin construction" — and the file trails off mid-sentence at idea six: *"Musical
generation is not necessarily best created"* with no completion. **There is no stated reason
for why this was dropped anywhere in the file, and I am not going to invent one.** What is
independently visible is that the project that was actually built (PluginForge, initial
commit 2026-07-21) scoped down to a single-plugin NL→DSP generator from day one — it never
has a transport, timeline, or multi-track concept anywhere in its architecture
(`host/CMakeLists.txt`: `FORMATS VST3 Standalone` only, per `CLAUDE.md` Stack section). That
the narrower scope is what got built is a fact; whether that was a deliberate "this was too
big, start smaller" call or something else is a real unknown, not a gap to paper over.

### Capacity reality check

One developer, agent-leveraged (§0). Current state: 15 open defects (`docs/BUGS.md`,
confirmed count), a broken `/export` (PF-053, `docs/BUGS.md:91`), no AU or Windows target at
all (`CLAUDE.md` Stack section: "VST3 + Standalone output only... there is no AU or macOS
target"), and — the load-bearing fact — the project's own single-plugin flagship use case
still cannot make a verified "it sounds right" claim: PF-079 (`docs/BUGS.md:117`) measures
render-safety 25-40 percentage points below compile rate, with the fix "not yet landed."
Building a full DAW layer (tracks, timeline, mixer, clip launching, transport) on top of
this is a categorically larger engineering surface than anything built to date. MAGDA's own
architecture is instructive here: it is built on **Tracktion Engine**, an existing, mature
library that supplies tracks/timeline/mixer/clip infrastructure — MAGDA's maintainers did
not build that from scratch either. A PluginForge pivot toward the full ask would face the
same choice (adopt an existing DAW-engine library — itself a major new dependency decision,
architecture-gated per AGENTS.md §4 — or build transport/timeline/clip/mixer from scratch),
and nothing in this project's own history gives a base rate for either path, which is why
this estimate is explicitly colder than H1/H2's.

**Full "music OS" ask, cold estimate: 6-18 months, plausibly longer, for a minimally
competitive single-developer MVP** — adopting an existing engine to shortcut transport/
timeline, still needing original track/mixer UI, clip launching, and a DAW-level NL-control
translation layer analogous to MAGDA's own CFG-constrained-decoding DSL step
(`docs/competitive_landscape.md:168-170`), while retaining the existing Faust generation
layer as the "plugin construction" half. This range should be read as genuinely uncertain,
not conservatively padded — there is a real chance it is an underestimate, because there is
no comparable prior work in this repo's history to anchor it against.

### The 10% version — hunted, as instructed

Incant already generates both effects and instruments from NL (`CLAUDE.md`, "Key
architectural decisions"). The cheap middle ground the task asked to hunt for: **an
NL-composed rack of multiple generated plugins inside the existing single JUCE host** — not
a new DAW, no transport/timeline/clip engine, just a multi-slot signal chain the user
describes in one prompt ("a vintage drum bus: compressor into tape saturation into
reverb"), generated as N Faust patches and run in series inside the one plugin instance
that already exists.

What this would actually require, reusing existing substrate rather than inventing new
categories:
- **LLM-side decomposition.** Extending the existing typed-plan pattern
  (`llm/recommendation.py`, ADR-033) to decompose one NL request into N sub-prompts, each
  generating its own Faust patch — moderate prompt-engineering work, not new infrastructure.
- **Runtime extension.** `FaustEngine` today is architected around swapping exactly one DSP
  instance (CLAUDE.md's "DSP swap protocol" invariant). Hosting N JIT-compiled instances
  with defined serial (and eventually parallel) routing between them is a real, legitimate
  extension to that protocol — not free, and itself a scope increase worth its own sign-off,
  but smaller than inventing a DAW's mixer/routing graph from nothing.
- **UI.** Could plausibly reuse `ParamGridPanel` N times (one section per chain slot) rather
  than designing a new rack UI from scratch.
- **A real constraint collision, flagged rather than hidden:** `CLAUDE.md`'s "pre-allocated
  64-slot parameter pool" is a per-plugin-instance budget today. Whether a 3-effect rack
  gets 64 slots total or 64 per slot is an open design question this document does not
  resolve — it's exactly the kind of detail that would need to be answered before, not
  during, implementation.

**Cold estimate for a first working 2-3-step serial rack MVP: 4-8 person-weeks** — better
anchored than the full ask because it reuses existing architecture (FaustEngine, ParamPool,
the ADR-033 typed-plan pattern) rather than inventing new infrastructure categories, but
still a real architecture-scale change (a new "multi-DSP-instance hosting" component)
requiring human sign-off before implementation starts, per AGENTS.md §4.

### Kill criteria

**For the full ask:** if a follow-up, primary-sourced check of MAGDA's own manual/changelog
(a more targeted version of the fetch R9's researcher already attempted and could not fully
corroborate) confirms the Faust-AI-panel claim, then H3's full version is not "build a new
category," it is "catch up to an open-source incumbent with 933 commits and real, if thin,
community reception already." At that point the honest framing requires a specific reason
Incant would out-execute MAGDA — none has surfaced in this research — and that single
confirmation check should be the actual next action before spending one hour building
anything, not an afterthought.

**For the 10% version:** if a first implementation spike reveals that the 64-slot
`ParamPool` ceiling, the DSP-swap protocol's single-instance assumption, or the render
oracle's single-patch model require a genuine redesign rather than an "add a loop over N
instances" extension — i.e., if it turns out to be a disguised full architecture change —
stop and re-scope. The entire premise of calling this a 10% version is that it is additive
to the existing single-plugin host, not a new DSP-hosting architecture; if that premise
doesn't survive contact with the actual code, the cost estimate above is void.

### Strongest counterargument (both versions)

The project's own prior research already names the single most urgent, unresolved risk as
whether a generated plugin sounds like what was asked for at all — PF-079 measures
render-safety 25-40pp below compile rate, with the fix not yet landed
(`docs/BUGS.md:117`), and `STATUS.md`'s own framing (per `CLAUDE.md`, "One judgment has no
instrument") treats the listening judgment as still open. **Any version of H3 — even the
cheap rack idea — adds scope on top of a product whose foundational single-plugin
guarantee is not yet actually true.** A rack of N patches, each individually capable of the
already-measured +715dB runaway-gain or inverted-`select2` DC-offset failure modes, is
strictly worse than one such patch, not better, until PF-079 is fixed. This is the most
honest reason to defer H3 in any form right now: it is premature relative to the project's
own most basic, already-diagnosed correctness gap — not a reason it's a bad idea forever,
a reason it's a bad idea *before* that gap closes.

---

## 4. Ranked recommendation

**Ranking: H1 > H2 > H3.**

H3 is the weakest bet by a wide margin, adversarially assessed: it is cold, possibly
already-lost against an open-source incumbent with real (if thin) community traction, and —
even in its cheapest 10% form — adds risk on top of an unresolved core-product correctness
gap rather than reducing it. It should not be pursued now in either form, full or 10%,
pending at minimum the MAGDA confirmation check named in its kill criteria and the PF-079
fix named in H1.

Between H1 and H2: H1 is cheaper (~3-6 person-weeks vs. H2's ~3-5 plus an optional 3-6 for
publication), executes the competitive-landscape doc's own already-considered, already-
drafted recommendation ("lead with guarantees, not concept," `docs/competitive_landscape.md:
394-397`) rather than opening a new lane, and targets a gap `docs/competitive_landscape.md`
itself reconfirmed as still open as of this date (no competitor publishes a reliability
number — "Decisive question," lines 231-266) — a stronger, more explicitly-confirmed
opportunity than H2's bet, which the project's own prior research (R9 §B4) already rates
"occupied-but-narrow-opening-exists," not open. H1's PF-079 remediation is also work the
product needs regardless of any positioning decision — it is real-correctness work wearing
a marketing-cost label, which makes it the better place to spend the next several weeks
even setting the competitive argument aside.

**If I had to bet: H1, specifically the PF-079 remediation first (it is needed regardless of
which positioning or licensing call follows), then the licensing decision, then the README
rewrite and AUR package, in that order.**

**The strongest counterargument to this recommendation, stated honestly:** H1 is a
"catch-up-on-credibility" bet for a product almost nobody outside the developer has used —
`docs/competitive_landscape.md:272-275` names the risk directly: every day spent on
internal polish (licensing language, positioning copy, a benchmark fix) is a day a shipping
competitor compounds a real user base and a patch library, and that criticism applies to H1
exactly as much as it applies to any other internal-facing work this project has ever
second-guessed itself over. There is also a real chance the bet backfires on its own terms:
being first to publish a systematic reliability number only helps if that number is
flattering once published, and if render-safety remains meaningfully below compile-rate even
after the two known root causes are fixed — a real possibility, not a strawman, since
`dynamics` is already the worst-performing category on both tested archives and the fix
addresses only the two failure modes found so far — then H1's central differentiator
("measured") could read worse publicized than left unpublished. Both risks are real and
neither is resolved by this document; they are the reason this is offered as a bet for the
human to weigh, not a conclusion.

---

## Sources

All internal citations above are to files in this repository, cited by path and line number
where the content has stable line numbers (`docs/BUGS.md`, `docs/decisions.md`,
`docs/competitive_landscape.md`, `docs/phases/*.md`, the backup `IDEAS.md`) or by named
section where it does not (`CLAUDE.md`, which this session's tooling surfaces without line
numbers). All competitive, licensing, audience, visibility, and publication claims are
sourced to this session's own prior research docs — `docs/research/R6-visibility-ledger.md`,
`R7-publication-assessment.md`, `R8-licensing-options.md`, `R9-audience-findings.md` — rather
than re-researched here, per this task's explicit instruction not to duplicate that work;
section references are given inline above (e.g. "R9 §B4," "R7 §4," "R8 §5") rather than
repeated as a separate bibliography.
