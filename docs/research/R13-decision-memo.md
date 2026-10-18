# R13 — Decision Memo: Market-Fit & Visibility for PluginForge / Incant Audio

**Written 2026-10-14, synthesizing R6–R12 (this session) plus the existing
`docs/competitive_landscape.md`.** This memo makes no architecture or licensing decision —
those are human-gated per `AGENTS.md §4` — it ranks options and states a recommendation as a
bet for the user to accept, modify, or reject. Read R6–R12 for full detail; this is the
one-page version meant to actually get read before the next move.

---

## 1. The refreshed threat model

The question going in was "how do we stand out." The honest answer, after this session, is
narrower: **the field got crowded fast, and standing out is now mostly about not embarrassing
yourself while a narrow, real gap stays open.**

What changed since `docs/competitive_landscape.md`'s last substantive update (2026-08-06):

- **Two more MCP-native agent competitors shipped** — ConjureDSP (since ~April) and, newly
  found this session, **Amorph itself** (since 2026-09-22). The "agent reaches for this" lane
  is more occupied than the project's own prior docs assumed.
- **Audio Plugin Coder (APC)** is open source, agent-agnostic, MIT, ~330 stars, and already
  has CLAP in beta — on the current JUCE 7.0.9 target, via `clap-juce-extensions`, with no
  JUCE 9 upgrade required (B2 finding).
- **MAGDA** is the single biggest new fact: a free, open-source (GPL-3), Linux-native DAW on
  JUCE+Tracktion Engine that reportedly *also* lets a user vibe-code Faust effects from a
  prompt. Two independent secondary sources make this claim; one primary doc page the
  researcher reached did not corroborate it. **Treat as a real, unconfirmed threat, not a
  settled fact** — but if it holds, it falsifies the "nobody combines NL DAW control and NL
  plugin generation" premise behind the "music OS" hypothesis, in Faust, Incant's own engine.
- **A new, previously untracked competitor surfaced incidentally**: Chaos Audio's "AI FX
  Builder" (found by the B3 agent via GRAME's own LLM-guidance page, which lists nine
  production systems combining LLMs with Faust). `docs/competitive_landscape.md` should be
  updated to track it; this memo flags it rather than re-researching it.
- **Moat #2 — "no competitor publishes reliability data" — still holds**, confirmed
  independently by two separate research passes (A1's competitive refresh and A3's academic
  survey). One competitor (Sonorie) now markets an unsupported reliability claim with no
  methodology — the *rhetoric* of measurement is entering the market before the *evidence*
  does. Whoever publishes a real number first still owns this gap.
- **Press has started gating, and the gate is a license, not an engineering standard.** Synth
  Anatomy's October 2026 policy shift names exactly one concrete, actionable lever for full
  coverage: "open-source the code, etc." Everything else (UI quality, "genuine innovation") is
  impression, not a checklist (A2/R6 finding — corrects the task brief's "5-point checklist"
  framing, which overstated how formal the policy actually is).
- **Licensing is not urgent.** JUCE's GPL/AGPL obligation triggers at public distribution, not
  development; PluginForge has never done a public release. Faust/libfaust is not the limiting
  license at all (LGPL 2.1 + an explicit exception already lets the host binary be distributed
  "under terms of your choice"). Going open-source is a genuine choice available at low cost,
  not a forced or urgent one (A4/R8 finding).

**Net: the competitive landscape didn't get friendlier, but the one differentiated, unoccupied
claim this project could make — measured reliability — is still unclaimed by anyone else.**
That is the one piece of good news this refresh turned up, and it is conditional on actually
being true, which is where the claims audit matters.

---

## 2. What Incant can honestly say today (from R10's claims ledger)

The single most important finding of this entire session is procedural, not competitive:
**this project has an unusually strong habit of catching its own overclaims** (the faust-rs
causal-claim withdrawal, the "arm A's zero-visibility claim was itself an overclaim"
correction) — and R10's audit found several more that haven't been caught yet. Fix these
before any public move, regardless of which hypothesis gets pursued.

**Safe to say today:**
- NL → Faust → LLVM JIT → VST3/Standalone, Linux x86-64. (Accurate, consistent across docs.)
- A compiler-error retry loop (up to 3 attempts) with classified error feedback exists and runs.
- A dedicated signal-level safety stage (`OutputGuard`) checks NaN/Inf/DC/runaway before output
  — described factually, without the word "safe" as a blanket guarantee.
- The self-correction pattern itself (two public walk-backs of its own prior claims) is a true,
  differentiating claim about process.

**Do not say yet:**
- Anything implying the output "sounds good" or "matches the prompt" — **zero instrument
  exists for this claim**, full stop, per `CLAUDE.md`'s own admission. This is the single
  highest-risk gap: the first person who tries the product and plays a chord critically will
  falsify an unqualified version of this.
- "Safe" / "production-ready" unqualified — PF-079 is open (render-safety runs 25–40pp below
  compile rate), plus six other open defects touching correctness and UI-state integrity.
- "Polyphonic synthesizers" (README) — confirmed false against the actually-monophonic engine.
  **Fix this specific line before any demo video, press pitch, or Show HN post that lets
  anyone play a chord.** This is the cheapest, highest-consequence fix in the whole session.
- Any specific compile/retry/render-safe percentage — real numbers exist but are stale (predate
  two shipped prompt fixes) and underpowered (n=1 against the project's own n≥3 bar).
- "VST3/AU" (STATUS.md) — no AU target exists; a sibling instance of this exact string was
  already fixed elsewhere and this one was missed.
- Anything about binary size/LLVM linkage sourced from ADR-001 — it describes a static-link
  build that isn't what's actually shipping (dynamic link against system `libfaust.so`).

---

## 3. Ranked visibility sequence — cheapest credible moves first

Pulling the single best move from each relevant lane (R6, R9 §B2, R9 §B4):

1. **Fix the README's "polyphonic synthesizers" claim.** Near-zero cost, removes the single
   most falsifiable public claim in the project. Do this regardless of everything else below.
2. **Write a Hügelton-style AI-disclosure sentence** on the public GitHub README — the facts
   are already true and already written in `CLAUDE.md`; this is publishing, not engineering.
   Clears the disclosure norm at Synth Anatomy, KVR forum, and Gearspace without touching the
   license.
3. **Publish an AUR `-bin` package.** One PKGBUILD, the maintainer's own daily-driver distro,
   zero comparators in the competitive set have done this (unverified beyond MAGDA specifically,
   per R9 §B2 — MAGDA has neither AUR nor a completed Flathub listing either).
4. **Record one demo video of the existing, already-logged QWERTY→heard-notes session**
   (`docs/sessions/017-phase2-interactive-host.md`) and post it to Show HN with the
   self-correction story told plainly. R6's evidence from three real 2025–2026 Show HN cases
   says a working demo + evidence-backed, non-overclaiming prose beats both polish and open
   source as the decisive factor there — and "we publicly withdrew our own finding, twice" is
   unusually strong, rare material for that specific audience.
5. **List on KVR's Product Database; post once on the KVR forum and Gearspace's New Product
   Alert**, leading with the Linux/Faust/JIT distinctness plus the disclosure line from #2.
6. **Target NIME 2027's short-paper tier once its CFP opens** (promised this month per R7) with
   the PF-079 finding — but only after PF-079 is actually fixed or explicitly reframed as a
   before/after study (see §4 below; R7 notes the paper's contribution doesn't require the fix
   to land first, unlike the marketing claim does).

Items 1–3 cost essentially nothing and should happen regardless of which pivot hypothesis is
chosen. Item 4 is the one with the best credibility/cost ratio for the user's actual personal
goal (hiring-manager/DSP-peer visibility, per the original framing of this session).

---

## 4. The three hypotheses, costed (full detail in R11)

| | H1 — Best-in-class OSS NL→DSP | H2 — Platform/SDK | H3 — Music OS / build-your-own-DAW |
|---|---|---|---|
| **Cost** | ~3–6 person-weeks | ~3–5 weeks core + optional 3–6 weeks publication | Full ask: 6–18+ months, genuinely uncertain. 10% version (NL-composed rack in the existing host): 4–8 weeks |
| **Dominant cost driver** | Fixing PF-079's two root causes (the only part that's real correctness work, not positioning) | MCP wrapper + CLAP + fixing `/export` (PF-053) — all independent, none blocking | DAW-engine infrastructure (transport/timeline/mixer) nothing in this project's history calibrates against |
| **What must be true first** | A human license decision (GPLv3 vs. open-core); PF-079 fixed *before* any number is published | Nothing blocks the core three tools; export-wrapping waits on PF-053 | MAGDA's AI-Faust-panel claim needs a primary-source confirmation check *before* spending an hour building anything |
| **Kill criterion** | If render-safety stays materially below compile-rate after the two known fixes land, stop publishing a reliability number | If no real external-agent usage materializes in 4–6 weeks of the MCP wrapper being live, the core bet is unconfirmed | If MAGDA's claim is confirmed, H3 isn't "build a new category," it's "catch up to an incumbent with 933 commits" — reframe or drop |
| **Strongest counterargument** | A month of internal polish while Amorph compounds a real user base; the bet only pays off if the eventual number is flattering | Already rated "occupied-but-narrow-opening-exists" by the project's own prior research (R9 §B4) — not a wide-open lane | Adding scope on top of a product whose foundational single-plugin correctness claim (PF-079) isn't true yet is premature in any form |

**Ranking: H1 > H2 > H3.** H3 should not be pursued now in either form — not because it's a bad
idea forever, but because it is premature relative to a correctness gap the project has already
diagnosed in its existing, much smaller surface area. The MAGDA confirmation check is cheap
(one targeted fetch) and should happen before this is revisited, regardless.

**If forced to bet: H1, in this order** — PF-079 remediation first (it's needed regardless of
any positioning decision; this is real correctness work wearing a marketing-cost label), then
the license decision, then the README/positioning rewrite and the AUR package.

**The honest counter to that bet**, stated once rather than re-litigated per hypothesis: every
week spent on H1's internal cleanup is a week Amorph and MAGDA compound real user bases this
project does not have. The bet is not "H1 is risk-free" — it's that H1 buys the cheapest
credible claim available, and that claim (measured reliability) still targets a gap nobody else
has filled, which H2 and H3 do not offer with comparable confidence today.

---

## 5. Explicit kill list — stop doing these

- **Stop implying "polyphonic" anywhere user-facing** until real multi-voice allocation exists,
  not just overlapping release tails.
- **Stop treating ADR-001's "~30MB, must link libfaust+LLVM" as a true description of the
  build** — it describes a static-link architecture the project doesn't use.
- **Stop citing the 2026-08-31 efficacy numbers as current** — they predate two shipped prompt
  fixes and don't meet the project's own n≥3 bar. Either re-run them (free-tier, no paid call
  needed — PF-031 is already in flight) or stop citing them.
- **Stop treating "safe" as a one-word, unqualified public claim** while PF-079 is open.
- **Stop pursuing H3 in any form** until the MAGDA confirmation check lands and PF-079 is fixed
  — in that order, not in parallel with either still outstanding.
- **Stop assuming the agent/MCP lane is wide open** — it's occupied by two shipped competitors;
  the real, unduplicated opening there is narrow (render-check as an agent-callable safety
  gate), not the headline "agent writes your DSP" capability.

---

## 6. What still needs the human specifically

Per `COLLABORATION.md §1`, one judgment has no instrument and never will by writing more docs:
**whether a generated plugin sounds like what was asked for.** R12's session kit is ready —
8 pre-screened candidates, a REAPER runbook adapted from the existing session-017 procedure, and
a scorecard that takes under a minute per patch. Nothing else in this memo matters as much as
that session actually happening; every "measured" claim this project might want to make is
downstream of it.

One live, previously-untracked finding surfaced while preparing that kit, flagged here rather
than acted on: a compressor patch (`dynamics-01/L3`) records `first_try_compiles: true` in the
committed archive but renders completely silent when independently re-checked today — a fresh
instance of the PF-079 pattern not currently named in that bug's `docs/BUGS.md` entry. Worth a
look before the next efficacy re-run.

---

## Sources

This memo synthesizes, without re-deriving: `docs/competitive_landscape.md` (updated this
session), `docs/research/R6-visibility-ledger.md`, `R7-publication-assessment.md`,
`R8-licensing-options.md`, `R9-audience-findings.md`, `R10-claims-ledger.md`,
`R11-pivot-costs.md`, `R12-session-kit.md`. See each for full citations and file:line evidence.
