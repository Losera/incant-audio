#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <faust/dsp/llvm-dsp.h>
#include <faust/gui/MapUI.h>
#include <functional>
#include <string>
#include <vector>
#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>

class FaustEngine
{
public:
    // Which Faust UI primitive declared this parameter. Faust's buildUserInterface
    // reports slider/button/checkbox through distinct callbacks, but the metadata
    // capture used to flatten them all into a bare min/max/step range — so a
    // toggle was indistinguishable from a continuous control downstream.
    // Auto-layout (docs/ui_design_plan.md §3) needs the distinction to pick a
    // widget, so it is preserved here rather than re-inferred from the range.
    enum class Kind
    {
        HSlider,
        VSlider,
        NumEntry,
        Button,      // momentary — resets to 0 when released
        CheckButton  // latching toggle
    };

    // Value-curve requested by the patch via [scale:log] / [scale:exp].
    // Faust strips metadata from the label and delivers it through
    // UI::declare(zone, "scale", ...) immediately BEFORE the widget's add call
    // (verified against faust -lang cpp output, 2026-07-21) -- so this can only
    // be populated by a UI that implements declare(), never by parsing labels.
    enum class Scale { None, Log, Exp };

    struct ParamInfo
    {
        std::string label;
        float       defaultValue;
        float       min;
        float       max;
        float       step;
        Kind        kind;

        // ── Declared metadata (see Scale above) ─────────────────────────────
        Scale       scale = Scale::None;
        std::string unit;                 // "Hz", "dB", "ms", ... ; drives the
                                          // default curve when scale == None
        bool        isMenu = false;       // [style:menu{...}] -- discrete indices

        // Direct pointer into the owning DSP instance's memory, captured during
        // buildUserInterface. This is what makes pushToFaust RT-safe: writing
        // *zone replaces a string-keyed MapUI lookup (up to three std::map
        // probes per parameter per block, plus an fprintf on miss -- an I/O
        // syscall on the audio thread).
        //
        // ⚠️ LIFETIME: valid only while the DSP instance that produced it is
        // alive. It dangles the moment that instance is deleted, so it may only
        // ever be read from the buffer ParamPool published for the CURRENT DSP.
        // FaustEngine::compile's drain + double-buffer swap is what makes that
        // safe; see ParamPool::remap.
        FAUSTFLOAT* zone = nullptr;
    };

    using ParamList = std::vector<ParamInfo>;
    using CompileCallback = std::function<void(const ParamList&, const std::string& error)>;

    void setParamValue(const std::string& label, float value);

    FaustEngine()  = default;
    ~FaustEngine();

    void prepare(double sampleRate, int blockSize);
    void release();
    void process(juce::AudioBuffer<float>& buffer);

    // Async — queues a compile on the engine's single persistent worker thread.
    // cb fires on that worker (NOT the message thread), inside compileMutex,
    // before ready=true. Returns immediately.
    //
    // Only the NEWEST queued request survives: the pending slot holds one job, so
    // a rapid sequence of Generate clicks compiles the last prompt and silently
    // drops the superseded ones rather than queueing a backlog the user has
    // already moved past.
    void compile(const juce::String& faustCode, CompileCallback cb);

    // Stops the worker and joins it. Idempotent, safe to call from any thread
    // except the worker itself.
    //
    // ⚠️ CALL THIS BEFORE ANYTHING THE CALLBACK TOUCHES IS DESTROYED.
    // The compile callback reaches into ParamPool and the processor's
    // onFaustCompile* handlers. ~FaustEngine calls shutdown() as a backstop, but
    // by then sibling members may already be gone: members are destroyed in
    // reverse declaration order, and FaustEngine is declared before ParamPool in
    // PluginProcessor.h, so ~FaustEngine runs LAST. PluginForgeProcessor's
    // destructor therefore calls this explicitly, first.
    void shutdown();

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
    // The single worker already serialises compiles, so it is now uncontended;
    // it is kept because ParamPool::remap's single-writer argument is written in
    // terms of it, and because it fails safe if a second worker is ever added.
    std::mutex compileMutex;

    // ── Compile worker ──────────────────────────────────────────────────────
    // Replaces the per-call `std::thread(...).detach()`, which had no owner and
    // no join: on 2026-07-22 CI run 29883556305, ThreadSanitizer caught the main
    // thread running static destructors at process exit while a detached compile
    // thread was still live (race at FaustEngine.cpp:192 against JUCE's
    // LeakedObjectDetector teardown). A detached thread capturing `this` cannot
    // be made safe by ordering alone — it has to be joined.
    //
    // Single pending slot rather than a queue: superseded compiles are worthless,
    // and a queue would make the plugin work through a backlog of prompts the
    // user has already replaced.
    void workerLoop();
    void runCompile(const std::string& code, const CompileCallback& cb);

    std::thread             worker;
    std::mutex              jobMutex;
    std::condition_variable jobCv;
    std::string             pendingCode;
    CompileCallback         pendingCb;
    bool                    hasJob   = false;
    bool                    stopping = false;   // guarded by jobMutex
};
