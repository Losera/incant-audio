#include "FaustEngine.h"
#include <thread>
#include <faust/dsp/llvm-dsp.h>
#include <faust/gui/UI.h>

// ---------------------------------------------------------------------------
// ParamCapture — minimal UI that records slider metadata during buildUserInterface.
// MapUI stores zone pointers but discards init/min/max/step; this captures all four.
// ---------------------------------------------------------------------------
struct ParamCapture : public UI
{
    FaustEngine::ParamList params;

    void addHorizontalSlider(const char* label, FAUSTFLOAT*, FAUSTFLOAT init,
                              FAUSTFLOAT fmin, FAUSTFLOAT fmax, FAUSTFLOAT step) override
    {
        params.push_back({ label, float(init), float(fmin), float(fmax), float(step) });
    }
    void addVerticalSlider(const char* label, FAUSTFLOAT*, FAUSTFLOAT init,
                            FAUSTFLOAT fmin, FAUSTFLOAT fmax, FAUSTFLOAT step) override
    {
        params.push_back({ label, float(init), float(fmin), float(fmax), float(step) });
    }
    void addNumEntry(const char* label, FAUSTFLOAT*, FAUSTFLOAT init,
                     FAUSTFLOAT fmin, FAUSTFLOAT fmax, FAUSTFLOAT step) override
    {
        params.push_back({ label, float(init), float(fmin), float(fmax), float(step) });
    }
    void addButton(const char* label, FAUSTFLOAT*) override
    {
        params.push_back({ label, 0.0f, 0.0f, 1.0f, 1.0f });
    }
    void addCheckButton(const char* label, FAUSTFLOAT*) override
    {
        params.push_back({ label, 0.0f, 0.0f, 1.0f, 1.0f });
    }
    void addHorizontalBargraph(const char*, FAUSTFLOAT*, FAUSTFLOAT, FAUSTFLOAT) override {}
    void addVerticalBargraph(const char*, FAUSTFLOAT*, FAUSTFLOAT, FAUSTFLOAT) override {}
    void addSoundfile(const char*, const char*, Soundfile**) override {}
    void openTabBox(const char*) override {}
    void openHorizontalBox(const char*) override {}
    void openVerticalBox(const char*) override {}
    void closeBox() override {}
    void declare(FAUSTFLOAT*, const char*, const char*) override {}
};

// ---------------------------------------------------------------------------

FaustEngine::~FaustEngine()
{
    delete activeDSP.load();
    if (factory)
        deleteDSPFactory(factory);
}

void FaustEngine::prepare(double sampleRate, int blockSize)
{
    sr    = sampleRate;
    block = blockSize;
}

void FaustEngine::release()
{
    ready.store(false, std::memory_order_release);
}

void FaustEngine::setParamValue(const std::string& label, float value)
{
    activeUI.setParamValue(label, value);
}

void FaustEngine::process(juce::AudioBuffer<float>& buffer)
{
    // load-acquire pairs with the store-release in compile() after the DSP swap.
    if (!ready.load(std::memory_order_acquire))
        return;

    // relaxed is safe here: the acquire on ready already synchronised all writes
    // the compile thread made before setting ready=true (including the new DSP pointer).
    llvm_dsp* dsp = activeDSP.load(std::memory_order_relaxed);

    // In-place: write pointers serve as both input and output.
    // const_cast removes the pointer-level const from float* const* → float**.
    float** io = const_cast<float**>(buffer.getArrayOfWritePointers());
    dsp->compute(buffer.getNumSamples(), io, io);
}

void FaustEngine::compile(const juce::String& faustCode, CompileCallback cb)
{
    std::thread([this, code = faustCode.toStdString(), cb]() mutable
    {
        // compileMutex prevents two concurrent compiles from racing inside libfaust.
        // createDSPFactoryFromString is not thread-safe (per llvm-dsp.h header comment).
        // This lock is never held on the audio thread.
        std::lock_guard<std::mutex> lock(compileMutex);

        std::string errorMsg;
        llvm_dsp_factory* f = createDSPFactoryFromString(
            "dsp", code, 0, nullptr, "", errorMsg, -1);

        if (!f)
        {
            cb({}, errorMsg);
            return;
        }

        llvm_dsp* dsp = f->createDSPInstance();
        if (!dsp)
        {
            deleteDSPFactory(f);
            cb({}, "createDSPInstance() returned null");
            return;
        }

        dsp->init(static_cast<int>(sr));

        // Capture parameter metadata (min/max/default/step) for the ParamPool.
        ParamCapture capture;
        dsp->buildUserInterface(&capture);

        // Build the MapUI for audio-thread parameter writes.
        // SUBTLE: newUI holds raw float* pointers into dsp's internal memory.
        // It becomes invalid the moment dsp is deleted — they are always swapped together.
        MapUI newUI;
        dsp->buildUserInterface(&newUI);

        // ── Atomic swap ──────────────────────────────────────────────────────────
        // Protocol rationale (both bugs this replaced): docs/fixplan_pushtofaust_swap.md.
        //
        // Step 1: signal not-ready so no NEW audio-thread section starts.
        // SUBTLE: seq_cst, not release — this store must be globally ordered before
        // the audioBusy load in Step 2 (store→load handshake with enterAudio(); a
        // release store can reorder past that load and both threads see stale values).
        ready.store(false, std::memory_order_seq_cst);

        // Step 2: drain in-flight audio-thread sections. processBlock() brackets all
        // engine use in enterAudio()/exitAudio(); once audioBusy hits zero, no reader
        // holds activeDSP or activeUI. Spinning is fine here: this is the detached
        // compile thread (never the audio thread) and the wait is bounded by one
        // audio callback.
        while (audioBusy.load(std::memory_order_seq_cst) != 0)
            std::this_thread::yield();

        // Step 3: swap the DSP pointer. acq_rel acquires the old value (deleted in
        // Step 7) and releases the new value to the audio thread.
        llvm_dsp* old = activeDSP.exchange(dsp, std::memory_order_acq_rel);

        // Step 4: swap the MapUI. Plain move is safe now — the drain in Step 2
        // proved no audio-thread call is inside setParamValue() on the old one
        // (this closes the TOCTOU reported 2026-07-18).
        activeUI = std::move(newUI);

        // Step 5: swap factory ownership, then publish the new param labels while
        // ready is still false. cb → ParamPool::remap() runs here, on this thread,
        // inside compileMutex — so any block that later observes ready==true sees
        // the new DSP and the new labels together, never the old-labels/new-DSP
        // mismatch that caused the setParamValue-not-found spam.
        llvm_dsp_factory* oldFactory = factory;
        factory = f;
        cb(capture.params, "");

        // Step 6: mark ready. store-release pairs with load-acquire in enterAudio()/
        // process(): all writes above (activeDSP, activeUI, factory, labels) are
        // visible to the audio thread after it observes ready=true.
        ready.store(true, std::memory_order_release);

        // Step 7: free the old objects — off the audio thread (heap deallocation is
        // not RT-safe), and after ready=true so the audio gap excludes the multi-ms
        // LLVM factory teardown. Safe: old/oldFactory became unreachable in Steps
        // 3–5 and the drain guarantees no reader still holds them.
        delete old;
        if (oldFactory)
            deleteDSPFactory(oldFactory);
    }).detach();
}
