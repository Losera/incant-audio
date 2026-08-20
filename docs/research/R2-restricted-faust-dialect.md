# R2 — Modifying Faust, or a restricted "LLM-safe" dialect

**Frozen point-in-time record, 2026-07-25. Not maintained.** See `docs/research/README.md`.

**Verdict: do not modify Faust. The restricted dialect already exists — it is just not
enforced.** Written 2026-07-25, session 2 lane R.

---

## 1. The dialect is already here

The audit asked whether a curated stdlib subset would eliminate whole failure classes.
It would, and one is already in production. `tools/gen_stdlib_block.py` generates the
stdlib section of `llm/prompts/system_prompt.txt` (lines 30–125, 96 of the prompt's 173
lines) from the installed libraries. Measured 2026-07-25 against Faust 2.85.5:

| | count |
|---|---:|
| `.lib` files installed in `/usr/share/faust/` | 53 |
| Public symbols across them | 1,511 |
| Symbols the prompt actually advertises | **59** |

So the model is already steered toward **3.9% of the stdlib** — a curated, LLM-safe
subset in everything but name. The dialect question is settled; what is open is
enforcement.

## 2. The gap: the subset constrains the prompt, never the output

`.claude/hooks/check_prompt_invariants.py` verifies that every `ns.func` token *in the
prompt* resolves against the installed library (its item 4). That is a real, load-bearing
check and it caught a real defect. But it scopes the prompt only. **Nothing validates
generated code against the advertised symbol set** — not `llm/faust_validator.py`, not
`llm/generate.py`, not any test.

The consequence shows up directly in the corpus. The three `undefined symbol` failures
(`R1` §2) split into two mechanically distinguishable kinds:

| Symbol the model emitted | In the installed stdlib? | Kind |
|---|---|---|
| `chorus` | No — appears in no `.lib` | Hallucination |
| `ping_pong` | No — appears in no `.lib` | Hallucination |
| `flanger_mono` | **Yes** — `effect.lib`, `phaflangers.lib` | **Namespace omitted** |

That third row matters more than it looks. `flanger_mono` is a real function the model
called without its namespace prefix. It is not a hallucination and the prompt is not at
fault — CLAUDE.md's 2026-07-21 note lists `ef.flanger` among "functions that DO NOT EXIST",
which is true of that exact spelling but obscures that a correctly-namespaced flanger is
available. A static check does not merely reject this class; **it can repair it**, by
resolving the bare name to its owning namespace and rewriting.

Meanwhile `chorus` and `ping_pong` appear in the prompt 4 and 1 times respectively — as
*warnings*, under "EFFECTS WITH NO STDLIB PRIMITIVE — build these yourself"
(`llm/prompts/system_prompt.txt:23`). The model reached for them anyway. **That is the
evidence that prompt-level prohibition does not hold, and the argument for moving the
constraint to a check.**

## 3. Why not modify Faust itself

The audit put this genuinely in scope. It should come back out:

- **Nothing in the failure data asks for it.** Of 14 real compile failures
  ([[R1-grammar-constrained-decoding]] §2), none is caused by Faust's language design.
  They are truncation, name resolution, arity, recursion and interval analysis — all of
  which a fork inherits unchanged.
- **A fork forfeits the compile oracle.** The single most valuable asset here is that a
  free, fast, correct reference compiler says yes or no. A modified Faust means
  maintaining that compiler against upstream (2.85.5 today) forever, for one developer.
- **It moves work to the wrong side of the boundary.** Restricting the *language* means
  every user of the plugin gets a weaker Faust. Restricting the *generated output* costs
  a whitelist check and leaves the language intact — and the plugin JITs arbitrary Faust,
  including patches a human writes.
- **libfaust's API is not the obstacle.** `FaustEngine.cpp` already drives compile/swap
  cleanly. There is no capability the project wants and cannot reach.

The one genuinely interesting variant — a curated *library* (`pluginforge.lib`) shipping
vetted implementations of the effects the stdlib lacks (ping-pong delay, chorus,
bounded feedback delay) — is not a Faust modification at all. It is an ordinary Faust
library, it needs no fork, and it directly targets PF-024's named failure classes. That is
worth doing; call it what it is.

## 4. Recommendation

1. **Enforce the existing subset at the output boundary.** Extend the pre-flight check
   proposed in [[R1-grammar-constrained-decoding]] §5 to resolve every `ns.func` in
   *generated* code against the installed stdlib, and to resolve bare identifiers that
   exist under a namespace. Reuse `tools/gen_stdlib_block.py`'s symbol table — it is
   already built and already trusted.
2. **Auto-repair the namespace-omission class** before spending a retry on it. It is the
   cheapest possible win: a table lookup instead of a provider request.
3. **Write `examples/pluginforge.lib`** for the effects the stdlib genuinely lacks, and
   reference it from the prompt. This is the "restricted dialect" idea in its useful form.
4. **Do not fork Faust.** Revisit only if a failure class appears that is attributable to
   the language rather than to generation — none is present today.

Related: [[R1-grammar-constrained-decoding]] §5, [[R3-perceptual-oracle]].
