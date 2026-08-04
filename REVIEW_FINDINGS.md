# REVIEW_FINDINGS.md — Brief 0: Reality check

Read-only evidence pass. No code changes. Citations are `path:line` or
`path` for whole-file/dir evidence.

## 1. Top-level subsystems and real dependency edges

- **`host/`** — JUCE 7 C++17 plugin (`host/Source/*.cpp,*.h`, built via
  `host/CMakeLists.txt`). Depends on `faust/dsp/llvm-dsp.h` (libfaust/LLVM,
  `host/Source/FaustEngine.h:3`) and JUCE (`add_subdirectory(${JUCE_PATH} JUCE)`,
  `host/CMakeLists.txt:9`).
- **`llm/`** — Python prompt/provider layer. `llm/generate.py:27-28` imports
  `providers` and `router` (same dir, not a package import). `llm/providers.py:48`
  imports `httpx` (real network). `llm/router.py` has zero non-stdlib imports
  (`import re`, `llm/router.py:28`) — it is pure keyword scoring, not an LLM call
  (stated explicitly at `llm/router.py:11-15`).
- **`bench/`** — Python benchmark/regression harness, imports from both `llm/`
  and its own dir (`bench/run_benchmark.py:134-135` reads
  `llm/prompts/system_prompt.txt` and `instrument_prompt.txt` directly by path).
- **`tools/`** — standalone Python scripts (`gen_stdlib_block.py`, `check.sh`,
  etc.), invoked by hooks/tests, not imported by `llm/` or `host/`.
- **`tests/`** (Python, pytest) and **`host/tests/`** (C++ harnesses, built by
  `host/CMakeLists.txt`) — test both `llm/` and `host/` respectively; no
  production code imports from either test tree.
- **`.claude/hooks/`** — PreToolUse hooks, registered in `.claude/settings.json`,
  invoked by the Claude Code harness, not by any subsystem above.

**The one real cross-language edge**: `host/Source/PromptPanel.cpp:389` spawns
`python3 llm/generate.py` as a `juce::ChildProcess` from the JUCE UI/message
thread (script located by walking up from the binary,
`host/Source/PromptPanel.cpp:102`), reads one JSON line off stdout
(`host/Source/PromptPanel.cpp:496-498`), and on success calls into
`PluginForgeProcessor::loadFaustCode()` → `FaustEngine::compile()`. This is the
**only** path from `host/` into `llm/` — it is a subprocess boundary, not a
compiled or linked dependency, and it runs off the audio thread (see §2).

## 2. Does the audio callback path reach LLM/provider, network, or filesystem code?

**No.** `PluginForgeProcessor::processBlock()` (`host/Source/PluginProcessor.cpp:146`)
calls only: `FaustEngine::enterAudio/exitAudio` (atomics, header-inline),
`ParamPool::pushToFaust` (`host/Source/PluginProcessor.cpp:266`),
`FaustEngine::process` → `dsp->compute()`, and `OutputGuard::process`
(`host/Source/PluginProcessor.cpp:271`). Read `ParamPool::pushToFaust`
(`host/Source/ParamPool.cpp`) and `OutputGuard::process`
(`host/Source/OutputGuard.cpp:26`) directly: no file I/O, no sockets, no
subprocess calls, no calls into `llm/` in either.

This exact scope is also what `.claude/hooks/check_rt_safety.py` mechanically
enforces (see §7) — its own docstring names the identical four-function closure
(`.claude/hooks/check_rt_safety.py:6-13`).

The subprocess call into `llm/generate.py` happens from `PromptPanel`, on the
message thread, gated by user action (Generate button) —
`host/Source/PromptPanel.cpp:355-389`. It never touches `processBlock`.

## 3. Where plugin structure lives; is any of it duplicated?

- **Parameter definitions (host side):** 64 generic float params, "Macro 1".."Macro
  64", range `[0,1]`, created once in
  `PluginForgeProcessor::createParameterLayout()` (`host/Source/PluginProcessor.cpp:36-67`).
  These are DAW-visible identity; they carry no per-patch meaning.
- **Parameter identity/labels/units/curves (per patch):** discovered at compile
  time from the Faust source itself via `MapUI`/`ParamCapture`, captured as
  `FaustEngine::ParamInfo` (`host/Source/FaustEngine.h:66-101`: label, min, max,
  step, kind, scale, unit, group path). Mapped onto the 64 slots by
  `ParamPool::remap` (`host/Source/ParamPool.cpp`), keyed by a semantic identity
  computed in `host/Source/ParamIdentity.h`.
- **UI layout (grid arithmetic):** `host/Source/ParamGridLayout.h:20-38`
  (`columnsFor`/`rowsFor`), consumed only by `ParamGridPanel`
  (`host/Source/ParamGridLayout.h:15-16` states single-caller intent).
- **MIDI/voice mapping:** a **string-label naming contract**, not data —
  `extractVoiceControls` in `host/Source/FaustEngine.cpp:239-260` matches Faust
  parameter labels `"gate"`, `"freq"`/`"key"`, `"gain"`/`"vel"`/`"velocity"`
  case-sensitively to bind the three voice zones.

**Duplication found:** the voice-mapping label contract is declared on both
sides independently and must be kept in sync by hand — the C++ match list
(`host/Source/FaustEngine.cpp:252-257`) and the LLM-facing rule
(`llm/prompts/instrument_prompt.txt:9-14`, "The names are matched EXACTLY and
case-sensitively"). There is no shared source of truth generating either side
from the other; a rename on one side silently breaks instruments without a
compile error, since Faust does not know the names are special.

No other duplication found: parameter identity, ranges, and UI layout are each
computed in exactly one place.

## 4. What is the regression corpus made of?

Two distinct corpora exist under `bench/prompts/`, both plain-string prompts,
no per-entry metadata:
- `bench/prompts/prompts.json` — dict of 5 categories (`trivial`, `filters`,
  `time-based`, `dynamics`, `generative`) × 5 strings each = **25 entries total**
  (verified by loading the file). This is the "25-prompt benchmark" referenced
  throughout `docs/` and `bench/check_prompt_regression.py:34-35`.
- `bench/prompts/recovery_prompts.json` — same shape, subset (`filters`,
  `generative`, `time-based`), used for the cheap smoke check
  (`bench/check_prompt_regression.py:22-23`).
- `bench/prompts/regression_check.json`, `bench/prompts/p6_battery.json`,
  `bench/prompts/tiered_prompts.json` — related but structurally different
  (`p6_battery.json` has `_note`/`_source`/`prompts` keys;
  `tiered_prompts.json` has `description`/`effects`/`tiers`/`version`).

Separately, **`bench/ladder_corpus.json`** is a flat JSON array, 19 recorded
generation *results* (not prompts to run) — each entry has keys `provider`,
`model`, `category`, `prompt`, `dsl`, `code`, `first_try_compiles`, `error`,
`timestamp`. This is a captured-outcomes log, not an input corpus, and is a
different artifact from the two above; conflating them would be a mistake this
brief flags rather than making.

## 5. Are prompt texts referenced by identifier, or copied inline?

Referenced by file path everywhere checked — **no inline copy found**.
`llm/generate.py:47,61`, `llm/export_prompt.py:33`, `bench/run_benchmark.py:134-135`,
`bench/check_prompt_regression.py:57`, `tools/gen_stdlib_block.py:55`,
`tools/measure_prompt_tokens.py:25-28`, and every prompt-related test
(`tests/test_prompt_headroom.py:92`, `tests/test_prompt_stdlib.py:36`,
`tests/test_prompt_claims.py:49`, `tests/test_providers_unit.py:525,537`,
`tests/test_control_wiring.py:227`, `.claude/hooks/check_prompt_invariants.py`,
`tests/test_project_structure.py:21,39-55`) all do
`(...).read_text()` on `llm/prompts/system_prompt.txt` or
`llm/prompts/instrument_prompt.txt` by path, never a copied string literal.
Grepped `host/Source/*.cpp,*.h` for prompt-shaped literals (`"You are a"`,
example Faust snippets) — none found; the host never carries prompt text.

## 6. Generated vs. hand-written docs; which are stale against their subject code?

**Generated:** the STDLIB REFERENCE block inside `llm/prompts/system_prompt.txt`
is generated from `/usr/share/faust/*.lib` by `tools/gen_stdlib_block.py --write`
(`tools/gen_stdlib_block.py:35,566-569`). This is a splice into a prompt file,
not a `docs/` file — found no generated file under `docs/`.

**Hand-written and confirmed stale** (last-touch commit of the doc vs. last-touch
commit of the code it describes, via `git log -1 -- <path>`):

| Doc | Doc last touched | Subject code | Code last touched |
|---|---|---|---|
| `docs/architecture.md` | `23d16dc` 2026-07-21 (initial commit) | `host/Source/FaustEngine.cpp`, `ParamPool.cpp`, `PluginProcessor.cpp` | `e7d7c20`/`496c35e` 2026-08-03 |
| `docs/fixplan_pushtofaust_swap.md` | `23d16dc` 2026-07-21 | `FaustEngine.cpp` (swap protocol it documents) | 2026-08-03 |
| `docs/audio_thread_example.md` | `23d16dc` 2026-07-21 | `PluginProcessor.cpp` (the RT-safety rules `check_rt_safety.py` cites this file for) | 2026-08-03 |
| `docs/goals_and_next_steps.md` | `23d16dc` 2026-07-21 | project-wide | n/a |

By contrast `docs/decisions.md` (2026-08-04), `docs/ui_design_plan.md`
(2026-07-30), and `docs/BUGS.md` (2026-08-03) have all moved recently and track
current code. Not checked exhaustively against "last 20 commits touching
subject code" per doc — the table above is the sample that answers the
question with direct evidence; a full per-doc audit was out of this brief's
budget.

## 7. Automated enforcement vs. prose-only

| Invariant | Enforcement | Evidence |
|---|---|---|
| No allocation/lock on audio thread | **Hook**, PreToolUse on Write/Edit/MultiEdit | `.claude/hooks/check_rt_safety.py`, registered `.claude/settings.json:6-30`, given teeth (asserted to actually block, not just exist) by `tests/test_control_wiring.py:244,295,330` |
| libfaust lifecycle / swap-protocol ordering | **Automated test**, not a hook — TSan harness built and run by `tools/check.sh` (line 176-185) and by CI's harness list (`tools/check.sh:114`) | `host/tests/ParamPoolConcurrencyTest.cpp:1-20` (drives the real `compile()`→`remap()`/`processBlock()`→`pushToFaust()` object graph under ThreadSanitizer) |
| Parameter denormalization in the macro pool | **Automated test** — covered by `ParamMapTest`, `EditorSessionTest` (`host/tests/EditorSessionTest.cpp:948-1023`, asserts the *display* is denormalized while the underlying parameter is not), and `OfflineRenderTest` (`host/tests/OfflineRenderTest.cpp:21,737`) | not a hook; a compiled test target, run via `check.sh` |
| Teardown ordering for the compile thread | **Partial/approximated automated coverage, not exact** | `host/tests/EditorSessionTest.cpp:43` states outright: "DAW-driven teardown ordering. Approximated by destroying on the message [thread]" — i.e. the real DAW teardown sequence is not reproduced, only approximated. `host/tests/ParamPoolConcurrencyTest.cpp:26-33` separately flags that the compile thread is detached and nothing in production code lets a harness wait for it to finish — worked around with a fixed sleep, documented as a known harness limitation, not a proof. `host/tests/EditorSessionTest.cpp:675` ("destroying the editor mid-generation did not crash") is a smoke check, not an ordering proof. |

`tests/test_control_wiring.py` itself exists because, on 2026-07-25, all
declared hooks were found never to have executed due to a settings.json shape
error (`tests/test_control_wiring.py:5-9`) — the file's stated purpose is to
distinguish a hook that is merely *declared* from one that is proven to *block*
a real case. That distinction is why "hook" above is reported only where a
`test_..._blocks_...` case in `tests/test_control_wiring.py` was found, not just
a hook script's existence.

## Not found

- No `docs/` file found matching "auto-generated" that is actually a generated
  artifact (the two greps that matched — `docs/competitive_landscape.md`,
  `docs/BUGS.md`, `docs/sessions/002-refine-loop-and-ui-redesign.md` — use the
  phrase in prose, not as a file header marking generated content).
- No `check_rt_safety.py`-equivalent hook or test found for "teardown ordering
  for the compile thread" as its own named invariant — the closest evidence is
  the partial coverage listed in §7.
