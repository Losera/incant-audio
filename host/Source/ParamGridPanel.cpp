#include "ParamGridPanel.h"
#include "ParamMap.h"

ParamGridPanel::ParamGridPanel(PluginForgeProcessor& p)
    : processor(p)
{
    for (int i = 0; i < MAX_KNOBS; ++i)
    {
        auto& s = paramSliders[static_cast<size_t>(i)];
        s.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        s.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 64, 16);
        addChildComponent(s);   // invisible until a compile maps this slot

        // Attachment binds to the slot's full 0-1 range; what that 0-1 value
        // means to the live DSP is pushToFaust()'s contract, not the UI's.
        paramAttachments[static_cast<size_t>(i)] =
            std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
                processor.apvts, ParamPool::slotId(i), s);

        auto& l = paramNameLabels[static_cast<size_t>(i)];
        l.setJustificationType(juce::Justification::centred);
        l.setFont(juce::Font(12.0f));
        addChildComponent(l);
    }
}

void ParamGridPanel::refreshParamKnobs(const FaustEngine::ParamList& params)
{
    const int numMapped =
        juce::jmin(static_cast<int>(params.size()), ParamPool::POOL_SIZE);

    // ── Seed EVERY mapped slot, not just the ones with a visible knob ───────
    // This loop used to stop at MAX_KNOBS (8) while the pool holds 64. Slots
    // 8..63 therefore kept whatever they last held (0.0 on a fresh instance).
    // Under the old raw push that was merely wrong; now that pushToFaust
    // denormalises, 0.0 maps to each zone's MINIMUM -- a 12-param patch would
    // come up with its 9th..12th controls pinned to the bottom of their range
    // (a cutoff at 20 Hz: silence). Knob *visibility* is still capped at 8
    // below; slot *seeding* must cover the whole pool.
    for (int i = 0; i < numMapped; ++i)
    {
        const auto& p = params[static_cast<size_t>(i)];
        if (auto* rp = processor.apvts.getParameter(ParamPool::slotId(i)))
        {
            // Shared with pushToFaust's forward direction -- the two must be
            // exact inverses or the knob lies about the value it is sending.
            rp->setValueNotifyingHost(
                juce::jlimit(0.0f, 1.0f, ParamMap::mapZoneToSlot(p.defaultValue, p)));
        }
    }

    // ── Knob visibility and labels ─────────────────────────────────────────
    numVisibleKnobs = juce::jmin(numMapped, MAX_KNOBS);

    for (int i = 0; i < MAX_KNOBS; ++i)
    {
        const bool on = i < numVisibleKnobs;
        paramSliders[static_cast<size_t>(i)].setVisible(on);
        paramNameLabels[static_cast<size_t>(i)].setVisible(on);

        if (!on)
            continue;

        paramNameLabels[static_cast<size_t>(i)].setText(
            juce::String(params[static_cast<size_t>(i)].label),
            juce::dontSendNotification);
    }
}

void ParamGridPanel::resized()
{
    // 4 columns × 2 rows. Panel-local coordinates: the shell places this panel
    // so that (0,0) here sits exactly where the old grid's origin was, keeping
    // knob positions pixel-identical to the pre-split layout.
    auto area = getLocalBounds();
    const int cols  = 4;
    const int cellW = area.getWidth() / cols;
    const int cellH = 95;
    for (int i = 0; i < MAX_KNOBS; ++i)
    {
        auto cell = juce::Rectangle<int>(area.getX() + (i % cols) * cellW,
                                         area.getY() + (i / cols) * cellH,
                                         cellW, cellH);
        paramNameLabels[static_cast<size_t>(i)].setBounds(cell.removeFromTop(16));
        paramSliders[static_cast<size_t>(i)].setBounds(cell.reduced(4));
    }
}
