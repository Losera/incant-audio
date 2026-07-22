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
    runawayBlocks = 0;
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

    int overScaleRun = runawayBlocks;

    for (int ch = 0; ch < numCh; ++ch)
    {
        float* data = buffer.getWritePointer(ch);
        const bool hasDcState = ch < MAX_CHANNELS;

        // Local copies keep the IIR state in registers across the sample loop
        // instead of round-tripping through the arrays every sample.
        float x1 = hasDcState ? dcX1[static_cast<size_t>(ch)] : 0.0f;
        float y1 = hasDcState ? dcY1[static_cast<size_t>(ch)] : 0.0f;

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
                runawayBlocks = 0;
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
                ++overScaleRun;
            else
                overScaleRun = 0;

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
        }
    }

    // ── 4b. Runaway verdict ─────────────────────────────────────────────────
    runawayBlocks = overScaleRun;
    if (runawayBlocks >= runawayThreshold)
    {
        muted.store(true, std::memory_order_relaxed);
        trip.store(Trip::Runaway, std::memory_order_relaxed);
        buffer.clear();
        runawayBlocks = 0;
    }
}
