# llm/ — LLM Generation Layer

Python layer that turns a natural-language prompt into validated Faust DSL.

## How it works

`generate.py` has three entry modes:

| Mode | Use |
|---|---|
| `python generate.py --prompt "..."` | Subprocess mode used by the C++ editor — prints a JSON result to stdout (`generate_json()`) |
| `python generate.py --json ...` | JSON wire mode (ADR-011 contract) |
| `python generate.py` | Interactive CLI |

Pipeline per ADR-005: call the LLM (Anthropic by default; provider abstraction exists but only
`anthropic` is dispatched) → validate the returned Faust with the real `faust` compiler
(`faust_validator.py`, `faust -lang cpp <file> -o /dev/null`) → on failure, feed compiler stderr
back into the next attempt, up to 3 retries.

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
python -m pytest tests/ -m integration          # needs ANTHROPIC_API_KEY + faust binary
```

## Claude prompts for this area

From the root README series — run in order, hold Claude to the stated mode:

- **P1** *(HUMAN-OWNED, draft-only)* — draft ADR-011 ratifying the argv IPC mechanism this
  layer's `--prompt` mode implements; the human commits it to `docs/decisions.md`.
- **P5** *(DELEGATE)* — re-run the full 25-prompt benchmark to settle ADR-009's ≥96% compile-rate
  prediction (committed baseline: 84%; 9-prompt subset on 2026-07-18: 89%). See
  `bench/README.md` for baseline-seeding rules.
- **P8** *(HUMAN-OWNED, draft-only)* — finish the ADR-008 Claude-vs-Gemini evaluation (the
  Gemini benchmark run never completed) and draft the decision.
