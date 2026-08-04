#include "FaustEngine.h"
#include "ParamIdentity.h"
#include "VoiceContract.generated.h"
#include <cmath>
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

    // ── Group nesting state ─────────────────────────────────────────────────
    // Every open*Box pushes, every closeBox pops, so the stack is exactly the
    // chain of groups enclosing whatever widget arrives next. openedBoxes counts
    // pushes so the OUTERMOST box (Faust's filename wrapper) can be identified
    // by position rather than by name -- a patch is free to contain a real group
    // whose name matches the file, and dropping that one by name would be wrong.
    std::vector<std::string> groupStack;
    int openedBoxes = 0;
    int closedBoxes = 0;

    void pushGroup(const char* label)
    {
        groupStack.emplace_back(label ? label : "");
        ++openedBoxes;
    }

    // Slash-joined, outermost first, with Faust's filename wrapper dropped.
    // Empty for a parameter declared outside any group -- the fallback case a
    // sectioned layout has to handle.
    std::string currentGroup() const
    {
        std::string out;
        for (size_t i = 1; i < groupStack.size(); ++i)   // i = 1 skips the wrapper
        {
            if (groupStack[i].empty())
                continue;                                // unnamed box adds no level
            if (! out.empty())
                out += '/';
            out += groupStack[i];
        }
        return out;
    }

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
        info.zone  = zone;
        info.group = currentGroup();

        // Stable identity, derived here and nowhere else. Collisions are resolved
        // against the ids assigned EARLIER IN THIS SAME PASS, which is why the
        // scan is over `params` rather than over some external registry: two
        // controls can only collide within one patch.
        {
            std::vector<std::string> taken;
            taken.reserve(params.size());
            for (const auto& p : params)
                taken.push_back(p.id);

            info.id = ParamIdentity::disambiguate(
                          ParamIdentity::base(info.group, info.label), taken);
        }

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
    // ── Bargraphs — outputs, captured since 2026-08-02 ──────────────────────
    // These were empty bodies, so `hbargraph`/`vbargraph` were invisible to the
    // entire host: a patch could publish a level meter and nothing downstream
    // ever learned it existed. The information was always delivered here, on the
    // same UI interface as the sliders; it was simply dropped -- the same shape as
    // the group-nesting callbacks below, which were empty until 2026-07-31.
    //
    // A bargraph has min/max but NO init and NO step. Verified in the installed
    // header, not recalled: /usr/include/faust/gui/UI.h:66-67 declares
    //     addHorizontalBargraph(const char* label, REAL* zone, REAL min, REAL max)
    // against :60-61
    //     addHorizontalSlider(label, REAL* zone, REAL init, REAL min, REAL max, REAL step)
    // -- two fewer arguments, and the two that are missing are exactly the ones a
    // control the user turns would need. So the default is
    // synthesised as the minimum -- a meter at rest reads its floor, which is what
    // a silent signal measures -- and the step as 0, meaning continuous. Neither
    // is a value the patch author supplied, and neither is used to drive audio:
    // ParamMap only reads them for DISPLAY on a control that is never written.
    void addHorizontalBargraph(const char* label, FAUSTFLOAT* zone,
                               FAUSTFLOAT fmin, FAUSTFLOAT fmax) override
    {
        params.push_back(consume(label, zone, float(fmin), float(fmin), float(fmax),
                                 0.0f, FaustEngine::Kind::Meter));
    }
    void addVerticalBargraph(const char* label, FAUSTFLOAT* zone,
                             FAUSTFLOAT fmin, FAUSTFLOAT fmax) override
    {
        params.push_back(consume(label, zone, float(fmin), float(fmin), float(fmax),
                                 0.0f, FaustEngine::Kind::Meter));
    }
    void addSoundfile(const char*, const char*, Soundfile**) override {}

    // ── Group nesting ───────────────────────────────────────────────────────
    // These four were empty bodies until 2026-07-31, so every parameter arrived
    // as a flat list and a sectioned layout was impossible. The information was
    // always here -- Faust interleaves these calls with the widget adds -- it was
    // simply dropped. See ParamInfo::group for the verified call sequence.
    //
    // The outermost box is skipped: Faust wraps the entire UI in one box named
    // after the .dsp file, which is a filename, not a section. Tracking depth
    // rather than testing the name keeps that robust when a patch happens to
    // name a real group the same thing as the file.
    void openTabBox(const char* label) override        { pushGroup(label); }
    void openHorizontalBox(const char* label) override { pushGroup(label); }
    void openVerticalBox(const char* label) override   { pushGroup(label); }

    void closeBox() override
    {
        // Defensive: a malformed UI tree could close more boxes than it opened.
        // Faust does not do this, but ParamCapture must not corrupt memory if a
        // future libfaust ever did.
        if (! groupStack.empty())
            groupStack.pop_back();
        ++closedBoxes;
    }
};

// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Voice-control extraction.
//
// Mirrors dsp_voice::extractPaths (/usr/include/faust/dsp/poly-dsp.h:233-254),
// which matches on the SUFFIX of a control's full path: endsWith(path,"/gate"),
// "/freq", "/key", "/gain", "/vel", "/velocity". ParamInfo::label is exactly
// that last path component (ParamCapture records the widget label Faust hands
// it, with the group path kept separately), so comparing the whole label is the
// same test.
//
// Exact and case-sensitive, matching Faust. See the VoiceControls comment in
// FaustEngine.h for why leniency here would be a trap rather than a kindness.
//
// The match tables (kGateLabels/kFreqLabels/kGainLabels) are GENERATED from
// llm/voice_contract.json by tools/gen_voice_contract.py -- the same file
// llm/prompts/instrument_prompt.txt's GENERATED VOICE CONTRACT block comes
// from, so the two cannot say different things. See VoiceContract.generated.h.
// ---------------------------------------------------------------------------
namespace
{
FaustEngine::VoiceControls extractVoiceControls(const FaustEngine::ParamList& params)
{
    FaustEngine::VoiceControls vc;

    for (const auto& p : params)
    {
        if (p.zone == nullptr)
            continue;

        // First match wins per role, across params AND across a zone's own
        // label list (checked in the generated table's order, which is the
        // canonical match order). Faust collects EVERY matching path into a
        // vector and drives them all; a patch with two "gate" controls is
        // pathological, and taking the first keeps this phase simple. Phase 1
        // hands the job to dsp_poly, which does the vector properly.
        //
        // A param's label can equal at most ONE of the six accepted strings,
        // so checking the three zones independently (rather than the original
        // if/else-if chain across all six) cannot double-assign a param --
        // there is no string that appears in two of the tables below.
        if (! vc.gate)
            for (const auto& e : pf::VoiceContract::kGateLabels)
                if (p.label == e.name) { vc.gate = p.zone; break; }

        if (! vc.freq)
            for (const auto& e : pf::VoiceContract::kFreqLabels)
                if (p.label == e.name) { vc.freq = p.zone; vc.freqIsKey = e.rawUnits; break; }

        if (! vc.gain)
            for (const auto& e : pf::VoiceContract::kGainLabels)
                if (p.label == e.name) { vc.gain = p.zone; vc.gainIsVel = e.rawUnits; break; }
    }

    return vc;
}

// The parameters the USER controls: everything except the three the voice owns.
//
// This matters for correctness, not tidiness. ParamPool::pushToFaust writes
// every mapped slot into its zone on EVERY block. If "gate" stayed mapped, that
// write would land after noteOn's and clamp the gate to whatever the slider
// says -- so a note would either never start or never stop, depending on the
// slider. Same for freq: the pitch would be the knob's, not the note's.
//
// Filtered by ZONE IDENTITY against what extractVoiceControls actually bound,
// not by re-testing the six names. Two reasons, and the second is a real defect
// the name-list version had:
//   - the two functions cannot disagree, because there is only one decision;
//   - extractVoiceControls binds FIRST MATCH PER ROLE, so a patch declaring both
//     "freq" and "key" drives only one of them. Excluding by name withheld BOTH,
//     leaving the unbound one invisible AND unwritten -- a control the user could
//     not reach and nothing else moved. It now stays an ordinary knob.
//
// Only applied when the patch is a full instrument, so an effect that happens to
// have a slider named "freq" (a sine oscillator, say) is untouched.
FaustEngine::ParamList withoutVoiceControls(const FaustEngine::ParamList& params,
                                            const FaustEngine::VoiceControls& vc)
{
    FaustEngine::ParamList out;
    out.reserve(params.size());

    for (const auto& p : params)
    {
        if (p.zone == vc.gate || p.zone == vc.freq || p.zone == vc.gain)
            continue;

        out.push_back(p);
    }

    return out;
}
} // namespace

void FaustEngine::noteOn(int note, int velocity)
{
    FAUSTFLOAT* freq = voiceFreq.load(std::memory_order_relaxed);
    FAUSTFLOAT* gain = voiceGain.load(std::memory_order_relaxed);
    FAUSTFLOAT* gate = voiceGate.load(std::memory_order_relaxed);

    if (freq == nullptr || gain == nullptr || gate == nullptr)
        return;

    // Same conversions Faust applies (poly-dsp.h:163-166, :240-251): "/freq"
    // wants Hz via the equal-tempered formula, "/key" the raw note number;
    // "/gain" wants velocity normalised to 0-1, "/vel" the raw 0-127.
    *freq = voiceFreqIsKey.load(std::memory_order_relaxed)
                ? static_cast<FAUSTFLOAT>(note)
                : static_cast<FAUSTFLOAT>(440.0 * std::pow(2.0, (note - 69) / 12.0));

    *gain = voiceGainIsVel.load(std::memory_order_relaxed)
                ? static_cast<FAUSTFLOAT>(velocity)
                : static_cast<FAUSTFLOAT>(velocity / 127.0);

    // Gate LAST. The envelope triggers on this edge, and it must not fire until
    // the pitch and level it should sound at are already in place -- otherwise
    // the attack is a sample or more of the PREVIOUS note.
    *gate = static_cast<FAUSTFLOAT>(1);

    currentNote = note;
}

void FaustEngine::noteOff(int note)
{
    FAUSTFLOAT* gate = voiceGate.load(std::memory_order_relaxed);
    if (gate == nullptr)
        return;

    // Last-note-priority: release only if this is the note that is actually
    // sounding. Without the check, releasing a note the player already replaced
    // by a newer one would cut the newer note short -- the classic monosynth
    // legato bug, and very audible when trills overlap by a few samples.
    if (note != currentNote)
        return;

    *gate = static_cast<FAUSTFLOAT>(0);
    currentNote = -1;
}

void FaustEngine::allNotesOff()
{
    if (FAUSTFLOAT* gate = voiceGate.load(std::memory_order_relaxed))
        *gate = static_cast<FAUSTFLOAT>(0);

    currentNote = -1;
}

void FaustEngine::silenceVoice()
{
    // The bracket is the whole point: without it this races the compile worker
    // and writes a gate zone belonging to a DSP being deleted. See the header.
    if (!enterAudio())
        return;

    allNotesOff();
    exitAudio();
}

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
    const bool rateChanged = (sampleRate != sr);

    sr    = sampleRate;
    block = blockSize;

    // Output staging for arity mismatches, sized HERE because process() is the
    // audio thread and setSize allocates. Deliberately above the !rateChanged
    // early return: a host may change the block size while keeping the sample
    // rate (device buffer-size change is the common case), and a scratch buffer
    // sized for the old block would be overrun by the new one.
    //
    // Not guarded by the drain below. JUCE contracts prepareToPlay and
    // processBlock as non-concurrent, which is the same assumption the sr/block
    // stores above already make.
    scratch.setSize(kMaxChannels, blockSize, /*keepExistingContent*/ false,
                    /*clearExtraSpace*/ true, /*avoidReallocating*/ false);

    // PF-018. Storing the members was ALL this used to do. If the host changed
    // sample rate after a patch went live, the DSP kept running at the rate it was
    // instanceInit'd with — every rate-dependent constant wrong, so a 1 kHz filter
    // sat at 2 kHz and a 500 ms delay ran 250 ms, silently, until the next
    // recompile. Hosts do this routinely: switching audio device, changing project
    // rate, or offline bounce at a different rate.
    if (! rateChanged)
        return;

    // Nothing live to re-init. A later compile() will init at the new rate.
    if (activeDSP.load(std::memory_order_acquire) == nullptr)
        return;

    // Same drain protocol as compile() steps 1-2, and for the same reason: the
    // audio thread may be inside process() right now, and instanceConstants
    // rewrites the very state compute() is reading.
    //
    // compileMutex is taken FIRST so this cannot interleave with a compile's own
    // swap. Neither is ever held on the audio thread, so this cannot block it —
    // the audio thread only ever touches the ready/audioBusy atomics.
    std::lock_guard<std::mutex> lock(compileMutex);

    // Re-read under the lock: a compile could have swapped or cleared the pointer
    // between the check above and here.
    llvm_dsp* dsp = activeDSP.load(std::memory_order_acquire);
    if (dsp == nullptr)
        return;

    const bool wasReady = ready.load(std::memory_order_seq_cst);

    // Step 1: no NEW audio-thread section may start. seq_cst for the same
    // store->load handshake reason spelled out in compile().
    ready.store(false, std::memory_order_seq_cst);

    // Step 2: drain in-flight sections. Bounded by one audio callback.
    while (audioBusy.load(std::memory_order_seq_cst) != 0)
        std::this_thread::yield();

    // Step 3: re-init. instanceConstants + instanceClear is the documented pair
    // for a rate change that KEEPS control values (faust/dsp/dsp.h:135-143 —
    // instanceConstants "init instance constant state"; instanceClear "init
    // instance state (like delay lines...) but keep the control parameter
    // values"). Deliberately NOT instanceInit(), which also calls
    // instanceResetUserInterface() and would throw away the user's knob positions
    // on a device switch. Delay lines must be cleared: their contents are samples
    // at the OLD rate, and reading them at the new one is a burst of garbage.
    dsp->instanceConstants(static_cast<int>(sr));
    dsp->instanceClear();

    // Step 4: republish. Only restore ready if it was set — a rate change must not
    // bring a DSP live that the swap protocol had deliberately parked.
    if (wasReady)
        ready.store(true, std::memory_order_seq_cst);
}

int FaustEngine::liveDspSampleRateForTest() const
{
    llvm_dsp* dsp = activeDSP.load(std::memory_order_acquire);
    return dsp != nullptr ? dsp->getSampleRate() : 0;
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

    // PF-023, defense in depth. The invariant (ready==true => activeDSP!=null) does
    // hold in the current swap protocol, so this branch is unreachable today —
    // which is exactly why it is cheap. Without it there is ZERO margin if the
    // ordering in compile() is ever changed, and the failure mode is a segfault on
    // the audio thread: the worst place in the program to take one. A null DSP
    // means passthrough, matching the !ready early-return above.
    if (dsp == nullptr)
        return;

    const int n        = buffer.getNumSamples();
    const int bufChans = buffer.getNumChannels();

    // relaxed for the same reason as activeDSP above: the acquire on `ready`
    // already published these together with the DSP they describe (Step 3b).
    const int numIns  = dspNumIns.load(std::memory_order_relaxed);
    const int numOuts = dspNumOuts.load(std::memory_order_relaxed);

    // Fast path: the patch's arity matches the host's exactly. In-place, no copy.
    // This is the overwhelmingly common case — every stereo effect — and it is
    // byte-for-byte what this function did before the arity work, deliberately:
    // the routing below must not cost the common case anything.
    if (numIns == bufChans && numOuts == bufChans)
    {
        // const_cast removes the pointer-level const from float* const* → float**.
        float** io = const_cast<float**>(buffer.getArrayOfWritePointers());
        dsp->compute(n, io, io);
        return;
    }

    // ── Mismatched arity ────────────────────────────────────────────────────
    // The compile-time gate in runCompile guarantees numIns <= kMaxChannels and
    // 1 <= numOuts <= kMaxChannels, so the only cases reaching here are a patch
    // with fewer channels than the host: mono in, mono out, or a split.
    //
    // Compute into `scratch` rather than in place. Faust's compute() is only
    // safe with inputs == outputs when the arities agree; with 1-in/2-out,
    // output 0 would share storage with input 0 and the second output would read
    // an input the first had already overwritten.
    //
    // Defensive: a block larger than prepare() was told about would overrun the
    // staging buffer. Passing the dry signal through is the safe failure — and
    // it is silent, which is why prepare() sizes from the host's own blockSize
    // rather than a guess.
    if (scratch.getNumSamples() < n || scratch.getNumChannels() < numOuts)
        return;

    float* const* bufChannels = buffer.getArrayOfWritePointers();
    float* const* outChannels = scratch.getArrayOfWritePointers();

    // Stack arrays, not vectors: this is the audio thread and kMaxChannels is a
    // compile-time bound.
    float* ins [kMaxChannels] = { nullptr, nullptr };
    float* outs[kMaxChannels] = { nullptr, nullptr };

    for (int ch = 0; ch < numIns; ++ch)
        ins[ch] = bufChannels[ch < bufChans ? ch : bufChans - 1];

    for (int ch = 0; ch < numOuts; ++ch)
        outs[ch] = outChannels[ch];

    dsp->compute(n, ins, outs);

    // Fan the patch's outputs back across the host's channels. A mono patch
    // (numOuts == 1) is DUPLICATED to both, which is the whole point: before
    // this, channel 1 kept the untouched dry input, so a generated sine came out
    // of the left speaker with the dry signal still on the right.
    for (int ch = 0; ch < bufChans; ++ch)
        juce::FloatVectorOperations::copy(bufChannels[ch],
                                          outChannels[ch < numOuts ? ch : numOuts - 1],
                                          n);
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

        // ── Arity gate ───────────────────────────────────────────────────────
        // Faust will happily compile a process with any channel count; the host
        // is fixed stereo. Before this gate, process() handed JUCE's channel
        // array straight to compute() with no bounds check, so a patch declaring
        // more channels than the host indexed past the end of that array ON THE
        // AUDIO THREAD.
        //
        // Primary sources for the failure mode:
        //  - juce_AudioSampleBuffer.h:342 — getArrayOfWritePointers() returns the
        //    raw `channels` array;
        //  - juce_AudioSampleBuffer.h:441 — `channels[numChannels] = nullptr`, so
        //    the array is null-TERMINATED at index numChannels;
        //  - faust/dsp/dsp.h:192 — compute(count, inputs, outputs) is the contract;
        //    `faust -lang cpp` on `process = _,_,_;` emits
        //    `FAUSTFLOAT* output2 = outputs[2];` followed by `output2[i0] = ...`.
        //
        // So for a 2-channel buffer io[2] is the null terminator (a null deref)
        // and io[3] onward is past the allocation (a genuine out-of-bounds read,
        // then a write through whatever it finds). Rejecting here converts both
        // into a compile error the user can read — and which the generate.py
        // retry loop could later feed back to the model as stderr.
        //
        // Rejected, not clamped: silently dropping channels 3+ of a patch the
        // model meant to be quadraphonic would produce plausible-sounding wrong
        // audio, which is harder to diagnose than a refusal.
        const int numIns  = dsp->getNumInputs();
        const int numOuts = dsp->getNumOutputs();

        if (numOuts < 1 || numOuts > kMaxChannels || numIns > kMaxChannels)
        {
            delete dsp;
            deleteDSPFactory(f);
            cb({}, "This patch declares " + std::to_string(numIns) + " input(s) and "
                   + std::to_string(numOuts) + " output(s), which this plugin cannot "
                   "route: it is stereo, so process must have at most "
                   + std::to_string(kMaxChannels) + " inputs and 1 or "
                   + std::to_string(kMaxChannels) + " outputs. "
                   "Write `process = <left>, <right>;` for stereo, or a single "
                   "expression for mono.");
            return;
        }

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

        // Does this patch declare the voice contract? Decided here, once, from
        // the compiled DSP itself -- no metadata, no prompt cooperation, nothing
        // for the model to forget. See VoiceControls in FaustEngine.h.
        const VoiceControls vc = extractVoiceControls(capture.params);

        // What ParamPool and the editor see. For an instrument the three voice
        // controls are withheld: they belong to note events, not to knobs, and
        // leaving them mapped would let pushToFaust overwrite every note.
        const ParamList publishedParams =
            vc.valid() ? withoutVoiceControls(capture.params, vc) : capture.params;

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

        // Step 3a: publish the voice contract WITH the DSP it describes. Same
        // ordering argument as the arity below: written here, while ready is
        // false and no audio-thread section is in flight, and made visible by
        // Step 6's release store.
        //
        // Cleared first so a patch that is NOT an instrument cannot inherit the
        // previous patch's zones -- which would be a use-after-free, since those
        // point into the DSP deleted in Step 7.
        voiceGate.store(vc.gate, std::memory_order_relaxed);
        voiceFreq.store(vc.freq, std::memory_order_relaxed);
        voiceGain.store(vc.gain, std::memory_order_relaxed);
        voiceFreqIsKey.store(vc.freqIsKey, std::memory_order_relaxed);
        voiceGainIsVel.store(vc.gainIsVel, std::memory_order_relaxed);
        voiceValid.store(vc.valid(), std::memory_order_relaxed);
        currentNote = -1;   // the old note belongs to a DSP that is about to die

        // Step 3b: publish the new DSP's arity WITH it. relaxed is sufficient —
        // these are read on the audio thread only after the acquire on `ready`
        // (Step 6's release store), which orders every write in this block. They
        // must not be written anywhere else: an arity that disagreed with
        // activeDSP would route audio into the wrong channel count.
        dspNumIns.store(numIns, std::memory_order_relaxed);
        dspNumOuts.store(numOuts, std::memory_order_relaxed);

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
        cb(publishedParams, "");

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
