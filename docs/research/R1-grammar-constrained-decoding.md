# R1 — Grammar-constrained decoding over the Faust grammar

**Frozen point-in-time record, 2026-07-25. Not maintained.** See `docs/research/README.md`.

**Verdict: NO-GO.** Not "later, when we have time" — no-go on the evidence, with a
named cheaper substitute that dominates it on this corpus.

Written 2026-07-25, session 2 lane R. Sources are cited by `file:line` or by URL.
All repo measurements are reproducible from §6.

---

## 1. What was asked

The 2026-07-25 adversarial audit ranked this the highest-value unexplored idea:

> ADR-009's duplicate-`process` rule is currently enforced by *asking the model
> nicely* and then checking with a regex hook. A grammar constraint over the Faust
> syntax makes that entire failure class unrepresentable. Your P6 battery lost three
> of fourteen prompts to invalid Faust (PF-024) — those are exactly the failures
> constrained decoding removes for free.

Three claims are load-bearing there: that the failures are syntactic, that a grammar
removes them, and that it is free. The first is half true, the second is true only for
that half, and the third is false.

## 2. The failure surface, measured

Every compile error recorded in both corpora — `bench/results/results.json` (25 prompts)
and `bench/results/efficacy/pilot_20260720.json` (50 generations, 65 attempts) —
classified by **which compiler stage rejected it**. Only parser-stage rejections are
preventable by a context-free constraint.

| Stage that rejected it | n | Prevented by a Faust CFG constraint? |
|---|---:|---|
| PARSE — genuine ill-formed token | 4 | **Yes**, by construction |
| PARSE — premature EOF (`unexpected $end`) | 3 | **No** — see §3 |
| EVAL — name resolution (`undefined symbol`) | 3 | Only if the stdlib symbol table is baked in as terminals |
| EVAL — duplicate definition (ADR-009) | 1 | **Yes**, expressible as a production |
| EVAL — non-terminating recursion | 1 | No — not context-free |
| TYPE — arity mismatch | 1 | No — Faust box types are not context-free |
| TYPE — interval analysis | 1 | No — requires abstract interpretation |
| *(API billing errors, miscounted as compile failures)* | *5* | *N/A — see [[truncation-confound-HANDOFF-S1]]* |

**14 real compile failures. A Faust CFG constraint eliminates 5 of them — 36%
(95% CI 16–61%). It leaves 64% untouched.**

Even at the optimistic end of that interval, a majority of failures survive. The audit's
framing — that constrained decoding converts an 88% compile ceiling into a solved
problem — is not supported. It converts a syntax ceiling into a semantics ceiling.

This is the expected result once stated plainly: **Faust's hard part is not its syntax.**
Faust is a block-diagram algebra. `_ <: fi.resonlp(a,b,c) : +` is syntactically valid at
almost any arity; whether the outputs of the left operand match the inputs of the right
is a *type* judgement the parser never makes. Every one of the errors in the bottom four
rows above is a well-formed Faust program that means the wrong thing.

## 3. Why the `$end` failures are not syntax errors at all

Three of the seven parse-stage failures report `syntax error, unexpected $end`. In bison,
`$end` *is* the end-of-input token: the parser ran out of input mid-construct. That is not
a model writing bad syntax. It is the response being **cut off at the output token cap**.

The evidence is direct. `bench/results/efficacy/pilot_20260720.json`, record
`L1/generative-03`, final attempt, ends mid-identifier:

```
chorusR(x) = x * 0.6 + de.
```

`max_tokens=1024` is hardcoded at `llm/generate.py:58`, `bench/run_efficacy_study.py:139`
and `bench/run_benchmark.py:110`. The `anthropic` spec used for the pilot
(`llm/providers.py:144`) declares no `min_max_tokens`, so it defaults to `0`
(`llm/providers.py:77`) and no floor was applied. The project already documented this
exact signature for a different provider — `llm/providers.py:112` records gpt-oss-20b
reaching Faust as an empty `.dsp` with *"syntax error, unexpected $end"* — but never
connected it to the study data.

**Grammar-constrained decoding does not fix this, and cannot.** A constrained decoder
masks illegal tokens; it does not extend the budget. It would refuse to emit EOS while
the grammar is unsatisfied, so the run terminates on `max_tokens` with a grammatically
incomplete program instead of a grammatically invalid one. The plugin still fails. The
error message changes; the outcome does not.

Full analysis and the fix in [[truncation-confound-HANDOFF-S1]].

## 4. What it would cost — the part that closes the question

### 4.1 No provider in the registry can do it

Every provider in `llm/providers.py` supports JSON Schema constrained decoding and
nothing else. Arbitrary CFG/EBNF/GBNF is not exposed by any of them.

| Provider (`llm/providers.py`) | Constrained decoding available | Arbitrary CFG? |
|---|---|---|
| groq (`:97`) | JSON Schema, `strict:true`, gpt-oss-20b/120b only | No — [docs](https://console.groq.com/docs/structured-outputs) |
| gemini (`:81`) | `responseSchema`, an OpenAPI-3.0 subset | No |
| openrouter (`:124`) | JSON Schema passthrough, model-dependent | No |
| ollama (`:134`) | JSON Schema `format` only | No — GBNF PRs idle/closed, [ollama#6237](https://github.com/ollama/ollama/issues/6237) |

JSON Schema cannot express Faust. Faust expressions nest arbitrarily; JSON Schema's
string constraints are at best regular (and groq's documented keyword set does not
include `pattern` at all). Wrapping Faust source in a JSON string field constrains the
*envelope*, not the *contents* — which is the only part that fails.

So the technique requires self-hosting: llama.cpp's server (GBNF), or vLLM with
XGrammar/llguidance/outlines (EBNF). Note that `ollama` — the registry's designated local
option — is *not* one of them; it would have to be replaced.

### 4.2 Self-hosting means giving up the model that works

The dev box (measured 2026-07-25): **NVIDIA RTX A2000 Laptop, 4096 MiB VRAM**, 31 GiB
system RAM. That admits roughly a 3–7B model at 4-bit quantisation, or a ~20B model
offloaded to CPU at unusable speed. `gpt-oss-120b` — the model that currently works — is
two orders of magnitude out of reach.

And the registry already records how larger models fail on Faust. `llm/providers.py:115`:

> `llama-3.3-70b-versatile` emits no reasoning tokens, but **hallucinates stdlib**
> (`ba.log2linear`, `ba.linear2log`) and writes `: * gain`.

A 70B model already fails at name resolution and arity — the two classes a CFG does not
cover. A 7B model will fail at them far more. So the trade on offer is:

> **Give up the only model that gets Faust's semantics right, to buy a guarantee about
> the 36% of failures that are syntactic.**

That is a bad trade at any implementation cost, and the implementation cost is not zero:
`compiler/parser/faustparser.y` is 783 lines with 126 token/precedence declarations and
64 nonterminals, and would need hand-translation to GBNF or Lark, then maintenance
against Faust releases (we are on 2.85.5).

## 5. What to do instead — a static pre-flight check

Every class the grammar would catch is also catchable by a **$0 static check that runs
before `faust` is invoked**, with no decoding control, no provider lock-in and no model
downgrade. Three checks, all cheap:

1. **Truncation detection.** Read `finish_reason` / `stop_reason` from the provider
   response. Nothing in the codebase reads it today — grep `llm/providers.py` for
   `stop_reason` returns nothing, and `llm/providers.py:225` explicitly documents that
   fence-stripping "tolerates a missing closing fence — a truncated response still
   yields its code", i.e. truncated output is *designed* to flow downstream silently.
2. **Duplicate-`process` check** (ADR-009). One pass over top-level definitions.
3. **Symbol resolution.** Every `ns.func` checked against the stdlib symbol table that
   `tools/gen_stdlib_block.py` already generates for the prompt. The table exists; nothing
   validates *output* against it, only the prompt (`.claude/hooks/check_prompt_invariants.py`).

Coverage on this corpus: **7 of 14 failures (50%, 95% CI 27–73%)** prevented or correctly
diagnosed — versus 36% for grammar-constrained decoding, at a fraction of the cost and
with no architectural commitment.

The second-order win is larger than the first. `llm/generate.py:111` and `:133` feed raw
`faust` stderr back to the model as the repair prompt. For a truncated program that
string is `syntax error, unexpected $end`, which instructs the model to fix syntax it did
not get wrong — so it regenerates a program of the same length, which truncates again.
`L1/generative-03` failed all three attempts this way. A retry loop that cannot succeed
still spends its full request budget, which is also the mechanism underneath **PF-019**'s
120-second timeout cliff. Correct diagnosis converts three wasted requests into one
retry with a raised budget or an explicit "write a shorter program" instruction.

## 6. Reproducing §2

```bash
python3 - <<'PY'
import json, collections
def stage(e):
    if 'credit balance' in e: return 'API_ERROR'
    if 'syntax error' in e:
        return 'PARSE/eof' if '$end' in e else 'PARSE/illformed'
    for k,v in [('multiple definitions','EVAL/duplicate'),
                ('undefined symbol','EVAL/name'),
                ('endless evaluation','EVAL/recursion'),
                ('number of inputs','TYPE/arity'),
                ('interval','TYPE/interval')]:
        if k in e: return v
    return 'OTHER'
rows=[]
for p in ['bench/results/results.json','bench/results/efficacy/pilot_20260720.json']:
    for r in json.load(open(p)):
        rows += [stage(e) for e in (r.get('errors') or ([r['error']] if r.get('error') else []))]
print(collections.Counter(rows))
PY
```

## 7. Revisit conditions

This is a no-go against the current constraints, not a permanent one. Reopen if **any**
of these changes:

- A free hosted provider exposes EBNF/GBNF constrained decoding (groq is the likely first,
  since its strict mode already compiles JSON Schema to a CFG internally).
- The static pre-flight check from §5 ships and the residual failure mix becomes
  parse-dominated — that would mean the semantic classes were solved and the argument
  inverts.
- The project acquires GPU capacity to self-host a model competitive with `gpt-oss-120b`
  on Faust semantics.

Related: [[truncation-confound-HANDOFF-S1]], [[R3-perceptual-oracle]], [[R5-publishable-run]].
