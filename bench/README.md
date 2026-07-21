# PluginForge DSL Benchmark

Measures **first-try compile success rate** for Claude-generated DSP code across two target DSLs:
- **Faust** — algebraic DSL, compiled via `faust -lang cpp`
- **Cmajor** — typed OOP-style DSL, validated via `cmaj play --dry-run`

25 natural-language prompts × 2 DSLs = **50 total generations**.

---

## Prerequisites

| Requirement | How to verify |
|-------------|--------------|
| Faust compiler | `faust --version` |
| Cmajor compiler | `cmaj --version` (binary at `~/.local/bin/cmaj`) |
| Anthropic API key | Set in `PluginForge/.env` as `ANTHROPIC_API_KEY=sk-...` |
| Python deps | `pip install -r bench/requirements.txt` |

---

## How to Run

```bash
cd ~/PluginForge

# 1. Dry run — one generation to verify setup (fast, ~5 seconds)
python bench/run_benchmark.py --dry-run

# 2. Full benchmark (~5–10 minutes, 50 API calls)
python bench/run_benchmark.py

# 3. Score and visualize results
python bench/score_results.py
```

---

## Regression-check loop

`bench/check_prompt_regression.py` automates a step that was previously manual:
re-running the benchmark whenever `llm/prompts/system_prompt.txt` or
`bench/prompts/system_faust.txt` changes, to catch a prompt-wording regression in
the Faust first-try compile rate before it's discovered by hand later.

Start it during a session where you're actively editing either prompt file:

```bash
/loop 15m python bench/check_prompt_regression.py
```

Each tick:
1. Hashes both prompt files against `bench/results/.prompt_baseline.json`. Unchanged
   since the last check → prints "no change, skipping" and exits immediately —
   near-zero cost, no API calls.
2. Changed → runs `pytest -m "not integration"` (cheap, structural).
3. If that passes → runs the 9-prompt recovery subset
   (`bench/prompts/recovery_prompts.json`) via
   `run_benchmark.py --provider claude --prompts ...` — a cheap smoke signal, **not**
   the full 25-prompt suite. This overwrites `bench/results/results.json`, same as
   running `run_benchmark.py` by hand.
4. Compares the resulting Faust first-try rate against
   `recorded_faust_compile_rate` in the baseline file; flags a regression if it
   drops more than 5 points below that, or under 90% outright.
5. Never edits the prompt files itself — report only. (They're HUMAN-OWNED per
   COLLABORATION.md §1; `.claude/hooks/protect_human_owned.py` blocks a direct edit
   to them regardless.)

This is **not** meant to run unattended on a schedule — start it manually, stop it
manually when you're done editing prompts. The full, unfiltered 25-prompt run
(matching ADR-009's actual ≥96% validation bar) stays a manual, human-triggered
final check before merging a prompt change — the loop's 9-prompt subset is a cheap
early-warning signal, not a substitute for that.

**Seeding/resetting the baseline:** `bench/results/.prompt_baseline.json` holds the
two file hashes and the recorded compile rate to compare against. Update
`recorded_faust_compile_rate` by hand after a deliberate, human-approved prompt
change that intentionally moves the number (e.g. after actually re-running the
full 25-prompt suite and reviewing the result) — the script never rewrites that
field itself, only the hash fields.

---

## Output

| File | Description |
|------|-------------|
| `bench/results/results.json` | One record per generation — prompt, DSL, code, compile result, error |
| `bench/results/benchmark_chart.png` | Bar charts: per-category rates + overall comparison |

### results.json record schema

```json
{
  "category":           "filters",
  "prompt":             "a warm analog-style low-pass filter...",
  "dsl":                "faust",
  "code":               "import(\"stdfaust.lib\");\n...",
  "first_try_compiles": true,
  "error":              "",
  "timestamp":          "2026-04-22T18:00:00+00:00"
}
```

---

## Interpreting Results

- **first_try_compiles** measures whether the LLM's raw first output compiles without any retry or correction. This is the primary metric for PluginForge, because our pipeline defaults to 3-retry error-correction — a higher first-try rate means fewer API calls per generation.

- **Subjective quality** (musicality, parameter range choices, algorithm selection) is NOT automated. Review `results.json` manually after running to rate outputs 1–5 per prompt.

- **Category difficulty** is intentional — `trivial` prompts should approach 100%; `generative` prompts (FM synth, Karplus-Strong) are genuinely hard for both DSLs.

---

## Expected Runtime

~5–10 minutes for the full 50-generation run at `claude-opus-4-6` with `temperature=0`. Rate limits are unlikely to trigger but the script handles API errors gracefully (logs and continues).

---

## Notes

- The harness does **NOT** implement retry logic. That is intentional — we are measuring the inherent first-try quality of LLM output for each DSL.
- Cmajor validation: `cmaj play --dry-run` always exits 0; success is detected by presence of `"Loaded:"` and absence of `"error:"` in stdout.
- Faust validation: `faust -lang cpp <file> -o /dev/null` returns non-zero on any error.
- All generated `.dsp` / `.cmajor` temp files are cleaned up after each validation.
