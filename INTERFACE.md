# INTERFACE.md — the host/ <-> llm/ subprocess boundary

`PromptPanel.cpp` shells out to `llm/generate.py`, parses one JSON line
back. No shared header, no schema file — this file IS the schema.

**Located**: env override `PLUGINFORGE_LLM_SCRIPT`, else walk parent dirs
from the binary for `llm/generate.py` (:91-102); not found =>
`generateScript` invalid, submit surfaces "generate.py not found".
**Invoked**: `juce::ChildProcess::start(argv, ...)` — no shell, no
injection — `argv = [pythonExe, scriptPath, "--prompt", text]`, or
`--request-file <tmpfile>` when refining with a prior source (:389-423).
**Read**: `readAllProcessOutput()` merges stdout+stderr (:496); the LAST
line starting with `{` is taken as JSON (:498-506, generate.py's own
"exactly one JSON line" promise, generate.py:488) — `getProperty` with
defaults for `success`/`faust_code`/`error`/`reason`.

## The four failure modes
- **Malformed output**: SPECIFIED for no `{`-line at all — branch (:508-520)
  shows the full raw output as the error. EMERGENT for a `{`-line that
  parses but lacks fields or has wrong types — `getProperty` defaults
  silently stand in; no schema validation exists on the C++ side.
- **Non-zero exit**: never checked — no `getExitCode()` in `runGeneration`.
  SPECIFIED to never happen in subprocess mode (generate.py:489, "always
  exits 0"); if it does anyway (import-time crash, OOM kill), EMERGENT —
  falls through to "no JSON" by accident of the merged pipe.
- **Timeout**: SPECIFIED. `kSubprocessTimeoutMs`=180s (:164, backstop for
  a wedged interpreter — generate.py budgets itself ~117s, reports
  `reason:"timeout"` first). Expiry kills the child, reports "generator
  wedged" (:451-479) — UNLESS `stale()` (:458) says a supersede/teardown
  caused the same wait to end; that must not read as a timeout.
- **Editor destruction mid-generation**: SPECIFIED (PF-006). `stopping` +
  `generation` bump + `activeChild->kill()` (`shutdownWorker`, :170-200)
  make every later `stale()` (:382,458,487,555) a no-op return before
  touching the processor. `PromptPanelThreadingTest` covers this one only.

## Brief D's pattern
`llm/voice_contract.json` (+ `gen_voice_contract.py`) is the first
contract here with one canonical source generating both sides — not
this wire format, but the template a future shared contract (this
schema included) should follow: one file, generated consumers, `--check`.
