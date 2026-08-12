#include "KeyboardPanel.h"
#include "Theme.h"

KeyboardPanel::KeyboardPanel(PluginForgeProcessor& p)
    : processor(p),
      keyboardComponent(keyboardState, juce::MidiKeyboardComponent::horizontalKeyboard)
{
    // This panel's OWN listener, registered on the state the component was
    // built with -- addListener() verified against
    // juce_MidiKeyboardState.h:170-173. The component registers a SEPARATE,
    // private listener on the same object for its own key-down highlighting
    // (juce_MidiKeyboardComponent.h:251-252); the two coexist because
    // ListenerList supports multiple listeners, and neither touches the audio
    // thread.
    keyboardState.addListener(this);

    // A middling range: enough to audition a generated voice without the
    // keyboard needing to scroll for ordinary playing. setAvailableRange
    // verified against juce_KeyboardComponentBase.h:89-96 (inclusive, 0..127).
    keyboardComponent.setAvailableRange(36, 96);
    keyboardComponent.setOctaveForMiddleC(4);   // juce_KeyboardComponentBase.h:208-219
    keyboardComponent.setKeyWidth(18.0f);

    // Computer-keyboard (QWERTY) input drives the SAME MidiKeyboardState and
    // therefore the SAME Listener callback below -- that is what makes the
    // on-screen click and a typed key "the same producer" per NoteRing.h's
    // contract (juce_MidiKeyboardComponent.h:37-40 documents the top-two-rows
    // default mapping; setKeyPressBaseOctave, :136-145, is what lines the
    // mapped notes up with the visible range above).
    keyboardComponent.setKeyPressBaseOctave(4);

    // Mouse-Y sets velocity on the on-screen keyboard (top = loud, bottom =
    // quiet, JUCE's own convention) -- one ctor line, not a subclass, contra
    // docs/sessions/010-alpha-ui-architecture.md §4's original description
    // (juce_MidiKeyboardComponent.h:73). Computer-keyboard input has no mouse
    // position, so it always uses the fixed 1.0f the first argument sets.
    keyboardComponent.setVelocity(1.0f, true);

    // ── Octave controls (C5) ────────────────────────────────────────────────
    addAndMakeVisible(octaveDownButton);
    addAndMakeVisible(octaveUpButton);
    addAndMakeVisible(octaveLabel);
    octaveDownButton.onClick = [this] { shiftOctave(-1); };
    octaveUpButton.onClick   = [this] { shiftOctave(+1); };
    octaveLabel.setJustificationType(juce::Justification::centred);
    octaveLabel.setFont(Theme::Type::caption());
    octaveLabel.setColour(juce::Label::textColourId, Theme::textSecondary);
    updateOctaveLabel();   // sets the initial "Oct: 4" text and button enablement

    // ── Theme ────────────────────────────────────────────────────────────────
    // ColourIds verified against juce_MidiKeyboardComponent.h:155-164.
    keyboardComponent.setColour(juce::MidiKeyboardComponent::whiteNoteColourId, Theme::textPrimary);
    keyboardComponent.setColour(juce::MidiKeyboardComponent::blackNoteColourId, Theme::surfaceSunken);
    keyboardComponent.setColour(juce::MidiKeyboardComponent::keyDownOverlayColourId, Theme::accent);
    keyboardComponent.setColour(juce::MidiKeyboardComponent::mouseOverKeyOverlayColourId,
                                Theme::accent.withAlpha(0.35f));
    keyboardComponent.setColour(juce::MidiKeyboardComponent::textLabelColourId, Theme::textSecondary);
    keyboardComponent.setColour(juce::MidiKeyboardComponent::keySeparatorLineColourId, Theme::outline);
    keyboardComponent.setColour(juce::MidiKeyboardComponent::shadowColourId, Theme::surface);

    addAndMakeVisible(keyboardComponent);

    disabledLabel.setText("Load an instrument to play", juce::dontSendNotification);
    disabledLabel.setJustificationType(juce::Justification::centred);
    disabledLabel.setFont(Theme::Type::caption());
    disabledLabel.setColour(juce::Label::textColourId, Theme::textSecondary);
    addChildComponent(disabledLabel);

    setPlayable(false);   // no voice contract until a patch actually declares one
}

KeyboardPanel::~KeyboardPanel()
{
    keyboardState.removeListener(this);
}

void KeyboardPanel::resized()
{
    // Control-row strip on top (C5), piano below. Carved from the SAME
    // keyboardH the panel already had -- PluginEditor.h's Chrome struct grew
    // keyboardH 64->72 in the prior commit specifically to make this split
    // possible; that height change is not repeated here. disabledLabel keeps
    // covering the whole panel (unchanged) so "nothing to play" still reads
    // as a full-panel state, octave controls included.
    auto area = getLocalBounds();
    auto controlRow = area.removeFromTop(kControlRowH);

    controlRow.removeFromLeft(4);
    octaveDownButton.setBounds(controlRow.removeFromLeft(24));
    octaveLabel.setBounds(controlRow.removeFromLeft(48));
    octaveUpButton.setBounds(controlRow.removeFromLeft(24));

    keyboardComponent.setBounds(area);
    disabledLabel.setBounds(getLocalBounds());
}

void KeyboardPanel::shiftOctave(int delta)
{
    const int newOctave = juce::jlimit(kMinOctave, kMaxOctave, currentOctave + delta);
    if (newOctave == currentOctave)
        return;   // idempotent at the clamp boundary, like setPlayable() above

    currentOctave = newOctave;
    const int semitoneShift = 12 * (currentOctave - kDefaultOctave);

    // Both calls move together -- see the class-header comment on
    // shiftOctave() in KeyboardPanel.h for why the clamps below never
    // invert the range.
    keyboardComponent.setKeyPressBaseOctave(currentOctave);
    keyboardComponent.setAvailableRange(
        juce::jlimit(0, 127, kDefaultAvailableLow  + semitoneShift),
        juce::jlimit(0, 127, kDefaultAvailableHigh + semitoneShift));

    updateOctaveLabel();
}

void KeyboardPanel::updateOctaveLabel()
{
    octaveLabel.setText("Oct: " + juce::String(currentOctave), juce::dontSendNotification);
    octaveDownButton.setEnabled(currentOctave > kMinOctave);
    octaveUpButton.setEnabled(currentOctave < kMaxOctave);
}

void KeyboardPanel::setPlayable(bool canPlay)
{
    if (canPlay == playable)
        return;                       // idempotent, like ParamGridPanel::setControlStyle

    playable = canPlay;
    keyboardComponent.setEnabled(playable);
    keyboardComponent.setAlpha(playable ? 1.0f : 0.35f);
    disabledLabel.setVisible(! playable);

    if (! playable)
    {
        // An effect patch has no voice contract. Release any key the user is
        // still physically holding rather than leaving it stuck down for
        // whichever instrument re-enables the keyboard next.
        // MidiKeyboardState::allNotesOff(0) fires handleNoteOff for every held
        // note across all channels (juce_MidiKeyboardState.h:96-103,
        // juce_MidiKeyboardState.cpp:104-116, which loops noteOff() 1..16),
        // which forwards through pushKeyboardNote() below exactly like a real
        // release -- so this cannot leave FaustEngine holding a gate the ring
        // was never told to close.
        keyboardState.allNotesOff(0);
    }
}

void KeyboardPanel::handleNoteOn(juce::MidiKeyboardState*, int /*midiChannel*/,
                                 int midiNoteNumber, float velocity)
{
    // Message thread only (see the class comment). MidiKeyboardComponent's
    // velocity is 0..1 (juce_MidiKeyboardComponent.h:66-73); pushKeyboardNote
    // wants the raw 0..127 FaustEngine::noteOn expects. Floored at 1 so a
    // very-light click (or a computer-keypress, which JUCE reports at 1.0 but
    // costs nothing to guard) is never mistaken for the velocity-0 note-off
    // convention processBlock's MIDI walk cares about
    // (juce_MidiMessage.h:239,266, cited in PluginProcessor.cpp) -- NoteRing
    // itself does not use that convention (it carries `on` as a separate
    // bool), but there is no reason to manufacture a velocity-0 note-on here.
    const int vel = juce::jlimit(1, 127, juce::roundToInt(velocity * 127.0f));
    processor.pushKeyboardNote(midiNoteNumber, vel, true);
}

void KeyboardPanel::handleNoteOff(juce::MidiKeyboardState*, int /*midiChannel*/,
                                  int midiNoteNumber, float /*velocity*/)
{
    processor.pushKeyboardNote(midiNoteNumber, 0, false);
}
