# INTERFACE.md — the host/ <-> llm/ subprocess boundary

*Line citations verified against `host/Source/PromptPanel.cpp` 2026-08-19 — the previous version
of this file had drifted on every citation (see PF-065, `docs/BUGS.md`, discovered while tracing
the REAPER "generate.py not found" defect).*

`PromptPanel.cpp` shells out to `llm/generate.py`, parses one JSON line
back. No shared header, no schema file — this file IS the schema.

**Located**: env override `PLUGINFORGE_LLM_SCRIPT`, else walk parent dirs
from the binary for `llm/generate.py` (:136-157); not found =>
`generateScript` invalid, submit surfaces "generate.py not found".
Resolved ONCE in `PromptPanel`'s constructor — reinstalling a different
script mid-session (a test-fixture trap; see `FakeGenerator.h`'s comments
and `EditorSessionTest.cpp` scenarios 16/24/26) has no effect on a
`PromptPanel` that already exists. **This resolver does not reach an
installed VST3** (`~/.vst3/…`) — see PF-065.
**Invoked**: `juce::ChildProcess::start(argv, ...)` — no shell, no
injection — `argv = [pythonExe, scriptPath, "--prompt", text]`, or
`--request-file <tmpfile>` (:520-576) whenever the request carries
structured fields: `prior_source`, `kind`, or `refine_mode`
(`"surgical"` | `"context"` | absent — absent for a "New"/Fresh
generation, which sends no prior source to refine in the first place).
**Read**: `readAllProcessOutput()` merges stdout+stderr (:646); the LAST
line starting with `{` is taken as JSON (:650-658, generate.py's own
"exactly one JSON line" promise, generate.py:590) — `getProperty` with
defaults for `success`/`faust_code`/`error`/`reason`, plus the additive
`prior_source_dropped`/`prior_source_refused` flags read at
`PromptPanel.cpp` :709,720.

## The five failure modes
- **Malformed output**: SPECIFIED for no `{`-line at all — branch (:658-665)
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
  wedged" (:599-622) — UNLESS `stale()` (:606) says a supersede/teardown
  caused the same wait to end; that must not read as a timeout.
- **Editor destruction mid-generation**: SPECIFIED (PF-006). `stopping` +
  `generation` bump + `activeChild->kill()` (`shutdownWorker`, :244-263)
  make every later `stale()` (:513,606,637,765) a no-op return before
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
