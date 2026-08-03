# Session 001 — Playground shell: parameter identity, MIDI, code editor

**Status: in progress.** Parameter identity and the note-queue half of MIDI are landed;
the code editor and the shell layout are not.

**This file is the living record for this session — keep editing it, do not fork a new one.**
It was `docs/session_plan_2026-08-02.md` until the naming rule landed
(COLLABORATION.md, "Naming documents"): that name was written on one day, planned for a
second, and executed across a third, so it was wrong for two of the three days it mattered.

Two things below have different lifetimes, and the difference is deliberate:

- **The execution record** (this section) is current and gets updated as work lands.
- **The plan text** from "## 1. Current-state report" down is **frozen exactly as written**.
  Every claim in it was read at HEAD `d587665`. Several are now false — that is the point.
  Rewriting them would destroy the only evidence of what changed, and a corrected plan that
  no longer shows its own errors is worth less than an honest stale one. Where it is wrong,
  the execution record says so.

Detailed task breakdown for the remaining work:
`~/.claude/plans/2-pluginforge-playground-cozy-hummingbird.md`.

---

## Execution record

### Landed and pushed

| Commit | What |
|---|---|
| `e7d7c20` | **§4 Tasks 1–4**: `ParamIdentity.h`, identity-keyed `remap()` + slot-reuse policy, state blob `schemaVersion 2` (`<ParamMap>` + `idScheme`, v1 falls back to positional), forced-Fresh boundary hack retired, `hbargraph`/`vbargraph` captured as `Kind::Meter`. PF-051 closed, PF-052 filed (capture closed, **rendering still absent**). |
| `ca34955` | `tools/screenshot_ui.sh` matched on window *title*, so a terminal sitting in `~/PluginForge` was captured instead of the plugin — with a success line and a plausible geometry. Matches on class now. |
| `a04c9e2` | **W3.1**: `NoteRing.h` — SPSC lock-free note queue, message thread → audio thread. Two test targets (ASan + TSan), both wired into `check.sh` and CI in the same commit. |
| `496c35e` | **W3.2**: processor owns the ring; `processBlock` drains it inline in the `enterAudio()` bracket. `OfflineRenderTest` 158 → 179 checks. |
| `e0cd9a9` | **Document naming rule + `check_doc_naming.py`**, and this file renamed out of its date. COLLABORATION.md §7 table row and a "Naming documents" section; 13 new control-wiring assertions. |

`tools/check.sh full` green — 17 rungs, then 19 once the two NoteRing rungs landed.

**Writing the W3.2 tests found two bugs in W3.2 itself**, both worth remembering:
the drain was inside the `isInstrument()` gate (so an effect patch never emptied
the queue — 545 dropped, and the next instrument would have received the backlog as
one burst), and `NoteRing::reset()` documented a `prepareToPlay` caller that must not
exist, because JUCE serialises `prepareToPlay` against `processBlock` but not against
the message thread.

Also recorded, because it is the recurring shape here: in the no-drain red case the
*release* assertion still reported OK, since a tail of ~0 is trivially under 1% of a
held level of ~0. Only the "SOUNDS" check had teeth. Same trap as the `en.ar` finding
on 2026-08-01.

### Left to do — the playground shell

Scope decided this session: **minimal shell**, `PluginForgeSynth` Standalone as the demo
target, dirty-flag (not a revision ledger) for source-of-truth. The full dock/tabs/splitter/
zoom shell, the Controls tab with CC binding, and the theme pass are all explicitly deferred.

- ~~**W3.2** processor owns the ring; `processBlock` drains it~~ — **done, `496c35e`.**
- **W3.3** `MidiKeyboardComponent` + `MidiKeyboardState` in the editor; QWERTY via
  `setKeyPressBaseOctave`; disable with a one-line reason when `isInstrument()` is false;
  reset the keyboard state on compile success.
- **W3.4** red case: recompile while a note is held (acceptance test 8 — safe by mechanism, covered by **no test today**).
- **W2.1** probe libfaust's `error_msg` format — **still unverified, blocks W2.5.**
- **W2.2–2.6** editable editor, Ctrl+Enter recompile, `showSource` clobber guard, error strip, dirty-flag confirm.
- **W1.1–1.2** keyboard band in `resized()` + `chromeHeight` assert update; four-state status indicator.

### What was learned

**Three of the brief's premises were stale, and correcting them is most of why this is a
one-night job.** Denormalization is *fixed* (`ParamMap.h:83-131`), the MIDI note path *exists
and is complete* (`PluginProcessor.cpp:188-220`, `FaustEngine.cpp:300-326` — including mtof,
gate-written-last and mono last-note-priority), and the code editor is *not a stub*
(`CodeEditorPanel.h:56-57` already holds a `CodeDocument` and a `CodeEditorComponent`). Two
more items the brief asked for were already built: parameter preservation across recompile is
the `ParamIdentity` work, and "one compile in flight" is an existing single-slot supersede.
**`CLAUDE.md:142` still calls the code editor a stub — that line is now wrong and should be
fixed.**

**`tools/check.sh full` prints `PASS` for tests whose target failed to build.** Observed
directly: `JitTargetTest` reported PASS against a `libpf_cpu_shim.so` dated 2026-07-30 while
its own source failed to compile. Ninja stops, ctest runs whatever binaries are already on
disk, and the summary prints green underneath a `FAIL` line. **This is the project's signature
defect — a control reporting success about something other than what you think — and it is
still open.** Worth a PF row: ctest should depend on a successful build.

**A date in a filename is a claim, and it goes stale like any other.** This file was
`session_plan_2026-08-02.md`: written on one day, planned for a second, executed across a
third. `docs/test_plan_today_2026-07-21.md` is wrong twice in one name. The rule is now in
COLLABORATION.md ("Naming documents") and enforced by `check_doc_naming.py` — living
documents get `docs/sessions/NNN-topic.md`, point-in-time records keep their dates because
there the date is the *content*. The distinction that matters is not "does the name contain
a date" but **"will this document be edited again."**

**And the hook proved the point about red cases immediately.** One of its six blocking cases
did not block when first written: the relative-day-word pattern anchored on a separator or
end-of-string, and `.md` is neither — so `test_plan_today.md` sailed straight through while
`test_plan_today_2026` was caught. Reading the regex would not have found it; running it did.
Numbering starts at `001` and earlier sessions are deliberately **not** renumbered — `git log`
is their record, and inventing numbers for them would be inventing history.

**Drop-oldest is not implementable in an SPSC ring**, and the plan asked for it. Freeing the
oldest slot means advancing the *consumer's* cursor from the *producer*, which makes both
indices multi-writer. The queue drops the newest instead and counts it; the musical
consequence (a dropped note-off leaves a note sounding) is bounded by the drain running every
block, by monophonic note-stealing, and by `allNotesOff()` staying reachable.

**Two hazards found in the existing editor that the W2 work must handle**, neither previously
recorded:
1. `PluginEditor.cpp:104` calls `showSource()` unconditionally on every compile success, and
   `showSource` calls `loadContent` — which resets caret and scroll. The moment the editor
   becomes writable, **the user's own recompile throws away their cursor position.**
2. A superseded compile **never fires its callback** (`FaustEngine.cpp:566-582`) — the pending
   `cb` is destroyed unfired. A naive status indicator sticks on "compiling" forever when you
   mash the recompile key.

**The Standalone already gives a MIDI-input device picker for free** —
`StandalonePluginHolder` builds an `AudioDeviceSelectorComponent` with
`showMidiInputOptions = true` (`juce_StandaloneFilterWindow.h:538-543`), persisted across
relaunch. Building one into a top bar would be duplicated work that only helps the VST3, where
the DAW owns devices anyway. That deleted a chunk of W1.

**Open, unverified, and blocking:** libfaust's in-process `error_msg` format.
`llvm-dsp.h:229` documents it only as "the error string to be filled"; the **CLI** format is
evidenced (`docs/benchmark_analysis.md:55`) but the CLI will not read stdin, so this cannot be
settled by reading and must be probed. Error-line highlighting — the highest-value item in W2 —
depends entirely on the answer.

**Stale artifacts noticed, not yet fixed:** `host/tests/ui_fixtures/reference_manifest.json`
holds 15 records but a sixth fixture now exists, so the next `ui_iterate.sh` produces 18;
`UiDesignGallery.cpp:78` hardcodes `editor.setSize(480, 460)`, so a taller shell changes every
snapshot. Expect a `--accept` rebaseline rather than a clean diff.

---

## 1. Current-state report (Phase 0)

### 1.1 How Faust UI metadata becomes JUCE controls

The whole harvest is one class: `ParamCapture`, a `Faust::UI` subclass, `FaustEngine.cpp:11-177`.
It is instantiated once per successful compile (`FaustEngine.cpp:647-648`) and its output
(`FaustEngine::ParamList`) is the *only* description of the patch that anything downstream sees.

**Honored today:**

| Faust construct | Where captured | Where used |
|---|---|---|
| `hslider` / `vslider` / `nentry` / `button` / `checkbox` | `FaustEngine.cpp:122-149` → `ParamInfo::kind` | widget choice, `ParamGridPanel::applyPresentation` |
| `[scale:log]` / `[scale:exp]` | `FaustEngine.cpp:84-85` → `ParamInfo::scale` | `ParamMap::curveFor`, `ParamMap.h:53-57` |
| `[unit:Hz]` etc. | `FaustEngine.cpp:87-90` → `ParamInfo::unit` | curve default (`ParamMap.h:65-66`), display text (`ParamMap.h:260`) |
| `[style:menu{…}]` / `[style:radio{…}]` | `FaustEngine.cpp:91-95` → `ParamInfo::isMenu` (bool only) | discreteness, `ParamMap.h:41-42` |
| `hgroup` / `vgroup` / `tgroup` nesting | `FaustEngine.cpp:164-176` → `ParamInfo::group`, slash-joined, outermost box dropped | **nothing** — see below |
| `min`/`max`/`init`/`step` | the `add*` callbacks | `ParamMap` denormalization both directions |

**Silently dropped — verified, each one:**

1. **`hbargraph` / `vbargraph` are empty function bodies** (`FaustEngine.cpp:150-151`).
   A patch that publishes a meter, a gain-reduction readout, or an envelope follower
   produces **nothing** — the widget never enters `ParamList`, so no meter can exist.
   The request's "visible metering where the DSP exposes `hbargraph`" is currently
   impossible at the capture layer, not the UI layer.
2. **`[style:knob]` is read and discarded.** `FaustEngine.cpp:94` tests the style string
   only for the `menu`/`radio` prefixes; `knob`, `led`, `numerical` fall through and set
   `pendingIsMenu = false`. The patch's own presentation request is thrown away.
3. **Menu entry *names* are discarded** — only the `isMenu` flag survives.
   `ParamMap.h:252-257` documents the consequence: a mode selector can display `2`,
   never `Sawtooth`.
4. **`addSoundfile` is an empty body** (`FaustEngine.cpp:152`).
5. **File-level `declare name/author/version/…` is never read.** The `declare()` override
   (`FaustEngine.cpp:66-96`) handles per-zone metadata only; `dsp::metadata()` is never
   called anywhere in the tree. There is no plugin title, no author, no version.
6. **`[tooltip:]`, `[hidden:]` — not captured.** No tooltip surface exists.

**The group path is captured and used by nothing.** `ParamInfo::group` landed 2026-07-31
(`3ed3dbc`) and its only reader in the whole tree is a test accessor,
`ParamGridPanel::controlGroupForTest` (`ParamGridPanel.cpp:406`). Layout is a flat
sqrt-derived grid — `cols = clamp(ceil(sqrt(N)), 2, 6)` (`ParamGridLayout.h:24-30`) — inside
a scrolling `Viewport`. Section structure exists in the data and not on the screen.
STATUS.md's "Works" section states this plainly at `STATUS.md:113`.

**Control style today** is a global three-way cycle, not per-control:
`Faithful` / `Rotary` / `Horizontal` (`ParamGridPanel.h:78-84`), persisted in the state blob
as `uiStyle` (`PluginProcessor.cpp:416`, default `"faithful"`). It is the *only* visual
choice a user can make, it applies to every control at once, and it is the existing proof
that a visual change can be made without touching the DSP (`ParamGridPanel.h:66-73`:
attachments survive a style flip).

### 1.2 The 64-slot macro pool

- **Size and IDs.** `ParamPool::POOL_SIZE = 64` (`ParamPool.h:14`). IDs are `macro_0…macro_63`
  via the single shared scheme `ParamPool::slotId()` (`ParamPool.h:28`).
- **Declared once**, in `PluginProcessor::createParameterLayout()` (`PluginProcessor.cpp:36-70`),
  as `AudioParameterFloat` with an **explicit** `NormalisableRange<float>(0,1)` — explicit
  because the convenience ctor hardcodes `interval 0.01` and quantised every slot to 100
  positions (PF-040, argued in the comment at `PluginProcessor.cpp:48-60`).
- **Host-facing display name is `"Macro 1"…"Macro 64"`** (`PluginProcessor.cpp:63`) and cannot
  be renamed after init — `ParamPool.cpp:40-42` records why. So the DAW automation lane says
  `Macro 7` while the plugin window says `Cutoff`. That is STATUS.md Broken #6.
- **`remap()` is positional.** `ParamPool.cpp:33-53`: `params[i]` → slot `i`, unconditionally.
  Slots past the patch's param count get a null-zone sentinel (`:47-52`).
- **Params beyond 64 are silently dropped.** The loop caps at `POOL_SIZE` and `pushToFaust`
  iterates `min(infos.size(), slots.size())` (`ParamPool.cpp:83`). A 70-param patch compiles,
  publishes 70 zones, and six of them are never written and never displayed, with no error
  anywhere. **This is not in `docs/BUGS.md` — new finding, filed below as PF-051.**
- **Thread safety.** Double-buffered; `remap()` writes the inactive buffer and publishes with
  a release store, `pushToFaust()` acquire-loads once per block (`ParamPool.cpp:28-58`,
  `:80-81`). Single-writer is guaranteed by `compileMutex` being held across the whole compile
  lambda.
- **Conversion is centralized** in `ParamMap.h` — `mapSlotToZone` (`:83-131`),
  `mapZoneToSlot` (`:135-178`), `formatZone` (`:239-261`), `parseZone` (`:266-291`).
  This is the one place the project already solved a "four faces of one problem" bug, and
  the header comment (`ParamMap.h:12-16`) says so explicitly. **It is the template for §3.**

### 1.3 The hot-swap path — what is replaced, what survives

Seven-step protocol, `FaustEngine.cpp:~668-745`, rationale in `docs/fixplan_pushtofaust_swap.md`.

**Atomically replaced:** `activeDSP` (step 3), the voice-control zone pointers and
`voiceValid` (step 3a), `dspNumIns`/`dspNumOuts` (step 3b), `activeUI` MapUI (step 4),
the LLVM factory and — via the `cb()` call at step 5 — `ParamPool`'s info buffer and the
processor's `currentLabels`. All published together behind one release store on `ready`
(step 6). The old DSP and factory are freed at step 7, after `ready=true`, off the audio thread.

**Survives a swap:**
- **Macro slot values**, but *only by index*, and only in `LoadMode::Iterate`
  (`PluginProcessor.cpp:311-313`). `LoadMode::Fresh` resets every mapped slot to the new
  patch's declared default (`resetMappedSlotsToDefaults`, `PluginProcessor.cpp:225-258`).
- **Faust source + originating prompt**, committed only on compile success under `metaMutex`
  (`PluginProcessor.cpp:320-327`) — PF-022's fix.
- **`uiStyle`** (`PluginProcessor.cpp:416-435`), which is the existing proof that a
  presentation choice can outlive a DSP swap.

**Does not survive, because it does not exist:**
- **Presets.** `getNumPrograms() { return 1; }`, `setCurrentProgram` empty,
  `getProgramName` returns `{}` (`PluginProcessor.h:86-90`). The only occurrence of the word
  "preset" in `host/Source/` is a future-tense comment (`PluginProcessor.cpp:378`).
  **There is no preset bank, no snapshot, no A/B.**
- **Host automation *meaning*.** The IDs `macro_N` are stable, so a DAW lane never
  disconnects — but what slot 7 *controls* changes with every regeneration. The plugin
  already knows this is wrong: crossing the instrument/effect boundary force-resets to
  `Fresh` (`PluginProcessor.cpp:304-313`) precisely because an index shift would land a
  cutoff value on a pitch control, and **that comment names label-keyed remap as the real
  fix and calls the current behaviour a mitigation.**
- **MIDI arriving during a swap is dropped** (`PluginProcessor.cpp:151-160`), deliberately
  and with the reasoning written down.

**Persisted-state format (v1)** — `PluginProcessor.cpp:374-416`: a `ValueTree` → XML blob with
root `PluginForgeState`, attributes `schemaVersion`, `faustSource`, `prompt`, `uiStyle`, and a
`<STATE>` child holding the APVTS tree verbatim. The DSP is never serialized; restore
recompiles the source in `Iterate` mode (`PluginProcessor.cpp:96-100`).

### 1.4 Where the instrument/effect distinction lives

In **three independent places**, none of which is authoritative:

1. **Runtime, from the compiled DSP.** `extractVoiceControls` (`FaustEngine.cpp:194+`) mirrors
   `dsp_voice::extractPaths` (`/usr/include/faust/dsp/poly-dsp.h:233-254`) — a patch is an
   instrument iff it declares all three of `gate`, `freq`(or `key`), `gain`(or `vel`),
   matched exactly and case-sensitively (`FaustEngine.h:100-129`). Exposed as
   `isInstrument()` (`FaustEngine.h:133`). This drives: the MIDI walk
   (`PluginProcessor.cpp:171`), withholding the three voice controls from the published
   param list (`FaustEngine.cpp:657-659`), the forced-Fresh boundary check, and the
   OutputGuard runaway policy (ADR-020, `PluginProcessor.cpp:342-351`).
2. **Generation-side.** `llm/router.py` — deterministic keyword scoring, no LLM call, with
   the rationale for both in its module docstring (`router.py:4-27`). Chooses between
   `system_prompt.txt` (12,006 chars) and `instrument_prompt.txt` (7,784 chars). Returned to
   the host as an additive `kind` field (`generate.py:315-318`) — **and only on the success
   path; `_failure()` does not carry it** (`generate.py:310-313`).
3. **Build time.** `PF_IS_SYNTH` / `JucePlugin_IsSynth` → two CMake targets,
   `PluginForgeHost` (Fx) and `PluginForgeSynth` (Instrument), differing in exactly four
   traits (`host/CMakeLists.txt:28-140`). Drives `acceptsMidi()` (`PluginProcessor.h:75`),
   the optional input bus (`PluginProcessor.cpp:123-124`), and `getTailLengthSeconds()`
   (`PluginProcessor.h:84`).

**It is nowhere in a persisted IR.** The state blob has no `kind`. On project reload the
classification is re-derived from the recompiled DSP. That happens to work, but it means
there is no record of what the user *asked for* — only of what the model *produced*.

### 1.5 JUCE, formats, CMake

- **JUCE 7.0.9** — `/home/losera/JUCE/modules/juce_core/system/juce_StandardHeader.h:30-32`.
  ⚠️ **CLAUDE.md is wrong here**: it says "JUCE vendored at `host/JUCE`". `host/JUCE` is a
  CMake *build output* directory; the actual checkout is `$HOME/JUCE`
  (`host/CMakeLists.txt:5`). Minor, but it is the exact drift class this repo keeps paying for.
- **Formats: VST3 + Standalone only**, on both plugin targets (`host/CMakeLists.txt:31`, `:109`).
  No AU, AUv3, CLAP, LV2 or AAX.
- **`COPY_PLUGIN_AFTER_BUILD FALSE`** on both (`:39`, `:117`) — the VST3 has never been
  installed to a scan path.
- **`JUCE_WEB_BROWSER=0` / `JUCE_USE_CURL=0` on every target** (`:61-65`, `:127-131`, and
  every test target), load-bearing: the comment at `:48-56` records that the defaults of 1
  broke the build, and that Arch ships webkit2gtk-4.1 where JUCE 7 looks for 4.0.
- **WebView parameter relay does not exist in this JUCE.** `WebBrowserComponent::Options`
  at `juce_WebBrowserComponent.h:52-120` offers exactly `withBackend`,
  `withKeepPageLoadedWhenBrowserIsHidden`, `withUserAgent`, and WinWebView2 sub-options.
  There is no `withNativeIntegration`, no `WebSliderRelay`, no
  `WebControlParameterIndexReceiver` — those are JUCE 8.0.1+. **A WebView UI on this tree
  means a JUCE major-version upgrade or a hand-written bridge.** This is new information
  ADR-019 did not have.
- **Twelve build targets** in `host/CMakeLists.txt`, two of them plugins. JUCE recompiles its
  modules per target; the file's own comment (`:92-93`) calls this out as the cost of the
  second plugin target.

### 1.6 Where the review findings live, and their status

| Artifact | Contents |
|---|---|
| `docs/BUGS.md` | The durable registry, PF-001…PF-050, with the "a row flips to fixed only after someone reads the code at HEAD" rule (`:23-25`) |
| `docs/decisions.md` | ADR-001…006, 007-009, 011, 012, 019, 020 (477 lines) |
| `docs/architectural_decisions/*.md` | ADR-007, 008, 009, 011 as individual files + a README index |
| `docs/architecture_review_2026-07-21.md` | The original adversarial review, full pipeline |
| `STATUS.md` | The live top-N view, rewritten each session |

**The four findings named in the request are all closed:**

| Finding | ID | Status | Evidence |
|---|---|---|---|
| Parameter denormalization in the macro pool | PF-001 | **fixed** | `efbb5a5`; `ParamMap.h` is the fix, `ParamPool.cpp:96` the call site |
| Detached-thread teardown races | PF-003 (compile), PF-006 (generate) | **fixed** | `d10f59e`, `18e862e`; `PromptPanelThreadingTest` is the red case |
| Benchmark / product prompt drift | PF-007 | **fixed** | prompt-unify 2026-07-21; one prompt file since |
| State-persistence gaps | PF-002 | **fixed** | `c34bbb6`; `StatePersistenceTest` round-trips the blob |

**Still open and relevant to this plan:** PF-024 (generation defects, in-progress),
PF-032 (noise gate renders silent, in-progress), PF-038 (knob ordering is Faust's
alphabetical path order), PF-039 (dead rotary arm + a design doc that describes it as live),
PF-041/PF-042 (the judge is not interpretable), PF-045 (`en.*` seconds-vs-ms).
PF-011, PF-013, PF-035, PF-043 are benchmark/evidence items, independent of A and B.

### 1.7 What I could not determine

1. **The "intelligence-per-request audit" output is not in this repository.**
   `grep -rl "intelligence.per.request"` across all `*.md` returns nothing. Either it lives
   outside the repo, or it is called something else. **I need the artifact or its name
   before Workstream C can consolidate it** — see Open Question 1.
2. **Whether JUCE 7.0.9 can build an LV2 wrapper.** I did not verify the `FORMATS LV2`
   path against this checkout. Marked as needs-check rather than asserted either way.
3. **`pluginval`'s actual coverage.** Commit `a451350` (2026-07-30) records "pluginval
   passes at strictness 10" against the **Release VST3 of the Fx target**, with the
   Steinberg sub-test skipped. `which pluginval` now returns nothing — the binary was
   fetched ad hoc and is not on PATH. **The instrument target has never been validated.**
   STATUS.md:154 says the same.
4. **How the current UI actually looks.** `artifacts/ui_gallery/` exists and
   `UiDesignGallery` renders the real editor against five fixtures, but I have not viewed
   the PNGs. The request's premise that the UIs are ugly is taken as given, not verified
   by eye — and per CLAUDE.md that judgment is not delegable to me anyway.

---

## 2. Decision register

Reversibility cost is stated as *what it would take to undo after one session's work*.

### D1 — Generated UI: compiled component tree vs. declarative IR

**Options.** (a) The model emits JUCE `Component` layout code, compiled into the binary.
(b) The model emits a UI IR; a fixed native renderer in the plugin draws it.
(c) The model emits HTML/CSS; a WebView renders it.

**Recommendation: (b), a UI IR rendered by a fixed native engine.**

**Reasoning.** Option (a) is not a trade-off in this architecture — it is infeasible.
The plugin JITs *Faust* via libfaust/LLVM; there is no C++ toolchain in the loop and never
has been. Generating JUCE layout code would require shipping clang inside the plugin or
regenerating and rebuilding the binary per visual tweak, which violates constraint #1
outright: a retheme would be a full rebuild, not a partial iteration. The argument about
models being bad at pixel arithmetic is true and secondary; the architectural argument is
decisive on its own.

Option (b) satisfies both constraints directly. A UI IR is data: it swaps without touching
`activeDSP`, it diffs, it versions, it is editable by direct manipulation (drag a knob →
mutate a field → re-layout), and it is small — which the quota rule wants. The project is
already at the door: `uiStyle` is a persisted presentation field that survives a DSP swap
(`PluginProcessor.cpp:416`), and `docs/ui_design_plan.md:81-88` names the declarative
paradigm as "architecturally closest to our own plan."

**What would change my mind.** If a design review concluded that acceptable visual quality
requires per-plugin custom *drawing* rather than parameterized drawing — bespoke
illustration, not composed tokens. Then the IR would need an escape hatch (an embedded
vector-path field), not a different architecture.

**Reversibility: cheap.** The IR is additive — a new state-blob field and a new renderer
path alongside `ParamGridPanel`. Deleting it restores today's behaviour. **Not a one-way door.**

### D2 — WebView surface

**Recommendation: keep ADR-019 standing (native now), and record the newly-verified reason.**

**Reasoning.** ADR-019 declined WebView on build-dependency grounds. There is now a second,
larger obstacle it did not know about: **JUCE 7.0.9 has no WebView parameter-relay API**
(§1.5, verified at `juce_WebBrowserComponent.h:52-120`). Adopting WebView therefore costs a
JUCE 8 major upgrade across twelve targets, *plus* the webkit2gtk-4.1/4.0 mismatch on Arch,
*plus* a host-compatibility surface, *plus* a new RT-safe bridge — before a single knob
renders. The model-quality argument for HTML/CSS is real and I do not dispute it; it is
simply not worth that entry price when a token system plus parameterized vector controls
delivers most of the diversity for none of it.

**The hedge that makes this safe: design the UI IR renderer-agnostic.** No JUCE types in the
schema, no pixel coordinates the native renderer happens to use — geometry expressed as
grid cells and spans. Then a WebView renderer is a second backend reading the same document,
not a rewrite. That is the cheap option to keep open.

**What would change my mind.** A JUCE 8 upgrade landing for an unrelated reason (it would
remove the largest cost), or a listening/looking pass concluding the native renderer cannot
reach acceptable quality.

**If it were adopted**, the binding path would be JUCE 8's `WebSliderRelay` +
`WebSliderParameterAttachment` per macro slot, which keeps parameter writes on the message
thread and leaves `pushToFaust` untouched; hosts with a restricted webview would need the
native `ParamGridPanel` as a runtime fallback, selected on `WebBrowserComponent`
construction failure. Recording this so the option is specified, not so it is taken.

**Reversibility: the decision is free to reverse** (it authorizes no work, as ADR-019 itself
notes). The *hedge* — renderer-agnostic schema — is the thing that must be got right up
front, and it costs nothing to do so.

### D3 — How visual quality is produced

**Recommendation: design tokens + layout archetypes + parameterized vector controls.
The model chooses and composes; it never lays out from scratch.**

Three separate pieces, and the separation is the point:

1. **Token sets** — named, hand-authored, versioned in the repo as data. Fields: palette
   (bg, surface, surface-alt, accent, accent-alt, text, text-dim, track, indicator),
   typography (family class, scale ratio, weight pair), radius scale, spacing scale,
   shadow/depth rule, control geometry (knob arc span, indicator width, cap style).
   **Diversity comes from having ~12 good token sets and composing them**, not from the
   model inventing colour. Two plugins made the same day look unrelated because they drew
   different sets — a property of the catalogue, not of sampling temperature.
2. **Layout archetypes** — `channel-strip`, `modular-rack`, `synth-panel`, `pedal`,
   `dual-column`. Each is a fixed section grammar the renderer knows how to draw; the model
   picks one and fills its slots. `synth-panel` maps directly onto the `hgroup`/`vgroup`
   structure `ParamInfo::group` already captures and nothing yet reads (§1.1) — **so the
   first archetype is nearly free: the data is already there.**
3. **Control renderers** — `arc-knob`, `ring-knob`, `notched-knob`, `endless-encoder`,
   `h-slider`, `v-slider`, `toggle`, `segmented`, `meter`. Each is a function of
   (bounds, token set, normalized value, ParamInfo) drawing `juce::Path`s. **Adding a style
   = one function + one enum entry + one gallery fixture**, with `UiDesignGallery`
   (`host/CMakeLists.txt:563-599`) as the existing instrument for looking at it.

**Usability requirements ride the parameter model, not the renderer**: units and skew already
come from `ParamMap` (`curveFor`, `formatZone`); double-click-to-default needs `ParamInfo::
defaultValue`, which is captured; fine-drag is a `juce::Slider` modifier setting. The one
genuinely missing input is **metering**, which is blocked at the capture layer by the empty
bargraph bodies (`FaustEngine.cpp:150-151`) — that is a three-line fix plus a `ParamInfo`
kind, and it should not be conflated with UI work.

**Reversibility: cheap and incremental.** Token sets are data files. **Not a one-way door.**

### D4 — Instrument/effect classification: where it lives

**Recommendation: the IR is authoritative and persisted; the compiled DSP is the *verifier*,
not a second decider.**

Today's three deciders (§1.4) agree by luck. Make `kind` a persisted field written by the
router at generation, carried on **both** the success and failure paths of `generate_json`,
and stored in the state blob. At compile, `extractVoiceControls` still runs — but its job
becomes *agreement checking*: if the IR says instrument and the voice contract is absent,
that is a **named, user-visible, actionable state** ("this was meant to be playable and
isn't — regenerate"), not silence. Today it is silence: the patch simply never responds to
notes and `STATUS.md` Broken #1 is the result.

**Failure behaviour when classification is wrong**, decided explicitly:
- *Effect classified as instrument* → patch gets a gate, is silent until a note arrives.
  Detected by the disagreement check above; surfaced as a one-click "this is an effect" override.
- *Instrument classified as effect* → today's behaviour exactly (a patch reading the input
  channel). This is why `effect` remains the router default (`router.py:25-26`).
- The override writes `kind_source: "user"` and is **sticky across regeneration** — a user
  who has corrected a misroute must not have to correct it twice.

**Reversibility: cheap** (an additive field, same treatment as `reason` and `kind` already got
under PF-019). **Not a one-way door.**

### D5 — Polyphony

**Recommendation: do not build it tomorrow. Keep Phase 0 monophonic, and take the smaller
first slice.**

**Reasoning, and this is the "larger than it looks" flag the rules ask for.** Faust's own
machinery is present (`/usr/include/faust/dsp/poly-dsp.h`, `poly-llvm-dsp.h`, `MidiUI.h`,
and `dsp_poly_effect` at `poly-dsp.h:933-990` for the shared post-FX bus). Using it is
correct and I recommend it over hand-rolled voice allocation — but it **changes the type
that the swap protocol swaps**. Today `activeDSP` is `std::atomic<llvm_dsp*>` and step 7
does `delete old`. A `dsp_poly` is a different object with a different lifecycle, and the
zone pointers `ParamCapture` captures — the thing that makes `pushToFaust` RT-safe
(`FaustEngine.h:83-94`) — become aggregate-UI zones fanned out across voices. That touches
the single most carefully-reasoned component in the repo, the one whose ordering fixed
~1,100 errors. It is a session of its own, with its own TSan run, and it must not be
smuggled into a UI session.

**The smaller first slice** is what STATUS.md already sequences: an on-screen + computer
keyboard on a lock-free path, so a monophonic instrument becomes *audible*. Polyphony is
worth nothing until something can play a note at all.

**Reversibility: expensive if rushed.** Getting the poly lifecycle wrong reintroduces a
class of bug this project has already paid for twice. **This is the closest thing here to a
one-way door** — not because it can't be reverted, but because a race introduced into the
swap protocol is discovered late and diagnosed slowly.

### D6 — Mid-note DSP swap behaviour

**Recommendation: kill with a short declick fade.**

Today a swap is an abrupt discontinuity: the old DSP is deleted, `currentNote = -1`
(`FaustEngine.cpp` step 3a), and MIDI in the swap block is dropped
(`PluginProcessor.cpp:151-160`). Voice migration is out of the question — voice state lives
inside an LLVM-JIT'd object with no serializable representation. A fade is the only option
that is both correct and RT-safe: set a flag at swap step 1, have `OutputGuard` (already in
the path, already reset inside the compile callback at `PluginProcessor.cpp:340`) apply a
~5 ms ramp. No allocation, no lock, no new thread interaction.

**Reversibility: cheap.** **Not a one-way door.**

### D7 — MPE

**Recommendation: later, gated on polyphony.** Not "never" — the market expects it — but MPE
without per-note polyphony is not a partial implementation, it is a meaningless one. The
precondition is D5 landing. Stated so it is a decision and not a silence.

### D8 — Format matrix

**Recommendation: VST3 + Standalone now (Linux). CLAP next. AU/AUv3 blocked on hardware.
AAX never.**

| Format | Call | Why |
|---|---|---|
| VST3, Standalone | **now** — already built | `host/CMakeLists.txt:31`, `:109` |
| CLAP | **next**, after the parameter model | Cheapest addition: no codesigning, no notarization, and its parameter model is *closer* to what §3 wants (stable IDs, not ordinals) |
| AU / AUv3 | **deferred — hard-blocked** | The deciding variable is hardware: the dev box is Arch Linux only (CLAUDE.md, "Primary and only dev target"). `auval` is macOS-only and cannot run here at all |
| LV2 | **deferred** | Linux-native and cheap *if* JUCE 7.0.9 supports it — unverified (§1.7) |
| AAX | **never** | Requires an Avid NDA and PACE signing. Out of scope for a low-code studio product |

**Reversibility: cheap per format** (a `FORMATS` line and a validation pass), except AU,
which is blocked on acquiring a Mac.

### D9 — Parameter identity across regenerations

**Recommendation: a readable semantic slug, computed at capture, persisted, with an explicit
slot-reuse policy. This is the root-cause fix and it is first.**

Full contract in §5.1. Two sub-decisions worth stating:

- **Slug, not hash.** The request suggests hashing. I recommend a readable slug
  (`env/attack`, `filter/cutoff#2`) because this identity is **internal**: the host never
  sees it. VST3 sees `macro_N`, which must stay ordinal and stable forever — that is the
  entire point of the fixed pool (ADR-004). Since the identity's only consumers are the
  state blob, the UI IR and the CC map, readability wins outright: a human debugging a
  failed migration can read `filter/cutoff → macro_3` and cannot read `a3f9c1`.
  **What would change my mind:** if identities ever became host-visible or size-constrained.
- **Slot-reuse policy** (§5.1) must be decided *with* the ID scheme, not after. An ID scheme
  without a reuse policy just moves the ambiguity.

**Reversibility: this is the one-way door.** Once identities are written into saved user
projects, changing the slug derivation orphans every automation lane and every preset in
every saved session. **The derivation rule must be right on the first commit, and it must be
versioned in the blob so a future change can migrate rather than break.** That is why it is
task 1 and why it ships with the migration path, not after it.

---

## 3. Root-cause analysis

**Hypothesis under test:** *denormalization bugs, unstable parameter identity, MIDI CC
mapping and UI control binding are four faces of "there is no single authoritative parameter
model."*

**Verdict: it holds.** The evidence, and note that the strongest piece is a *confirmed
prediction*, not a restatement:

1. **The hypothesis has already been tested once and won.** PF-001 (denormalization) and
   PF-037 (the display showing raw slot numbers) were the same bug seen twice —
   `ParamMap.h:12-16` says so in the file's own words: *"Two halves of one conversion,
   implemented once each, disagreeing."* The fix was not two fixes; it was
   **centralization into one authority**. Both defects closed and neither has recurred.
2. **The remaining defects are the same shape.** `ParamPool::remap` is positional
   (`ParamPool.cpp:33-53`), so parameter identity *is* ordinal position.
   - PF-038 ("knobs appear alphabetically") is that fact seen from the UI.
   - Broken #6 ("the DAW still sees raw slots") is that fact seen from the host.
   - The forced-Fresh on instrument-boundary crossing (`PluginProcessor.cpp:304-313`) is a
     *workaround* for that fact — **and its own comment names label-keyed remap as the fix
     and calls itself a mitigation.** The codebase has already diagnosed this.
   - Silent truncation past 64 params (PF-051, new) is the same absence: nothing owns the
     question "which parameter is this."
3. **The unbuilt work needs the same thing.** A UI IR must name a control
   (`{"param": "filter/cutoff"}`); a MIDI CC map must name a target
   (`{"cc": 74, "param": "filter/cutoff"}`); a preset must name what it stores. All three
   need a stable name, none can be built on an ordinal, and if they are built on three
   *different* stable names the project has re-created the PF-001 shape at triple scale.

**Consequence for build order: the parameter model is built first, and everything else
sequences behind it.** Workstream A cannot define its IR's `param` field without it.
Workstream B cannot define CC mapping without it. Both would otherwise invent an identity
scheme independently — which is precisely the failure being diagnosed.

**Conflicts between the review's recommendations and the new designs — resolved:**

| Conflict | Resolution |
|---|---|
| ADR-019 says native widgets, no WebView; the product goal wants model-generated visual diversity | **ADR-019 wins**, on strengthened grounds (§1.5: JUCE 7.0.9 has no relay API — a fact ADR-019 did not have). The goal is met by tokens + archetypes, not by a browser |
| ADR-002 rejects a JSON IR between LLM and compiler; the UI IR is a JSON IR | **No conflict — different layer.** ADR-002 is about the *DSP* path: no JSON between prompt and Faust. The UI IR sits beside the DSP and never transpiles into it. A bad UI IR degrades to the fallback grid; it can never produce bad audio. Worth writing as an explicit ADR so nobody re-litigates it |
| ADR-004's fixed 64-slot pool vs. "stable parameter identity" | **No conflict — they compose.** The pool stays ordinal and host-facing forever; identity is an internal layer *above* it. The slug is the key, the slot is the storage |
| `ui_design_plan.md` §3 proposes an LLM post-pass returning layout-hint JSON | **Superseded in mechanism, kept in intent.** The UI IR *is* that hint, emitted in the same call rather than a second one — a second round trip does not fit the 100 s budget (`generate.py:73`) or the free-tier quota, the same argument `router.py:12-16` already made for not making routing an LLM call |
| `ui_design_plan.md` §3 describes a rotary fallback that does not exist (PF-039) | **The doc is wrong**; the correction is already inline at `ui_design_plan.md:107-118`. In the new renderer rotary becomes reachable *by choice* (a control style), which closes PF-039 by making the described behaviour real rather than by deleting it |

---

## 4. Tomorrow's sequenced plan

Sized against this project's observed cadence (one substantial feature + tests + STATUS
rewrite per session). **Cut line marked.** Every task states files, verification, dependency.

### Task 1 — `ParamIdentity`: derive and publish a stable ID *(the root-cause fix)*
- **Files:** `host/Source/FaustEngine.h` (add `id` to `ParamInfo`),
  `host/Source/FaustEngine.cpp` (`ParamCapture::consume`, derive from `group` + `label`).
- **Verify:** new `host/tests/ParamIdentityTest.cpp` — derivation, normalization,
  collision suffixing, empty-group case. Rung: `check.sh fast` (new ctest target) and
  `check.sh full`. **Red case first:** two params named `Gain` in different groups must not
  collide; two in the *same* group must get `#2`.
- **Depends on:** nothing. **This is the gate for everything below.**

### Task 2 — Identity-keyed remap + slot-reuse policy
- **Files:** `host/Source/ParamPool.h/.cpp` (`remap` takes the previous id→slot map;
  reuse, free, assign), `host/Source/PluginProcessor.cpp` (owns the map; drop the
  forced-Fresh boundary hack at `:304-313` once retention is identity-based).
- **Verify:** new `EditorSessionTest` scenario — load patch A, move a knob, load patch B
  which *reorders* and *renames* params, assert the surviving parameter kept its value **and
  its slot**, and that a removed param's slot reset to 0 rather than leaking into a newcomer.
  **Plus its negative:** a genuinely different patch must not retain values by accident.
  Rung: `check.sh full`.
- **Depends on:** Task 1.

### Task 3 — Persist the identity map; state blob schemaVersion 2 + migration
- **Files:** `host/Source/PluginProcessor.cpp` (blob format §5.1; `<ParamMap>` child),
  `host/tests/StatePersistenceTest.cpp`.
- **Verify:** round-trip a v2 blob; **load a v1 blob and assert it still restores** (v1 has no
  map → fall back to positional, which is exactly what v1 was saved under). Rung: `check.sh full`.
- **Depends on:** Task 2. **This is the one-way door — it is where the derivation rule becomes
  permanent, so it ships with the version field and the fallback in the same commit.**

### Task 4 — File PF-051 and close the capture gaps that block metering
- **Files:** `docs/BUGS.md` (PF-051, silent truncation past 64 params),
  `host/Source/FaustEngine.cpp:150-151` (bargraph capture → a new `Kind::Meter`),
  `host/Source/ParamPool.cpp` (meters are read-only: never written by `pushToFaust`).
- **Verify:** a `ui_fixtures` patch with an `hbargraph`; assert it reaches `ParamList` with
  `Kind::Meter` and that `pushToFaust` does **not** write its zone. Rung: `check.sh full`.
- **Depends on:** nothing — parallel-safe, cheap, and it unblocks the UI's metering
  requirement at the layer that actually blocks it.

### ✂️ **CUT LINE — Tasks 1-4 are the session. Everything below is stretch.**

Tasks 1–3 are one coherent change with one red case each and a migration; task 4 is small
and independent. That is a full session for this repo, and it moves the item STATUS.md's
own comment (`PluginProcessor.cpp:308-310`) names as unbuilt — which is the `assumed` metric.

### Stretch 5 — UI IR schema + fallback renderer
- **Files:** new `docs/ui_ir_schema.md`, `host/Source/UiIr.h` (parse + validate),
  `host/Source/ParamGridPanel.cpp` (consume a UI IR when present; today's sqrt grid becomes
  the `default` archetype when absent).
- **Verify:** `UiDesignGallery` renders each fixture under a hand-written IR; a malformed IR
  must fall back to the grid without an assertion. **No LLM involvement yet** — the IR is
  hand-written this session, which keeps the schema honest before a model is asked to emit it.
- **Depends on:** Task 1 (the `param` field references an identity).

### Stretch 6 — First token set + `synth-panel` archetype
- **Depends on:** Stretch 5. Uses `ParamInfo::group`, which is already captured and used by
  nothing (§1.1) — the cheapest possible first archetype.

**Explicitly not tomorrow** — see §6.

---

## 5. Interface contracts

Field-level, so the three workstreams can proceed in parallel once Task 1 lands.

### 5.1 Parameter model

```cpp
// host/Source/FaustEngine.h — added to ParamInfo
struct ParamInfo {
    // ... existing fields unchanged: label, defaultValue, min, max, step,
    //     kind, scale, unit, isMenu, group, zone ...

    // Stable semantic identity. Derived at capture, never from ordinal position.
    // Consumers: ParamPool slot assignment, the persisted map, UI IR `param`
    // references, MIDI CC targets, preset keys.
    std::string id;
};
```

**Derivation rule — versioned, because changing it orphans saved projects (D9):**

```
id_v1(p) = slug(p.group) + "/" + slug(p.label)     // "/" omitted when group is empty
slug(s)  = lowercase(s), every char outside [a-z0-9] -> '_',
           runs of '_' collapsed to one, leading/trailing '_' trimmed
collision: within one ParamList, the 2nd..Nth occurrence of an id gets "#2".."#N"
           in Faust capture order (which is deterministic — Faust emits groups and
           widgets alphabetically, FaustEngine.h:76-80)
```

**Slot-reuse policy, applied in `ParamPool::remap`:**

1. For each new param in capture order: if its `id` exists in the previous id→slot map
   **and** that slot is not already claimed this pass, reuse that slot. The user's value
   is retained.
2. Otherwise assign the **lowest free slot**.
3. Slots whose previous id is absent from the new list are freed **and reset to 0** — never
   left holding a value a newcomer could inherit. (This is the PF-020 hazard, generalized.)
4. If more than `POOL_SIZE` params survive assignment, the overflow is **reported as a
   compile-time error to the user**, not silently dropped (PF-051).
5. `Kind::Meter` params occupy no slot — they are read-only and never written by
   `pushToFaust`.

**Persisted blob, schemaVersion 2** (v1 fields unchanged; additive children):

```xml
<PluginForgeState schemaVersion="2" faustSource="…" prompt="…" uiStyle="faithful"
                  kind="instrument" kindSource="router" idScheme="v1">
  <STATE> … 64 macro_* PARAM children, verbatim apvts … </STATE>
  <ParamMap>
    <Slot index="0" id="osc/detune"/>
    <Slot index="1" id="filter/cutoff"/>
  </ParamMap>
  <UiIr> … §5.2, as an escaped JSON string attribute or a child tree … </UiIr>
</PluginForgeState>
```

**Migration:** a blob with no `schemaVersion` or `schemaVersion="1"` has no `<ParamMap>`;
restore falls back to positional remap — which is exactly the semantics it was saved under,
so old projects are bit-identical in behaviour. `idScheme` exists so a future derivation
change can migrate rather than orphan.

### 5.2 UI IR (v1) — renderer-agnostic by construction

No JUCE types, no pixel coordinates. Geometry is grid cells and spans, so a WebView backend
could render the same document (D2's hedge).

```json
{
  "schema": 1,
  "kind": "instrument",
  "title": "Warm Analog Bass",
  "archetype": "synth-panel",
  "tokens": "midnight-brass",
  "sections": [
    {
      "id": "osc",
      "title": "OSC",
      "span": 2,
      "controls": [
        { "param": "osc/detune", "style": "arc-knob",  "size": "lg",
          "label": "Detune", "span": 1 },
        { "param": "osc/shape",  "style": "segmented", "size": "md" }
      ]
    },
    {
      "id": "amp",
      "title": "AMP",
      "span": 1,
      "controls": [
        { "param": "amp/level", "style": "v-slider", "size": "md" },
        { "param": "amp/out",   "style": "meter",    "size": "sm" }
      ]
    }
  ]
}
```

| Field | Type | Rule |
|---|---|---|
| `schema` | int | Required. Unknown value → fall back to the default grid, never refuse to open |
| `kind` | enum | `instrument` \| `effect`. Must agree with §5.3; disagreement is surfaced, not silently resolved |
| `title` | string | Display only. Falls back to the plugin name |
| `archetype` | enum | `synth-panel` \| `channel-strip` \| `modular-rack` \| `pedal` \| `dual-column` \| `grid` (the current behaviour, and the fallback) |
| `tokens` | string \| object | A named set from the catalogue, or an inline override object with the same fields |
| `sections[].id` | string | Unique within the doc |
| `sections[].title` | string | May be empty (an unlabelled section) |
| `sections[].span` | int 1-4 | Relative width within the archetype's row grammar |
| `controls[].param` | string | **A `ParamIdentity` id (§5.1).** An id not present in the compiled patch is *dropped with a warning*, not an error |
| `controls[].style` | enum | `arc-knob` \| `ring-knob` \| `notched-knob` \| `endless-encoder` \| `h-slider` \| `v-slider` \| `toggle` \| `segmented` \| `meter`. Unknown → the `Kind`-derived default |
| `controls[].size` | enum | `sm` \| `md` \| `lg` |
| `controls[].label` | string | Optional override of the Faust label |
| `controls[].span` | int | Optional; defaults to 1 |

**Invariants the renderer enforces regardless of what the IR says:**
- A `Button`/`CheckButton` param never renders as a continuous control (PF-005's structural
  promise, `ParamGridPanel.h:74-77`).
- A `Kind::Meter` param is never writable.
- Any compiled param **not** referenced by the IR is appended to a trailing `grid` section —
  a parameter is never invisible because the model forgot it.

### 5.3 Instrument/effect IR field

Extends the ADR-011 response schema (additive, same treatment `reason` and `kind` already
received under PF-019) and the state blob:

```json
{
  "success": true, "faust_code": "…", "attempts": 1, "error": null, "reason": "ok",
  "kind": "instrument",
  "kind_source": "router",
  "voice": { "polyphony": 1, "convention": "freq_gain_gate" }
}
```

| Field | Type | Rule |
|---|---|---|
| `kind` | enum | `instrument` \| `effect`. **Must be present on the failure path too** — today `_failure()` omits it (`generate.py:310-313`), so a failed instrument request is indistinguishable from a failed effect one |
| `kind_source` | enum | `router` (keyword scoring) \| `user` (explicit override — **sticky across regeneration**) \| `verified` (agreed with the compiled voice contract) |
| `voice.polyphony` | int | 1 today. The field exists now so D5 does not need a schema change later |
| `voice.convention` | enum | `freq_gain_gate` \| `key_vel_gate` — the two spellings `extractVoiceControls` already distinguishes (`FaustEngine.h:117-119`), whose **units differ** |

---

## 6. Explicit non-goals for tomorrow

- **Polyphony** (D5). Not a compression of the estimate — a deliberate deferral with a
  named precondition.
- **MPE** (D7), pitch bend, aftertouch, CC64 sustain.
- **Sample-accurate MIDI event splitting.** Known and documented
  (`PluginProcessor.cpp:176-181`); worth doing *after* a pitch gate exists to prove the
  split changed nothing else.
- **WebView, and any JUCE 8 upgrade** (D2).
- **Any new LLM call, and any prompt change.** Stretch 5 hand-writes the IR on purpose:
  the schema must be proven before a model is asked to emit it, and prompt headroom is
  ~124 tokens (CLAUDE.md).
- **CLAP, AU, LV2, AAX** (D8).
- **Presets, undo, and the on-disk `.pforge` project format.** They all key on
  `ParamIdentity`; building them before it exists is how you get three identity schemes.
- **`getTailLengthSeconds()` derived from the envelope** (`PluginProcessor.h:82-83` TODO).
- **The piano roll and the on-screen keyboard.** Genuinely valuable and genuinely next —
  but they make the instrument *audible*, and tomorrow's work makes it *stable*. Sequencing
  is Open Question 2.
- **A listening pass.** Not delegable (CLAUDE.md; COLLABORATION.md §1).

---

## 7. Open questions — ranked by how much they block

1. **Where is the "intelligence-per-request audit" output?** *(Blocks Workstream C's
   consolidation entirely.)* It is not in this repo under that name (§1.7). Paste it, name
   the file, or tell me to consolidate without it — in which case the ADR set covers only
   `docs/BUGS.md`, `docs/architecture_review_2026-07-21.md` and the existing ADRs.

2. **Does tomorrow do the parameter model, or the keyboard?** *(Blocks the session's shape.)*
   My recommendation is the parameter model, because it is the root cause and because both
   UI and MIDI work write into it. But STATUS.md's own "Next three" has the keyboard as
   item 2, and **an instrument nobody can play is the thing currently blocking your listening
   pass** — which outranks a diff read. If the priority is hearing a synth this week, say so
   and I will invert tasks 1-3 and the keyboard.

3. **Is the identity slug allowed to be a one-way door on the first commit?** *(Blocks Task 3.)*
   §5.1 versions the scheme so a future change can migrate — but only if `idScheme` ships in
   the same commit as the first persisted map. Confirm you want the version field now rather
   than adding it when it first hurts.

4. **How many token sets, and who authors them?** *(Blocks Stretch 6, not the cut line.)*
   Diversity is a property of the catalogue (D3). ~12 hand-authored sets is my estimate for
   "two plugins don't look like siblings." I can draft them, but colour and type are taste,
   and taste in this project has been explicitly reserved to you.

5. **Is a Mac available, ever?** *(Blocks AU/AUv3 permanently, blocks nothing tomorrow.)*
   D8 defers them on hardware grounds. If the answer is no, AU should be recorded as
   `wontfix-for-now` rather than sitting on a roadmap as perpetually-next.
