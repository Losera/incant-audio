#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <faust/dsp/llvm-dsp.h>
#include <faust/gui/MapUI.h>
#include <functional>
#include <string>
#include <vector>
#include <atomic>
#include <memory>
#include <mutex>

class FaustEngine
{
public:
    struct ParamInfo
    {
        std::string label;
        float       defaultValue;
        float       min;
        float       max;
        float       step;
    };

    using ParamList = std::vector<ParamInfo>;
    using CompileCallback = std::function<void(const ParamList&, const std::string& error)>;

    void setParamValue(const std::string& label, float value);

    FaustEngine()  = default;
    ~FaustEngine();

    void prepare(double sampleRate, int blockSize);
    void release();
    void process(juce::AudioBuffer<float>& buffer);

    // Async — compiles on a detached background thread; cb fires on that compile
    // thread (NOT the message thread), inside compileMutex, before ready=true.
    void compile(const juce::String& faustCode, CompileCallback cb);

    bool isReady() const { return ready.load(std::memory_order_acquire); }

    // Audio-thread guard bracketing every use of the engine from processBlock().
    // The compile thread drains audioBusy to zero (after ready=false) before it
    // mutates activeDSP/activeUI or deletes the old DSP — see compile().
    //
    // SUBTLE: increment-THEN-check order, and seq_cst on this handshake, are both
    // load-bearing. This is a two-flag store→load handshake (Dekker): audio thread
    // does fetch_add(audioBusy) then load(ready); compile thread does
    // store(ready=false) then load(audioBusy). With anything weaker than seq_cst,
    // store-load reordering lets both threads see the other's OLD value — audio
    // proceeds with ready==true while compile sees audioBusy==0 and mutates under
    // it. Checking ready before incrementing has the same hole.
    bool enterAudio()
    {
        audioBusy.fetch_add(1, std::memory_order_seq_cst);
        if (!ready.load(std::memory_order_seq_cst))
        {
            audioBusy.fetch_sub(1, std::memory_order_release);
            return false;
        }
        return true;
    }

    void exitAudio() { audioBusy.fetch_sub(1, std::memory_order_release); }

private:
    double              sr     = 44100.0;
    int                 block  = 512;
    std::atomic<bool>   ready  { false };
    std::atomic<int>    audioBusy { 0 };   // in-flight audio-thread sections (see enterAudio)

    llvm_dsp_factory*      factory   = nullptr;
    std::atomic<llvm_dsp*> activeDSP { nullptr };
    MapUI                  activeUI;

    // SUBTLE: compileMutex is held only on the compile thread, never on the audio thread.
    // createDSPFactoryFromString is not thread-safe per faust/dsp/llvm-dsp.h —
    // this mutex prevents two concurrent compile() calls from racing inside libfaust.
    std::mutex compileMutex;
};
