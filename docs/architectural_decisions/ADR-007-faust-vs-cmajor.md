# ADR-007 — Faust vs Cmajor DSL Selection

| | |
|---|---|
| **Status** | Accepted — Faust retained |
| **Date** | 2026-05-01 |
| **Supersedes** | ADR-007 stub in docs/decisions.md |

## Context

After Day 1 scaffolding we deferred the DSL choice pending a quantitative benchmark
(ADR-006). Cmajor was identified as a credible alternative because it ships its own
LLVM JIT and VST3/AU export, potentially eliminating the need to wire `libfaust`
in Day 2. The benchmark ran `claude-opus-4-6` against 25 prompts × 5 categories
× 2 DSLs = 50 total generations, each validated by the respective compiler.

## Benchmark results (Claude provider)

| Category | Faust | Cmajor |
|----------|-------|--------|
| trivial | 5/5 (100%) | 4/5 (80%) |
| filters | 5/5 (100%) | 2/5 (40%) |
| time-based | 4/5 (80%) | 2/5 (40%) |
| dynamics | 3/5 (60%) | 5/5 (100%) |
| generative | 5/5 (100%) | 2/5 (40%) |
| **TOTAL** | **22/25 (88%)** | **15/25 (60%)** |

## Failure analysis

**Faust — 3 failures, single error class**

All three failures are `multiple definitions of symbol 'process'` (or a local
variable like `dtime`). Claude occasionally emits two `process =` lines in complex
patches (sidechain compressor, brick-wall limiter, ping-pong delay). This is one
fixable mistake class — a single prompt rule eliminates it (see ADR-009).

**Cmajor — 10 failures, five distinct error classes**

1. Output endpoint name hallucination: Claude invents names like `highpassOut`,
   `bandOut` that do not exist in the generated processor — the language requires
   exact endpoint name matching inside graph connections.
2. `float64`→`float32` implicit conversion: Cmajor is strict; Claude defaults to
   double-precision literals and `processor.period` returns `float64`.
3. Reserved keyword collision: Claude uses `input` as a local variable name;
   `input` is a keyword in Cmajor.
4. Unsigned literal suffix: `1664525u` — Cmajor does not accept C-style `u` suffix.
5. Expression-to-endpoint connection: `60.0f -> filter.frequency` is invalid
   syntax for feeding a constant to a value endpoint.

These are independent failure modes requiring independent prompt fixes. They also
reflect that Cmajor has significantly less training-data coverage than Faust.

## Decision

Retain Faust as the sole LLM output DSL. Do not pursue Cmajor integration.

## Reasons

1. **28-point compile-rate gap** (88% vs 60%) is decisive for production latency
   and API cost. At 60%, ~40% of user requests hit at least one retry; at 88%
   only ~12% do — and those 12% are already handled by the retry loop (ADR-005).

2. **Faust failures are a single fixable class.** One additional system prompt
   rule (`never define the same symbol more than once`) is estimated to close the
   gap to ≥96%. Cmajor's five error classes would require five separate fixes with
   uncertain coverage.

3. **Cmajor's JIT embed is less documented.** The `cmaj` SDK does not expose a
   stable C++ embedding API equivalent to `libfaust`. The Day-2 libfaust wiring
   path is already stubbed and documented.

4. **No net binary-size benefit.** Cmajor still requires LLVM; the size trade-off
   is neutral.

5. **Architectural consistency.** ADR-001, CLAUDE.md, and all existing prompts are
   already Faust-oriented. Switching would invalidate the system prompt and all
   example `.dsp` patches.

## Consequences

- Day-2 task remains: wire `libfaust` JIT into `FaustEngine.cpp`.
- Apply ADR-009 prompt hardening to prevent duplicate-symbol failures.
- Cmajor tooling (`cmaj`) can remain installed as a comparison reference but is
  not part of the production pipeline.
- If a future benchmark with a more Cmajor-trained model closes the gap to < 5pp,
  re-open this decision.
