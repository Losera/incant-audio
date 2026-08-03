// OfflineRenderTest — the OBJECTIVE half of the P6 listening battery, automated.
//
// THE FINDING THIS ANSWERS. The 2026-07-25 audit's central charge is that this
// project documents verification instead of running it: nine of seventeen open
// bugs are unmeasured claims rather than defects, and the first-ever audible
// validation (2026-07-24) had to be driven by hand. docs/p6_test_battery.md is
// explicit that a human is the only instrument for "does it SOUND right" — and
// that is true. But it conflates two questions, and only one of them needs ears:
//
//   subjective — "is this the warm sunlit filter I asked for?"   ears, always
//   objective  — "does it produce finite, non-silent, non-runaway audio
//                 at a sane level, without crashing?"            a machine
//
// Every failure in the 2026-07-24 battery that was NOT a generation failure was
// of the second kind. Those never needed a human and should never have waited for
// one. This file is that half.
//
// WHY NOT bench/render_oracle.py. That renders through `faust2sndfile` — the
// offline Faust compiler, a DIFFERENT code path from the one that ships. This
// renders through the ACTUAL plugin: PluginForgeProcessor::processBlock, so the
// libfaust JIT, the FaustEngine swap protocol, ParamPool's denormalisation, and
// OutputGuard all run exactly as they do in a DAW. A patch can be perfectly
// well-behaved under faust2sndfile and still produce garbage here, because most
// of what this project got wrong lived in the glue, not in the Faust.
//
// WHAT IS ASSERTED, per patch:
//   * the JIT compile succeeds and a DSP goes live
//   * rendered output contains no NaN and no Inf
//   * no DC runaway
//   * output is not silent
//   * RMS sits inside sane bounds (not vanishing, not exploding)
//   * peak is bounded
//   * the parameter count and labels ParamPool published match the patch
//   * no crash (the harness reaching its end IS this assertion)
//
// NEGATIVE CONTROLS. Three patches are deliberately pathological — NaN, hard DC,
// runaway gain. They exist so the assertions above can be shown to FAIL when they
// should. A safety test that has never seen an unsafe input is not evidence, and
// this project has been bitten by exactly that (the ADR-009 prompt-sync hook
// verified one sentence for weeks while the files it guarded diverged). For those
// three, the assertion is inverted: OutputGuard must CATCH the problem, so the
// audible output stays clean AND the guard reports it tripped.
//
// NOT COVERED, stated explicitly (COLLABORATION.md §3):
//   * Nothing here says a patch sounds like its prompt. That is PF-013 and it is
//     still open; this measures safety and liveness, not fidelity.
//   * The corpus is hand-written Faust representing the P6 categories, NOT
//     LLM-generated. It cannot regress on generation quality — it is a fixed
//     target for the AUDIO path. Generation quality is bench/'s job.
//   * No real-time behaviour is measured. Rendering here is a tight loop, not a
//     scheduled audio callback, so this says nothing about deadline misses.
//   * Denormal and subnormal performance is not tested.
//
// Build/run:
//   cmake --build build --target OfflineRenderTest
//   ./build/OfflineRenderTest_artefacts/Debug/OfflineRenderTest

#include "../Source/PluginProcessor.h"

// WavAudioFormat / AudioFormatWriter for --capture. PluginProcessor.h pulls in
// juce_audio_processors, which does NOT transitively expose juce_audio_formats.
#include <juce_audio_formats/juce_audio_formats.h>

#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <mutex>
#include <vector>

// libfaust leaks its parser buffers on every compile (FAUST_scan_buffer, inside
// libfaust.so): third-party, not reachable from anything this repo can free.
// Matched on the library so a leak in OUR code still fails the test.
extern "C" const char* __lsan_default_suppressions()
{
    return "leak:libfaust\n";
}

namespace
{
int failures = 0;
int checks   = 0;

void check(bool condition, const juce::String& what)
{
    ++checks;
    std::printf("    [%s] %s\n", condition ? " OK " : "FAIL", what.toRawUTF8());
    if (! condition)
        ++failures;
}

// ── Signal statistics over a rendered buffer ────────────────────────────────
struct Stats
{
    bool  anyNaN     = false;
    bool  anyInf     = false;
    float peak       = 0.0f;
    float rms        = 0.0f;
    float dc         = 0.0f;   // mean sample value
    int   sampleCount = 0;
};

void accumulate(Stats& s, const juce::AudioBuffer<float>& buffer, double& sumSq, double& sum)
{
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
    {
        const float* data = buffer.getReadPointer(ch);
        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            const float v = data[i];
            if (std::isnan(v)) s.anyNaN = true;
            else if (std::isinf(v)) s.anyInf = true;
            else
            {
                s.peak = std::max(s.peak, std::abs(v));
                sumSq += static_cast<double>(v) * v;
                sum   += v;
                ++s.sampleCount;
            }
        }
    }
}

// A deterministic, broadband, stereo test signal: a sine plus a repeatable
// pseudo-random component. Broadband matters — a pure sine through a low-pass
// tuned above it is indistinguishable from a bypass, so a filter patch would
// "pass" a silence check while doing nothing.
class TestSignal
{
public:
    void fill(juce::AudioBuffer<float>& buffer)
    {
        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            const float sine  = 0.35f * std::sin(phase);
            const float noise = 0.15f * (nextRandom() * 2.0f - 1.0f);
            phase += twoPiFOverSr;
            if (phase > 6.283185307f) phase -= 6.283185307f;

            for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                buffer.setSample(ch, i, sine + noise);
        }
    }

    void prepare(double sampleRate) { twoPiFOverSr = (float) (2.0 * 3.14159265358979 * 440.0 / sampleRate); }

private:
    // xorshift32: deterministic across runs and platforms, unlike rand().
    float nextRandom()
    {
        state ^= state << 13; state ^= state >> 17; state ^= state << 5;
        return (float) (state & 0xFFFFFF) / (float) 0xFFFFFF;
    }

    juce::uint32 state = 0x12345678u;
    float phase = 0.0f;
    float twoPiFOverSr = 0.0628f;
};

// Loads a patch and blocks until the JIT compile resolves. Returns false on
// compile failure or timeout — both are test failures, reported by the caller.
bool loadAndAwait(PluginForgeProcessor& p, const juce::String& source,
                  PluginForgeProcessor::LoadMode mode
                      = PluginForgeProcessor::LoadMode::Fresh,
                  int timeoutMs = 20000)
{
    std::mutex m;
    std::condition_variable cv;
    bool done = false, ok = false;

    p.onFaustCompileSuccess = [&](const FaustEngine::ParamList&)
    {
        std::lock_guard<std::mutex> lock(m);
        done = true; ok = true; cv.notify_all();
    };
    p.onFaustCompileFailure = [&](const juce::String&)
    {
        std::lock_guard<std::mutex> lock(m);
        done = true; ok = false; cv.notify_all();
    };

    p.loadFaustCode(source, "offline render test", mode);

    std::unique_lock<std::mutex> lock(m);
    cv.wait_for(lock, std::chrono::milliseconds(timeoutMs), [&] { return done; });
    lock.unlock();

    p.onFaustCompileSuccess = nullptr;
    p.onFaustCompileFailure = nullptr;
    return ok;
}

// Renders `blocks` blocks of the test signal through the processor and returns
// the statistics of what came out.
Stats render(PluginForgeProcessor& p, double sampleRate, int blockSize, int blocks)
{
    juce::AudioBuffer<float> buffer(2, blockSize);
    juce::MidiBuffer midi;
    TestSignal signal;
    signal.prepare(sampleRate);

    Stats s;
    double sumSq = 0.0, sum = 0.0;

    for (int b = 0; b < blocks; ++b)
    {
        signal.fill(buffer);
        p.processBlock(buffer, midi);
        accumulate(s, buffer, sumSq, sum);
    }

    if (s.sampleCount > 0)
    {
        s.rms = (float) std::sqrt(sumSq / s.sampleCount);
        s.dc  = (float) (sum / s.sampleCount);
    }
    return s;
}

// Per-channel view of a render. The aggregate Stats above sums across channels,
// which is exactly the wrong instrument for an arity defect: a mono patch whose
// output reaches ONE channel while the other carries the dry input has a healthy
// aggregate RMS and is broken. What separates those two cases is whether the
// channels are IDENTICAL, so that is what this measures.
struct ChannelStats
{
    float rms[2]     = { 0.0f, 0.0f };
    float maxAbsDiff = 0.0f;   // max |L-R| over the render
};

ChannelStats renderPerChannel(PluginForgeProcessor& p, double sampleRate,
                              int blockSize, int blocks)
{
    juce::AudioBuffer<float> buffer(2, blockSize);
    juce::MidiBuffer midi;
    TestSignal signal;
    signal.prepare(sampleRate);

    ChannelStats cs;
    double sumSq[2] = { 0.0, 0.0 };
    int    n = 0;

    for (int b = 0; b < blocks; ++b)
    {
        signal.fill(buffer);
        p.processBlock(buffer, midi);

        const float* l = buffer.getReadPointer(0);
        const float* r = buffer.getReadPointer(1);

        for (int i = 0; i < blockSize; ++i)
        {
            if (std::isfinite(l[i]) && std::isfinite(r[i]))
            {
                sumSq[0] += static_cast<double>(l[i]) * l[i];
                sumSq[1] += static_cast<double>(r[i]) * r[i];
                cs.maxAbsDiff = std::max(cs.maxAbsDiff, std::abs(l[i] - r[i]));
                ++n;
            }
        }
    }

    if (n > 0)
        for (int ch = 0; ch < 2; ++ch)
            cs.rms[ch] = (float) std::sqrt(sumSq[ch] / n);

    return cs;
}

// Renders with a MIDI note held for the first `noteOnBlocks` blocks, then
// released. Sibling of render() rather than a parameter on it, so the 124
// existing checks keep calling exactly the code they always did.
//
// The note-on is placed at sample 0 of block 0 and the note-off at sample 0 of
// block `noteOnBlocks`. processBlock applies events at block granularity in this
// phase (see PluginProcessor.cpp), so a finer offset would be a fiction.
struct MidiRender
{
    ChannelStats held;      // statistics while the note is sounding
    float        tailRms = 0.0f;   // RMS of the last kTailBlocks, after release
};

// How many trailing blocks the tail RMS averages over.
//
// It was ONE, and that was a latent flake rather than a passing test. With
// note-off at block 40 and a 0.2 s release, the single-block window at block 59
// covered 0.1963-0.2072 s after note-off -- its LEADING EDGE 3.7 ms BEFORE the
// release finished, so a good fraction of the samples it averaged still carried
// signal. It passed only because an ADSR's final milliseconds are near zero, and
// any change to block size, sample rate, or the corpus's release time would have
// flipped it with no code change.
//
// Callers now run well past the release and average the last four blocks, which
// makes both a tighter threshold and a stable one possible.
constexpr int kTailBlocks = 4;

MidiRender renderWithMidi(PluginForgeProcessor& p, double sampleRate, int blockSize,
                          int blocks, int noteOnBlocks, int note, int velocity)
{
    juce::AudioBuffer<float> buffer(2, blockSize);
    TestSignal signal;
    signal.prepare(sampleRate);

    MidiRender mr;
    double sumSq[2] = { 0.0, 0.0 };
    int    n = 0;
    double tailSq = 0.0;
    int    tailN  = 0;

    for (int b = 0; b < blocks; ++b)
    {
        juce::MidiBuffer midi;
        if (b == 0)
            midi.addEvent(juce::MidiMessage::noteOn(1, note, (juce::uint8) velocity), 0);
        else if (b == noteOnBlocks)
            midi.addEvent(juce::MidiMessage::noteOff(1, note), 0);

        signal.fill(buffer);
        p.processBlock(buffer, midi);

        const float* l = buffer.getReadPointer(0);
        const float* r = buffer.getReadPointer(1);

        // Accumulate only while the note is held. Including the release tail
        // would drag the RMS toward zero and make "did it sound" ambiguous.
        if (b < noteOnBlocks)
        {
            for (int i = 0; i < blockSize; ++i)
            {
                if (std::isfinite(l[i]) && std::isfinite(r[i]))
                {
                    sumSq[0] += static_cast<double>(l[i]) * l[i];
                    sumSq[1] += static_cast<double>(r[i]) * r[i];
                    mr.held.maxAbsDiff = std::max(mr.held.maxAbsDiff, std::abs(l[i] - r[i]));
                    ++n;
                }
            }
        }

        if (b >= blocks - kTailBlocks)
        {
            for (int i = 0; i < blockSize; ++i)
                if (std::isfinite(l[i]))
                    tailSq += static_cast<double>(l[i]) * l[i];
            tailN += blockSize;
        }
    }

    if (n > 0)
        for (int ch = 0; ch < 2; ++ch)
            mr.held.rms[ch] = (float) std::sqrt(sumSq[ch] / n);

    if (tailN > 0)
        mr.tailRms = (float) std::sqrt(tailSq / tailN);

    return mr;
}

struct Patch
{
    const char* name;
    const char* category;       // which P6 question this stands in for
    const char* source;
    int         expectedParams; // -1 = don't assert
    // Pathological patches only: the trip OutputGuard must latch. Asserting this
    // is what separates "the guard caught it" from "the patch happened to be
    // inert" — without it, a patch that silently produced nothing would pass the
    // safety assertions for entirely the wrong reason.
    OutputGuard::Trip expectedTrip = OutputGuard::Trip::None;
};

const char* tripName(OutputGuard::Trip t)
{
    switch (t)
    {
        case OutputGuard::Trip::NonFinite: return "NonFinite";
        case OutputGuard::Trip::Runaway:   return "Runaway";
        case OutputGuard::Trip::None:      break;
    }
    return "None";
}

// The corpus. Hand-written Faust, one per P6 battery category, so a failure here
// points at the AUDIO path rather than at generation quality.
const std::vector<Patch> kWellBehaved = {
    { "gain", "trivial control",
      "import(\"stdfaust.lib\");\n"
      "process = _,_ : *(g), *(g) with { g = hslider(\"Gain\",0.5,0,1,0.01); };", 1 },

    { "resonant lowpass", "P6 #1 anchor — the L4 filter",
      "import(\"stdfaust.lib\");\n"
      "cutoff = hslider(\"Cutoff[scale:log]\",800,20,20000,1);\n"
      "q = hslider(\"Q\",0.7,0.7,8,0.01);\n"
      "process = fi.resonlp(cutoff,q,1), fi.resonlp(cutoff,q,1);", 2 },

    { "bounded delay", "P6 #2/#10 — delay, with a BOUNDED line",
      "import(\"stdfaust.lib\");\n"
      "dt = hslider(\"Time\",200,1,1000,1) : ba.sec2samp/1000;\n"
      "fb = hslider(\"Feedback\",0.4,0,0.95,0.01);\n"
      "d = +~(de.fdelay(48000, dt) : *(fb));\n"
      "process = d, d;", 2 },

    { "tremolo", "P6 #8 — 'it should breathe'",
      "import(\"stdfaust.lib\");\n"
      "rate = hslider(\"Rate\",4,0.1,20,0.1);\n"
      "depth = hslider(\"Depth\",0.7,0,1,0.01);\n"
      "lfo = 1 - depth*(0.5+0.5*os.osc(rate));\n"
      "process = *(lfo), *(lfo);", 2 },

    { "saturation", "P6 #7 — 'angry, tearing the cone apart'",
      "import(\"stdfaust.lib\");\n"
      "drive = hslider(\"Drive\",4,1,20,0.1);\n"
      "sat = *(drive) : ma.tanh : *(1/drive);\n"
      "process = sat, sat;", 1 },

    { "highpass + trim", "P6 #6 — 'cold, glassy, far away'",
      "import(\"stdfaust.lib\");\n"
      "hp = hslider(\"Corner[scale:log]\",1200,20,8000,1);\n"
      "trim = hslider(\"Trim[unit:dB]\",0,-24,6,0.1) : ba.db2linear;\n"
      "process = fi.highpass(2,hp)*trim, fi.highpass(2,hp)*trim;", 2 },

    // A checkbox that SELECTS between two audible paths rather than gating one.
    // The obvious form -- `process = *(checkbox("Engage"))` -- renders silent at
    // its declared default of 0, which is a legitimate patch but useless here;
    // see kSilentByDefault below, where that case is pinned deliberately.
    { "toggle blend", "kind-aware params — a checkbox, not a rotary",
      "import(\"stdfaust.lib\");\n"
      "on = checkbox(\"Bright\");\n"
      "voice = _ <: fi.lowpass(2,800), fi.highpass(2,2000) : ba.selectn(2, on);\n"
      "process = voice, voice;", 1 },
};

// Patches that are CORRECT and yet render silence at their declared defaults.
//
// Found by this harness on 2026-07-25, and worth pinning rather than avoiding.
// LoadMode::Fresh (PF-020) reseeds every slot to the patch's own default, which
// is the right behaviour — but a patch whose gain-shaped control defaults to 0
// is then silent until the user moves something. To this battery, and to a user
// who has just clicked Generate, that is INDISTINGUISHABLE from a dead patch.
//
// The objective half cannot resolve it: "silent" is the correct output. Only the
// subjective half, or a check on declared defaults, can. Pinned here so the
// limitation is a recorded fact rather than a surprise during a listening pass.
const std::vector<Patch> kSilentByDefault = {
    { "zero-default gate", "checkbox gating the whole signal, default off",
      "import(\"stdfaust.lib\");\n"
      "on = checkbox(\"Engage\");\n"
      "process = *(on), *(on);", 1 },
};

// Deliberately unsafe. These prove the assertions can fail, and that OutputGuard
// is what stands between a bad generated patch and the user's speakers.
const std::vector<Patch> kPathological = {
    // Divide by a slider whose declared default is 0. A literal `0/0` is rejected
    // by the Faust compiler ("division by 0"), so the zero has to arrive at
    // RUNTIME to get past it — which is also how a generated patch would really
    // do this: declare a control that can legitimately be zero, then divide by it.
    { "NaN generator", "x / 0 at runtime, via a zero-default slider",
      "import(\"stdfaust.lib\");\n"
      "d = hslider(\"Divisor\",0,0,1,0.01);\n"
      "process = /(d), /(d);", 1, OutputGuard::Trip::NonFinite },

    // Unbounded positive feedback: y[n] = x[n] + 1.5*y[n-1]. Reaches float
    // infinity within roughly 700 samples of any non-zero input, so this is the
    // Inf path rather than the NaN path, and it is the single most likely way a
    // generated delay or filter blows up in practice.
    { "Inf by runaway feedback", "y[n] = x[n] + 1.5*y[n-1]",
      "import(\"stdfaust.lib\");\n"
      "process = +~*(1.5), +~*(1.5);", -1, OutputGuard::Trip::NonFinite },

    { "hard DC offset", "constant +0.9 added",
      "import(\"stdfaust.lib\");\n"
      "process = +(0.9), +(0.9);", -1, OutputGuard::Trip::None },

    // Loud, but NOT a runaway. Worth keeping precisely because the naive
    // expectation is wrong: the runaway detector requires |y| >= 1.0 for 0.5s
    // CONTINUOUSLY (OutputGuard.cpp:82-86, threshold sr*kRunawaySeconds), and an
    // oscillating signal returns to zero every half-cycle no matter how loud it
    // is. So a +60 dB sine is limited, never muted -- which is the RIGHT answer:
    // loud is a job for the limiter, and muting a merely-loud patch would be a
    // worse bug than the one being guarded against. What Runaway actually detects
    // is DIVERGENCE, not level; the next entry is the case that trips it.
    { "loud but oscillating", "+60 dB sine — limited, must NOT mute",
      "import(\"stdfaust.lib\");\n"
      "process = *(1000), *(1000);", -1, OutputGuard::Trip::None },

    // Slow monotonic divergence: y[n] = x[n] + 1.0005*y[n-1]. Grows past 1.0 and
    // STAYS there -- the feedback term quickly dominates the oscillating input,
    // so magnitude never returns to zero -- but takes seconds to reach float
    // infinity. That is the window in which Runaway is the only thing that can
    // catch it, and it is the realistic shape of a generated filter with a
    // marginally unstable pole.
    { "slow divergence", "y[n] = x[n] + 1.0005*y[n-1] — must trip Runaway",
      "import(\"stdfaust.lib\");\n"
      "process = +~*(1.0005), +~*(1.0005);", -1, OutputGuard::Trip::Runaway },
};

// ── Arity corpus ────────────────────────────────────────────────────────────
// Every other corpus in this file is 2-in/2-out, so nothing here has ever
// exercised a patch whose channel count disagrees with the host's. The bus
// layout is fixed stereo (PluginProcessor.cpp:6-8), and FaustEngine::process
// hands JUCE's channel array straight to dsp->compute with no bounds check:
//
//     float** io = const_cast<float**>(buffer.getArrayOfWritePointers());
//     dsp->compute(buffer.getNumSamples(), io, io);
//
// juce_AudioSampleBuffer.h:342 returns the raw `channels` array, and :441
// null-terminates it at `channels[numChannels]`. So for a 2-channel buffer
// io[2] is NULLPTR and io[3] onward is past the end of the allocation. Faust's
// generated compute lifts every output pointer into a local before the sample
// loop (faust/dsp/dsp.h:192 is the contract; the codegen shape is visible in
// `faust -lang cpp` output), so a 3-output patch dereferences null and a
// 4-output patch also reads out of bounds — both on the AUDIO THREAD.
//
// The generator cases are subtler and worse to diagnose, because they do not
// crash: a 1-output patch writes channel 0 and leaves channel 1 holding the dry
// input. Aggregate RMS looks healthy. What proves it is the two channels being
// identical, which is why these use renderPerChannel and not render.
struct ArityPatch
{
    const char* name;
    const char* category;
    const char* source;
    bool        mustCompile;   // false = FaustEngine must REJECT it, not render it
    bool        mustBeMono;    // true  = both channels must be sample-identical
};

const std::vector<ArityPatch> kArity = {
    // Rejected: more outputs than the host has channels.
    { "three outputs", "io[2] is JUCE's null terminator",
      "import(\"stdfaust.lib\");\n"
      "process = _,_,_;", false, false },

    { "four outputs", "io[3] is past the end of the allocation",
      "import(\"stdfaust.lib\");\n"
      "process = _,_,_,_;", false, false },

    // Rejected: more inputs than the host has channels. Faust READS io[2] here,
    // which is the null pointer, before it writes anything.
    // 3-in/2-out. Written as `+, _` rather than an explicit routing expression
    // because `_,_,_ :> _,_` is rejected by the Faust compiler itself (3 is not
    // a multiple of 2), which would have made this case assert that Faust
    // validates arity rather than that WE do.
    { "three inputs", "reads io[2] — the null terminator",
      "import(\"stdfaust.lib\");\n"
      "process = +, _;", false, false },

    // Accepted, and must reach BOTH channels.
    { "mono generator", "0-in/1-out — the shape every synth prompt produces",
      "import(\"stdfaust.lib\");\n"
      "process = os.osc(220) * hslider(\"Level\",0.3,0,1,0.01);", true, true },

    { "mono effect", "1-in/1-out — trivially reachable from a careless prompt",
      "import(\"stdfaust.lib\");\n"
      "process = *(hslider(\"Gain\",0.5,0,1,0.01));", true, true },

    // Accepted, and must NOT be corrupted by in-place aliasing. Faust only
    // contracts compute() for inputs == outputs when the arities match; with
    // 1-in/2-out the single input shares storage with output 0.
    { "one in, two out", "aliasing: input 0 shares storage with output 0",
      "import(\"stdfaust.lib\");\n"
      "process = _ <: fi.lowpass(2,800), fi.highpass(2,2000);", true, false },
};

// ── Instrument corpus ───────────────────────────────────────────────────────
// A patch is an instrument when it declares Faust's voice contract: controls
// named exactly "gate", "freq" (or "key") and "gain" (or "vel"). FaustEngine
// detects that from the compiled DSP, withholds those three from ParamPool, and
// drives them from MIDI note events.
//
// What these pin, and why each is a separate case:
//  - a gated synth is SILENT until a note arrives, and sounds when one does.
//    Before the MIDI walk existed the gate sat at its declared default forever,
//    so the patch was either permanently silent or a permanent drone.
//  - the three voice controls do NOT appear as knobs. If they stayed mapped,
//    ParamPool::pushToFaust would rewrite them every block and stomp the note.
//  - an ordinary effect with a "freq" slider and no gate is NOT an instrument
//    and keeps every one of its knobs. This is the case that makes the
//    detection safe for the existing corpus.
struct InstrumentPatch
{
    const char* name;
    const char* category;
    const char* source;
    bool        isInstrument;   // does it declare the full voice contract?
    int         expectedParams; // knobs the user should see (voice controls excluded)
};

const std::vector<InstrumentPatch> kInstruments = {
    { "gated saw synth", "the full voice contract",
      "import(\"stdfaust.lib\");\n"
      "freq = hslider(\"freq\",440,20,2000,0.01);\n"
      "gain = hslider(\"gain\",0.5,0,1,0.01);\n"
      "gate = button(\"gate\");\n"
      "cutoff = hslider(\"Cutoff\",1200,50,8000,1);\n"
      "process = os.sawtooth(freq) * gain * en.adsr(0.01,0.1,0.7,0.2,gate)\n"
      "          : fi.resonlp(cutoff,2,1) <: _,_;", true, 1 },

    // "key"/"vel" spelling: same contract, different units. Faust hands /key the
    // raw note number and /vel the raw 0-127 velocity (poly-dsp.h:243-251), so a
    // patch using these must convert for itself -- and this pins that we pass the
    // right one.
    // ⚠️ en.adsr, NOT en.ar. This is not a style choice.
    //
    // en.ar is a ONE-SHOT: envelopes.lib:85-86 says "release is triggered when
    // the envelope value reaches 1" -- it ignores the gate's level entirely and
    // releases on its own. This patch originally used it, which made the
    // "DECAYS after note-off" assertion below VACUOUS: the envelope had already
    // released by itself, so the check passed whether or not note-off worked.
    // Measured 2026-07-31 -- with reset() deliberately broken, this patch's gate
    // stayed at 1 and the output still fell to zero.
    //
    // Only en.adsr sustains while the gate is held. Any corpus patch asserting
    // something about note-off must use it.
    { "key/vel spelling", "the alternate contract, raw units",
      "import(\"stdfaust.lib\");\n"
      "key = hslider(\"key\",69,0,127,1);\n"
      "vel = hslider(\"vel\",100,0,127,1);\n"
      "gate = button(\"gate\");\n"
      "process = os.osc(ba.midikey2hz(key)) * (vel/127.0)\n"
      "          * en.adsr(0.01,0.1,0.7,0.2,gate) <: _,_;", true, 0 },

    // ── A deliberately LOUD synth (ADR-020's motivating shape) ──────────────
    // os.square is +/-1 by construction: it never dips below unity between
    // transitions the way a signal crossing zero does, so it is the waveform that
    // makes a latching runaway watchdog wrong for instruments.
    //
    // ⚠️ SCOPE, measured rather than assumed: this case does NOT reach the
    // watchdog here. The note is held 40 blocks and the threshold is 47 (0.5 s at
    // 48k/512), and the patch's level is the VOICE gain, which velocity 100 sets
    // to 0.787 -- so it never even crosses unity. It earns its place as an
    // integration case (a loud gated synth renders, decays and is not muted), not
    // as the guard's red case.
    //
    // The authoritative red cases for ADR-020 are in OutputGuardTest, where the
    // signal and the duration can be driven exactly: mono-vs-stereo trip timing,
    // Report-does-not-mute, and NonFinite-still-latches.
    { "loud square synth", "the shape behind ADR-020",
      "import(\"stdfaust.lib\");\n"
      "freq = hslider(\"freq\",440,20,2000,0.01);\n"
      "gain = hslider(\"gain\",1,0,1,0.01);\n"
      "gate = button(\"gate\");\n"
      "process = os.square(freq) * gain * en.adsr(0.001,0.01,1.0,0.05,gate)\n"
      "          <: _,_;", true, 0 },

    // The safety case: a generator with a "freq" slider and NO gate. Not an
    // instrument, so nothing is withheld and it drones as it always did.
    { "sine with a freq knob", "freq alone is NOT the contract",
      "import(\"stdfaust.lib\");\n"
      "freq = hslider(\"freq\",440,20,2000,0.01);\n"
      "amp = hslider(\"amp\",0.3,0,1,0.01);\n"
      "process = os.osc(freq) * amp <: _,_;", false, 2 },
};

constexpr double kSampleRate = 48000.0;
constexpr int    kBlockSize  = 512;
constexpr int    kBlocks     = 200;   // ~2.1s at 48k — long enough for LFOs/decay
} // namespace

// ── Capture mode ────────────────────────────────────────────────────────────
// `OfflineRenderTest --capture <in.dsp> <out.wav>` renders one arbitrary patch
// to a WAV instead of running the corpus. It exists so bench/p6_capture.py can
// hand a human something to LISTEN to, which is the one judgement in this
// project with no instrument (COLLABORATION.md §1).
//
// WHY THIS BINARY AND NOT bench/render_oracle.py. The oracle renders through
// faust2sndfile — the offline Faust compiler, a DIFFERENT code path from the one
// that ships. This renders through PluginForgeProcessor::processBlock, so the
// libfaust JIT, the FaustEngine swap protocol, ParamPool's denormalisation and
// OutputGuard all run exactly as they do in a DAW. A patch can be well-behaved
// under faust2sndfile and garbage here, because most of what this project got
// wrong lived in the glue. If a human is going to spend their ears on it, they
// should be hearing the real thing.
//
// Parameters sit at the patch's declared defaults: loadFaustCode defaults to
// LoadMode::Fresh, so every mapped slot is seeded from the patch's own default
// before a sample is rendered. That is the same position a user hears
// immediately after clicking Generate, which is the moment being simulated.
int runCapture(const juce::String& dspPath, const juce::String& wavPath)
{
    juce::File dspFile(dspPath);
    if (! dspFile.existsAsFile())
    {
        std::printf("capture: no such file: %s\n", dspPath.toRawUTF8());
        return 2;
    }

    PluginForgeProcessor p;
    p.prepareToPlay(kSampleRate, kBlockSize);

    if (! loadAndAwait(p, dspFile.loadFileAsString()))
    {
        std::printf("capture: JIT compile FAILED for %s\n", dspPath.toRawUTF8());
        return 3;
    }

    // Render to a buffer rather than streaming, so the stats below describe
    // exactly the samples written to disk.
    juce::AudioBuffer<float> block(2, kBlockSize);
    juce::AudioBuffer<float> whole(2, kBlockSize * kBlocks);
    juce::MidiBuffer midi;
    TestSignal signal;
    signal.prepare(kSampleRate);

    Stats s;
    double sumSq = 0.0, sum = 0.0;
    for (int b = 0; b < kBlocks; ++b)
    {
        signal.fill(block);
        p.processBlock(block, midi);
        accumulate(s, block, sumSq, sum);
        for (int ch = 0; ch < 2; ++ch)
            whole.copyFrom(ch, b * kBlockSize, block, ch, 0, kBlockSize);
    }
    if (s.sampleCount > 0)
    {
        s.rms = (float) std::sqrt(sumSq / s.sampleCount);
        s.dc  = (float) (sum / s.sampleCount);
    }

    juce::File wav(wavPath);
    wav.getParentDirectory().createDirectory();
    wav.deleteFile();

    juce::WavAudioFormat format;
    // createWriterFor TAKES OWNERSHIP of the stream on success and deletes it on
    // failure (juce_AudioFormat.h) — hence the release() and the raw new. The
    // writer is scoped so it flushes before the stats are printed.
    std::unique_ptr<juce::FileOutputStream> stream(wav.createOutputStream());
    if (stream == nullptr)
    {
        std::printf("capture: cannot write %s\n", wavPath.toRawUTF8());
        return 4;
    }
    {
        std::unique_ptr<juce::AudioFormatWriter> writer(
            format.createWriterFor(stream.get(), kSampleRate, 2, 24, {}, 0));
        if (writer == nullptr)
        {
            std::printf("capture: cannot create WAV writer\n");
            return 4;
        }
        stream.release();               // now owned by writer
        writer->writeFromAudioSampleBuffer(whole, 0, whole.getNumSamples());
    }

    // One machine-readable line, so the driver does not have to parse prose.
    std::printf("CAPTURE_OK wav=%s params=%d rms=%.6f peak=%.6f dc=%.6f nan=%d inf=%d muted=%d\n",
                wav.getFullPathName().toRawUTF8(),
                p.currentLabelsForTest().size(),
                s.rms, s.peak, s.dc,
                s.anyNaN ? 1 : 0, s.anyInf ? 1 : 0,
                p.isOutputMuted() ? 1 : 0);
    return 0;
}

int main(int argc, char** argv)
{
    if (argc == 4 && juce::String(argv[1]) == "--capture")
    {
        juce::ScopedJuceInitialiser_GUI juceInit;
        return runCapture(juce::String::fromUTF8(argv[2]),
                          juce::String::fromUTF8(argv[3]));
    }
    if (argc > 1)
    {
        std::printf("usage: OfflineRenderTest [--capture <in.dsp> <out.wav>]\n");
        return 2;
    }

    // SUBTLE: this must outlive every PluginForgeProcessor below. The APVTS ctor calls
    // startTimerHz(10) (juce_AudioProcessorValueTreeState.cpp:265 -> juce_Timer.cpp:352),
    // and Timer::startTimer asserts a MessageManager exists (juce_Timer.cpp:336). Without
    // this line the assertion fires once per processor and the run leaks TimerThread,
    // AsyncUpdater, Thread and 3x WaitableEvent at exit.
    //
    // It was cosmetic until adab1fc added a gdb post-mortem to the CI step. jassertfalse
    // breaks ONLY under a debugger (juce_PlatformDefs.h -- `if (juce_isRunningUnderDebugger())
    // JUCE_BREAK_IN_DEBUGGER`), so the bare run printed the assertion and continued, while
    // the gdb re-run trapped on the FIRST one and never reached the real fault. That is why
    // four red CI runs were read as a Timer bug: the post-mortem meant to make the SIGILL
    // "describe itself" was describing something else entirely. See PF-026/PF-027.
    juce::ScopedJuceInitialiser_GUI juceInit;

    std::printf("OfflineRenderTest — objective half of the P6 battery\n");
    std::printf("  built as: %s (PF_IS_SYNTH=%d)\n",
                PF_IS_SYNTH ? "INSTRUMENT" : "effect", (int) PF_IS_SYNTH);
    std::printf("  %d well-behaved patches, %d pathological controls, %d arity cases\n",
                (int) kWellBehaved.size(), (int) kPathological.size(), (int) kArity.size());
    std::printf("  %.0f Hz, %d-sample blocks, %d blocks (~%.1fs) per patch\n\n",
                kSampleRate, kBlockSize, kBlocks,
                kBlocks * kBlockSize / kSampleRate);

    // ── Build-time plugin traits ─────────────────────────────────────────────
    // The ONLY assertions that differ between OfflineRenderTest (PF_IS_SYNTH=0)
    // and OfflineSynthRenderTest (PF_IS_SYNTH=1), and the reason the second
    // target exists at all. Before it, every harness compiled as an effect and
    // the instrument build's traits were asserted by nothing -- PluginForgeSynth
    // shipped with acceptsMidi(), its bus layout and its release tail untested.
    //
    // Asserted in BOTH directions rather than only under #if PF_IS_SYNTH, so the
    // two builds cannot silently swap behaviour and still pass.
    {
        std::printf("  --- build-time plugin traits ---\n\n");

        PluginForgeProcessor p;

        // An instrument MUST advertise MIDI input or no host routes notes to it.
        // An effect must NOT, or DAWs present it wrongly for no gain.
        check(p.acceptsMidi() == (PF_IS_SYNTH != 0),
              juce::String("acceptsMidi() is ") + (PF_IS_SYNTH ? "true" : "false")
                  + " for this build");

        // Reporting 0 on an instrument lets a host stop calling processBlock the
        // moment the last note ends, truncating the release.
        check((p.getTailLengthSeconds() > 0.0) == (PF_IS_SYNTH != 0),
              juce::String("getTailLengthSeconds() is ")
                  + juce::String(p.getTailLengthSeconds(), 1) + "s");

        check(! p.producesMidi(), "producesMidi() is false in both builds");

        // ── Bus layouts ──────────────────────────────────────────────────────
        // Until isBusesLayoutSupported existed, JUCE's default accepted ANY
        // layout (juce_AudioProcessor.h:1329) while FaustEngine::kMaxChannels is
        // 2 -- so a 5.1 host was told yes and then got its dry input back.
        using CS = juce::AudioChannelSet;
        auto layout = [](const CS& in, const CS& out)
        {
            PluginForgeProcessor::BusesLayout l;
            l.inputBuses.add(in);
            l.outputBuses.add(out);
            return l;
        };

        check(p.checkBusesLayoutSupported(layout(CS::stereo(), CS::stereo())),
              "stereo in / stereo out is accepted");

        // The instrument case: nothing to process. Accepting this is what lets a
        // DAW instantiate the plugin on an instrument track at all.
        check(p.checkBusesLayoutSupported(layout(CS::disabled(), CS::stereo()))
                  == (PF_IS_SYNTH != 0),
              juce::String("a DISABLED input bus is ")
                  + (PF_IS_SYNTH ? "accepted (instrument)" : "refused (effect)"));

        // Beyond what scratch is sized for. Refused rather than silently ignored.
        check(! p.checkBusesLayoutSupported(layout(CS::stereo(), CS::create5point1())),
              "5.1 output is REFUSED, not silently passed through");
        check(! p.checkBusesLayoutSupported(layout(CS::create5point1(), CS::stereo())),
              "5.1 input is REFUSED");

        std::printf("\n");
    }

    // ── Well-behaved corpus ──────────────────────────────────────────────────
    for (const auto& patch : kWellBehaved)
    {
        std::printf("  %s  (%s)\n", patch.name, patch.category);

        PluginForgeProcessor p;
        p.prepareToPlay(kSampleRate, kBlockSize);

        int mappedParams = 0;
        p.onFaustCompileSuccess = nullptr;

        if (! loadAndAwait(p, patch.source))
        {
            check(false, juce::String(patch.name) + ": JIT compile succeeded");
            continue;
        }
        check(true, "JIT compiled and a DSP went live");

        const auto s = render(p, kSampleRate, kBlockSize, kBlocks);

        check(! s.anyNaN, "no NaN in rendered output");
        check(! s.anyInf, "no Inf in rendered output");
        check(s.sampleCount > 0, "output samples were produced");

        // Not silent. The test signal is ~0.28 RMS in; a patch that outputs
        // nothing at all is broken even if it compiled.
        check(s.rms > 1.0e-4f,
              juce::String("output is not silent (rms ") + juce::String(s.rms, 5) + ")");

        // Bounded. OutputGuard soft-limits, so this also confirms the guard did
        // not have to rescue a patch that should never have needed rescuing.
        check(s.peak <= 4.0f,
              juce::String("peak is bounded (") + juce::String(s.peak, 3) + ")");
        check(s.rms < 2.0f,
              juce::String("rms is not exploding (") + juce::String(s.rms, 5) + ")");

        // No DC runaway. A little DC is legitimate for some topologies; a large
        // constant offset is a defect and eats headroom.
        check(std::abs(s.dc) < 0.1f,
              juce::String("no DC runaway (mean ") + juce::String(s.dc, 5) + ")");

        // A well-behaved patch must not have tripped the safety net.
        check(! p.isOutputMuted(), "OutputGuard did NOT need to mute this patch");

        // Parameter mapping: what the patch declares is what the pool published.
        if (patch.expectedParams >= 0)
        {
            // Read the slot->label map directly. This used to serialise a blob and
            // count <SlotLabels> children, but that node was dropped from the
            // persisted format on 2026-07-27 (nothing read it) — so the mapping is
            // now observed where it actually lives, in the processor.
            mappedParams = p.currentLabelsForTest().size();
            check(mappedParams == patch.expectedParams,
                  juce::String("parameter count matches the patch (expected ")
                      + juce::String(patch.expectedParams) + ", mapped "
                      + juce::String(mappedParams) + ")");
        }
        std::printf("\n");
    }

    // ── Pathological controls — the assertions must be able to fail ──────────
    std::printf("  --- negative controls: OutputGuard must catch these ---\n\n");
    for (const auto& patch : kPathological)
    {
        std::printf("  %s  (%s)\n", patch.name, patch.category);

        PluginForgeProcessor p;
        p.prepareToPlay(kSampleRate, kBlockSize);

        if (! loadAndAwait(p, patch.source))
        {
            // A pathological patch that does not even compile tells us nothing
            // about OutputGuard. Report and move on rather than failing: the
            // Faust compiler rejecting it is a legitimate outcome.
            std::printf("    [SKIP] did not compile — nothing for the guard to catch\n\n");
            continue;
        }

        const auto s = render(p, kSampleRate, kBlockSize, kBlocks);

        // Whatever the patch did internally, what reaches the output must be safe.
        check(! s.anyNaN, "no NaN reaches the output (guard caught it)");
        check(! s.anyInf, "no Inf reaches the output (guard caught it)");
        check(s.peak <= 4.0f,
              juce::String("output stays bounded (peak ") + juce::String(s.peak, 3) + ")");
        check(std::abs(s.dc) < 0.5f,
              juce::String("DC is blocked or muted (mean ")
                  + juce::String(s.dc, 5) + ")");

        // The assertion that makes the four above mean something. Without it, a
        // patch that produced pure silence would satisfy every safety check
        // while proving nothing about OutputGuard.
        if (patch.expectedTrip != OutputGuard::Trip::None)
        {
            check(p.isOutputMuted(),
                  "OutputGuard actually TRIPPED (not merely a quiet patch)");
            check(p.outputTrip() == patch.expectedTrip,
                  juce::String("tripped as ") + tripName(patch.expectedTrip)
                      + " (got " + tripName(p.outputTrip()) + ")");
        }
        else
        {
            // DC offset is handled by the DC blocker, which is continuous
            // filtering rather than a latched trip -- so the guard must NOT mute.
            check(! p.isOutputMuted(),
                  "handled without muting (DC blocker, not a latched trip)");
        }
        std::printf("\n");
    }

    // ── Silent-at-default: a real hazard the objective half cannot resolve ───
    {
        std::printf("  --- silent at declared defaults (a limitation, pinned) ---\n\n");
        for (const auto& patch : kSilentByDefault)
        {
            std::printf("  %s  (%s)\n", patch.name, patch.category);

            PluginForgeProcessor p;
            p.prepareToPlay(kSampleRate, kBlockSize);
            if (! loadAndAwait(p, patch.source))
            {
                check(false, juce::String(patch.name) + ": compiled");
                continue;
            }

            const auto s = render(p, kSampleRate, kBlockSize, kBlocks);

            // Asserted SILENT, deliberately. If this ever starts producing audio,
            // either LoadMode::Fresh stopped honouring declared defaults or the
            // patch changed — both worth knowing.
            check(s.rms < 1.0e-4f,
                  juce::String("renders SILENT at its declared defaults, as designed "
                               "(rms ") + juce::String(s.rms, 7) + ")");
            check(! s.anyNaN && ! s.anyInf, "silence is clean, not NaN");
            std::printf("      NOTE: to a user who just clicked Generate this is\n"
                        "      indistinguishable from a dead patch. The objective\n"
                        "      battery cannot tell them apart — silence is correct here.\n");
            std::printf("\n");
        }
    }

    // ── Arity: the host is stereo, and the patch may not be ──────────────────
    {
        std::printf("  --- arity: the patch's channel count vs the host's ---\n\n");
        for (const auto& patch : kArity)
        {
            std::printf("  %s  (%s)\n", patch.name, patch.category);

            PluginForgeProcessor p;
            p.prepareToPlay(kSampleRate, kBlockSize);

            const bool compiled = loadAndAwait(p, patch.source);

            if (! patch.mustCompile)
            {
                // The assertion is on the REJECTION, and the patch is deliberately
                // not rendered even when it slips through: a patch with more
                // channels than the host dereferences JUCE's null terminator on
                // the audio thread, and a test binary that segfaults reports
                // nothing about the eleven checks after it. A readable FAIL is
                // worth more here than a demonstration.
                check(! compiled,
                      "REJECTED at compile, before it could reach the audio thread");
                if (compiled)
                    std::printf("      NOTE: this patch is LIVE with an arity the host\n"
                                "      cannot serve. Not rendered on purpose — see above.\n");
                std::printf("\n");
                continue;
            }

            if (! compiled)
            {
                check(false, juce::String(patch.name) + ": JIT compile succeeded");
                std::printf("\n");
                continue;
            }
            check(true, "JIT compiled and a DSP went live");

            const auto cs = renderPerChannel(p, kSampleRate, kBlockSize, kBlocks);

            check(cs.rms[0] > 1.0e-4f,
                  juce::String("left channel carries signal (rms ")
                      + juce::String(cs.rms[0], 5) + ")");
            check(cs.rms[1] > 1.0e-4f,
                  juce::String("right channel carries signal (rms ")
                      + juce::String(cs.rms[1], 5) + ")");

            if (patch.mustBeMono)
            {
                // The load-bearing one. Both channels being non-silent is NOT
                // evidence the mono output was duplicated — before the arity fix
                // channel 1 held the untouched dry input, which is non-silent and
                // wrong. Only sample-identity distinguishes the two.
                // NOTE: juce::String(const char*) asserts the literal is ASCII
                // (juce_String.cpp:315, CharPointer_ASCII::isValidString) -- an
                // em dash here fires a JUCE assertion mid-run. Punctuation in
                // check() messages stays ASCII; std::printf is unaffected.
                check(cs.maxAbsDiff < 1.0e-6f,
                      juce::String("both channels are IDENTICAL - the mono output was "
                                   "duplicated, not left holding dry input (max |L-R| ")
                          + juce::String(cs.maxAbsDiff, 7) + ")");
            }
            else
            {
                // A 1-in/2-out patch splitting into a lowpass and a highpass must
                // NOT come out identical; if it does, one branch overwrote the
                // other through the aliased input pointer.
                check(cs.maxAbsDiff > 1.0e-4f,
                      juce::String("the two outputs are genuinely different - no "
                                   "aliasing collapse (max |L-R| ")
                          + juce::String(cs.maxAbsDiff, 5) + ")");
            }

            check(! p.isOutputMuted(), "OutputGuard did NOT need to mute this patch");
            std::printf("\n");
        }
    }

    // ── Instruments: MIDI must reach the DSP ─────────────────────────────────
    {
        std::printf("  --- instruments: the voice contract and MIDI ---\n\n");
        for (const auto& patch : kInstruments)
        {
            std::printf("  %s  (%s)\n", patch.name, patch.category);

            PluginForgeProcessor p;
            p.prepareToPlay(kSampleRate, kBlockSize);

            if (! loadAndAwait(p, patch.source))
            {
                check(false, juce::String(patch.name) + ": JIT compile succeeded");
                std::printf("\n");
                continue;
            }
            check(true, "JIT compiled and a DSP went live");

            // The three voice controls must not be knobs. This is the assertion
            // that keeps ParamPool from overwriting every note.
            const int knobs = p.currentLabelsForTest().size();
            check(knobs == patch.expectedParams,
                  juce::String("user knobs exclude the voice controls (expected ")
                      + juce::String(patch.expectedParams) + ", mapped "
                      + juce::String(knobs) + ")");

            if (! patch.isInstrument)
            {
                // Not an instrument: it should drone with no MIDI at all, and
                // keep every knob it declared.
                const auto s = render(p, kSampleRate, kBlockSize, 40);
                check(s.rms > 1.0e-4f,
                      juce::String("sounds WITHOUT any MIDI, as an effect should (rms ")
                          + juce::String(s.rms, 5) + ")");
                std::printf("\n");
                continue;
            }

            // Silent until played. 40 blocks (~0.43 s) of no MIDI at all.
            const auto quiet = render(p, kSampleRate, kBlockSize, 40);
            check(quiet.rms < 1.0e-4f,
                  juce::String("SILENT with no note held (rms ")
                      + juce::String(quiet.rms, 7) + ")");

            // Now play one. Held for 40 blocks, then released, 80 blocks total.
            //
            // 80 rather than 60 so the tail window sits COMFORTABLY past the
            // release rather than straddling its end: blocks 76-79 are
            // 0.384-0.427 s after note-off, against the corpus's longest release
            // of 0.2 s. See kTailBlocks for what the old 60/last-one window did.
            const auto mr = renderWithMidi(p, kSampleRate, kBlockSize, 80, 40, 69, 100);

            check(mr.held.rms[0] > 1.0e-3f,
                  juce::String("SOUNDS while a note is held (rms ")
                      + juce::String(mr.held.rms[0], 5) + ")");
            check(mr.held.rms[1] > 1.0e-3f,
                  juce::String("reaches both channels (right rms ")
                      + juce::String(mr.held.rms[1], 5) + ")");

            // Release. The envelope must actually let go -- this is the
            // gate-driven sibling of the render oracle's never_decays gate, and
            // it is what would catch an ADSR whose release is mis-scaled
            // (PF-045: seconds vs samples turns a 200 ms release into 2.7 hours).
            // 1% of held, not 10%. A correct window affords a much tighter
            // threshold than the straddling one did, and tighter is what makes
            // this catch a mis-scaled release (PF-045: seconds read as samples
            // turns a 200 ms release into 2.7 hours) rather than merely notice a
            // patch that stopped entirely.
            check(mr.tailRms < mr.held.rms[0] * 0.01f,
                  juce::String("DECAYS after note-off (tail rms ")
                      + juce::String(mr.tailRms, 7) + " vs held "
                      + juce::String(mr.held.rms[0], 5) + ")");

            check(! p.isOutputMuted(), "OutputGuard did NOT need to mute this patch");

            // ── The hanging note ─────────────────────────────────────────────
            // Hold a note and NEVER release it (noteOnBlocks past the end, so
            // renderWithMidi emits no note-off), then do what a host does on
            // transport stop: call reset().
            //
            // RED CASE: before FaustEngine::silenceVoice() existed, reset() was
            // not overridden at all. The gate zone kept its 1, only a MIDI
            // note-off could clear it, and there were no events left to send --
            // so the patch droned forever. This assertion is the whole reason
            // the override exists.
            (void) renderWithMidi(p, kSampleRate, kBlockSize, 20, 9999, 69, 100);
            p.reset();

            // Let the release finish before measuring: 40 blocks is 0.43 s at
            // 48k, comfortably past the corpus patches' longest release (0.2 s).
            // Measuring ACROSS the release would fail a working fix.
            (void) render(p, kSampleRate, kBlockSize, 40);

            const auto afterReset = render(p, kSampleRate, kBlockSize, 20);
            check(afterReset.rms < 1.0e-4f,
                  juce::String("reset() RELEASES a held note (rms ")
                      + juce::String(afterReset.rms, 7) + ")");

            std::printf("\n");
        }
    }

    // ── PF-018: a live DSP must follow a sample-rate change ──────────────────
    // The assertion the audit asked for as an executable spec. Before the fix,
    // prepare() stored the new rate and returned, so the DSP kept running at the
    // rate it was instanceInit'd with — a 500 ms delay silently became 250 ms.
    {
    // ── Meters are captured, and take no slot ────────────────────────────────
    // Faust's hbargraph/vbargraph is an OUTPUT: the DSP writes it, the UI reads
    // it. ParamCapture's two bargraph callbacks were empty function bodies until
    // 2026-08-02, so a patch publishing a meter published nothing -- the widget
    // never entered ParamList and no metering could exist anywhere in the product.
    //
    // Two claims, and they pull in opposite directions, which is why one patch
    // asserts both: the meter must be SEEN (it reaches the published params) and
    // must NOT be given a macro slot (writing its zone would overwrite the
    // measurement with a knob position every block, and eight meters must not cost
    // eight of the 64 knobs).
    //
    // The source is the same fixture the UI gallery uses, read from disk rather
    // than duplicated here, so the two cannot drift.
    {
        std::printf("  --- meters: captured as outputs, and unslotted ---\n\n");

        const auto fixture = juce::File(__FILE__).getParentDirectory()
                                 .getChildFile("ui_fixtures")
                                 .getChildFile("06_effect_metered.dsp");
        check(fixture.existsAsFile(), "the metered fixture is on disk");

        if (fixture.existsAsFile())
        {
            PluginForgeProcessor p;
            p.prepareToPlay(kSampleRate, kBlockSize);

            if (loadAndAwait(p, fixture.loadFileAsString()))
            {
                check(true, "the metered patch JIT compiled");

                // Drive + Level + Out. The bargraph is published like any other
                // parameter: this is the half that was silently missing.
                const auto labels = p.currentLabelsForTest();
                check(labels.contains("Out"),
                      juce::String("the hbargraph reached the published params (")
                          + labels.joinIntoString(", ") + ")");

                // ...but only the two SLIDERS get slots.
                check(p.mappedSlotCountForTest() == 2,
                      juce::String("the meter takes NO macro slot (2 knobs expected, "
                                   "mapped ")
                          + juce::String(p.mappedSlotCountForTest()) + ")");

                const auto ids = p.slotIdsForTest();
                bool meterSlotted = false;
                for (const auto& id : ids)
                    if (id == "out")
                        meterSlotted = true;
                check(! meterSlotted, "no slot is bound to the meter's identity");

                // And the patch still makes audio with the meter attached --
                // attach() must not have broken the signal path.
                const auto s = render(p, kSampleRate, kBlockSize, 40);
                check(s.rms > 1.0e-4f,
                      juce::String("the metered patch still produces audio (rms ")
                          + juce::String(s.rms, 5) + ")");
                check(! s.anyNaN && ! s.anyInf,
                      "the metered patch produces no NaN/Inf");
            }
            else
            {
                check(false, "the metered patch JIT compiled");
            }
            std::printf("\n");
        }
    }

        std::printf("  --- PF-018: sample-rate change on a live DSP ---\n\n");

        PluginForgeProcessor p;
        p.prepareToPlay(44100.0, kBlockSize);
        const bool loaded = loadAndAwait(
            p, "import(\"stdfaust.lib\");\n"
               "process = fi.lowpass(2, hslider(\"Cutoff\",1000,20,20000,1)), "
               "fi.lowpass(2, hslider(\"Cutoff\",1000,20,20000,1));");
        check(loaded, "patch compiled at 44100 Hz");

        if (loaded)
        {
            check(p.liveDspSampleRateForTest() == 44100,
                  "live DSP reports 44100 Hz after the first prepare");

            // The host switches device / project rate mid-session.
            p.prepareToPlay(96000.0, kBlockSize);

            check(p.liveDspSampleRateForTest() == 96000,
                  "live DSP FOLLOWED the change to 96000 Hz (PF-018)");

            // And it must still render safely at the new rate.
            const auto s = render(p, 96000.0, kBlockSize, 100);
            check(! s.anyNaN && ! s.anyInf, "no NaN/Inf after the rate change");
            check(s.rms > 1.0e-4f, "still producing audio after the rate change");
            check(! p.isOutputMuted(), "rate change did not trip OutputGuard");
        }
        std::printf("\n");
    }

    std::printf("%s (%d failure%s)\n",
                failures == 0 ? "PASS" : "FAIL", failures, failures == 1 ? "" : "s");
    // One machine-readable line for tools/health_report.py. The `checks` total is
    // the point: every harness here already carried an exact `failures` int and
    // threw the denominator away, so "0 failures" was indistinguishable from
    // "ran nothing". Format is fixed and shared by all seven harnesses.
    // The harness NAME must track the build, not the source file: this same .cpp
    // is compiled twice, as OfflineRenderTest and as OfflineSynthRenderTest.
    // health_report.py keys its per-lane records on this string
    // (tools/health_report.py:99), so emitting one name from both binaries would
    // silently collapse two lanes into one -- the second overwriting the first.
    std::printf("PF_SUMMARY harness=%s checks=%d failures=%d\n",
                PF_IS_SYNTH ? "OfflineSynthRenderTest" : "OfflineRenderTest",
                checks, failures);
    return failures == 0 ? 0 : 1;
}
