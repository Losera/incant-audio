# llm/ — LLM Generation Layer

Python layer that turns a natural-language prompt into validated Faust DSL.

## How it works

`generate.py` has three entry modes:

| Mode | Use |
|---|---|
| `python generate.py --prompt "..."` | Subprocess mode used by the C++ editor — prints a JSON result to stdout (`generate_json()`) |
| `python generate.py --json ...` | JSON wire mode (ADR-011 contract) |
| `python generate.py` | Interactive CLI |

Pipeline per ADR-005: call the LLM (see Providers below) → validate the returned Faust with the
real `faust` compiler (`faust_validator.py`, `faust -lang cpp <file> -o /dev/null`) → on failure,
feed compiler stderr back into the next attempt, up to 3 retries.

## Providers

`providers.py` is the registry every component calls — `generate.py`, all three bench harnesses,
and the scorer's judge. Adding a provider is one entry in `PROVIDERS`, not a fourth copy of a
client constructor.

**PluginForge is free-only by default.** `anthropic` is the one paid entry and is refused unless
`PLUGINFORGE_ALLOW_PAID=1`. The guard lives in `assert_free()`, called from each module's
`__main__` — every path that can actually spend money is a CLI invocation, while library
functions stay drivable by tests with a mocked transport.

| Provider | Key | Free-tier shape (measured 2026-07-21) |
|---|---|---|
| `gemini` | `GOOGLE_API_KEY` | no card; **5 requests/minute per model** |
| `groq` | `GROQ_API_KEY` | no card; ~14,400 requests/day — the volume option |
| `openrouter` | `OPENROUTER_API_KEY` | `:free` models; ~50/day until $10 spent |
| `ollama` | *none* | fully local, no quota, offline |
| `anthropic` | `ANTHROPIC_API_KEY` | **PAID** — needs `PLUGINFORGE_ALLOW_PAID=1` |

Select with `PLUGINFORGE_PROVIDER` in `PluginForge/.env`. `generate.py` calls `load_dotenv()` at
import and `juce::ChildProcess` inherits the environment, so the plugin picks up a provider
change **with no C++ change and no rebuild**. Optional: `PLUGINFORGE_MODEL`,
`PLUGINFORGE_MIN_INTERVAL` (seconds between calls, for per-minute caps).

Check everything at once — costs nothing, model-list endpoints aren't billed generations:

```bash
python llm/providers.py --check all              # key / reachability / model table
python llm/providers.py --list-models gemini     # live ids; they churn, don't guess
```

Two behaviors live in the adapter layer rather than in the prompts, because
`prompts/system_prompt.txt` is HUMAN-OWNED:

- **Fence stripping** — open-weight models wrap output in ```` ```faust ```` fences, which fails
  `faust -lang cpp` on line 1. Stripped for every free provider; deliberately **off** for
  anthropic so the frozen 0.88 baseline stays bit-comparable.
- **Token headroom** (`min_max_tokens`) — reasoning models bill hidden thinking against the same
  cap. Measured on `gemini-3.6-flash`: `max_output_tokens=1024` yielded 981 thinking tokens, 39
  visible, truncated mid-sentence; 4096 yielded valid Faust.

⚠️ `prompts/system_prompt.txt` is **HUMAN-OWNED product IP** (`COLLABORATION.md` §1).
`.claude/hooks/protect_human_owned.py` blocks Claude from editing it; don't paste its contents
into logs, READMEs, or benchmarks — reference it by path. The ADR-009 duplicate-symbol rule
lives in both this file and `bench/prompts/system_faust.txt` and must stay in sync.

## Tests

Unit tests (mocked, no network/faust needed) live in `tests/test_generate_unit.py` and
`tests/test_faust_validator_unit.py`. Integration tests (real API + real `faust`) are gated
behind `@pytest.mark.integration`:

```bash
python -m pytest tests/ -m "not integration"   # what CI runs
python -m pytest tests/ -m integration          # needs a provider key + faust binary
```

`tests/test_providers_unit.py` covers the registry with every transport mocked. Note that
`conftest.py` pins `PLUGINFORGE_PROVIDER=anthropic` for the session: `generate.py` loads `.env`
at import, and without the pin a developer's live provider selection would send the
mocked-client unit tests at a real API (this actually happened 2026-07-21 — the suite hung).

## Claude prompts for this area

From the root README series — run in order, hold Claude to the stated mode:

- **P1** *(HUMAN-OWNED, draft-only)* — draft ADR-011 ratifying the argv IPC mechanism this
  layer's `--prompt` mode implements; the human commits it to `docs/decisions.md`.
- **P5** *(DELEGATE)* — re-run the full 25-prompt benchmark to settle ADR-009's ≥96% compile-rate
  prediction (committed baseline: 84%; 9-prompt subset on 2026-07-18: 89%). See
  `bench/README.md` for baseline-seeding rules.
- **P8** *(HUMAN-OWNED, draft-only)* — finish the ADR-008 Claude-vs-Gemini evaluation (the
  Gemini benchmark run never completed) and draft the decision.
