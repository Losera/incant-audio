// OutputGuardTest — behavioural tests for host/Source/OutputGuard.
//
// This guard is the last thing between machine-generated DSP and a speaker, so
// its failure modes are tested as observable signal properties (peak level, DC
// decay time, mute latching) rather than as internal state.
//
// Build/run:
//   cmake --build build --target OutputGuardTest
//   ./build/OutputGuardTest_artefacts/OutputGuardTest

#include "../Source/OutputGuard.h"
#include <cmath>
#include <cstdio>
#include <limits>

namespace
{
int failures = 0;
int checks   = 0;

void check(bool condition, const char* what)
{
    ++checks;
    std::printf("  [%s] %s\n", condition ? " OK " : "FAIL", what);
    if (! condition)
        ++failures;
}

constexpr double kSR = 48000.0;

juce::AudioBuffer<float> makeBuffer(int numSamples, float fill)
{
    juce::AudioBuffer<float> b(2, numSamples);
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < numSamples; ++i)
            b.getWritePointer(ch)[i] = fill;
    return b;
}

float peakOf(const juce::AudioBuffer<float>& b)
{
    return b.getMagnitude(0, b.getNumSamples());
}
} // namespace

int main()
{
    std::printf("OutputGuardTest\n");

    // ── 1. Silence in => silence out, never muted ───────────────────────────
    {
        OutputGuard g;
        g.prepare(kSR);
        auto buf = makeBuffer(512, 0.0f);
        g.process(buf);
        check(peakOf(buf) == 0.0f, "silence in -> silence out");
        check(! g.isMuted(), "silence does not trip the guard");
    }

    // ── 2. NaN => finite output, muted, reason reported ─────────────────────
    {
        OutputGuard g;
        g.prepare(kSR);
        auto buf = makeBuffer(512, 0.5f);
        buf.getWritePointer(0)[100] = std::numeric_limits<float>::quiet_NaN();
        g.process(buf);

        bool allFinite = true;
        for (int ch = 0; ch < buf.getNumChannels(); ++ch)
            for (int i = 0; i < buf.getNumSamples(); ++i)
                if (! std::isfinite(buf.getReadPointer(ch)[i]))
                    allFinite = false;

        check(allFinite, "NaN input -> all-finite output");
        check(g.isMuted(), "NaN input latches mute");
        check(g.trippedBy() == OutputGuard::Trip::NonFinite, "trip reason is NonFinite");
    }

    // ── 3. Inf is caught by the same path ───────────────────────────────────
    {
        OutputGuard g;
        g.prepare(kSR);
        auto buf = makeBuffer(256, 0.1f);
        buf.getWritePointer(1)[7] = std::numeric_limits<float>::infinity();
        g.process(buf);
        check(g.isMuted(), "Inf input latches mute");
    }

    // ── 4. +6 dBFS square is limited to the ceiling ─────────────────────────
    {
        OutputGuard g;
        g.prepare(kSR);
        // 2.0 linear == +6 dBFS. Alternating sign so the DC blocker has nothing
        // to remove and the limiter is what is being measured.
        juce::AudioBuffer<float> buf(2, 512);
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < 512; ++i)
                buf.getWritePointer(ch)[i] = (i % 2 == 0) ? 2.0f : -2.0f;

        g.process(buf);
        check(peakOf(buf) <= OutputGuard::kCeiling + 1.0e-6f,
              "+6 dBFS square limited to <= -0.3 dBFS ceiling");
        check(! g.isMuted(), "a single over-scale block does not mute (needs sustain)");
    }

    // ── 5. Hot-but-valid signal is not muted ────────────────────────────────
    {
        OutputGuard g;
        g.prepare(kSR);
        // 0.95 peak sine, well under full scale: loud masters must survive.
        juce::AudioBuffer<float> buf(2, 4096);
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < 4096; ++i)
                buf.getWritePointer(ch)[i] =
                    0.95f * std::sin(2.0f * 3.14159265f * 440.0f * float(i) / float(kSR));

        for (int block = 0; block < 40; ++block)   // ~3.4 s of audio
            g.process(buf);

        check(! g.isMuted(), "hot-but-valid 0.95-peak sine is never muted");
    }

    // ── 6. Sustained over-scale trips the runaway watchdog ──────────────────
    {
        OutputGuard g;
        g.prepare(kSR);
        // Full-scale DC-free square, sustained past kRunawaySeconds.
        bool tripped = false;
        for (int block = 0; block < 200 && ! tripped; ++block)
        {
            juce::AudioBuffer<float> buf(2, 512);
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < 512; ++i)
                    buf.getWritePointer(ch)[i] = (i % 2 == 0) ? 4.0f : -4.0f;
            g.process(buf);
            tripped = g.isMuted();
        }
        check(tripped, "sustained over-scale trips the runaway watchdog");
        check(g.trippedBy() == OutputGuard::Trip::Runaway, "trip reason is Runaway");
    }

    // ── 6b. kRunawaySeconds must mean the same thing at any channel count ────
    //
    // RED CASE for the shared-counter defect. `overScaleRun` was a single int
    // declared OUTSIDE the `for (ch ...)` loop, incremented once per sample PER
    // CHANNEL and zeroed by any sample below unity in ANY channel. So a stereo
    // full-scale signal reached the threshold in half the samples a mono one
    // did -- 0.25 s against the 0.5 s the constant advertises -- and a quiet
    // left channel could hold off a diverging right one indefinitely.
    //
    // Measured before the fix: mono 47 blocks, stereo 24. After: both 47.
    {
        auto blocksToTrip = [](int channels)
        {
            OutputGuard g;
            g.prepare(kSR);
            for (int block = 0; block < 400; ++block)
            {
                juce::AudioBuffer<float> buf(channels, 512);
                for (int ch = 0; ch < channels; ++ch)
                    for (int i = 0; i < 512; ++i)
                        buf.getWritePointer(ch)[i] = (i % 2 == 0) ? 4.0f : -4.0f;
                g.process(buf);
                if (g.isMuted())
                    return block + 1;
            }
            return -1;
        };

        const int mono   = blocksToTrip(1);
        const int stereo = blocksToTrip(2);

        check(mono > 0 && stereo > 0, "sustained over-scale trips at both channel counts");

        std::printf("       mono tripped at %d blocks, stereo at %d\n", mono, stereo);
        check(mono == stereo,
              "the runaway threshold is a TIME, not a per-channel sample count");
    }

    // ── 6c. RunawayPolicy::Report warns without muting (ADR-020) ─────────────
    //
    // An instrument at sustained full scale is a LOUD PATCH, not a broken one:
    // os.square at gain 1.0 sits at |y| == 1.0 forever by construction and never
    // dips below. Latching there mutes it permanently until the next recompile,
    // with nothing the player can do. The limiter still bounds the output, so
    // the audio stays safe either way -- what changes is whether it survives.
    {
        OutputGuard g;
        g.prepare(kSR);
        g.setRunawayPolicy(OutputGuard::RunawayPolicy::Report);

        for (int block = 0; block < 200; ++block)
        {
            juce::AudioBuffer<float> buf(2, 512);
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < 512; ++i)
                    buf.getWritePointer(ch)[i] = (i % 2 == 0) ? 4.0f : -4.0f;
            g.process(buf);
        }

        check(! g.isMuted(), "Report policy does NOT mute a sustained-loud patch");
        check(g.trippedBy() == OutputGuard::Trip::Runaway,
              "Report policy still REPORTS the runaway, so the editor can warn");

        // And the audio is still there, still bounded.
        juce::AudioBuffer<float> buf(2, 512);
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < 512; ++i)
                buf.getWritePointer(ch)[i] = (i % 2 == 0) ? 4.0f : -4.0f;
        g.process(buf);

        float peak = 0.0f;
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < 512; ++i)
                peak = std::max(peak, std::abs(buf.getReadPointer(ch)[i]));

        check(peak > 0.1f, "audio still flows under Report policy");
        check(peak <= OutputGuard::kCeiling + 1.0e-6f,
              "and is still bounded by the limiter's ceiling");
    }

    // ── 6d. NonFinite latches regardless of policy ───────────────────────────
    // NaN is never a loud patch. Report must not weaken this.
    {
        OutputGuard g;
        g.prepare(kSR);
        g.setRunawayPolicy(OutputGuard::RunawayPolicy::Report);

        auto bad = makeBuffer(128, 0.2f);
        bad.getWritePointer(0)[3] = std::numeric_limits<float>::quiet_NaN();
        g.process(bad);

        check(g.isMuted(), "NonFinite still latches under Report policy");
        check(g.trippedBy() == OutputGuard::Trip::NonFinite, "trip reason is NonFinite");
    }

    // ── 7. Muted output stays silent until reset ────────────────────────────
    {
        OutputGuard g;
        g.prepare(kSR);
        auto bad = makeBuffer(128, 0.2f);
        bad.getWritePointer(0)[3] = std::numeric_limits<float>::quiet_NaN();
        g.process(bad);
        check(g.isMuted(), "guard is muted after NaN");

        auto good = makeBuffer(128, 0.3f);
        g.process(good);
        check(peakOf(good) == 0.0f, "latched mute silences subsequent good audio");

        g.reset();
        auto after = makeBuffer(128, 0.3f);
        g.process(after);
        check(! g.isMuted(), "reset clears the latch");
        check(peakOf(after) > 0.0f, "audio passes again after reset");
    }

    // ── 8. DC offset decays below 0.01 within 200 ms ────────────────────────
    {
        OutputGuard g;
        g.prepare(kSR);
        const int totalSamples = static_cast<int>(kSR * 0.2);  // 200 ms
        const int blockSize = 512;
        float lastPeak = 1.0f;

        for (int done = 0; done < totalSamples; done += blockSize)
        {
            auto buf = makeBuffer(blockSize, 0.5f);   // constant DC
            g.process(buf);
            lastPeak = std::fabs(buf.getReadPointer(0)[blockSize - 1]);
        }

        check(lastPeak < 0.01f, "0.5 DC offset decays below 0.01 within 200 ms");
        check(! g.isMuted(), "DC offset alone does not trip the guard");
    }

    std::printf("\n%s (%d failure%s)\n", failures == 0 ? "PASSED" : "FAILED",
                failures, failures == 1 ? "" : "s");
    // One machine-readable line for tools/health_report.py. The `checks` total is
    // the point: every harness here already carried an exact `failures` int and
    // threw the denominator away, so "0 failures" was indistinguishable from
    // "ran nothing". Format is fixed and shared by all seven harnesses.
    std::printf("PF_SUMMARY harness=%s checks=%d failures=%d\n", "OutputGuardTest", checks, failures);
    return failures == 0 ? 0 : 1;
}
