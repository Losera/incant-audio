# HANDOFF → Session 1: the output-truncation confound

> **RESOLVED 2026-07-25.** Kept as the evidence record; the fix it asked for has landed.
> Provider-side detection (`OutputTruncated`, `finish_reason`/`stop_reason` in all three
> adapters, `min_max_tokens` on the anthropic spec) plus `tests/test_truncation_detection.py`
> were written by session 1. `llm/generate.py` catching it — `MAX_OUTPUT_TOKENS = 4096`,
> the brevity retry instead of feeding back stderr, and the distinct `truncated` reason —
> plus the scorer denominator fix (`bench/score_efficacy.py::is_transport_error`,
> `tests/test_transport_error_exclusion.py`) were done in session 2. Commit `07d0997`.
>
> **What is fixed:** the pipeline can no longer mistake a cut-off program for bad Faust,
> and five API billing errors no longer sit in a compile-rate denominator.
> **What is NOT fixed:** §2.2's confound stands. Nothing has been re-measured, so the
> tiered study's non-monotonicity is still unresolved between "vague prompts are harder"
> and "vague prompts produce longer programs that truncate". That needs the re-run designed
> in [[R5-publishable-run]] §6 — and the corpus de-leaking in §4 first.

**Owner: S1** (touches `llm/`, which is S1's lane — session 2 is read-only there and has
changed nothing). Written 2026-07-25 by session 2, lane R.

**One line:** the pipeline throws away the provider's "I ran out of output budget" signal,
lets the truncated program reach `faust`, misreads the resulting `unexpected $end` as a
model syntax error, and then spends its entire retry budget asking the model to fix
syntax it did not get wrong.

This is a ~10-line fix. It touches PF-019, PF-011, PF-024 and the project's one
publishable result.

> **Updated after `4bea5f3` (PF-019, landed 2026-07-25 16:24 while this was being
> written).** That fix is good and it is not this bug. It adds a wall-clock
> `generation_budget()`, `RateLimited` / `BudgetExhausted` exceptions and a typed
> `reason` field (`ok` / `rate_limited` / `timeout` / `invalid_faust`). It does not
> touch `max_tokens`, and no adapter reads `finish_reason` / `stop_reason` — verified
> by grepping the commit. A truncated generation still returns `reason: "invalid_faust"`
> with a Faust syntax error attached, which is precisely the misdiagnosis described
> below. **The new typed-reason enum is the right home for a `truncated` reason** — this
> fix is now smaller than when it was written. Line numbers below are current as of
> `4bea5f3`.

---

## 1. The defect

`max_tokens=1024` is hardcoded in all three call sites:

- `llm/generate.py:92`
- `bench/run_efficacy_study.py:139`
- `bench/run_benchmark.py:110`

`ProviderSpec.min_max_tokens` (`llm/providers.py:77`) raises that floor per provider —
`gemini` and `groq` set `4096` (`:91`, `:104`) — but the `anthropic` spec (`:144`), which
produced every recorded benchmark and efficacy number, sets none, so it defaults to `0`.
Those runs executed at a raw 1024-token cap.

Nothing anywhere reads `finish_reason` / `stop_reason`. Grep the module: no adapter
inspects it. Worse, the truncated text is deliberately passed through —
`llm/providers.py:225`:

> Tolerates a missing closing fence — a truncated response still yields its code.

So a cut-off program is handed to `faust`, which reports `syntax error, unexpected $end`
(bison's `$end` *is* the EOF token — the parser ran out of input). The retry loop at
`llm/generate.py:164` and `:208` assigns that raw stderr to `error_ctx` and re-prompts
with *"Your previous output had this compiler error — fix it"* (`:104`). The model, told it
made a syntax error, writes another program of the same length, which truncates again.

`bench/results/efficacy/pilot_20260720.json`, record `L1/generative-03`: three attempts,
three failures, final code ending mid-identifier —

```
chorusR(x) = x * 0.6 + de.
```

The project had already diagnosed this signature on a *different* provider and written it
down at `llm/providers.py:112` (gpt-oss-20b reaching Faust as an empty `.dsp`:
*"syntax error, unexpected $end"*). The link to the study data was never made.

## 2. What it invalidates

### 2.1 Five API billing errors are recorded as Faust compile failures

`generative-05` returned `Your credit balance is too low` in all five tiers. The scorer
counts `first_try_compiles == False` regardless of cause (`bench/score_efficacy.py:164`),
so all five land in the compile-failure column.

| Tier | Published (`docs/prompt_efficacy_study.md:135-141`) | Compile-only (n=9) |
|---|---|---|
| L4 | 9/10 = 90% | 9/9 = **100%** |
| L3 | 9/10 = 90% | 9/9 = **100%** |
| L2 | 8/10 = 80% | 8/9 = **89%** |
| L1 | 5/10 = 50% | 5/9 = **56%** |
| L0 | 6/10 = 60% | 6/9 = **67%** |

One billing error per tier, so the *shape* survives — but every published figure is a
compile rate that includes a non-compile event, and the denominator is wrong.

### 2.2 The non-monotonicity tracks program length, not prompt expertise

The headline finding — compile rate is non-monotonic in prompt expertise — has an
unexamined competing explanation:

| Tier | First-try (compile-only) | Mean generated chars |
|---|---:|---:|
| L4 | 100% | 304 |
| L3 | 100% | 266 |
| L2 | 89% | 556 |
| L1 | 56% | **1304** |
| L0 | 67% | **1243** |

Failed generations average **1447** chars; successful ones **581**. Compile rate is
monotonically decreasing in output length even though it is non-monotonic in tier. The
longest generations sit at 2199–2210 chars, right where a 1024-token cap on
punctuation-dense Faust would bite.

The mechanism is plausible and mundane: vague prompts (L1 "vibe/metaphor") give the model
no target, so it writes a longer, more elaborate program; longer programs truncate.
**"Sensory metaphor is harder for the model to understand" and "sensory metaphor makes
the model write more code than fits in the budget" predict the same curve.** Nothing in
the current data separates them.

This does not destroy the finding. It means the finding as stated is not yet supported,
and a reviewer would ask this question first. Design for the corrected re-run is in
[[R5-publishable-run]].

## 3. The fix

Smallest change that gets the signal:

1. **Surface truncation.** Have each adapter in `llm/providers.py` return, or raise on,
   `finish_reason == "length"` / `stop_reason == "max_tokens"`. Do not let a truncated
   body reach `validate_faust()`. Post-`4bea5f3` this is a natural fit: add a
   `Truncated` exception beside `RateLimited` / `BudgetExhausted`, and a `truncated`
   member to the `reason` enum, so the host can say "the model ran out of room" instead
   of showing a Faust syntax error the model did not cause.
2. **Give the anthropic spec a `min_max_tokens`** consistent with the other two (`4096`),
   or lift the floor to the shared caller default so no provider silently runs at 1024.
3. **Do not feed truncation back as a compile error.** In `llm/generate.py`, when the
   cause is truncation, retry with a raised budget or an explicit
   *"produce a shorter program"* instruction — not with `faust` stderr. Today this path
   is a guaranteed-failure loop that still spends three provider requests.
4. **Fix the scorer.** `bench/score_efficacy.py` must partition API/transport errors out
   of the compile-rate denominator rather than counting them as compile failures.

Constraint worth respecting: raising `max_tokens` alone is *not* sufficient and is
recorded as such at `llm/providers.py:113` ("Raising max_tokens does not fix it" for the
reasoning-token case) and `:120` (groq returns 413 above ~7500 for gpt-oss-120b). The
detection in (1) is the load-bearing part; the budget change is secondary.

## 4. Why this is worth doing before anything else in the reliability lane

- **PF-019 (timeout cliff).** A retry loop that cannot succeed still spends its full
  request budget. Three guaranteed-failing requests against a 120s total is a mechanism
  for the cliff, independent of the httpx-timeout-equals-subprocess-cap bug already
  identified at `llm/providers.py:50` / `host/Source/PromptPanel.cpp:208`.
- **PF-024 (invalid Faust for whole prompt classes).** The affected classes are the ones
  that generate long programs. Some fraction of "the model cannot write ping-pong delay"
  may be "the model cannot write ping-pong delay *in 1024 tokens*". Untested either way.
- **PF-011 (the efficacy pilot generalises to nothing).** §2 above.
- **PF-013 (semantic fidelity unmeasured).** Unaffected, but note that a truncated program
  that happens to still compile would score as a success today. No check exists.

## 5. Verifying the fix

The regression test is cheap and needs no network: feed `validate_faust()` a known-good
Faust program truncated at 60% of its length, and assert the pipeline reports a
truncation reason rather than a syntax error.

Reproducing §2.2:

```bash
python3 - <<'PY'
import json, statistics as st, collections
recs=[r for r in json.load(open('bench/results/efficacy/pilot_20260720.json'))
      if not (r.get('errors') and 'credit balance' in r['errors'][0])]
by=collections.defaultdict(list)
for r in recs: by[r['tier']].append(r)
for t in ['L4','L3','L2','L1','L0']:
    g=by[t]; ok=sum(r['first_try_compiles'] for r in g)
    print(t, f"{ok}/{len(g)}", f"mean {st.mean([len(r['code'] or '') for r in g]):.0f} chars")
PY
```

Related: [[R1-grammar-constrained-decoding]] §3, [[R5-publishable-run]].
