#include "OutputGuard.h"
#include <cmath>

void OutputGuard::prepare(double sampleRate)
{
    const auto sr = sampleRate > 0.0 ? sampleRate : 44100.0;

    // One-pole DC blocker at ~5 Hz: R = 1 - 2*pi*fc/sr. Well below the audio
    // band, so the passband is untouched; fast enough that a step offset decays
    // in a fraction of a second.
    dcR = 1.0f - static_cast<float>(2.0 * 3.14159265358979 * 5.0 / sr);

    runawayThreshold = static_cast<int>(sr * static_cast<double>(kRunawaySeconds));
    reset();
}

void OutputGuard::reset()
{
    dcX1.fill(0.0f);
    dcY1.fill(0.0f);
    runawayRun.fill(0);
    muted.store(false, std::memory_order_relaxed);
    trip.store(Trip::None, std::memory_order_relaxed);
}

void OutputGuard::process(juce::AudioBuffer<float>& buffer)
{
    const int numCh = buffer.getNumChannels();
    const int numSamples = buffer.getNumSamples();

    // Already tripped: emit silence and stay tripped. Deliberately latching --
    // a diverging filter that is muted stops diverging only because we stopped
    // listening to it; unmuting automatically would re-expose the same blast.
    if (muted.load(std::memory_order_relaxed))
    {
        buffer.clear();
        return;
    }

    bool runawayFired = false;

    for (int ch = 0; ch < numCh; ++ch)
    {
        float* data = buffer.getWritePointer(ch);
        const bool hasDcState = ch < MAX_CHANNELS;

        // Local copies keep the IIR state in registers across the sample loop
        // instead of round-tripping through the arrays every sample.
        float x1 = hasDcState ? dcX1[static_cast<size_t>(ch)] : 0.0f;
        float y1 = hasDcState ? dcY1[static_cast<size_t>(ch)] : 0.0f;

        // Channels past MAX_CHANNELS carry no run across blocks -- they are still
        // limited and NaN-checked, they just cannot accumulate toward a verdict.
        int run = hasDcState ? runawayRun[static_cast<size_t>(ch)] : 0;

        for (int i = 0; i < numSamples; ++i)
        {
            float x = data[i];

            // ── 1. Non-finite ────────────────────────────────────────────────
            // Must precede the DC blocker: feeding NaN into y1 makes every
            // later sample NaN regardless of input, which would outlive the
            // offending patch.
            if (! std::isfinite(x))
            {
                muted.store(true, std::memory_order_relaxed);
                trip.store(Trip::NonFinite, std::memory_order_relaxed);
                buffer.clear();
                // State is poisoned by construction here; drop it so the next
                // patch starts clean.
                dcX1.fill(0.0f);
                dcY1.fill(0.0f);
                runawayRun.fill(0);
                return;
            }

            // ── 2. DC blocker ────────────────────────────────────────────────
            float y = x - x1 + dcR * y1;
            x1 = x;
            y1 = y;

            // ── 4a. Runaway accounting (pre-limiter) ─────────────────────────
            // Measured BEFORE limiting, because after limiting everything looks
            // well-behaved by definition -- the limiter would hide exactly the
            // condition we are trying to detect.
            const float mag = std::fabs(y);
            if (mag >= 1.0f)
            {
                if (++run >= runawayThreshold)
                    runawayFired = true;
            }
            else
            {
                run = 0;
            }

            // ── 3. Limiter: soft knee, hard ceiling ──────────────────────────
            // Continuous in value and slope at |y| == kKnee (tanh(0) == 0), so
            // moderate overs compress instead of clipping into harmonics.
            if (mag > kKnee)
            {
                const float over  = (mag - kKnee) / (kCeiling - kKnee);
                const float shape = kKnee + (kCeiling - kKnee) * std::tanh(over);
                y = (y < 0.0f) ? -shape : shape;
            }

            // Backstop: the soft knee only asymptotes to kCeiling, so clamp to
            // guarantee the contract rather than approach it.
            if (y > kCeiling)       y = kCeiling;
            else if (y < -kCeiling) y = -kCeiling;

            data[i] = y;
        }

        if (hasDcState)
        {
            dcX1[static_cast<size_t>(ch)] = x1;
            dcY1[static_cast<size_t>(ch)] = y1;
            runawayRun[static_cast<size_t>(ch)] = run;
        }
    }

    // ── 4b. Runaway verdict ─────────────────────────────────────────────────
    // The trip is REPORTED either way, so the editor can warn. Whether it also
    // mutes is the policy question ADR-020 settles: for an effect a sustained
    // 0 dBFS run is a diverging filter and muting is the point; for an
    // instrument it is a loud oscillator, and muting it permanently until the
    // next recompile is a worse outcome than the loudness. The limiter above
    // bounds the output at kCeiling in both cases, so nothing is unprotected.
    if (runawayFired)
    {
        trip.store(Trip::Runaway, std::memory_order_relaxed);

        if (runawayPolicy == RunawayPolicy::Latch)
        {
            muted.store(true, std::memory_order_relaxed);
            buffer.clear();
        }

        // Cleared either way. Under Latch nothing more will be measured; under
        // Report this re-arms, so a patch that stays over keeps re-asserting the
        // flag rather than reporting once and going quiet about it.
        runawayRun.fill(0);
    }
}
