# 020 — Generated faces v2 + the export path

Two dependency-ordered ladders toward the north star **"prompt → PluginForge →
downloadable plugin with its own face"**. Proposed by **ADR-038** (which also
accepts **ADR-023** + its 2026-08-13 amendment as the export design of record).
This file is the build order; the ADRs are the rationale.

- **Design target:** `docs/design/incant-ui/Generated Plugin Faces.dc.html`
  (four faces: Velvet Drift synth-panel, Iron Strip channel-strip, Echo Plate
  tape-unit, Dustfield texture-field) and `GKnob.dc.html`. Read
  `docs/design/incant-ui/README.md` §"Design tokens" and §1a–1d for exact
  geometry. These are **references written in HTML, not code to port** — recreate
  with `LookAndFeel_V4` overrides + `resized()` layout.
- **Already landed (ADR-035 steps 1–5):** `UiIr.h` schema 3,
  `ThemeValidate.h`, `GeneratedFaceLookAndFeel.h` (colours + one knob geometry +
  fonts), `ArchetypeLayout.h` (column / split / rail geometry), `llm/ui_face.py`.
- **Not yet built:** every visualizer in the mockup — ADSR bars, response
  curves, echo trails, the grain-cloud dot field, tape reels, big numeric
  readouts, toggle-chip rows, signal meters. `ArchetypeLayout.h:249` records it:
  the texture-field display region is *"reserved… nothing draws there yet."*

## How to run each step

One step per session. `/orient` → read this file's section for the step → state
the plan and flag anything in the mockup that conflicts with the actual code →
get approval → implement → `tools/check.sh` at the level touched → `/change-report`
→ `/handoff`. Work in a dedicated worktree.

**Evidence bar.** `GeneratedFaceLookAndFeel` / `ArchetypeLayout` paint and layout
code is Tier 2 — primary source cited `file:line`, a test added, an explicit
"not verified" line. The final "does the face read right" judgement is a human's
(COLLABORATION.md §1) — the machine check is geometry, not taste.

---

## Track F — generated faces v2

Ordered cheapest-first. F1–F3 add no component subsystem, no signal tap, no
second renderer. **F4 is an architecture gate with its own ADR.**

### F1 — knob geometry parity + bipolar rendering

**This is a bugfix on a shipped feature and is NOT gated on ADR-038**
(COLLABORATION.md §1: "a known defect is never gated").

**The defect.** `GeneratedFaceLookAndFeel::drawRotarySlider`
(`host/Source/GeneratedFaceLookAndFeel.h:280-333`) already draws the GKnob
arc/stroke/pointer correctly for a unipolar param. But every param-grid slider
holds a **0..1 normalised** value (the `macro_*` APVTS params; denormalisation to
real units happens only for the readout text — `ParamGridPanel.cpp:293-298`), so
a bipolar Faust param (`min < 0 < max`, e.g. an `Offset` of −1..+1) sits at
normalised **0.5** when it is at its musical centre, and the value arc fills a
~144° wedge **from the arc start**. That is the odd chunky crescent on the
"Offset 0.00" and "Drive" knobs in the 2026-09-09 distortion screenshot — the
knob looks broken because it is drawing "half full" for "centred".

**The fix.**

1. **Mark bipolar controls at build time.** In `ParamGridPanel::refreshParamKnobs`
   (`ParamGridPanel.cpp:117-267`), the per-slot loop has the `FaustEngine::ParamInfo`
   `p` with `p.min` / `p.max` / `p.defaultValue` (`FaustEngine.h:72-79`). After
   creating the `Slider` (`:248-258`), when `p.min < 0.0f && p.max > 0.0f`, set
   `sl->getProperties().set("bipolarDetent", -p.min / (p.max - p.min))` — the
   normalised position of real-zero. This works for the heuristic path too; no
   LLM or `UiIr` change. `juce::Component::getProperties()` is a `NamedValueSet`
   readable from `drawRotarySlider`'s `juce::Slider&` argument.
2. **Draw the value arc from the detent.** In `drawRotarySlider`, read
   `slider.getProperties().getWithDefault("bipolarDetent", {})`. When present,
   compute `detentAngle = rotaryStartAngle + detent * (rotaryEndAngle -
   rotaryStartAngle)` and draw the value arc `detentAngle → toAngle` (either
   direction — `Path::addCentredArc` takes `fromRadians`/`toRadians` in the
   sign order given). Keep the near-zero guard, applied to `|sliderPos − detent|`
   instead of `sliderPos`. Pointer geometry is unchanged.
3. **Pin the track colour.** The mockup's track is `rgba(255,255,255,.10)` —
   **ink at 10 % alpha**, not the `line` token (`README.md:253`). The current
   `trackColour = Theme::outline` / `t.line` (`GeneratedFaceLookAndFeel.h:339`)
   is a different colour and, on a light face (Iron Strip), potentially the
   wrong lightness. Set `trackColour` from the validated `text` token at
   `~0.10f` alpha; verify against all four faces' screenshots.

**Traps.**
- `setRotaryParameters(pi*1.2f, pi*2.8f)` is set by `ParamGridPanel.cpp`, not the
  LookAndFeel — `drawRotarySlider` receives those as `rotaryStartAngle` /
  `rotaryEndAngle` and must not re-derive them (`GeneratedFaceLookAndFeel.h:255-259`).
- `getProperties()` on a `Slider` that a `SliderAttachment` owns is fine — the
  attachment touches the value, not the property set.
- A perfectly symmetric range is not guaranteed (Iron Strip EQ is ±12 dB, but a
  drive-offset might be −0.5..+1.0). The detent is `-min/(max-min)`, never
  hard-coded 0.5.

**Done when.** A ±range param renders a value arc centred on the detent; a
unipolar param is unchanged; `EditorSessionTest` asserts the fill start for a
bipolar vs a unipolar param (a `drawRotarySlider` geometry `ForTest` seam, or a
snapshot diff); the four faces still read correct in a `tools/ui_iterate.sh`
contact sheet (human look). `tools/check.sh full` green.

### F2 — toggle / enum chip rows

**Gated on ADR-038.**

The mockup's SAW / SQR / TRI (1a), SYNC / FREE / PING-PONG (1c),
FREEZE / REVERSE (1d) — filled-selected-vs-outlined-unselected chip pairs.

- `GeneratedFaceLookAndFeel` has **no** `drawToggleButton` override
  (`ForgeLookAndFeel.h:25` lists it as a candidate, none exists — confirmed by
  grep). Add one: filled rect in the accent with the `surface` text colour when
  on; 1px outline in `line` with dim text when off; mono font at the mockup's
  9–10px / .1em.
- The `UiIr` needs to know a param is enum-like. `ControlRef.style` already has
  `"toggle"` (`UiIr.h:29`). Decide in F2: does `deriveLayoutFromGroups` infer it
  (a Faust `nentry` with integer `step` and a small `max-min`, or a cluster of
  `checkbox` params sharing a group), or does only the `ui_face` producer emit
  it? Infer where possible — the heuristic path must not regress to plain knobs.
- A three-state enum from a single `nentry` needs a radio-group widget, not three
  `ToggleButton`s bound to one slot. `juce::AudioProcessorValueTreeState::
  ButtonAttachment` is 0..1 only. This may need a small `ComboBox`-backed or
  custom radio control bound through a value conversion — scope it in the step.

**Done when.** An `nentry`-with-3-values param renders as a chip row; toggling
moves the slot value; `EditorSessionTest` covers it; `check.sh full` green.

### F3 — bespoke archetype geometry

**Gated on ADR-038.**

`ArchetypeLayout.h` already routes `synth-panel`/`channel-strip` → `columns()`,
`tape-unit` → `split()`, `texture-field` → `rail()` (`:315-321`). Gaps vs the
mockup:

- **`columns()` does not balance** — its own comment admits it (`:186-188`:
  "columns can end at different heights… the mockups (Velvet Drift's
  OSC/FILTER/ENV/FX columns are visibly uneven)"). The mockup wants four equal
  columns. Balance by distributing sections to minimise the tallest column, or
  by a fixed equal split when section count ≤ 4.
- **`split()` (tape-unit)** puts sections left/right by content but does not
  reserve the mockup's left-hand structure: the two reel circles, the big Oswald
  time readout, the SYNC/FREE selector. That structure is decorative geometry
  (no signal) — a reserved sub-region the renderer fills, like `rail()` reserves
  one.
- **`rail()` (texture-field)** already reserves the display region (`:249-250`)
  but F3 does not fill it — that is F4. F3 just makes the rail width and the
  control-slider styling (the mockup's labelled horizontal sliders with square
  thumbs and right-aligned mono readouts) match.

Keep `contentHeightForSections()` the one height function (`ArchetypeLayout.h:112`,
`ParamGridPanel.h:51-57` — the `kChromeHeight` defect shape). Extend
`ParamGridLayoutTest.cpp` (no overlaps, every control placed once, per archetype).

**Done when.** The four archetypes lay out recognisably like 1a–1d's *structure*
(not yet their visualizers); `ParamGridLayoutTest` green for each; `check.sh full`
green; a contact-sheet human look.

### F4 — the visualizer subsystem  *(ARCHITECTURE GATE — own ADR)*

A `FaceVisual` drawing layer for the mockup's data displays. **Do not start
without its own ADR** — it is a new component subsystem and, for the real-signal
visuals, a new audio-thread surface.

Two classes, and the ADR must scope which are in:

- **Derivable** (no new plumbing) — the ADSR bars (from the env params' current
  values), the echo-trail / grain-histogram bars (decorative, or from the
  feedback param), the tape reels, the grain-cloud dot field. These are pure
  functions of param values the panel already holds.
- **Real-signal** (new architecture) — the filter/EQ **response curve** needs an
  offline frequency-response evaluation of the JIT'd DSP (a swept sine through a
  copy of the patch, off the audio thread); the **output meter** and Iron
  Strip's **gain-reduction histogram** need a metering path that survives PF-052
  (`Kind::Meter` zones are discarded upstream in `ParamPool` — `docs/BUGS.md`).
  The editor's existing `displayLevel` 30 Hz tick (`PluginEditor.cpp`) is one
  real signal already available; everything else is not.

`README.md:259`: *"The response plots in 1a/1b are placeholders for real curves
the DSP can already provide"* — that claim needs verifying against the actual
code before F4's ADR relies on it.

### F5 — display typography + numeric readouts

**Gated on ADR-038, sequences after F3/F4.**

The Oswald "375" delay readout, the "playhead 1.42 s", the per-knob mono
readouts in the accent. Depends on F3's reserved regions and F4's signal data
for the live ones. `theme.readout` / `theme.display` enums already exist
(`GeneratedFaceLookAndFeel.h:215-221`).

---

## Track E — plugin export (PF-053)

Design of record: **ADR-023 + its 2026-08-13 amendment** (accepted by ADR-038).
The hard part — AOT Faust emission with no libfaust at build or runtime — is
already designed. `tools/export_repo.py` and `tests/test_export_repo.py` exist;
`/export` is gated by `.claude/skills/export/SKILL.md` and stays gated until E3.

### E1 — AOT emit

`libfaust`'s `generateAuxFilesFromString(name, dsp, argc, argv, err)`
(`/usr/include/faust/dsp/libfaust.h:117-119`, symbol confirmed exported in the
linked `libfaust.so` per ADR-023's amendment) → a `class mydsp : public dsp`
header for the accepted patch, **in-process**, no subprocess, no new dependency.
Exercise it end to end — the amendment verified the symbol's existence and
linkage but *not* its emitted output.

**Done when.** A helper (in the host, or a small tool the host shells to) turns
`currentSource()` into a compilable `mydsp` header; a round-trip test compiles
the emitted header against a stub `main`.

### E2 — wire `processBlock` + fix identity

Rewrite `tools/export_repo.py`'s `Plugin.cpp` template (`:90-130`, currently
`// Placeholder: passthrough`) so the exported `processBlock` instantiates
`mydsp`, calls `buildUserInterface` on a `MapUI`, and calls `compute` — a
stripped `FaustEngine` with the entire swap protocol deleted (a frozen export
never recompiles). Derive `PLUGIN_CODE` / `PRODUCT_NAME` per-patch from
`libfaust`'s `generateSHA1(data)` (`libfaust.h:42`, confirmed exported). Fix
`tools/export/CMakeLists.txt.j2:21`'s hardcoded `PLUGIN_CODE Pfh1` — it collides
with the shipping `PluginForgeHost` target, the exact defect that file's own
comment warns against (ADR-023 amendment, Finding 1).

**Done when.** `tools/export_repo.py` emits a project that compiles clean; the
`TRUE == "TRUE"` bare-identifier bug (amendment Finding 1) is gone;
`tests/test_export_repo.py` covers the DSP wire, not just the shared-code
compile.

### E3 — prove sound + un-gate

Build an exported project; `pluginval --strictness 5 --validate` it; a render
check (reuse `bench/render_oracle.py`'s `measure()` shape — no NaN / silence /
DC / runaway). Replace `.claude/skills/export/SKILL.md`'s refusal message with
the working invocation. **This is the only step that lifts the `/export` gate**,
on the skill's own three stated criteria: builds clean, loads in a DAW, makes
sound.

**Done when.** An exported effect and an exported synth each pass `pluginval` at
strictness 5 and the render check; `/export` runs; a human loads one exported
plugin in REAPER and hears it.

### E4 — export the face  *(own ADR)*

Today an exported plugin renders `juce::GenericAudioProcessorEditor` — a bare
parameter list. E4 emits the `UiIr` JSON + a minimal `GeneratedFaceLookAndFeel`
port (or a data-driven editor built from the IR) so the downloaded plugin
carries its identity. Composes F1–F3 with E1–E3. New renderer surface in the
exported project → its own ADR (ADR-023 amendment §3 anticipated reusing
`UiIr.h` / `ParamCapture` here rather than a parallel implementation).

---

## Deferred / open

- **The `claude_design` MCP** (`DesignSync`) needs `/design-login` to pull a
  possibly-updated `Generated Plugin Faces.dc.html` / `support.js` from the
  design project. Until then the committed on-disk copy is the source of record
  (COLLABORATION.md §8; `support.js` is a browser harness — ignore).
- **F4's "the DSP can already provide these curves" claim** (`README.md:259`) —
  unverified against the code.
- **Track B — shell redesign (ADR-036)** shares `ArchetypeLayout.h` with F3 but
  is not sequenced by ADR-038.
