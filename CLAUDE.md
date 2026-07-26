# PluginForge — Claude Code Context

## What this project is
LLM-guided program synthesis for real-time DSP audio plugins.
Pipeline: Natural language prompt → LLM → Faust DSL → LLVM JIT → VST3/AU

## Key architectural decisions
- LLM outputs Faust DSL (not raw C++ or JSON IR)
- Faust chosen over JSON IR: algebraic DSL, LLMs generate it more reliably
- JIT via libfaust/LLVM inside JUCE host plugin (single distributable artifact)
- Pre-allocated 64-slot parameter pool to satisfy DAW fixed-param requirements
- Error correction loop: compiler stderr fed back to LLM, up to 3 retries

## Stack
- JUCE 7 (C++17) — audio plugin framework, VST3/AU output
- libfaust + LLVM — JIT compiler embedded in host plugin
- Python — LLM prompt layer; provider-agnostic via llm/providers.py (free-only by
  default: gemini / groq / openrouter / local ollama; anthropic is paid and gated
  behind PLUGINFORGE_ALLOW_PAID=1). Draft rationale: docs/ADR-012-free-provider-layer-DRAFT.md
- CMake + Ninja — build system
- Arch Linux (primary dev target)

## Where it currently stands
**`STATUS.md`. Read it first, every session.** It is rewritten each session and is the
only status record.

The per-file narrative that used to live here was deleted on 2026-07-25, completing a
migration this file had already announced. It had become actively wrong — it still said
state persistence was an empty stub (implemented in `c34bbb6`), that the P6 audible test
had never run (it ran 2026-07-24), and that the suite held 231 tests (312). A document
that lies is worse than a missing one, because a reader cannot tell which half to trust.
`git log` has every word of it.

## The development cycle
One command, cost-ordered, cumulative. Run the cheapest level that covers what you touched.

```
tools/check.sh fast     ~2 s     every commit    unit tests + control wiring
tools/check.sh full     ~2 min   every push      + prompt grounding, build, TSan
tools/check.sh audio    ~1 min   touched llm/    + render oracle over the corpus  ($0)
tools/check.sh quota    opt-in   deliberate      the 25-prompt benchmark (spends quota)
tools/check.sh assumed           anytime         how many claims are still unverified
```

**The one number is `assumed`.** Every piece of work should move at least one claim out of
STATUS.md's "Assumed, never checked" list. It cannot be improved by writing documentation,
which is the entire reason it is the metric.

**A control counts only once it has been seen failing.** On 2026-07-25 all five enforcement
hooks were found never to have run — `.claude/settings.json` had `PreToolUse` at the file
root instead of nested under `"hooks"`, and Claude Code ignores a wrongly-shaped file
silently. This was the third time the project mistook a declared control for a running one,
after PAIR mode and the ADR-009 sync hook. So: new gates ship with a red case,
`tests/test_control_wiring.py` asserts every hook still has teeth, and "it's configured" is
not evidence.

## Invariants worth knowing before you touch the audio path
- **DSP swap protocol.** `FaustEngine` compiles off-thread; an `audioBusy` drain guard
  (`enterAudio()`/`exitAudio()` bracketing `processBlock`, seq_cst store→load) closes the
  `activeUI` TOCTOU, and the compile callback fires **before** `ready=true` so ParamPool
  labels and the new DSP publish together. That ordering fixed ~1,100 "setParamValue not
  found" errors the first TSan run exposed. Rationale: `docs/fixplan_pushtofaust_swap.md`.
- **Parameters are declared once, in `PluginProcessor::createParameterLayout()`**, and
  ParamPool looks them up by the shared `ParamPool::slotId()` scheme. ParamPool must never
  create them — doing so trips `jassert(!state.isValid())` on every slot.
- **`pushToFaust()` is on the audio thread.** No allocation, no locks, no logging, no map
  lookups. `check_rt_safety.py` guards `FaustEngine.cpp` and `PluginProcessor.cpp` only, and
  cannot follow a call graph — it does not cover this function (PF-015).
- **One system prompt**, `llm/prompts/system_prompt.txt`, shared by the product and both
  bench harnesses. Its stdlib block is generated from the installed `/usr/share/faust/*.lib`
  by `tools/gen_stdlib_block.py`; a hook rejects any `ns.func` that does not resolve.
  **Benchmark numbers recorded before 2026-07-21 measured a since-deleted prompt file and
  are not comparable to anything.**
- **`tests/conftest.py` pins `PLUGINFORGE_PROVIDER=anthropic`.** Without it a developer's
  `.env` makes the mocked-client unit tests dispatch real network calls, which once hung the
  suite and burned free-tier quota.

## Do not
- Do not suggest switching from Faust to raw C++ generation
- Do not suggest JSON IR as an intermediary (decision was made against this)
- Do not use sudo npm install
- Do not stage or commit across the whole tree — the human edits this repo at the same
  time, sharing one index. Use explicit paths. (Enforced by `check_bash_denylist.py`.)

## File map
```
host/Source/FaustEngine.*     libfaust JIT wrapper, compile worker, swap protocol
host/Source/PluginProcessor.* lifecycle, state blob, load path (Fresh/Iterate modes)
host/Source/ParamPool.*       64-slot parameter pre-allocation
host/Source/ParamMap.h        0–1 ↔ real units (Hz/dB/ms), log/exp/linear curves
host/Source/OutputGuard.*     NaN/DC/limit/runaway safety net before the speakers
host/Source/*Panel.*          editor shell: prompt, code editor (stub), param grid
llm/generate.py               LLM call + Faust validation + retry loop
llm/providers.py              five providers, three adapters, free-only rule
llm/prompts/system_prompt.txt the one prompt (stdlib block is generated)
bench/render_oracle.py        renders compiled Faust offline and measures it
tools/check.sh                the gate ladder — start here
examples/*.dsp                reference Faust patches
```

## Collaboration protocol
Before any non-trivial task, also load COLLABORATION.md (revision 2, 2026-07-21). Claude
writes the code, including the audio path and the prompts; what is gated is consequence,
not file category. It defines the four consult-first triggers, the two-tier evidence bar
(Tier 2 requires a primary source cited by file:line), the change-report format, and
STATUS.md. The three-mode protocol (DELEGATE / PAIR / HUMAN-OWNED) is retired — see
COLLABORATION.md §9.

CLAUDE.md answers what this project is; COLLABORATION.md answers how we build it;
STATUS.md answers where it currently stands.
