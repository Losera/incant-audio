---
paths:
  - "host/Source/**"
  - "host/tests/**"
  - "llm/prompts/**"
  - "bench/prompts/**"
---

# You are in Tier 2 territory

These paths carry COLLABORATION.md §3's consequential-change bar. Not a permission gate —
§1 removed those — an **evidence** bar. All three are required to land:

1. **A primary source, cited by `file:line`.** Read the header; do not recall the
   library. Not "JUCE does X" but `juce_ChildProcess.h:88`. Not "Faust clamps this" but
   `/usr/include/faust/gui/MapUI.h:150` — which is where reading the actual header
   revealed it does *not* clamp, the defect that had silently disabled the controls on
   essentially every generated plugin while 234 tests passed.
2. **A test or a runnable check**, added or extended. If the property genuinely cannot be
   tested — a shutdown race, an audible result — say so and name what a human would have
   to do instead.
3. **An explicit statement of what was NOT verified.** Every Tier 2 report ends with the
   unverified remainder. An empty remainder is itself a claim, and had better be true.

**"Verified" is a banned word without a named artifact.** "Looks correct", "should be
fine", "this is the standard pattern" do not land Tier 2 changes.

## Audio-path invariants (`host/Source/`)

- **`processBlock`, `FaustEngine::process`, `ParamPool::pushToFaust`, and
  `OutputGuard::process` run on the audio thread.** No allocation, no locks, no I/O, no
  logging, no map lookups. `check_rt_safety.py` blocks these four by name and **cannot
  follow a call graph** — a fifth function arriving on the audio thread is invisible to
  it and is yours to catch.
- **The DSP swap protocol is load-bearing.** The `audioBusy` drain guard
  (`enterAudio()`/`exitAudio()` bracketing `processBlock`, seq_cst store→load) closes the
  `activeUI` TOCTOU, and the compile callback fires **before** `ready=true` so ParamPool
  labels and the new DSP publish together. That ordering fixed ~1,100 "setParamValue not
  found" errors the first TSan run exposed. Rationale: `docs/fixplan_pushtofaust_swap.md`.
- **Parameters are declared once**, in `PluginProcessor::createParameterLayout()`.
  ParamPool looks them up by `ParamPool::slotId()` and must never create them — doing so
  trips `jassert(!state.isValid())` on every slot.
- Touching atomics or memory ordering? The citation requirement means the ordering
  argument, not just the header.

## Prompts (`llm/prompts/`, `bench/prompts/`)

- **Two system prompts**, `llm/prompts/system_prompt.txt` (effects) and
  `llm/prompts/instrument_prompt.txt` (instruments, added `d587665`), selected by
  `llm/router.py`'s keyword scoring — never an LLM call. Each has its stdlib block
  generated from the installed `/usr/share/faust/*.lib` by `tools/gen_stdlib_block.py`
  (per-profile since `d587665`); `check_prompt_invariants.py` rejects any `ns.func`
  in either file that does not resolve, and covers the whole `llm/prompts/` directory,
  not one filename (`09786ec`).
- **A prompt edit owes a benchmark statement.** Either re-run the affected benchmark or
  state that the baseline is now stale. Changing a prompt and leaving a stale baseline in
  place is a Tier 2 violation. Numbers recorded before 2026-07-21 measured a since-deleted
  prompt file and are comparable to nothing.

## Why this is a rule and not a hook

A hook is deterministic and beats a rule wherever the invariant is mechanically
checkable — see `.claude/hooks/` and the `invariant-hook-writer` agent. This file covers
what the hooks provably cannot: reachability past the four named functions, and whether
the evidence behind a change actually exists. COLLABORATION.md §9's finding is the reason
it exists at all — *"Verification behavior caught it; authorship ceremony did not."*
