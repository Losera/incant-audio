# R5 — Making the tiered-prompt study publishable

**Two conclusions.** First: **both 2026 venues named in the audit are closed** — the
deadline pressure is imaginary and the extra time is exactly what the study needs.
Second: the headline result currently has **three independent confounds**, two of which I
found in the raw data and one of which the project flagged in 2026-07 and never checked.
Fixing them is cheap. Written 2026-07-25, session 2 lane R.

---

## 1. The venues are closed. Stop optimising for September.

The audit's Phase 3 gate is *"Target ISMIR's 21 September abstract deadline, or DAFx if
the timing works better. Gate: submitted."* Neither is reachable:

| Venue | Audit's claim | Actual |
|---|---|---|
| DAFx 2026 (MIT, 1–4 Sep) | "a plausible home" | **Papers and demos closed.** Even the Challenge track closed **17 July 2026** — 8 days before this note |
| ISMIR 2026 (Abu Dhabi, 8–12 Nov) | "mandatory abstract deadline 21 September 2026" | **Abstract deadline was 20 April 2026**, full paper 27 April, notification 10 July, camera-ready 31 July |

The "21 September" date does not correspond to any ISMIR 2026 deadline.
([DAFx26](https://dafx26.mit.edu/authors/), [ISMIR 2026 CFP](https://ismir2026.ismir.net/authors/call-for-papers))

**This is good news.** A study with three unresolved confounds submitted in eight weeks
gets rejected; the same study submitted in eight months, with a semantic metric attached,
is a real contribution. Realistic targets:

- **NeurIPS 2026 AI4Music workshop** — the nearest thing to a natural home, since the
  closest published work to this thesis (arXiv 2508.05473) appeared there in 2025.
  Workshop CFPs land late (typically Aug–Oct for a December conference), so this is the
  one to watch actively rather than plan around.
- **DAFx 2027 / ISMIR 2027** — submissions ~March–April 2027. Eight months. This is the
  realistic primary target and it comfortably fits the work below.

## 2. Confound 1 — the compile-rate denominator is contaminated

Five API billing errors are recorded as Faust compile failures, one per tier. Every
published tier figure is wrong. Detail and table:
[[truncation-confound-HANDOFF-S1]] §2.1.

## 3. Confound 2 — output truncation, not prompt comprehension

Compile rate is monotonically decreasing in *generated program length*, and generated
length varies by tier far more than compile rate does. `max_tokens=1024` with no
truncation detection anywhere. Detail: [[truncation-confound-HANDOFF-S1]] §2.2.

One competing explanation *is* ruled out, and it is worth stating because it strengthens
the case. **Input prompt length does not track the effect**:

| Tier | Mean prompt chars | First-try (compile-only) | Mean generated chars |
|---|---:|---:|---:|
| L4 | 162 | 100% | 304 |
| L3 | **46** | 100% | 266 |
| L2 | 107 | 89% | 556 |
| L1 | 84 | 56% | 1304 |
| L0 | 69 | 67% | 1243 |

L3 has the *shortest* prompts and a perfect compile rate. So this is not "short prompts
are hard". It is specifically about how much code the model chooses to write in response.

## 4. Confound 3 — tier-rule leakage, and it is worst exactly where the finding lives

The study flagged this internal-validity threat itself and never closed it. Closed now.

Scanning all 50 L1/L0 prompts in `bench/prompts/tiered_prompts.json` for effect-category
and parameter vocabulary the tier definitions exclude: **12 of 50 hits, of which ~5 are
genuine leaks.** Being fair about the rest — "mixing console", "Pioneer mixer", "mixing
desk" trip a naive `mix` match but refer to hardware, not a dry/wet parameter; "when I
release" is a key-release metaphor, not the ADSR stage.

The genuine ones:

| Effect | Tier | Prompt | Leaks |
|---|---|---|---|
| time-based-01 | L0 | "the dotted-eighth **echo** The Edge uses on…" | names the effect category |
| time-based-02 | L0 | "the bouncing stereo **echo** all over…" | names the effect category |
| time-based-05 | L0 | "the lush metal-plate vocal **reverb** on…" | names the effect category |
| generative-02 | L0 | "the raw buzzing analog **synth** lead from 'Popcorn'" | names the effect category |
| generative-02 | L1 | "a bright sharp **synth** note that blooms…" | names the effect category |

**Four of the five genuine leaks are at L0.** L0 is the tier the headline finding says
*recovers* (67% vs L1's 56%), and the published interpretation is that a concrete cultural
reference gives the model more to anchor on.

There is now a duller competing explanation: **L0 prompts more often just say what the
effect is.** "The dotted-eighth echo The Edge uses" tells the model "delay" outright; L1's
"make my synth sound warm and muffled, like it's playing from another room" does not say
"filter". If naming the category is what drives the recovery, the finding is about
vocabulary, not about cultural anchoring — and that is a materially less interesting
claim.

The data cannot currently distinguish them. A reviewer would ask this in the first round.

## 5. What the corpus already has going for it

Not all bad news. `bench/prompts/tiered_prompts.json` carries per-effect
`target` and `expected_primitives` fields — e.g. `filters-01` → target
`"resonant low-pass filter"`, expected `["fi.resonlp", "fi.lowpass", "moog", "fi.svf"]`.
That is a **semantic label that already exists** for all 25 effects and is currently
unused by any scorer. Combined with the render oracle ([[R3-perceptual-oracle]]), turning
those into expected spectral signatures is the cheapest route to a second measurement axis.

The 5 × 5 design (25 effects × 5 tiers, balanced across trivial / filters / time-based /
dynamics / generative) is sound, and the tier ladder is a genuinely novel instrument. The
problem is entirely in execution and measurement, not in design.

## 6. The run that would be submittable

In dependency order. Nothing here needs a GPU, and every provider call is free-tier.

1. **Fix the pipeline first.** Truncation detection, a real token budget, API errors
   partitioned out of the compile denominator. [[truncation-confound-HANDOFF-S1]] §3.
   *Everything below is void without this — it is the same mistake as the 2026-07-20 run.*
2. **De-leak the corpus.** Rewrite the ~5 leaking L0/L1 prompts so no low-tier prompt
   names its effect category. Then re-run the §4 check as a test, so it cannot regress.
   Log the change — the old and new corpora are not comparable and that must be on record.
3. **Add generated length as a recorded covariate.** Token counts in and out, and the
   provider's finish reason, on every record. Without this the length confound can be
   argued about but not settled.
4. **Add the semantic axis.** Per-prompt expected spectral signatures from
   `expected_primitives` + `target`, scored by `bench/render_oracle.py`. This is what
   makes the paper's central argument — *compile rate is not quality* — supportable rather
   than asserted. Note the coverage gap: generators do not render yet
   ([[R3-perceptual-oracle]] §5), so either close that or report on the 20 effects that do
   and say so.
5. **Run n=125 across three models on free providers.** The registry already makes this a
   flag change; cost $0. Use groq for volume (gemini's 20/day is unusable, per
   `llm/providers.py:93`). Three models breaks the single-model artifact.
   Note the recorded constraint: opus-4.7+ reject `temperature` (`llm/providers.py:39`),
   so "temperature 0 as a locked control" needs an explicit decision — probably report
   provider defaults and repeat runs rather than pretend to determinism.
6. **Repeat each cell.** n=1 per (effect, tier, model) cannot separate model variance from
   tier effect. Three repeats makes 375 generations — still free, still hours not days.

**Then** ask whether the non-monotonicity survives. It may not. If tier effects collapse
once truncation and leakage are removed, *that is still a publishable result* — "apparent
prompt-expertise effects in code-generation benchmarks are substantially explained by
output-length truncation" is a methodological finding other people building text-to-audio
benchmarks need, and it generalises beyond Faust.

The honest framing either way: this project's contribution is **the evaluation instrument**
— a tiered prompt ladder, a compile oracle, and a spectral oracle over a real DSL — not a
claim about how musicians talk to machines. The latter needs the instrument to be sound
first.

## 7. Reproducing §4

```bash
python3 - <<'PY'
import json
d=json.load(open('bench/prompts/tiered_prompts.json'))
CAT=['filter','delay','echo','reverb','chorus','flanger','phaser','compressor',
     'limiter','gate','expander','distortion','oscillator','synth','envelope']
for e in d['effects']:
    for t in ['L1','L0']:
        p=' '+e['tiers'][t].lower()+' '
        if (h:=[w for w in CAT if w in p]):
            print(f"{e['effect_id']:14} {t}: {h}  {e['tiers'][t][:70]}")
PY
```

Related: [[truncation-confound-HANDOFF-S1]], [[R3-perceptual-oracle]],
[[R4-training-our-own-model]].
