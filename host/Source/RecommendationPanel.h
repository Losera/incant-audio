#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <memory>
#include <vector>

// Full-width, editor-local review surface for the typed recommendation result.
// It intentionally owns no processor/state reference: recommendations are transient
// and cross into PromptPanel only when the user explicitly accepts or bypasses them.
class RecommendationPanel : public juce::Component
{
public:
    RecommendationPanel();
    ~RecommendationPanel() override;

    void setRecommendation(const juce::var& plan, const juce::String& provider,
                           const juce::String& model);
    void setFailure(const juce::String& message, bool allowBypass, bool targetMismatch);
    void markStale();
    void clear();

    juce::var editedPlan() const;
    bool hasPlan() const { return planVisible; }
    bool isStale() const { return stale; }

    std::function<void(const juce::var&, const juce::String&, const juce::String&)> onGenerate;
    std::function<void()> onRetry;
    std::function<void()> onGenerateDirect;
    std::function<void()> onDismiss;
    std::function<void(bool)> onVisibilityChanged;

    void resized() override;
    void paint(juce::Graphics&) override;

    // Test seams use the same button callbacks and serialized model as the UI.
    void clickGenerateForTest() { generateButton.triggerClick(); }
    void clickRetryForTest() { retryButton.triggerClick(); }
    void clickDirectForTest() { directButton.triggerClick(); }
    bool directVisibleForTest() const { return directButton.isVisible(); }
    void setModulePurposeForTest(int index, const juce::String& text);
    juce::String validationErrorForTest() const { return validation.getText(); }
    juce::String titleForTest() const { return title.getText(); }
    int moduleCountForTest() const;
    int controlCountForTest() const;

private:
    struct ModuleRow;
    struct ControlRow;

    void rebuildRows(const juce::var& plan);
    void relayoutContent();
    void refreshModuleChoices();
    void addModule();
    void addControl();
    void moveModule(int index, int delta);
    void moveControl(int index, int delta);
    void removeModule(int index);
    void removeControl(int index);
    bool validateEdits(juce::String& error) const;
    void setPanelVisible(bool shouldBeVisible);

    juce::Label title;
    juce::Label summary;
    juce::Label warning;
    juce::Label validation;
    juce::Viewport viewport;
    juce::Component content;
    juce::TextButton addModuleButton { "+ Module" };
    juce::TextButton addControlButton { "+ Control" };
    juce::TextButton generateButton { "Generate from Plan" };
    juce::TextButton retryButton { "Retry" };
    juce::TextButton directButton { "Generate Directly" };
    juce::TextButton dismissButton { "Dismiss" };

    std::vector<std::unique_ptr<ModuleRow>> modules;
    std::vector<std::unique_ptr<ControlRow>> controls;
    juce::String providerName;
    juce::String modelName;
    juce::String summaryText;
    juce::String kindName;
    juce::String familyName;
    juce::var constraints;
    bool planVisible = false;
    bool stale = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RecommendationPanel)
};
