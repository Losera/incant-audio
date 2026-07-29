#include "ParamGridPanel.h"
#include "ParamGridLayout.h"
// ParamMap.h is deliberately NOT included. This panel used to convert patch
// defaults into slot values itself; that seeding moved to the processor (PF-033,
// see refreshParamKnobs). Leaving the include would invite it back.

ParamGridPanel::ParamGridPanel(PluginForgeProcessor& p)
    : processor(p)
{
    addAndMakeVisible(viewport);
    // We own `content` as a member — the Viewport must NOT delete it (second arg
    // false). setViewedComponent / setScrollBarsShown verified against
    // juce_Viewport.h:76 and :201. Vertical scrollbar only; the content width is
    // fitted to the visible area so a horizontal bar never appears.
    viewport.setViewedComponent(&content, false);
    viewport.setScrollBarsShown(true, false);
}

int ParamGridPanel::contentHeightForCurrentMode() const
{
    return ParamGridLayout::rowsFor(static_cast<int>(controls.size())) * kCellH;
}

int ParamGridPanel::preferredContentHeight() const
{
    return contentHeightForCurrentMode();
}

void ParamGridPanel::refreshParamKnobs(const FaustEngine::ParamList& params)
{
    const int numMapped =
        juce::jmin(static_cast<int>(params.size()), ParamPool::POOL_SIZE);

    // ── NO SEEDING HERE. It belongs to the processor, and only to it ────────
    // This function used to seed every mapped slot from the patch defaults, on
    // every compile, unconditionally. That was correct when it was written and
    // became a data-loss bug the moment LoadMode existed (PF-033).
    //
    // The restore path is the failure. setStateInformation replaces the APVTS
    // state with the SAVED values and then recompiles with LoadMode::Iterate
    // precisely so nothing resets them (PluginProcessor.cpp:65-69). The compile
    // succeeds, the callback hops to the message thread, it lands HERE — and the
    // seeding overwrote every restored value with the patch's declared default.
    // So reopening a saved project put every knob back to factory position, but
    // only if the editor happened to be open, which is the same
    // conditional-on-the-UI defect PF-020 fixed, running in the other direction.
    // StatePersistenceTest could not see it: it never constructs an editor.
    //
    // The processor already does this properly. resetMappedSlotsToDefaults()
    // (PluginProcessor.cpp:112-142) covers all 64 slots rather than just the
    // mapped ones, zeroes the unmapped remainder so a stale value cannot reappear
    // under a later patch, uses the same ParamMap conversion, and runs inside the
    // swap protocol's safe window — after the audioBusy drain, before ready=true
    // — which is the only point at which slot values can be rewritten without
    // pushToFaust concurrently reading them. A message-thread write from here had
    // none of those properties.
    //
    // The reason the seeding mattered at all still holds: pushToFaust
    // denormalises, so a slot left at 0.0 maps to its zone MINIMUM (a 20 Hz
    // cutoff = silence). That is exactly what the processor's Fresh path now
    // guarantees, on every load, with or without an editor.

    // ── Rebuild the widgets ─────────────────────────────────────────────────
    // clear() first so each old attachment detaches (Control destroys attachment
    // before widget) before we bind a fresh one to the same slot ID: a slot may
    // switch widget KIND between patches (a knob patch replaced by a toggle patch),
    // and two live attachments on one parameter is undefined.
    controls.clear();
    controls.reserve(static_cast<size_t>(numMapped));

    for (int i = 0; i < numMapped; ++i)
    {
        const auto& p  = params[static_cast<size_t>(i)];
        const auto  id = ParamPool::slotId(i);
        Control c;

        // Keep the metadata so a presentation change can restyle without rebuilding.
        // The zone pointer must NOT survive the copy: it points into the DSP instance
        // that produced it and dangles the moment that instance is deleted
        // (FaustEngine.h:59-64), which this copy outlives.
        c.meta      = p;
        c.meta.zone = nullptr;

        c.label = std::make_unique<juce::Label>();
        c.label->setJustificationType(juce::Justification::centred);
        c.label->setFont(juce::Font(12.0f));
        c.label->setText(juce::String(p.label), juce::dontSendNotification);
        content.addAndMakeVisible(*c.label);

        if (p.kind == FaustEngine::Kind::Button
            || p.kind == FaustEngine::Kind::CheckButton)
        {
            // Toggle-kind params must render as a ToggleButton, never a rotary
            // (docs/ui_design_plan.md §3). ButtonAttachment maps the 0..1 slot to
            // the button's on/off state (juce_AudioProcessorValueTreeState.h:590).
            auto tb = std::make_unique<juce::ToggleButton>();
            c.buttonAtt =
                std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
                    processor.apvts, id, *tb);
            content.addAndMakeVisible(*tb);
            c.widget = std::move(tb);
        }
        else
        {
            // TYPE only. Every styling decision — slider style, text box, readout —
            // belongs to applyPresentation() below, so that a mode change can
            // restyle in place without touching an attachment.
            auto sl = std::make_unique<juce::Slider>();
            c.sliderAtt =
                std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
                    processor.apvts, id, *sl);
            content.addAndMakeVisible(*sl);
            c.widget = std::move(sl);
        }

        applyPresentation(c);
        controls.push_back(std::move(c));
    }

    layoutControls();
    ++refreshCount;     // see refreshCountForTest() in the header
}

void ParamGridPanel::applyPresentation(Control& c)
{
    // Anything that is not a Slider has no style to apply. A ToggleButton returns
    // here, which is why no rotary code below can ever reach a toggle-kind param.
    auto* sl = dynamic_cast<juce::Slider*>(c.widget.get());
    if (sl == nullptr)
        return;

    switch (c.meta.kind)
    {
        case FaustEngine::Kind::HSlider:   // juce_Slider.h:63/96
            sl->setSliderStyle(juce::Slider::LinearHorizontal);
            sl->setTextBoxStyle(juce::Slider::TextBoxRight, false, 56, 16);
            break;
        case FaustEngine::Kind::VSlider:   // :64/99
            sl->setSliderStyle(juce::Slider::LinearVertical);
            sl->setTextBoxStyle(juce::Slider::TextBoxBelow, false, 56, 16);
            break;
        case FaustEngine::Kind::NumEntry:  // :75/96 — number box w/ inc-dec
            sl->setSliderStyle(juce::Slider::IncDecButtons);
            sl->setTextBoxStyle(juce::Slider::TextBoxLeft, false, 56, 16);
            break;
        case FaustEngine::Kind::Button:
        case FaustEngine::Kind::CheckButton:
        default:                            // :73 — rotary is the fallback
            sl->setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
            sl->setTextBoxStyle(juce::Slider::TextBoxBelow, false, 64, 16);
            break;
    }
}

void ParamGridPanel::layoutControls()
{
    const int n    = static_cast<int>(controls.size());
    const int cols = ParamGridLayout::columnsFor(n);
    // No `rows` local: the row count is only ever needed as a height, and that now
    // comes from contentHeightForCurrentMode() below.

    // Decide the scrollbar from geometry (not from the Viewport's current, possibly
    // stale, scrollbar state) so the content width is deterministic and no
    // horizontal bar ever appears. getScrollBarThickness() verified juce_Viewport.h:244.
    const int fullW    = viewport.getWidth();
    const int fullH    = viewport.getHeight();
    // Through contentHeightForCurrentMode(), NOT a second `rows * kCellH` — the
    // shell sizes the window from preferredContentHeight() and this lays the cells
    // out; two independent copies of that arithmetic is the kChromeHeight defect
    // (see PluginEditor.h), which drifted in exactly this way.
    const int contentH = contentHeightForCurrentMode();
    const bool vscroll = contentH > fullH;
    const int viewW    = juce::jmax(0, fullW - (vscroll ? viewport.getScrollBarThickness() : 0));

    // Content is at least as tall as the viewport so a short patch fills the region
    // (no dead scroll gap); a tall patch scrolls.
    content.setSize(viewW, juce::jmax(contentH, fullH));

    if (n == 0 || cols == 0 || viewW == 0)
        return;

    const int cellW = viewW / cols;
    for (int i = 0; i < n; ++i)
    {
        auto& c = controls[static_cast<size_t>(i)];
        auto cell = juce::Rectangle<int>((i % cols) * cellW,
                                         (i / cols) * kCellH,
                                         cellW, kCellH);
        c.label->setBounds(cell.removeFromTop(kLabelH));
        auto body = cell.reduced(4);

        if (c.buttonAtt != nullptr)
            // A toggle is a fixed-height control; centre a compact box in the cell
            // rather than stretching a checkbox across it.
            c.widget->setBounds(body.withSizeKeepingCentre(
                juce::jmin(body.getWidth(), 90), juce::jmin(body.getHeight(), 28)));
        else
            c.widget->setBounds(body);
    }
}

void ParamGridPanel::resized()
{
    viewport.setBounds(getLocalBounds());
    layoutControls();
}

// ── Test-only observables ───────────────────────────────────────────────────
// Read the widget's ACTUAL runtime type and style rather than re-deriving it
// from the Faust Kind. Re-deriving would make the test agree with
// refreshParamKnobs by construction and prove nothing — the claim under test is
// precisely that the switch above maps Kind to widget the way the header says.

ParamGridPanel::WidgetKind ParamGridPanel::controlKindForTest(int index) const
{
    if (index < 0 || index >= static_cast<int>(controls.size()))
        return WidgetKind::Unknown;

    auto* w = controls[static_cast<size_t>(index)].widget.get();
    if (dynamic_cast<juce::ToggleButton*>(w) != nullptr)
        return WidgetKind::Toggle;

    if (auto* sl = dynamic_cast<juce::Slider*>(w))
    {
        switch (sl->getSliderStyle())
        {
            case juce::Slider::LinearHorizontal: return WidgetKind::HorizontalSlider;
            case juce::Slider::LinearVertical:   return WidgetKind::VerticalSlider;
            case juce::Slider::IncDecButtons:    return WidgetKind::IncDec;
            case juce::Slider::RotaryHorizontalVerticalDrag:
            case juce::Slider::Rotary:
            case juce::Slider::RotaryHorizontalDrag:
            case juce::Slider::RotaryVerticalDrag: return WidgetKind::Rotary;
            default: return WidgetKind::Unknown;
        }
    }
    return WidgetKind::Unknown;
}

juce::String ParamGridPanel::controlLabelForTest(int index) const
{
    if (index < 0 || index >= static_cast<int>(controls.size()))
        return {};
    auto* l = controls[static_cast<size_t>(index)].label.get();
    return l != nullptr ? l->getText() : juce::String();
}

double ParamGridPanel::controlValueForTest(int index) const
{
    if (index < 0 || index >= static_cast<int>(controls.size()))
        return 0.0;

    auto* w = controls[static_cast<size_t>(index)].widget.get();
    if (auto* sl = dynamic_cast<juce::Slider*>(w))
        return sl->getValue();
    if (auto* tb = dynamic_cast<juce::ToggleButton*>(w))
        return tb->getToggleState() ? 1.0 : 0.0;
    return 0.0;
}

const char* ParamGridPanel::widgetKindName(WidgetKind k)
{
    switch (k)
    {
        case WidgetKind::Rotary:           return "Rotary";
        case WidgetKind::HorizontalSlider: return "HorizontalSlider";
        case WidgetKind::VerticalSlider:   return "VerticalSlider";
        case WidgetKind::IncDec:           return "IncDec";
        case WidgetKind::Toggle:           return "Toggle";
        case WidgetKind::Unknown:
        default:                           return "Unknown";
    }
}
