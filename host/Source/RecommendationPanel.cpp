#include "RecommendationPanel.h"
#include "Theme.h"

namespace
{
constexpr int kRowH = 30;
constexpr int kGap = 5;
constexpr int kNameLimit = 40;
constexpr int kPurposeLimit = 160;

juce::String property(const juce::var& value, const juce::Identifier& name)
{
    return value.isObject() ? value.getProperty(name, {}).toString() : juce::String();
}
}

struct RecommendationPanel::ModuleRow : juce::Component
{
    juce::TextEditor name, purpose;
    juce::TextButton up { "Up" }, down { "Dn" }, remove { "X" };

    ModuleRow()
    {
        for (auto* c : std::initializer_list<juce::Component*>{ &name, &purpose, &up, &down, &remove })
            addAndMakeVisible(c);
        name.setTextToShowWhenEmpty("Module", juce::Colours::grey);
        purpose.setTextToShowWhenEmpty("Purpose", juce::Colours::grey);
    }
    void resized() override
    {
        auto r = getLocalBounds();
        remove.setBounds(r.removeFromRight(26));
        down.setBounds(r.removeFromRight(26));
        up.setBounds(r.removeFromRight(26));
        name.setBounds(r.removeFromLeft(170).reduced(1));
        purpose.setBounds(r.reduced(1));
    }
};

struct RecommendationPanel::ControlRow : juce::Component
{
    juce::TextEditor name, purpose;
    juce::ComboBox module;
    juce::Label hints;
    juce::TextButton up { "Up" }, down { "Dn" }, remove { "X" };
    juce::String selectedModule, range, defaultValue, unit;

    ControlRow()
    {
        for (auto* c : std::initializer_list<juce::Component*>{ &name, &module, &purpose,
                                                                 &hints, &up, &down, &remove })
            addAndMakeVisible(c);
        name.setTextToShowWhenEmpty("Control", juce::Colours::grey);
        purpose.setTextToShowWhenEmpty("Audible purpose", juce::Colours::grey);
        hints.setColour(juce::Label::textColourId, Theme::textSecondary);
        hints.setJustificationType(juce::Justification::centredLeft);
    }
    void resized() override
    {
        auto r = getLocalBounds();
        remove.setBounds(r.removeFromRight(26));
        down.setBounds(r.removeFromRight(26));
        up.setBounds(r.removeFromRight(26));
        name.setBounds(r.removeFromLeft(135).reduced(1));
        module.setBounds(r.removeFromLeft(145).reduced(1));
        hints.setBounds(r.removeFromRight(175).reduced(2, 0));
        purpose.setBounds(r.reduced(1));
    }
};

RecommendationPanel::RecommendationPanel()
{
    setVisible(false);
    for (auto* c : std::initializer_list<juce::Component*>{ &title, &summary, &warning,
                                                             &validation, &viewport,
                                                             &addModuleButton, &addControlButton,
                                                             &generateButton, &retryButton,
                                                             &directButton, &dismissButton })
        addAndMakeVisible(c);
    viewport.setViewedComponent(&content, false);
    viewport.setScrollBarsShown(true, false);
    title.setColour(juce::Label::textColourId, Theme::textPrimary);
    summary.setColour(juce::Label::textColourId, Theme::textSecondary);
    warning.setColour(juce::Label::textColourId, Theme::progress);
    validation.setColour(juce::Label::textColourId, Theme::danger);

    addModuleButton.onClick = [this] { addModule(); };
    addControlButton.onClick = [this] { addControl(); };
    retryButton.onClick = [this] { if (onRetry) onRetry(); };
    directButton.onClick = [this] { if (onGenerateDirect) onGenerateDirect(); };
    dismissButton.onClick = [this] { clear(); if (onDismiss) onDismiss(); };
    generateButton.onClick = [this]
    {
        juce::String error;
        if (! validateEdits(error))
        {
            validation.setText(error, juce::dontSendNotification);
            return;
        }
        validation.setText({}, juce::dontSendNotification);
        if (onGenerate) onGenerate(editedPlan(), providerName, modelName);
    };
}

RecommendationPanel::~RecommendationPanel() = default;

void RecommendationPanel::setPanelVisible(bool shouldBeVisible)
{
    if (isVisible() == shouldBeVisible) return;
    setVisible(shouldBeVisible);
    if (onVisibilityChanged) onVisibilityChanged(shouldBeVisible);
}

void RecommendationPanel::setRecommendation(const juce::var& plan,
                                             const juce::String& provider,
                                             const juce::String& model)
{
    providerName = provider;
    modelName = model;
    kindName = property(plan, "kind");
    familyName = property(plan, "family");
    summaryText = property(plan, "summary");
    constraints = plan.getProperty("constraints", juce::Array<juce::var>());
    planVisible = true;
    stale = false;
    title.setText(property(plan, "title"), juce::dontSendNotification);
    summary.setText(summaryText + " | " + provider + "/" + model,
                    juce::dontSendNotification);

    juce::StringArray warnings;
    if (auto constraints = plan.getProperty("constraints", {}); constraints.isArray())
        for (const auto& item : *constraints.getArray())
            warnings.add(property(item, "message"));
    warning.setText(warnings.joinIntoString(" | "), juce::dontSendNotification);
    validation.setText({}, juce::dontSendNotification);
    rebuildRows(plan);
    retryButton.setVisible(false);
    directButton.setVisible(false);
    addModuleButton.setVisible(true);
    addControlButton.setVisible(true);
    generateButton.setVisible(true);
    setPanelVisible(true);
    resized();
}

void RecommendationPanel::setFailure(const juce::String& message, bool allowBypass,
                                     bool targetMismatch)
{
    modules.clear(); controls.clear(); content.removeAllChildren();
    planVisible = false; stale = false;
    title.setText(targetMismatch ? "Use the other Incant Audio target" : "Recommendation failed",
                  juce::dontSendNotification);
    summary.setText(message, juce::dontSendNotification);
    warning.setText({}, juce::dontSendNotification);
    validation.setText({}, juce::dontSendNotification);
    retryButton.setVisible(! targetMismatch);
    directButton.setVisible(allowBypass && ! targetMismatch);
    addModuleButton.setVisible(false);
    addControlButton.setVisible(false);
    generateButton.setVisible(false);
    setPanelVisible(true);
    resized();
}

void RecommendationPanel::markStale()
{
    if (! planVisible || stale) return;
    stale = true;
    warning.setText("This plan is stale because the prompt, family, or mode changed. Recommend again.",
                    juce::dontSendNotification);
    generateButton.setEnabled(false);
}

void RecommendationPanel::clear()
{
    modules.clear(); controls.clear(); content.removeAllChildren();
    planVisible = false; stale = false;
    generateButton.setEnabled(true);
    setPanelVisible(false);
}

void RecommendationPanel::rebuildRows(const juce::var& plan)
{
    modules.clear(); controls.clear(); content.removeAllChildren();
    if (auto list = plan.getProperty("modules", {}); list.isArray())
        for (const auto& item : *list.getArray())
        {
            auto row = std::make_unique<ModuleRow>();
            row->name.setText(property(item, "name"), false);
            row->purpose.setText(property(item, "purpose"), false);
            modules.push_back(std::move(row));
        }
    if (auto list = plan.getProperty("controls", {}); list.isArray())
        for (const auto& item : *list.getArray())
        {
            auto row = std::make_unique<ControlRow>();
            row->name.setText(property(item, "name"), false);
            row->purpose.setText(property(item, "purpose"), false);
            row->selectedModule = property(item, "module");
            row->range = property(item, "range_hint");
            row->defaultValue = property(item, "default_hint");
            row->unit = property(item, "unit");
            row->hints.setText(juce::StringArray { row->range, row->defaultValue, row->unit }
                                   .joinIntoString("  "), juce::dontSendNotification);
            controls.push_back(std::move(row));
        }
    relayoutContent();
}

void RecommendationPanel::relayoutContent()
{
    content.removeAllChildren();
    for (int i = 0; i < static_cast<int>(modules.size()); ++i)
    {
        auto& row = *modules[static_cast<size_t>(i)];
        content.addAndMakeVisible(row);
        row.up.onClick = [this, i] { moveModule(i, -1); };
        row.down.onClick = [this, i] { moveModule(i, 1); };
        row.remove.onClick = [this, i] { removeModule(i); };
        row.name.onTextChange = [this] { refreshModuleChoices(); };
    }
    for (int i = 0; i < static_cast<int>(controls.size()); ++i)
    {
        auto& row = *controls[static_cast<size_t>(i)];
        content.addAndMakeVisible(row);
        row.up.onClick = [this, i] { moveControl(i, -1); };
        row.down.onClick = [this, i] { moveControl(i, 1); };
        row.remove.onClick = [this, i] { removeControl(i); };
    }
    refreshModuleChoices();
    int y = 0;
    for (auto& row : modules) { row->setBounds(0, y, juce::jmax(700, viewport.getWidth() - 12), kRowH); y += kRowH + kGap; }
    y += kGap;
    for (auto& row : controls) { row->setBounds(0, y, juce::jmax(700, viewport.getWidth() - 12), kRowH); y += kRowH + kGap; }
    content.setSize(juce::jmax(700, viewport.getWidth() - 12), juce::jmax(y, viewport.getHeight()));
}

void RecommendationPanel::refreshModuleChoices()
{
    for (auto& row : controls)
    {
        auto selected = row->module.getText().isNotEmpty() ? row->module.getText() : row->selectedModule;
        row->module.clear(juce::dontSendNotification);
        for (int i = 0; i < static_cast<int>(modules.size()); ++i)
            row->module.addItem(modules[static_cast<size_t>(i)]->name.getText(), i + 1);
        row->module.setText(selected, juce::dontSendNotification);
    }
}

void RecommendationPanel::addModule()
{
    if (modules.size() >= 5) return;
    auto row = std::make_unique<ModuleRow>();
    row->name.setText("New Module", false); row->purpose.setText("Describe this stage", false);
    modules.push_back(std::move(row)); relayoutContent();
}
void RecommendationPanel::addControl()
{
    if (controls.size() >= 12 || modules.empty()) return;
    auto row = std::make_unique<ControlRow>();
    row->name.setText("New Control", false); row->purpose.setText("Describe its audible effect", false);
    row->selectedModule = modules.front()->name.getText(); controls.push_back(std::move(row)); relayoutContent();
}
void RecommendationPanel::moveModule(int i, int d)
{
    const int j = i + d; if (j < 0 || j >= static_cast<int>(modules.size())) return;
    std::swap(modules[static_cast<size_t>(i)], modules[static_cast<size_t>(j)]); relayoutContent();
}
void RecommendationPanel::moveControl(int i, int d)
{
    const int j = i + d; if (j < 0 || j >= static_cast<int>(controls.size())) return;
    std::swap(controls[static_cast<size_t>(i)], controls[static_cast<size_t>(j)]); relayoutContent();
}
void RecommendationPanel::removeModule(int i)
{
    if (modules.size() <= 1 || i < 0 || i >= static_cast<int>(modules.size())) return;
    modules.erase(modules.begin() + i); relayoutContent();
}
void RecommendationPanel::removeControl(int i)
{
    if (controls.size() <= 1 || i < 0 || i >= static_cast<int>(controls.size())) return;
    controls.erase(controls.begin() + i); relayoutContent();
}

bool RecommendationPanel::validateEdits(juce::String& error) const
{
    juce::StringArray names, controlNames;
    for (const auto& row : modules)
    {
        const auto name = row->name.getText().trim();
        if (name.isEmpty()) { error = "Every module needs a name."; return false; }
        if (name.length() > kNameLimit) { error = "Module names must be 40 characters or fewer."; return false; }
        if (names.contains(name, true)) { error = "Module names must be unique."; return false; }
        const auto purpose = row->purpose.getText().trim();
        if (purpose.isEmpty()) { error = "Every module needs a purpose."; return false; }
        if (purpose.length() > kPurposeLimit) { error = "Module purposes must be 160 characters or fewer."; return false; }
        names.add(name);
    }
    for (const auto& row : controls)
    {
        const auto name = row->name.getText().trim();
        if (name.isEmpty()) { error = "Every control needs a name."; return false; }
        if (name.length() > kNameLimit) { error = "Control names must be 40 characters or fewer."; return false; }
        if (controlNames.contains(name, true)) { error = "Control names must be unique."; return false; }
        const auto purpose = row->purpose.getText().trim();
        if (purpose.isEmpty()) { error = "Every control needs a purpose."; return false; }
        if (purpose.length() > kPurposeLimit) { error = "Control purposes must be 160 characters or fewer."; return false; }
        if (! names.contains(row->module.getText(), true)) { error = "Every control must select a module."; return false; }
        if (row->range.length() > 60 || row->defaultValue.length() > 60 || row->unit.length() > 24)
        { error = "Control range/default/unit hints exceed the supported length."; return false; }
        controlNames.add(name);
    }
    if (stale) { error = "Recommend again before generating from this stale plan."; return false; }
    return true;
}

juce::var RecommendationPanel::editedPlan() const
{
    auto* plan = new juce::DynamicObject();
    plan->setProperty("schema", 1); plan->setProperty("title", title.getText());
    plan->setProperty("summary", summaryText);
    plan->setProperty("kind", kindName);
    plan->setProperty("family", familyName);
    plan->setProperty("constraints", constraints);
    juce::Array<juce::var> moduleList;
    for (const auto& row : modules)
    {
        auto* item = new juce::DynamicObject(); item->setProperty("name", row->name.getText().trim());
        item->setProperty("purpose", row->purpose.getText().trim()); moduleList.add(juce::var(item));
    }
    juce::Array<juce::var> controlList;
    for (const auto& row : controls)
    {
        auto* item = new juce::DynamicObject(); item->setProperty("name", row->name.getText().trim());
        item->setProperty("module", row->module.getText()); item->setProperty("purpose", row->purpose.getText().trim());
        item->setProperty("range_hint", row->range); item->setProperty("default_hint", row->defaultValue);
        item->setProperty("unit", row->unit); controlList.add(juce::var(item));
    }
    plan->setProperty("modules", moduleList); plan->setProperty("controls", controlList);
    return juce::var(plan);
}

int RecommendationPanel::moduleCountForTest() const { return static_cast<int>(modules.size()); }
int RecommendationPanel::controlCountForTest() const { return static_cast<int>(controls.size()); }
void RecommendationPanel::setModulePurposeForTest(int index, const juce::String& text)
{
    if (index >= 0 && index < static_cast<int>(modules.size()))
        modules[static_cast<size_t>(index)]->purpose.setText(text, false);
}

void RecommendationPanel::resized()
{
    auto r = getLocalBounds().reduced(10);
    title.setBounds(r.removeFromTop(24)); summary.setBounds(r.removeFromTop(20));
    warning.setBounds(r.removeFromTop(warning.getText().isEmpty() ? 0 : 22));
    auto actions = r.removeFromBottom(30);
    dismissButton.setBounds(actions.removeFromRight(80)); actions.removeFromRight(5);
    directButton.setBounds(actions.removeFromRight(130)); actions.removeFromRight(5);
    retryButton.setBounds(actions.removeFromRight(75)); actions.removeFromRight(5);
    generateButton.setBounds(actions.removeFromRight(155));
    addModuleButton.setBounds(actions.removeFromLeft(85)); actions.removeFromLeft(5);
    addControlButton.setBounds(actions.removeFromLeft(90));
    validation.setBounds(r.removeFromBottom(20)); viewport.setBounds(r);
    relayoutContent();
}

void RecommendationPanel::paint(juce::Graphics& g)
{
    g.setColour(Theme::surface); g.fillRoundedRectangle(getLocalBounds().toFloat(), 6.0f);
    g.setColour(Theme::outline); g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(0.5f), 6.0f, 1.0f);
}
