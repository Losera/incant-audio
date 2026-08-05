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

    // Called from the editor's EXISTING 30Hz timer (PluginEditor.cpp's
    // timerCallback, already ticking for the level meter -- see
    // PluginForgeProcessor::isInstrumentForTest()'s header comment for why
    // polling it from there is safe and idiomatic, not merely a test hook).
    // Dims/disables the keyboard for a patch with no voice contract (an
    // effect), rather than silently accepting clicks that would produce notes
    // nothing consumes.
    void setPlayable(bool canPlay);
    bool isPlayableForTest() const { return playable; }

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

    static constexpr int kMidiChannel = 1;

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

    bool playable = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(KeyboardPanel)
};
