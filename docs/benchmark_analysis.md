# Benchmark Analysis

Generated: 2026-05-04. Source: `bench/results/results.json` (50 records, Claude provider only).
Benchmark run date: 2026-05-01.

**Note on discrepancy with ADR-007:** The raw JSON yields Faust 23/25 (92%) and Cmajor 16/25 (64%). ADR-007 reports Faust 22/25 (88%) and Cmajor 15/25 (60%). The difference is one record in each column — likely a rerun or prompt adjustment after ADR-007 was written. This document uses the JSON as the authoritative source. The qualitative conclusions in ADR-007 are unaffected.

---

## Aggregate Scores (Claude provider)

| DSL | Pass | Total | Rate |
|-----|------|-------|------|
| **Faust** | **23** | **25** | **92%** |
| **Cmajor** | **16** | **25** | **64%** |

Gap: **28 percentage points** (28pp). This is decisive — not noise.

---

## Score Breakdown by Category

| Category | Faust | Faust % | Cmajor | Cmajor % | Delta |
|----------|-------|---------|--------|----------|-------|
| trivial | 5/5 | 100% | 5/5 | 100% | 0pp |
| filters | 5/5 | 100% | 1/5 | 20% | -80pp |
| time-based | 3/5 | 60% | 2/5 | 40% | -20pp |
| dynamics | 5/5 | 100% | 5/5 | 100% | 0pp |
| generative | 5/5 | 100% | 3/5 | 60% | -40pp |
| **TOTAL** | **23/25** | **92%** | **16/25** | **64%** | **-28pp** |

**Key observation:** Cmajor and Faust are tied in two categories (trivial, dynamics) and far behind in three (filters, generative, and modestly in time-based). The failure load is not evenly spread — it concentrates in categories that require stdlib knowledge (filters: must know svf endpoint names) or precise type handling (generative: FM math, Karplus-Strong RNG).

---

## Faust Failure Analysis (2 failures)

### Faust Failure 1 — ping-pong delay
**Category:** time-based
**Prompt:** "a ping-pong delay that bounces between left and right"
**Error:**
```
ERROR : after 400 evaluation steps, the compiler has detected an endless evaluation cycle of 12 steps
```
**Classification: SEMANTIC**
The model defined `del_l` in terms of `del_r` and `del_r` in terms of `del_l` in a `with {}` block. Faust's signal graph evaluator cannot resolve this circular dependency statically. The correct Faust idiom for ping-pong feedback requires the `+~` (feedback) operator applied separately on each channel. The model used `with {}` bindings as if they were sequential imperative statements, which they are not — they are simultaneous algebraic equations. This failure reflects a subtle understanding gap about Faust's functional signal-graph semantics.

---

### Faust Failure 2 — tape-style flanger
**Category:** time-based
**Prompt:** "a tape-style flanger with feedback"
**Error:**
```
/tmp/tmpguxbm_dc.dsp:9 : ERROR : undefined symbol : flanger_mono
```
**Classification: HALLUCINATION**
The model used `ef.flanger_mono()` which does not exist in `stdfaust.lib`. Faust's effects library (`ef`) does not include a `flanger_mono` primitive — a flanger must be built from `de.fdelay` and an LFO. The model appears to have inferred the name by analogy with `ef.chorus` and other `ef.*` functions.

---

## Cmajor Failure Analysis (9 failures)

### Cmajor Failure 1 — high-pass filter
**Category:** filters
**Prompt:** "a high-pass filter with a gentle 12dB per octave slope"
**Error:**
```
/patch.cmajor:13:16: error: Cannot find symbol 'highpassOut'
    filter.highpassOut -> out;
```
**Classification: HALLUCINATION**
The model generated a `graph` using `std::filters::tpt::svf::Processor` and attempted to route `filter.highpassOut` to the output. The SVF processor exposes only `.out` (which carries the current filter mode's output). There is no `highpassOut` endpoint. The model hallucinated an output name by analogy with the prompt ("high-pass filter").

---

### Cmajor Failure 2 — band-pass filter
**Category:** filters
**Prompt:** "a band-pass filter with adjustable center frequency and Q"
**Error:**
```
/patch.cmajor:13:16: error: Cannot find symbol 'bandOut'
    filter.bandOut -> out;
```
**Classification: HALLUCINATION**
Same root cause as Failure 1. The model invented `bandOut` on the assumption that the SVF filter exposes named per-mode outputs. All three filter-type hallucinations (highpassOut, bandOut, notch) follow the same wrong mental model of the SVF processor.

---

### Cmajor Failure 3 — notch filter
**Category:** filters
**Prompt:** "a notch filter for removing 60Hz hum, with a width control"
**Error:**
```
/patch.cmajor:13:16: error: Cannot find symbol 'notch'
    filter.notch -> out;
```
**Classification: HALLUCINATION**
Third instance of the SVF endpoint hallucination pattern. The model invented `notch` as a named output endpoint.

---

### Cmajor Failure 4 — high-shelf filter
**Category:** filters
**Prompt:** "a high-shelf filter for adding air to vocals"
**Error:**
```
/patch.cmajor:19:14: error: Expressions can only be connected to a stream or value endpoint
    1.0f -> lpFilter.q;
```
**Classification: SEMANTIC**
The model attempted to drive a node's `q` parameter with a literal float constant using the `->` connection operator. In Cmajor, `->` connects streams and value endpoints — it cannot source from a literal expression. The correct approach is to declare a local `input value float` and route it, or initialize the processor's parameter via a different mechanism.

---

### Cmajor Failure 5 — chorus
**Category:** time-based
**Prompt:** "a subtle chorus effect with rate and depth"
**Error:**
```
/patch.cmajor:33:37: error: Could not resolve argument types for function call min(float64, float32)
    let delayL = max (0.0f, min (modL, float32 (maxDelaySamples - 2)));
```
**Classification: SEMANTIC**
The model produced a mix of `float32` and `float64` arithmetic. `processor.period` and `processor.frequency` return `float64`. Multiplying a `float64` by a `float32` produces a `float64`. Calling `min(float64, float32)` is ambiguous in Cmajor's type system — the model failed to insert the necessary `float32(...)` cast around the mixed-type expression.

---

### Cmajor Failure 6 — tape-style flanger
**Category:** time-based
**Prompt:** "a tape-style flanger with feedback"
**Error:**
```
/patch.cmajor:50:38: error: Cannot determine the type of the value to be written to this endpoint
    out <- in * (1.0f - mix) + delayed * mix;
```
**Classification: SEMANTIC**
Type inference failure: `delayed` was read from a `float<2>` buffer but the expression `delayed * mix` mixes a vector type with a `float32` scalar in a context where the `out` endpoint type cannot be inferred. The compiler cannot resolve the final expression's type. This is a variant of the float64/float32 mixing issue — the model generated plausible-looking code but miscounted the type chain.

---

### Cmajor Failure 7 — plate reverb
**Category:** time-based
**Prompt:** "a simple plate reverb with decay time and damping"
**Error:**
```
/patch.cmajor:46:26: error: Found "input" when expecting identifier
    float allpass (float input, float[] buf, int& pos, int len, float g)
```
**Classification: SYNTAX**
The model used `input` as a function parameter name. `input` is a reserved keyword in Cmajor (used to declare processor inputs). The model almost certainly transferred this from C/C++ muscle memory where `input` is a common parameter name. This failure would never appear in Faust, where there is no such keyword.

---

### Cmajor Failure 8 — 2-operator FM synth
**Category:** generative
**Prompt:** "a 2-operator FM synth with ratio and modulation index"
**Error:**
```
/patch.cmajor:51:36: error: Cannot implicitly convert 'float64' to 'float32'
    let stereo = float<2> (sample, sample);
```
**Classification: SEMANTIC**
`sample` was computed through a chain involving `processor.period` (float64). The final value retained float64 type, then was passed to `float<2>(sample, sample)` which expects float32 components. The model missed the required explicit cast to float32 before constructing the stereo vector.

---

### Cmajor Failure 9 — Karplus-Strong
**Category:** generative
**Prompt:** "a Karplus-Strong plucked string synthesizer"
**Error:**
```
/patch.cmajor:23:38: error: Unrecognised suffix on literal
    rngState = rngState * 1664525u + 1013904223u;
```
**Classification: SYNTAX**
The model used C-style unsigned integer literal suffixes (`u`) which are not recognized by the Cmajor compiler. Cmajor integers are typed by context, not suffix. This is a direct transfer of C/C++ idiom that does not exist in Cmajor.

---

## Failure Classification Summary

### Faust (2 failures)

| # | Prompt | Classification | Root cause |
|---|--------|----------------|------------|
| 1 | ping-pong delay | SEMANTIC | Circular dependency in `with {}` bindings; misunderstood algebraic vs sequential semantics |
| 2 | tape-style flanger | HALLUCINATION | Used `ef.flanger_mono` which does not exist in `stdfaust.lib` |

### Cmajor (9 failures)

| # | Prompt | Classification | Root cause |
|---|--------|----------------|------------|
| 1 | high-pass filter | HALLUCINATION | Invented SVF output endpoint `highpassOut` |
| 2 | band-pass filter | HALLUCINATION | Invented SVF output endpoint `bandOut` |
| 3 | notch filter | HALLUCINATION | Invented SVF output endpoint `notch` |
| 4 | high-shelf filter | SEMANTIC | Connected literal constant to node endpoint via `->` |
| 5 | chorus | SEMANTIC | Mixed `float64`/`float32` in `min()` call |
| 6 | tape-style flanger | SEMANTIC | Type inference failure; mixed vector and scalar types in endpoint write |
| 7 | plate reverb | SYNTAX | Used reserved keyword `input` as function parameter name |
| 8 | FM synth | SEMANTIC | Failed to cast `float64` chain to `float32` before vector construction |
| 9 | Karplus-Strong | SYNTAX | Used C-style `u` unsigned integer literal suffix |

**By type:**
- HALLUCINATION: 3 (all SVF endpoint names; one failure pattern, three instances)
- SYNTAX: 2 (reserved keyword `input`; C-style `u` suffix)
- SEMANTIC: 4 (literal→endpoint; float64/float32 type mixing; type inference)
- INCOMPLETE: 0

---

## Summary Recommendation

**Are Cmajor's failures the kind that better prompt engineering could fix, or are they structural?**

They are substantially fixable by prompt engineering, but the fix requires five independent, independently-verifiable rules — and that scope is itself a risk signal.

Here is the breakdown:

**The SVF hallucinations (3 failures, 1 fix):** All stem from a single wrong assumption — that `std::filters::tpt::svf::Processor` exposes named per-mode outputs. One rule eliminates all three: "The SVF filter has one output, `.out`. To produce a high-pass output, use a different processor type, not a different endpoint name." High confidence this fix holds.

**The float64/float32 failures (3 failures, 1 fix):** All stem from `processor.frequency` and `processor.period` returning `float64`. One rule covers all three: "Always wrap `processor.frequency` and `processor.period` in `float32(...)`. All arithmetic in `void main()` should use `float32` unless you have a specific reason not to." High confidence this fix holds.

**The syntax failures (2 failures, 2 fixes):** Two independent rules needed — avoid the `input` keyword as an identifier, and avoid C-style `u` suffix on literals. Both are simple constraints with no edge cases.

**The literal→endpoint failure (1 failure, 1 fix):** One rule: "Constants cannot be connected to node endpoints with `->`. Declare a local `input value float` instead." Moderate confidence — this failure is a Cmajor graph-model concept gap, not just a syntax error.

**In total, five prompt rules could in principle push Cmajor from 16/25 (64%) to approximately 22–24/25 (88–96%), closing or eliminating the gap.**

However, the decision is not purely about whether the rules are expressible. It is about whether the training-data coverage of Cmajor is sufficient that, when rules are added, the model reliably applies them across all category types. The hallucination pattern (inventing endpoint names by semantic analogy) is particularly concerning — it suggests the model does not have strong ground-truth coverage of the Cmajor stdlib. Prompt rules constrain output but do not add knowledge.

**The honest assessment:** A focused recovery test — rerunning only the 9 failed Cmajor prompts with a hardened system prompt — is the right next step before making a final call. If the patched prompt achieves 7+/9 on the failed set (bringing total to ≥23/25), the gap is effectively closed. If it achieves 4/9 or fewer, the failures are structural and Faust should be confirmed without further Cmajor work.

Note that ADR-007 already concluded "Faust retained" on 2026-05-01, citing the 28pp gap and the five-error-class complexity. The recovery test described in `docs/next_steps.md` is the mechanism for confirming or reversing that decision.
