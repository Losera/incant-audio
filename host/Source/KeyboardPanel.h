#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"

// ── KeyboardPanel ────────────────────────────────────────────────────────────
// On-screen + computer-keyboard note input for the editor -- the last piece of
// the NoteRing path (host/Source/NoteRing.h): the queue and its drain in
// PluginProcessor::processBlock already existed and were TSan-proven; nothing
// in the editor ever called the producer side. This is that producer.
//
// THE CONTRACT THIS MUST KEEP (NoteRing.h:20-24, PluginProcessor.h:172-193):
//   * Producer is the message thread. Consumer is the audio thread. Exactly
//     one of each -- NoteRing is SPSC, not safe for two producers.
//   * The on-screen keyboard and computer-keyboard (QWERTY) input are THE SAME
//     producer: both arrive as juce::MidiKeyboardState::Listener callbacks on
//     the message thread, through the ONE juce::MidiKeyboardState this panel
//     owns. That is what makes routing both into one PluginForgeProcessor
//     instance correct rather than a second-producer bug.
//   * The ONLY function that may reach the audio thread from here is
//     PluginForgeProcessor::pushKeyboardNote(). This class must NEVER call
//     juce::MidiKeyboardState::processNextMidiBuffer() or otherwise feed MIDI
//     into processBlock directly: that call takes MidiKeyboardState's own
//     juce::CriticalSection (juce_MidiKeyboardState.h:182,
//     juce_MidiKeyboardState.cpp:140 `const ScopedLock sl (lock)`), and
//     processBlock never takes a lock. NoteRing.h exists precisely to avoid
//     that call, and juce::MidiKeyboardComponent's OWN internal listener (it is
//     privately a MidiKeyboardState::Listener too, juce_MidiKeyboardComponent.h
//     :49-52, for its own key-down highlighting) never calls
//     processNextMidiBuffer() either -- verified by reading
//     juce_MidiKeyboardComponent.cpp for the only two call sites, both driven
//     by mouse/computer-keyboard input, i.e. the message thread.
//   * Note-priority (last-note-priority / legato) is FaustEngine's job
//     (FaustEngine.cpp:485-528: freq/gain set before the gate, and noteOff only
//     releases the gate for the note that is still currentNote). This class
//     forwards raw note-on/note-off events and adds no priority logic of its
//     own -- a second, competing copy of that logic here would be the bug, not
//     a safety net.
class KeyboardPanel : public juce::Component,
                      private juce::MidiKeyboardState::Listener
{
public:
    explicit KeyboardPanel(PluginForgeProcessor&);
    ~KeyboardPanel() override;

    void resized() override;

    // Re-applies octaveLabel/disabledLabel fonts once this panel is actually
    // attached under PluginForgeEditor. See ForgeLookAndFeel.h's
    // resolveThemeFont() header comment: the constructor's own setFont()
    // calls run before that attachment exists (PluginForgeEditor's
    // member-initialiser list, before addAndMakeVisible), so they resolve
    // against the wrong LookAndFeel. JUCE calls this automatically the
    // moment addChildComponent() sets parentComponent
    // (juce_Component.cpp:1166-1187).
    void parentHierarchyChanged() override;

    // Called from the editor's EXISTING 30Hz timer (PluginEditor.cpp's
    // timerCallback, already ticking for the level meter -- see
    // PluginForgeProcessor::isInstrumentForTest()'s header comment for why
    // polling it from there is safe and idiomatic, not merely a test hook).
    // Dims/disables the keyboard for a patch with no voice contract (an
    // effect), rather than silently accepting clicks that would produce notes
    // nothing consumes.
    void setPlayable(bool canPlay);
    bool isPlayableForTest() const { return playable; }

    // Called after a successful instrument generation
    // (PluginEditor.cpp's onFaustCompileSuccess) so QWERTY plays immediately,
    // without the user needing to click the piano first. routeKeyStateChanged
    // below makes the editor forward key transitions regardless of what
    // currently holds focus, but JUCE's dispatch walk still STARTS at
    // whichever component holds focus (juce::ComponentPeer::
    // handleKeyUpOrDown, juce_ComponentPeer.cpp:224-243) -- right after
    // Generate, that is still the prompt box. Moving focus onto the piano
    // directly makes MidiKeyboardComponent::keyStateChanged the FIRST thing
    // the walk reaches, no editor-level forwarding needed for this case.
    // Only meaningful once the widget is actually enabled -- JUCE does not
    // grant keyboard focus to a disabled component -- so the caller must
    // ensure setPlayable(true) has already run this call, not rely on the
    // next 30Hz timer tick to catch up first.
    void focusForPlaying()
    {
        ++focusForPlayingCallCount;
        keyboardComponent.grabKeyboardFocus();
    }
    // Proves the WIRING (this was called after a successful instrument
    // generation), not that focus actually moved -- grabKeyboardFocus()
    // requires a real peer/window (juce::Component::isShowing()'s
    // requirement, juce_Component.cpp), which host/tests/EditorSessionTest.cpp
    // never creates. Same category of limitation as
    // routeKeyStateChangedCallCountForTest() below: a real physical/OS
    // interaction is outside what a headless harness on this machine can
    // observe (STATUS.md Broken #1).
    int focusForPlayingCallCountForTest() const { return focusForPlayingCallCount; }

    // Widget-level readback, distinct from isPlayableForTest()'s flag: this is
    // what actually proved the constructor no-op bug (the flag was correctly
    // false from the very first frame; the WIDGETS silently were not --
    // KeyboardPanel.cpp's applyPlayableVisuals() comment has the full story).
    bool keyboardEnabledForTest() const  { return keyboardComponent.isEnabled(); }
    float keyboardAlphaForTest() const   { return keyboardComponent.getAlpha(); }
    bool disabledLabelVisibleForTest() const { return disabledLabel.isVisible(); }

    // ── Test-only producer entry points (host/tests/EditorSessionTest.cpp) ──
    // Drive keyboardState.noteOn()/noteOff() directly -- exactly what a mouse
    // click on the component or a mapped computer-keypress does internally
    // (juce_MidiKeyboardComponent.cpp's mouseDown/keyPressed handlers both
    // call state.noteOn/noteOff on the MidiKeyboardState they were built with).
    // This is NOT a shortcut around KeyboardPanel's own wiring: it still goes
    // through juce::MidiKeyboardState::noteOn/noteOff, which synchronously
    // calls this panel's handleNoteOn/handleNoteOff below
    // (juce_MidiKeyboardState.h:79-94,145-153,166-167 -- "called synchronously
    // ... when a note is being played with its ...noteOn() method"), so a test
    // that uses these is exercising the SAME production code path a real
    // gesture would reach, not a parallel one.
    //
    // Deliberately bypasses the Component's enabled/hit-test state (a click on
    // a disabled MidiKeyboardComponent never reaches its mouseDown handler at
    // all), which is what lets scenario 20 (effect-patch-disables-keyboard)
    // prove the DRAIN discards the note rather than merely proving the mouse
    // can't reach the widget.
    void noteOnForTest(int note, float velocity)
    {
        keyboardState.noteOn(kMidiChannel, note, velocity);
    }
    void noteOffForTest(int note)
    {
        keyboardState.noteOff(kMidiChannel, note, 0.0f);
    }

    // ── Computer-keyboard (QWERTY) routing seam, added 2026-08-12 ───────────
    // keyboardComponent is a SIBLING of the editor's other panels (the prompt
    // box, the toolbar buttons), not an ancestor of whatever the user last
    // clicked. JUCE's own key-state dispatch walks UP the parent chain from
    // whichever component currently holds keyboard focus
    // (juce::ComponentPeer::handleKeyUpOrDown, juce_ComponentPeer.cpp:224-243),
    // so it only ever reached keyboardComponent's own keyStateChanged()
    // (juce_MidiKeyboardComponent.cpp:254-283, which is what turns a held
    // QWERTY key into a note) when the piano itself already held focus --
    // which, before this method existed, only happened if the user clicked
    // the piano first. PluginForgeEditor::keyStateChanged() calls this on
    // every key transition so the piano no longer needs to be the walk's
    // starting point. Safe to call unconditionally: juce::TextEditor
    // deliberately swallows this same walk while IT holds focus
    // ("(overridden to avoid forwarding key events to the parent)",
    // juce_TextEditor.cpp:2189-2205), so normal typing in the prompt box is
    // unaffected -- this call is simply never reached while that is true.
    bool routeKeyStateChanged(bool isKeyDown)
    {
        ++routeCallCountForTest;
        return keyboardComponent.keyStateChanged(isKeyDown);
    }
    int routeKeyStateChangedCallCountForTest() const { return routeCallCountForTest; }

    // ── Octave controls, added 2026-08-12 (C5) ──────────────────────────────
    // [<]/[>] buttons carved from the control-row strip resized() reserves
    // (docs/sessions/010-alpha-ui-architecture.md §4: "shifts the
    // MidiKeyboardComponent range and setKeyPressBaseOctave() by +-1. Range
    // clamped 0-8" -- octave NUMBER 0-8, not MIDI note numbers). Shifts both
    // together so the visible piano and what QWERTY plays never drift apart --
    // the exact invariant scenario 22 checks live, post-C5.
    //
    // Clamp safety, worked by hand rather than asserted at runtime: the
    // available range is [36,96] at the default octave 4. A shift of
    // 12*(octave-4) is added to both ends, independently clamped to
    // MIDI's [0,127]. At the extremes (octave 0: delta -48; octave 8: delta
    // +48) the two clamps never cross -- octave 0 gives [0,48] (low clamped
    // from -12), octave 8 gives [84,127] (high clamped from 144) -- so no
    // runtime width check is needed for a fixed 0-8 octave range.
    void shiftOctave(int delta);
    int  currentOctaveForTest() const { return currentOctave; }
    int  availableRangeLowForTest() const  { return keyboardComponent.getRangeStart(); }
    int  availableRangeHighForTest() const { return keyboardComponent.getRangeEnd(); }

private:
    // juce::MidiKeyboardState::Listener. Fires synchronously from
    // keyboardState.noteOn()/noteOff() -- called either by
    // juce::MidiKeyboardComponent's own mouse/computer-keyboard handling or by
    // noteOnForTest/noteOffForTest above -- always on the message thread, per
    // the header cited there. Forwards the raw event only; see the class
    // comment for why no priority logic belongs here.
    void handleNoteOn(juce::MidiKeyboardState*, int midiChannel,
                      int midiNoteNumber, float velocity) override;
    void handleNoteOff(juce::MidiKeyboardState*, int midiChannel,
                       int midiNoteNumber, float velocity) override;

    // Called from parentHierarchyChanged() below, once this panel is
    // actually attached under PluginForgeEditor -- see that override's
    // comment for why resolving any earlier would be both wrong and noisy.
    void applyFonts();

    static constexpr int kMidiChannel = 1;

    // The construction-time values shiftOctave() shifts away from and back
    // to; see the octave-controls comment above for the clamp arithmetic.
    static constexpr int kDefaultOctave        = 4;
    static constexpr int kMinOctave            = 0;
    static constexpr int kMaxOctave            = 8;
    static constexpr int kDefaultAvailableLow  = 36;
    static constexpr int kDefaultAvailableHigh = 96;
    static constexpr int kControlRowH          = 24;   // top strip; piano gets the remaining 48 of keyboardH's 72

    void updateOctaveLabel();

    // Applies `playable`'s CURRENT value to the widgets (enabled/alpha/label).
    // Split out of setPlayable() so the constructor can apply the true initial
    // state without going through setPlayable()'s idempotence guard, which
    // would otherwise no-op it (playable's member initializer already reads
    // false, matching the ctor's setPlayable(false) call — "unchanged" by
    // definition, so the widgets never actually got dimmed/disabled/labelled
    // until the first REAL transition arrived, which for an effect patch
    // never comes). No keyboardState side effects here (see setPlayable) --
    // this only ever mirrors state onto widgets, safe to call before any note
    // could exist.
    void applyPlayableVisuals();

    PluginForgeProcessor& processor;

    // Declared in construction order: keyboardState must outlive
    // keyboardComponent (the component holds a reference to it, and also
    // registers its OWN private listener on it -- juce_MidiKeyboardComponent.h
    // :251-252 -- independent of the addListener(this) call in the .cpp).
    juce::MidiKeyboardState     keyboardState;
    juce::MidiKeyboardComponent keyboardComponent;

    // Shown only while !playable, overlaid on the dimmed keyboard so an
    // effect patch reads as "nothing to play" rather than merely looking
    // washed out for no stated reason.
    juce::Label disabledLabel;

    // Octave controls (C5): a [<] [Oct: N] [>] cluster in the control-row
    // strip resized() carves from the top of keyboardH. Left enabled
    // regardless of setPlayable()'s dim/disable state -- ranging the
    // keyboard is UI-only and touches nothing NoteRing/audio-thread related,
    // so there is no reason to gate it on a voice contract existing yet.
    juce::TextButton octaveDownButton { "<" };
    juce::TextButton octaveUpButton   { ">" };
    juce::Label      octaveLabel;
    int              currentOctave = kDefaultOctave;

    bool playable = false;
    int  routeCallCountForTest = 0;
    int  focusForPlayingCallCount = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(KeyboardPanel)
};
