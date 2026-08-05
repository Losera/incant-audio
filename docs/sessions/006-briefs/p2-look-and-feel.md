touches:  host/Source/ForgeLookAndFeel.h (new file), host/Source/PluginEditor.h,
          host/Source/PluginEditor.cpp
depends:  none (no CONTRACT.md covers PluginEditor)
provides: none (no CONTRACT.md exists yet for PluginEditor construction/destruction order;
          if you judge one is now warranted, say so in your change report rather than
          writing one unprompted -- CONTRACT.md creation is a documented pattern in this
          repo but is out of this brief's stated scope)

This brief implements B2 from session 002: a JUCE LookAndFeel_V4 subclass giving generated
plugins a styled, non-default look.

## Read first, in this order

1. docs/sessions/002-refine-loop-and-ui-redesign.md, lines 355-380, IN FULL. This is the
   full scope of B2: every LookAndFeel_V4 virtual to override (drawRotarySlider,
   drawLinearSlider, getSliderThumbRadius, createSliderTextBox, drawToggleButton +
   drawTickBox, drawButtonBackground, getTextButtonFont, drawLabel + getLabelFont,
   fillTextEditorBackground + drawTextEditorOutline, drawScrollbar +
   getDefaultScrollbarWidth, createSliderButton) plus the JUCE header citations backing
   each.
2. docs/sessions/002-handoff-README.md, IN FULL. It contains:
   - The JUCE Font-global leak trap (lines ~22-33): a juce::Font must NEVER be a
     namespace-scope / global object -- its destructor runs at static-deinit time, after
     JUCE's typeface cache is already torn down, and this WAS caught once (~180 leaked
     objects). juce::Colour is safe as a global (it wraps a plain uint32) -- only Font is
     the trap. host/Source/Theme.h already demonstrates the correct pattern (Type:: tokens
     are zero-arg functions returning a fresh Font, not global Font objects) -- follow that
     exact pattern for any new font-related tokens in ForgeLookAndFeel.h.
   - Pre-verified JUCE virtual signatures (JUCE 7.0.9 headers): juce_Slider.h:922-961,
     juce_Button.h:395,406,411,398, juce_Label.h:280-281, juce_TextEditor.h:703-704,
     juce_ScrollBar.h:375,390.
   - LookAndFeel_V4::drawRotarySlider (juce_LookAndFeel_V4.cpp:1064-1112) is already a clean
     flat arc driven purely by findColour() -- so calling setColourScheme(...) built from
     Theme:: tokens may solve most of "looks unstyled" before writing any bespoke drawXxx
     override. This sufficiency is UNVERIFIED. Try the colour-scheme-only approach first,
     check visually, and only add bespoke draw overrides for whatever still looks wrong
     after that.
3. host/Source/Theme.h, IN FULL, before starting. Reuse its Colour tokens -- do not invent
   new colours. ForgeLookAndFeel should consume Theme:: tokens via setColour() /
   findColour().

## Critical installation traps (carry forward verbatim from session 002)

- Header-only design is DELIBERATE, not a shortcut to skip. PluginEditor.cpp appears in
  seven target_sources lists in host/CMakeLists.txt, so a .cpp companion for
  ForgeLookAndFeel means seven edit sites and link-error risk. Keep ForgeLookAndFeel.h
  header-only.
- Call setLookAndFeel(&lnf) on the editor's own component in the PluginForgeEditor
  constructor -- NEVER setDefaultLookAndFeel (that is process-global and unsafe to leave set
  in a DAW host).
- The ForgeLookAndFeel member (lnf) must be declared BEFORE the three child panel members in
  PluginEditor.h -- C++ member destruction order requires the panels be destroyed before the
  look-and-feel they reference is torn down.
- setLookAndFeel(nullptr) must be the FIRST line of ~PluginForgeEditor() (check whether the
  destructor currently has an empty/default body) because ~LookAndFeel() asserts if still
  attached to live components.

## End state

- tools/check.sh full is green.
- Host builds clean.
- EditorSessionTest passes (it is display-gated and exercises real component
  construction/destruction, so it will catch a Font-global-style leak or a setLookAndFeel
  ordering bug).
- Run tools/ui_iterate.sh WITHOUT --accept first. Visually/diff-check that only expected
  snapshot regions changed before ever accepting a new baseline.

## Out of scope

Do not touch KeyboardPanel.*, PluginProcessor.*, NoteRing.h, EditorSessionTest.cpp, or any
CMake file. Do not write a CONTRACT.md. Do not add a .cpp file for ForgeLookAndFeel.
