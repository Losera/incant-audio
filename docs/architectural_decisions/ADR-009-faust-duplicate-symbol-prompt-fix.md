# ADR-009 — Harden Faust System Prompt Against Duplicate Symbol Errors

| | |
|---|---|
| **Status** | Accepted |
| **Date** | 2026-05-01 |

## Context

The ADR-007 benchmark found 3 Faust failures, all with the same error:
`multiple definitions of symbol 'process'` (or a local like `dtime`). Claude
occasionally emits two `process =` definitions in complex patches — typically
when it writes one as a placeholder and then overwrites it, or when it defines
a sub-expression variable with the same name as a top-level `process`.

Affected prompts: sidechain compressor, brick-wall limiter, ping-pong delay.

## Decision

Add a single rule to `llm/prompts/system_faust.txt`:

> Every Faust program must define `process` exactly once. Never define the same
> variable or function name more than once in a single file. If you need
> intermediate values, use `let` bindings or `with { }` blocks inside the
> existing `process` definition.

## Reasons

- All three benchmark failures share the same root cause; one rule fixes all three.
- The fix is additive (no existing examples break).
- Estimated post-fix compile rate: ≥96% (22/25 + ~2 of the 3 affected prompts;
  the third may need a retry).
- The retry loop (ADR-005) remains as a backstop for any residual failures.

## Consequences

- `llm/prompts/system_faust.txt` gains one constraint rule.
- Re-run the benchmark after applying the fix to confirm ≥96% on Faust.
- No changes to `FaustEngine.cpp`, `generate.py`, or any other layer.
