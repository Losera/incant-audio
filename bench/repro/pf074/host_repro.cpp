// PF-074 diagnostic driver — real production code paths, not a hand-rolled
// DSP harness. Scaffolding for ~/.claude/plans/i-was-trying-out-steady-graham.md
// Phase 2/3. Not part of the shipped build; compiled standalone against the
// already-built OfflineRenderTest object files (see README.md in this dir for
// the exact command).
//
// Reuses loadAndAwait()/renderWithMidi() verbatim from host/tests/OfflineRenderTest.cpp
// (the project's own idiom for driving PluginForgeProcessor off-DAW) rather than
// reinventing them, per CLAUDE.md "actively search for existing... reuse".
//
// What this answers that bench/repro/pf074/sweep_driver.cpp could not: whether
// the SECOND generation (gen4, "silent from the start" after pressing New) is
// silent because of its own DSP (ruled out — sweep_driver showed it is healthy
// at its own declared defaults) or because of HOST STATE carried across the
// LoadMode::Fresh swap — OutputGuard's latch, or a parameter-pool seeding bug.
// This drives the exact sequence the user performed: load patch A, break it via
// the real parameter path (apvts, not a bare Faust zone), confirm the latch,
// restore the knob, confirm it stays dead, then press "New" into patch B and
// read the processor's own account of what happened.

#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <fstream>
#include <mutex>
#include <sstream>
#include <thread>

#include "../../../host/Source/PluginProcessor.h"
#include "../../../host/Source/ParamMap.h"

using Proc = PluginForgeProcessor;

static juce::String slurp(const char* path)
{
    std::ifstream f(path);
    std::stringstream ss;
    ss << f.rdbuf();
    return juce::String(ss.str());
}

// Verbatim idiom from host/tests/OfflineRenderTest.cpp:163 (loadAndAwait),
// extended to also capture the per-slot ParamList the real editor receives
// (PluginProcessor.cpp's compile-success branch:
// "onFaustCompileSuccess(paramPool.publishedSlots())") since ParamPool itself
// is private and this is the one way to see slot->label/zone mapping from
// outside the processor. Not reproduced by refactor-and-include because this
// test binary has no library target to link against; this is the smallest
// faithful copy plus the one addition this diagnosis needs.
static bool loadAndAwait(Proc& p, const juce::String& source, const char* promptLabel,
                          FaustEngine::ParamList& publishedOut,
                          Proc::LoadMode mode = Proc::LoadMode::Fresh, int timeoutMs = 20000,
                          juce::String* errorOut = nullptr)
{
    std::mutex m;
    std::condition_variable cv;
    bool done = false, ok = false;

    p.onFaustCompileSuccess = [&](const FaustEngine::ParamList& params)
    {
        std::lock_guard<std::mutex> lock(m);
        done = true; ok = true; publishedOut = params; cv.notify_all();
    };
    p.onFaustCompileFailure = [&](const juce::String& err, const juce::String&)
    {
        std::lock_guard<std::mutex> lock(m);
        done = true; ok = false;
        if (errorOut) *errorOut = err;
        cv.notify_all();
    };

    p.loadFaustCode(source, promptLabel, mode);

    std::unique_lock<std::mutex> lock(m);
    cv.wait_for(lock, std::chrono::milliseconds(timeoutMs), [&] { return done; });
    lock.unlock();

    p.onFaustCompileSuccess = nullptr;
    p.onFaustCompileFailure = nullptr;
    return ok;
}

// Find the live slot index whose published label matches `label`, and set it
// through the SAME apvts path a knob drag takes (setValueNotifyingHost on the
// macro param), not a bare Faust zone write.
static bool setNamedParam(Proc& p, const FaustEngine::ParamList& slots,
                          const std::string& label, float rawValue, float* outNorm = nullptr)
{
    for (size_t i = 0; i < slots.size(); ++i)
    {
        if (slots[i].label != label) continue;
        float norm = ParamMap::mapZoneToSlot(rawValue, slots[i]);
        if (outNorm) *outNorm = norm;
        auto* param = p.apvts.getParameter(ParamPool::slotId(static_cast<int>(i)));
        if (param == nullptr) return false;
        param->setValueNotifyingHost(norm);
        return true;
    }
    return false;
}

static float peakOf(juce::AudioBuffer<float>& buf)
{
    float peak = 0.0f;
    for (int ch = 0; ch < buf.getNumChannels(); ++ch)
    {
        const float* d = buf.getReadPointer(ch);
        for (int i = 0; i < buf.getNumSamples(); ++i)
            peak = std::max(peak, std::fabs(d[i]));
    }
    return peak;
}

// Continuous sweep through the REAL host path: every block, push a new target
// for up to two named params (linearly interpolated) via the same apvts
// setValueNotifyingHost + pushToFaust route a knob drag takes, then render
// that block and check it. Far more faithful to an actual drag gesture than
// a single discrete jump -- a drag sends many small steps, not one big one.
static void sweepReport(Proc& p, const FaustEngine::ParamList& slots, const char* tag,
                        const std::string& paramA, float aFrom, float aTo,
                        const std::string& paramB, float bFrom, float bTo,
                        int blocks, int blockSize, int note)
{
    juce::AudioBuffer<float> buf(2, blockSize);
    juce::MidiBuffer midi;
    float maxPeak = 0.0f;
    bool anyNonFinite = false;
    int firstNonFiniteBlock = -1;
    bool wasMuted = false;
    int muteBlock = -1;

    for (int b = 0; b < blocks; ++b)
    {
        double t = (blocks > 1) ? static_cast<double>(b) / static_cast<double>(blocks - 1) : 0.0;
        if (!paramA.empty())
            setNamedParam(p, slots, paramA, static_cast<float>(aFrom + t * (aTo - aFrom)));
        if (!paramB.empty())
            setNamedParam(p, slots, paramB, static_cast<float>(bFrom + t * (bTo - bFrom)));

        buf.clear();
        midi.clear();
        if (b == 0)
            midi.addEvent(juce::MidiMessage::noteOn(1, note, (juce::uint8) 100), 0);
        p.processBlock(buf, midi);

        for (int ch = 0; ch < buf.getNumChannels(); ++ch)
            for (int i = 0; i < buf.getNumSamples(); ++i)
                if (!std::isfinite(buf.getSample(ch, i)))
                {
                    anyNonFinite = true;
                    if (firstNonFiniteBlock < 0) firstNonFiniteBlock = b;
                }
        maxPeak = std::max(maxPeak, peakOf(buf));

        if (p.isOutputMuted() && !wasMuted) { wasMuted = true; muteBlock = b; }
    }

    std::printf("[%s] final muted=%s trip=%s peak=%.6f nonFiniteSeen=%s%s%s\n",
                tag,
                p.isOutputMuted() ? "YES" : "no",
                p.outputTrip() == OutputGuard::Trip::NonFinite ? "NonFinite"
                    : p.outputTrip() == OutputGuard::Trip::Runaway ? "Runaway" : "None",
                maxPeak,
                anyNonFinite ? "YES" : "no",
                firstNonFiniteBlock >= 0 ? (" firstNonFiniteBlock=" + std::to_string(firstNonFiniteBlock)) : "",
                muteBlock >= 0 ? (" muteBlock=" + std::to_string(muteBlock)) : "");
}

static void renderAndReport(Proc& p, const char* tag, int blocks, int blockSize,
                            double sampleRate, int note, bool gateHeld)
{
    juce::AudioBuffer<float> buf(2, blockSize);
    juce::MidiBuffer midi;
    float maxPeak = 0.0f;
    bool anyNonFinite = false;

    for (int b = 0; b < blocks; ++b)
    {
        buf.clear();
        midi.clear();
        if (gateHeld && b == 0)
            midi.addEvent(juce::MidiMessage::noteOn(1, note, (juce::uint8) 100), 0);
        p.processBlock(buf, midi);
        for (int ch = 0; ch < buf.getNumChannels(); ++ch)
            for (int i = 0; i < buf.getNumSamples(); ++i)
                if (!std::isfinite(buf.getSample(ch, i))) anyNonFinite = true;
        maxPeak = std::max(maxPeak, peakOf(buf));
    }

    std::printf("[%s] muted=%s trip=%s peak=%.6f nonFiniteSeen=%s\n",
                tag,
                p.isOutputMuted() ? "YES" : "no",
                p.outputTrip() == OutputGuard::Trip::NonFinite ? "NonFinite"
                    : p.outputTrip() == OutputGuard::Trip::Runaway ? "Runaway" : "None",
                maxPeak,
                anyNonFinite ? "YES" : "no");
}

int main(int argc, char** argv)
{
    if (argc < 3)
    {
        std::fprintf(stderr, "usage: host_repro <patchA.dsp> <patchB.dsp>\n");
        return 2;
    }

    juce::ScopedJuceInitialiser_GUI juceInit;

    const juce::String patchA = slurp(argv[1]);
    const juce::String patchB = slurp(argv[2]);
    constexpr double sampleRate = 48000.0;
    constexpr int blockSize = 256;

    Proc p;
    p.prepareToPlay(sampleRate, blockSize);

    std::printf("=== step 1: load patch A (the 'worked, then died' patch), LoadMode::Fresh ===\n");
    FaustEngine::ParamList slotsA;
    if (!loadAndAwait(p, patchA, "pf074 repro A", slotsA))
    {
        std::fprintf(stderr, "patch A failed to compile -- cannot continue\n");
        return 1;
    }
    std::printf("isInstrument=%s  publishedSlots=%zu\n", p.isInstrumentForTest() ? "yes" : "no", slotsA.size());
    renderAndReport(p, "A: fresh load, note on", 40, blockSize, sampleRate, 48, true);

    std::printf("\n=== step 2: drive Resonance to 0 via the REAL apvts path (what a knob drag can reach) ===\n");
    float norm = -1.0f;
    bool found = setNamedParam(p, slotsA, "Resonance", 0.0f, &norm);
    std::printf("setNamedParam(Resonance, 0.0) found=%s norm01=%.6f\n", found ? "yes" : "no", norm);
    renderAndReport(p, "A: Resonance=0 applied", 10, blockSize, sampleRate, 48, false);

    std::printf("\n=== step 2b: SUSTAIN Resonance at 0 for ~1.6s (190 blocks) -- does si.smoo's\n"
                "state ever reach literal float 0.0 and trip the NaN the bare-DSP test found? ===\n");
    setNamedParam(p, slotsA, "Resonance", 0.0f, &norm);
    renderAndReport(p, "A: Resonance held at 0 for 1.6s", 190, blockSize, sampleRate, 48, false);

    std::printf("\n=== step 3: restore Resonance to ~original (0.7) -- does it come back? ===\n");
    setNamedParam(p, slotsA, "Resonance", 0.7f, &norm);
    renderAndReport(p, "A: Resonance restored to 0.7", 10, blockSize, sampleRate, 48, false);

    std::printf("\n=== step 3b: CONTINUOUS sweep through the real host path, matching the user's\n"
                "actual gesture -- Resonance UP (0.7->max) while Cutoff moves (1200->50), ~2s,\n"
                "many small per-block steps like a real knob drag, not one jump ===\n");
    sweepReport(p, slotsA, "A: Resonance 0.7->5 + Cutoff 1200->50, 2s drag",
                "Resonance", 0.7f, 5.0f, "Cutoff", 1200.0f, 50.0f, 376, blockSize, 48);

    std::printf("\n=== step 3c: same sweep, reversed direction and a wider net -- Resonance\n"
                "0.7->5 while Cutoff SWEEPS UP (1200->8000), the other half of 'cutoff moved' ===\n");
    sweepReport(p, slotsA, "A: Resonance 0.7->5 + Cutoff 1200->8000, 2s drag",
                "Resonance", 0.7f, 5.0f, "Cutoff", 1200.0f, 8000.0f, 376, blockSize, 48);

    std::printf("\n=== step 4: press 'New' -- load patch B (the regenerated, 'silent from the start' patch), LoadMode::Fresh ===\n");
    juce::String errB;
    FaustEngine::ParamList slotsB;
    bool okB = loadAndAwait(p, patchB, "pf074 repro B", slotsB, Proc::LoadMode::Fresh, 20000, &errB);
    std::printf("compile B: %s%s\n", okB ? "SUCCESS" : "FAILURE", okB ? "" : (" -- " + errB).toRawUTF8());
    if (okB)
    {
        std::printf("isInstrument=%s  publishedSlots=%zu\n", p.isInstrumentForTest() ? "yes" : "no", slotsB.size());
        renderAndReport(p, "B: fresh load, note on", 40, blockSize, sampleRate, 45, true);
    }
    else
    {
        std::printf("[B: compile failed] muted=%s trip=%s  (NOTE: PluginProcessor.cpp's failure\n"
                     "branch does not call outputGuard.reset() -- if A left it latched, it is still\n"
                     "latched right now, regardless of what B's own DSP would have sounded like)\n",
                     p.isOutputMuted() ? "YES" : "no",
                     p.outputTrip() == OutputGuard::Trip::NonFinite ? "NonFinite"
                         : p.outputTrip() == OutputGuard::Trip::Runaway ? "Runaway" : "None");
    }

    return 0;
}
