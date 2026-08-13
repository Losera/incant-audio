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
| Locating `generate.py` from the installed binary | **Closed 2026-07-19** — upward search from the executable (dev layouts) + `PLUGINFORGE_LLM_SCRIPT` env override (installed layouts); old sibling-path guess never matched any real layout |
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
| **Status** | Proposed |
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
existing ~124-token headroom budget.

**Reasons**
- Sectioned layout is the first step toward visual identity without leaving native widgets
- Renderer-agnostic: a WebView can consume the same IR when one arrives (ADR-019)
- Schema versioning prevents silent breakage across LLM output generations
- The "append unmentioned controls" invariant preserves zero-IR backward compatibility

**Consequences**
- `applyUiIr()` is currently unreachable at runtime (no callers in `onFaustCompileSuccess`);
  wiring it is Track 1.2 of the build order
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
