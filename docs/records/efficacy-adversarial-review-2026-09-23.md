# Prompt-efficacy study — adversarial review, 2026-09-23

Point-in-time record. Not maintained after today — see COLLABORATION.md §8 ("a document that
records a point in time must carry a date and then stop changing"). Written against
`docs/prompt_efficacy_study.md`, `bench/run_efficacy_study.py`, `bench/score_efficacy.py`,
`bench/prompts/tiered_prompts.json`, and the shipped-model archives (`efficacy_groq_20260831.json`,
`efficacy_ollama_20260828_judged.json`, `efficacy_groq_dynamics_postfix_20260909.json`,
`pilot_20260720_pf041042_recheck_judged.json`), judged from the standpoint of an LLM-evaluation
methodologist, a Faust/DSL-compiler engineer, and a domain skeptic about what any of this proves.

**Headline, ahead of the detail:** the study's own metric — `faust -lang cpp` accept/reject —
overstates real success by **25 to 40 percentage points** once the compiled output is actually
rendered and measured. STATUS.md's "no monotonic tier gradient (L2–L4 flat ~84% first-try)"
reads as a robust, near-ceiling result; the same archive's render-safety rate is **56–68%** at
those same tiers, with concrete NaN/Inf, four-digit-dB runaway gain, and DC-offset failures
sitting inside what the compile gate calls success. Separately: the task sitting in STATUS.md's
reserved evidence slot — "re-run the groq grid at n≥3" — **cannot be executed today, at any
budget**, because the harness's own duplicate-key guard rejects a second sample at the same
cell. Both are demonstrated below, not asserted.

---

## Pass A — LLM-evaluation methodology

### A1/A2 — wrong estimator, wrong test, for a design that is already paired

`tiered_prompts.json` is a matched-set design by construction — all 25 effects appear at every
tier (`docs/prompt_efficacy_study.md:36-38`) specifically so tier deltas are attributable to
phrasing, not effect difficulty. `score_efficacy.py` nonetheless reports tier rates as if they
were independent binomials with no paired test between them. The correct tool — exact McNemar on
discordant pairs — already exists in this repo (`bench/score_repair_ab.py:135-141`,
`mcnemar_exact`) for exactly this reason. This review reuses it rather than re-deriving a second
implementation: `bench/score_efficacy_render.py::pairwise_tier_tests` imports it directly.

**Run against both real archives** (render-safety, matched by `effect_id`, all ten tier pairs,
each archive independently): groq's largest discordant split is L3-vs-L2
(`L3_only=0, L2_only=3, p=0.25`); every other groq pair returns `p ≥ 0.375`. Ollama's tightest
split is L2-vs-L0 (`p=0.0625`); every other pair returns `p ≥ 0.21`. **Zero of the twenty
pairwise comparisons across both archives clears p<0.05.** Read plainly: on render-safety,
neither archive can currently distinguish any tier from any other tier, statistically — a
materially different statement than "no monotonic tier gradient," which reads as a positive
finding rather than an absence of power.

### A3 — cell-level and category-level claims are under-powered, and one is on record uncorrected

PF-031's own measured noise floor (`docs/BUGS.md:2141-2239`) is a 4-point rate spread on 25
trials from an *unchanged* prompt and tree — the arithmetic (binomial SE ≈ 7pp at p≈0.85) was
optimistic; the true floor was worse. At n=5 per tier×category cell the SE is materially larger
still. **"A category-level `dynamics` weakness at every tier"** (`STATUS.md:457-458`) is a
subgroup claim read off 5-cell counts per tier, with no stated multiplicity correction across 25
(cell) × 5 (error-class) comparisons. This review's own render-safety data gives the claim a
partial rescue on different grounds (§ B1: dynamics fails render-safety at 12/19, 63% — the
worst category by a real margin) — but that is evidence for a *different* claim (render safety,
not first-try compile rate) than the one on record, and should be cited as such rather than
folded into the original.

### A4 — provider variance is not model variance, and n≥3 on groq would conflate them

PF-031's amendment (`docs/BUGS.md`, cited in `docs/prompt_efficacy_study.md`'s own confound
section) already established: groq at temperature 0 gives 88%/84% on an unchanged tree with only
partial failure-set overlap, while local ollama gives *identical* failure sets and 20/25
byte-identical generations. **Any n≥3 run on groq (the shipping provider, and the one PF-031's
evidence slot names) estimates a mixture of model uncertainty and provider-sampling noise, not
model uncertainty alone.** The study should say which quantity it is reporting, or run the
repeat-sampling arm on the near-deterministic provider (ollama) and treat groq's spread as a
separately-quantified nuisance term.

### A5 — judge self-evaluation, unresolved

PF-041 already fixed the tautological version (`score_efficacy.py:42-46` — L4 scoring 2.00/2.00
against its own text). The remaining exposure: on the groq archive the judge would be
`gpt-oss-120b` grading `gpt-oss-120b`'s own output. Not measured this session (would need a
judge pass, out of scope — no model calls). Disposition: **open, flagged, not run.**

### A6 — the retry loop's product-fidelity is asserted, not re-checked

`run_efficacy_study.py:11-19` documents that the original confound-control rationale
(`docs/prompt_efficacy_study.md:49-52`) was *reversed* after the bench and product prompts
diverged and reconverged. Whether "retry-corrected" in the current archives still means what
`llm/generate.py`'s live retry loop does was not independently re-verified this session — noted
as an assumption carried forward, not a new defect, since the harness's own docstring already
discloses the history.

### A7 — rate limits belong in a censoring bucket, not the failure bucket (fixed, this session)

`bench/score_efficacy_render.py::is_censored` now excludes `terminal_reason == "rate_limited"`
records from both the compiled and render-safety denominators, mirroring the precedent already in
this repo (`bench/repair_ab_repro/METHODOLOGY.md` WP2 — `terminal_reason` classification,
excluded not failed). Measured effect on the groq archive: **2 of 125 records** are censored this
way (both L1). `score_efficacy.py` itself still counts these as ordinary failures — a small,
disclosed, one-tier bias in the current headline numbers, not corrected here (that file's own
evidence bar, not this review's scope to edit without a Tier-2 report of its own).

### A8 — tier-rule leakage, audited mechanically for the first time (this session)

Named as *the* internal-validity threat since 2026-07-21 (`docs/prompt_efficacy_study.md:42-44`)
and left to a human spot-check that, as far as this review can find, never happened. Built and
run: `bench/tier_leakage_screen.py`, an outcome-blind lexicon screen (derived from the tier rules'
own worked examples and each effect's own `category`/`target` fields — see that module's
docstring for exactly why this order matters). Frozen in `tests/test_tier_leakage_screen.py`.

**Result on the committed dataset:**
- **L2 ("no parameter jargon"): 0/25 violate.** (First pass flagged 9/25 on "knob" — reverted
  after a hand-read showed every hit was `docs/prompt_efficacy_study.md`-compliant L2 vocabulary
  ("a simple volume knob"), not jargon; see the module's own comment for the false-positive
  lexicon entries tried and dropped.)
- **L1 ("no category or parameter"): 3/25 flag.** Hand-read verdict on each:
  - `generative-02`: **real leak** — the L1 text literally says "a bright sharp **synth**
    note," naming the effect category outright.
  - `filters-05`: likely false positive — "fresh **air** and light around it" uses the word in
    its ordinary sense, not as the `air`-band EQ term.
  - `generative-05`: likely false positive — "like touching a real **string**" is the sensory
    metaphor itself (a physical guitar/harp string), not a disguised category name.

**Disposition:** one confirmed leak out of 125 L1/L0 cells (0.8%) is not enough to explain the
L1 trough on its own (48–50% first-try vs. 84–92% elsewhere) — but it is one data point the
study's own H1/H2 hypothesis testing never accounted for, and the screen is now there to check
again if `tiered_prompts.json` is ever edited.

### A9 — provenance gaps in the record schema

Records pin `provider`/`model`/`prompt` but neither a system-prompt content hash nor the Faust
version. `llm/prompts/system_prompt.txt` has already drifted under a live baseline once this
quarter (`STATUS.md:465-472`), and the dev box (2.85.9) and CI (2.70.3+ds, per the AOT-emit
work this session found on the same branch) run different Faust versions. Not fixed this
session — folded into the schema-change ADR draft below (F1/2e), since it is the same "add a
field, bump the contract" decision.

### A10 — `render_oracle.corpus_main()`'s own selection silently drops every retry rescue

Discovered while wiring Track 2 (not previously on record anywhere): `corpus_main()` filters on
`first_try_compiles` only (`render_oracle.py:1050`) — correct for its actual caller
(`bench/ladder_corpus.json`, a frozen first-try-only fixture) but wrong for an efficacy archive,
where `retry_success` records are exactly the ones the product ships. Measured: **21 of 125**
records in `efficacy_groq_20260831.json` have `terminal_reason=="compiled"` with
`first_try_compiles=False` — genuine retry rescues that a naive `--corpus` render of this file
would silently never check. `bench/score_efficacy_render.py::is_compiled` selects
`first_try_compiles or retry_success` instead, calling the same `render_oracle.analyse_corpus()`
driver rather than duplicating its gate logic.

---

## Pass B — Faust / DSL-compiler semantics

### B1 — the oracle in daily use answers the wrong question, and this is now measured end to end

`faust -lang cpp -o /dev/null` is front-end accept/reject: syntax, types, arity. It has no
opinion on whether the resulting signal graph is stable, silent, DC-free, or bounded — the
generative-Karplus-Strong regression already on file (+79.6 dB runaway, DC 2.35, peak 4541,
`docs/BUGS.md:828-832`) is exactly this failure mode, previously known only as one anecdote. This
review renders **every compiled patch in the four committed archives** through
`bench/render_oracle.py` (real signal statistics: NaN/Inf, silence, runaway gain, peak, DC
offset, plus the burst-probe tail check) via the new `bench/score_efficacy_render.py`, at $0 and
zero model calls.

**Groq (shipping model, 125 cells) — compiled vs. renders-safely:**

| tier | n | censored | compiled | unsupported | render-safe | render-safe rate |
|---|---|---|---|---|---|---|
| L4 | 25 | 0 | 23 | 4 | 17 | **68%** (89% of judged) |
| L3 | 25 | 0 | 24 | 5 | 14 | **56%** (74% of judged) |
| L2 | 25 | 0 | 24 | 5 | 17 | **68%** (89% of judged) |
| L1 | 25 | 2 | 21 | 4 | 16 | **64%** (94% of judged) |
| L0 | 25 | 0 | 22 | 5 | 16 | **64%** (94% of judged) |

("of judged" excludes the structurally-unmeasurable generative/unsupported cells from the
denominator — see B4. Either reading sits well below the 84–96% first-try/retry-corrected
figures on record.)

**By category (groq):** dynamics 12/19 render-safe (63%) — the worst by a clear margin; filters
20/24 (83%); time-based 23/24 (96%); trivial 24/24 (100%); generative 1/23 judged, 22/23
structurally unsupported (see B4).

**11 compiled-but-unsafe patches in this one archive**, verbatim reasons from the oracle:
silence (5: a de-esser, a compressor, a limiter safety wall, a noise gate, one more limiter),
runaway gain (4, one at **+715 dB** and peak ~4×10³⁵ — a numeric-overflow-adjacent expander;
another at +426 dB on a sidechain compressor; another +278 dB on a band-pass filter), NaN/Inf (1,
a band-pass filter), never-decays / DC-offset limiters (2, both brick-wall limiters — the same
effect concept fails independently on both the groq and ollama archives, see below).

**Ollama archive (125 cells, independently generated by a different model,
`qwen2.5-coder:7b`):**

| tier | n | compiled | unsupported | render-safe | render-safe rate (of judged) |
|---|---|---|---|---|---|
| L4 | 25 | 23 | 3 | 15 | 75% |
| L3 | 25 | 23 | 4 | 17 | 89% |
| L2 | 25 | 21 | 3 | 14 | 78% |
| L1 | 25 | 21 | 1 | 17 | 85% |
| L0 | 25 | 22 | 2 | 19 | 95% |

By category: dynamics 14/21 (67%); filters 18/23 (78%); generative 5/8 judged (13/21
structurally unsupported — an even larger unsupported share than groq's, see B4); time-based
24/24 (**100%**); trivial 21/21 (**100%**). 15 compiled-but-unsafe patches this archive, the same
mix of silence / runaway-gain / DC-offset / never-decays. Pairwise McNemar: same picture as
groq — the tightest split is L2-vs-L0 at `p=0.0625`, still short of 0.05; every other pair
`p ≥ 0.21`.

**Two specific failures recur on BOTH archives, independently generated by two different
models** — the strongest evidence in this review that these are prompt/DSP-idiom regressions,
not one model's bad luck:
- **"Brick-wall limiter for mastering"** — DC offset (~0.95–0.98) and never-decays (loop gain at
  or above unity) on groq (L3) *and* ollama (L4). The same effect concept, the same failure
  signature, two unrelated generators.
- **"Upward expander"** — runaway gain (+715 dB groq, +46.6 dB ollama) with a peak far outside
  plausible range on both. The magnitude differs by an order of magnitude, the failure mode does
  not.

Time-based and trivial are the only two categories with a **100% render-safe rate on both
archives** — the strongest evidence that the render oracle is not simply noisy or over-strict; it
discriminates cleanly between categories the compile gate treats identically.

**Pilot archive (filters + generative, N=37 compiling):** 17 passed, **3 failed — all NaN/Inf,
all three the SAME target** ("air"/high-shelf-EQ effect, at three different tiers: L2, L1, L0),
17 structurally unsupported (generative). A third independent confirmation that a specific
DSP idiom — not a random one-off — reliably produces unsafe output regardless of which tier
prompt asked for it.

**Dynamics postfix archive (25 cells, post-PF-024-fix re-run):** 17 compiled, 13 render-safe, 1
unsupported, **3 unsafe**: the same brick-wall-limiter DC/never-decays failure (L3), an upward
expander that renders NaN/Inf (L3), and a "surface the ghost notes" prompt (L1) that also renders
NaN/Inf. The PF-024 fix this postfix run was measuring (routing_arity / `_,_)` syntax) is
confirmed still working — none of these three failures are that class — but the render layer
finds three *different* live defects the compile-rate re-measurement had no way to see.

**Disposition: this is the single most consequential finding in this review.** The compile-rate
metric the study and STATUS.md report is not wrong on its own terms, but it answers "did the
compiler accept it," not "does it work," and the gap between those two questions is large,
reproducible across two different generator models and three different archives, and concentrated
in exactly the category (`dynamics`) STATUS.md already flagged as weak for an unrelated reason.

### B2 — `expected_primitives` substring matching is not a semantics check

`fi.resonlp|fi.lowpass|moog|fi.svf` matches a patch that names the primitive in an unused binding
never routed into `process`. The semantically sound form is reachability in the *propagated*
signal graph. Both compilers can produce this text: C++ Faust's `-sg` aux output (the untracked
`examples/lowpass.dsp-sig.dot` in this repo is exactly this, produced by a manual local run —
pointer-keyed, not diffable across recompiles) and faust-rs's `--dump-sig-dag` /
`--dump-sig-dag-prepared` (stable-id, added to faust-rs *after* the tag this project pins — see
`docs/records/faust-rs-delta-2026-09-23.md`). Not built this session (would require a signal-graph
reachability walker, real new code, out of the "no model calls, harness only" budget) — recorded
as the concrete next step, not a vague aspiration: whoever picks this up should reach for
faust-rs's DAG dump specifically, because it is diffable and the C++ dump is not.

### B3 — the error taxonomy is hand-patched regexes over stderr, and faust-rs offers a versioned check

`ERROR_CLASS_RULES` was already patched once after a real gap surfaced mid-pilot
(`docs/prompt_efficacy_study.md:180-184`). faust-rs's `FRS-*` codes are a stable, versioned
classification of the same failure space — cross-tabulating the C++-stderr-substring class
against the FRS code over the 15 archived never-compiled cells was already done as part of
`bench/frs_rederive.py` (re-run this session at three faust-rs builds, see the faust-rs delta
record): all six `routing_arity` failures map 1:1 to `FRS-PROP-0002` on every build tested.
Extending this to a full corpus-wide cross-tab (all error classes, all compiled-and-unsafe render
failures too) is a natural Track-2 follow-on, not done this session for time.

### B4 — generators are not a small edge case, they are 15–18% of every archive, silently unmeasured

`render_oracle` structurally cannot judge a zero-input patch (`UnsupportedPatch`,
`render_oracle.py:186-208`). Measured share this session: **22 of 125** groq cells (17.6%), **17
of 37** pilot cells (46% — the pilot deliberately over-samples generative), **1 of 25** dynamics
cells. This is not new information (the oracle's own docstring names it), but it has never before
been rolled into a tier table with an actual count next to it. Any headline that reports a single
render-safety percentage for a whole archive without naming this exclusion is making a claim
about 82–100% of the data while silent on the rest — which is exactly the "absence of a claim
must not read as a passed claim" discipline `render_oracle.py` itself already enforces
internally; this review's tables (§ B1) keep `unsupported` broken out for the same reason.

---

## Pass C — construct validity (the one that outranks the rest)

Every metric in this study and in this review — compile rate, render-safety, `expected_primitives`
substring match, LLM-judge score — is a proxy. `COLLABORATION.md:31-48` states the actual
question plainly: *"One judgment has no instrument: whether a generated plugin sounds like what
was asked for."* Render-safety (§ B1) is a strictly better proxy than compile rate — it catches
things a human would call a bug on first listen — but it is still not that judgment. A patch
that renders with no NaN, no DC, no runaway gain, and stops when the input stops can still be the
wrong filter shape, the wrong dynamics curve, or an ugly-sounding one.

The honest framing, already drafted elsewhere in this repo and worth repeating here because it
outranks every number above it: `docs/research/R5-publishable-run.md:1-8` — **the contribution of
this whole apparatus is the evaluation instrument itself**, not a claim about how musicians talk
to machines. This review extends the instrument (render-safety, paired stats, a leakage screen,
a faust-rs provenance guard); none of it substitutes for `docs/p6_test_battery.md`'s listening
pass, which has not been run against either the groq or the ollama 125-cell grid.

---

## What changed this session vs. what is still owed

**Landed, committed, tested:**
- `bench/score_efficacy_render.py` + `tests/test_score_efficacy_render.py` (10/10 passing) — the
  render-level rescorer, with a shared render cache (an early cache-less version timed out at 10
  minutes on the groq archive; documented in the module itself as a "seen failing" note).
- `bench/tier_leakage_screen.py` + `tests/test_tier_leakage_screen.py` (5/5 passing) — the A8
  leakage audit, its lexicon corrected in view (knob/slider false positives removed and recorded,
  not silently dropped).
- `bench/frs_build_compare.py` — the committed, reproducible form of the faust-rs cross-build
  accept/reject comparison (§ faust-rs delta record).
- `bench/repair_ab_repro/Dockerfile` — re-pinned to a commit, guarded by flag presence instead of
  a version string that cannot distinguish builds (verified both directions against real
  binaries; see the faust-rs delta record for why the full `docker build` itself hit an
  unrelated, pre-existing Arch `faust` version drift instead).

**Explicitly not done, and why:**
- **No model call of any kind** — the n≥3 groq re-run PF-031 asks for was not attempted, and
  could not have succeeded regardless (see F1 below).
- **B2's signal-graph reachability check** — real new code, out of this session's harness-only
  budget.
- **A5's cross-family judge pass** — spends quota, explicitly out of scope.
- **The schema ADR (F1/2e)** — drafted as a proposal, not committed; a contract-changing decision
  under COLLABORATION.md trigger 3, the human's call.

**F1, restated as the reason the evidence slot is stuck:** `bench/run_efficacy_study.py:307-337`
(`prepare_resume`) raises on a duplicate `(effect_id, tier)` key, and the record schema carries
no repetition field. STATUS.md's reserved `*(evidence)*` item — "re-run the groq grid at n≥3" —
is not a matter of spending the quota; **the harness rejects the second sample before any request
is made.** A schema change (a `rep` field, `prepare_resume` keying on `(effect_id, tier, rep)`,
and — per A1 above — a proportion-based collapse in `score_efficacy.py`, not a majority-vote one)
is the actual prerequisite, and it is gated (COLLABORATION.md §2 trigger 3, "the efficacy-record
schema" named explicitly as a multi-consumer contract). Proposed, not committed, this session.
