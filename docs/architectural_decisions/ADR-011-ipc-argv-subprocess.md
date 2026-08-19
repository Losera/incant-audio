# ADR-011 — Editor ↔ LLM-layer IPC: one-shot argv subprocess

**Status:** Accepted — ratified by the human 2026-07-19 and recorded in `../decisions.md`
(the authoritative copy).

## Context

`PluginEditor` needs the Faust code that `llm/generate.py` produces. The mechanism shipped on
2026-07-16 without a decision being recorded first — the since-deleted
`docs/decisions_reconstructed.md` flagged Decision [011] as the last Open item (that file was
removed 2026-08-11 as superseded; `git log` has it). This ADR ratifies (rather than re-litigates) the shipped
mechanism, now that it has survived a month of use and a full build.

## Decision

One-shot subprocess per generation, arguments via argv, result via stdout:

- The editor spawns `python3 <path>/generate.py --prompt "<text>"` with
  `juce::ChildProcess::start(StringArray, ...)` — argv array, **no shell interpretation** of the
  prompt text.
- `generate.py --prompt` (→ `generate_json()`) prints exactly **one JSON line** to stdout:
  `{"success": bool, "faust_code": str, "error": str, "attempts": int}` — the wire contract
  already pinned by `tests/test_generate_unit.py::TestGenerateJson`.
- The editor takes the last stdout line starting with `{` (tolerates stray stderr/traceback
  text), parses with `juce::JSON`, and hands `faust_code` to
  `PluginForgeProcessor::loadFaustCode()`.

## Alternatives considered

- **Persistent stdin/stdout pipe** (long-lived Python worker): saves ~100ms interpreter startup
  per generation, but needs a framing protocol, liveness/restart handling, and version-skew
  management between plugin and worker. Generation latency is dominated by the LLM call
  (seconds), so the saving is noise.
- **Local socket / HTTP daemon:** all of the above plus port management and a security surface —
  unjustified for a same-machine, same-user, one-request pipeline.

## Rationale

Stateless (each generation independent — no worker lifecycle), crash-isolated (a Python
traceback can't take the plugin down; it degrades to an error label), trivially debuggable
(run the same command in a terminal), and the perf cost is invisible behind LLM latency.

## Consequences / hardening status

| Item | Status |
|---|---|
| Prompt injection via shell | Closed by design — argv array, never a shell string |
| Unbounded hang if generate.py stalls | **Closed 2026-07-19** — 120s `waitForProcessToFinish` cap + `kill()` (PluginEditor.cpp) |
| Locating `generate.py` from the installed binary | **Reopened 2026-08-19 — PF-065.** Upward search from the executable (dev layouts, works) + `PLUGINFORGE_LLM_SCRIPT` env override (installed layouts) was marked Closed 2026-07-19 on the strength of the design, never exercised against a real installed bundle. Confirmed broken in REAPER: an installed VST3 at `~/.vst3/…` has no repo above it for the upward search to find, and a DAW launched from a desktop icon does not inherit the env override. See `docs/BUGS.md` PF-065. |
| Interpreter discovery (`python3` must be on PATH) | **Closed 2026-07-19** — `PLUGINFORGE_PYTHON` env override for venv/installed layouts; bare `python3` PATH lookup remains the dev default |
| Per-call interpreter startup (~100ms) | Accepted — invisible behind LLM latency |
| Ready-state UX (button re-enables before JIT finishes) | **Closed 2026-07-19** — `onFaustCompileSuccess` callback fires from the compile thread when the JIT swap lands; status label shows "Ready — DSP live, N params mapped" |

## Revisit trigger

If generation becomes interactive/streaming (token-by-token preview) or multi-turn, the
one-shot model stops fitting — that's the point to reopen this against the persistent-worker
option, as a new ADR.
