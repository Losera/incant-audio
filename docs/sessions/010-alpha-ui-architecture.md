# Session 010 — Alpha UI Architecture

**Date:** 2026-08-07
**Status:** Design locked, ready for implementation

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

---

## 10. Verification

- `tools/check.sh fast` — must pass after every commit (unit tests + control wiring)
- `tools/check.sh full` — must pass before push (prompt grounding, build, TSan)
- `EditorSessionTest` — scenario 27 (QWERTY), plus new scenarios for history/progress
- `UiDesignGallery` — regenerate PNGs to show the new palette and 65/35 split
- Manual: open Standalone, verify the palette shift, keyboard controls, prompt history, error expansion
- Manual: open browser dev-cockpit, verify prompt input → generation → status update

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
