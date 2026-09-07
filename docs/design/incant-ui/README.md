# Handoff: Incant Audio UI (generated faces + shell redesign)

> **Location note (2026-09-03).** This bundle was distilled from the design-tool export
> `design_handoff_generated_plugin_faces/` (a browser harness, `support.js`, is omitted).
> It now lives at `docs/design/incant-ui/` as a dated, read-only point-in-time record
> (COLLABORATION.md §8) — supersede it with a new dated bundle rather than editing it.
> Where this text or `GENERATION_PLAN.md` says `design_handoff_generated_plugin_faces/…`,
> read `docs/design/incant-ui/…`. ADR-035 (accepted) and ADR-036 are the decision records;
> `docs/sessions/018-incant-ui-faces-and-shell.md` is the multi-session build plan.

**Start here:** `GENERATION_PLAN.md` is the current build order, re-grounded against `main`. It
supersedes the "Implementation order" section below on one point — **UiIr schema 3 has already
landed** in `UiIr.h` (Theme struct, per-token degradation, `[1,3]` ceiling, always-written block),
so Step 1 here is done. Begin at contrast validation.

Two bodies of work are in this bundle:

- **Generated plugin faces** — per-plugin identity for compiled patches. Sections 1a–1d below.
- **Shell redesign** — the host chrome around them, on unchanged Ember tokens. Section 2 below.

## Overview

Incant Audio renders every generated patch with one shell look: `ForgeLookAndFeel` + Ember
Console tokens + `ParamGridPanel`'s sqrt grid or one-control-per-row sectioned list. The goal of
this work is that a generated plugin looks like its own product — its own archetype, palette,
type and metering — while the host chrome around it stays Ember Console.

Three things ship together:
1. `UiIr` schema 3 — schema 2 plus a `theme` block (colours, type tokens, knob style, density).
2. A per-face renderer: a second LookAndFeel scoped to `ParamGridPanel`, plus archetype layouts.
3. A post-compile LLM call that emits the IR, validated host-side, falling back to
   `deriveLayoutFromGroups()`.

## About the design files

The `.dc.html` files in this bundle are **design references written in HTML** — they show
intended look, geometry and colour, and they are not code to port. The target is C++17 / JUCE 7:
recreate them with `juce::LookAndFeel_V4` overrides and `Component::resized()` layout, using this
repo's existing conventions (header-only style helpers next to `Theme.h`/`ParamGridLayout.h`,
`*ForTest` accessors, message-thread-only UI mutation).

`support.js` is the harness that renders the `.dc.html` files in a browser. Ignore it.

## Fidelity

**High fidelity.** Colours, sizes and type in the four faces are final intent. The shell
recreation (`Incant Audio Shell.dc.html`) is a faithful rebuild of what ships today — use it as
the before-picture and as a check that any refactor did not move the chrome.

---

## Screens

### 0. Shell recreation (reference only, do not implement)

`Incant Audio Shell.dc.html`. Two states, both rebuilt from source:

- **Effect, 900×500.** Margin 16, title band 32 (Pirata One 18px `#f5f0e6` left; `Knobs: auto`
  120×32 and `Show code` 110×32 right, 6px gaps). Split region: left grid 562px, 4px divider gap
  with a 1px `#383838` seam, right column 302px. Right column top-to-bottom: PromptPanel 254,
  gap 8, meter 14. Bottom: gap 8, SampleBrowserPanel 64.
- **Instrument, 900×786.** Same, plus sectioned grid (heading 20, control rows 95, section gap
  4; content 642 against a 570 viewport so it scrolls) and a 72px keyboard band (24px control
  row: `<` 24, `Oct: N` 48, `>` 24; piano gets the remaining 48).

Numbers come from `PluginEditor.h::Chrome`, `PluginEditor.cpp::resized()`,
`PromptPanel::resized()`, `ParamGridPanel::layoutControls()/layoutSectioned()`,
`SampleBrowserPanel::resized()`, `KeyboardPanel::resized()`.

### 1a. `synth-panel` — Velvet Drift

760×440. Surface `#0e0f13`. Header 54px: name in Space Grotesk 700 / 19px / .16em on `#eef2ee`,
right side `8-voice · poly` and eight 5px voice dots (`#8fe3c1` lit, `rgba(255,255,255,.14)`
unlit) in IBM Plex Mono 10px `rgba(238,242,238,.4)`. Body: four equal columns split by 1px
`rgba(255,255,255,.07)` rules, 18px/16px padding. Column headings Space Grotesk 500 / 10px /
.22em `rgba(238,242,238,.45)`.

- OSC: two 54px arc knobs (Detune, Blend) + a three-state waveform selector, selected chip filled
  `#8fe3c1` with `#0e0f13` text, unselected 1px `rgba(255,255,255,.16)`.
- FILTER: 64px Cutoff (the `lg` control) + 44px Reso, and a 44px filter-response plot, 1.5px
  `#8fe3c1` stroke in a `rgba(255,255,255,.09)` box.
- ENV: four 4px × 120px vertical tracks (`rgba(255,255,255,.09)`) with `#8fe3c1` fill and a 2px
  `#eef2ee` cap, labels A/D/S/R.
- FX: two 54px knobs + an output meter, 6px track with `#8fe3c1` fill, `OUT / −6.2 dB` in mono 9px.

### 1b. `channel-strip` — Iron Strip

760×440, light. Panel `linear-gradient(#d7d3c9,#c6c2b8)`, ink `#1c1b18`, single accent `#b4402f`,
display Barlow Condensed, readouts IBM Plex Mono. Header 46px is a dark `#1c1b18` bar with
`IRON STRIP` at 600/20px/.3em in `#e8e4d9`.

Three columns (1.4 / 1 / 132px fixed), 1px `rgba(28,27,24,.18)` rules. EQ column: 104px response
plot on `#f0ede5` with a `rgba(28,27,24,.25)` border and a 2px `#b4402f` curve, then four 48px
knobs with `#efece4` faces and `rgba(28,27,24,.18)` tracks. Comp column: an 8-bar gain-reduction
histogram in the same plot box, then three 48px knobs. Output column: an 8px fader track
(`#1c1b18`) with a `#efece4` cap, an 8px meter with a `#4f7a48` body and `#b4402f` peak zone, and
a full-width `BYPASS` block in the dark ink.

### 1c. `tape-unit` — Echo Plate

760×440. Surface `#17140f`, ink `#efe6d4`, accent `#f0a63c`, display Oswald. Header 50px with a
9px lamp (`#f0a63c`, 10px glow) before the name at 600/18px/.26em.

Left 330px: two reel circles (104px and 74px, 1px `rgba(239,230,212,.22)` rings with `#221d16`
hubs) joined by a 1px tape line; a 46px Oswald time readout `375` in `#f0a63c` next to
`ms · 1/8 dotted`; a three-way SYNC / FREE / PING-PONG selector, selected filled amber with
`#17140f` text. Right: three 58px knobs (Feedback, Wow, Saturate), an eight-bar echo-trail
display fading from `#f0a63c` to 10% alpha, and a MIX slider with a 14px `#efe6d4` thumb.

### 1d. `texture-field` — Dustfield

760×440. Surface `#08080b`, ink `#e9e7f2`, accent `#a78bfa`, alt `#67e8f9`, display Archivo.
Left: a grain-cloud panel — three layered radial-gradient dot fields (violet 1.4px, cyan 1px,
violet 1px at 47×39 / 29×53 / 17×23) on `#0b0b11` with a 1px `rgba(233,231,242,.1)` border, a
horizontal centre rule, and a 2px `#67e8f9` playhead with a 12px glow. Corner labels in mono 9px.
Right 212px: two 54px knobs (Density, Size), four labelled horizontal sliders (Spray, Pitch,
Shimmer, Mix) with 3px tracks, 10px square `#e9e7f2` thumbs and right-aligned mono readouts, then
FREEZE (filled `#a78bfa`, `#08080b` text) and REVERSE (1px outline) buttons.

### Knob geometry (all four faces)

Arc from **1.2π to 2.8π** — the same `setRotaryParameters` values `applyPresentation()` already
sets. Track `rgba(255,255,255,.10)` (or `rgba(28,27,24,.18)` on light), value arc in the accent,
round caps, stroke 4px at 52px diameter (scale with size). Pointer: a 2px accent line from 34%
radius to just inside the arc. Label under the knob, 9px, .14em, uppercase, dim ink; readout
below it in mono 10px in the accent. See `GKnob.dc.html`.

---

## Implementation order

Each step is independently shippable and independently testable.

### Step 1 — `UiIr` schema 3 (no visual change)

`host/Source/UiIr.h`.

- Add `struct Theme { std::string surface, panel, line, text, textDim, accent, accentAlt,
  display, readout, knob, density; }` with defaults equal to the Ember Console tokens.
- `parse()`: raise the ceiling from `schema > 2` to `schema > 3`; read `theme` when
  `schema >= 3`. `toVar()`: always write it, same policy as the `components` block.
- Unknown enum values (`display`, `readout`, `knob`, `density`) degrade to the default token, not
  to `empty()` — one bad string must not discard a whole valid layout.
- Persist the IR JSON in the state blob alongside `faustSource`/`prompt` in
  `PluginProcessor::getStateInformation`/`setStateInformation` so a reopened project restores its
  face without regenerating.

Tests: round-trip a schema-3 IR; a schema-4 IR parses as `empty()`; a schema-3 IR with a garbage
`theme.display` keeps its sections.

### Step 2 — theme validation, host-side

New header next to `Theme.h`, e.g. `ThemeValidate.h`, no JUCE `Component` dependency.

- Parse hex/rgba to `juce::Colour`; compute WCAG relative luminance with the same formula
  `Theme.h`'s contrast table documents.
- Enforce: `text` on `surface` ≥ 7:1, `textDim` ≥ 4.5:1, `accent` ≥ 3:1, and `accent` ≠
  `accentAlt` ≠ `text` by a minimum ΔE or luminance delta. Ember's own `#f5f0e6`/`#8a8378`
  numbers are the reference values to check the implementation against.
- Failure is **per-token**: substitute the Ember token for the failing one and keep the rest.
  Never reject the whole face — that is the mistake the `GeneratedAccent` bone swatch already
  taught this codebase once (see `Theme.h`'s RESOLVED RISK note).

Tests: the four faces in this bundle all pass; the historical bone `#f5f0e6` accent fails and is
replaced.

### Step 3 — a LookAndFeel per face

New `host/Source/GeneratedFaceLookAndFeel.h`, header-only, same convention as
`ForgeLookAndFeel.h`.

- Construct from a validated `UiIr::Theme`. Build its `ColourScheme` the same way
  `ForgeLookAndFeel` does, from theme tokens instead of `Theme::` constants.
- Override `drawRotarySlider` for the `arc` knob style (arc + pointer as specified above),
  `drawLinearSlider` for the 3–6px track + accent fill, and `drawToggleButton` for the filled/
  outlined chip pairs used in 1a, 1c and 1d.
- Attach it to `paramGridPanel` only — `paramGridPanel.setLookAndFeel(&faceLnf)` — so the title,
  prompt column, sample browser and keyboard keep the shell look. Do **not** call
  `setDefaultLookAndFeel`; `ForgeLookAndFeel.h`'s header explains why.
- Lifetime, both rules load-bearing: declare the member **before** `paramGridPanel` in
  `PluginEditor.h`, and call `paramGridPanel.setLookAndFeel(nullptr)` before it dies.
  `~LookAndFeel()` asserts if anything still points at it.
- Fonts cannot be free text. Embed a fixed set (suggest four display faces and two monos) through
  the existing `PluginForgeAssets` CMake target and dispatch by name in `getTypefaceForFont`,
  exactly as `ForgeLookAndFeel` does for the current five. `theme.display`/`theme.readout` are an
  enum over that set.

Tests: a compile with a theme produces a `ParamGridPanel` whose `findColour` values differ from
the shell's while the editor's own do not; teardown with the face attached passes the existing
leak check.

### Step 4 — archetype layouts

This is the step that makes grouped patches read as panels. Today `layoutSectioned()` places
every control at `x = 0`, one per row, at `kCellH` — a half-width list.

- New `host/Source/ArchetypeLayout.h`: free functions, no JUCE dependency, mirroring
  `ParamGridLayout.h`. Given section count, per-section control counts, spans and a target size,
  return rectangles.
- `synth-panel` / `channel-strip`: sections become **columns**, controls flow inside a column;
  a section's `span` widens its column. `tape-unit`: a two-region split (transport left, tone
  right). `texture-field`: a display region plus a control rail. `pedal` / `utility`: the
  existing grid is fine.
- Keep one source of truth for height: `contentHeightForCurrentMode()` and the layout pass must
  call the same function. `PluginEditor.h`'s `kChromeHeight` note records what happens otherwise.
- Write `host/tests/ParamGridLayoutTest.cpp`. `ParamGridLayout.h`'s own header comment says this
  file "has never existed" and asks whoever reads it to either write it or delete the claim.

Tests: 6-param effect and 18-param synth against each archetype; no overlapping rects; every
control placed exactly once.

### Step 5 — the generation call

- New action in `llm/generate.py` (metadata-to-metadata, after a successful compile), plus a
  prompt file. `ui_ir_system_prompt.md` in this bundle is the prompt text — it is a draft, and
  prompt wording is worth a second read before it ships.
- Input: the captured param table (label, kind, group, range, unit) + the user's prompt +
  `isInstrument`. Output: the JSON object.
- Validate before use: schema in range, every writable param named exactly once, no
  Button/CheckButton given a continuous style, no `Kind::Meter` listed as a control, theme
  passes Step 2. Any failure → `deriveLayoutFromGroups()`, which stays the floor.
- Cheap and optional. A failed or slow second call must never delay the DSP going live.

### Step 6 — verification loop

- Drive the four prompts in `ui_ir_system_prompt.md`'s worked examples through
  `tools/ui_iterate.sh` and diff the contact sheet against `Generated Plugin Faces.dc.html`.
- The dev-cockpit export (`PLUGINFORGE_COCKPIT_STATE`) already reports control labels, groups and
  the derived accent; extend it with the archetype and theme so a run is checkable without eyes.
- Add `EditorSessionTest` scenarios: theme applied, theme rejected per-token, IR restored from a
  reopened project, archetype layout placed every control.

---

## Design tokens

**Shell (unchanged, from `Theme.h`).** background `#050505`, surface `#0c0c0c`, surfaceSunken
`#000000`, surfaceRaised `#131313`, outline `#383838`, textPrimary `#f5f0e6`, textSecondary
`#8a8378`, accent `#ff4b1f`, progress `#ffb03d`, danger `#ec3b52`, meterHot `#ff8f4d`. Generated
accents: ember `#ff4b1f`, amber `#ffb03d`, rust `#d9542b`, coral `#ff7a45`. Space 4/8/12/16/24;
radius 3/6/10/14; stroke 1/1.5/3.5/4. Type: Pirata One 18 (title, once), Big Shoulders Display
SemiBold 11/12/14, Work Sans 11/12, JetBrains Mono 12.

**Faces.**

| | surface | ink | dim ink | accent | alt | display | readout |
|---|---|---|---|---|---|---|---|
| Velvet Drift | `#0e0f13` | `#eef2ee` | `rgba(238,242,238,.45)` | `#8fe3c1` | — | Space Grotesk | IBM Plex Mono |
| Iron Strip | `#d7d3c9`→`#c6c2b8` | `#1c1b18` | `rgba(28,27,24,.6)` | `#b4402f` | `#4f7a48` | Barlow Condensed | IBM Plex Mono |
| Echo Plate | `#17140f` | `#efe6d4` | `rgba(239,230,212,.4)` | `#f0a63c` | — | Oswald | IBM Plex Mono |
| Dustfield | `#08080b` | `#e9e7f2` | `rgba(233,231,242,.5)` | `#a78bfa` | `#67e8f9` | Archivo | IBM Plex Mono |

Face rules: hairlines at 7–10% ink alpha; section headings 9–11px uppercase at .2em+ tracking in
dim ink; readouts always mono, always in the accent; one accent per face (an alt is allowed only
for a distinct signal — the Dustfield playhead, the Iron Strip peak zone).

## Assets

None. Every graphic is drawn — arcs, rules, bars, dot fields. The response plots in 1a/1b are
placeholders for real curves the DSP can already provide. Fonts are Google/OFL faces and must be
embedded through `PluginForgeAssets`, not resolved from the host OS.

---

## 2. Shell redesign

Two directions for the host chrome, both on the Ember Console tokens unchanged — nothing is
repainted. They live in the same `Incant Audio Shell.dc.html`, in a section above the recreations.

**The problem being solved.** `kLeftFraction` is a fixed 0.65, so at `kMinWindowW` = 700 the right
column computes to 232px and PromptPanel's own button row drops widgets to 0px — `Chrome::promptH`'s
own comment records `refineSelector` hitting 0px at the 900px default. Both directions take the
prompt off the width budget entirely, so no band depends on a percentage.

**2a Command Bar** (`screenshots/2a-command-bar-1160.png`). The prompt becomes a full-width bar at
the bottom of the window. The grid owns everything above it as a hairline lattice —
`repeat(auto-fit, minmax(180px, 1fr))`, cells sharing 1px `#383838` borders with no gaps, section
headers spanning `1/-1` at 27px with a rank number in the accent. A UiIr `span: 2` section renders
its headline control as a double-width cell with a 62px knob instead of a 40px one, which is what
finally makes that field visible. Title-bar disclosures become fixed 30px squares, so the title
truncates under pressure instead of the buttons vanishing.

**2b Rail + Dock** (`screenshots/2b-rail-dock-1160.png`). A 52px mode rail on the left replaces the
title-bar disclosure cluster. The prompt column becomes a user-dragged dock — a persisted width with
a 280px floor and a 40% ceiling, not `kLeftFraction`. One 26px status bar absorbs the meter, the
status line and the sample-browser line, which today are three separate bands.

**Both at the 700×500 minimum** (`screenshots/2c-both-at-700-minimum.png`). 2a drops to three
columns and collapses family/refine behind a `⋯` while Generate keeps its full hit target. 2b's dock
auto-collapses to a 30px vertical tab below its 280px floor and the grid takes the width back. The
2a frame shows the grid genuinely scrolling at that size, with the same 8px scroll thumb the
recreation draws — an 18-param synth does not fit a 700px window, and the affordance is the point.

**Implementation notes.** The lattice is a real `Component::resized()` change, not a repaint: it
replaces the sqrt-grid formula (`cols = clamp(ceil(sqrt(N)), 2, 6)`) with a width-driven column
count, and it wants the same `ArchetypeLayout.h` free functions Step 4 introduces. Keep
`contentHeightForSections()` as the one height function — `layoutSectioned()` and
`contentHeightForCurrentMode()` already share it precisely so they cannot disagree.

Open question for the developer: 2a removes the right column, which is where the compile-error
region currently lives. `Chrome::promptH` calls that "a scrollable error region" but neither the
current build nor these mockups draw a scroll affordance on it. Decide where a 20-line Faust error
goes in 2a before building it.

---

## Files

`screenshots/` holds a 2× PNG of every screen — the fastest way to see the target
before reading geometry. The `.dc.html` files remain the source of record for exact values.

| Screenshot | Screen |
|---|---|
| `screenshots/shell-effect-900x500.png` | Current editor, effect state (before-picture) |
| `screenshots/shell-instrument-900x786.png` | Current editor, instrument state (before-picture) |
| `screenshots/1a-velvet-drift-synth-panel.png` | 1a Velvet Drift |
| `screenshots/1b-iron-strip-channel-strip.png` | 1b Iron Strip |
| `screenshots/1c-echo-plate-tape-unit.png` | 1c Echo Plate |
| `screenshots/1d-dustfield-texture-field.png` | 1d Dustfield |
| `screenshots/2a-command-bar-1160.png` | 2a Command Bar, 1160px |
| `screenshots/2b-rail-dock-1160.png` | 2b Rail + Dock, 1160px |
| `screenshots/2c-both-at-700-minimum.png` | Both directions at the 700px minimum |

| File | What it is |
|---|---|
| `GENERATION_PLAN.md` | **Current build order** — the five gaps, in dependency order |
| `Incant Audio Shell.dc.html` | Shell redesign (2a/2b/2c) + the recreations (R1/R2) |
| `Generated Plugin Faces.dc.html` | The four target faces |
| `GKnob.dc.html` | Arc-knob geometry used by all four faces |
| `ShellCell.dc.html` | Lattice grid cell used by the shell redesign |
| `PFCell / PFRotary / PFSection / PFSamples .dc.html` | Shell-recreation parts (current widgets) |
| `ui_ir_system_prompt.md` | Schema-3 prompt text (the LLM's system prompt) |
| `github.md` | Repo association + screen map (which files each screen came from) |
| `support.js` | Browser harness for the `.dc.html` files — not part of the design |

To view a mockup: open the `.dc.html` file in a browser with `support.js` alongside it.

Source of record for the recreation: `host/Source/PluginEditor.{h,cpp}`, `Theme.h`,
`ForgeLookAndFeel.h`, `PromptPanel.{h,cpp}`, `ParamGridPanel.{h,cpp}`, `UiIr.h`,
`SampleBrowserPanel.{h,cpp}`, `KeyboardPanel.{h,cpp}`, `CodeEditorPanel.cpp`,
`docs/ui_design_plan.md`.
