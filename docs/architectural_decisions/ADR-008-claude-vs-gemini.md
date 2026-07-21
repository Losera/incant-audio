# ADR-008 — Claude vs Gemini LLM Provider

| | |
|---|---|
| **Status** | Accepted — Claude retained (benchmark pending full Gemini run) |
| **Date** | 2026-05-01 |
| **Supersedes** | ADR-008 stub in docs/decisions.md |

## Context

The benchmark harness supports both `claude-opus-4-6` and `gemini-2.0-flash`.
The 2026-05-01 benchmark run was Claude-only (`--provider claude`). The Gemini
run is pending (requires `GOOGLE_API_KEY`).

## Current data

Claude (Faust): **88%** | Claude (Cmajor): **60%**

Gemini results: not yet collected.

## Decision (provisional)

Retain Claude as the production provider until the Gemini run completes.
If Gemini is within 5 percentage points on Faust, prefer Claude (already
integrated, retry loop tested, no additional API key management).

## Next action

Run `python bench/run_benchmark.py --provider gemini` and update this file with
the comparison table. If Gemini exceeds Claude by > 5pp on Faust compile rate,
open a migration ADR.
