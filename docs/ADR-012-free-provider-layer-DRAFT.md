# DRAFT — ADR-012 + study-design amendment

**Status: draft for human review. Nothing here has been committed to the ADR log.**

`docs/decisions.md` and `docs/architectural_decisions/` are HUMAN-OWNED under
COLLABORATION.md §1 (Claude may draft, the human authors). This file follows the
`docs/ADR-009-verdict-DRAFT.md` precedent: if you accept the text, paste it and delete
this file.

Drafted 2026-07-21. Three items: **(1)** a new ADR-012, **(2)** an amendment to
`docs/prompt_efficacy_study.md` §4's locked confound controls, **(3)** a note on what
this does to ADR-008.

---

## Proposed change 1 — new ADR-012

```markdown
# ADR-012 — Provider abstraction with a free-only constraint

| | |
|---|---|
| **Status** | Proposed |
| **Date** | 2026-07-21 |

## Context

Every LLM call in PluginForge went to Anthropic, constructed inline at three separate
call sites (`llm/generate.py::_call_api`, `bench/run_benchmark.py::_make_generators`,
`bench/run_efficacy_study.py::make_claude_generator`). On 2026-07-20 the Anthropic
account ran out of credit; on 2026-07-21 this was re-verified live (two probes,
`req_011CdFoNp7rZhfFsNpQMNzbi` and `req_011CdFoYPiSmFLbGES2uAR47`, both
`400 "Your credit balance is too low"`).

The consequence was total: the plugin could not generate a single patch, the 125-prompt
efficacy run (P9) could not start, and the prototype's end-to-end test (P6) — the last
15% of the project — could not be attempted. A single provider's billing state was a
single point of failure for the whole product.

## Decision

1. **One registry.** `llm/providers.py` owns every adapter. All four call sites obtain
   a generator from `make_generator(provider, system_prompt=..., model=..., ...)`.
   Adding a provider is one `ProviderSpec` entry.
2. **Free-only by default.** `anthropic` is the sole `free=False` entry and is refused
   unless `PLUGINFORGE_ALLOW_PAID=1`. Enforcement is in `assert_free()`, called from
   each runnable entry point.
3. **Four free backends**, covered by three adapters — `gemini` (google-genai),
   plus `groq` / `openrouter` / `ollama`, which are all OpenAI-compatible and share one
   `httpx` implementation. No new dependency was required.
4. **Selection is configuration, not code.** `PLUGINFORGE_PROVIDER` in
   `PluginForge/.env`. The code default remains `anthropic`, so no historical behavior
   changed by fiat.

## Alternatives considered

- **Top up the Anthropic account and change nothing.** Restores the status quo without
  removing the single point of failure, and leaves a hobby project's progress gated on
  a balance. Rejected — though note this ADR does not preclude it: set
  `PLUGINFORGE_ALLOW_PAID=1`.
- **Switch the hardcoded provider to Gemini.** Same fragility, different vendor.
- **Local-only (ollama).** Never blockable, but a 4GB-VRAM laptop GPU caps it at ~7B
  quantized models, and Faust is a niche DSL. Kept as a registry entry — the
  can't-be-blocked floor — not as the default.

## Consequences

- The plugin no longer requires a paid API account to function. Verified end-to-end
  2026-07-21 on `gemini-3.6-flash`: prompt → Faust → real `faust` compile → success on
  the first attempt.
- `CLAUDE.md`'s stack description ("Python (anthropic SDK) — LLM prompt layer") is now
  narrower than reality and should read as a provider-agnostic layer.
- Benchmark numbers are per provider+model and not comparable across them. See
  `bench/results/.prompt_baseline.json` schema v2 and change 2 below.
- Two failure modes previously masked by Claude's tuned prompt are now handled in the
  adapter layer, because `llm/prompts/` is HUMAN-OWNED and could not be edited:
  markdown-fence stripping, and a per-provider `min_max_tokens` floor for models that
  bill hidden reasoning tokens against the output cap.

## Revisit trigger

If free-tier rate limits make the efficacy study impractical, or if measured free-model
Faust quality falls far enough below the frozen 0.88 Claude baseline to change product
decisions, reopen with the measurement in hand.
```

---

## Proposed change 2 — amend `docs/prompt_efficacy_study.md` §4

§4 currently reads, in part:

> - Model: `claude-opus-4-6`, `temperature=0`, `max_tokens=1024` (identical to bench).
>   **Model-era boundary (2026-07-21):** … Bumping the harnesses therefore means dropping
>   the determinism control, which is a change to this locked design and needs a human
>   decision (see §9).
> - Provider: claude only for the main run. Gemini comparison belongs to P8/ADR-008.

Both bullets are now overtaken by events. Suggested replacement:

```markdown
- Model/provider: **per-run, recorded in every record** (`provider` and `model` fields,
  added 2026-07-21). The harness takes `--provider` / `--model`; defaults are
  `gemini` and that provider's pinned model.
- **Provider-era boundary (2026-07-21).** Every number in §6 and §7 was produced on
  `claude-opus-4-6`. Those runs are **not re-measurable** — the Anthropic account is out
  of credit (ADR-012) — so they are frozen as historical record, and any free-provider
  run establishes a *separate* baseline rather than continuing that series. Do not
  compare across the boundary without labelling the provider change as a confound; it is
  a larger one than the model-era boundary it supersedes.
- `temperature=0` is **restored** as a live confound control. It had become
  unenforceable when the product moved to `claude-opus-4-8` (opus-4-7+ reject the
  parameter with a 400), which is why the harnesses were stranded on opus-4-6. All four
  free providers accept `temperature=0`, so the determinism control and the current
  model are no longer in conflict — this resolves the open item flagged at §4 and §9.
- `max_tokens=1024` remains the intent, but is applied as a **floor-adjusted** budget
  per provider (`ProviderSpec.min_max_tokens`). Reasoning models bill hidden thinking
  against the same cap: measured 2026-07-21 on `gemini-3.6-flash`, a 1024 cap produced
  981 thinking tokens, 39 visible tokens, and a response truncated mid-sentence
  (`finish_reason=MAX_TOKENS`); at 4096 the same prompt returned valid Faust. Treat the
  raw cap as a provider-specific implementation detail and the *visible output budget*
  as the controlled variable.
```

---

## Proposed change 3 — ADR-008 is now answerable

ADR-008 (`docs/decisions.md:193`) has been **"Under evaluation"** since 2026-04-29,
blocked on "the Gemini benchmark run never completed". That run is now one command:

```bash
python bench/run_benchmark.py --provider gemini
```

Note the framing has shifted underneath it. ADR-008 asked *which provider is better*
assuming both were available; the operative question today is *which free provider is
good enough*, since the paid one is not reachable. You may prefer to close ADR-008 as
**Superseded by ADR-012** rather than complete it — your call, and the reason this is
in a draft rather than applied.
