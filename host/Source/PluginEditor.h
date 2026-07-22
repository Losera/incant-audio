#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include <array>
#include <memory>

class PluginForgeEditor : public juce::AudioProcessorEditor,
                          private juce::Timer
{
public:
    explicit PluginForgeEditor(PluginForgeProcessor&);
    ~PluginForgeEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    // 30Hz UI tick: pulls processor.outputLevel (relaxed atomic, written on the
    // audio thread) into displayLevel with instant attack / exponential decay,
    // and repaints only the meter strip.
    void timerCallback() override;

    PluginForgeProcessor& processor;

    juce::TextEditor promptInput;
    juce::TextButton  generateButton { "Generate" };
    juce::Label       statusLabel;

    juce::Rectangle<int> meterBounds;      // set in resized(), painted in paint()
    float displayLevel = 0.0f;             // message-thread only

    // Edge-detect for the output guard's latched mute, polled by timerCallback.
    // Stored so the status label is written only on a transition -- rewriting it
    // at 30Hz would stomp compile/error messages every frame.
    bool wasOutputMuted = false;           // message-thread only

    // ── Parameter knobs ─────────────────────────────────────────────────────
    // Bound once (constructor) to pool slots macro_0..macro_(MAX_KNOBS-1) via
    // SliderAttachment; hidden until a compile succeeds, then labelled and
    // shown by refreshParamKnobs(). Message-thread only.
    static constexpr int MAX_KNOBS = 8;
    std::array<juce::Slider, MAX_KNOBS> paramSliders;
    std::array<juce::Label,  MAX_KNOBS> paramNameLabels;
    std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>,
               MAX_KNOBS> paramAttachments;
    int numVisibleKnobs = 0;

    void refreshParamKnobs(const FaustEngine::ParamList& params);

    // ── LLM bridge ───────────────────────────────────────────────────────────
    // Resolved once in the constructor; empty juce::File (existsAsFile() == false)
    // if not found. juce::Component::SafePointer is available here via
    // juce_audio_processors.h, which pulls in juce_gui_basics.h itself.
    juce::File generateScript;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginForgeEditor)
};
