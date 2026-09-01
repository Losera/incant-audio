# INTERFACE.md — the host/ <-> llm/ subprocess boundary

*Line citations verified against `host/Source/PromptPanel.cpp` 2026-08-19 — the previous version
of this file had drifted on every citation (see PF-065, `docs/BUGS.md`, discovered while tracing
the REAPER "generate.py not found" defect).*

`PromptPanel.cpp` shells out to `llm/generate.py`, parses one JSON line
back. No shared header, no schema file — this file IS the schema.

**Located**: four steps, in order (`resolveGenerateScript()`, just above
`PromptPanel`'s constructor): (1) env override `PLUGINFORGE_LLM_SCRIPT`;
(2) walk parent dirs from the binary for `llm/generate.py`; (3) **ADR-032 v1**
— the `generate_script_path` in `~/.config/pluginforge/config.json` when set
and the file exists, placed *before* the XDG step so a user-set path beats a
stale install (PF-071); (4) `$XDG_DATA_HOME/pluginforge/llm/generate.py`
(else `~/.local/share/...`), the location `install.sh` writes to. Not found
by any step => `generateScript` invalid, submit surfaces "generate.py not
found". Resolved ONCE in `PromptPanel`'s constructor — reinstalling a
different script mid-session (a test-fixture trap; see `FakeGenerator.h`'s
comments and `EditorSessionTest.cpp` scenarios 16/24/26) has no effect on a
`PromptPanel` that already exists. Step 2 alone **does not reach an installed
VST3** (`~/.vst3/…`); step 3 is the config-file answer to that — see PF-065.
**Interpreter** (`pythonExe`): `resolvePythonExe()`, next to
`resolveGenerateScript()`, resolved once in the constructor — (1) env
`PLUGINFORGE_PYTHON`; (2) **ADR-032 v1 / PF-065** the `python_path` in
`config.json` when it names a file that exists (a launcher-started DAW inherits
no venv on `PATH`); (3) `"python3"`. `install.sh` writes `python_path` to the
runtime's own venv interpreter.

**Invoked**: `juce::ChildProcess::start(argv, ...)` — no shell, no
injection — `argv = [pythonExe, scriptPath, "--prompt", text]`, or
`--request-file <tmpfile>` (:520-576) whenever the request carries
structured fields: `prior_source`, `kind`, `refine_mode`
(`"surgical"` | `"context"` | absent — absent for a "New"/Fresh
generation, which sends no prior source to refine in the first place), or
`provider` / `model`.

**`provider` / `model` (optional, ADR-032 v1).** Two optional string fields
on the `--request-file` payload. Read from `active_provider` /
`active_model` in `~/.config/pluginforge/config.json` once at construction,
snapshotted with each job. The in-plugin picker (`PromptPanel`'s
`providerSelector` + `modelField`, one of `auto`/`gemini`/`groq`/`openrouter`/
`ollama`/`anthropic`) writes those two keys back via `PluginConfig::writeTo()`
on change, load-modify-write so it does not drop `generate_script_path` /
`python_path` / `soundfetch_interpreter_path`. The **"Paths…"** callout
(`PromptPanel`'s `pathsButton`) writes `generate_script_path` + `python_path`
the same way and re-resolves the runtime live, so no user ever hand-edits the
file — that is what closes PF-065's install-layout half alongside `install.sh`.
**Omitted entirely when not configured** ("auto" / blank) — an explicit `""`
would defeat `generate.py`'s `request.get("provider", DEFAULT_PROVIDER)`
fallback (the key would exist), which is exactly the launcher-started-DAW
case they exist to fix. `generate_json()` already consumes both
(`llm/generate.py:507-510`); the Python side is unchanged, and the paid
`anthropic` gate stays there (`PaidProviderError` unless
`PLUGINFORGE_ALLOW_PAID=1`) — the picker does not re-implement it.
Credentials are **not** here — they stay in `.env` / the environment, read by
the Python side (ADR-032 §4).
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
