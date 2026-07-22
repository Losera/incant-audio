# Test plan — today (2026-07-21)

One session, ~45 minutes. Everything here is runnable now. Tracks are ordered so a
failure in an earlier one tells you not to bother with the later ones.

**What you are testing:** the prompt-grounding fix (the prompt taught three Faust
functions that do not exist). **What you are not testing:** anything audible — the
parameter-denormalization defect means knobs do not reach the DSP, so a listening
test would fail for an unrelated reason. Track D says what you *can* still see.

Quota: Groq ≈ 14,400 requests/day, Gemini **20/day**. Everything below uses Groq.
Total spend across all tracks: **~31 requests.**

---

## Track A — no API calls, ~3 min

Confirms the tooling and the repo are sound before you spend any quota.

```bash
cd ~/PluginForge

# 1. Every curated stdlib name exists in the installed Faust library
python3 tools/gen_stdlib_block.py --check

# 2. No fabricated ns.func anywhere in the system prompt
python3 tools/gen_stdlib_block.py --verify-prompt

# 3. Full unit suite, including the 5 new prompt-grounding tests
python3 -m pytest -m "not integration" -q

# 4. The prompt tests specifically, verbose — these are the new guard
python3 -m pytest tests/test_prompt_stdlib.py -v
```

**Pass looks like:** `OK -- all 59 curated stdlib entries resolve`, `OK -- every stdlib
reference ... resolves`, `240 passed`, and 5 passed in step 4.

**If step 4 fails** — most likely `test_every_few_shot_example_compiles` — a few-shot
example in the prompt stopped compiling. Stop and read the assertion; it prints the
compiler error. Do not proceed to Track B, since the prompt is the thing under test.

---

## Track B — 5 API calls, ~2 min  ← the important one

The targeted regression check. Three effects whose failures were caused by the
fabricated functions, plus two controls that already worked.

```bash
python3 bench/run_benchmark.py --provider groq \
    --prompts bench/prompts/regression_check.json
```

**What each line means:**

| Prompt | Before the fix | What to look for |
|---|---|---|
| tape-style flanger | FAIL `undefined symbol : flanger_mono` (2026-05 **and** 2026-07-19) | Should now pass — `pf.flanger_mono` is in the prompt with its real signature |
| subtle chorus | prompt taught the nonexistent `ef.chorus` | Should now pass — built from `de.fdelay` + LFO |
| ping-pong delay | FAIL `endless evaluation cycle` | **Expect this to still fail.** A dry run today reproduced it: the model now uses real primitives but writes `left` and `right` mutually recursive. Grounding did not fix this one. |
| low-pass filter | PASS | Control — must still pass |
| stereo gain in dB | PASS | Control — must still pass |

**The result that matters is the flanger.** It failed identically twice, two months
apart, and was recorded as a persistent *model* failure. If it passes now, that
confirms it was a prompt bug.

**Record the outcome.** Paste the summary table into the session, or note it — it is
the first evidence about whether the fix helped.

---

## Track C — 25 API calls, ~10–15 min

Full benchmark, to replace the void baseline. Only worth running if Track B looks
sane.

```bash
python3 bench/run_benchmark.py --provider groq
```

Results land in `bench/results/results.json`; a summary table prints at the end.

```bash
# Per-category breakdown of what just ran
python3 bench/score_results.py
```

**Context for reading the number:** the old 0.88 baseline is **void** — it was measured
on the deleted `bench/prompts/system_faust.txt`, using `claude-opus-4-6`. Today's run is
a *different prompt* on a *different model* (`groq / openai/gpt-oss-120b`). It is a new
baseline, not a comparison. Do not read a delta into it.

**Do not overwrite `bench/results/.prompt_baseline.json` yourself** — tell me the number
and I will write the provenance record (score + model + prompt hash + provider + date)
so this can never again be compared across incomparable conditions.

---

## Track D — the plugin, ~10 min, no API calls

What you can legitimately check today, given the denormalization defect.

```bash
cd ~/PluginForge
cmake -S host -B host/build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DJUCE_PATH="$JUCE_PATH"
cmake --build host/build --target PluginForgeHost_Standalone

# Launch the standalone app
./host/build/PluginForgeHost_artefacts/Debug/Standalone/PluginForge\ Host
```

In the app, type a prompt (e.g. *a warm analog low-pass filter*) and hit Generate.

**Should work:**
- status goes `Generating...` → `JIT compiling: ...` → `Ready — DSP live, N params mapped`
- N knobs appear, labelled with the Faust parameter names
- the level meter moves when audio passes

**Expected to be wrong — do not file these, they are broken #1:**
- turning a knob does little or nothing audible
- a filter patch sounds silent or fully closed

**Worth noting if you see it:** any *other* failure — a crash, a hang, a status that
never leaves "JIT compiling", garbled parameter names. Those are new information.

---

## What to tell me afterwards

1. Track B: which of the five passed.
2. Track C: the overall rate and the per-category table, if you ran it.
3. Track D: anything that failed *other* than the knobs not working.

That is enough for me to (a) write the baseline provenance record, (b) decide whether
the ping-pong prompt needs a worked example rather than a prose hint, and (c) start the
denormalization fix with a known-good generation path underneath it.

---

## Skip today

- **Listening tests** — blocked by broken #1. First thing worth doing once it lands.
- **The 125-prompt efficacy study** — 125+ requests, and it should run *after* the
  baseline is re-established, not before.
- **Gemini** — 20 requests/day would be exhausted by Track C alone.
- **Anything requiring `PLUGINFORGE_ALLOW_PAID=1`** — no reason to spend money here.
