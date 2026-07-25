# R3 — The evaluation oracle

**Recommendation: ship spectral render-and-measure. Do not build CLAP.**

Unlike the rest of this lane, this note is not a proposal. The oracle is built, validated
and committed: `bench/render_oracle.py`. It runs today, on this machine, with no new
dependencies. Written 2026-07-25, session 2 lane R.

---

## 1. The state before this note

PF-013 — "semantic fidelity is unmeasured". The only oracle in the project is
`faust -lang cpp -o /dev/null` (`llm/generate.py:90`). A patch that compiles perfectly and
sounds nothing like the request scores identically to one that nails it. The audit's
framing is right: this is why no other number in the project means anything.

The audit proposed three routes in increasing ambition — (1) render + spectral features,
(2) CLAP embedding similarity, (3) code-to-audio embedding alignment — and treated (1) as
the cheap fallback. **For this project's corpus, (1) is not the cheap fallback. It is the
correct answer, and (2) is the wrong tool.** §4 argues that.

## 2. What was built

`bench/render_oracle.py`. Two layers, deliberately separate:

- **`measure()`** — objective, prompt-independent gates: NaN/Inf, silence, DC offset,
  runaway gain, peak. This is the automatable half of the P6 battery
  (`docs/p6_test_battery.md`), and it is pass/fail.
- **`features()`** — spectral descriptors for asking whether the patch matches its
  description: per-band gain vs input, spectral centroid shift, crest-factor change.
  Evidence, not a verdict.

Dependencies: `numpy` and `scipy` only — both already installed. No torch, no librosa, no
soundfile, no network, no provider quota. It shells out to `faust2sndfile`, already part of
the required Faust install. A run costs zero API requests, which matters because free-tier
quota is the binding constraint on everything else.

```
python bench/render_oracle.py patch.dsp          # human-readable
python bench/render_oracle.py patch.dsp --json   # machine-readable, exit code is the gate
python bench/render_oracle.py --self-test        # three patches with known answers
```

## 3. It works, and it is calibrated against known physics

The self-test is the important part — an oracle nobody checks is worse than no oracle.
Three patches with answers known from theory, all passing:

| Patch | Expected | Measured |
|---|---|---|
| `fi.resonlp(1000, 0.707)` | −3 dB at the 1 kHz corner, ~12 dB/oct above | `800-1200: -3.0`, `4000-8000: -30.0`, `8000-20000: -46.0` |
| `_*0.5` | flat −6 dB everywhere | −6.0 in all six bands |
| `_*0` | silence gate must fire | `ok=False`, `output is silent` |

Run over the 25-prompt benchmark corpus (`bench/results/results.json`), full results in
`bench/results/oracle_20260725.json`:

> **17 of 17 renderable patches produce usable audio. 5 of 22 are outside the harness
> (§5).** No NaN, no silence, no DC runaway, no runaway gain.

That is the first objective audio measurement in this project's history. Read it
carefully, though — it is a *floor*, not a triumph. It says the patches that compile are
not broken. It says nothing yet about whether they match their prompts, which is §6.

Spot-checking the spectral features against what the prompts asked for, they do:

| Prompt | 50-200 | 200-800 | 800-1200 | 1200-4k | 4-8k | 8-20k |
|---|---:|---:|---:|---:|---:|---:|
| low-pass, cutoff+resonance | −0.0 | −0.4 | **−3.0** | −12.4 | −30.0 | −46.0 |
| high-pass, gentle 12 dB/oct | **−8.3** | +1.0 | +1.5 | +0.4 | +0.1 | +0.0 |
| band-pass, adjustable centre | −27.7 | −3.1 | **−0.1** | −8.2 | −29.4 | −45.8 |
| high-shelf, "air" for vocals | +0.0 | +0.0 | +0.1 | +0.4 | +1.3 | **+2.5** |
| notch, 60 Hz hum | **−0.2** | −0.0 | −0.0 | −0.0 | −0.0 | −0.0 |

Every one of those is the correct signature for the effect named in the prompt.

### 3.1 One design finding worth keeping

**Global summary statistics are not sufficient; per-band ratios are required.** The
high-pass patch shows `rms_ratio = +0.1 dB` and `centroid_shift = -0.01 oct` — by summary
statistics it appears to do nothing. Per-band it is unmistakable: **−8.3 dB below 200 Hz,
flat above.** The reason is physical: white noise has uniform energy per Hz, so cutting
0–500 Hz removes ~2% of total energy and barely moves either aggregate.

A naive global log-spectral tilt is likewise weak. Measured on the two filters: it
separated the low-pass cleanly (−40.2 dB/decade) but barely moved for the high-pass
(+1.4), because a 1 kHz high-pass passes most of the spectrum and the fit is dominated by
its flat passband. **Any scoring built on a single scalar brightness feature will
systematically under-detect high-pass and shelf errors.** Bands are cheap; use them.

## 4. Why not CLAP — the part that disagrees with the audit

CLAP is trained to align audio with *captions* — "a dog barking", "a jazz piano solo". It
discriminates sound-event and genre semantics. It is not trained to discriminate *"low-pass
at 1 kHz"* from *"low-pass at 4 kHz"*, and there is no reason to expect it can.

The deeper problem is structural, and it is specific to effects:

> **An effect's output is dominated by its input material, not by the effect.** Feed CLAP
> two seconds of filtered white noise and it will tell you about noise. The prompt says
> "warm analog low-pass filter"; the audio contains no "warmth" semantics CLAP was trained
> to name, because the content is whatever the user happened to play through it.

CLAP similarity between an *effect description* and *processed audio* is close to
meaningless. This is not a criticism of CLAP; it is a category error in applying it. The
audit's own caveat — CLAP measures semantic relevance, not perceptual quality — understates
the problem for this domain.

Where CLAP *would* be appropriate is the `generative` category, where the patch produces
the content rather than transforming it. That is **5 of 25 prompts (20%)** of the
benchmark corpus — and, as §5 explains, exactly the 5 the current harness cannot render.
So even the defensible use of CLAP is blocked behind other work, and it would cost torch,
a model download, and GPU-or-slow-CPU inference to serve a fifth of the corpus.

**Verdict: not now. Revisit only after generators render, and only for that category.**

## 5. The honest limitation: generators do not render

`faust2sndfile`'s architecture is input-file-driven. Given a zero-input DSP it exits 0 and
writes a 44-byte WAV header with an empty data chunk. Measured 2026-07-25 on Faust 2.85.5.
Two workarounds were tried and both produce the same empty output:

```faust
process = !, component("gen.dsp");           // 1 in, N out
process = (_,_ : !,!), component("gen.dsp"); // 2 in, N out
```

The `-c <samples>` flag documented in `faust2sndfile`'s usage text is not accepted by the
generated binary (`./osc : unrecognized option -c`).

The oracle now detects this precisely rather than reporting a bare failure: `patch_arity()`
asks `faust -json` for the I/O count and raises `UnsupportedPatch` on zero inputs, so
"we cannot measure this" never gets confused with "this patch is broken". That distinction
matters — silently scoring an unrenderable generator as a failure is exactly the bug that
put five API billing errors into the compile-rate column
([[truncation-confound-HANDOFF-S1]] §2.1).

**The right fix is not a better shell-out.** It is the C++ offline harness that S1's Part 2
already calls for: generate → compile → JIT-load → render offline. Driving the existing
`FaustEngine` handles zero-input patches natively *and* exercises the real production code
path, rather than a parallel binary built by a different toolchain. `render_oracle.py`
should be understood as the Python-side oracle for effects (80% of the corpus, working
now), not as the final architecture.

## 6. What this does not close

Being precise, because over-claiming here is how PF-013 got written in the first place:

- **This measures "did it do something spectrally sane", not "did it do what was asked".**
  Turning §3's table into a verdict needs a per-prompt expected-signature spec — for each
  corpus prompt, the bands that must move and in which direction. That is a day of work
  over 25 prompts and it is the obvious next step. It does not need any new technology.
- **No perceptual quality judgement.** Nothing here detects that a filter sounds harsh, or
  that a reverb sounds metallic. That genuinely needs ears or a trained model.
- **Parameter sweeps are unmeasured.** Every patch is rendered at default parameter values.
  A patch whose cutoff knob does nothing scores identically to one that works. The harness
  can drive parameters (the built binaries accept `-frequency`, `-amplitude` etc. as CLI
  options — visible in `./osc option list`), so this is reachable, and it is probably the
  single highest-value extension.
- **n is small.** 17 patches, one model, one prompt version.

## 7. Reproducing

```bash
python bench/render_oracle.py --self-test          # must print ALL PASS
python -c "
import json,sys; sys.path.insert(0,'bench'); import render_oracle as ro
recs=[r for r in json.load(open('bench/results/results.json')) if r['first_try_compiles']]
print(sum(ro.analyse(r['code']).get('measurement',{}).get('ok',False) for r in recs), 'of', len(recs))"
```

Related: [[R1-grammar-constrained-decoding]] §5, [[R5-publishable-run]],
[[truncation-confound-HANDOFF-S1]].
