#include "ParamGridPanel.h"
#include "ParamGridLayout.h"
#include "ParamMap.h"

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

int ParamGridPanel::preferredContentHeight() const
{
    return ParamGridLayout::rowsFor(static_cast<int>(controls.size())) * kCellH;
}

void ParamGridPanel::refreshParamKnobs(const FaustEngine::ParamList& params)
{
    const int numMapped =
        juce::jmin(static_cast<int>(params.size()), ParamPool::POOL_SIZE);

    // ── Seed EVERY mapped slot, not just the ones with a widget ─────────────
    // Unchanged from the Task-0 split: pushToFaust denormalises, so a slot left
    // at 0.0 maps to its zone MINIMUM (a 20 Hz cutoff = silence). Seed the whole
    // mapped range from the patch defaults regardless of what gets a widget.
    for (int i = 0; i < numMapped; ++i)
    {
        const auto& p = params[static_cast<size_t>(i)];
        if (auto* rp = processor.apvts.getParameter(ParamPool::slotId(i)))
        {
            // Exact inverse of pushToFaust's forward map — the two must agree or a
            // knob lies about the value it sends.
            rp->setValueNotifyingHost(
                juce::jlimit(0.0f, 1.0f, ParamMap::mapZoneToSlot(p.defaultValue, p)));
        }
    }

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
            auto sl = std::make_unique<juce::Slider>();
            switch (p.kind)
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
            c.sliderAtt =
                std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
                    processor.apvts, id, *sl);
            content.addAndMakeVisible(*sl);
            c.widget = std::move(sl);
        }

        controls.push_back(std::move(c));
    }

    layoutControls();
}

void ParamGridPanel::layoutControls()
{
    const int n    = static_cast<int>(controls.size());
    const int cols = ParamGridLayout::columnsFor(n);
    const int rows = ParamGridLayout::rowsFor(n);

    // Decide the scrollbar from geometry (not from the Viewport's current, possibly
    // stale, scrollbar state) so the content width is deterministic and no
    // horizontal bar ever appears. getScrollBarThickness() verified juce_Viewport.h:244.
    const int fullW    = viewport.getWidth();
    const int fullH    = viewport.getHeight();
    const int contentH = rows * kCellH;
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
