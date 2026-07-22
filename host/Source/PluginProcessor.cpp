#include "PluginProcessor.h"
#include "PluginEditor.h"

PluginForgeProcessor::PluginForgeProcessor()
    : AudioProcessor(BusesProperties()
          .withInput ("Input",  juce::AudioChannelSet::stereo(), true)
          .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "STATE", createParameterLayout()),
      paramPool(apvts)
{
}

PluginForgeProcessor::~PluginForgeProcessor() {}

juce::AudioProcessorValueTreeState::ParameterLayout
PluginForgeProcessor::createParameterLayout()
{
    // Params are created here (not in ParamPool) because the apvts constructor
    // needs the full ParameterLayout up front -- see ParamPool.cpp's constructor
    // comment for what went wrong when ParamPool tried to add them afterward via
    // the legacy createAndAddParameter(). ParamPool::slotId() is the single shared
    // ID scheme both sides use.
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;
    params.reserve(static_cast<size_t>(ParamPool::POOL_SIZE));

    for (int i = 0; i < ParamPool::POOL_SIZE; ++i)
    {
        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            ParamPool::slotId(i),               // paramID (stable — DAW automation uses this)
            "Macro " + juce::String(i + 1),     // display name
            0.0f, 1.0f, 0.0f
        ));
    }

    return { params.begin(), params.end() };
}

void PluginForgeProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    faustEngine.prepare(sampleRate, samplesPerBlock);
    outputGuard.prepare(sampleRate);
}

void PluginForgeProcessor::releaseResources()
{
    faustEngine.release();
}

void PluginForgeProcessor::processBlock(juce::AudioBuffer<float>& buffer,
                                         juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    // SUBTLE: this bracket is what lets FaustEngine::compile() safely mutate
    // activeDSP/activeUI — it drains audioBusy to zero after ready=false. Every
    // engine touch on the audio thread must sit between enterAudio()/exitAudio();
    // adding an engine call outside the bracket reintroduces the swap race.
    if (!faustEngine.enterAudio())
    {
        // No DSP live: input passes through untouched; still feed the meter so
        // the editor shows the dry signal (audio-path-alive indicator).
        outputLevel.store(buffer.getMagnitude(0, buffer.getNumSamples()),
                          std::memory_order_relaxed);
        return;
    }

    paramPool.pushToFaust(faustEngine);
    faustEngine.process(buffer);

    // SUBTLE: the guard runs ONLY on the generated-DSP path, deliberately. The
    // early-return above passes the host's own input through untouched, and
    // limiting or DC-blocking a user's dry signal would be a bug, not a safety
    // measure. Everything this guards against originates in machine-generated
    // Faust. Kept inside the enterAudio bracket so its cost is included in the
    // drain the compile thread waits on.
    outputGuard.process(buffer);

    faustEngine.exitAudio();

    outputLevel.store(buffer.getMagnitude(0, buffer.getNumSamples()),
                      std::memory_order_relaxed);
}

void PluginForgeProcessor::loadFaustCode(const juce::String& faustCode)
{
    faustEngine.compile(faustCode, [this](const FaustEngine::ParamList& params,
                                          const std::string& error) {
        if (error.empty())
        {
            paramPool.remap(params);

            // A new patch gets a clean verdict: clear any latched mute from the
            // one it replaces.
            //
            // SUBTLE: reset() writes non-atomic filter state (dcX1/dcY1/
            // runawayBlocks) from the COMPILE thread, which would be a data race
            // against the audio thread anywhere else. It is safe at exactly this
            // point and nowhere else: this callback runs at FaustEngine::compile
            // step 5 -- after the audioBusy drain (step 2) proved no audio-thread
            // section is in flight, and before ready=true (step 6) lets a new one
            // start. The same drain that makes the activeDSP/activeUI swap safe
            // covers this. Do not move this call outside the compile callback.
            outputGuard.reset();
            if (onFaustCompileSuccess)
                onFaustCompileSuccess(params);
        }
        else
        {
            juce::Logger::writeToLog("FaustEngine compile error: " + juce::String(error));
            if (onFaustCompileError)
                onFaustCompileError(juce::String(error));
        }
    });
}

juce::AudioProcessorEditor* PluginForgeProcessor::createEditor()
{
    return new PluginForgeEditor(*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new PluginForgeProcessor();
}
