# PluginForge — Architecture Decision Log

Each entry follows the ADR format: **Status → Context → Decision → Consequences**.
Update status to `Superseded` (with a reference) rather than deleting old entries.

---

## ADR-001 — Use Faust DSL as the LLM output target

| | |
|---|---|
| **Status** | Accepted |
| **Date** | 2026-04-28 |

**Context**  
The LLM must output something that can be deterministically compiled into runnable DSP code inside a JUCE plugin. The candidates evaluated were: raw C++, JSON IR (a custom intermediate representation), and Faust DSL.

**Decision**  
Use Faust DSL as the sole LLM output format.

**Reasons**
- Faust is an algebraic signal-processing DSL with a small, well-defined grammar — LLMs generate it more reliably than raw C++.
- The Faust compiler (`libfaust`) can be embedded as a library inside the JUCE plugin, enabling LLVM JIT without a separate install.
- Faust's standard library (`stdfaust.lib`) covers the vast majority of common DSP primitives (filters, delays, oscillators, dynamics), so the LLM rarely needs to invent implementation details.
- Compiler errors are structured and short — feeding them back to the LLM in a retry loop is practical (ADR-005).

**Consequences**
- The plugin binary must link `libfaust` + LLVM (~30 MB added to binary size).
- DSP algorithms that require Faust-unsupported constructs (e.g., multi-rate processing, direct pointer manipulation) cannot be generated.
- LLM prompt engineering must focus on Faust idioms and stdlib functions.

---

## ADR-002 — Reject JSON IR as an intermediary format

| | |
|---|---|
| **Status** | Accepted |
| **Date** | 2026-04-28 |

**Context**  
A custom JSON IR was proposed as a middle layer between LLM output and DSP code, with a separate transpiler converting JSON → Faust or JSON → C++.

**Decision**  
Do not use JSON IR. Have the LLM output Faust directly.

**Reasons**
- A JSON IR schema must be designed, documented, and maintained — it is a second language the LLM must learn on top of DSP concepts.
- Transpiler bugs would create a silent failure mode between LLM output and compiled DSP.
- Faust is already a higher-level IR; adding JSON IR is redundant indirection.

**Consequences**
- No abstraction layer between LLM and compiler; Faust-specific prompt engineering is required.
- If Faust is ever replaced (see ADR-007), the LLM prompts must be rewritten from scratch.

---

## ADR-003 — Use JUCE 7 as the audio plugin host framework

| | |
|---|---|
| **Status** | Accepted |
| **Date** | 2026-04-28 |

**Context**  
The project needs to produce VST3 and AU plugin binaries that load in commercial DAWs (Ableton, Logic, Reaper, etc.).

**Decision**  
Use JUCE 7 (C++17) as the plugin framework.

**Reasons**
- JUCE is the de-facto standard for cross-format audio plugin development; it generates VST3 and AU from a single codebase.
- Mature CMake integration with `juce_add_plugin`.
- Large community, extensive stdlib (audio buffers, parameter management, UI toolkit).

**Consequences**
- JUCE license terms apply (GPL for open-source; commercial license required for closed-source distribution).
- JUCE cannot rename plugin parameters at runtime — fixed parameter IDs (`macro_0..63`) are required (ADR-004).

---

## ADR-004 — Pre-allocate a fixed 64-slot parameter pool

| | |
|---|---|
| **Status** | Accepted |
| **Date** | 2026-04-28 |

**Context**  
JUCE (and the VST3/AU specs) require that the number and identity of plugin parameters be fixed at plugin load time. Generated Faust patches have a variable number of parameters (1–~12 typically). When the user generates a new patch, the parameter count changes.

**Decision**  
Pre-allocate 64 parameter slots (`macro_0` … `macro_63`) at plugin startup. Generated patch parameters are mapped into these slots; unused slots are hidden via display name but remain registered with the DAW.

**Reasons**
- Allows live DSP swap without reloading the plugin or breaking DAW automation lanes.
- 64 slots is sufficient headroom for any realistic generated patch (complex FM synths rarely exceed 20 parameters).

**Consequences**
- DAW automation lanes always show 64 parameters even when fewer are active — display names mitigate confusion.
- `ParamPool::pushToFaust()` must remap slot indices to Faust parameter labels after every compile.
- Slots beyond the active count must be silent (zeroed gain or passthrough) rather than undefined.

---

## ADR-005 — Implement a 3-attempt LLM retry loop with compiler stderr feedback

| | |
|---|---|
| **Status** | Accepted |
| **Date** | 2026-04-28 |

**Context**  
LLM-generated Faust code does not always compile on the first attempt. Failing silently or surfacing raw compiler errors to the user is a poor experience.

**Decision**  
On compile failure, append the Faust compiler's stderr to the next LLM message and re-request generation, up to 3 total attempts.

**Reasons**
- Faust compiler errors are concise and informative — the LLM can correct most issues (unknown function names, type mismatches) given the error text.
- 3 attempts caps API cost at a predictable maximum per user request.
- The benchmark (ADR-006) measures first-try success rate precisely because fewer retries are better.

**Consequences**
- Worst-case latency is 3× a single generation round-trip (~15 seconds at Opus-class models).
- If all 3 attempts fail, the error is surfaced in the plugin UI's status label.
- `llm/generate.py` owns the retry loop; `FaustEngine.cpp` is retry-unaware.

---

## ADR-006 — Use first-try compile rate as the primary benchmark metric

| | |
|---|---|
| **Status** | Accepted |
| **Date** | 2026-04-29 |

**Context**  
We need a quantitative signal to compare DSL options and LLM providers during the architecture evaluation phase (Days 1–2).

**Decision**  
The benchmark metric is **first-try compile rate**: the fraction of natural-language prompts for which the LLM's first response compiles without error, measured across 25 prompts × 2 DSLs × N LLM providers = 50–100 total generations.

**Reasons**
- First-try success directly predicts average API cost and latency in the production retry loop (ADR-005).
- It is fully automatable — no human listening required.
- Stratified across 5 difficulty categories (trivial → generative) to reveal per-difficulty strengths.

**Consequences**
- Subjective quality (musicality, parameter range choices) is not captured — manual review of `results.json` is needed after the automated run.
- Benchmark harness lives in `bench/` and is separate from the production `llm/` layer.

---

## ADR-007 — Evaluate Cmajor as an alternative to Faust (benchmark pending)

| | |
|---|---|
| **Status** | Accepted — Faust retained. See docs/architectural_decisions/ADR-007-faust-vs-cmajor.md |
| **Date** | 2026-04-29 (closed 2026-05-01) |

**Context**  
Cmajor (cmaj) is a typed, OOP-style DSP language developed by JUCE's original creator (Jules Storer). It compiles to native code via LLVM and supports VST3/AU export natively. It was installed on the dev machine and is a credible alternative to Faust.

**Decision**  
Run the ADR-006 benchmark against both Faust and Cmajor with both Claude and Gemini providers before committing to Faust for the Day-2 JIT wiring.

**Evaluation criteria**

| Criterion | Weight | Notes |
|-----------|--------|-------|
| First-try compile rate | High | Primary metric — see ADR-006 |
| LLM prompt complexity | Medium | Smaller stdlib surface → easier prompting |
| JIT embed complexity | High | Must embed inside JUCE without a system install |
| Runtime performance | Medium | Both use LLVM; parity expected |
| Ecosystem / docs | Low | Both adequate; Faust has larger community |

**Cmajor risk factors identified**
- `cmaj play --dry-run` always exits 0; success detection requires parsing stdout for `"Loaded:"` and absence of `"error:"` — fragile.
- Cmajor's standard library (`std::filters`, `std::oscillators`) is smaller than Faust's `stdfaust.lib` — LLM must write more custom DSP math.
- JIT embedding path for Cmajor inside a JUCE plugin is less documented than libfaust.

**Result (2026-05-01)**  
Benchmark complete. Claude: Faust 88% (22/25), Cmajor 60% (15/25). Faust retained.
Full analysis in `docs/architectural_decisions/ADR-007-faust-vs-cmajor.md`.

---

## ADR-008 — Compare Claude and Gemini as LLM providers

| | |
|---|---|
| **Status** | Under evaluation |
| **Date** | 2026-04-29 |

**Context**  
The production `llm/generate.py` currently hard-codes the Anthropic Claude API. Gemini (Google) offers comparable capability at potentially different price/latency points. The benchmark harness now supports both providers.

**Decision**  
Run the full benchmark with both `claude-opus-4-6` and `gemini-2.0-flash` and compare first-try compile rates per DSL and per difficulty category.

**Models under test**

| Provider | Model | Notes |
|----------|-------|-------|
| Anthropic | `claude-opus-4-6` | Current production model |
| Google | `gemini-2.0-flash` | Fast, capable; broad code generation training |

**Next action**  
Run `python bench/run_benchmark.py --provider both`, then `python bench/score_results.py`. Update this entry with the winning provider and rationale. If results are within 5 percentage points, prefer Claude (already integrated, retry loop tested).

---

# ADR-011 — Editor ↔ LLM-layer IPC: one-shot argv subprocess

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
| Locating `generate.py` from the installed binary | **Still open — PF-065, partial fix 2026-08-20.** Upward search (dev layouts) + `PLUGINFORGE_LLM_SCRIPT` (installed layouts) was wrongly marked Closed 2026-07-19 — never exercised against a real installed bundle. Confirmed broken in REAPER, 2026-08-19 (`~/.vst3` has no repo above it). 2026-08-20 added a third step, checking the fixed location `install.sh` already documents (`$XDG_DATA_HOME/pluginforge` or `~/.local/share/pluginforge`) — closes the packaged-install case, does NOT close the reported REAPER repro (a dev-loop copy with no XDG runtime present), which still needs the env override. See `docs/BUGS.md` PF-065. |
| Interpreter discovery (`python3` must be on PATH) | Open — acceptable on the Arch dev target; revisit at distribution time (venv/absolute path) |
| Per-call interpreter startup (~100ms) | Accepted — invisible behind LLM latency |
| Ready-state UX (button re-enables before JIT finishes) | Open — tracked as the follow-on in `docs/pair_draft_editor_llm_bridge.md` point E |

## Revisit trigger

If generation becomes interactive/streaming (token-by-token preview) or multi-turn, the
one-shot model stops fitting — that's the point to reopen this against the persistent-worker
option, as a new ADR.

*To add a new decision: copy the ADR template below, increment the number, and fill in the fields.*

## ADR-011 — Amendment (2026-08-04): `--request-file` mode and an additive `prior_source`

| | |
|---|---|
| **Status** | Accepted |
| **Date** | 2026-08-04 |

**Context**
Refine (`docs/competitive_landscape.md:107-109`'s P1 gap, "our refine loop is our moat #1
made user-visible") needed the prior Faust source to reach the LLM. The prior source
routinely exceeds what's comfortable to pass as a single argv string, and ADR-011's
original decision text is explicit that argv is the transport ("no shell interpretation of
the prompt text") — extending argv further rather than adding a second transport would have
strained that boundary rather than honoured it.

**Decision**
Additive, not a replacement for the argv/stdout contract above:

- `generate.py` gains a third subprocess mode, `--request-file <path>`, read once via
  `_read_request_file()` and otherwise passed through the SAME `_run_subprocess_mode` /
  `generate_json()` path as `--prompt` — one ADR-011 JSON line out, same as always
  (`llm/generate.py`).
- The request schema gains an optional `prior_source` field. `generate_json` folds it into
  the existing user message via a new `_REFINE_PREAMBLE` constant (no new prompt file — see
  `docs/sessions/002-refine-loop-and-ui-redesign.md` for the three reasons) and preflights it
  against the groq token ceiling (`providers.preflight_prior_source`) before the retry loop:
  fits → sent whole; doesn't fit → dropped entirely, never truncated.
- The response schema gains an optional `prior_source_dropped: true`, additive on the
  success path only, same treatment `kind` got in `d587665` — every existing consumer reads
  `success`/`faust_code`/`error` and is unaffected by an extra key.
- `PromptPanel` writes the request as a `juce::TemporaryFile(".json")` with forced `"\n"`
  line endings (`juce_File.h:781-784`'s `replaceWithText` defaults to `"\r\n"`) and degrades
  to a plain `--prompt` request — never a hollow `prior_source` — when there's nothing yet to
  refine (first generation) or the write fails.

**Consequences / hardening status**

| Item | Status |
|---|---|
| `--request-file` missing from the `_subprocess_mode` credential-precheck flag | Closed at introduction — the exact trap A2 was written to avoid; see `_subprocess_mode`'s three-way `or` in `llm/generate.py`'s `__main__` |
| Refine payload blowing the groq token ceiling | Closed — `providers.preflight_prior_source`, drop-not-truncate; measured live (`tools/measure_prompt_tokens.py`) that a 725-char prior source alone left 5 tokens of slack, so this was not optional |
| Whether the model actually honours a folded-in prior source | Open — no test in this repo can prove it; the stated unverified remainder is one live groq run with a marker control surviving into the returned patch |

**Rationale**
Same reasoning ADR-011's original "Alternatives considered" already used against a
persistent worker or a socket: this is one more one-shot subprocess mode, not a new
lifecycle. `_request-file` and `prior_source` are both additive to a wire contract that
already had precedent for growing this way (`kind`, `reason` before it) — no existing
caller's request or response shape changes.

## ADR-011 — Amendment (2026-08-06): `refine_mode` splits Refine into surgical Add vs contextual Redo

| | |
|---|---|
| **Status** | Proposed |
| **Date** | 2026-08-06 |

**Context**
The 2026-08-04 amendment above got the prior Faust source to the LLM at all, but left the
host's Refine control a single ON/OFF toggle, and STATUS.md's Broken #3 named what that
toggle conflated: a *surgical add* ("preserve the DSP exactly, add only the requested
change") and a *regenerate with context* ("full re-generation, prior source as reference")
are semantically different requests, and the toggle could only ever express the second one.
Step 8 of the 12-step generation workflow ("additive change — plugin stays the same besides
a new addition") specifically needs the first, which did not exist.

**Decision**
Additive, same treatment as the 2026-08-04 amendment before it — no existing request or
response shape changes:

- `PromptPanel`'s single `ToggleButton` becomes a 3-item `ComboBox`: New (id 1, unchanged
  Fresh behavior) / Add (id 2) / Redo (id 3). Both Add and Redo send `prior_source`
  (`LoadMode::Iterate`, same as the old toggle's ON state); they differ in the new request
  field below.
- The request schema gains an optional `refine_mode: "surgical" | "context"` field, read
  by `generate_json` (`llm/generate.py`). It selects which of two new preambles —
  `_SURGICAL_PREAMBLE` ("MINIMAL, SURGICAL change... preserve structure, signal routing,
  control names, and behavior EXACTLY") or `_CONTEXT_PREAMBLE` ("free to REWRITE it from
  scratch") — folds `prior_source` into the user message, via `_refine_preamble_for`.
  `refine_mode` absent (an older host, or no `prior_source` at all) keeps the ORIGINAL
  `_REFINE_PREAMBLE` byte-for-byte — the legacy path is unchanged, not merely similar.
- The two modes diverge at the existing token-budget preflight (A6 in the amendment
  above). `"context"` keeps the established soft-drop: doesn't fit → `prior_source` is
  cleared and the response carries `prior_source_dropped: true`, same as before this
  amendment. `"surgical"` HARD-FAILS instead: doesn't fit →
  `_prior_source_refused_response()` returns `success: false, reason: "error",
  attempts: 0`, plus additive `prior_source_refused: true`, and `generate_faust` is never
  called. Silently degrading Add to a full regen would violate the "minimal, surgical
  change" contract the user picked Add for — soft-dropping is Redo's whole
  reason to exist, so Add gets the opposite policy on purpose, not a shared one.
- The host surfaces the refusal from `PromptPanel`'s existing `! success` branch (the same
  one every other generation failure already takes) — `onFaustCompileSuccess` is provably
  unreachable for a `success: false` response, so the surfacing does NOT live there.
  `setError()` already shows generate.py's guidance text unconditionally; the added code is
  the status line's specific text (`statusForReason("error")` alone would say the generic
  "LLM error", contradicting that function's own "say what to DO" contract) and wiring the
  previously-dead `lastPriorSourceRefused` flag.
- `PromptPanel::setRefineModesAvailable(bool)` gates Add/Redo's `ComboBox` items on whether
  a prior source exists at all — seeded in the constructor from
  `processor.currentSource()` (required for a DAW project load, which calls
  `setStateInformation` before `createEditor` even runs, so `onFaustCompileSuccess` is not
  yet wired at restore time) AND refreshed from `onFaustCompileSuccess` (required for the
  same-session case: the first generation in an empty project).

**Reasons**
- Same reasoning as the 2026-08-04 amendment's own: additive to a wire contract with
  precedent for growing this way (`kind`, `reason`, `prior_source_dropped` before it).
- The ADR-011 response `reason` enum stays closed (`ok | invalid_faust | truncated |
  timeout | rate_limited | error`) — a refusal reuses `"error"` and is discriminated by
  the new flag, not a sixth reason value, consistent with how `prior_source_dropped`
  was handled rather than growing the enum.

**Consequences**

| Item | Status |
|---|---|
| Add/Redo unreachable in the real UI (`setItemEnabled(2/3, false)` at construction, nothing ever re-enabling them) | Closed 2026-08-06 — `setRefineModesAvailable`, two call sites (see Decision) |
| `prior_source_refused` read but never surfaced (dead accessor, `priorSourceRefusedForTest()` permanently false) | Closed 2026-08-06 |
| Whether the model actually honours EITHER new preamble in production | Open — same unverified remainder session 002 already named for the single legacy preamble, now doubled across two framings; no test in this repo can prove it, only that the transport carries the right text (`EditorSessionTest` scenarios 16, 25, 26) |
| A DAW project load restoring a source-less blob into an already-open editor, then a later restore attempt clearing an offered Add/Redo mid-session | Open, deliberately not built for — `setRefineModesAvailable`'s force-back-to-New guard makes the display consistent if this path is ever exercised, but nothing in the current restore flow drives it |

## ADR-009 — Verdict (2026-07-19): the rule worked, the prediction did not

| | |
|---|---|
| **Status** | Accepted — verdict appended 2026-07-25 |
| **Date** | 2026-07-19 (measured), ratified 2026-07-25 |

**Context**
ADR-009 added a duplicate-`process` constraint rule to the system prompt and predicted
"≥96% first-try compile on Faust", with an open action item to confirm by re-run. The
re-run happened on 2026-07-19 and the ADR was never updated, so it read as pending
rather than falsified for six days.

**Decision**
Record the measurement. Full 25-prompt re-run: **22/25 = 88%**, not ≥96%.
`bench/results/.prompt_baseline.json` moved 0.84 → 0.88.

**Reasons**
- The *rule* did its job: no duplicate-symbol regressions in the re-run.
- The *prediction* about the resulting compile rate was wrong. Two of three failures
  (ping-pong SEMANTIC, flanger HALLUCINATION) were exact repeats of the 2026-05 run —
  i.e. failure modes the rule was never going to address.
- Conflating "the rule worked" with "the number was hit" is what let the open action
  item sit unclosed.

**Consequences**
- ADR-009 references `llm/prompts/system_faust.txt` (`:20`, `:37`), a file that **no
  longer exists** — the prompt was unified into `llm/prompts/system_prompt.txt` on
  2026-07-21 and that path was deleted. Read those lines as the unified prompt.
- The 88% figure is itself now void twice over: measured against the since-deleted
  prompt, and (2026-07-25) measured with an undetected output-truncation confound.
  Detail: `docs/prompt_efficacy_study.md` §6.

---

## ADR-012 — Provider abstraction with a free-only constraint

| | |
|---|---|
| **Status** | Accepted |
| **Date** | 2026-07-21, ratified 2026-07-25 |

**Context**
Every LLM call went to Anthropic, constructed inline at three separate call sites. When
the Anthropic credit ran out, the entire project stopped: the 125-prompt efficacy run was
rejected pre-generation, producing zero data. A single hard-coded provider was a
single point of failure for all measurement.

**Decision**
One provider registry, `llm/providers.py`, is the only place any component gets an LLM
callable. Five providers, three adapters. `anthropic` is marked `free=False` and is
refused unless `PLUGINFORGE_ALLOW_PAID=1`; every runnable entry point calls
`assert_free()`.

**Reasons**
- Adding a provider becomes a registry entry, not a fourth copy of a client constructor.
- Free-only by default makes an unattended run incapable of spending money.
- Selection through `PLUGINFORGE_PROVIDER` in `.env` reaches the plugin via
  `juce::ChildProcess` inheritance — no C++ change, no rebuild.

**Consequences**
- Restored end-to-end generation on a free provider with no rebuild (`3ebe249`).
- The registry became the natural home for two things that cannot live in the prompt:
  markdown-fence stripping, and `min_max_tokens` (reasoning models bill hidden thinking
  against the output cap).
- Cross-provider numbers are not comparable to Anthropic-era ones. ADR-008's
  "under evaluation" cross-model comparison is now cheap to run and still unrun (PF-012).

---

## ADR-019 — UX-generation surface: native widgets now, WebView deferred

| | |
|---|---|
| **Status** | Accepted |
| **Date** | 2026-07-24, ratified 2026-07-25 |

**Context**
Competing prompt-to-plugin tools increasingly build plugin UI as HTML/JS in a webview.
PluginForge's UI is 100% native JUCE widgets, and webview is deliberately compiled out —
`JUCE_WEB_BROWSER=0` / `JUCE_USE_CURL=0` on every target. That was load-bearing, not
incidental: those defaults of 1 were what broke the build until 2026-07-18.

**Decision**
Keep the native-declarative surface. Do not adopt a webview UI now.

**Reasons**
- Re-enabling `JUCE_WEB_BROWSER` reintroduces the gtk3/webkit2gtk dependency that
  blocked the build, on a platform (Arch) shipping webkit2gtk-4.1 where JUCE looks for 4.0.
- The auto-layout param grid already covers the fixed-64-slot case; the ecosystem survey
  found zero of 19 fixed-param plugins using a bare generic editor.
- It is a dependency and distribution change (§2 trigger 4) with no user-visible win yet.

**Consequences**
- Generated UI stays limited to what the param-grid layout can express.
- Revisit trigger, stated so this is falsifiable: if generated patches routinely need a
  control the grid cannot express, or if a shipped competitor's browser-first iteration
  loop measurably beats ours, reopen.
- **This is the one entry here that decides direction rather than recording a decision
  already taken.** It authorizes no work — it declines work — so reversing it is free.

---

## ADR-020 — OutputGuard's runaway watchdog latches for effects, reports for instruments

| | |
|---|---|
| **Status** | Accepted |
| **Date** | 2026-08-01 |

**Context**

`OutputGuard` mutes the output after `kRunawaySeconds` (0.5 s) of continuous
`|y| >= 1.0`, measured pre-limiter, and the mute **latches** until the next
`reset()` — i.e. until the next successful compile. The rule was written when this
project generated only effects, and for an effect it is right: sustained 0 dBFS
without ever dipping below is a diverging filter, and the blast is the thing being
prevented.

Instruments break the premise. `os.square(f) * gain` at gain 1.0 sits at `|y| == 1.0`
on **every sample, forever, by construction** — a square wave does not return below
unity between transitions the way an oscillating signal that crosses zero does, and
the 5 Hz DC blocker cannot pull it under within 0.5 s. So the first loud square-wave
synth anyone generates is muted half a second in and stays muted, with nothing the
player can do about it short of regenerating the patch. Effects essentially never
produce this; synths do it the moment someone turns the level up.

A second, independent defect was found in the same code and is fixed alongside
rather than decided here: `overScaleRun` was a single `int` declared outside the
per-channel loop, so it counted once per sample **per channel** and was zeroed by any
sample below unity in **any** channel. Stereo therefore tripped in 0.25 s against the
advertised 0.5 s (measured: mono 47 blocks, stereo 24), and a quiet left channel
could hold off a diverging right one indefinitely. That is a straightforward bug —
`kRunawaySeconds` did not denote a time — and needed no ruling.

**Decision**

Split the *detection* from the *response*. Detection is unchanged. The response
becomes a policy set per compiled patch:

- `RunawayPolicy::Latch` — effects. Mute and stay muted. Unchanged behaviour.
- `RunawayPolicy::Report` — instruments. Set `Trip::Runaway` for the editor to
  display, but keep passing audio.

`Trip::NonFinite` latches under **both** policies. The policy is set from the compile
callback, from `FaustEngine::isInstrument()` — the same voice-contract detection that
already drives MIDI routing — inside the `audioBusy` drain window, for the same
reason `outputGuard.reset()` is.

**Reasons**

- **The limiter, not the mute, is what protects the speakers.** The soft knee plus
  hard clamp bounds output at `kCeiling` (−0.3 dBFS) on every sample regardless of
  policy. `Report` gives up a backstop, not the guarantee. This is the load-bearing
  argument: if muting were the only thing standing between a runaway and the
  monitors, the answer would be different.
- **For an instrument, sustained full scale is a statement about loudness, not
  correctness.** The condition does not distinguish "diverging filter" from "square
  wave at unity gain", and for a synth the second is overwhelmingly more likely.
- **A latching mute is unrecoverable from the UI.** `reset()` is called only from
  `prepareToPlay` and the compile callback, so a muted instrument stays muted through
  every note the player tries. A permanently silent plugin reads as a broken plugin.
- **Detection is still worth keeping for instruments.** A synth *can* diverge, and
  `Trip::Runaway` remains visible in the editor. What changes is that the user is
  told rather than cut off.

**Consequences**

- A genuinely diverging instrument patch is now limited rather than silenced. It will
  be loud and bounded instead of loud and then absent. Accepted deliberately.
- `OutputGuard` gains a mode, which is state that can be set wrongly. It is set in
  exactly one place, from a property of the compiled DSP rather than from a build
  flag, so a patch cannot disagree with the policy applied to it.
- `OutputGuardTest` grew the red cases: mono and stereo must trip at the same *time*
  (47 blocks both, was 47/24); `Report` must not mute but must still report and must
  still bound the peak; `NonFinite` must latch under `Report`.
- **Not settled here:** whether the editor surfaces `Trip::Runaway` differently when
  it did not mute. Today the UI reads `isMuted()` for its warning, so a reporting
  trip on an instrument is currently detected and **not displayed**. That is a UI
  task, and it is the honest gap in this decision.

---

<details>
<summary>ADR template</summary>

```markdown
## ADR-XXX — Title

| | |
|---|---|
| **Status** | Proposed / Accepted / Rejected / Superseded by ADR-YYY |
| **Date** | YYYY-MM-DD |

**Context**  
Why does this decision need to be made?

**Decision**  
What was decided?

**Reasons**
- Bullet points

**Consequences**
- Trade-offs and follow-on tasks
```
</details>

## ADR-021 — PluginSpec is not built

Date: 2026-08-04
Status: Accepted

Context
An architecture review proposed a PluginSpec: a structured artifact
carrying signal topology, parameter schema, UI layout, MIDI map, and
acceptance criteria, from which Faust, the JUCE parameter tree, and the
UI would be generated. Brief A gathered evidence from the 19 recorded
generations in bench/ladder_corpus.json, compiling each with
faust-json 2.85.9.

Evidence
- 14 of 19 entries (74%) need nothing beyond FaustEngine::ParamInfo.
- 0 of 19 use hgroup. 0 of 19 use any widget other than hslider. 0 of 19
  carry [scale:] or [style:] metadata. The only vgroup present is the
  Faust compiler's inserted wrapper.
- The 2 real gaps are signal-graph-level facts outside a per-parameter
  struct's scope by construction, not missing fields.

Decision
Do not build PluginSpec. Parameter structure stays discovered at compile
time via MapUI/ParamCapture into ParamInfo. I/O topology is read live
from the compiled dsp instance and never declared.

Consequence
The remaining unmet need is acceptance criteria — capturing what a
generation was asked for so the result can be checked against it. That
is a different artifact from a structural spec and is tracked separately.

Revisit if
Generated plugins begin exhibiting layout intent (grouping, ordering,
control-type preference) in a meaningful fraction of a larger corpus.
The 0/19 result is what kills this; a non-zero result reopens it.

---

Addendum (2026-08-04, Brief F): the "read it live off the compiled dsp
instance" branch of Q1's option 1 (OPEN_QUESTIONS.md) is now implemented,
not just proposed. `FaustEngine::runCompile`'s post-compile validation gate
(`validatePatch()`, FaustEngine.cpp) reads `dsp->getNumInputs()`/
`getNumOutputs()` at compile time and rejects a patch whose topology cannot
run in this host, before the atomic swap publishes it — no PluginSpec field,
exactly the "cheapest option" named above. Confirmed against the same 19-entry
corpus this ADR's evidence came from: the gate's `input-channels` check fires
on entries 10 and 18 (4 inputs, twice the host's stereo maximum) and on no
other entry; entry 1's mono/stereo reversal is unchanged and un-caught, because
it is an intent-vs-result question (see the boundary comment on
`validatePatch()`), not a topology-legality one — a different mechanism, still
not built. See host/tests/ValidationGateTest.cpp for the per-check coverage.

---

## ADR-022 — Per-generated-plugin visual identity: heuristic native-widget variation, not a new LLM artifact or WebView

| | |
|---|---|
| **Status** | Accepted |
| **Date** | 2026-08-05 |

**Context**
Competing tools are named in docs/competitive_landscape.md as beating PluginForge on
generated-UI quality ("'Beautiful UIs' is a stated competitor strength and our weakest
lane"). The question raised: should each generated plugin get a unique, professionally-
designed visual presentation, inferred from the prompt with no explicit spec. Two
existing ADRs bear directly: ADR-021 rejected a structured design-intent artifact
("PluginSpec") on 0/19 evidence of structural affordance usage; ADR-019 rejected a
WebView UI surface, which is what would make bespoke per-plugin graphics easy.

**Decision**
1. Do not add a new LLM-emitted design-intent output to pursue visual uniqueness.
   ADR-021's revisit trigger ("a non-zero result reopens this") has not fired --
   reconfirmed 2026-08-05 (tools/check.sh full's presentation-affordances report,
   same 19-entry corpus: 0/19 hgroup, style, or scale usage).
2. Do not reopen ADR-019 to pursue bespoke per-plugin graphics. No new evidence
   against the gtk3/webkit2gtk build-breakage risk that motivated it.
3. Accepted near-term scope: a per-generation visual palette derived
   DETERMINISTICALLY and HEURISTICALLY from facts already produced by compilation
   (ParamInfo labels/groups, unit metadata, instrument-vs-effect from the voice
   contract), applied through the same setColour()-token mechanism
   ParamGridPanel/KeyboardPanel/Theme.h already use. No new LLM output, no WebView,
   no new distributable artifact -- ordinary Tier-1 C++/UI work, not gated by this
   ADR or any other.
4. Out of scope, explicitly: whether a generated plugin can be exported as its own
   separate distributable standalone. The current architecture already permits a
   coarse version for free (the state blob persists a patch; the built
   PluginForgeSynth/Host binary plus a saved state file is already shareable). A
   genuinely stripped, single-patch-only build is a materially bigger undertaking
   touching ADR-001/ADR-003's JIT-in-host-plugin model and needs its own ADR.

**Reasons**
- Both closed ADRs' stated revisit triggers are unmet; nothing here is new evidence
  against either.
- A heuristic palette costs no schema change and no prompt edit -- doesn't trigger
  §3's Tier-2 prompt/benchmark obligations.
- Keeps CLAUDE.md's "single distributable artifact" line intact without presupposing
  an answer to the export question, which deserves its own dedicated decision.

**Consequences**
- "Unique" means computed-per-generation, not hand-designed -- a real ceiling.
  If that proves competitively insufficient, ADR-019 is the one to reopen, on its
  own stated triggers, not this one.
- The export/standalone-per-generation question stays open, tracked separately --
  not silently resolved by this ADR's silence on it.
- Revisit if: ADR-021's trigger fires on a larger/fresher corpus, or the heuristic
  palette, once built, measurably reads as worse than the current static Theme.

**Amendment (2026-08-13): point 3 was never built — the data it needs is already
captured and thrown away**

Picked back up against a direct user complaint ("the generated plugin should have a
unique design"). Point 3's ungated Tier-1 work was never started: `grep -rn
"heuristic\|per-generation\|derive.*palette" host/Source/` returns nothing.

**What was found, verified by reading the code, not recalled:**

`ParamCapture` (`FaustEngine.cpp:22-`) implements the full Faust `UI` interface and
already records everything point 3 needs: group nesting via `pushGroup`/`closeBox`
(`FaustEngine.cpp:36`, `:217-221`) into `ParamInfo::group` (`FaustEngine.h:66-`),
plus scale, unit, menu style, and `hbargraph`/`vbargraph` meters
(`FaustEngine.cpp:176-203`, `Kind::Meter`). `host/tests/ui_fixtures/
reference_manifest.json`'s `04_generator_grouped` entry (`:124-134`) is direct
evidence: a 17-parameter synth with four clean sections captured —
`groups: ["Env"×5, "Filter"×3, "Fx"×4, "Osc"×5]` — rendered as `grid: [5, 4]`, an
undifferentiated flat grid. The structure exists and is discarded before paint.

Three more pieces are built and unreachable, not merely unplanned:

1. **`ParamGridPanel::applyUiIr()` and `layoutSectioned()`** (`ParamGridPanel.cpp:410`,
   `:500`) are complete implementations of ADR-024's Phase 1a renderer. `grep -rn
   applyUiIr host/Source host/tests` outside their own definitions returns nothing —
   zero callers, confirming ADR-024's own consequences section
   (`:819-820`, "currently unreachable at runtime").
2. **Section headings compute geometry and are never drawn.**
   `layoutSectioned()`'s heading rectangle is discarded on arrival:
   `(void) heading;` (`ParamGridPanel.cpp:520`), with the comment "painted via
   paint() from activeSections" pointing at a `paint()` override that does not
   exist — `grep -n "::paint(" ParamGridPanel.cpp ParamGridPanel.h` is empty. Wiring
   `applyUiIr()` alone would produce sectioned geometry with no visible section
   titles.
3. **`Kind::Meter` (PF-052) is captured and never rendered.** Deliberately excluded
   from a macro slot (`ParamPool.cpp:74`, comment: "See FaustEngine::Kind::Meter"),
   confirmed still absent per `docs/BUGS.md`.

Also newly confirmed, not previously written down anywhere: the title is the literal
hardcoded string `"PluginForge"` (`PluginEditor.cpp:516`), and there is no
plugin-naming, branding, or graphics pipeline in the repo at all — no doc, no ADR, no
code. A plugin displaying its own identity is a prerequisite for "unique" that point 3
never named. `CLAUDE.md`'s rename freeze (`:17-21`) covers identifiers, namespaces and
the `PLUGINFORGE_*` contract — not a runtime-computed display string — so a
per-generation title is not blocked by it, but is worth stating explicitly rather than
leaving a future session to guess.

**Decision (this amendment): build point 3 as four tracks, in this order, each landing
with a `tools/ui_iterate.sh` gallery fixture and `reference_manifest.json` update
(the existing headless design-iteration loop, otherwise sitting idle with nothing to
iterate on):**

1. Wire `applyUiIr()` from `ParamInfo::group`, called from `onFaustCompileSuccess` —
   synthesizes a `UiIr::Layout` from data already captured, not from a new LLM
   field. No prompt change, no headroom cost. This is ADR-024 Track 1.2, now
   concrete: the population source is heuristic derivation from Faust group
   metadata, a third option ADR-024 did not name alongside its "hand-authored" (1a,
   shipped) and "LLM-emitted" (1b, headroom-gated) sources.
2. Add the missing `paint()` override so section titles actually render — Track 1 is
   incomplete without it.
3. Render `Kind::Meter`. Highest-signal "this is a real plugin" element for the
   effort; **`PLUGIN_HEALTH_PLAN.md` (P1.10) requires UI-direction approval before
   meter work** — request it before starting, do not treat this amendment as that
   approval.
4. The heuristic palette point 3 originally specified: derived from group names,
   unit mix, and instrument-vs-effect, through the existing `Theme.h`/
   `ForgeLookAndFeel.h` token mechanism.

Plugin naming/branding is explicitly out of scope for this amendment — flagged as a
real gap, not silently absorbed into track 4's palette work.

**Consequences of this amendment**
- Restates the ADR's original ceiling (`:701-703`) unchanged: all four tracks produce
  *computed* variation, not designed identity. The recorded escape hatch if that
  proves insufficient is still ADR-019's WebView reopening, or session 001 D1's
  cheaper alternative (an embedded vector-path field in the IR) — neither triggered
  by this amendment.
- Track 1 lands before ADR-023's export work (see that ADR's own 2026-08-13
  amendment) so an exported plugin inherits sectioned layout rather than a second
  renderer being built for it.

---

## ADR-025 — Dev-cockpit: localhost mirror for development iteration

| | |
|---|---|
| **Status** | Proposed |
| **Date** | 2026-08-06 |

**Context**
PluginForge's iteration loop requires manual screenshot capture (`tools/screenshot_ui.sh`) and has no structured view of plugin state. Competing tools offer browser-based iteration surfaces with live previews. ADR-019 rejected WebView in the plugin; this builds a complementary localhost mirror instead.

**Decision**
Build a localhost web server (`dev-cockpit/server.py`) that:
1. Serves a static HTML iterate surface at `http://localhost:8765/`
2. Exposes `/api/screenshot` to capture live UI via `tools/screenshot_ui.sh`
3. Exposes `/api/state` to read plugin state from a file the plugin writes

The plugin remains 100% native JUCE. The browser is a *mirror*, not a component.

**Reasons**
- Zero C++ dependencies — server is pure Python, UI is static HTML
- Stays inside ADR-019 — no WebView in the plugin binary
- Enables future phases (UI IR editor, sample audition control, export workflow)
- File polling is simplest viable; WebSocket can be added without breaking the browser

**Consequences**
- Dev-cockpit is a development tool, not a product feature — not shipped with the plugin
- Screenshot capture requires Hyprland compositor (per `tools/screenshot_ui.sh`)
- One plugin instance per cockpit session (state file is singleton)
- Revisit if: a shipped competitor's browser-first iteration loop measurably beats ours

---

## ADR-023 — Export: repo-first, then binary

| | |
|---|---|
| **Status** | Proposed |
| **Date** | 2026-08-06 |

**Context**
Users want to share generated plugins beyond the PluginForge host. ADR-022 explicitly deferred "a stripped, single-patch-only build" as a bigger undertaking. The current architecture allows a coarse version (host binary + state file), but a genuine export requires CMake + JUCE + pinned Faust patch + themed UI.

**Decision**
Two-phase export:

**Phase 2a — Repo export:**
- Generate a git repo with `CMakeLists.txt` (JUCE plugin target, libfaust linkage), `Source/Plugin.cpp` (JIT wrapper), `Source/PluginEditor.cpp` (themed UI), `Patch.dsp` (generated Faust source), and `README.md` (build instructions).
- The repo is buildable on the target platform (Linux first).
- The export does NOT include vendored JUCE — the user must have JUCE installed or point `JUCE_PATH`.

**Phase 2b — Binary export (deferred):**
- Build the repo in a sandbox (Docker/VM).
- Strip to single-patch VST3/Standalone.
- Sign and notarize (macOS).

**Reasons**
- Repo-first is reproducible: user can edit the patch and rebuild.
- Binary export is complex (codesign, notarization, Windows signing) — defer until repo export proven.
- Enables user customization (hand-edit the Faust source, add controls).

**Consequences**
- Requires a "freeze" operation: pin the Faust compiler version, JUCE version, and plugin state.
- The export repo is NOT a PluginForge project — it is a standalone artifact.
- No round-trip: edits to the exported repo are not imported back.
- Revisit if: users consistently ask for binary-only export without source access.

**Amendment (2026-08-13): the Phase 2a stub cannot compile, and the assumption that
export needs libfaust at all was wrong**

Picked back up against a direct user request ("deploy generations as standalone
apps... export as VSTs"). Two findings, both verified this session by reading the
actual code, change the design.

**Finding 1 — `tools/export_repo.py` is not incomplete, it is uncompilable.**
Confirmed by reading it directly, not by trusting its own claims:
- `processBlock` never touches `buffer`, never links or calls Faust — the comment on
  it literally says `// Placeholder: passthrough` (`export_repo.py:94`).
- `acceptsMidi()`/`getTailLengthSeconds()` render as bare, undefined identifiers
  compared to string literals: `plugin_type["needs_midi"]` is the Python string
  `"TRUE"`/`"FALSE"` (`export_repo.py:38-39`), substituted UNQUOTED into the C++
  template, producing `return TRUE == "TRUE";` (`:103`) and
  `return TRUE == "TRUE" ? 2.0 : 0.0;` (`:105`) — `TRUE` is not a defined identifier
  in that scope; this does not compile.
- `tools/export/CMakeLists.txt.j2:21` hardcodes `PLUGIN_CODE Pfh1` — identical to the
  shipping `PluginForgeHost` target (`host/CMakeLists.txt:33`), which that very
  file's own comment warns against: `:109`, "MUST differ from the Fx target's Pfh1
  — hosts identify a plugin by this code, and a collision makes one shadow the
  other."
- Reachable from nothing (no UI button, no CI step); `.claude/skills/export/
  SKILL.md:3` gates itself "STUB — DO NOT RUN". Phase 2a starts from a rewrite, not
  a repair.

**Finding 2 — the hard part is already solved, and no existing doc had noticed.**
The project already reasons about `faust -lang cpp` only as a CLI validator
(`llm/faust_validator.py:12`), never as an export mechanism. But the very
`libfaust.so` the host already links (`host/CMakeLists.txt`'s
`find_library(LIBFAUST_LIB faust)`) exports an in-process AOT path:
`generateAuxFilesFromString(name_app, dsp_content, argc, argv, error_msg)`
(`/usr/include/faust/dsp/libfaust.h:117-119`) — confirmed present in the linked
library, `nm -D /usr/lib/libfaust.so.2.85.9 | c++filt` shows the demangled symbol
exported. It emits ahead-of-time C++ (`class mydsp : public dsp`) for a live patch
**in-process, with no subprocess and no new dependency** — the host already links
everything this needs.

The consequence changes the export's whole dependency shape: **the frozen export
needs no libfaust at build time on the user's machine, and none at runtime.** The
current template's `find_library(faust REQUIRED)` (`CMakeLists.txt.j2:45`) exists
only because Phase 2a assumed the exported plugin would still JIT. An AOT-frozen
export is an ordinary JUCE plugin.

**Decision (this amendment): Phase 2a is redesigned around AOT emission, and its
acceptance criteria are made explicit rather than left implicit:**

1. **The freeze operation.** `generateAuxFilesFromString` → `class PatchDSP : public
   dsp` → a wrapper `PluginProcessor` that instantiates it, calls
   `buildUserInterface` on a `MapUI`, and calls `compute`. This is a stripped
   `FaustEngine` with the entire swap protocol deleted — that protocol
   (`docs/fixplan_pushtofaust_swap.md`) exists only because the JIT host recompiles
   at runtime; a frozen export never does.
2. **Per-generation plugin identity, not a shared code.** Hosts key on the 4-char
   `PLUGIN_CODE`; Finding 1's collision shows what happens without this. Derive
   `PLUGIN_CODE` and `PRODUCT_NAME` deterministically from the patch — libfaust
   already exports `generateSHA1(data)` (`libfaust.h:42`, confirmed exported
   alongside `generateAuxFilesFromString`), and `PluginProcessor` already persists
   both source and prompt in the state blob. VST3 additionally derives a class UID
   from manufacturer + code + name, so this is not cosmetic.
3. **Reuse, not a second renderer.** `ParamCapture`/`ParamInfo` and `UiIr.h` already
   produce everything an exported editor needs to render the same grid, and
   `ParamPool` already handles the 64-slot APVTS mapping. Sequence this after
   ADR-022's amendment (Track 1, wiring `applyUiIr`) lands, so the exported plugin
   inherits sectioned layout instead of a parallel implementation being built for
   an export path that doesn't have it yet.
4. **Acceptance criteria, explicit** (per `PLUGIN_HEALTH_PLAN.md` P1.10, "keep
   export gated until it produces a validated standalone project", and the
   `/export` skill's own three landing requirements): the exported project builds
   clean, loads in a DAW, and makes sound. None of the three has ever been
   demonstrated for any version of this feature.

**A prerequisite this amendment will not paper over:** export inherits STATUS.md's
Broken #2 — this project has never had a plugin in a DAW.
`COPY_PLUGIN_AFTER_BUILD` is `FALSE` on both shipping targets and pluginval is not
installed (`PLUGIN_HEALTH_PLAN.md` P0.4). Validating an *exported* plugin requires
first solving host validation for the plugin already shipped — that is P0.4 and
STATUS.md's own Next-three #1, and it is a real blocking dependency, not an aside.

**Consequences of this amendment**
- Materially lowers the bar for Phase 2a: no sandboxed build step is needed to
  produce a libfaust-free artifact, which was previously implied to belong to the
  deferred Phase 2b.
- Phase 2b (sign/notarize) is unchanged and still deferred.
- Revisit if: `generateAuxFilesFromString`'s emitted C++ turns out to need
  Faust-version-specific runtime support code this repo does not already vendor —
  not yet checked; the symbol's existence and linkage were verified this session,
  its emitted output was not yet exercised end to end.

---

## ADR-024 — UI IR: renderer-agnostic layout for generated plugins

| | |
|---|---|
| **Status** | Accepted (2026-08-14) |
| **Date** | 2026-08-06 |

**Context**
Generated plugins have no visual identity beyond a 64-slot parameter grid. ADR-022 chose
heuristic native widgets over a new LLM artifact or WebView, but the grid has no
structure — no sections, no headings, no per-control style hints. The LLM already knows
the logical structure of the effect it is generating (which parameters are related, which
are primary vs secondary), but that knowledge is lost between generation and rendering.

**Decision**
Introduce `UiIr::Layout` (`host/Source/UiIr.h`): a versioned, renderer-agnostic
intermediate representation describing how the 64-slot grid should be structured. The IR
specifies sections (each with a title, column span, and ordered list of control
references), per-control style tokens (`"arc-knob"`, `"slider"`, `"toggle"`, `"inc-dec"`),
and size hints (`"sm"`, `"md"`, `"lg"`). No pixel coordinates, no JUCE types — string
keys and integer spans only, so a future WebView backend reads the same JSON.

The schema is versioned (`Layout.schema == 1` today). Controls present in the compiled
DSP but absent from the IR are appended to a trailing "Parameters" section, preserving
backward compatibility — a patch without an IR renders identically to today.

Phase 1a (this decision): the renderer (`ParamGridPanel::applyUiIr`) and the schema
(`UiIr.h`) are shipped. IRs are hand-authored only; the LLM does not emit this yet.

Phase 1b (gated on prompt headroom): the system prompt teaches the LLM to emit a
`ui_ir` field alongside Faust code. Requires measuring the token cost against the
existing ~140-token headroom budget *(corrected 2026-08-25 — re-measured via
`python3 tests/test_prompt_headroom.py`; the 124 figure predated the 2026-07-31
stdlib trim and was copied forward uncorrected)*.

**Reasons**
- Sectioned layout is the first step toward visual identity without leaving native widgets
- Renderer-agnostic: a WebView can consume the same IR when one arrives (ADR-019)
- Schema versioning prevents silent breakage across LLM output generations
- The "append unmentioned controls" invariant preserves zero-IR backward compatibility

**Consequences**
- ~~`applyUiIr()` is currently unreachable at runtime (no callers in
  `onFaustCompileSuccess`); wiring it is Track 1.2 of the build order~~ **Closed
  2026-08-14 (session 014, `08e24a8`).** `ParamGridPanel::deriveLayoutFromGroups()`
  (a pure heuristic over `ParamInfo::group`, no prompt change, no LLM) now feeds
  `applyUiIr()` from `PluginEditor`'s compile-success path. Fixing this wiring exposed a
  real use-after-free — `refreshParamKnobs()` cleared `controls` without clearing
  `activeSections`/`irLookup`, so a *shrinking* recompile over a sectioned patch
  dereferenced freed `Control*` pointers — confirmed live under this binary's own ASAN
  build, fixed in the same commit. Canonical section ordering (Osc→Filter→Env→Fx) and a
  suppression threshold (≤1 group or <4 controls → flat grid, preserving the
  backward-compatibility invariant above) are both covered by `EditorSessionTest`
  scenarios 35–36.
- The schema may need expansion (groups, collapsible sections, sub-patches) — version
  bumping is built into the design
- Revisit if: the LLM cannot reliably produce IRs within the prompt budget, or the
  sectioned layout proves worse than the flat grid for most patches

**Note (2026-08-13):** Track 1.2 is now concrete — see ADR-022's same-day amendment.
The population source is neither 1a's hand-authored IR nor 1b's LLM-emitted IR, but
a third option this ADR did not name: heuristic derivation from
`ParamInfo::group`, data `FaustEngine.cpp`'s `ParamCapture` already records and
currently discards. Zero prompt change, zero headroom cost — ready before 1b's
headroom question is settled.

**Note (2026-08-14):** Promoted Proposed → Accepted, per this ADR's own D5 (recorded in
`STATUS.md`'s 2026-08-14 addendum): "ADR-024 promotes Proposed → Accepted as part of
whichever session implements this." Session 014 implemented Track 1.2 in full (see the
Consequences update above) and separately shipped ADR-022 §3's heuristic per-generation
accent palette (`ParamGridPanel::derivePalette()`), built on the same sectioned-layout
wiring — recorded under ADR-022, not here, since it is a color choice over the grid this
ADR renders, not a layout-IR change itself.

---

## ADR-026 — Sampler / drum machine plugins: deferred

| | |
|---|---|
| **Status** | Proposed |
| **Date** | 2026-08-06 |

**Context**
Instrument plugins (synths) and effects are the immediate targets. Samplers and drum
machines are a natural next category, but they require sample playback, which has
fundamentally different infrastructure: sample loading, memory mapping, time-stretched
playback, and per-note polyphony — none of which exist in the current Faust-only pipeline.

**Decision**
Defer samplers and drum machines to a future phase. Faust's `soundfiles.lib` provides
`so.player`, `so.loop`, `so.loop_speed`, and `so.play_interp` — genuine sample-playback
primitives — but they require the soundfile at compile time, which constrains the current
JIT architecture.

When samplers are re-entered, the recommended approach is:
- A pre-built C++ sample layer handles loading, memory mapping, and voice allocation
- Faust handles the filtering and effects processing on the loaded samples
- The two are composed at the plugin level, not inside Faust

**Reasons**
- Samplers require sample-pack distribution, which is a product/UX question, not just a
  DSP one — it touches packaging, file format, and user expectation
- The current Faust JIT pipeline cannot handle compile-time soundfiles without reworking
  the architecture
- Effects and instrument (synth) plugins are sufficient for the alpha; adding samplers
  now would dilute focus

**Consequences**
- `Kind::Sampler` and `Kind::DrumMachine` are not defined in the current plugin-type
  taxonomy
- If samplers are prioritized before the C++ sample layer exists, `soundfiles.lib` can
  be used with static sample embedding (limited but functional)
- Revisit if: the alpha reveals that samplers are the dominant use case, or if a Faust
  upstream change enables runtime sample loading

---

## Status audit — 2026-08-06

ADR-023 and ADR-025 are recorded here as **Proposed**, but their implementation
code shipped in the same session that drafted them (`docs/sessions/008-vision-architecture.md`):

- ADR-023 (Export): `tools/export_repo.py` and `tools/export/CMakeLists.txt.j2` are
  staged for commit. The export stub is known-broken (PF-053) and gated behind
  `.claude/skills/export/SKILL.md` refusal, but the code exists.
- ADR-025 (Dev-cockpit): `dev-cockpit/server.py` and `dev-cockpit/static/index.html`
  are staged. The cockpit state export was gated (off by default) by the same session,
  but the server code ships.

The Status field of both remains **Proposed** pending human review. The decision to
ship code before ratifying the ADR is itself a decision that should be acknowledged
or reversed, not silently corrected.

- **ADR-011 — Amendment (2026-08-06)** (`refine_mode` / surgical Add vs contextual
  Redo, above) is the same pattern one more time: `host/Source/PromptPanel.{h,cpp}`,
  `PluginEditor.{h,cpp}`, `llm/generate.py`, and both test suites are all staged for
  commit, verified green (`tools/check.sh full`, `EditorSessionTest` 213/213), while
  the amendment's own Status field reads Proposed. Same acknowledgement, not silent
  correction.

---

## ADR-027 — Generation-intent fidelity: no automated critique gate on live generation; fix the semantic judge for offline benchmark use only

| | |
|---|---|
| **Status** | Accepted 2026-08-16 (§2's authorized fix landed same day, see `docs/BUGS.md` PF-041/PF-042) |
| **Date** | 2026-08-16 |

**Context**
ADR-021 rejected a structured PluginSpec and named the remaining unmet need explicitly:
"acceptance criteria — capturing what a generation was asked for so the result can be
checked against it... tracked separately." That need has sat untracked since 2026-08-04.
Two unrelated tools have since been built that partially touch it:

- `bench/score_efficacy.py`: an LLM grades a generation 0/1/2 against the prompt.
  PF-041/PF-042 (found 2026-07-30, still open): L4-tier grading is tautological (the
  "ground truth" is byte-identical to the L4 generation prompt, verified 10/10) and the
  0/1/2 scale collapses to binary in practice ('1' returned once in 44 gradings).
- `bench/spectral_judge.py` (new, closed Broken #9 2026-08-06): acoustic-feature
  thresholds (band gain, centroid shift, crest factor, tail length) against five
  keyword-matched FX categories only (lowpass, highpass, reverb/delay/echo,
  compressor/limiter/gate/expander, distortion). No instrument coverage, no qualitative
  intent. Report-only; never gates a generation.

Neither tool can answer "does this match what was asked" in the general case.
COLLABORATION.md §1 states this is not incidental: "whether a generated plugin sounds
like what was asked for... is not delegable to a hook or a model." The mechanism that
does work, per a live 2/2 measurement the same session this ADR was drafted, is the
human-driven refine loop (Add/Redo) — a human notices a mismatch and asks for a fix,
honored by the model.

Separately, PF-011 records the efficacy pilot "generalizes to nothing" (N=50, 1 model,
2/5 categories) — partly because no trustworthy automated signal exists to bulk-evaluate
unattended runs against, which is the actual, still-live cost of ADR-021's deferral.

**Decision**
1. No automated critique/refine pass is added to the live, user-facing generation path.
   The existing Add/Redo refine workflow already serves this, at lower cost (no extra
   LLM call per generation, no added latency, no risk of manufacturing false confidence
   that erodes the listening pass COLLABORATION.md §1 protects) and without contradicting
   this project's stated philosophy.
2. PF-041 and PF-042 are fixed, scoped explicitly to `bench/score_efficacy.py`'s offline
   benchmark path — never wired into `llm/generate.py` or any live request.
   - PF-041: replace the L4-prompt-as-ground-truth with an independently authored
     acceptance spec per effect, written once, never derived from any tier's actual
     generation prompt.
   - PF-042: investigate rubric phrasing vs. judge-model bluntness (untested which, per
     `docs/BUGS.md`) before deciding whether a rubric rewrite or a different/larger judge
     model is the fix; re-measure the 0/1/2 distribution against the same 44-record set
     as a regression check.
3. `bench/spectral_judge.py` stays report-only. Broadening its category coverage or
   promoting it to a gate is out of scope for this ADR — revisit separately if the
   false-positive rate across a larger corpus is ever measured.

**Reasons**
- Tier 2 (`llm/prompts/*`, `llm/generate.py`) evidence bar would apply to any live-path
  change; a live critique gate's own reliability is unmeasured (no data exists on either
  judge's false-positive/false-negative rate against real prompts), so it could not clear
  that bar today regardless of direction taken.
- Fixing PF-041/PF-042 is bounded, well-diagnosed, offline-only work with an existing
  regression harness (the 44-record set) to check against — much lower risk than building
  a new pipeline stage.
- Keeps faith with COLLABORATION.md §1's explicit, load-bearing position rather than
  routing around it silently.

**Consequences**
- PF-011's efficacy pilot remains ungeneralizable until PF-041/PF-042 are actually fixed
  (this ADR authorizes the fix; it does not do it).
- No new pipeline stage, no new RT-safety surface, no new live-path failure mode.
- The "acceptance criteria" need ADR-021 deferred is now scoped (offline benchmark
  evaluation only) rather than open-ended.
- Revisit if: unattended/batch generation (running many prompts with no human present,
  beyond bench pilots) becomes an actual project goal — that changes the cost/benefit
  calculus Decision §1 rests on, since there would then be a live consumer with no human
  refine loop available to fall back on.

## ADR-028 — Context-clear handoff protocol: guard and re-inject, do not gate

| | |
|---|---|
| **Status** | Accepted 2026-08-21 |
| **Date** | 2026-08-21 |

**Context**
Context clears are the one session boundary this project had no instrument for.
`/orient` covers session start, `/change-report` covers a landed change, STATUS.md
covers where the project is overall — none of them cover "the next agent has none of
what I just learned." The loss is a recorded live failure, not a hypothetical:
`STATUS.md`'s "Waiting on you" #0 records a machine dying mid-session and a later
agent wrongly concluding the work was lost, and an earlier entry records four branches
stranded across sessions and never reaching `main`. `.claude/RESUME.md`, the one
harness-generated artifact that already exists for this moment, carries a session UUID
and, in practice, "No task list was active; see transcript" — no project state at all.

`~/.config/agent-policy/AGENTS.md` §11 ("Agent handoff") already specifies the ten
fields a handoff needs and §2 says "do not make me reconstruct the previous session
from raw logs." Nothing implemented it in any project. This ADR implements it here
first (CLAUDE.md's own naming rule already commits future architectural work to a
project-first rollout — see "Follow-up" below).

The natural design — make `/handoff` **mandatory** — runs into a hard limit of the
platform, not a gap in this project's discipline: no Claude Code hook can force a
skill to run. Frontmatter controls only whether the model *may* invoke a skill, never
whether it must. `/clear` does fire `SessionEnd`, but that event cannot block and runs
on a roughly 1.5-second budget — far too late and far too small a window to author a
document. A `Stop` hook can genuinely block a turn, but blocking the turn is exactly
the shape `tests/test_control_wiring.py`'s `TestHooksDoNotOverreach` already argues
against for every other control here: *"a gate that blocks ordinary work gets switched
off, which is the same as dead"* — and Claude Code overrides a Stop hook after 8
consecutive blocks regardless, so a narrow Stop gate would not even be durably
mandatory if built.

**Decision**
1. **No blocking gate.** `/handoff` is a skill, invoked the same way `/change-report`
   already is — by trigger-phrase matching and by CLAUDE.md instruction — with no
   hook empowered to refuse a turn or a session for skipping it.
2. **The guarantee moves from "the handoff is written" to "a handoff that exists is
   never missed."** A `SessionStart` hook (`handoff_injector.py`) re-injects
   `.claude/HANDOFF.md` as `additionalContext` on every `clear`, `compact`, `resume`,
   and `startup`, and — following `tools/status_digest.sh`'s established rule that
   silence is the one forbidden output — prints an explicit `NO HANDOFF ON DISK` line
   rather than nothing when none exists, plus a staleness banner when the recorded
   HEAD has moved or the file is over 24h old.
3. **A narrower, unblockable safety net covers the unplanned case.** A `PreCompact`
   hook (`handoff_precompact.py`) snapshots branch, HEAD, `git status --porcelain` and
   `git diff --stat` to `.claude/handoff-state.json` before compaction discards
   context. It **never writes `HANDOFF.md`** — a shell command has no access to the
   agent's reasoning, so a machine-generated stub silently overwriting a real,
   human-authored handoff would be a strict downgrade rather than a safety net.
4. **One fixed path, overwritten in place.** `.claude/HANDOFF.md` and
   `.claude/handoff-state.json`, always exactly those names, gitignored, never an
   accumulating log. Count on disk is 0, 1, or 2, never more, and there is never a
   window where zero handoffs exist between one being replaced and the next being
   written, because there is no delete step — only overwrite.
5. **Both hooks get the same four-part teeth pattern** every other control in
   `tests/test_control_wiring.py` gets: a shape test (registration), a red case (a
   missing handoff prints the marker; a stale HEAD is flagged; the PreCompact hook
   leaves `HANDOFF.md` byte-identical), a green case (neither hook ever blocks, for
   any source or trigger), and an explicit NOT COVERED paragraph — see
   `TestHandoffHasTeeth` / `TestHandoffDoesNotOverreach`.

**Reasons**
- Matches the pattern this project already uses for the identical limitation on
  `/orient`: `tests/test_control_wiring.py` states outright that "/orient is actually
  run at session start… is a human habit, not a mechanism," and does not pretend
  otherwise. This ADR extends the same honest framing to session end rather than
  inventing a stronger claim it cannot back.
- Separating prose (`HANDOFF.md`) from machine state (`handoff-state.json`) is what
  makes the unplanned-exhaustion safety net safe to have at all — a merged file would
  create a real risk of losing a good handoff to a worse one.
- No architecture, schema, or build-system change; this is agent-workflow tooling
  under `.claude/`, the same category `/orient` and `/change-report` already occupy.

**Consequences**
- A handoff can still simply not get written — this ADR does not close that gap and
  states so rather than obscuring it. What it closes is the *next* failure mode: a
  handoff that was written but never reaches the next session.
- Two new hooks add to the `SessionStart`/`PreCompact` surface `tests/test_control_wiring.py`
  already knows about (`KNOWN_EVENTS` already included both, unused until now).
- Does not move `tools/check.sh assumed` — this is workflow infrastructure, not
  evidence, and does not claim the reserved `*(evidence)*` slot in STATUS.md's "Next
  three things."
- **Follow-up, deliberately excluded here:** promoting the skill to
  `~/.claude/skills/handoff/` so other projects get it too, once it has survived a few
  real clears in this repo. It needs correct `name:` frontmatter before going global,
  or it becomes the next `/orient` shadowing incident.
- Revisit if: Claude Code ships a hook event that can genuinely gate `/clear` itself
  (not just `SessionEnd`'s current no-block, ~1.5s-budget shape) — that would let a
  future revision close the gap Consequence 1 names instead of only mitigating it.

## ADR-029 — Component descriptor: let the compiled patch describe its own surface

| | |
|---|---|
| **Status** | Accepted 2026-08-25 |
| **Date** | 2026-08-25 |

**Context**
A 2026-08-24 audit ("The Discard Problem" / "Compiler as Instrument" session
briefings) found five places where the compiler or the LLM already produces metadata
useful for a distinct, professional-looking generated plugin, and the host discards
it before it reaches the screen:

1. `[style:knob]` is parsed and dropped — `FaustEngine.cpp:102-106` only tests for
   `menu`/`radio`; `ParamInfo` (`FaustEngine.h:66-79`) has no generic style field to
   hold anything else.
2. `hgroup`/`vgroup` orientation is discarded at capture — `openHorizontalBox` and
   `openVerticalBox` both call the identical `pushGroup(label)`
   (`FaustEngine.cpp:218-219`), so a group's intended layout axis never survives.
3. `Kind::Meter` is captured (`FaustEngine.cpp:197,203`) and has no pool slot to
   render into — already filed as PF-052, unchanged by this proposal.
4. The window title is the literal string `"PluginForge"`
   (`PluginEditor.cpp:564`), regardless of what was generated.
5. Which fixed UI bands a plugin gets is currently one bespoke boolean per band, not
   a single descriptor: `89268ec` (2026-08-24, landed on this branch before this ADR
   was drafted) already made the keyboard band conditional on
   `processor.isInstrumentForTest()` via `PluginEditor.h:352`'s `includeKeyboard`
   parameter to `verticalChrome()` — but the sample-browser band was a deliberate,
   separate call to leave unconditional ("Sample browser stays unconditional by
   design", same commit message). That is a real, already-made decision this ADR
   does not reopen; it is cited here because the next two bullets generalize the
   *mechanism* `89268ec` used for the keyboard into something that could decide
   sample-browser inclusion too, if that decision is ever revisited — which is a
   separate question from this ADR.

ADR-024 already solved the adjacent problem — renderer-agnostic *section* layout,
via `UiIr::Layout` (`host/Source/UiIr.h`), populated today by
`ParamGridPanel::deriveLayoutFromGroups()`, a pure heuristic over `ParamInfo::group`
with no LLM output and no prompt change (ADR-024's 2026-08-13 note). ADR-022 §3 used
the same derivation pattern for `derivePalette()` (`ParamGridPanel.cpp:642-658`): hash
the instrument bit plus sorted group names, modulo `Theme::GeneratedAccent`'s four
swatches, applied to three `Slider` colour IDs (`ParamGridPanel.cpp:304-306`). Both
existing systems are deterministic, post-compile, and reuse facts the compiler
already produced. This ADR proposes extending that same pattern to answer one more
question: not just how the parameter grid is sectioned or coloured, but **which
components exist in the window at all**, and what it's titled.

**Decision**
1. Do not add a new LLM-emitted design artifact. ADR-021's "no PluginSpec" and
   ADR-019's "no WebView" both stand; nothing here reopens either. Every input this
   ADR proposes reading already exists post-compile: the voice contract
   (`FaustEngine::isInstrument()`, `FaustEngine.h:178`), `ParamInfo::group`, and
   `Kind::Meter` zones.
2. Add a `style` field to `ParamInfo` (currently only `isMenu : bool`,
   `FaustEngine.h:79`) so `[style:knob]` and future style hints survive capture
   instead of being silently absorbed into the `menu`/`radio` check at
   `FaustEngine.cpp:102-106`.
3. Capture `hgroup`/`vgroup` orientation before it collapses into a bare group name
   at `FaustEngine.cpp:218-219` — carry it on the group path so a future layout pass
   can honor it, without committing yet to what the renderer does with it.
4. Extend `UiIr::Layout`'s schema (version bump per ADR-024's own versioning design,
   `UiIr.h:43`) with a component list — which of `{keyboard, sample browser, meter}`
   are present — computed by the same deterministic pass that already produces
   `deriveLayoutFromGroups()`, from the voice contract and captured `Kind::Meter`
   zones. This generalizes `89268ec`'s single keyboard boolean into one descriptor
   mechanism, without itself deciding to change the sample-browser call `89268ec`
   already made on the record.
5. Compute a short plugin title from the same inputs `derivePalette()` already
   hashes (family + accent), replacing the literal `"PluginForge"` at
   `PluginEditor.cpp:564`. No new information; a name instead of a colour index.

**Reasons**
- Every one of these is subtractive or derivative — nothing here asks the model for
  more output, so none of it re-opens COLLABORATION.md §3's Tier-2 prompt/benchmark
  obligations. The information already exists; this proposal stops discarding it.
- Keeps faith with ADR-024's own versioned-schema design instead of adding a second,
  parallel descriptor mechanism next to `UiIr::Layout`.
- Point 5 (title) costs one function and zero new captured data.

**Consequences**
- `verticalChrome()` (`PluginEditor.h:352`) would need to read the descriptor rather
  than a single hardcoded `includeKeyboard` bool once more than one band is
  descriptor-driven — a real signature change, not just an additive field.
- `PF-052` (`Kind::Meter` has no pool slot) becomes a prerequisite for the meter
  component actually rendering, not just being listed as present — this ADR does not
  close PF-052 itself.
- Whether the sample-browser band ever becomes descriptor-driven (reopening
  `89268ec`'s explicit "unconditional by design" call) is a separate decision this
  ADR deliberately leaves open rather than answering by implication.
- Revisit if: the schema version bump proves incompatible with saved state blobs
  from patches authored under schema 1, or the component list's heuristic produces a
  visibly wrong result on a real corpus patch (analogous to ADR-024's own suppression
  threshold, added after `<4 controls` sectioning looked wrong in practice).

**Accepted 2026-08-25, same session, by explicit user decision** — Proposed →
Accepted, per COLLABORATION.md §2 ("Claude drafts an ADR and proposes it; the human
decides"). Acceptance is the direction, not the implementation: none of the code
changes in Decision §2-5 have been written yet. Landing them still goes through the
ordinary `tools/check.sh` ladder and, for anything touching `host/Source/`, the
Tier 2 evidence bar — this entry authorizes the direction, not a bypass of either.

---

## ADR-030 — Do not adopt LangGraph to orchestrate the generation pipeline

| | |
|---|---|
| **Status** | Accepted 2026-08-27 |
| **Date** | 2026-08-27 |

**Context**

The question raised: should `langgraph` (LangChain's graph-orchestration library, used
standalone — never full LangChain) model the offline generation pipeline as a state
machine of typed nodes and conditional edges, with checkpointing?

The pipeline it would orchestrate, read at HEAD:

- **One linear function.** `generate_json()` (`llm/generate.py`) is a single function with
  one bounded loop: `for attempt in range(1, max_retries + 1)` (`llm/generate.py:568`,
  `max_retries` defaults to 3). Its state is four scalars — `error_ctx`, `truncated`,
  `attempt`, `prior_source_dropped` — and it leaves through a handful of explicit
  `return` points (a rate-limit failure, a budget/timeout failure, a truncation failure,
  a validated success, an exhausted-retries failure).
- **All routing decided once, before the loop.** Provider, model, `kind` (which system
  prompt), `refine_mode`, and whether `prior_source` fits the token budget are all
  resolved before the first attempt and are explicitly required to stay fixed across
  every attempt — re-deciding mid-loop "would let a retry silently switch prompts
  mid-generation and repair the code against rules the first attempt never saw"
  (`llm/generate.py:279-292`, `:540-560`).
- **A one-shot process.** `generate.py` runs as an argv subprocess (ADR-011) that lives
  ~150 s under a 180 s hard kill and exits. There is no server, no session, no resumable
  state.
- **One deliberate compression point, not a conversation.** A refine request's prior
  Faust source is folded into a single user message on purpose — the multi-turn framing
  was considered and rejected (`llm/generate.py:195-196`, and
  `docs/sessions/002-refine-loop-and-ui-redesign.md`).
- **Deliberately shallow error handling.** `error_classes.RETRY_HINT` carries exactly one
  entry by design (`llm/error_classes.py:130-131`); the taxonomy classifies, it does not
  branch a topology.
- **Three runtime dependencies** in root `requirements.txt` (`anthropic`,
  `python-dotenv`, `pytest`; `httpx` is a fourth in practice — see the aside below).

**Alternative considered**

Adopt `langgraph` standalone: express the pipeline as a typed `StateGraph`, the retry
cycle as a conditional edge back to the generate node, with LangGraph's checkpointer
persisting state between steps.

**Decision — do not adopt.**

It is a pipe with one bounded cycle, not a graph. Every headline feature of LangGraph
maps onto something this project has already refused or does not have:

| LangGraph feature | This pipeline |
|---|---|
| Checkpointing / resumable state | One-shot subprocess, ~150 s, nothing to resume |
| Streaming | ADR-011's named revisit trigger — not adopted |
| Multi-turn / conversational state | Deliberately folded to one message (`generate.py:195-196`) |
| Tool-calling agent loop | Not used; generation is a single non-streaming text call (research doc §4.1) |
| Conditional-edge topology | One `for` loop with a fixed pre-decided route |

A prior partial verdict is already on record:
`docs/research/plugin-evolution-ui-provider-architecture-2026-08-13.md:336` — "Retain the
registry instead of adding LangChain or LiteLLM. Faust generation is a narrow
non-streaming text path…". This ADR generalizes that from the provider layer to the
orchestration layer.

Adopting it is a COLLABORATION.md §2 double-trigger — **trigger 4** (a new Python
package) and **trigger 2** (it would define the pipeline's control-flow architecture) —
with no demonstrated problem (AGENTS.md §12: "Every additional abstraction layer must
solve a demonstrated problem").

**Consequences**

- The retry loop stays a hand-written `for` loop in `generate_json()`.
- **Explicit reopen trigger.** Revisit only if the *offline* pipeline grows a genuine
  multi-node topology — concretely, if **≥2** of the following land:
  1. a `faust-rs --check` advisor that branches the repair strategy on the returned FRS
     error code (the `#26` work);
  2. an offline critic / decompose pass before or between generation attempts;
  3. provider-failover routing (attempt on provider A, fall back to provider B on a
     class of failure).
  Even item 1 alone is ~5 lines inside the existing loop, not a graph. At the ≥2 point,
  re-evaluate `langgraph` **against a hand-rolled dispatch table** as a fresh ADR — the
  bar is a topology a `dict` of `{state: handler}` cannot express cleanly, not merely
  "more than one branch".
- No migration or rollback implication: nothing is being changed.

**Aside — a real under-declaration this ADR surfaced (fixed separately).** `httpx` is a
hard, unconditional `import` at `llm/providers.py:56` but appears only in
`bench/requirements.txt`, not root `requirements.txt`. It is already an effective runtime
dependency; the manifest just does not say so. Corrected in the same session as this ADR
(see the change report / handoff), reasoned as ungated under §2 ("correcting an
under-declaration, not adding a dependency"). PF-067 is the cautionary neighbour — an
*uncapped* `anthropic>=0.40.0` resolved to 1.0.0 and pulled `httpx2`, breaking every
`import httpx` — so the added pin carries an upper bound.

**Accepted 2026-08-27, same session, by explicit user decision** ("Accept ADR-030 and
ADR-031"). Acceptance records the decision not to adopt; it does not foreclose a future
reconsideration under the reopen trigger above.

---

## ADR-031 — Knowledge tooling: an ID-resolution test and a headless graph emitter, not an Obsidian vault

| | |
|---|---|
| **Status** | Accepted 2026-08-27 |
| **Date** | 2026-08-27 |

**Context**

The question raised: should development adopt a knowledge-graph tool — concretely an
Obsidian vault over `docs/` — for backlink navigation, a graph view, and Dataview-style
dashboards over the project's records?

The repository already *is* an ID-addressed, test-enforced knowledge graph:

- **~6 ID namespaces**: `PF-NNN` (defects, `docs/BUGS.md`), `ADR-NNN` (`docs/decisions.md`
  + `docs/architectural_decisions/`), `docs/sessions/NNN-*`, `D1`–`DN` design-decision
  series, the `P0`–`P6` phase series, plus hook / skill / agent names.
- **On the order of a thousand cross-references** as bare IDs and **~700 backtick
  repo-paths** — the latter already parsed and dead-reference-checked by
  `tests/test_control_wiring.py:190`'s `_PATH_RE`, against `LIVE_DOC_FILES` (`:299`) and
  the `.claude/skills|agents|rules` tree. Real markdown hyperlinks number in the dozens:
  `docs/` is deliberately not a wiki.
- **The governing rule is `COLLABORATION.md:335-336`**: "Every process document is either
  (a) mechanically checked against the mechanism it describes, or (b) dated and
  read-only." Anything that is neither is deleted, not maintained (`COLLABORATION.md:382`,
  §8 table at `:360`).
- **The 2026-08-19 purge's root cause was exactly this idea's target**: not markdown
  volume (the docs:code ratio had *improved*) but staleness, dead cross-references, and a
  shadowed ritual — diagnosed by a throwaway reference-graph agent
  (`docs/records/doc-purge-2026-08-19.md`).

**Problem**

Live dead cross-references exist right now:

- `ADR-013` is cited from two continuously-live documents — `COLLABORATION.md:162` and
  `.claude/skills/change-report/SKILL.md:61` (the same worked-example line, copied) — and
  **was never written**. (The `docs/architecture_review_2026-07-21.md` table also lists
  `ADR-013`/`014`/`015` as intended, but that is a dated point-in-time doc and is
  allowed to name things that were never built, same as git history.)
- `PF-061` is referenced at `STATUS.md:357`, self-labelled "unfiled tracking", with **no
  `docs/BUGS.md` registry row**.
- `ADR-010` and `ADR-014`–`ADR-018` were never written; unlike `ADR-013` they are not
  currently cited from any live document, so they are latent, not active, defects.
- The `P`-series and `D`-series have no registry file at all.

Nothing mechanical catches an unresolved `ADR-NNN` or `PF-NNN` today — only unresolved
backtick *paths*.

**Alternative considered**

Adopt an Obsidian vault over `docs/`: `[[wikilink]]` cross-references, the graph view,
Dataview dashboards, backlink panes.

**Decision — four parts.**

1. **No knowledge-graph tool as infrastructure.** An Obsidian vault is a third document
   category under `COLLABORATION.md:382` (neither mechanically-checked nor
   dated-and-frozen); it is a GUI application invisible to this project's headless
   tooling and agent workflow; and migrating backtick paths to `[[wikilinks]]` would
   **silently disable** `test_control_wiring.py`'s dead-reference check — the exact
   "declared control that never ran" failure this project has hit four times
   (`CLAUDE.md`, "A control counts only once it has been seen failing"). It also produces
   documentation, which `CLAUDE.md` states cannot move the project's one metric
   (`assumed`).

2. **The in-philosophy fix: ID-resolution checks in `tests/test_control_wiring.py`.**
   Every `ADR-NNN` cited in a `LIVE_DOC_FILES` member (and the extension tree) must
   resolve to a heading in `docs/decisions.md` or a file in
   `docs/architectural_decisions/`; every `PF-NNN` cited anywhere in the tracked tree
   must resolve to a `docs/BUGS.md` registry row. Ships **with red cases**
   (`CLAUDE.md`). Built in WP3a of the plan this ADR authorizes.

3. **The graph the vault cannot build itself: `tools/kg.py`.** A headless,
   dependency-free (stdlib only) emitter of the project's ID graph — nodes = documents +
   `ADR-NNN` + `PF-NNN` + `session NNN`; edges = "document references ID" and "ID defined
   in document" — as Mermaid (default; renders in a fenced block and in an Obsidian
   note), DOT, or JSON. It flags **dangling** references and **orphan** documents
   distinctly, because those two categories are what the 2026-08-19 purge was made of.
   **Not a CI gate**: it makes no assertions, and wiring a zero-assertion lane into
   `check.sh` would report it as healthy (`host/CMakeLists.txt`'s `UiDesignGallery`
   precedent). It is a viewing tool. Built in WP3b, sharing WP3a's scanner.

4. **Obsidian permitted only as a personal, non-authoritative lens.** Constraints (to be
   added to `COLLABORATION.md §8`): nothing authoritative lives only in the vault; no
   tool, hook, test, or skill may depend on Obsidian being present; `.obsidian/` and any
   generated `docs/_graph.md` are gitignored; the backtick-path citation syntax is
   unchanged. Under those constraints it is a convenience for backlink navigation and the
   graph view, not architecture.

**Reopen trigger**

If parts 2 + 3 plus vault navigation still leave a real "query the record like a
database" need (e.g. "every open `PF` in lane S1 touched since date X"), the next step is
YAML frontmatter across the corpus plus a `tools/kg.py --check` CI gate — never Obsidian
plugins as the engine. That is a fresh ADR at that point.

**Consequences**

- WP3a adds ~60 lines of test and forces a decision on the `ADR-013` / `PF-061` live
  dangling refs (fix the prose, write a stub, file the row, or an explicit allowlist with
  the reason quoted — recorded in the change report).
- `tools/kg.py` adds a script and no dependency.
- `COLLABORATION.md §8` gains the four Obsidian constraints above once this ADR is
  Accepted.

**Accepted 2026-08-27, same session, by explicit user decision** ("Accept ADR-030 and
ADR-031"). WP3a (`tests/test_control_wiring.py::TestIdReferencesResolve`) and WP3b
(`tools/kg.py`, `tools/id_graph.py`) landed the same session; the `COLLABORATION.md §8`
edit in part 4 lands with this acceptance. Acceptance is the direction — a `tools/kg.py
--check` CI gate and corpus-wide frontmatter remain out of scope (the reopen trigger).

---

## ADR-032 — In-plugin provider/model selection and a plugin-read config file (narrow v1)

| | |
|---|---|
| **Status** | Accepted 2026-08-29 |
| **Date** | 2026-08-28 |

**Context**

The plugin owns none of its configuration. Which LLM provider and model to use, where
`llm/generate.py` lives, which interpreter runs Soundfetch, and every API key — all of it
is read from the environment the DAW process happened to inherit. `PromptPanel.cpp` and
`PluginProcessor.*` contain **zero** `"provider"` / `"model"` request-JSON fields
(`docs/research/plugin-evolution-ui-provider-architecture-2026-08-13.md` §10.2, checked
directly); the user cannot pick a provider or model from the plugin at all.

A DAW started from a desktop launcher inherits none of the `PLUGINFORGE_*` variables and no
`.env`. This is not hypothetical:

- **PF-071** — resolution falls through to a stale 2026-08-15 XDG-installed runtime that
  defaults to the *paid* provider and has no `.env`, so a launcher-started REAPER/Carla
  shows an "anthropic provider error" (reproduced 2026-08-28 in both).
- **PF-065** — the installed VST3 can't find `generate.py` at all without an exported
  `PLUGINFORGE_LLM_SCRIPT`.
- **Soundfetch** — "cannot fetch anything" because the interpreter env vars aren't set
  (on top of PF-056's 403 key).

The full design (research doc §4: `ProviderProfile` schema, native OpenAI, custom
OpenAI-compatible endpoints, model-discovery UI, connection testing, and an
OS-credential-vault bridge) is large, and §4.4 explicitly declines to choose the
credential-bridge mechanism — "The bridge is an ADR-level implementation decision." That
open question should not block fixing the observed failures.

**Alternatives considered**

1. **The full §4 v1** (OpenAI native + custom endpoints + `ProviderProfile` + OS-vault
   bridge). Rejected *for v1*: it is scheduled as Phase 3, gated behind the module-project
   and UI work (Phases 1–2), and step 3 of it is the unsettled §4.4 credential bridge. Too
   much surface, and none of it is what PF-065/PF-071 need.
2. **Do nothing — keep env-only config.** Rejected: PF-065 and PF-071 prove it is broken in
   a real host, and "export three variables before launching your DAW" is not a product.
3. **LangChain / LiteLLM to abstract providers.** Rejected — ADR-030, and research §4.1:
   the registry already covers five providers; a framework hides the token/limit/finish-
   reason differences this project needs to see.
4. **Auto-failover between providers when one errors.** Rejected (research §4.3): it can
   disclose a private prompt and generated source to a second vendor and incur unexpected
   cost. A provider error stays a provider error.

**Decision — the narrow v1 from research doc §10.2, and only that**

1. **Add `provider` and `model` to the request-JSON contract.** `generate_json()` already
   accepts both (`llm/generate.py:507`); this is a C++-side and `INTERFACE.md` change — the
   `--request-file` schema gains two optional string fields. This is the cross-component
   contract change ADR-032 exists to gate (COLLABORATION.md §2 trigger 3).
2. **An in-plugin picker for the five already-integrated providers** — Gemini, Groq,
   OpenRouter, Ollama, gated Anthropic. No new adapter code: `_make_anthropic`,
   `_make_gemini`, `_make_openai_compat` (`llm/providers.py`) already cover all five. The
   model field is a free-text entry plus whatever curated list is cheap to ship; no
   discovery API call in v1.
3. **A plugin-read config file**, `$XDG_CONFIG_HOME/pluginforge/config.json` (else
   `~/.config/pluginforge/config.json`), versioned, holding: `active_provider`,
   `active_model`, `generate_script_path`, `soundfetch_interpreter_path`. The plugin reads
   it at construction and (a) passes provider/model as request-JSON fields, (b) adds it as
   a resolution source in `resolveGenerateScript()` **before** the XDG step, (c) passes the
   interpreter path to `SoundfetchClient`. The **JUCE UI writes this file**; it is not
   `.env` and carries no secret — §4.4's prohibition is on writing `.env` and on storing
   *credentials* outside a real vault, not on a non-secret preferences file.
4. **Credentials stay exactly where they are** — `.env` / environment, read by the Python
   side. v1 does not add an in-plugin key field and does not touch credential storage.
5. **Active provider/model is an application preference** (§4.3): applied to the next
   request only, captured as an immutable snapshot per generation, never patch or DAW-session
   state. One config file shared across DAW instances.

**Explicitly deferred to v2** (a later ADR, likely with `/architecture-planning` for the
credential bridge): in-plugin API-key entry; the OS-credential-vault bridge (§4.4's open
question); native OpenAI (Responses API) and generic custom OpenAI-compatible endpoints;
the `ProviderProfile` persistence schema; connection-test and model-discovery UI; the
Soundfetch "Sound Sources" settings area (§5.2). v2 is where "type your key into the
plugin" lands.

**Consequences**

- `INTERFACE.md` gains two optional request fields — a wire-contract edit, done when the
  code lands, Tier 2.
- A new `config.json` format: small, versioned, non-secret, `~/.config/pluginforge/`.
  Migration is trivial — absent file → today's env-only behaviour unchanged.
- `PromptPanel` gains a provider/model control and a config read; `resolveGenerateScript()`
  gains a config-file source ahead of the XDG step (which also blunts PF-071 — a config
  path beats a stale install).
- **Rollback:** delete `config.json`; the plugin falls back to environment resolution
  exactly as today. No persisted state format changes, no schema version bump on patches.
- **Does not fix** PF-065's install-layout half (a real `install.sh` writing a fresh,
  version-matched runtime + `.env`, and the plugin preferring it over a stale one). That
  stays with PF-065.

**Adversarial critique**

- *"A fourth resolution source for `generate.py` is more complexity on the exact path PF-065
  already made fragile."* True — but a user-controlled config file is the one source a user
  can actually fix when it's wrong, unlike an upward directory walk or a silent XDG
  fallback. And it lets the plugin surface *which* path it used.
- *"Writing a config file from the JUCE UI is the thin end of writing `.env` from the UI,
  which §4.4 forbids."* The line §4.4 draws is secrets, not preferences — a provider name
  and a file path are not credentials. The moment v1 is tempted to add a key field, that is
  the v2 ADR, not a config.json field.
- *"Shipping a free-text model field with no validation invites `gpt-5-turbo-9000`
  typos."* Accepted for v1 — a bad model id fails fast with a provider error, which is
  recoverable; the curated list covers the common case. Discovery is v2.

**Status: Accepted 2026-08-29.** Drafting an ADR is ungated (COLLABORATION.md §2);
acceptance is the human's, given 2026-08-29 after an `/architecture-planning` walk. This
authorises the v1 scope above and the `INTERFACE.md` contract change; it does **not**
authorise any v2 item. Verified while walking the decision: `generate_json()` already
reads `request.get("provider", …)` / `request.get("model")` (`llm/generate.py:507-510`)
and the three provider adapters at `llm/providers.py:743-745` already cover all five
providers — so the v1 "no Python change, C++/`INTERFACE.md` only" claim holds. Implementation
is separate, unstarted, and Tier 2 (`INTERFACE.md` wire contract + `PromptPanel` C++).
PF-065's install-layout half is out of scope and stays open with PF-065; PF-071's
stale-runtime mechanism is what decision item 3's config-file-before-XDG ordering addresses.

---

## ADR-033 — Pre-generation design-plan review ("recommend" action)

| | |
|---|---|
| **Status** | Accepted with conditions — 2026-08-31 |
| **Date** | 2026-08-31 |
| **Relates to** | ADR-011 (stdout JSON schema), ADR-032 (in-plugin provider/model; backend landed PR #42) |

**Context**

The Codex branch `feat/recommendation-mvp` (PR #39, +1,379 / −27, no ADR) adds a
**pre-generation design-plan review workflow**: before Faust is generated, a `recommend`
LLM call returns a bounded, editable plan — title, summary, 1–5 ordered modules, 1–12
controls, deterministic product constraints. The user edits it in a native
`RecommendationPanel`, accepts, and the edited object flows into generation as `design_plan`
(folded into the prompt by `recommendation.format_design_brief()`). The feature is
well-built: `llm/recommendation.py` does typed validation (per-field length caps,
duplicate-name folding, module-reference integrity); `constraints_for()` injects
MONO_VOICE / CUSTOM_METERS_UNAVAILABLE / LIVE_INPUT_NOT_SAMPLE_PLAYER deterministically
rather than trusting the planner; 44 Python tests + 2 C++ `EditorSessionTest` scenarios; the
panel is editor-local and transient (a prompt/family/mode change marks it stale; project
state never stores it).

Three things need a decision, not a merge:

1. **The review step is mandatory on the Fresh path, not opt-in.** `refineSelector` defaults
   to "New"; `submitPrompt()` routes it to `queueRequest("recommend")` and the Generate
   button relabels to "Recommend". Every fresh UI generation becomes two LLM round-trips
   (`generate.py`: "A normal recommendation flow invokes this subprocess twice for one
   generation"). Free-tier provider quota is already the binding constraint on the PF-011
   efficacy run.
2. **It changes the legacy `generate` path.** `router.detect_target_mismatch()` runs inside
   `generate_json()` for any request carrying an explicit `kind` (verified: the call is in
   *both* `generate_json()` and `process_json_request()`). The host sends `kind` on `main`;
   so does `bench/check_refine_preamble_live.py`. A prompt whose vocabulary out-scores its
   selected target now returns `reason:"target_mismatch"` and does not generate — a hard
   behavior change, unmeasured against the corpus.
3. **It edits the ADR-011 wire contract** (three consumers — host C++, Python, bench): a
   top-level `action` dispatch (`generate` | `recommend`), a `design_plan` request field, a
   `recommendation` response object, two new `reason` codes (`target_mismatch`,
   `invalid_recommendation`). This is COLLABORATION.md §2 trigger 3.

Since PR #42 landed, the branch conflicts with `main` on `INTERFACE.md`,
`PromptPanel.{cpp,h}`, and `host/tests/EditorSessionTest.cpp` — including a **direct
scenario-number collision on 43** (main's is config-provider-model; the branch's is
recommendation-review).

**Decision**

**Accept the recommendation workflow. Four conditions before the branch merges, and it is
sequenced *after* the ADR-032 picker follow-up** (both touch `PromptPanel`'s provider/model
wiring, the request-JSON build, and the same tight control-area layout — picker-first means
one clean rebase, not two).

1. **Review is opt-in.** Default "New" calls `generate` directly (single round-trip,
   Generate button unchanged). Add an explicit opt-in — a fourth `refineSelector` item
   ("Plan"), so the existing invalidation wiring and `updateActionButton()` apply unchanged.
   `submitPrompt()` routes only that selection to `queueRequest("recommend")`. Add an
   `EditorSessionTest` scenario asserting the default path is one `generate` call, no panel.
2. **`detect_target_mismatch` is scoped to the `recommend` action.** Remove the call from
   `generate_json()`; keep it only in `process_json_request()`'s `recommend` branch. A
   mismatch guard on plain `generate` is a separate Tier-2 change with its own corpus
   re-run, not a rider. Add a Python test that the legacy path no longer calls it.
3. **Rebase onto the landed ADR-032 contract; one combined `INTERFACE.md` section;
   renumber the branch's scenarios (43 is taken).** Provider/model precedence, one
   `INTERFACE.md` sentence: **the config-file `active_provider`/`active_model` is the
   default; a `recommend` response's resolved `provider`/`model`, echoed back on the
   following `generate`, pins that generation and overrides the config default; absent an
   echoed pin, the config default applies.**
4. **COLLABORATION.md §3–§5 hygiene:** non-empty commit bodies; the branch's own
   `INTERFACE.md` + `STATUS.md` updates; `EditorSessionTest` scenarios built and green under
   `tools/check.sh full`.

Scope boundary: `recommendation` schema stays `schema:1`, bounded (≤5 modules, ≤12 controls,
`_LIMITS` caps). No planner-authored constraint codes — `constraints_for()` stays
deterministic and host-authoritative. The panel stays editor-local and transient.

**Reasons**

- The branch is tested and rots against `main`; a decision now is cheaper than a cold
  re-review after the release.
- Doubling the default-path LLM call count against the quota that blocks the one metric is a
  direction call the human must make — hence opt-in (condition 1).
- `detect_target_mismatch` on the legacy path is a silent behavior change to a path this
  feature is not otherwise about; it deserves its own evidence (condition 2).
- The workflow *is* Phase-5, which `docs/phases/README.md` says runs parallel *into* the
  release, not before it — hence sequenced behind the release-relevant picker work.

**Consequences**

- `INTERFACE.md` gains `action` / `design_plan` / `recommendation` / two `reason` codes on
  top of ADR-032's `provider`/`model` — one combined contract section.
- Default-path cost unchanged (condition 1); the opt-in path is two calls by explicit user
  choice, first capped at `max_tokens=1200`.
- `PF-014` prompt log records only `generate` actions (branch already does this).
- CLAUDE.md file-map / prompt-files section gains `llm/recommendation.py` and
  `llm/prompts/recommendation_prompt.md` when the code lands (Tier 1, at land time).
- Rollback: `action` is optional, defaults to `generate`; removing the opt-in control + the
  panel returns to today's behavior, no schema break, no state migration.
- New source files — `host/CMakeLists.txt` adds them to the target; no new dependency.
- **Unverified until the implementation lands:** whether the planner's output quality on the
  shipping free-tier model is worth the round-trip — a reading/listening judgment, not a
  test.

---

## ADR-034 — A single-purpose reproduction container for issue #26

| | |
|---|---|
| **Status** | Accepted — 2026-09-01, by explicit user decision |
| **Date** | 2026-09-01 |
| **Relates to** | ADR-030 (does *not* trip its reopen trigger — see Reasons), ADR-031 (same "nothing depends on it" constraint) |

**Context**

Stéphane Letz (GRAME) asked, on [Losera/incant-audio#26](https://github.com/Losera/incant-audio/issues/26),
to see the corpus + harness behind our result that faust-rs diagnostics *lower* the
repair-loop success rate on a small local model. He is an external collaborator and will
want to re-run it, likely against a larger model of his own.

The scripts and data are already committed (`bench/repair_ab_repro/`). `verify.py` re-derives every
headline number from committed data with only `scipy` — no model, no compiler, ~1 s. The
reproduction barrier is the **two compilers** for a live replay: Faust (C++) for arm A and
the compile gate, and faust-rs (Rust) for arms B/C. Building both from scratch on an
arbitrary machine is the step most likely to stop someone.

CLAUDE.md / AGENTS.md §12 are explicit that Docker is not introduced without a demonstrated
need, and COLLABORATION.md §2 trigger 4 gates "distribution". This ADR is the consult.

**Decision**

Add **one** Dockerfile, `bench/repair_ab_repro/Dockerfile`, that builds a pinned environment for
reproducing issue #26 and nothing else:

- `archlinux:base-devel` base (the machine the corpus was built on; `faust` in the Arch
  repo is currently the pinned 2.85.9), `faust-rs` built from source at the `0.8.0` tag,
  `python` + `scipy` + `matplotlib`, and a copy of the `bench/repair_ab_repro/` package plus the
  shared modules it imports.
- The **LLM is not in the image.** Models are multi-GB; the harness points at a host
  ollama or any OpenAI-compatible endpoint at run time.
- Entrypoint dispatches `verify` / `rederive` / `replay` / `score` / `shell`.

**It is wired into nothing** — not `tools/check.sh`, not CI, not `host/CMakeLists.txt`, not
the product build. No repo test, hook, or skill may depend on it (the same rule ADR-031 put
on the Obsidian vault). The project remains un-containerised.

**Alternatives considered**

1. **Documentation only — point at the committed files + a `requirements.txt`.** Cheapest,
   and kept as the primary path regardless: `verify.py` needs only scipy, and README
   steps 1–3 do not need the container. The container is an *additional* on-ramp for the
   compiler-toolchain half, not the only one.
2. **A GitHub Release tarball with a build script.** Same build barrier, plus a second
   artifact to keep in sync with the repo. A Release of the *pointer* (README + commit
   pin) is still worth doing; a Release bundling a build script is strictly worse than a
   Dockerfile.
3. **Bake a small model into the image.** Rejected: even a 3B is ~2 GB, it fixes the model
   choice (defeating the point of an external re-run), and it drags in an ollama runtime.
4. **Nix flake instead of Docker.** Cleaner pinning, but Stéphane's audience is more likely
   to have Docker than Nix, and the repo has no Nix precedent — same §12 cost, smaller
   reach.

**Reasons**

- The result is **published on a public issue**; a claim that cannot be independently
  re-run is weak. Lowering the re-run cost for the compiler-toolchain half is proportionate
  to that. If it were only developer convenience it would not clear §12.
- Blast radius is a file delete: `bench/repair_ab_repro/Dockerfile` + `docker-entrypoint.sh` + the
  `docker*` Makefile targets. No schema, no build, no CI, no dependency-manifest change.
- **Does not trip ADR-030's reopen trigger.** That trigger is about the *offline generation
  pipeline* growing a multi-node topology (e.g. an FRS-code-branching repair advisor). This
  container ships a study and its replay harness; it adds no node to `generate_json()` and
  touches no product code.

**Consequences**

- First container in the repo. A reader who finds it must not conclude the project
  containerises — the Dockerfile header and this ADR both say so explicitly.
- **Arch base is not bit-reproducible** — `pacman -Syu` floats; the image is pinned only as
  tightly as Arch's current `faust`. For a hard pin we would build Faust from source at a
  tag, ~tripling image build time and size for a front-end-only use. If Stéphane needs
  bit-exact Faust, the source-build path is a follow-up, not v1.
- `cargo install --git --tag` at build time can break; the `FAUST_RS_REF` build arg lets a
  re-runner pin a different ref. `verify.py` — the load-bearing path — touches neither
  compiler, so a broken image never blocks reproducing the numbers, only the `replay` /
  `rederive` convenience.
- If a second reproduction container is ever proposed, that is the trigger to reconsider
  whether the project should have a `docker/` story at all. This ADR is deliberately scoped
  to exactly one.

**Accepted 2026-09-01 by explicit user decision** (AskUserQuestion — "Accept — keep the
container"). The draft `bench/repair_ab_repro/ADR-034-draft-repro-container.md` is superseded by
this entry and deleted.

---

## ADR-035 — Per-plugin generated faces: UiIr schema 3 + a scoped face renderer

| | |
|---|---|
| **Status** | **Accepted — 2026-09-03, by explicit user decision.** Step 1 (schema 3 + state persistence) landed 2026-09-03 (PR #51). Steps 2–6 authorized as direction; each still lands on its own change report + Tier-2 / semantic-diff review — Step 3 (a second `LookAndFeel`) especially. |
| **Date** | 2026-09-02 (accepted 2026-09-03) |
| **Relates to** | ADR-024 (extends its versioned `UiIr` schema), ADR-029 (schema 2 / components — same deterministic post-compile pattern), ADR-022 §3 (per-generation accent — narrowed, not reopened), ADR-019 (no WebView — unchanged), ADR-036 (the shell redesign around these faces — shares `ArchetypeLayout.h`) |

**Context**
Every generated patch renders with one shell look: `ForgeLookAndFeel` + Ember Console
tokens + `ParamGridPanel`'s sqrt grid or one-control-per-row sectioned list. ADR-022 §3
added a per-generation accent (one of four Ember swatches); ADR-024 and ADR-029 added
sectioned layout and a component descriptor — all deterministic, post-compile, reusing
facts the compiler already produced. What is still missing: a generated plugin that
looks like *its own product* — its own palette, type, knob style and archetype layout —
while the host chrome around it stays Ember Console.

An external design handoff supplies the target: `design_handoff_generated_plugin_faces/`
at the repo root — a README with a six-step plan and design tokens, a draft schema-3
generation prompt (`ui_ir_system_prompt.md`), and four high-fidelity reference faces as
`.dc.html` (Velvet Drift / Iron Strip / Echo Plate / Dustfield), one per worked example.
The bundle is kept **untracked** by explicit decision (2026-09-02): it is reference
material, not code, and a reader works from it on disk.

**Decision**
Six independently-shippable steps, each extending the ADR-024/029 mechanism rather than
adding a parallel one:

1. **UiIr schema 3** — schema 2 plus a `theme` block: colour strings
   (`surface/panel/line/text/textDim/accent/accentAlt`) and enums
   (`display/readout/knob/density`). Every field defaults to its Ember Console token, so
   schema 0/1/2 layouts are byte-unaffected. `parse()` degrades a missing/blank colour or
   an unrecognised enum to the default token **per field**, never rejecting the layout.
   The IR is persisted in the state blob as a `uiIr` root attribute — **a v3 state-blob
   amendment, not a `kStateSchemaVersion` bump**, by the same argument the `uiStyle`
   attribute used: an old blob simply lacks it and parses to `UiIr::empty()`, the
   un-themed state every patch already had. Nothing renders the restored IR yet.
   *Implemented — PR #51, commit `a8937b6`; green at `check.sh full`. New `UiIrTest`
   (41 checks) plus a `StatePersistenceTest` round-trip case.*

2. **Host-side theme validation** — a new `ThemeValidate.h` next to `Theme.h`, no JUCE
   `Component` dependency. Parse hex/rgba to `juce::Colour`, compute WCAG relative
   luminance with the formula `Theme.h` already documents, and enforce `text` on
   `surface` ≥ 7:1, `textDim` ≥ 4.5:1, `accent` ≥ 3:1, and `accent ≠ accentAlt ≠ text`.
   Failure is **per-token**: substitute the Ember token for the failing field and keep
   the rest. Never reject the whole face.

3. **A LookAndFeel per face** — a new header-only `GeneratedFaceLookAndFeel.h`, same
   convention as `ForgeLookAndFeel.h`, constructed from a validated `UiIr::Theme` and
   attached to `paramGridPanel` **only** (never `setDefaultLookAndFeel` — the reason
   `ForgeLookAndFeel.h`'s header already gives), so the title band, prompt column, sample
   browser and keyboard keep the shell look. Fonts are a fixed embedded set dispatched by
   name in `getTypefaceForFont`, exactly as `ForgeLookAndFeel` does today for its five;
   `theme.display`/`theme.readout` are enums over that set.

4. **Archetype layouts** — a new `ArchetypeLayout.h` of free functions, no JUCE
   dependency, mirroring `ParamGridLayout.h`. `synth-panel`/`channel-strip` lay sections
   out as columns; `tape-unit` splits transport/tone; `texture-field` is a display region
   plus a control rail; `pedal`/`utility` keep the existing grid. One source of truth for
   content height. This step also writes `host/tests/ParamGridLayoutTest.cpp`, which
   `ParamGridLayout.h`'s own header notes has never existed.

5. **The generation call** — a new metadata-to-metadata action in `llm/generate.py`,
   made after a successful compile. Input: the captured param table + the user's prompt +
   `isInstrument`. Output: the IR JSON. Validated host-side (schema in range, every
   writable param named exactly once, no continuous style on a `Button`/`CheckButton`, no
   `Kind::Meter` as a control, theme passes Step 2). Any failure →
   `deriveLayoutFromGroups()`, which stays the floor. Cheap and optional — a failed or
   slow second call must never delay the DSP going live.

6. **Verification loop** — drive the four worked-example prompts through
   `tools/ui_iterate.sh` and diff the contact sheet against the reference faces; extend
   the dev-cockpit export with archetype + theme so a run is checkable without eyes; add
   `EditorSessionTest` scenarios (theme applied, theme rejected per-token, IR restored
   from a reopened project, archetype layout placed every control).

**Reasons**
- Extends ADR-024's versioned schema and ADR-029's deterministic post-compile pass
  instead of introducing a second descriptor mechanism next to `UiIr::Layout`.
- Per-token degradation at every layer (parse, validation) — one bad string never costs
  a whole valid layout. This is ADR-022 §3's `GeneratedAccent` bone-swatch lesson applied
  structurally rather than case by case.
- The scoped LookAndFeel keeps the "one shell, many faces" separation: the chrome stays
  Ember Console, the generated panel becomes its own product.
- Step 5's LLM call is additive and non-blocking; the heuristic `deriveLayoutFromGroups()`
  stays the floor, so a provider outage or a malformed IR degrades to today's behaviour —
  not to a broken face, and never to bad audio.

**Consequences**
- **Narrows ADR-022 §3's `derivePalette()` to the no-IR path.** A schema-3 IR with a
  validated theme drives the panel's colours; the single-accent heuristic remains the
  fallback when there is no IR. ADR-022 §3 is not reopened, only scoped.
- **Step 3 is new UI architecture** — a second `LookAndFeel` instance, lifetime-coupled to
  `paramGridPanel`, plus an embedded font set through the `PluginForgeAssets` target. It
  does not land on this ADR's acceptance alone: it goes through a change report and the
  Tier-2 evidence bar / semantic diff review (AGENTS.md §4, COLLABORATION.md §2).
- **The design bundle is untracked.** A clean checkout has no
  `design_handoff_generated_plugin_faces/`; the four `.dc.html` faces are Step 6's
  contact-sheet targets, and losing the bundle loses that check's reference. Committing it
  (or a distilled `docs/` version) before Step 6 is worth considering.
- `PF-052` (`Kind::Meter` has no pool slot) remains a prerequisite for a meter component
  actually rendering — unchanged, not closed here.
- `theme.density` sits inside the `theme` block (per the handoff's JSON shape) though it
  is a Step-4 layout concern, not a Step-2/3 one. Minor; recorded so it is not read as a
  bug.
- Revisit if: the LLM cannot produce a contrast-valid, complete IR within the prompt
  budget (Step 5 falls back permanently; Steps 2–4 still stand on heuristic themes), or an
  archetype layout proves worse than the flat grid on a real corpus patch — the same
  failure mode ADR-024's `<4 controls` suppression threshold was added to catch.

**Status note**
Step 1 landed ahead of this ADR's acceptance because it is inert: a schema field and a
persisted attribute with no consumer, verifiable in full by `check.sh` with no design
judgement involved. Steps 2–6 were **Proposed** — this entry authorized the direction for
the human to accept or redirect, not the implementation.

**Status note (2026-09-03 — accepted)**
Accepted as direction by explicit user decision. Two grounding updates from the refreshed
design bundle (zip 2, 2026-09-03):

- **`GENERATION_PLAN.md` supersedes the Decision section's 1–6 step numbering.** It
  re-grounds the work against `main` into five dependency-ordered gaps and moves the
  **producer earliest** (`llm/ui_face.py` + a `ui_face` action in `generate.py`), because
  the host currently ignores its output (`ParamGridPanel::applyUiIr` reads
  `archetype`/`tokens`/`components`/`sections` but not `ir.theme`) — it is the cheapest,
  least risky piece. Build order:
  1. `host/Source/ThemeValidate.h` — WCAG contrast, per-token Ember fallback. Pure, no UI.
  2. `llm/ui_face.py` + a `ui_face` action — modelled on `llm/recommendation.py`. Host
     still ignores the output.
  3. `host/Source/GeneratedFaceLookAndFeel.h` wired into `applyUiIr` — faces get their
     colours; layout unchanged.
  4. `host/Source/ArchetypeLayout.h` + `host/tests/ParamGridLayoutTest.cpp` — faces get
     their geometry; the sectioned list becomes archetype columns.
  5. Cache the face in the state blob keyed on the source hash; extend the cockpit export
     with archetype + theme; `tools/ui_iterate.sh` contact sheet vs the committed
     `screenshots/`.

- **The "design bundle is untracked" consequence is resolved.** A distilled copy lives at
  `docs/design/incant-ui/` (README, GENERATION_PLAN, `ui_ir_system_prompt.md`,
  `github.md` screen-map, the nine `screenshots/*.png`, and the `.dc.html`
  source-of-record files). It is a **dated, read-only point-in-time record** per
  COLLABORATION.md §8 — superseded by a new dated bundle, never edited in place.
  `support.js` (the browser harness) is not committed. The full working bundle no longer
  lives at the repo root.

- **Open detail for gap 1:** `Theme.h`'s contrast table measures against `background`
  `#050505` (`Theme.h:47-53`: textPrimary 17.94:1, textSecondary 5.43:1, accent 6.09:1,
  progress 11.19:1), while `GENERATION_PLAN.md` / the bundle README say "against
  `surface`". Pin which reference colour `ThemeValidate.h` uses before implementing it;
  the measured numbers are the fixture either way, and the historical bone `#f5f0e6`
  accent (`Theme.h`'s RESOLVED RISK note) is the negative case.

The multi-session build order and per-gap traps are written up in
`docs/sessions/018-incant-ui-faces-and-shell.md`.

---

## ADR-036 — Incant shell redesign: prototype two directions, keep a code/compile region

| | |
|---|---|
| **Status** | Proposed (2026-09-03). **Amended 2026-09-04: direction picked (2a) ahead of the real-build prototype; see amendment below — §1/§2 superseded.** |
| **Date** | 2026-09-03 |
| **Relates to** | ADR-035 (shares `ArchetypeLayout.h` and the single content-height function), ADR-024 / ADR-029 (`UiIr` — `span` drives the lattice), ADR-022 §3 (Ember tokens — unchanged), ADR-019 (no WebView — unchanged), `docs/ui_design_plan.md` (prior UI-layout analysis), `docs/sessions/010-alpha-ui-architecture.md`, `docs/sessions/019-architecture-review-pipeline-and-ui.md` (the amendment's review) |

**Context**

`PluginEditor` splits its width at a fixed `kLeftFraction = 0.65` (`PluginEditor.h:384`).
At `kMinWindowW = 700` the right column computes to ~232px, and `PromptPanel`'s own
control row drops widgets to 0px — `Chrome::promptH`'s comment (`PluginEditor.cpp:678-692`)
records `refineSelector` already hitting 0px at the **900px default**, not only the
minimum. The prompt is on the width budget and collides with itself.

Separately, the user wants the param grid to read as a panel rather than a half-width
list, to be genuinely resize-aware, and to stop objects colliding — the same
`layoutSectioned()` one-control-per-row problem ADR-035 gap 4 addresses.

The refreshed design bundle carries two worked directions for the chrome, both on the
**unchanged** Ember Console tokens (`Theme.h`), in `Incant Audio Shell.dc.html` with 2×
screenshots (`docs/design/incant-ui/screenshots/`):

- **2a Command Bar** (`2a-command-bar-1160.png`) — the prompt becomes a full-width bottom
  bar; the grid owns everything above it as a hairline lattice
  (`repeat(auto-fit, minmax(180px,1fr))`, shared 1px `#383838` borders, section headers
  spanning `1/-1` with an accent rank number); a `UiIr` `span: 2` section renders a
  double-width cell with a 62px knob. Title-bar disclosures become fixed 30px squares so
  the **title** truncates under pressure, not the buttons.
- **2b Rail + Dock** (`2b-rail-dock-1160.png`) — a 52px left mode rail replaces the
  disclosure cluster; the prompt column becomes a user-dragged dock (persisted width,
  280px floor, 40% ceiling — not `kLeftFraction`); one 26px status bar absorbs the meter,
  the status line and the sample-browser line.
- **Both at 700×500** (`2c-both-at-700-minimum.png`) — 2a → 3 columns, family/refine
  behind `⋯`, Generate keeps its full hit target, grid scrolls with a visible 8px thumb;
  2b → dock auto-collapses to a 30px tab, grid takes the width back.

Both take the prompt off the width budget, so no band depends on a percentage.

**Decision**

1. **Prototype both directions and pick from the real build.** A shared prep commit does
   the direction-neutral work: extract `ArchetypeLayout.h` (the same free functions
   ADR-035 gap 4 introduces — the lattice's width-driven column count wants them), keep
   `contentHeightForSections()` as the single height function that both
   `layoutSectioned()` and `contentHeightForCurrentMode()` call (`ParamGridPanel.h:51-57`),
   and make the disclosure buttons fixed 30px squares (a strict improvement, valid under
   either direction). Then **two task branches off that base** —
   `feat/shell-command-bar`, `feat/shell-rail-dock` — each a full `resized()`
   implementation. Render both through `tools/ui_iterate.sh`, evaluate on the running
   Standalone, **pick one, merge it, delete the other branch.** No dead layout ships
   behind a preference flag.

2. **2a keeps a dedicated right-hand code/compile region.** `Incant Audio Shell.dc.html`
   left this open ("decide where a 20-line Faust error goes in 2a before building it").
   Resolved: the bottom command bar takes only the **prompt** off the width budget; a
   right-side region hosts `CodeEditorPanel`, showing the attempted Faust source with the
   highlighted error line (`PluginEditor.cpp:210-211`) and the full stderr (today also
   routed to `PromptPanel::setError`). Today `codeEditorPanel` is a hidden bottom-dock
   child (`PluginEditor.cpp:103, 733-735`); 2a promotes it to a first-class right column —
   persistent or toggled via the `{ }` title-bar square, decided in the prototype. 2b
   already has a persistent right dock, so this constraint is 2a-specific.

3. **Ember tokens are unchanged.** This is a `Component::resized()` change, not a repaint.
   The bundle's R1/R2 recreation (`screenshots/shell-effect-900x500.png`,
   `shell-instrument-900x786.png`) is the before-picture and the "did not move the chrome"
   regression check.

**Alternatives considered**

1. **Just make the divider draggable with a minimum right-column width (2b-lite).** The
   smallest fix for the collision. Kept as the **floor** — if both full directions stall
   on the real build, ship this. Rejected as the *only* change because it does not deliver
   the width-driven lattice or the resize-awareness the user asked for.
2. **Ship 2a-only or 2b-only without prototyping.** Rejected — the user's explicit call is
   to judge on the running build, and the two differ most in ways a screenshot understates
   (drag feel, a rail vs. a bottom bar under a DAW's own chrome).
3. **Carry both layouts permanently behind a user preference.** Rejected — a little-used
   layout rots undetected (CLAUDE.md's signature defect) and doubles the `resized()`
   maintenance surface for every future panel.
4. **A WebView shell.** Rejected — ADR-019 stands, unreopened.

**Reasons**

- The prep commit's deliverables (`ArchetypeLayout.h`, the single height function, the
  30px buttons) are needed by ADR-035 gap 4 regardless of which shell direction wins, so
  the two ADRs converge on one piece of code rather than forking it.
- Prototyping on branches, not behind a flag, keeps exactly one shell in the tree at all
  times and makes the loser's deletion a `git branch -D`, not a refactor.
- Keeping `CodeEditorPanel` first-class in 2a preserves the iterate-on-the-Faust
  workflow, which is the product's core loop — the prompt bar is for starting over, the
  code panel is for fixing what compiled wrong.

**Consequences**

- **This is new UI architecture.** The `resized()` rewrite, the sqrt-grid formula
  (`cols = clamp(ceil(sqrt(N)), 2, 6)`) giving way to a width-driven column count, and the
  `CodeEditorPanel` promotion each land on their own change report + Tier-2 /
  semantic-diff review — ADR acceptance authorizes the direction only (AGENTS.md §4).
- **Sequenced after ADR-035 gap 4**, or the prep commit lands `ArchetypeLayout.h` first
  and both tracks consume it. The lattice is not built twice.
- `kMinWindowW` / `kMinWindowH` may change — 2c is drawn at 700×500, today's minimum is
  700×400 (`PluginEditor.h:417, 424`). A `setResizeLimits` change is Tier-2, not §2-gated.
- The persisted dock width (2b) is a new state-blob attribute — additive, same policy as
  `uiStyle` / `uiIr`, no `kStateSchemaVersion` bump.
- A `setDefaultLookAndFeel` denylist guard for `host/Source/` becomes worth adding once
  ADR-035 gap 3 lands (hand to `invariant-hook-writer` then, not before).
- Revisit if: neither direction beats the draggable-divider floor on the real build, or
  the lattice proves worse than the sectioned columns on a real corpus patch.

---

**Amendment (2026-09-04): pick made ahead of the real-build prototype — 2a, with the
error region changed from a right column to a bottom sheet**

Made during an architecture-review session that read all nine bundle screenshots
(`docs/design/incant-ui/screenshots/`) directly, not only this ADR's prose description
of them, alongside ADR-019/021/022/024/027/029/030/033/035 and the DSP-pipeline question
that prompted the review (see `docs/sessions/019-architecture-review-pipeline-and-ui.md`
for the full review). This amendment supersedes **Decision §1** (prototype-both,
pick-later) and **Decision §2** (2a's error region is a persistent right column) below.
§3 (Ember tokens unchanged) is untouched.

**§1 superseded — the pick is 2a, not a build-both prototype.** Reasons, weighed against
the two directions' own worked screenshots rather than against each other in the
abstract:

1. **2a fully removes the width tax; 2b re-imposes a smaller version of it.**
   `PluginEditor.h:411`'s `kLeftFraction = 0.65` is this ADR's own stated defect, and
   `Chrome::promptH`'s comment (cited above) records the collision at the **900px
   default**, not only `kMinWindowW`. 2a takes the prompt off the width budget entirely.
   2b's dock has a **280px floor** (this ADR's own Context section) — a permanent
   right-hand tax at exactly the width where the grid is starved, i.e. the same defect
   shape at a smaller constant.
2. **One layout engine, not two.** 2a's lattice
   (`repeat(auto-fit, minmax(180px,1fr))`, a width-driven column count) is the same
   mechanism `ArchetypeLayout.h` (ADR-035 gap 4) already has to build. 2b's dock-plus-rail
   does not reuse it — it is a second, independent layout concern (persisted width,
   floor/ceiling, collapse-to-tab) with its own state and its own edge cases at 2c's
   700×500 minimum.
3. **2b's rail introduces modality the product's core loop does not want.** The 52px mode
   rail (`{ }` / `◎` / `⋯` / `...`, per `2b-rail-dock-1160.png`) implies code, samples and
   the grid become mutually exclusive views. Generate → read error → fix source is one
   continuous loop today (`PromptPanel::setError` → `CodeEditorPanel::highlightErrorLine`,
   `PluginEditor.cpp:210-211`); a rail that hides the grid to show the error breaks the
   "see the mismatch, fix it" motion this ADR's own §2 reasoning already values.

Building both branches per §1's original process (`feat/shell-command-bar`,
`feat/shell-rail-dock`) is not authorized under this amendment. §1's shared prep commit
(`ArchetypeLayout.h`, the single height function, 30px disclosure buttons) is unaffected
and still lands first, per ADR-035 gap 4's own dependency.

**§2 superseded — the error/code region is a bottom sheet, not a persistent right
column.** §2's own text left this "decided in the prototype"; the prototype step is now
skipped, so the choice is made here instead, on the same reasoning §2 already used against
carrying dead layout: a persistent right column re-imports exactly the width tax point 1
above rejects 2b for, only inside 2a. The error is also **transient and temporally bound**
to the prompt that produced it — unlike the code-inspection workflow §2 correctly wants to
preserve, an error does not need to occupy screen space between generations.

Resolution: `CodeEditorPanel` becomes a sheet that expands the bottom command bar
**upward**, anchored to the prompt row that triggered it (collapsed height: 0, costing the
grid nothing when idle). It opens automatically on a compile failure — same trigger
`PromptPanel::setError` already fires — and stays user-togglable via the `{ }` title-bar
square for the read-only "inspect the accepted source" case §2's reasoning is about. This
keeps §2's core value (the iterate-on-Faust workflow stays first-class) while keeping
ADR-036's own stated principle — "no band depends on a percentage" — true for the error
region as well as the prompt.

**Consequences of this amendment**
- The two `feat/shell-*` branches named in §1 are replaced by one implementation branch,
  `feat/shell-command-bar`, built directly against this amended decision. No branch is cut
  for 2b.
- §1's "Alternatives considered" #1 (draggable-divider floor) and #3 (carry both behind a
  flag) are unaffected — #1 remains the fallback if 2a stalls on the real build; #3 remains
  rejected.
- The bottom-sheet error region is new geometry not in any bundle screenshot or `.dc.html`
  file. It has no pixel-value source of record the way the rest of 2a does — the
  implementing session sizes it against `CodeEditorPanel`'s existing content and states its
  chosen dimensions in the change report, since there is no design reference to cite.
- Everything in the original ADR's **Consequences** section stands unchanged (new UI
  architecture, Tier-2 change-report obligation, `ArchetypeLayout.h` sequencing,
  `kMinWindowW`/`kMinWindowH` revisit, the persisted-dock-width note — which now applies to
  nothing, since 2b is not built — and the `setDefaultLookAndFeel` denylist note).
- Revisit if: the bottom-sheet error region proves worse in the real build than a
  persistent region would have — this amendment is itself a judgment made from
  screenshots, not from the running Standalone, and ADR-036's original point (judge on the
  real build) is not overruled in principle, only sequenced later for this one element.

---

## ADR-037 — Offline generation evaluation: persist the accepted design plan, score it after the fact

| | |
|---|---|
| **Status** | Proposed (2026-09-04) |
| **Date** | 2026-09-04 |
| **Relates to** | ADR-021 (named the unmet need — "acceptance criteria"), ADR-027 (declined a *live* critique gate; this ADR does not reopen it), ADR-033 (`recommendation.py` — the acceptance-criteria source), ADR-030 (orchestration tripwire; this is item 2 of 3) |

**Context**

ADR-021 rejected a structured `PluginSpec` on 0/19-corpus evidence and named the actual
remaining gap explicitly: *"the remaining unmet need is acceptance criteria — capturing
what a generation was asked for so the result can be checked against it. That is a
different artifact from a structural spec and is tracked separately."* It has sat
untracked since 2026-08-04.

ADR-027 separately declined to add a live critique/refine gate to the generation path —
correctly, on grounds that still hold: no measured judge reliability, added latency and
free-tier quota on every generation, and the risk of manufacturing false confidence that
erodes the listening pass COLLABORATION.md §1 protects (*"whether a generated plugin
sounds like what was asked for... is not delegable to a hook or a model"*). ADR-027 §1's
decision stands and this ADR does not touch it — everything here runs **offline**, with no
live-path consumer.

What has changed since both ADRs: `llm/recommendation.py` (ADR-033, accepted, opt-in)
already produces the artifact ADR-021 asked for and never built separately. Its typed,
capped output — title, 1–5 ordered modules, 1–12 controls
(`llm/recommendation.py:15-17`'s `MAX_MODULES`/`MAX_CONTROLS`), `schema: 1`
(`llm/recommendation.py:133`) — **is** a per-generation acceptance-criteria record; the
`recommend` panel workflow just discards it after the user accepts and generation runs.
Separately, `bench/render_oracle.py`'s `measure()` already produces objective per-render
facts (NaN/Inf, silence, DC, runaway gain, peak/RMS) for every corpus entry, offline, at
zero cost — this is exactly the substrate an evaluation stage needs and none of it is
wired to any acceptance record today.

STATUS.md's "assumed, never checked" metric cannot move on documentation; it moves only
on evidence. This ADR is one instance of that metric's own design working as intended —
proposed because it closes a named, dated gap with a mechanism already proven out
(`recommendation.py`, `render_oracle.py`), not because a new capability was invented to
fill a slot in an external pipeline diagram.

**Decision**

1. **Persist the accepted plan.** When a `recommend` round is accepted and the following
   `generate` call succeeds, write the accepted `Recommendation` object into the state
   blob as a new root attribute, `acceptancePlan` — same additive policy `uiIr` already
   established (`PluginProcessor.cpp:772,798`: a new root attribute, no
   `kStateSchemaVersion` bump, an old blob simply lacks it). Scope: **only** the opt-in
   `recommend` → `generate` path (ADR-033 condition 1) writes it; the default `generate`
   path leaves it absent, same as today.
2. **A new offline evaluation entry point**, `bench/evaluate_against_plan.py`, run
   explicitly (a `check.sh` level or a standalone invocation — implementer's call, not
   wired into any live request path). Input: a corpus of `(faustSource, acceptancePlan)`
   pairs, sourced either from real saved state blobs or from a benchmark run that exercises
   the `recommend` path. Output per entry: `render_oracle.measure()`'s objective facts,
   plus a **structural** check against the plan (every planned control name resolvable in
   the compiled `ParamInfo` table; no unplanned control silently absent) — no semantic
   judgment of whether the audio matches the plan's *intent*, which stays the deferred,
   harder problem ADR-027 already declined to automate.
3. **No live-path change of any kind.** No new field in the `generate` request/response
   contract, no new `reason` code, no timing dependency added to `generate_json()`. A
   provider outage or a malformed plan affects nothing this ADR touches, because nothing
   this ADR touches runs while a user is waiting on a result.

**Alternatives considered**

1. **A live critique gate, reopening ADR-027 §1.** Rejected — no new evidence exists
   against ADR-027's reasoning; nothing here changes the free-tier-quota or
   judge-reliability facts that reasoning rests on. ADR-027's own reopen trigger
   (unattended batch generation becoming a real product goal) has not fired.
2. **A new LLM-authored acceptance-criteria artifact, separate from `recommendation.py`.**
   Rejected — `recommendation.py` already exists, is typed, capped, tested (44 Python
   tests + 2 `EditorSessionTest` scenarios per ADR-033), and is exactly what ADR-021 asked
   for. Building a second artifact next to it would repeat ADR-029's discard-problem
   pattern in reverse: inventing new output before checking what already exists goes
   unused.
3. **Score every generation, not only the opt-in `recommend` path.** Rejected for this
   ADR — the default `generate` path has no acceptance record to score against by
   ADR-033's own design (condition 1: review is opt-in). Scoring the default path would
   need a different acceptance-criteria source (e.g. the raw prompt text as a weak proxy)
   and is a separate, smaller-evidence proposal if it is ever wanted.

**Reasons**

- Closes ADR-021's named gap using an artifact that already exists and is already tested,
  rather than adding a new one.
- Strictly additive to the state blob, matching `uiIr`'s precedent exactly — no schema
  version bump, no migration.
- Zero live-path risk: the evaluation stage has no caller on the request/response path
  `generate_json()` serves, so it cannot introduce a new live failure mode, latency, or
  quota cost.
- Moves STATUS.md's `assumed` metric on a real claim (does the generated result satisfy
  its own accepted plan, structurally) rather than by writing documentation about the
  claim.

**Consequences**

- `PluginProcessor.h`/`.cpp` gain a new persisted field and its round-trip test, mirroring
  `UiIrTest`'s pattern for `uiIr`.
- `bench/evaluate_against_plan.py` is new surface with its own test coverage; it depends on
  `render_oracle.py`'s existing `measure()` and does not modify it.
- **ADR-030 tripwire.** This is the second of the three items ADR-030's revisit clause
  names (an offline critic/decompose pass). If the issue-#26 `faust-rs` error-code advisor
  (the first item) also lands, ADR-030's own re-evaluation clause activates — against a
  hand-rolled dispatch table, per its explicit instruction, not against an orchestration
  framework by default. Recording this now so the count is not rediscovered cold.
- The harder problem ADR-021 also left open — semantic fidelity, not structural
  completeness — remains unsolved and is not claimed to be solved by this ADR. This is a
  structural-completeness checker, not a judge of whether the plan's intent was honored.
- Revisit if: the structural check's false-positive/false-negative rate, once measured
  against a real corpus, turns out too noisy to be worth the state-blob field — mirrors the
  caution ADR-027 §1 already applied to a harder version of this same class of tool.

**Unverified until implementation.** No code accompanies this proposal. The concrete
`acceptancePlan` JSON shape, the `bench/evaluate_against_plan.py` CLI surface, and which
`check.sh` level (if any) runs it by default are implementation-session decisions, not
settled here.
