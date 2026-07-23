#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include <array>
#include <memory>

// ── ParamGridPanel ──────────────────────────────────────────────────────────
// The parameter-control surface of the editor. Owns the knob grid and the
// slot-seeding logic that runs on every successful compile.
//
// This is currently a mechanical lift of the fixed 8-knob grid out of the
// old monolithic PluginForgeEditor (docs/FLEET.md Wave 0, Task 0): ZERO
// behaviour change. The deterministic auto-layout (ui_design_plan.md §3 —
// cols=clamp(ceil(sqrt(N)),2,6), kind-aware widgets, N>24 scrolling, lifting
// MAX_KNOBS) lands on top of this seam in Wave 1.
//
// Message-thread only. refreshParamKnobs() is invoked by the shell from inside
// its onFaustCompileSuccess callAsync hop.
class ParamGridPanel : public juce::Component
{
public:
    explicit ParamGridPanel(PluginForgeProcessor&);

    void resized() override;

    // Called by the shell when a compile succeeds, with the full captured
    // param list (already copied onto the message thread by the shell).
    void refreshParamKnobs(const FaustEngine::ParamList& params);

private:
    PluginForgeProcessor& processor;

    // Bound once (constructor) to pool slots macro_0..macro_(MAX_KNOBS-1) via
    // SliderAttachment; hidden until a compile succeeds, then labelled and
    // shown by refreshParamKnobs().
    static constexpr int MAX_KNOBS = 8;
    std::array<juce::Slider, MAX_KNOBS> paramSliders;
    std::array<juce::Label,  MAX_KNOBS> paramNameLabels;
    std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>,
               MAX_KNOBS> paramAttachments;
    int numVisibleKnobs = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ParamGridPanel)
};
