# Session 002 handoff — Refine loop (done) + UI design system (Theme.h landed, rest open)

Read `docs/sessions/002-refine-loop-and-ui-redesign.md` first — this file is a short
pointer into it, not a replacement. That file has the full approved plan (frozen, "## 1.
Plan, as approved") and the execution record (current, updated through Part A's landing).

## Where things stand right now

**Part A (A1–A8, the refine loop) is fully landed, committed, and pushed to local `main`**
(not yet pushed to `origin` — 9 commits ahead as of the last check). Three commits:
`d85ae37` (A1), `3a94080` (A2/A3/A6), `5090b55` (A4/A5/A8). `tools/check.sh full` was green
after each. Refine now actually sends the prior Faust source to the LLM — see the session
doc's execution record for the full description.

**Part B is starting.** `host/Source/Theme.h` exists on disk and is wired into
`PluginEditor.cpp`, `PromptPanel.cpp`, `CodeEditorPanel.cpp`, `ParamGridPanel.cpp` (B1).
**This is verified but NOT YET COMMITTED**:

- Verified: `ninja EditorSessionTest PluginForgeHost_VST3 PromptPanelThreadingTest` builds
  clean; `EditorSessionTest` passes 143/143 with **no leak warnings**;
  `PromptPanelThreadingTest` passes 9/9.
- **A real bug was caught and fixed during B1**: the first version of `Theme.h` declared
  the `Type::*` font tokens as `inline const juce::Font` namespace-scope globals.
  `juce::Font` holds a `ReferenceCountedObjectPtr<SharedFontInternal>`
  (`juce_Font.h:483`) that caches a `Typeface`; a global-scope `Font`'s destructor runs at
  static-deinit time, *after* `ScopedJuceInitialiser_GUI` (a `main()`-local) has already
  torn down JUCE's typeface cache. `EditorSessionTest` caught this immediately as a leak
  (`Typeface`/`OwnedArray`/`CustomTypeface`/`Path`, ~180 objects). Fixed by making every
  `Theme::Type::*` token a zero-arg function returning a fresh `Font` instead of a stored
  global — the same shape `LookAndFeel::getLabelFont()`-style overrides already use.
  `Theme::` colour tokens are unaffected (`juce::Colour` wraps a plain `uint32`, which is
  exactly what `juce::Colours::red` etc. already do as raw globals — verified against
  `juce_Colours.h`).
- **Not re-run since B1 landed**: `tools/check.sh full` (a background run timed out at
  280s without a fail — inconclusive, not a red signal, but not a green one either).
  Re-run it before committing B1.
- Suggested commit message shape once verified: `B1: Theme.h -- named colour/type tokens
  replace the 15 scattered inline-hex/bare-size call sites`, crediting the Font-global leak
  finding explicitly (it's a real, non-obvious JUCE trap worth recording for the next
  person who reaches for a global `Font`).

**B2–B6 have not been started.** Full designs for each are in the frozen plan text,
section "Part B — a real native design system", in the session doc. Short version:

- **B2.** `host/Source/ForgeLookAndFeel.h` (new, header-only `LookAndFeel_V4` subclass).
  Every virtual it needs to override was verified this session against the real JUCE 7.0.9
  headers (not recalled) — citations below, so the next agent doesn't have to re-derive
  them:
  - `drawRotarySlider`, `drawLinearSlider*`, `getSliderThumbRadius`, `createSliderButton`,
    `createSliderTextBox` — all pure virtuals in `Slider::LookAndFeelMethods`,
    `juce_Slider.h:922-961`.
  - `drawButtonBackground`, `drawToggleButton`, `drawTickBox`, `getTextButtonFont` —
    `Button::LookAndFeelMethods`, `juce_Button.h:395,406,411,398`.
  - `drawLabel`, `getLabelFont` — `Label::LookAndFeelMethods`, `juce_Label.h:280-281`.
  - `fillTextEditorBackground`, `drawTextEditorOutline` — `TextEditor::LookAndFeelMethods`,
    `juce_TextEditor.h:703-704`.
  - `drawScrollbar`, `getDefaultScrollbarWidth` — `ScrollBar::LookAndFeelMethods`,
    `juce_ScrollBar.h:375,390`.
  - Worth knowing before writing the rotary/linear slider overrides:
    `LookAndFeel_V4::drawRotarySlider` (`juce_LookAndFeel_V4.cpp:1064-1112`) is already a
    clean flat-arc design driven entirely by `findColour()` lookups
    (`Slider::rotarySliderOutlineColourId/rotarySliderFillColourId/thumbColourId`) — no
    custom geometry needed for a first pass. A `ColourScheme` built from `Theme::` tokens
    via `setColourScheme(...)` may get most of the "looks unstyled" problem solved before
    any bespoke `drawXxx` override is written at all. Confirm this by eye
    (`tools/ui_contact_sheet.py` / the Standalone) before spending time hand-rolling
    geometry V4 already gives you for free.
  - Installation trap (already verified, in the plan text): `setLookAndFeel(&lnf)` in
    `PluginForgeEditor`'s constructor, **not** `setDefaultLookAndFeel` (process-global,
    unsafe when the plugin shares a process with the DAW and other instances of itself).
    `lnf` must be a member declared *before* the three child panels in `PluginEditor.h`
    (so it outlives them), and `setLookAndFeel(nullptr)` must be the first line of
    `~PluginForgeEditor` (`~LookAndFeel` asserts if anything still points at it).
- **B3.** Sectioned layout from Faust group metadata (`ParamGridPanel.h:110-111` already
  captures it, nothing lays out by it yet). Pure-JUCE `sectionsFor(...)` in
  `ParamGridLayout.h`; a sectioned branch in `ParamGridPanel`'s
  `contentHeightForCurrentMode()`/`layoutControls()`. **Critical constraint**: must not
  reorder `controls` itself — every `*ForTest` accessor and `EditorSessionTest` scenario 2's
  by-position assertions depend on index stability.
- **B4.** Recommend the system font stack (no font-embedding work); flag embedding as a
  build-dependency question if ever wanted. Effectively already covered by B1's
  `Theme::Type` scale.
- **B5.** New `host/tests/ParamGridLayoutTest.cpp` (pure unit tests for `sectionsFor`) +
  one `CMakeLists.txt` target. Run `tools/ui_iterate.sh` **without** `--accept` first,
  confirm the diff matches the prediction (`window` changes only for the two `*_grouped`
  fixtures), then accept.
- **B6.** Full `tools/check.sh full` gate, then commit. Recommend one commit per B-item
  (B2, B3, B5) rather than one giant Part-B commit, matching how Part A landed.

## Agentic workflow to parallelize B2–B6

**Why parallelize at all**: B2 (`ForgeLookAndFeel.h`, `PluginEditor.h/.cpp`) and B3
(`ParamGridLayout.h`, `ParamGridPanel.h/.cpp`) touch **disjoint files** — verified against
the plan's own "Files touched" table. They have no data dependency on each other either:
B2 is chrome (how a widget draws), B3 is placement (where a widget goes). Nothing about
one constrains the other's implementation.

```
Phase 1 (parallel, isolated worktrees)
├── Agent A → B2: ForgeLookAndFeel.h + PluginEditor wiring
└── Agent B → B3: sectionsFor() + ParamGridPanel sectioned layout
        (both start from the same commit: current HEAD after B1)

Phase 2 (sequential, single agent, after BOTH Phase 1 branches merge)
└── B5: ParamGridLayoutTest.cpp (needs B3's sectionsFor to exist),
         + tools/ui_iterate.sh verification (needs BOTH B2's new chrome and
           B3's new layout on screen to capture the real target state —
           running it against only one half would capture an intermediate,
           throwaway visual state)

Phase 3 (sequential)
└── B6: tools/check.sh full, then commit (recommend one commit per landed
         B-item during Phase 1/2, so B6 here is mostly the final gate + a
         last look, not a big-bang commit)
```

**How to actually run Phase 1 in this harness**: two `Agent` calls in the *same* message
(so they run truly in parallel, not sequentially), each with `subagent_type: "claude"` (or
omitted) and `isolation: "worktree"` so each gets its own git worktree off current `main`
and cannot clobber the other's uncommitted edits. Merge order doesn't matter (disjoint
files ⇒ no conflict); whichever lands first, the other rebases trivially. Do **not** run
B5 or B6 as part of either Phase 1 agent's task — both need the *other* branch's output
first.

**Prompt to hand each Phase 1 agent** (fill in the bracketed part; everything else is
common context both need):

> You're implementing **[B2: `host/Source/ForgeLookAndFeel.h` / B3: sectioned Faust-group
> layout]** from PluginForge's session doc `docs/sessions/002-refine-loop-and-ui-redesign.md`
> — read that file's "## 1. Plan, as approved" section, specifically **[Part B, item B2 /
> Part B, item B3]**, for the full cited design (JUCE header line numbers, the exact
> constraint list). Also read `docs/sessions/002-handoff-README.md` for the short version
> and the verified virtual-method citations already worked out this session — don't
> re-derive them. Build and run `EditorSessionTest`
> (`cmake --build host/build --target EditorSessionTest && ./host/build/
> EditorSessionTest_artefacts/Debug/EditorSessionTest`) to verify: 0 failures, and **check
> for JUCE leak-detector output at the end** — this session found a real leak (global
> `juce::Font` objects; see the README) and that class of bug is easy to reintroduce.
> Report back a five-line change report (COLLABORATION.md §4: CHANGED / WHY / VERIFIED /
> RISK / YOUR MOVE) when done. Do not touch files the other half (B2 vs B3) owns — they
> are disjoint by design; if you find you need to, stop and flag it rather than guessing.

## What's genuinely unverified, stated per COLLABORATION.md §3

- Whether the model actually honours a folded-in `prior_source` in production (Part A's
  stated remainder — no test in this repo can prove it; needs one live groq run with a
  marker control).
- Whether `Theme.h`'s wiring survives a full `tools/check.sh full` run — manually verified
  a subset (host build + `EditorSessionTest` + `PromptPanelThreadingTest`), not the full
  ladder (prompt grounding, TSan, render oracle, pitch gate are all irrelevant to a
  colour/font header, but "irrelevant" is an assumption, not a measurement).
- Whether B2's ColourScheme-only approach (see the B2 note above) is actually sufficient,
  or whether bespoke `drawXxx` overrides are needed — not tried, only reasoned about from
  reading `LookAndFeel_V4.cpp`'s existing implementation.
