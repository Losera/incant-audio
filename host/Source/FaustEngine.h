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

        // ── Enclosing group path ────────────────────────────────────────────
        // Slash-joined names of the hgroup/vgroup/tgroup boxes this parameter
        // sits inside, OUTERMOST FIRST, with the compiler-inserted outermost box
        // dropped -- Faust always wraps the whole UI in one box named after the
        // .dsp file, which is a filename and not a section heading. A parameter
        // declared outside any group therefore has an EMPTY path, and that is
        // the case a sectioned layout must fall back on.
        //
        // Faust reports this through openVerticalBox/openHorizontalBox/openTabBox
        // and closeBox, interleaved with the widget adds. Verified against
        // `faust -lang cpp` output for host/tests/ui_fixtures/04_generator_grouped.dsp,
        // which emits (generated C++, buildUserInterface):
        //     openVerticalBox("04_generator_grouped");
        //       openVerticalBox("Env");
        //         addHorizontalSlider("Attack", ...);   -> group == "Env"
        //       closeBox();
        //     ...
        //     closeBox();
        // Note this needs NO [group:...] metadata: ordinary vgroup()/hgroup() in
        // the DSL is enough, so a model that writes idiomatic Faust already
        // produces it.
        //
        // Ordering note: Faust emits the groups ALPHABETICALLY, and the widgets
        // alphabetically inside each. The editor does not sort -- it preserves
        // this order exactly. What has been recorded as "lexicographic knob
        // ordering" (PF-038) is therefore Faust's own path ordering, not
        // anything ParamGridPanel does.
        std::string group;

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

    // ── The voice contract ──────────────────────────────────────────────────
    // Faust's polyphony layer identifies an instrument by the SUFFIX of a
    // control's path (dsp_voice::extractPaths, /usr/include/faust/dsp/poly-dsp.h
    // :233-254): "/gate", "/freq" or "/key", "/gain" or "/vel"|"/velocity".
    // A patch is an instrument only when all THREE roles are present -- Faust's
    // own predicate (MidiUI.h:115-132, has_freq && has_gate && has_gain).
    //
    // Requiring all three is what keeps this safe for the existing corpus: a
    // sine-oscillator effect with a slider literally named "freq" and no gate is
    // NOT an instrument, so nothing about it changes.
    //
    // ⚠️ Matched case-sensitively against the exact lowercase names, deliberately.
    // Being lenient here would make more generated patches playable TODAY and
    // then silently diverge in phase 1, when Faust's own dsp_poly does the
    // binding and ignores anything it does not recognise. Our detection and
    // Faust's must agree; the prompt is the place to teach the spelling.
    //
    // The two flags record which spelling was used, because the UNITS differ:
    // "/freq" wants Hz and "/key" the raw note number; "/gain" wants 0-1 and
    // "/vel" the raw 0-127 velocity.
    struct VoiceControls
    {
        FAUSTFLOAT* gate = nullptr;
        FAUSTFLOAT* freq = nullptr;
        FAUSTFLOAT* gain = nullptr;
        bool freqIsKey = false;
        bool gainIsVel = false;

        bool valid() const { return gate != nullptr && freq != nullptr && gain != nullptr; }
    };

    // True when the LIVE DSP declares the full voice contract. Safe to call from
    // the audio thread inside the enterAudio()/exitAudio() bracket.
    bool isInstrument() const { return voiceValid.load(std::memory_order_relaxed); }

    // ⚠️ AUDIO THREAD ONLY, and only inside the enterAudio()/exitAudio() bracket.
    // These write DSP zones directly -- the same memory pushToFaust writes and
    // compute() reads -- so they carry exactly the same rules: no allocation, no
    // locks, and never while a swap could be in flight. The bracket is what makes
    // that true; calling these from the message thread races the compile worker.
    //
    // Monophonic in this phase: one note at a time, last-note-priority. Phase 1
    // replaces the bodies with dsp_poly voice allocation, and processBlock's MIDI
    // walk does not change.
    void noteOn(int note, int velocity);
    void noteOff(int note);
    void allNotesOff();

    // allNotesOff() for callers that are NOT already inside the bracket --
    // prepareToPlay and reset(), which run on the message thread.
    //
    // Without this, a note held when the transport stops or the host re-prepares
    // leaves *gate == 1 with no further MIDI able to clear it, and the patch
    // drones forever. The MIDI walk cannot help: it only runs when events arrive.
    //
    // Brackets internally with enterAudio()/exitAudio(). audioBusy is a COUNTER
    // (fetch_add/fetch_sub, :203/:212), so an extra holder from another thread is
    // exactly what the drain guard is built to tolerate. If a compile is mid-swap
    // enterAudio() returns false and this is a no-op -- which is correct, not a
    // missed case: the incoming DSP's gate zone starts at its declared default
    // and runCompile clears currentNote in the same swap.
    void silenceVoice();

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

    // Test-only. The rate the LIVE DSP is actually running at (Faust's own
    // getSampleRate(), faust/dsp/dsp.h:114), or 0 when none is live. PF-018 is
    // exactly the statement "this tracks prepare()", and before the fix it did
    // not — so asserting it needs the DSP's opinion, not our stored member.
    //
    // NOT audio-thread safe and not meant to be: call it from a test thread with
    // no compile in flight.
    int liveDspSampleRateForTest() const;

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

    // The host's channel count, and therefore the most a patch may declare in
    // either direction. The output bus is fixed stereo; the INPUT bus is required
    // on the Fx target and optional on the instrument target (PluginProcessor.cpp,
    // `.withInput(..., PF_IS_SYNTH == 0)`). So this is not a tunable — it is a
    // statement about what processBlock can actually serve, and it is what
    // isBusesLayoutSupported() enforces against the host's proposed layout.
    static constexpr int kMaxChannels = 2;

private:
    double              sr     = 44100.0;
    int                 block  = 512;
    std::atomic<bool>   ready  { false };
    std::atomic<int>    audioBusy { 0 };   // in-flight audio-thread sections (see enterAudio)

    llvm_dsp_factory*      factory   = nullptr;
    std::atomic<llvm_dsp*> activeDSP { nullptr };
    MapUI                  activeUI;

    // ── Arity of the LIVE DSP ───────────────────────────────────────────────
    // Faust's process may declare any channel count; the host has exactly
    // kMaxChannels. These are published in the swap alongside activeDSP and read
    // on the audio thread, so process() can route rather than assume.
    //
    // SUBTLE: relaxed is correct on both sides for the same reason activeDSP is
    // read relaxed — the acquire on `ready` (enterAudio, then process) already
    // synchronises every write the compile thread made before its release store
    // in Step 6. They are atomic rather than plain ints so that the read is not
    // a data race under TSan when a compile is parked mid-swap.
    std::atomic<int> dspNumIns  { 0 };
    std::atomic<int> dspNumOuts { 0 };

    // ── Voice-control zones of the LIVE DSP ─────────────────────────────────
    // Published in the swap alongside activeDSP, read on the audio thread under
    // the same acquire on `ready`, so relaxed is correct on both sides (see the
    // dspNumIns/dspNumOuts note above). Atomic rather than plain so the read is
    // not a data race under TSan when a compile is parked mid-swap.
    //
    // ⚠️ LIFETIME: these point into the live DSP instance, exactly like
    // ParamInfo::zone, and dangle the moment it is deleted. The drain in the
    // swap is what makes reading them safe.
    std::atomic<FAUSTFLOAT*> voiceGate { nullptr };
    std::atomic<FAUSTFLOAT*> voiceFreq { nullptr };
    std::atomic<FAUSTFLOAT*> voiceGain { nullptr };
    std::atomic<bool>        voiceFreqIsKey { false };
    std::atomic<bool>        voiceGainIsVel { false };
    std::atomic<bool>        voiceValid     { false };

    // Which note is sounding, for last-note-priority release.
    //
    // Audio thread, PLUS one write from the compile worker: runCompile clears it
    // to -1 in the swap, because the note it names belongs to the DSP that is
    // about to be deleted. That write is not a race and does not need an atomic,
    // but the reason is the swap protocol rather than exclusive ownership, so it
    // has to be stated: the clear happens after the audioBusy drain (no audio
    // section is in flight) and before Step 6's `ready` store-release, which the
    // audio thread acquires in enterAudio() before it may call noteOn/noteOff.
    // That release/acquire pair is what orders the clear against every later
    // audio-thread access.
    //
    // ⚠️ Moving this write outside the drain window makes it a genuine data race.
    int currentNote = -1;

    // Output staging for patches whose arity does not match the host's. Faust
    // only contracts compute() for in-place use (inputs == outputs) when the two
    // arities agree (faust/dsp/dsp.h:192); a 1-in/2-out patch would otherwise
    // have output 0 sharing storage with input 0. Sized in prepare() — never in
    // process(), which is the audio thread.
    juce::AudioBuffer<float> scratch;

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
