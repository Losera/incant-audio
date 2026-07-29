#include "CodeEditorPanel.h"

CodeEditorPanel::CodeEditorPanel(PluginForgeProcessor& p)
    : processor(p)
{
    addAndMakeVisible(header);
    header.setText("Generated Faust (read-only)", juce::dontSendNotification);
    header.setJustificationType(juce::Justification::centredLeft);
    header.setFont(juce::Font(12.0f));
    header.setColour(juce::Label::textColourId, juce::Colour(0xff9399b2));

    addAndMakeVisible(editor);
    // Phase 3a is the view alone. Editing lands with a Compile button, not before
    // -- a user who can type into this box but has no way to apply what they typed
    // has been given something worse than a label
    // (juce_CodeEditorComponent.h:237).
    editor.setReadOnly(true);
    editor.setScrollbarThickness(8);
    editor.setColour(juce::CodeEditorComponent::backgroundColourId,
                     juce::Colour(0xff181825));
    editor.setColour(juce::CodeEditorComponent::defaultTextColourId,
                     juce::Colour(0xffcdd6f4));
    editor.setColour(juce::CodeEditorComponent::lineNumberBackgroundId,
                     juce::Colour(0xff11111b));
    editor.setColour(juce::CodeEditorComponent::lineNumberTextId,
                     juce::Colour(0xff6c7086));
    editor.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), 12.0f,
                              juce::Font::plain));

    // Seed from whatever is already live, so a panel revealed AFTER a compile
    // shows the live patch instead of staying blank until the next one.
    showSource(processor.currentSource());
}

CodeEditorPanel::~CodeEditorPanel() = default;

void CodeEditorPanel::showSource(const juce::String& faustSource)
{
    // loadContent resets the document and clears its undo history
    // (juce_CodeEditorComponent.h:65-69). That is the intent: this is a view of
    // the current patch, not an accumulating buffer.
    if (faustSource.isEmpty())
    {
        editor.loadContent("// No patch compiled yet.\n"
                           "// Describe a plugin above and press Generate.\n");
        return;
    }
    editor.loadContent(faustSource);
}

void CodeEditorPanel::resized()
{
    auto area = getLocalBounds();
    header.setBounds(area.removeFromTop(16));
    editor.setBounds(area);
}

void CodeEditorPanel::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff181825));
}
