touches:  host/tests/EditorSessionTest.cpp
depends:  none written down (there is no CONTRACT.md covering PluginEditor -- only
          INTERFACE.md:31 mentions editor destruction mid-generation)
provides: none

Do not touch any file other than host/tests/EditorSessionTest.cpp. No CMake changes are
needed -- a new scenario registers via one call line in main(), plus the scenario-count
string and the PF_SUMMARY print must be kept in sync (see "Bookkeeping" below).

IMPORTANT: this brief runs strictly AFTER docs/sessions/006-briefs/p2-look-and-feel.md has
landed and its own tools/check.sh full is green. P2 adds a member to PluginEditor and edits
its constructor/destructor; every scenario in EditorSessionTest.cpp constructs a
PluginEditor, and there is no CONTRACT.md for PluginEditor construction/destruction order to
declare that dependency mechanically. Do not start this brief until P2's commit is on disk.
If P2 has not landed, stop and say so rather than proceeding.

## Read first, in this order

1. host/Source/KeyboardPanel.h and host/Source/KeyboardPanel.cpp, in full.
2. host/Source/PluginEditor.cpp -- especially the ~30 Hz timerCallback that drives
   setPlayable.
3. host/Source/PluginProcessor.h (pushKeyboardNote) and the drain loop in
   host/Source/PluginProcessor.cpp.
4. host/Source/NoteRing.h.
5. host/tests/EditorSessionTest.cpp, in full. Scenarios 17-20 are the closest precedent --
   study their structure: the check() helper, the scenario() helper, pumpUntil(),
   struct Session, loadAndSettle(), renderUntilReady().

Before writing anything, grep the file for the current highest scenario number and use the
next two numbers after it. Do not assume the next numbers are 22/23 -- read the file and
confirm the real next number first.

## Scenario A -- held-note release on setPlayable(false)

Existing scenario 20 loads an effect FIRST, so KeyboardPanel::setPlayable(false)'s call to
keyboardState.allNotesOff(0) (in KeyboardPanel.cpp, inside setPlayable) currently runs with
an empty held-note set -- deleting that allNotesOff(0) call today breaks no existing test.
Write a new scenario that:

1. Loads an INSTRUMENT.
2. Holds a note (note-on).
3. Loads an EFFECT, triggering the DSP swap.
4. Drives whatever mechanism calls setPlayable(false) -- either the 30 Hz timer in
   PluginEditor, or a test-only setter if one exists (check KeyboardPanel.h for
   noteOnForTest / noteOffForTest and any setPlayableForTest entry points).
5. Asserts: (a) the held note actually released -- a note-off reached the ring / DSP; and
   (b) droppedKeyboardNotes() or the equivalent counter is sane (not incrementing from this
   path).

Name the scenario clearly as exercising the allNotesOff(0) code path in
KeyboardPanel::setPlayable.

## Scenario B -- QWERTY / computer-keyboard mapping, scoped honestly

juce_MidiKeyboardComponent.cpp's keyStateChanged() gates real note-firing on
KeyPress::isCurrentlyDown() -- actual OS keyboard state. This machine has no
wtype/ydotool/xdotool (already confirmed absent -- EditorSessionTest.cpp's own header
comment near the top records this). A true synthesized-keypress end-to-end test is NOT
achievable here.

Do not attempt to fake keyStateChanged(true) calls to work around this -- that takes the
wrong branch internally and produces note-offs, not note-ons.

Instead write a scenario that asserts the STATIC CONTRACT only:

- That KeyboardPanel's setKeyPressBaseOctave(4) call lines up sensibly with its
  setAvailableRange(36, 96) call. Read the actual current values out of the file -- do not
  assume these numbers are still correct, cite what you actually find.
- That nothing on the keyboard-widget construction path calls processNextMidiBuffer (grep
  to confirm).

The scenario's comment header MUST include an explicit "NOT COVERED" block stating: real
keypress-to-note firing is untested on this machine because no synthetic-input tool is
installed, and this scenario only checks that the mapping is self-consistent, not that a
keypress actually produces a note.

## Known gap, comment only

Nothing today asserts the velocity floor (jlimit(1, 127, ...) or equivalent in
KeyboardPanel::handleNoteOn) -- existing scenarios 17/18 always pass full velocity (1.0f),
so a regression removing the floor would be invisible. Note this as a known gap in a
comment. Do not write a third scenario for it unless it is trivial to add alongside
Scenario A -- it is not required scope for this brief.

## Bookkeeping

Grep for the "20 scenarios" (or similar) count string and bump it to match the new total.
Locate the PF_SUMMARY print and keep it in sync -- do not break its format,
tools/health_report.py parses it.

## Red-case discipline (required, not optional)

After writing Scenario A: locally delete the allNotesOff(0) line in KeyboardPanel.cpp,
rebuild, confirm Scenario A fails, then restore the line and confirm it passes again. This
must be reported explicitly in the change report -- a test never seen red is not yet
evidence. This is a hard project rule; cite CLAUDE.md's "A control counts only once it has
been seen failing."

## End state

tools/check.sh full is green, EditorSessionTest passes with the new scenario count, and the
change report states the red-case result for Scenario A explicitly.

## Out of scope

Do not touch KeyboardPanel.h/.cpp, PluginEditor.*, PluginProcessor.*, NoteRing.h, or any
CMake file. Do not add a CONTRACT.md for PluginEditor -- that is out of this brief's scope
even if you judge one is warranted; say so in the change report instead.
