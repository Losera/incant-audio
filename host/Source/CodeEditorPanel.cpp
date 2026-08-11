#include "CodeEditorPanel.h"
#include "Theme.h"

CodeEditorPanel::CodeEditorPanel(PluginForgeProcessor& p)
    : processor(p)
{
    addAndMakeVisible(header);
    header.setText("Generated Faust (read-only)", juce::dontSendNotification);
    header.setJustificationType(juce::Justification::centredLeft);
    header.setFont(Theme::Type::body());
    header.setColour(juce::Label::textColourId, Theme::textSecondary);

    addAndMakeVisible(editor);
    // Phase 3a is the view alone. Editing lands with a Compile button, not before
    // -- a user who can type into this box but has no way to apply what they typed
    // has been given something worse than a label
    // (juce_CodeEditorComponent.h:237).
    editor.setReadOnly(true);
    editor.setScrollbarThickness(8);
    editor.setColour(juce::CodeEditorComponent::backgroundColourId, Theme::surface);
    editor.setColour(juce::CodeEditorComponent::defaultTextColourId, Theme::textPrimary);
    editor.setColour(juce::CodeEditorComponent::lineNumberBackgroundId, Theme::surfaceSunken);
    editor.setColour(juce::CodeEditorComponent::lineNumberTextId, Theme::outline);
    editor.setFont(Theme::Type::mono());

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
    g.fillAll(Theme::surface);
}
