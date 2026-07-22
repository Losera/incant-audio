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

    // ── Pending declare() metadata ──────────────────────────────────────────
    // Faust emits metadata as declare(zone, key, value) calls immediately
    // BEFORE the widget they describe, all carrying that widget's zone pointer:
    //
    //     declare(&fHslider0, "scale", "log");
    //     declare(&fHslider0, "unit",  "Hz");
    //     addHorizontalSlider("Cutoff", &fHslider0, 1e3, 2e1, 2e4, 1.0);
    //
    // (Verified against `faust -lang cpp` output, 2026-07-21.) Note the label
    // reaching addHorizontalSlider is "Cutoff" -- the bracketed metadata is
    // already stripped, so it CANNOT be recovered by parsing the label. This
    // accumulator is the only way to see it. Mirrors APIUI's fCurrentScale
    // approach, including resetting after each widget consumes it.
    FAUSTFLOAT*         pendingZone  = nullptr;
    FaustEngine::Scale  pendingScale = FaustEngine::Scale::None;
    std::string         pendingUnit;
    bool                pendingIsMenu = false;

    void declare(FAUSTFLOAT* zone, const char* key, const char* value) override
    {
        // A declare for a different zone means the previous widget never
        // arrived (bargraph, or a form we ignore); drop the stale accumulation
        // rather than letting it leak onto the next widget.
        if (zone != pendingZone)
        {
            pendingZone   = zone;
            pendingScale  = FaustEngine::Scale::None;
            pendingUnit.clear();
            pendingIsMenu = false;
        }

        const std::string k = key ? key : "";
        const std::string v = value ? value : "";

        if (k == "scale")
        {
            if (v == "log")      pendingScale = FaustEngine::Scale::Log;
            else if (v == "exp") pendingScale = FaustEngine::Scale::Exp;
        }
        else if (k == "unit")
        {
            pendingUnit = v;
        }
        else if (k == "style")
        {
            // "menu{'A':0;'B':1}" / "radio{...}" -- both are discrete choosers.
            pendingIsMenu = v.rfind("menu", 0) == 0 || v.rfind("radio", 0) == 0;
        }
    }

    // Attaches whatever declare() accumulated for this zone, then clears it so
    // the next widget starts clean.
    FaustEngine::ParamInfo consume(const char* label, FAUSTFLOAT* zone,
                                   float init, float fmin, float fmax, float step,
                                   FaustEngine::Kind kind)
    {
        FaustEngine::ParamInfo info { label ? label : "", init, fmin, fmax, step, kind };
        info.zone = zone;

        if (zone != nullptr && zone == pendingZone)
        {
            info.scale  = pendingScale;
            info.unit   = pendingUnit;
            info.isMenu = pendingIsMenu;
        }

        pendingZone   = nullptr;
        pendingScale  = FaustEngine::Scale::None;
        pendingUnit.clear();
        pendingIsMenu = false;
        return info;
    }

    void addHorizontalSlider(const char* label, FAUSTFLOAT* zone, FAUSTFLOAT init,
                              FAUSTFLOAT fmin, FAUSTFLOAT fmax, FAUSTFLOAT step) override
    {
        params.push_back(consume(label, zone, float(init), float(fmin), float(fmax),
                                 float(step), FaustEngine::Kind::HSlider));
    }
    void addVerticalSlider(const char* label, FAUSTFLOAT* zone, FAUSTFLOAT init,
                            FAUSTFLOAT fmin, FAUSTFLOAT fmax, FAUSTFLOAT step) override
    {
        params.push_back(consume(label, zone, float(init), float(fmin), float(fmax),
                                 float(step), FaustEngine::Kind::VSlider));
    }
    void addNumEntry(const char* label, FAUSTFLOAT* zone, FAUSTFLOAT init,
                     FAUSTFLOAT fmin, FAUSTFLOAT fmax, FAUSTFLOAT step) override
    {
        params.push_back(consume(label, zone, float(init), float(fmin), float(fmax),
                                 float(step), FaustEngine::Kind::NumEntry));
    }
    void addButton(const char* label, FAUSTFLOAT* zone) override
    {
        params.push_back(consume(label, zone, 0.0f, 0.0f, 1.0f, 1.0f,
                                 FaustEngine::Kind::Button));
    }
    void addCheckButton(const char* label, FAUSTFLOAT* zone) override
    {
        params.push_back(consume(label, zone, 0.0f, 0.0f, 1.0f, 1.0f,
                                 FaustEngine::Kind::CheckButton));
    }
    void addHorizontalBargraph(const char*, FAUSTFLOAT*, FAUSTFLOAT, FAUSTFLOAT) override {}
    void addVerticalBargraph(const char*, FAUSTFLOAT*, FAUSTFLOAT, FAUSTFLOAT) override {}
    void addSoundfile(const char*, const char*, Soundfile**) override {}
    void openTabBox(const char*) override {}
    void openHorizontalBox(const char*) override {}
    void openVerticalBox(const char*) override {}
    void closeBox() override {}
};

// ---------------------------------------------------------------------------

FaustEngine::~FaustEngine()
{
    // Backstop only — PluginForgeProcessor's destructor calls shutdown() first,
    // while the objects the compile callback touches are still alive. See the
    // warning on shutdown() in FaustEngine.h.
    shutdown();

    delete activeDSP.load();
    if (factory)
        deleteDSPFactory(factory);
}

void FaustEngine::shutdown()
{
    {
        std::lock_guard<std::mutex> lock(jobMutex);
        if (stopping)
            return;                 // idempotent
        stopping = true;
        hasJob   = false;           // drop anything queued but not started
        pendingCb = nullptr;
    }
    jobCv.notify_all();

    if (worker.joinable())
        worker.join();
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
    {
        std::lock_guard<std::mutex> lock(jobMutex);
        if (stopping)
            return;                 // shutting down: drop the request

        // Single slot: a newer request replaces an older un-started one. Rapid
        // Generate clicks should land on the last prompt, not work through a
        // backlog of prompts the user has already abandoned.
        pendingCode = faustCode.toStdString();
        pendingCb   = std::move(cb);
        hasJob      = true;

        if (!worker.joinable())     // started lazily, on first use
            worker = std::thread([this] { workerLoop(); });
    }
    jobCv.notify_one();
}

void FaustEngine::workerLoop()
{
    for (;;)
    {
        std::string     code;
        CompileCallback cb;
        {
            std::unique_lock<std::mutex> lock(jobMutex);
            jobCv.wait(lock, [this] { return hasJob || stopping; });
            if (stopping)
                return;
            code   = std::move(pendingCode);
            cb     = std::move(pendingCb);
            hasJob = false;
        }

        runCompile(code, cb);
    }
}

void FaustEngine::runCompile(const std::string& code, const CompileCallback& cb)
{
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

        // Bail out before publishing if shutdown began while libfaust was working.
        // Past this point the protocol calls cb(), which reaches into ParamPool and
        // the processor's handlers — the very objects a teardown is dismantling.
        // Discarding a compile nobody will hear is free; calling back into a
        // half-destroyed processor is not.
        {
            std::lock_guard<std::mutex> lock(jobMutex);
            if (stopping)
            {
                delete dsp;
                deleteDSPFactory(f);
                return;
            }
        }

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
        // holds activeDSP or activeUI. Spinning is fine here: this is the compile
        // worker (never the audio thread) and the wait is bounded by one audio
        // callback.
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
    }
}
