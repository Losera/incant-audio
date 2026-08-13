# Session 010 — Alpha UI Architecture

**Date:** 2026-08-07
**Status:** Design locked, ready for implementation

**2026-08-13 RECONCILIATION NOTE (C7).** This plan was written against a mental
model, not a reading, of the code, and implementation (sessions after 010,
through C1-C6 and the subsequent merge with main's "deterministic plugin
families and sample browser" work) both corrected several of its numbers and
deliberately deviated from parts of it. Amended in place, per section, below
-- the original text stays as the record of what was decided and why; each
note says what actually shipped and, where it differs, why. Do not treat any
number or claim in the unmarked sections below as current without checking
the note for that section first.

---

## 1. Decisions Made

| Decision | Choice | Rationale |
|----------|--------|-----------|
| Browser model | Dev-cockpit mirror (localhost browser mirrors plugin state) | ADR-025, confirmed. The plugin is the runtime; the browser is the prompt-iteration surface. |
| Layout split | Grid-dominant 65/35 (left grid / right prompt) | Grid holds sectioned UiIr previews and needs room for multi-column layouts; prompt is a text field + buttons that fits in 35%. |
| iPlug2 | Rejected (confirmed). Stay with JUCE. | Session 008 analysis stands: 2-3 month rewrite, quality gap is about design not framework. Note: JUCE 7.0.9 has no CLAP wrapper (the session 008 claim was inaccurate), but CLAP is out of scope for alpha. |
| Palette | Tokyo Night — electric blue accent, amber hot | Shift from Catppuccin Mocha (teal/pink) to Tokyo Night (blue/amber) while keeping the dark-mode foundation. New Theme.h tokens. |
| Keyboard | Fix QWERTY (Broken #1) + inline octave/scale/velocity controls | Close the static-contract-only test gap. Add octave up/down buttons and a scale dropdown in the keyboard band. Velocity via mouse Y-position on on-screen keys. |
| Alpha scope | Keyboard + Code editor + Prompt workflow (NOT visual design system, NOT UiIr emission) | Functional improvements, not a design-system polish pass. The palette shift is the visual change. |
| Sectioned layout | Hand-authored only (Phase 1a). LLM does not emit UiIr. | Keep the flat sqrt-grid as default. Sectioned layout activates when hand-authored IR is provided. |
| Window resize | Adjust minimum to 700x400 for 65/35 split | 455 grid + 235 prompt + margins + divider = ~700px minimum width. Max stays 1600x1200. |
| Prompt workflow | All three: history + generation progress + better error display | History dropdown (last 10-20 prompts, up-arrow recall). Attempt counter (1/3, 2/3, 3/3). Expandable multiline error region with copy button. |
| Dev-cockpit scope | Prompt iteration surface | Browser edits prompts → sends to plugin → sees result. Plugin handles params/keyboard. |

---

## 2. Palette: Tokyo Night

Replace Catppuccin Mocha tokens in `Theme.h` with Tokyo Night palette. The structural role of each token stays identical — only the hex values change.

| Token | Old (Catppuccin) | New (Tokyo Night) | Role |
|-------|------------------|-------------------|------|
| `base` | `#1e1e2e` | `#1a1b26` | Window background |
| `mantle` | `#181825` | `#16161e` | Panel/code-editor background |
| `crust` | `#11111b` | `#0f0f17` | Recessed wells (meter track, line numbers) |
| `text` | `#cdd6f4` | `#c0caf5` | Primary text |
| `subtext` | `#9399b2` | `#a9b1d6` | Secondary text (panel headers) |
| `overlay` | `#6c7086` | `#565f89` | Tertiary/dim text (line numbers) |
| `yellow` | `#f9e2af` | `#e0af68` | In-progress / working state |
| `errorText` | `#f2c9d3` | `#f7768e` | Error region text |
| `meterCool` | `#94e2d5` | `#7aa2f7` | Blue — low end of level meter |
| `meterHot` | `#f38ba8` | `#f9e2af` | Amber — hot end of level meter |

The ForgeLookAndFeel ColourScheme maps directly: `meterCool` becomes the `defaultFill` (accent), `meterHot` stays `highlightedFill`. The visual identity shifts from teal/pink to blue/amber while retaining the same structural roles.

**2026-08-13 RECONCILIATION NOTE.** Every token name above was renamed on
implementation (`host/Source/Theme.h`) to describe UI role rather than
palette family, so a future palette change stays a one-file edit:
`base`->`background`, `mantle`->`surface`, `crust`->`surfaceSunken`,
`text`->`textPrimary`, `subtext`->`textSecondary`, `overlay`->`outline`,
`yellow`->`progress`, `errorText`->`danger`, `meterCool`->`accent`. A new
token, `surfaceRaised` (`#242536`, cards and raised controls), was added with
no equivalent here. One value deliberately deviates from this table, flagged
in the approved implementation plan: this table sets `meterHot` to `#e0af68`
-- identical to `yellow`/`progress`. That is a real role collision (the
meter's hot end and the "generation in progress" state would read as the same
colour whenever both are on screen), so the shipped `meterHot` is Tokyo
Night's orange `#ff9e64` instead; `progress` kept `#e0af68`. Every other value
in the table above matches what shipped. See `Theme.h`'s own header comment
for the measured contrast ratios that were not part of this plan.

---

## 3. Layout: 65/35 Split

### Current (50/50)

```
kLeftFraction = 0.5f
splitW = windowWidth - 2*margin(16) - dividerW(4)
leftW  = (splitW - dividerW) * 0.5 = ~440px at 900w
rightW = splitW - dividerW - leftW = ~440px
```

### Proposed (65/35)

```
kLeftFraction = 0.65f
splitW = windowWidth - 2*margin(16) - dividerW(4)
leftW  = (splitW - dividerW) * 0.65 = ~572px at 900w
rightW = splitW - dividerW - leftW = ~312px
```

### Minimum window width

```
minW = 2*margin(16) + dividerW(4) + minLeftW + minRightW
     = 36 + 400 + 264 = 700px
```

- `minLeftW = 400` — enough for a 4-column grid (4 × 95px + gaps)
- `minRightW = 264` — enough for the prompt text (220px band) + controls
- `kMinWindowH` stays 400

### Chrome struct changes

```cpp
// PluginEditor.h — Chrome struct update
struct Chrome
{
    int margin      = 16;
    int titleH      = 32;
    int dividerW    = 4;
    // Right column — prompt band shrinks slightly to fit 35%
    int promptH     = 220;  // unchanged — vertical budget is independent of width
    int gapMeter    = 8;
    int meterH      = 14;
    int gapRow      = 10;
    int rowH        = 24;
    // Full-width bottom bands — unchanged
    int gapKeyboard = 8;
    int keyboardH   = 72;   // was 64 — grows to accommodate inline octave/scale controls
    int gapCode     = 8;
    int codeH       = 240;
};

static constexpr float kLeftFraction = 0.65f;  // was 0.5f
static constexpr int kMinWindowW     = 700;    // was 800
```

### Static assert update

```cpp
static_assert(rightColumnHeight(Chrome{}) == 276);  // unchanged
static_assert(verticalChrome(Chrome{}) == 144);     // was 136: keyboardH 64→72
```

**2026-08-13 RECONCILIATION NOTE.** This section's arithmetic was wrong twice
over, and both errors were corrected in the code rather than here until now.
First: `minLeftW(400) + minRightW(264) = 664`, not the `700` the "Minimum
window width" heading claims -- that 700 is actually
`2*margin(32) + minLeftW + minRightW` with the divider folded into
`minLeftW`'s 400, a 60/40 split at 700px, not 65/35 (corrected in `d26e990`'s
commit message and `PluginEditor.h`'s own comment, which spells out the real
arithmetic: `splitW = 700 - margin*2(32) = 668; leftW = round((668 -
dividerW(4)) * 0.65) = 432; rightW = 668 - 4 - 432 = 232`). Second, and
unrelated to the first: the disclosure row (`codeToggle`/`styleToggle`, plus
the now-removed `auditionSelector`) moved into the title band on `a087af2`
because 65/35 left the right column too narrow to hold both it and
`PromptPanel`'s own button row -- so `rightColumnHeight` DROPPED `gapRow(10) +
rowH(24)`, landing at **242**, not the 276 both this section and the
`static_assert` above still claim. `verticalChrome` moved a third time, for a
reason this plan never anticipated: main's "deterministic plugin families and
sample browser" PR added a `SampleBrowserPanel` full-width band
(`gapSamples(8) + samplesH(64)`) that landed via the 2026-08-13 merge
alongside `keyboardH`'s 72. Current values, both verified against
`PluginEditor.h`/`.cpp` as of that merge:
```cpp
static_assert(rightColumnHeight(Chrome{}) == 242,
              "promptH(220) + gapMeter(8) + meterH(14) = 242.");
static_assert(verticalChrome(Chrome{}) == 216,
              "margin(16) + titleH(32) + gapSamples(8) + samplesH(64) "
              "+ gapKeyboard(8) + keyboardH(72) + margin(16) = 216.");
```

---

## 4. Keyboard: Inline Controls + QWERTY Fix

### Broken #1 closure

Add an end-to-end test that fires a real QWERTY keypress via `juce::MidiKeyboardComponent::keyPressed()` and asserts the note arrives at the processor. Two challenges:
1. No `wtype`/`ydotool`/`xdotool` on the machine — use JUCE's `KeyPress` simulation directly via the component's `keyPressed()` handler (message-thread call, same as a real keypress)
2. The test must verify the full path: `keyPressed()` → `MidiKeyboardState::noteOn()` → `handleNoteOn()` → `pushKeyboardNote()` → `NoteRing` → `processBlock` drain

New `EditorSessionTest::scenario27_qwertyEndToEnd`:
- Construct editor, call `processor.prepareToPlay()`
- Simulate a keypress (e.g. 'a' key, which maps to MIDI note 48 per JUCE's default mapping)
- Assert `processor.lastKeyboardNote() == 48` (add a test accessor to PluginProcessor)
- Assert `processor.lastKeyboardVelocity() > 0.0f`

### Inline controls

Add to the keyboard band (above the piano keys, inside the same `KeyboardPanel`):

```
┌─────────────────────────────────────────────────┐
│ [◀] [Oct: 4] [▶]  [Scale: Chromatic ▾]        │
│ ╔═══════════════════════════════════════════════╗│
│ ║  C   D   E   F   G   A   B   C'              ║│
│ ║  Q   W   E   R   T   Y   U   I               ║│
│ ╚═══════════════════════════════════════════════╝│
└─────────────────────────────────────────────────┘
```

- **Octave buttons**: `[◀]` / `[▶]` flanking the current octave number. Clicking shifts the `MidiKeyboardComponent` range and `setKeyPressBaseOctave()` by ±1. Range clamped 0-8.
- **Scale dropdown**: ComboBox with predefined scales. When selected, only keys in the scale are enabled (non-scale keys dimmed/hidden). Scales: Chromatic, Major, Minor, Pentatonic, Blues, Dorian, Mixolydian.
- **Velocity via mouse Y**: On the on-screen `MidiKeyboardComponent`, map the Y position of the click to velocity (top = 127, bottom = 40). Override `mouseDown` in a custom keyboard component subclass.

### KeyboardPanel height

Grow `keyboardH` from 64 to 72 to accommodate the control row. The control row is ~24px (buttons + dropdown), the piano keys remain ~48px.

**2026-08-13 RECONCILIATION NOTE.** The Broken #1 closure plan above is
impossible as written and was abandoned, not implemented: `keyPressed()` is
not the method that fires notes from a real QWERTY press --
`juce::MidiKeyboardComponent::keyStateChanged()` is (continuous held-state
polling, a different JUCE virtual entirely) -- and the real defect (C4) was
never a missing test but a missing SHELL-LEVEL route: `PluginForgeEditor` had
no `keyStateChanged()` override at all, so JUCE's own dispatch walk never
reached the piano unless it already held keyboard focus. The fix added
exactly that override, forwarding unconditionally to
`KeyboardPanel::routeKeyStateChanged()`, and scenario 28 proves the shell
routes on every key transition -- what it does NOT and cannot prove is that a
real OS keypress reaches this call at all (`KeyPress::isCurrentlyDown()`
reads actual compositor state; no synthetic-input tool exists on this
machine). This narrows Broken #1, it does not close it, and no test in this
repo closes it. The **scale dropdown was cut** (C5, user's explicit answer):
no supporting JUCE API exists for it, and building one would need a
`drawWhiteNote`/`drawBlackNote`-overriding subclass, materially more work than
this section implied. Octave buttons (`[<]`/`[>]`) and mouse-Y velocity both
shipped as described, with one correction: velocity-by-mouse-Y is one ctor
line (`setVelocity(1.0f, true)`, `juce_MidiKeyboardComponent.h:73`), not a
custom keyboard-component subclass. `keyboardH` did grow 64->72 as planned,
and the control row IS 24px (`KeyboardPanel::kControlRowH`) with the piano at
48px -- but the control row holds only the octave buttons, since the scale
dropdown never shipped.

---

## 5. Prompt Workflow: History + Progress + Errors

### Prompt history

Add to `PromptPanel`:
- `juce::StringArray promptHistory` — last 20 prompts, persisted in state blob (Phase 1)
- `juce::ComboBox historyDropdown` — populated from `promptHistory`, visible after first generation
- Up-arrow in `promptInput` cycles through history (newest first)
- On generation submit, the prompt is prepended to `promptHistory` (deduplicated)
- `submitPromptForTest` / `historyCountForTest` test accessors

### Generation progress

Replace the single "Generating..." / "Ready." state label with a richer status display:
- **Attempt counter**: "Generating (1/3)..." → "Compiling (1/3)..." → "Generating (2/3)..." etc.
- **Phase indicator**: Shows current phase: `Generating` → `Compiling` → `Success` / `Error`
- **Visual progress**: A thin animated bar (not determinate — indeterminate animation during generation, determinate during compile)
- The existing `statusLabel` already receives state transitions from `runGeneration()` — add the attempt number and phase strings to those transitions

### Better error display

Replace the 200-char truncation in `errorTextForTest()`:
- Expand `errorBox` to show full Faust stderr (already exists as a `juce::TextEditor` but truncated)
- Add a "Copy" button next to the error region
- Parse Faust stderr for line numbers and show them in the error text (the retry loop already gets the full stderr)
- Keep the 200-char truncation in the status label (brief feedback), but show the full error in the expandable region below

**2026-08-13 RECONCILIATION NOTE.** All three pieces of this section shipped,
eventually (C6, 2026-08-12), but not as specced, and prompt history in
particular is already built and this doc's own §9 (below) still calls it
deferred -- read that note too.

- **History**: no `historyDropdown` ComboBox. The shipped mechanism is the
  pre-existing `historyButton` + `PopupMenu` (a dropdown would have
  duplicated it), plus up-arrow CYCLING through entries (a walking index,
  clamped at the oldest, reset on any real edit) rather than always
  recalling just the newest. It is also now genuinely persisted in the state
  blob (schemaVersion 3, a `<PromptHistory>` child) -- this section's
  "persisted in state blob (Phase 1)" was aspirational when written; Phase 1
  (state persistence) had not landed yet at the time, but had landed
  (`c34bbb6`) before C6 shipped this specific field.
- **Generation progress**: the attempt counter (1/3, 2/3, 3/3) was CUT --
  it contradicts "overseer FLEET ruling #2a" (`PromptPanel.h`'s own citation):
  the one-shot subprocess exposes no live attempt count, so a counter would
  be fabricated, not observed. What shipped is a single indeterminate pulse
  label ("Working... Ns"), no separate phase indicator
  (Generating/Compiling/Success/Error) beyond that.
- **Error display**: the "Copy" button was never built. Everything else
  shipped: `setError()` now actually gets called with the FULL,
  untruncated Faust stderr (it existed as a public method, documented for
  exactly this, since before this session -- nothing called it until C6),
  the status label keeps the 200-char-capped prefix, and the offending line
  is parsed and highlighted in the code view. One correction to "Parse Faust
  stderr for line numbers": the format is not a fixed `line N:` -- Faust's
  own diagnostic reads `dsp:<line> : ERROR : ...`, and the spacing around
  the first colon is NOT stable across Faust versions (this machine's
  2.85.9 omits the space; CI's installed Faust includes it) -- found because
  the naive parser passed every local test and then silently failed in CI.

---

## 6. Code Editor Polish

### Basic syntax highlighting

JUCE ships `CPlusPlusCodeTokeniser` but no Faust tokenizer. Options:
1. **Generic tokenizer (monochrome)** — already available, zero effort
2. **Keyword-highlight tokenizer** — regex-based, ~50 lines, highlighting Faust keywords (`process`, `hslider`, `vslider`, `with`, `import`, `declare`, `effect`, `generator`), comments (`//`), strings, and numbers
3. **Full tokenizer** — subclass `CodeTokeniser`, implement `TokenType` identification for Faust syntax. More work, but proper IDE-grade highlighting.

**Alpha choice: option 2** — regex-based keyword highlighting in a custom `CodeEditorComponent` subclass. Sufficient for "readable Faust code" without the full tokenizer investment.

### Line numbers

Add line numbers to the left gutter of the code view. `CodeEditorComponent` supports this natively via `setLineNumbersVisible(true)`.

### Error line highlighting

Parse Faust stderr for line numbers (format: `line N: error: ...`) and highlight the offending line in the code view. Add a `highlightErrorLine(int line)` method to `CodeEditorPanel`.

### Keyboard shortcut

Add `Cmd/Ctrl+Shift+C` to toggle code visibility (replaces the "Show code" button click for power users). The button stays for discoverability.

**2026-08-13 RECONCILIATION NOTE.** The "Alpha choice: option 2" (regex-based
Faust keyword highlighting) was never built. As of this note the shipped
editor still uses option 1 -- a `nullptr` tokeniser, monochrome
(`CodeEditorPanel.h`'s own comment explains why: JUCE ships no Faust
tokeniser, and a hand-written one is still-deferred future work, not
something this alpha attempted). **Line numbers needed no work at all**:
`CodeEditorComponent` shows them by default (`juce_CodeEditorComponent.cpp:
468`); there was never a flag to flip. Error line highlighting DID ship
(C6) -- `CodeEditorPanel::highlightErrorLine(int)`, parsing Faust's `dsp:
<line>` diagnostic (see §5's note above on why that parse has to tolerate
version-dependent spacing) and selecting/scrolling to the line via the
editor's own text-selection highlight, not custom paint code. It also pushes
the ATTEMPTED source into the code view on a compile failure, not just the
last successful one (`PF-022` keeps the source-of-record pointed at the last
SUCCESS, so without this the highlight would point at an arbitrary line in
unrelated, already-working code). The `Ctrl+Shift+C` shortcut shipped as
specced (C6), as a `PluginForgeEditor::keyPressed()` override -- a genuinely
different JUCE virtual from the keyboard-routing `keyStateChanged()` override
C4 added (one-shot press/release chord vs. continuous held-state polling);
the two are deliberately not unified into one override.

---

## 7. Dev-Cockpit: Prompt Iteration Surface

### Current state

- `dev-cockpit/server.py` — localhost HTTP server, port 8765
- `dev-cockpit/static/index.html` — two-panel: live screenshot (auto-refresh 2s) + state JSON (auto-refresh 100ms)
- Plugin writes state JSON at ~10Hz via `writeCockpitState()`

### Alpha additions

**Prompt input in browser:**
- Add a text input + Generate button to the browser surface
- On submit, the browser POSTs the prompt to a new `/api/generate` endpoint
- `server.py` writes the prompt to a temp file and spawns `generate.py --prompt <file>` (mirrors the plugin's own subprocess bridge)
- The browser shows generation progress by polling `/api/state` (which already mirrors the plugin's status)

**Live prompt sync:**
- When the user types in the plugin's prompt box, the browser mirrors the current prompt text
- When the user types in the browser's prompt input, the browser shows a "Send to plugin" button that copies the prompt into the plugin's prompt box (via the state mirror's write path — new `/api/set-prompt` endpoint)

**Theme preview (read-only):**
- Display the current Theme.h palette values as color swatches in the browser
- This is observation-only for the alpha — no runtime theme editing

**2026-08-13 RECONCILIATION NOTE.** Only the read-only half of this section
shipped, and even that took until C3 (2026-08-12): `setCockpitStatePath()`
had zero callers anywhere in the repo until then, so `cockpitEnabled` was
permanently false and `/api/state` could only ever 503 no matter what any
doc claimed. C3 armed it (opt-in via `PLUGINFORGE_COCKPIT_STATE`), so the
mirror this section's "Current state" describes now genuinely works. The
**prompt input, live sync, and both new endpoints (`/api/generate`,
`/api/set-prompt`) were never built** and are explicitly deferred to a
separate architecture conversation, not merely unstarted: a synchronous
generate in `server.py`'s single-threaded `HTTPServer` would block the
100ms `/api/state` poll, and a browser-triggered generation would race the
plugin's own generation subprocess for the same free-tier quota with no
mutual exclusion anywhere. Neither problem has a stated solution. One more
correction, independent of the above: this section's implied CLI shape
(`generate.py --prompt <file>`) is not what the plugin's own bridge uses --
`--prompt` takes the prompt TEXT directly; the JSON-payload-in-a-file mode
this section actually needs is `--request-file <path>`, a separate flag.
Theme swatches were never built either.

---

## 8. Files Touched (Estimated)

### Core layout + palette
| File | Change |
|------|--------|
| `host/Source/Theme.h` | Tokyo Night palette (10 colour values) |
| `host/Source/PluginEditor.h` | `kLeftFraction = 0.65f`, `kMinWindowW = 700`, `keyboardH = 72` |
| `host/Source/PluginEditor.cpp` | `setSize(900, 500)` → `setSize(900, 500)` (no change), `setResizeLimits(700, 400, 1600, 1200)`, static_assert update |
| `host/Source/ForgeLookAndFeel.h` | ColourScheme — swap `meterCool`→blue, `meterHot`→amber |

### Keyboard improvements
| File | Change |
|------|--------|
| `host/Source/KeyboardPanel.h` | Add octave buttons, scale ComboBox, velocity-Y override |
| `host/Source/KeyboardPanel.cpp` | Implement octave shift, scale filtering, velocity mapping, `resized()` for control row |
| `host/tests/EditorSessionTest.cpp` | `scenario27_qwertyEndToEnd` |
| `host/Source/PluginProcessor.h` | `lastKeyboardNote()` / `lastKeyboardVelocity()` test accessors |

### Prompt workflow
| File | Change |
|------|--------|
| `host/Source/PromptPanel.h` | `promptHistory`, `historyDropdown`, progress state machine |
| `host/Source/PromptPanel.cpp` | History populate/cycle, progress display, expanded error region, copy button |

### Code editor
| File | Change |
|------|--------|
| `host/Source/CodeEditorPanel.h` | `highlightErrorLine()`, keyword tokenizer |
| `host/Source/CodeEditorPanel.cpp` | Regex-based Faust keyword highlighting, line numbers, error highlighting |

### Dev-cockpit
| File | Change |
|------|--------|
| `dev-cockpit/server.py` | `/api/generate` POST endpoint, `/api/set-prompt` POST endpoint |
| `dev-cockpit/static/index.html` | Prompt input + Generate button, live prompt sync, theme swatches |

### Tests
| File | Change |
|------|--------|
| `host/tests/EditorSessionTest.cpp` | Scenario 27 (QWERTY end-to-end), scenarios for history/progress |
| `tests/test_control_wiring.py` | No new hooks needed (this is Tier 1 — UI, not audio-thread) |

---

## 9. What Is NOT In This Alpha

- **Visual design system pass** (session 002 Part B: ForgeLookAndFeel drawing overrides beyond ColourScheme) — deferred
- **LLM-emitted UiIr** (session 002 B3, ADR-024 Phase 1b) — deferred
- **State persistence** (session 002 Phase 1 / P11) — deferred (prompt history won't persist until this lands)
- **Draggable splitter** — deferred (65/35 is fixed)
- **Full Faust tokenizer** — deferred (regex highlighter only)
- **CLAP output** — deferred (JUCE 7.0.9 has no CLAP wrapper; needs JUCE 8 or clap-wrapper submodule)
- **iPlug2 integration** — confirmed rejected

**2026-08-13 RECONCILIATION NOTE.** The "State persistence... deferred"
bullet was already false by the time most of this plan's other sections
shipped: P11 landed in `c34bbb6`, well before session 010's own
implementation work began, and `CLAUDE.md` had already flagged the doc that
kept calling it an empty stub as "worse than a missing one, because a reader
cannot tell which half to trust." Prompt history specifically DID land in
the persisted blob (C6, schemaVersion 3, `<PromptHistory>`) -- the
parenthetical here is simply no longer true. The other five bullets remain
accurate as of this note: none of the visual-system pass, UiIr emission,
draggable splitter, full tokenizer, or CLAP output have shipped.

---

## 10. Verification

- `tools/check.sh fast` — must pass after every commit (unit tests + control wiring)
- `tools/check.sh full` — must pass before push (prompt grounding, build, TSan)
- `EditorSessionTest` — scenario 27 (QWERTY), plus new scenarios for history/progress
- `UiDesignGallery` — regenerate PNGs to show the new palette and 65/35 split
- Manual: open Standalone, verify the palette shift, keyboard controls, prompt history, error expansion
- Manual: open browser dev-cockpit, verify prompt input → generation → status update

**2026-08-13 RECONCILIATION NOTE.** The first bullet is wrong and was known
to be wrong well before this note: `tools/check.sh fast` is pytest-only (no
build, no C++), so it cannot see a broken `static_assert` or anything else
in `host/`, and this entire body of work is C++. Every commit across C1-C6
was gated on `tools/check.sh full` instead, the level that actually
configures cmake and builds. "Manual: open Standalone" and "open browser
dev-cockpit" both remain genuinely undone as of this note -- every session
since has flagged them as non-delegable and none has closed them; see the
live `STATUS.md` "Waiting on you" section, not this doc, for their current
status.

---

## 11. Open Questions for Implementation

None remaining — all design decisions are locked via the three question rounds above. The plan is ready for implementation.

**Implementation order (recommended):**
1. Palette shift (Theme.h + ForgeLookAndFeel.h) — smallest diff, highest visual impact
2. 65/35 layout split (PluginEditor.h/cpp) — structural change, tested by UiDesignGallery
3. Keyboard inline controls + QWERTY fix (KeyboardPanel.h/cpp + test)
4. Prompt workflow (PromptPanel.h/cpp + test)
5. Code editor polish (CodeEditorPanel.h/cpp)
6. Dev-cockpit prompt iteration (server.py + index.html)
