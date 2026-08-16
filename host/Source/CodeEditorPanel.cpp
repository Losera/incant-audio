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

    addAndMakeVisible(copyButton);
    copyButton.onClick = [this] { copySource(); };

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
    // New source makes any earlier highlight stale -- clear it rather than
    // leaving a selection that no longer points at the line it was drawn for.
    lastHighlightedLine = 0;

    if (faustSource.isEmpty())
    {
        editor.loadContent("// No patch compiled yet.\n"
                           "// Describe a plugin above and press Generate.\n");
        return;
    }
    editor.loadContent(faustSource);
}

void CodeEditorPanel::highlightErrorLine(int faustLineNumber)
{
    if (faustLineNumber <= 0)
        return;   // this particular error carried no line info -- nothing to point at

    const int numLines = document.getNumLines();
    if (numLines <= 0)
        return;

    // CodeDocument lines are 0-based (juce_CodeDocument.h:129-130: "Lines are
    // numbered from zero"); Faust's are 1-based. Clamp into range rather than
    // reject an out-of-range line outright -- a stale line number from an
    // error against a slightly different source is still closer to useful
    // pointing at the nearest real line than pointing at nothing.
    const int zeroIndexed = juce::jlimit(0, numLines - 1, faustLineNumber - 1);
    const juce::CodeDocument::Position start(document, zeroIndexed, 0);
    const juce::CodeDocument::Position end(document, zeroIndexed,
                                            document.getLine(zeroIndexed).length());
    editor.selectRegion(start, end);
    editor.scrollToKeepLinesOnScreen({ zeroIndexed, zeroIndexed + 1 });

    lastHighlightedLine = faustLineNumber;
}

void CodeEditorPanel::copySource()
{
    juce::SystemClipboard::copyTextToClipboard(document.getAllContent());

    // Transient confirmation, not a status line this panel doesn't have.
    // One-shot via a member Timer -- see the class header comment for why
    // this isn't juce::Timer::callAfterDelay.
    copyButton.setButtonText("Copied!");
    startTimer(900);
}

void CodeEditorPanel::timerCallback()
{
    stopTimer();
    copyButton.setButtonText("Copy");
}

void CodeEditorPanel::resized()
{
    auto area = getLocalBounds();
    auto headerRow = area.removeFromTop(20);
    copyButton.setBounds(headerRow.removeFromRight(56).reduced(0, 2));
    header.setBounds(headerRow);
    editor.setBounds(area);
}

void CodeEditorPanel::paint(juce::Graphics& g)
{
    g.fillAll(Theme::surface);
}
