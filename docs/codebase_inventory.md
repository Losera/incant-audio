# Codebase Inventory

Generated: 2026-05-04. Based on reading every source file directly.
Production code is in `host/`, `llm/`, and `examples/`. Benchmark tooling is in `bench/`. Tests are in `tests/`.

---

## host/Source/

### FaustEngine.h
**Path:** `host/Source/FaustEngine.h`
**Purpose:** Declares the class that will wrap libfaust LLVM JIT compilation and audio processing.
**Status:** STUB
The header is structurally sound. Day-2 fields (`llvm_dsp_factory*`, `std::atomic<llvm_dsp*>`) are commented out — no libfaust types appear anywhere in the file.
**Public API:** `FaustEngine()`, `~FaustEngine()`, `prepare(double, int)`, `release()`, `process(juce::AudioBuffer<float>&)`, `compile(const juce::String&, CompileCallback)`, `isReady() const`; nested types: `ParamInfo`, `ParamList`, `CompileCallback`
**Key dependencies:** juce_audio_basics

---

### FaustEngine.cpp
**Path:** `host/Source/FaustEngine.cpp`
**Purpose:** Stub implementation — passthrough audio, simulated compile with empty param list.
**Status:** STUB
`prepare()` and `release()` just store values. `process()` returns early if `!ready`. `compile()` only calls `juce::Logger::writeToLog` and fires the callback with an empty `ParamList`. No libfaust API is called anywhere.
**Public API:** (implements FaustEngine.h)
**Key dependencies:** FaustEngine.h, juce_core

---

### ParamPool.h
**Path:** `host/Source/ParamPool.h`
**Purpose:** Declares the 64-slot parameter pool for DAW-compatible fixed-count parameter registration.
**Status:** PARTIAL
Constructor works. `remap()` stores label names but cannot rename JUCE parameters after construction (JUCE limitation; implementation explicitly notes this with `juce::ignoreUnused`). `pushToFaust()` is a complete no-op stub.
**Public API:** `ParamPool(AudioProcessorValueTreeState&)`, `remap(const FaustEngine::ParamList&)`, `pushToFaust(FaustEngine&)`, constant `POOL_SIZE = 64`
**Key dependencies:** juce_audio_processors, FaustEngine.h

---

### ParamPool.cpp
**Path:** `host/Source/ParamPool.cpp`
**Purpose:** Implements ParamPool — registers 64 `AudioParameterFloat` slots and maps them.
**Status:** PARTIAL
Constructor successfully creates 64 `macro_0`..`macro_63` parameters and registers them with APVTS. `remap()` stores `activeLabels` but the renaming block is a no-op (`juce::ignoreUnused(fp)`). `pushToFaust()` is `juce::ignoreUnused(engine)` and nothing else.
**Public API:** (implements ParamPool.h)
**Key dependencies:** ParamPool.h

---

### PluginProcessor.h
**Path:** `host/Source/PluginProcessor.h`
**Purpose:** Declares the JUCE `AudioProcessor` subclass that owns `FaustEngine` and `ParamPool`.
**Status:** WORKING
All required overrides declared. `loadFaustCode()` is the public entry point from the editor. `apvts` is public for editor access. Structurally complete; will produce silence until FaustEngine is real.
**Public API:** `PluginForgeProcessor()`, `~PluginForgeProcessor()`, `prepareToPlay(double, int)`, `releaseResources()`, `processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&)`, `createEditor()`, `loadFaustCode(const juce::String&)`, `apvts`
**Key dependencies:** juce_audio_processors, FaustEngine.h, ParamPool.h

---

### PluginProcessor.cpp
**Path:** `host/Source/PluginProcessor.cpp`
**Purpose:** Wires the JUCE audio lifecycle to FaustEngine and ParamPool; provides the DAW entry point.
**Status:** WORKING
`prepareToPlay` → `faustEngine.prepare`. `processBlock` → `paramPool.pushToFaust` then `faustEngine.process`. `loadFaustCode` → `faustEngine.compile` with a callback that calls `paramPool.remap`. Wiring is correct. Produces silence until FaustEngine stub is replaced.
**Public API:** (implements PluginProcessor.h) + `createPluginFilter()` (DAW entry point)
**Key dependencies:** PluginProcessor.h, PluginEditor.h

---

### PluginEditor.h
**Path:** `host/Source/PluginEditor.h`
**Purpose:** Declares the plugin UI: text input (`promptInput`), generate button, and status label.
**Status:** STUB
UI structure is declared. The button click handler exists but does not call the LLM or `processor.loadFaustCode()`.
**Public API:** `PluginForgeEditor(PluginForgeProcessor&)`, `paint(juce::Graphics&)`, `resized()`
**Key dependencies:** juce_audio_processors, PluginProcessor.h

---

### PluginEditor.cpp
**Path:** `host/Source/PluginEditor.cpp`
**Purpose:** Implements the plugin UI — dark background, layout, and button click.
**Status:** STUB
`generateButton.onClick` sets `statusLabel` to "Sending..." and then to "Sent: [text]" — it does NOT call any LLM service or `processor.loadFaustCode()`. This is the most critical missing link in the user-facing flow.
**Public API:** (implements PluginEditor.h)
**Key dependencies:** PluginEditor.h

---

## llm/

### generate.py
**Path:** `llm/generate.py`
**Purpose:** Generates and validates Faust DSL from natural language via the Claude API, with a 3-attempt retry loop feeding compiler stderr back to the model.
**Status:** WORKING
Functional end-to-end for Faust generation. Uses `claude-opus-4-6`, `max_tokens=1024`, system prompt from `llm/prompts/system_prompt.txt`. Retry loop correctly threads error context into subsequent calls. Can be run standalone from the CLI.
**Public API:** `generate_faust(user_prompt, error_context="") -> str`, `validate_faust(faust_code) -> tuple[bool, str]`, `generate_with_retry(user_prompt, max_retries=3) -> str`
**Key dependencies:** anthropic SDK, subprocess (faust system binary), dotenv, `llm/prompts/system_prompt.txt`

---

### faust_validator.py
**Path:** `llm/faust_validator.py`
**Purpose:** Standalone CLI tool for validating a `.dsp` file by invoking the faust compiler.
**Status:** WORKING
Thin wrapper around `faust -lang cpp <file> -o /dev/null`. Useful for manual validation from the terminal. Not used by `generate.py` (which has its own inline `validate_faust` function).
**Public API:** `validate_file(path: str) -> tuple[bool, str]`
**Key dependencies:** subprocess (faust system binary)

---

### prompts/system_prompt.txt
**Path:** `llm/prompts/system_prompt.txt`
**Purpose:** Few-shot system prompt for the production Claude API calls in `generate.py`.
**Status:** WORKING
Contains strict rules (Faust only, no markdown, `import("stdfaust.lib")` required, `process =` required, `hslider` format) and 4 few-shot examples (lowpass, gain, ping-pong delay, chorus).
**Note:** Does NOT yet contain the ADR-009 duplicate-symbol rule ("define `process` exactly once"). This is a known gap — adding it is estimated to close the 3 remaining Faust failure cases from the benchmark.
**Public API:** (data file, loaded at import by generate.py)
**Key dependencies:** generate.py

---

## bench/

### run_benchmark.py
**Path:** `bench/run_benchmark.py`
**Purpose:** Runs the 25-prompt × 2-DSL × N-provider benchmark and writes `results.json`.
**Status:** WORKING
Complete harness. Supports `claude` (via anthropic SDK) and `gemini` (via google-genai) providers. Validates Faust via `faust` CLI and Cmajor via `cmaj play --dry-run`. Results appended to a single JSON array. `--dry-run` flag for setup verification. Preflight checks API keys and compiler availability.
**Public API:** `run(providers, dry_run=False)`, `validate_faust(code)`, `validate_cmajor(code)`, `preflight_check(providers)`, `_make_generators(providers)`
**Key dependencies:** anthropic SDK, google-genai (optional), faust binary, cmaj binary, `bench/prompts/prompts.json`, `bench/prompts/system_faust.txt`, `bench/prompts/system_cmajor.txt`

---

### score_results.py
**Path:** `bench/score_results.py`
**Purpose:** Reads `results.json` and prints a per-category compile-rate table; optionally saves a PNG bar chart.
**Status:** WORKING
Handles multiple providers. Prints per-category breakdowns and a top-5 failure error analysis. Chart generation requires matplotlib but degrades gracefully without it.
**Public API:** `load_results()`, `compute_scores(records, provider)`, `print_table(scores, provider)`, `print_errors(records, provider, top_n=5)`, `make_chart(all_scores, providers)`
**Key dependencies:** `bench/results/results.json`, matplotlib (optional for chart)

---

### prompts/prompts.json
**Path:** `bench/prompts/prompts.json`
**Purpose:** The 25 natural-language benchmark prompts, keyed by category (trivial, filters, time-based, dynamics, generative).
**Status:** WORKING
**Public API:** (JSON data file, loaded by run_benchmark.py)

---

### prompts/system_faust.txt
**Path:** `bench/prompts/system_faust.txt`
**Purpose:** Faust system prompt used exclusively during benchmark runs (separate from the production prompt at `llm/prompts/system_prompt.txt`).
**Status:** WORKING
**Note:** Two separate Faust system prompts exist (bench vs production). Any ADR-009 fix should be applied to both.

---

### prompts/system_cmajor.txt
**Path:** `bench/prompts/system_cmajor.txt`
**Purpose:** Cmajor system prompt for benchmark runs. Specifies graph/processor patterns, built-in functions, and two worked examples.
**Status:** WORKING (for benchmarking purposes; not in production pipeline)

---

### results/results.json
**Path:** `bench/results/results.json`
**Purpose:** Benchmark output — 50 records (25 prompts × 2 DSLs), Claude provider only.
**Status:** WORKING (Gemini run not yet completed — see ADR-008)
**Note:** Contains only `"provider": "claude"` records. ADR-008 calls for a Gemini run.

---

## tests/

### conftest.py
**Path:** `tests/conftest.py`
**Purpose:** Configures pytest: adds `llm/` to `sys.path`, sets empty `ANTHROPIC_API_KEY` so `anthropic.Anthropic()` doesn't raise at import.
**Status:** WORKING

---

### test_generate_unit.py
**Path:** `tests/test_generate_unit.py`
**Purpose:** Unit tests for `generate.py` — mocked API client and subprocess; no real key or compiler needed.
**Status:** WORKING
26 test cases across `TestSystemPrompt`, `TestGenerateFaust`, `TestValidateFaust`, `TestGenerateWithRetry`, plus an `@integration`-marked class for live testing.

---

### test_faust_validator_unit.py
**Path:** `tests/test_faust_validator_unit.py`
**Purpose:** Unit tests for `faust_validator.py` — mocked subprocess.
**Status:** WORKING
8 unit tests + 1 `@integration` test requiring the faust compiler.

---

### test_faust_compile.py
**Path:** `tests/test_faust_compile.py`
**Purpose:** Integration test that invokes the real faust compiler on all `examples/*.dsp` files.
**Status:** WORKING (requires faust compiler; not marked `@integration` — will run in CI by default)

---

### test_llm_output.py
**Path:** `tests/test_llm_output.py`
**Purpose:** End-to-end tests: real LLM API call → real faust compiler validation.
**Status:** WORKING but **CI-unsafe**: not marked `@integration`, so it runs by default and requires both `ANTHROPIC_API_KEY` and the faust compiler. This will break CI on a clean runner.

---

### test_project_structure.py
**Path:** `tests/test_project_structure.py`
**Purpose:** Validates project scaffolding integrity — file existence, content contracts, stub markers.
**Status:** WORKING
No external tools required. Checks required file list, system prompt content, .env template, .dsp files, CMakeLists.txt references, CLAUDE.md content, FaustEngine stub markers, and ParamPool stub markers.

---

## Critical Path Trace

This is the data flow for a single user generation from text prompt to DSP audio, listing every function on the path with its current status.

```
User types text → clicks Generate
        │
        ▼
[STUB]  PluginEditor.cpp: generateButton.onClick
        Sets statusLabel but does NOT call LLM or loadFaustCode.
        BLOCKER: nothing below this line is reachable from the UI.
        ─────────────────────────────────────────────────────────
        (Below this line must currently be triggered manually or
         by running llm/generate.py directly from the CLI)
        ─────────────────────────────────────────────────────────
        │
        ▼
[WORKING]  llm/generate.py: generate_with_retry(user_prompt)
        │
        ▼
[WORKING]  llm/generate.py: generate_faust(user_prompt, error_context)
           → anthropic.Anthropic().messages.create(...)
        │
        ▼ (API response)
[WORKING]  llm/generate.py: validate_faust(faust_code)
           → subprocess: faust -lang cpp <tmp.dsp> -o /dev/null
        │
        ▼ (if fails, loops back up to 3 times with stderr feedback)
[WORKING]  PluginProcessor.cpp: loadFaustCode(faustCode)
        │
        ▼
[STUB]  FaustEngine.cpp: compile(faustCode, cb)
        Logs code, fires cb({}) immediately with empty ParamList.
        BLOCKER: no actual compilation; no DSP object created.
        │
        ▼ (callback)
[PARTIAL] ParamPool.cpp: remap(emptyParams)
        Stores labels; JUCE parameter renaming is a no-op.
        BLOCKER: parameters never get meaningful names in DAW.
        │
        ▼ (every audio block, on audio thread)
[STUB]  ParamPool.cpp: pushToFaust(engine)
        No-op. Parameter values never reach the DSP.
        BLOCKER: DAW automation does nothing.
        │
        ▼
[STUB]  FaustEngine.cpp: process(buffer)
        Returns early because ready == false.
        BLOCKER: buffer is passed through unmodified (silence or dry signal).
```

### Summary of blockers on the critical path

| Step | File | Blocker |
|------|------|---------|
| Editor → LLM | `PluginEditor.cpp:generateButton.onClick` | No LLM call or IPC wired |
| LLM → C++ | (IPC layer) | No inter-process channel exists |
| Compile | `FaustEngine.cpp:compile()` | libfaust JIT not linked |
| Audio | `FaustEngine.cpp:process()` | Returns early; no DSP object |
| Params → DSP | `ParamPool.cpp:pushToFaust()` | Complete no-op |

Zero audio flows through generated DSP today. The LLM layer (`llm/generate.py`) is the only fully functional component on the critical path, and it is only reachable by running it from the terminal.
