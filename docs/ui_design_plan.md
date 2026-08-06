# PluginForge UI: AI-Centered Design Plan

Status: DRAFT for human review (DELEGATE). Companion doc: `docs/ux_roadmap.md` (phased
delivery of the same ideas). Nothing here is committed work; each actionable item carries
an engagement-mode tag per COLLABORATION.md.

---

## 1. Current state

### Factual inventory of the editor

| Element | Detail | Where |
|---|---|---|
| Window | Fixed 480×410, `setSize()` in constructor; no resize support | `host/Source/PluginEditor.cpp:7` |
| Prompt box | Single-line `juce::TextEditor`, no multiline, placeholder "Describe your plugin..." | `PluginEditor.cpp:9-13` |
| Generate button | Spawns `generate.py --prompt` on a detached `std::thread` via `juce::ChildProcess`; disabled during run; 120s timeout + kill | `PluginEditor.cpp:48-205` (timeout `:124-135`) |
| Status label | Single `juce::Label`, one-line state machine: "Ready." → "Generating..." → "JIT compiling: <40 chars>..." → "Ready — DSP live, N params mapped" / error strings | `PluginEditor.cpp:58,201-202,263-267` |
| Compile-error surfacing | `onFaustCompileError` → status label, error truncated to 200 chars | `PluginEditor.cpp:240-249`, hook declared `PluginProcessor.h:43` |
| Compile-success surfacing | `onFaustCompileSuccess` (wired 2026-07-19, ADR-011 point E) → "Ready" text + `refreshParamKnobs()` | `PluginEditor.cpp:254-270`, hook `PluginProcessor.h:60` |
| Level meter | 30Hz timer polls `processor.outputLevel` (relaxed atomic), decaying peak, gradient fill | `PluginEditor.cpp:307-315` (tick), `:325-341` (paint); atomic `PluginProcessor.h:49` |
| Param knobs | 8 rotary sliders (`MAX_KNOBS = 8`) in a fixed 4×2 grid, bound once to slots `macro_0..7` via `SliderAttachment`; hidden until compile, then labelled/shown by `refreshParamKnobs()` | `PluginEditor.h:36-41`, `PluginEditor.cpp:211-228,273-303,356-368` |
| Param pool | 64 slots (`POOL_SIZE = 64`), shared `slotId()` scheme | `host/Source/ParamPool.h:14,28` |

### Gap list

**Persistence**
- `getStateInformation` / `setStateInformation` are empty stubs (`PluginProcessor.h:30-31`)
  — nothing survives a DAW project reload.
- The processor does not retain the current Faust source or the originating prompt;
  `loadFaustCode()` hands the string straight to `faustEngine.compile()` and drops it
  (`PluginProcessor.cpp:74-91`). No session resume of any kind.

**Iteration UX**
- No view of the generated Faust code anywhere in the UI (the status label shows the
  first 40 chars during compile, `PluginEditor.cpp:201-202`).
- No "refine" path: every Generate is a from-scratch one-shot; no prompt history,
  no A/B against the previous compile, no presets/snapshots.

**AI-feedback UX**
- The retry loop (up to 3 attempts inside `generate.py`, `llm/generate.py:69`) is
  invisible — the UI shows "Generating..." for up to 120s with no attempt counter.
- All feedback funnels through one truncated single-line label (200-char cap on
  compile errors, `PluginEditor.cpp:246`); multi-line Faust stderr is unreadable.
- No API-key UX: a missing `ANTHROPIC_API_KEY` surfaces as a Python traceback caught by
  the "no JSON in output" branch (`PluginEditor.cpp:155-165`) — accurate but hostile.
- No model selection UI, even though `generate_json()` already accepts a `model` key
  (`llm/generate.py:92`). Ties to ADR-008 (Under evaluation) — see `docs/ux_roadmap.md`
  Phase 4.

**UI surface**
- Only 8 of 64 pool slots get knobs; params 9+ compile and map (`pushToFaust` iterates
  all active slots) but are invisible in the editor — reachable only via DAW automation
  lanes. The Standalone build has no automation lanes at all, so slots 9+ are
  uncontrollable there (noted in `docs/prototype_test_plan.md` step 5).
- Fixed window size cannot adapt to param count (a 2-param gain and a 20-param synth
  get the same 4×2 grid).

---

## 2. Design-type taxonomy

This section doubles as the README-able description of the plugin design types
PluginForge targets. Four types:

| | Generator | Effect | Utility | Hybrid |
|---|---|---|---|---|
| **What** | Synth / instrument; produces sound | Insert processor; transforms input | Gain, metering, routing helpers | Generative effect (e.g. granular, shimmer) |
| **Typical param count** | 12–32 | 4–16 | 1–4 | 10–24 |
| **Natural UI paradigm** | Grouped sections (OSC / FILTER / ENV / FX) | Auto-grid knobs, one row per stage | Minimal custom panel (slider + meter) | Grouped sections + a custom visual panel |
| **Metering needs** | Output level, voice activity | In/out level, gain reduction where relevant | The meter IS the UI | Output level + activity/texture display |
| **64-slot ParamPool mapping** | Heaviest user; grouping metadata matters most | Comfortable fit; flat grid usually fine | Nearly empty pool; hide unused surface | Mid-heavy; benefits from sectioning |
| **Example prompts** | "an 80s analog-style synth pad with two detuned saws" | "a warm low-pass filter with a cutoff knob"; "an aggressive distortion with drive and output level" | "a stereo gain trim with a peak meter" | "a granular cloud texture from the input with density and pitch spray" |

### UI paradigms observed in the wild

The "Natural UI paradigm" row above was written from first principles. The P10 survey (§4)
found four paradigms actually in use, and the fourth was not in our vocabulary:

| Paradigm | What it is | Relevance to PluginForge |
|---|---|---|
| Generic editor | `GenericAudioProcessorEditor`, zero layout code | **Observed in 0 of 19** fixed-param repos — not a credible floor |
| Custom `LookAndFeel` | Hand-laid components, restyled JUCE widgets | The common case; what §3 auto-layout approximates |
| Webview | HTML/JS UI hosted in the plugin | Out of scope (we compile `JUCE_WEB_BROWSER=0`) |
| **Declarative / GUI-Magic** | Layout declared as XML/data, no hand-written `PluginEditor.cpp` | **Architecturally closest to our own plan** — see below |

The declarative paradigm (2 of 21 entries) is worth naming because PluginForge is already
heading there by a different route: an auto-layout driven by `ParamCapture` metadata *is* a
declarative layout, with the compiled DSP's parameter table standing in for the XML. The
proposed LLM-emitted layout hints (§3) would make that explicit. Prior art worth reading
before drafting the auto-layout rather than inventing the data model from scratch.

Notes:
- The current UI serves only the Effect/Utility bands well (≤8 params). Generator and
  Hybrid overflow `MAX_KNOBS` immediately — the strongest argument for auto-layout (§3).
- Faust itself distinguishes generators (`process = osc...`) from effects (consumes
  inputs); the input/output count of the compiled DSP is a cheap runtime signal of type.

### Feeding the taxonomy back into generation

- Runtime side (DELEGATE-able): infer type from compiled DSP I/O counts + param count and
  pick a layout paradigm accordingly — pure host-side logic, no prompt changes.
- Generation side — PROPOSED (HUMAN-OWNED — requires human authoring): tell the LLM which
  design type it is producing (or ask it to declare one) and/or emit Faust `[group:...]`
  metadata for UI grouping. Any such change touches `llm/prompts/system_prompt.txt`,
  which is HUMAN-OWNED product IP per COLLABORATION.md; wording is the human's.
  ParamCapture already sees Faust group structure via `openHorizontalBox`/`closeBox`
  callbacks it currently ignores (`FaustEngine.cpp:7-33` implements only the widget
  adders) — capturing groups is host-side and DELEGATE/PAIR, not prompt work.

---

## 3. AI-centered design loop

Goal: the UI is *derived from* the generated DSP, not fixed ahead of it.

**What we already have at compile time.** `ParamCapture` (a minimal Faust `UI` subclass,
`FaustEngine.cpp:7-33`) records `{label, defaultValue, min, max, step}` per widget into
`FaustEngine::ParamInfo` (`FaustEngine.h:15-22`). Widget kind (hslider / vslider /
nentry / button / checkbox) is known at capture time — the callbacks are distinct — but is
NOT currently retained in `ParamInfo`. Retaining it is a one-field addition (DELEGATE,
with a PAIR check that remap/pushToFaust are unaffected).

**Proposed post-pass (LLM, optional).** After a successful compile, send the captured
param metadata (labels, ranges, widget kinds, groups) to the LLM in a *separate, cheap
call* that returns a layout hint JSON: section grouping, ordering, control kind
(knob / slider / toggle — buttons and checkboxes must not render as rotaries), and a
suggested window size band. This is metadata-to-metadata; it never touches the DSP, so a
bad answer degrades to the fallback layout, never to bad audio. Needs a new mode in
`llm/generate.py` (DELEGATE) and a new prompt file — PROPOSED (HUMAN-OWNED — requires
human authoring) for the prompt text itself.

**Deterministic fallback: auto-layout rules (no LLM).** Pure grid math from N params:
- cols = clamp(ceil(sqrt(N)), 2, 6); rows = ceil(N / cols)
- window height = header (status/prompt/meter, ~170px today per `resized()`,
  `PluginEditor.cpp:344-368`) + rows × cellH (95px today, `PluginEditor.cpp:360`)
- toggle-kind widgets render as `juce::ToggleButton` instead of rotary
- N > ~24: sections become tabs or a scrollable viewport

> **Correction, 2026-07-30 (PF-039). Rotary is not the fallback widget, and never renders.**
> This section, and the table at `:22`, describe a rotary default that the shipped code does
> not have. `FaustEngine::Kind` has exactly five values and
> `ParamGridPanel::applyPresentation` handles all five explicitly (HSlider → horizontal,
> VSlider → vertical, NumEntry → inc/dec, Button/CheckButton → ToggleButton), so the
> `default:` rotary arm is **unreachable for any real Faust parameter**. `EditorSessionTest`
> scenario 2 asserts it: no control ever reports `WidgetKind::Rotary`.
> The `MAX_KNOBS = 8` fixed 4×2 grid in the `:22` table is likewise gone — PF-005 replaced it
> with the sqrt-derived scrolling grid described above. Both are kept rather than deleted
> because they record what the design *proposed*; neither describes what ships.

**Insertion point.** `refreshParamKnobs()` (`PluginEditor.cpp:273-303`) is already the
single place the UI reacts to a compile — it receives the full `ParamList` on the message
thread via `onFaustCompileSuccess`. Auto-layout replaces its fixed `MAX_KNOBS` loop and
the fixed grid in `resized()`; the `SliderAttachment`-per-slot pattern
(`PluginEditor.cpp:219-222`) generalizes unchanged. All message-thread work — no audio
or compile-thread interaction. Engagement mode: PAIR for the first dynamic-layout draft
(new pattern in the codebase), DELEGATE thereafter.

---

## 4. P10 — Ecosystem survey spec — **EXECUTED 2026-07-20**

**Results: the P10 survey** (21 repos, 3 parallel research agents; the source doc was retired
2026-07-27 once its conclusions were absorbed here — `git log -- docs/juce_plugin_survey.md`
has the full text). The spec below is retained as the executed design; the findings supersede
the assumptions it was written to test. Headline: **zero of 19 fixed-param entries used a bare
`GenericAudioProcessorEditor`**, even at 1–2 params — so the complexity-ladder question
posed under "Deliverable" resolved to *nobody ships GenericEditor at any param count*.
That supports keeping the §3 auto-layout as the UI floor rather than falling back to a
generic editor. The survey also surfaced a 4th UI paradigm now folded into §2.

The original spec, as executed:

**Scope.** Survey open-source JUCE plugins and Faust-based plugins.
- Seeds: `github.com/sudara/awesome-juce`; GitHub topics `juce-plugin`, `vst3`, `faust`;
  the faust2juce ecosystem; `grame-cncm/faust` examples tree.

**Data table schema (one row per repo):**

| Field | Notes |
|---|---|
| name / URL / stars / license | identity + adoptability |
| plugin type | per §2 taxonomy (generator / effect / utility / hybrid) |
| approx param count | from APVTS layout or UI inspection |
| UI paradigm | GenericEditor / custom LookAndFeel / webview |
| complexity proxy | LOC, module count |
| JUCE / Faust version | compat signal |

**Deliverable:** a survey doc — the table plus a "complexity ladder" summary: how UI
sophistication actually scales with param count and project size in the wild (does anyone
ship GenericEditor past ~10 params? where do custom panels start?). *(Delivered as
`docs/juce_plugin_survey.md`; retired 2026-07-27, conclusions absorbed above.)*

**Execution estimate:** 2–3 parallel research agents (split by seed list), read-only.
Engagement mode: DELEGATE (research/documentation; human reviews the survey doc).
**Outcome:** ran 2026-07-20 with 3 agents; the survey landed with the full table (since
retired — see Results above). Human review of the survey doc is still outstanding.
