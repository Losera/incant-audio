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
2026-07-16 without a decision being recorded first — `docs/decisions_reconstructed.md` flags
Decision [011] as the last Open item. This ADR ratifies (rather than re-litigates) the shipped
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
