# START HERE

*Your 5-minute re-orientation for PluginForge.*

---

## Where the project stands

You have a working LLM layer and a plugin skeleton, but no audio flows yet. The Python side — `llm/generate.py` — is fully functional. Give it a natural-language prompt from the terminal and it will call Claude, get Faust DSL back, validate it with the compiler, and print the result. That part works.

The C++ side is a different story. `FaustEngine.cpp` is a stub: when `compile()` is called, it logs the code and fires the callback with an empty parameter list. `process()` returns immediately without touching audio. `ParamPool::pushToFaust()` is a no-op. The plugin editor's Generate button updates a status label but never calls the LLM or `loadFaustCode()`. In short, the two halves of the pipeline — Python and C++ — are not connected to each other, and neither the JIT compilation nor the audio processing are implemented yet. The plugin compiles and loads in a DAW, but it is silent.

The test suite is in reasonable shape. Unit tests for the Python layer pass without needing an API key or a compiler installed. The project structure tests pass. Two integration tests (`test_llm_output.py`) run against the live API and are not yet guarded by a marker, which will be a problem for CI.

A benchmark was run on 2026-05-01 comparing Faust and Cmajor as target DSLs, using 25 natural-language prompts across 5 difficulty categories. Faust scored 23/25 (92%), Cmajor 16/25 (64%) — a 28-point gap. ADR-007 formally closed in favor of Faust on the same date.

## The immediate decision

Despite the ADR closing, there is an outstanding question about whether Cmajor's failures are fixable by prompt engineering alone. The benchmark analysis (`docs/benchmark_analysis.md`) shows that all 9 Cmajor failures trace to five specific, named causes — three types of type-system errors, two syntax errors, and three hallucinations of SVF filter endpoint names. Each is expressible as a prompt rule. The question is whether adding those rules would close the gap to within 10 percentage points, or whether the failures reflect a deeper knowledge gap in the model's Cmajor training data.

Before committing to the Day-2 libfaust wiring work, you should decide whether to run a focused Cmajor recovery test — rerunning only the 9 failed prompts against a hardened system prompt. If 7 or more recover, Cmajor deserves reconsideration. If not, Faust is confirmed and you move on.

This is a judgment call that only you can make. It costs roughly 30 minutes of work (prompt writing + one benchmark run). The upside is certainty; the downside of skipping it is potentially committing to libfaust JIT wiring that you'd have to undo.

## The very next concrete action

Read `docs/next_steps.md`. Part 1 lays out the recovery test step by step. Part 2 gives you both branches: what to do if Cmajor recovers, and what to do if it doesn't (which is essentially the existing Day-2 plan from `docs/goals_and_next_steps.md`).

If you've already decided you don't want to revisit Cmajor, skip to Part 2 Path B, start with B2 (wire libfaust JIT), and work down the list.

## Where to find everything

| Document | What it answers |
|----------|----------------|
| `STATUS.md` | What works, what is broken, and what is believed without evidence. Read this first — it is rewritten each session. (Replaces `docs/codebase_inventory.md`, deleted 2026-07-25: generated 2026-05-04, it still described FaustEngine as a stub and `pushToFaust()` as "a complete no-op", and said the plugin "produces silence". All false since May. `git log` has it if the 2026-05 snapshot is ever wanted.) |
| `docs/benchmark_analysis.md` | Full breakdown of the 50-generation benchmark: per-category scores, every failure classified by type, and a direct recommendation on prompt-engineering fixability |
| `docs/decisions_reconstructed.md` | All architectural decisions in ADR format, including two that are implied by the code but not yet formally documented (IPC mechanism, single-artifact deployment) |
| `docs/next_steps.md` | The recovery test task list and both post-decision paths, with each task marked [USER], [CLAUDE], or [PAIR] |
| `docs/decisions.md` | The primary ADR log (ADR-001 through ADR-008) — the authoritative record, do not overwrite. ADR-009 lives as a standalone file under `docs/architectural_decisions/` instead. |
| `docs/goals_and_next_steps.md` | The original Day-2 through Week-4 task breakdown (still accurate for the Faust path) |
