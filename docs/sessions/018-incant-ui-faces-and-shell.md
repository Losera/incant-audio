# 018 — Incant Audio UI: generated faces + shell redesign

Multi-session build plan for **ADR-035** (accepted 2026-09-03 — per-plugin generated
faces) and **ADR-036** (proposed 2026-09-03 — shell redesign, prototype two directions).

- **Decision records:** `docs/decisions.md` ADR-035, ADR-036.
- **Design bundle:** `docs/design/incant-ui/` — `GENERATION_PLAN.md` is the grounded build
  order; `README.md` has the per-face geometry; `screenshots/` are the verification
  targets; the `.dc.html` files are the source of record for exact px/colour values.
- **Already landed:** `UiIr` schema 3 (`host/Source/UiIr.h` — `Theme` struct, `[1,3]`
  ceiling, per-token degradation, `theme` block always written, persisted in the state
  blob). PR #51.

## How to run each session

One gap per session. `/orient` → read this file's section for the gap → state the plan
and flag anything in `GENERATION_PLAN.md` / the bundle README that conflicts with the
actual code → get approval → implement → `tools/check.sh` at the level you touched →
`/change-report` → `/handoff` → `/clear`.

Work in a dedicated worktree. `feat/theme-validate` already exists off `main` for gap A1.

Evidence bar: `host/Source/**` non-RT C++ is Tier 1 for tests, but anything touching
`ParamGridPanel` layout, the state blob, or a `LookAndFeel` lifetime is Tier 2 — primary
source cited `file:line`, a test added, and an explicit "not verified" line.

---

## Track A — generated faces (ADR-035)

Ordering is `GENERATION_PLAN.md`'s five gaps, cheapest first. The producer (A2) comes
early because the host ignores its output until A3.

### A1 — `host/Source/ThemeValidate.h` (contrast validation)

**Goal.** A header-only, no-`juce::Component` validator: parse hex/rgba → `juce::Colour`,
compute WCAG relative luminance, enforce contrast, and on failure **substitute the Ember
token for that one field** — never reject the whole `Theme`.

**Files.** New `host/Source/ThemeValidate.h`; new `host/tests/ThemeValidateTest.cpp`;
`host/CMakeLists.txt` (register the test).

**Traps.**
- **Reference colour is unresolved.** `Theme.h:47-53` measures contrast against
  `background` `#050505`; `GENERATION_PLAN.md` / the bundle README say "against
  `surface`" (`#0c0c0c`). Pick one, write it in the header comment, and use `Theme.h`'s
  measured numbers (textPrimary 17.94:1, textSecondary 5.43:1, accent 6.09:1, progress
  11.19:1) as the fixture for whichever reference you chose.
- **Thresholds** (from the bundle README): `text` ≥ 7:1, `textDim` ≥ 4.5:1, `accent`
  ≥ 3:1; and `accent` ≠ `accentAlt` ≠ `text` by a minimum luminance/ΔE delta.
- The negative case is the historical bone `#f5f0e6` accent — `Theme.h`'s RESOLVED RISK
  note explains why it fails ("on a bone-accented patch every knob …"). Your test must
  reject it and substitute.

**Done when.** The four bundle faces (Velvet Drift / Iron Strip / Echo Plate / Dustfield —
tokens in `docs/design/incant-ui/README.md` §"Design tokens") all pass; bone `#f5f0e6`
fails and is replaced; `tools/check.sh fast` green.

### A2 — `llm/ui_face.py` + a `ui_face` action

**Goal.** A second bounded non-DSP LLM call, made **after a successful compile**, that
emits the IR JSON. Host still ignores the output — this ships safely alone.

**Files.** New `llm/ui_face.py`; new `llm/prompts/ui_face_prompt.md` (text drafted in
`docs/design/incant-ui/ui_ir_system_prompt.md` — **prompt wording is Tier 2, read it
twice**); `llm/generate.py` (`action` dispatch at `:650`); new `tests/test_ui_face_unit.py`.

**Template.** `llm/recommendation.py` is the exact shape to copy: `PROMPT_PATH`,
`parse_and_validate_recommendation` → raises a typed error, `MAX_*` / `_LIMITS` caps,
`recommend_plugin()` calling `providers.make_generator(..., max_tokens=<small>)`.

**Traps.**
- **`llm/CONTRACT.md`:** exactly one JSON line on stdout; always exit 0 in subprocess
  mode — a failure is a `reason` field, never an exit code.
- **Do not touch the DSP prompt.** `prompt_builder.py` is already tight on headroom
  (`_MIN_UNFILTERED_HEADROOM`); a separate post-compile call costs nothing when it fails.
- **Input is the captured param table, not the prompt** — real `ParamInfo` (label, kind,
  group, range, unit) + `isInstrument`. That is what makes "every writable param appears
  exactly once" checkable.
- **Read `recommendation.py::constraints_for()` and reuse it** — do not restate the
  mono-voice / no-custom-meter / granular-is-live-input product truths in prose.
  `Components::meter` stays **derived**, never LLM-chosen (PF-052 discards meters upstream
  in `ParamPool`).

**Validation (all host-reachable, mirrored in the Python parser):** `schema` in `[1,3]`;
every writable captured param named exactly once (unknown labels dropped); no
`Button`/`CheckButton` given a continuous style (PF-005 stays structural); no
`Kind::Meter` in `controls`; theme passes A1. Any failure → the caller falls back to
`deriveLayoutFromGroups()`.

**Done when.** Parser tests cover each rejection; `generate.py` dispatches `ui_face`;
`tools/check.sh fast` green. (`audio` level if you touched `llm/` beyond the new file.)

### A3 — `host/Source/GeneratedFaceLookAndFeel.h` wired into `applyUiIr`

**Goal.** A second `LookAndFeel`, constructed from a validated `UiIr::Theme`, attached to
`paramGridPanel` **only**. Faces get their colours; layout is unchanged this session.

**Files.** New `host/Source/GeneratedFaceLookAndFeel.h`; `host/Source/PluginEditor.h`
(member declaration — see lifetime trap); `host/Source/ParamGridPanel.cpp::applyUiIr`
(`:749` — consume `ir.theme`); font bytes into the `PluginForgeAssets` CMake target;
`host/tests/EditorSessionTest.cpp` (scenario).

**Traps — both load-bearing, `~LookAndFeel()` asserts if missed.**
- Declare the member **before** `paramGridPanel` in `PluginEditor.h` (reverse declaration
  order at teardown — the reason `ForgeLookAndFeel lnf` sits where it does).
- Call `paramGridPanel.setLookAndFeel(nullptr)` before the face LnF dies.
- **Never `setDefaultLookAndFeel`** — `ForgeLookAndFeel.h`'s header explains why; the
  title band, prompt column, sample browser and keyboard must keep the shell look.
- **Fonts are a closed enum, not free text.** `Theme.h` documents why a file-scope
  `juce::Font` leaks (static-deinit vs `ScopedJuceInitialiser_GUI`). Each face token is a
  zero-arg function returning a fresh `Font`; `theme.display` / `theme.readout` are an
  enum over the embedded set. Four display faces + one mono covers all four mockups
  (families in `docs/design/incant-ui/README.md` §"Design tokens" → Faces table).
- Knob geometry: arc 1.2π→2.8π (the `setRotaryParameters` values `applyPresentation()`
  already sets), track at 10% ink alpha, value arc in the accent, 4px stroke at 52px dia.
  See `docs/design/incant-ui/GKnob.dc.html`.

**Done when.** A compile with a theme produces a `paramGridPanel` whose `findColour`
values differ from the shell's while the editor's own are unchanged; teardown with the
face attached passes the existing leak check; `tools/check.sh full` green.

### A4 — `host/Source/ArchetypeLayout.h` + `host/tests/ParamGridLayoutTest.cpp`

**Goal.** Make grouped patches read as panels. `layoutSectioned()` today places every
control at `x=0`, one per row, at `kCellH` — a half-width list.

**Files.** New `host/Source/ArchetypeLayout.h` (free functions, no `juce::Component`,
mirroring `ParamGridLayout.h`); `host/Source/ParamGridPanel.cpp::layoutSectioned` +
`contentHeightForSections`; new `host/tests/ParamGridLayoutTest.cpp`.

**Archetypes.** `synth-panel` / `channel-strip` → sections become columns, `span` widens
a column; `tape-unit` → transport-left / tone-right split; `texture-field` → display
region + control rail; `pedal` / `utility` → keep the existing grid.

**Traps.**
- **One height function.** `contentHeightForSections()` is already shared by
  `contentHeightForCurrentMode()` and `layoutSectioned()` (`ParamGridPanel.h:51-57`)
  precisely so they cannot disagree — the `kChromeHeight` defect both headers warn about.
  Extend it; do not add a second.
- `ParamGridLayout.h:10-15` says `ParamGridLayoutTest.cpp` "has never existed" and asks
  the next reader to write it or delete the claim. Write it — cover both the existing
  `ParamGridLayout` arithmetic and the new `ArchetypeLayout` functions.

**Done when.** A 6-param effect and an 18-param synth against each archetype: no
overlapping rects, every control placed exactly once; `tools/check.sh full` green.

### A5 — cache + cockpit export + contact-sheet verification

**Files.** `host/Source/PluginProcessor.cpp` (cache the face JSON in the state blob keyed
on the source hash — additive, no `kStateSchemaVersion` bump); `host/Source/PluginEditor.cpp`
(cockpit export — add archetype + theme to what `PLUGINFORGE_COCKPIT_STATE` writes);
`host/tests/EditorSessionTest.cpp` (theme applied / theme rejected per-token / IR restored
from a reopened project / archetype layout placed every control).

**Verification.** Drive the four worked prompts in `ui_ir_system_prompt.md` through
`tools/ui_iterate.sh`; diff the contact sheet against
`docs/design/incant-ui/screenshots/1a…1d`.

**Done when.** Reopening a saved project restores its face without a regenerate;
`tools/check.sh full` green; the contact sheet is a recognisable match (human eyes —
COLLABORATION.md §1, not delegable).

---

## Track B — shell redesign (ADR-036)

### B0 — prep commit (direction-neutral)

**Goal.** Everything both directions need, landed once.

- Extract `host/Source/ArchetypeLayout.h` if A4 has not already (Track B's lattice wants
  the same width-driven column functions).
- Confirm `contentHeightForSections()` stays the single height function.
- Disclosure buttons → fixed 30px squares (`PluginEditor.cpp::resized()` /
  `PluginEditor.h::Chrome`), so the title truncates under width pressure, not the buttons.

**Done when.** `tools/check.sh full` green; the R1/R2 recreation
(`docs/design/incant-ui/screenshots/shell-effect-900x500.png`,
`shell-instrument-900x786.png`) still matches — the "did not move the chrome" check.

### B1 — two prototype branches off B0

`feat/shell-command-bar` and `feat/shell-rail-dock`, each a full `resized()`
implementation:

- **2a Command Bar** — prompt as a full-width bottom bar; grid as a hairline lattice
  (`repeat(auto-fit, minmax(180px,1fr))`, shared 1px `#383838` borders, section headers
  span `1/-1` with an accent rank number); `span:2` → double-width cell, 62px knob.
  **Amendment (ADR-036 §2): a dedicated right-hand `CodeEditorPanel` region stays** —
  promote it from the hidden bottom-dock child (`PluginEditor.cpp:103, 733-735`) to a
  first-class right column, showing attempted source + highlighted error line
  (`:210-211`) + full stderr. Persistent vs. `{ }`-toggled is the prototype's call.
- **2b Rail + Dock** — 52px left mode rail replaces the disclosure cluster; prompt column
  is a user-dragged dock (persisted width, 280px floor, 40% ceiling — new additive
  state-blob attribute); one 26px status bar absorbs meter + status + sample line.

Both: replace `cols = clamp(ceil(sqrt(N)), 2, 6)` with a width-driven count; Ember tokens
unchanged (this is `resized()`, not repaint); handle the 700×500 minimum per
`2c-both-at-700-minimum.png`.

**Traps.** `kMinWindowW`/`kMinWindowH` may move (2c is 700×500, today's min is 700×400 —
`PluginEditor.h:417, 424`); a `setResizeLimits` change is Tier-2. `Chrome::promptH`'s
0px-collapse comment (`PluginEditor.cpp:678-692`) is the bug being fixed — keep it or
update it, don't leave it describing a layout that no longer exists.

### B2 — pick and converge

Render both through `tools/ui_iterate.sh`, evaluate on the running Standalone, pick one,
merge it, `git branch -D` the other. The draggable-divider "2b-lite" fix is the floor if
neither wins.

**Done when.** One shell in the tree; the other branch deleted; `tools/check.sh full`
green; CLAUDE.md "Key architectural decisions" + file map get a one-line ADR-036 entry
(ungated Tier 1).

---

## Deferred / open

- `setDefaultLookAndFeel` denylist hook for `host/Source/` — hand to `invariant-hook-writer`
  once A3 lands.
- The A1 contrast reference colour (`background` vs `surface`) — resolve before A1.
- `theme.density` sits in the `theme` block though it is an A4 layout concern (per the
  handoff JSON shape) — recorded so it is not read as a bug.
