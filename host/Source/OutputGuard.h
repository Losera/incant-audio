#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <atomic>

// ---------------------------------------------------------------------------
// OutputGuard — last line of defence between machine-generated DSP and a
// speaker/headphone. Runs on the audio thread immediately after
// FaustEngine::process(), on the generated DSP's output only.
//
// WHY THIS EXISTS: every other component in this project assumes the Faust code
// it is handed is sane. Nothing validates that assumption acoustically. `faust`
// compiling a patch proves it is syntactically legal, NOT that it is stable --
// an LLM-authored feedback path with a coefficient slightly over unity compiles
// perfectly and self-oscillates to full scale in milliseconds. That is the one
// failure mode in this project that can damage hardware or hearing.
//
// Chain order is deliberate:
//   1. non-finite check  -- NaN/Inf must be caught BEFORE they contaminate the
//                           DC blocker's IIR state (one NaN in y1 poisons every
//                           subsequent sample forever, even after the DSP is
//                           replaced).
//   2. DC blocker        -- removes offset before limiting, so a large DC term
//                           cannot eat the entire limiter headroom and duck the
//                           audible signal to nothing.
//   3. limiter           -- soft knee into a hard ceiling.
//   4. runaway watchdog  -- sustained over-full-scale => latch mute.
//
// ⚠️ RT-SAFETY IS MANUALLY MAINTAINED HERE. .claude/hooks/check_rt_safety.py
// scopes only FaustEngine::process and *::processBlock bodies and cannot see
// helper classes (its own docstring documents this limit). Nothing in this file
// may allocate, lock, block, or perform I/O. Every member is fixed-size and
// every method is branch-and-arithmetic only.
// ---------------------------------------------------------------------------
class OutputGuard
{
public:
    // Sized for the stereo bus this plugin declares, with headroom. Channels
    // beyond this are still limited and NaN-checked; they just share no DC
    // state (a DC blocker is per-channel and cannot be shared).
    static constexpr int MAX_CHANNELS = 8;

    // Ceiling: -0.3 dBFS. Chosen over 0.0 dBFS so that inter-sample peaks and
    // any downstream resampling still have somewhere to go.
    static constexpr float kCeiling = 0.966051f;

    // Soft-knee entry point, -1.4 dBFS. Below this the guard is bit-transparent
    // apart from the DC blocker. Deliberately close to the ceiling: this is a
    // safety net, not a mastering limiter, and a low knee would audibly
    // compress ordinary loud material during A/B against the dry signal.
    static constexpr float kKnee = 0.85f;

    // Runaway detection: output at or above 0 dBFS continuously for this long
    // is not music, it is a diverging filter. Deliberately generous -- a
    // legitimately hot master can touch 0 dBFS repeatedly, but not without
    // ever dropping below it.
    static constexpr float kRunawaySeconds = 0.5f;

    void prepare(double sampleRate);

    // Clears filter state and the latched mute. Called from prepareToPlay and
    // whenever a new DSP goes live (a fresh patch deserves a fresh verdict).
    void reset();

    // In-place. Safe to call with any channel count.
    void process(juce::AudioBuffer<float>& buffer);

    // Latched: once tripped, stays muted until reset(). Read by the editor's
    // 30 Hz timer on the message thread; relaxed is sufficient for a UI flag.
    bool isMuted() const { return muted.load(std::memory_order_relaxed); }

    // Why the guard tripped, for the UI. Valid only while isMuted().
    enum class Trip { None, NonFinite, Runaway };
    Trip trippedBy() const { return trip.load(std::memory_order_relaxed); }

private:
    // Per-channel DC blocker state: y[n] = x[n] - x[n-1] + R*y[n-1].
    std::array<float, MAX_CHANNELS> dcX1 {};
    std::array<float, MAX_CHANNELS> dcY1 {};
    float dcR = 0.9993f;                 // recomputed in prepare() for ~5 Hz

    int   runawayBlocks    = 0;          // consecutive over-scale sample count
    int   runawayThreshold = 24000;      // recomputed in prepare()

    std::atomic<bool> muted { false };
    std::atomic<Trip> trip  { Trip::None };
};
