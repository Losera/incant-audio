# Prompt-Efficacy Study — how prompt knowledge-level affects Faust generation

**Frozen point-in-time record, 2026-07-21. Not maintained.** Referenced by
`bench/run_benchmark.py`, `bench/run_efficacy_study.py`, `bench/score_efficacy.py` and
`llm/generate.py` for provenance; the design/pilot recorded below is historical, not a live plan.
The DELEGATE/HUMAN-OWNED mode tags below are vocabulary from the retired three-mode protocol
(COLLABORATION.md §9) — read as history, not instruction.

**Status:** designed + pilot executed 2026-07-19 · full 125-prompt run deferred (P9)
**Mode:** DELEGATE (new bench scaffolding + data files; human reviews diffs).
The two system prompts (`llm/prompts/system_prompt.txt`, `bench/prompts/system_faust.txt`)
are HUMAN-OWNED and are **never modified** by this study — it measures, it does not tune.

---

## 1. Motivation

Every prompt PluginForge has ever been tested with sits at one register: the "informed
producer" who knows effect names and parameter names ("a warm analog-style low-pass filter
with cutoff and resonance"). Real users span a much wider range — from DSP engineers who
can specify a topology, down to musicians who can only name a song the sound appears in.
We do not know whether the pipeline degrades gracefully, cliff-drops, or (plausibly)
*improves* when prompts carry less prescriptive detail. This study maps that curve and
identifies what specifically helps or hurts generation.

## 2. Tier scheme

| Tier | Persona | Operational rule |
|---|---|---|
| **L4** | DSP engineer | Must name an algorithm/topology AND ≥1 numeric range |
| **L3** | Informed producer | **Verbatim the 25 committed prompts in `prompts.json`** — anchors the study to the committed baseline (84% on 2026-07-16; **88% re-measured 2026-07-19**, see §6) |
| **L2** | Casual musician | Functional lay description; colloquial category words OK ("echo"); NO parameter jargon (no "cutoff", "Q", "feedback") |
| **L1** | Vibe / metaphor | Sensory metaphors only; must NOT name any effect category or parameter |
| **L0** | Reference | Artist/song/gear references only; must NOT describe the sound mechanically |

**Matched sets:** each of the 25 existing effects is rewritten at all 5 tiers →
**125 prompts** (`bench/prompts/tiered_prompts.json`). The target effect is constant
within a set, so per-set deltas are attributable to phrasing, not effect difficulty.
The existing category axis (trivial / filters / time-based / dynamics / generative)
is reused unchanged as the difficulty dimension.

**Internal-validity threat:** tier-rule leakage (an L1 prompt accidentally naming a
parameter). The human should spot-check L0/L1 rows in `tiered_prompts.json` when
reviewing the diff.

## 3. Metrics

1. **First-try compile rate** — existing oracle (`faust -lang cpp → /dev/null`), unchanged.
2. **Retry-corrected success + attempts (1–3)** — the study driver replicates
   `generate.py`'s stderr-feedback retry pattern *internally* (calling `generate.py`
   would silently swap the system prompt — a confound). Default `--retries 2` matches
   the product's 3-attempt loop.
3. **Error-class tagging** — first-attempt stderr mapped to the established taxonomy
   from `docs/benchmark_analysis.md`: SYNTAX / SEMANTIC / HALLUCINATION / INCOMPLETE
   (+ UNCLASSIFIED bucket listed for manual review). Answers *what* lower-knowledge
   phrasing breaks, not just *how much*.
4. **Semantic fidelity** (compile ≠ the right effect — the failure mode expected to
   GROW at L0/L1 while compile rate may stay high):
   - *Static heuristic:* per-effect `expected_primitives` any-of substring match
     (e.g. filters-01 → `fi.resonlp|fi.lowpass|moog|fi.svf`). Catches gross category
     misses ("gave me a delay when I asked for a filter"). Deliberately coarse; weak
     for the trivial category.
   - *LLM judge (optional `--judge`):* claude-haiku-4-5 scores each compiled result
     0–2 against the set's **L4 text as ground truth**. Limits, stated honestly: the
     judge reads code and cannot *hear* the result; it shares a model family with the
     generator; there is no listening test. Screening signal only — human spot-listens
     a sample in a later session.
   - *Deferred idea (not built):* render via `faust2sndfile` + spectral-feature checks.

## 4. Confound controls (locked)

- System prompt: `bench/prompts/system_faust.txt`, held constant for all tiers and all
  retry attempts. Documented external-validity caveat: the product path uses
  `llm/prompts/system_prompt.txt` (no stdlib cheat-sheet), so absolute rates here
  overstate the product slightly; tier *deltas* are the object of study.
- Model: `claude-opus-4-6`, `temperature=0`, `max_tokens=1024` (identical to bench).
  **Model-era boundary (2026-07-21):** every number recorded in §6 and §7 was produced on
  `claude-opus-4-6`. The product path (`llm/generate.py`) has since moved to
  `claude-opus-4-8`; the bench harnesses have **not** — they still pin opus-4-6 because
  `temperature=0` is a locked confound control here and opus-4-7+ reject `temperature`
  outright with a 400. Bumping the harnesses therefore means dropping the determinism
  control, which is a change to this locked design and needs a human decision (see §9).
  Until that is resolved, do not compare a post-bump run against the figures below without
  labelling the model change as a confound.
- Provider: claude only for the main run. Gemini comparison belongs to P8/ADR-008.
- Sampling: N=1 at temp 0 (not perfectly deterministic; noted). Resolution: 5 effects
  per tier×category cell → 20-point cell resolution; 25 per tier → 4-point tier
  resolution. Cell-level deltas are directional only; conclusions are drawn at tier
  level and tier×(easy/hard category-group) level. If tier deltas come out < ~8 points,
  escalate to N=3 repeats (deferred option).

## 5. Files & how to run

```
bench/prompts/tiered_prompts.json   # the 125-prompt matched-set dataset
bench/run_efficacy_study.py         # driver (separate from run_benchmark.py so the
                                    #  regression loop & results.json are untouched)
bench/score_efficacy.py             # tier×category matrices, error tagging, heuristics,
                                    #  optional --judge, optional --chart
bench/results/efficacy/             # timestamped results (never clobbers results.json)
```

```bash
# pilot (2 categories × 5 tiers = 50 prompts + retries):
python3 bench/run_efficacy_study.py --categories filters,generative
# full study:
python3 bench/run_efficacy_study.py
# scoring:
python3 bench/score_efficacy.py --results bench/results/efficacy/<file>.json \
    --prompts bench/prompts/tiered_prompts.json --chart
```

**Budget** (Opus ≈ $0.012–0.02/generation): pilot ≈ $1.0–1.7 · full 125 + retries
≈ $2.5–5 · Haiku judge pass ≈ $0.20.

## 6. Baseline result (P5 re-run, 2026-07-19)

Full 25-prompt L3 re-run: **22/25 (88%)** first-try — up from the committed 84%
(2026-07-16 baseline), still short of ADR-009's ≥96% prediction. **ADR-009's
prediction did not hold.** Failures:

| Prompt | Class | Error |
|---|---|---|
| ping-pong delay | SEMANTIC | endless evaluation cycle (same circular `with{}` failure as 2026-05) |
| tape-style flanger | HALLUCINATION | `undefined symbol: flanger_mono` (identical hallucination to 2026-05 — persistent across 2 months) |
| sidechain compressor | SYNTAX | `syntax error, unexpected ARROW` (new) |

Two of three failures are *exact repeats* of the May failures — stable failure modes,
good candidates for targeted (HUMAN-OWNED) prompt rules or few-shot examples.

## 7. Results

### 7.1 Pilot (filters + generative × 5 tiers, N=50 first attempts, 65 total generations)

Run 2026-07-20, `claude-opus-4-6`, `bench/prompts/system_faust.txt` held constant.
Raw results: `bench/results/efficacy/pilot_20260720.json`; chart:
`bench/results/efficacy/pilot_20260720_chart.png`.

**Per-tier aggregate (n=10 per tier, 5 filters + 5 generative):**

| Tier | First-try | Retry-corrected | Mean attempts |
|---|---|---|---|
| L4 (DSP engineer) | 9/10 (90%) | 9/10 (90%) | 1.00 |
| L3 (informed producer) | 9/10 (90%) | 9/10 (90%) | 1.00 |
| L2 (casual musician) | 8/10 (80%) | 9/10 (90%) | 1.10 |
| L1 (vibe/metaphor) | 5/10 (50%) | 8/10 (80%) | 1.50 |
| L0 (artist/song reference) | 6/10 (60%) | 9/10 (90%) | 1.40 |

**Reading it:** first-try compile rate is **not monotonic** in knowledge level — it
holds flat at L4/L3 (90%), degrades through L2 (80%) to a clear floor at L1 (50%),
then *partially recovers* at L0 (60%). Retry rescues nearly all of the gap: 4 of 5
tiers converge to 80–90% retry-corrected, at the cost of more attempts (mean 1.0 at
L3/L4 → 1.4–1.5 at L0/L1). **L1 (pure sensory metaphor, no effect-category or
parameter words) is the hardest tier in this pilot, harder even than L0's
artist/song references** — naming a concrete cultural reference point ("the sound
from song X") seems to give the model more to anchor generation on than an
adjective-only description.

**Error-class tagging of first-attempt failures** (after fixing a taxonomy gap this
pilot exposed — see below): L4/L3 failures are a single INCOMPLETE each (edge-case
empty output, not tier-related). L2 adds one SYNTAX. **L1 concentrates 3 of its 5
failures as SEMANTIC** — see detail below. L0's 4 failures are 3 SYNTAX + 1 INCOMPLETE.

**Taxonomy gap found and fixed:** the first scoring pass left 3 of L1's errors
UNCLASSIFIED. Reading the actual Faust compiler output showed all three shared the
established "algebraic vs sequential" misunderstanding already documented for the
ping-pong-delay failure in `docs/benchmark_analysis.md` — just with different surface
text:
- `multiple definitions of symbol 'process'` — a direct **regression of the ADR-009
  duplicate-symbol rule** under a vibe-only prompt ("make my synth sound warm and
  muffled, like it's playing from another room behind a wall"). The model had more
  structural freedom with no named effect/parameters to anchor on, and re-derived the
  same duplicate-`process` mistake ADR-009 was written to prevent.
- `number of outputs [1] ... must be equal to the number of inputs [2]` — a
  signal-graph arity mismatch (wiring a mono stage into a stereo one), the same class
  of "wrong idiom for wiring the graph" as the SEMANTIC failures already catalogued.
- `invalid delay parameter range: interval(0,2.14748e+09,0)` — a malformed literal
  argument to a delay primitive, i.e. parameter misuse rather than an invented name.

`bench/score_efficacy.py`'s `ERROR_CLASS_RULES` were extended with this vocabulary
(`multiple definitions of symbol`, `number of outputs`/`number of inputs`, `invalid
delay parameter range`, `must be between`/`must be equal to`) so future runs classify
these correctly; rescoring the same pilot data confirms zero UNCLASSIFIED fragments
remain. `tests/test_efficacy_unit.py` still passes 36/36 after the change.

**Heuristic semantic-fidelity pass rate** (expected-primitive match, coarse):
L4 88%, L3 100%, L2 88%, L1 87%, L0 88% — essentially flat across tiers in this
pilot. Given the heuristic's weak resolution (n≈8–9 per tier) this should be read as
"no gross category misses detected," not as evidence tier has no fidelity effect —
the LLM-judge pass (deferred) is needed to say more.

**Hypothesis check against §8:**
- **H1** (compile flat/higher at low tiers, fidelity drops) — **not supported**:
  compile rate drops sharply at L1, and the semantic heuristic stayed flat rather
  than dropping. Revise H1: compile robustness and semantic fidelity may be more
  decoupled than expected, or the heuristic is too coarse to see the effect — an
  argument for prioritizing the LLM-judge pass in the full run.
- **H2** (L4's numeric constraints increase SEMANTIC failures) — **not supported**
  in this pilot (L4's one failure was INCOMPLETE, not SEMANTIC) — but n=10 is too
  small to rule out; watch this in the full 125-prompt run.
- **H3** (HALLUCINATION is effect-specific, not tier-specific) — **can't yet
  confirm/deny**: zero HALLUCINATION-classified failures occurred in filters/
  generative this pilot. The known hallucination failure (`flanger_mono`) lives in
  the *time-based* category, excluded from this pilot — test in the full run.
- **H4** (retry rescues SYNTAX/HALLUCINATION better than SEMANTIC) — **partially
  supported**: L0's SYNTAX-heavy failures fully recovered on retry (6/10 → 9/10);
  L1's SEMANTIC-heavy failures recovered less completely (5/10 → 8/10) and took more
  attempts (mean 1.5, the highest of any tier) — consistent with SEMANTIC errors
  (circular/arity misunderstandings) being harder for the model to self-correct from
  stderr than a straightforward syntax error.

### 7.2 Full study (all 5 categories, 125 prompts)

**Attempted 2026-07-20, INVALID — not a result.** The full run returned 0/125 (0%)
across every single tier and category, with every failure tagged INCOMPLETE and
`mean_attempts == 1.00` everywhere (i.e. the retry loop never engaged). That
uniform, zero-variance pattern is not a model result — reading the raw error field
in `bench/results/efficacy/full_20260720_INVALID_insufficient_credits.json`
confirms all 125 requests were rejected before generation with the same Anthropic
API error: `"Your credit balance is too low to access the Anthropic API."` No
tokens were spent (the request never reached the model), so this cost nothing, but
it also produced no data. The output file and chart were renamed with an
`_INVALID_insufficient_credits` suffix rather than deleted, so they aren't mistaken
for real results but stay available for reference.

**Re-run P9** once the Anthropic account has credit again:
```bash
python3 bench/run_efficacy_study.py --out bench/results/efficacy/full_<date>.json
python3 bench/score_efficacy.py --results bench/results/efficacy/full_<date>.json \
    --prompts bench/prompts/tiered_prompts.json --chart bench/results/efficacy/full_<date>_chart.png
```
A quick sanity check before trusting the output: confirm at least one tier/category
cell shows a nonzero first-try rate and `mean_attempts` varies across records — a
flat 0% or a flat 1.00 mean-attempts across the whole table is the signature of this
same billing failure, not a real result.

## 8. What "helping vs hurting" will look like

Hypotheses the data can confirm or kill:
- **H1:** compile rate is flat-or-higher at L1/L0 (vaguer prompts → model defaults to
  well-worn stdlib idioms) while semantic fidelity drops sharply.
- **H2:** L4 numeric constraints *increase* SEMANTIC failures (more specified structure
  → more opportunities for circular definitions / wrong idioms) — the ping-pong pattern.
- **H3:** HALLUCINATION rate correlates with named-but-rare effects (flanger) regardless
  of tier — i.e. it's an effect property, not a phrasing property.
- **H4:** retry rescues SYNTAX/HALLUCINATION failures efficiently but not SEMANTIC ones
  (stderr for circular definitions is less actionable).

## 9. Deferred / follow-on

- Full 125-prompt run + judge pass (**P9**, ~$3–5).
- N=3 repeats if tier deltas < 8 points.
- Human listening spot-check of high-tier-delta sets (extends P6).
- Cross-provider phrasing sensitivity (fold into P8's Gemini run).
- Proposed-only (HUMAN-OWNED): prompt rules / few-shot examples targeting the persistent
  ping-pong and flanger_mono failures, informed by §6.
