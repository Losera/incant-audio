# INTERFACE.md — the host/ <-> llm/ subprocess boundary

`PromptPanel.cpp` shells out to `llm/generate.py`, parses one JSON line
back. No shared header, no schema file — this file IS the schema.

**Located**: env override `PLUGINFORGE_LLM_SCRIPT`, else walk parent dirs
from the binary for `llm/generate.py` (:91-102); not found =>
`generateScript` invalid, submit surfaces "generate.py not found".
Resolved ONCE in `PromptPanel`'s constructor — reinstalling a different
script mid-session (a test-fixture trap; see `FakeGenerator.h`'s comments
and `EditorSessionTest.cpp` scenarios 16/24/26) has no effect on a
`PromptPanel` that already exists.
**Invoked**: `juce::ChildProcess::start(argv, ...)` — no shell, no
injection — `argv = [pythonExe, scriptPath, "--prompt", text]`, or
`--request-file <tmpfile>` (:420-459) whenever the request carries
structured fields: `prior_source`, `kind`, or `refine_mode`
(`"surgical"` | `"context"` | absent — absent for a "New"/Fresh
generation, which sends no prior source to refine in the first place).
**Read**: `readAllProcessOutput()` merges stdout+stderr (:545); the LAST
line starting with `{` is taken as JSON (:548-556, generate.py's own
"exactly one JSON line" promise, generate.py:590) — `getProperty` with
defaults for `success`/`faust_code`/`error`/`reason`, plus the additive
`prior_source_dropped`/`prior_source_refused` flags read at
`PromptPanel.cpp` around :599-612.

## The five failure modes
- **Malformed output**: SPECIFIED for no `{`-line at all — branch (:561-566)
  shows the full raw output as the error. EMERGENT for a `{`-line that
  parses but lacks fields or has wrong types — `getProperty` defaults
  silently stand in; no schema validation exists on the C++ side.
- **Non-zero exit**: never checked — no `getExitCode()` in `runGeneration`.
  SPECIFIED to never happen in subprocess mode (generate.py:590-591,
  "always exits 0"); if it does anyway (import-time crash, OOM kill),
  EMERGENT — falls through to "no JSON" by accident of the merged pipe.
- **Timeout**: SPECIFIED. `kSubprocessTimeoutMs`=180s (backstop for
  a wedged interpreter — generate.py budgets itself ~117s, reports
  `reason:"timeout"` first). Expiry kills the child, reports "generator
  wedged" (~:504-522) — UNLESS `stale()` (:505) says a supersede/teardown
  caused the same wait to end; that must not read as a timeout.
- **Editor destruction mid-generation**: SPECIFIED (PF-006). `stopping` +
  `generation` bump + `activeChild->kill()` (`shutdownWorker`, :191-218)
  make every later `stale()` (:413,505,536,658) a no-op return before
  touching the processor. `PromptPanelThreadingTest` covers this one only.
- **Surgical (Add) refusal**: SPECIFIED. `refine_mode:"surgical"` whose
  prior source fails generate.py's token-budget preflight returns
  `success:false, reason:"error", attempts:0, prior_source_refused:true`
  instead of silently degrading — unlike `"context"`/absent, which soft-drop
  the prior source and continue (`prior_source_dropped:true`). Both are
  `success:true` responses; the refusal is not, and so takes the SAME
  `! success` branch as any other generation failure, never
  `onFaustCompileSuccess` — a refusal that reached that callback would mean
  a patch went live despite the LLM never having been asked to produce one.
  `EditorSessionTest` scenario 26 is the direct assertion of that routing.

## Brief D's pattern
`llm/voice_contract.json` (+ `gen_voice_contract.py`) is the first
contract here with one canonical source generating both sides — not
this wire format, but the template a future shared contract (this
schema included) should follow: one file, generated consumers, `--check`.
