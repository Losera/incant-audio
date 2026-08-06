// AuditionThreadingTest -- ThreadSanitizer stress harness for the audition-
// buffer race between loadAuditionSample (message thread) and processBlock
// (audio thread).
//
// THE BUG. loadAuditionSample() does `auditionBuffer = std::move(loaded)`
// (PluginProcessor.cpp) while processBlock reads auditionBuffer's internal
// state (getNumSamples, getNumChannels, getReadPointer, then the memcpy loop)
// on the audio thread. The `auditionActive` atomic flag gates NEW entrants
// only -- it does not protect an in-flight processBlock that already loaded
// auditionActive==true and is mid-memcpy when the message thread moves a new
// buffer over it.  That is a data race on the AudioBuffer object's non-atomic
// members and a use-after-free of the old buffer's heap block
// (juce_AudioSampleBuffer.h:205-228 / juce_HeapBlock.h:144-148).
//
// THE FIX. loadAuditionSample now stores auditionActive = false (seq_cst),
// drains FaustEngine::audioBusy to zero (waiting for in-flight processBlock
// to complete), THEN moves the buffer, THEN stores auditionActive = true
// (seq_cst).  processBlock's auditionActive load is seq_cst -- the store/load
// pair forms a Dekker handshake on audioBusy, exactly like the compile
// worker's ready=false / audioBusy drain (FaustEngine.cpp:900-964).
//
// WHAT THIS TEST DOES.
//   1. Compiles a trivial Faust patch so enterAudio() returns true and the
//      audition read path in processBlock is reached.
//   2. Writes a valid 2-channel WAV via juce::WavAudioFormat.
//   3. Spawns an audio-thread stand-in hammers processBlock in a tight loop.
//   4. Spawns a message-thread stand-in hammers loadAuditionSample in a tight
//      loop (re-loading the same WAV each time).
//   5. Runs for a bounded wall-clock time, then checks that the processor
//      is still in a valid state (output is finite, no mute latched).
//
// Under TSan, the pre-fix code (without the drain) produces a DATA RACE
// report on auditionBuffer's members (the non-atomic read/write race) or on
// the heap block itself.  Post-fix, TSan reports zero races.
//
// Under ASan, the pre-fix code can produce a heap-use-after-free when the
// old buffer's allocatedData (HeapBlock) is freed via HeapBlock move-assign
// (juce_HeapBlock.h:144-148: std::swap(data, other.data), then the moved-from
// object's destructor calls free()) while the audio thread's src pointer
// still references it.
//
// KNOWN LIMITATION (stated, not solved):
//   * The race window is probabilistic -- it depends on the message-thread
//     move and the audio-thread read overlapping in wall-clock time.  The
//     tight loop and the small WAV (~1ms file I/O per loadAuditionSample
//     call vs. ~10us per processBlock call for a trivial patch) make the
//     overlap likely over 10 seconds, but not guaranteed.  If the run
//     completes with zero TSan reports, that is evidence the fix is sound,
//     not a proof of absence (TSan samples ~1-2% of memory accesses).
//   * DAW-driven teardown ordering is not exercised; a host that tears down
//     from another thread cannot be simulated here.
//
// CMake registration (copy from ParamPoolTsanTest block in host/CMakeLists.txt):
//   juce_add_console_app(AuditionThreadingTest
//       PRODUCT_NAME "Audition Threading Test"
//   )
//   target_sources(AuditionThreadingTest PRIVATE
//       tests/AuditionThreadingTest.cpp
//       Source/PluginProcessor.cpp
//       Source/PluginEditor.cpp
//       Source/PromptPanel.cpp
//       Source/CodeEditorPanel.cpp
//       Source/ParamGridPanel.cpp
//       Source/KeyboardPanel.cpp
//       Source/FaustEngine.cpp
//       Source/ParamPool.cpp
//       Source/OutputGuard.cpp
//   )
//   target_compile_features(AuditionThreadingTest PRIVATE cxx_std_17)
//   target_compile_definitions(AuditionThreadingTest PRIVATE
//       JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)
//   target_compile_options(AuditionThreadingTest PRIVATE
//       -fsanitize=thread -g -O1)
//   target_link_options(AuditionThreadingTest PRIVATE -fsanitize=thread)
//   target_link_libraries(AuditionThreadingTest PRIVATE
//       juce::juce_audio_utils juce::juce_dsp juce::juce_gui_extra
//       juce::juce_recommended_config_flags ${LIBFAUST_LIB})
//
// Build/run:
//   cmake --build build --target AuditionThreadingTest
//   TSAN_OPTIONS="halt_on_error=1" ./build/AuditionThreadingTest_artefacts/.../AuditionThreadingTest

#include "../Source/PluginProcessor.h"

// WavAudioFormat / AudioFormatWriter for writing the test WAV.
// PluginProcessor.h pulls in juce_audio_processors, which does NOT
// transitively expose juce_audio_formats (OfflineRenderTest.cpp:60-62).
#include <juce_audio_formats/juce_audio_formats.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <thread>

// libfaust leaks its parser buffers on every compile (FAUST_scan_buffer,
// inside libfaust.so): third-party, unreachable from anything we can free.
// Matched on the library so a leak in OUR code still fails the test.
extern "C" const char* __lsan_default_suppressions()
{
    return "leak:libfaust\n";
}

namespace
{

constexpr double kSampleRate  = 48000.0;
constexpr int    kBlockSize   = 512;
constexpr int    kRunSeconds  = 10;

// A trivial Faust patch that compiles fast and processes fast, so processBlock
// runs in a tight loop and the audition-read path is reached every block.
const juce::String kPatch = "import(\"stdfaust.lib\");\n"
                            "g = hslider(\"Gain\",0.5,0,1,0.01);\n"
                            "process = _*g, _*g;\n";

// Writes a valid 2-channel WAV file with a 1-second 440 Hz sine at a known
// amplitude.  Returns the file path, or an empty File on failure.
juce::File writeTestWav(const juce::File& dir)
{
    const int length = static_cast<int>(kSampleRate);   // 1 second
    juce::AudioBuffer<float> buf(2, length);

    for (int i = 0; i < length; ++i)
    {
        const float sample = 0.5f * std::sin(2.0f * 3.14159265f * 440.0f
                                              * static_cast<float>(i) / static_cast<float>(kSampleRate));
        buf.setSample(0, i, sample);
        buf.setSample(1, i, sample);
    }

    auto wav = dir.getChildFile("audition_test.wav");
    wav.deleteFile();

    juce::WavAudioFormat format;
    std::unique_ptr<juce::FileOutputStream> stream(wav.createOutputStream());
    if (stream == nullptr)
    {
        std::printf("FAIL: cannot create output stream for %s\n",
                    wav.getFullPathName().toRawUTF8());
        return {};
    }
    {
        // createWriterFor takes ownership of the stream on success and deletes
        // it on failure (juce_AudioFormat.h:82-87).
        std::unique_ptr<juce::AudioFormatWriter> writer(
            format.createWriterFor(stream.get(), kSampleRate, 2, 16, {}, 0));
        if (writer == nullptr)
        {
            std::printf("FAIL: cannot create WAV writer\n");
            return {};
        }
        stream.release();  // now owned by writer
        writer->writeFromAudioSampleBuffer(buf, 0, length);
    }
    return wav;
}

// Spins until the compile callback has fired and currentSource reflects the
// requested patch.  No message-loop pump needed -- FaustEngine compiles on
// its own persistent worker thread (PluginProcessor.h:218-219, confirmed in
// ParamPoolConcurrencyTest.cpp's comment at the "KNOWN LIMITATION" block).
bool waitForReady(PluginForgeProcessor& p, const juce::String& source,
                  int timeoutMs = 15000)
{
    const auto deadline = juce::Time::getMillisecondCounter()
                          + static_cast<juce::uint32>(timeoutMs);
    while (juce::Time::getMillisecondCounter() < deadline)
    {
        if (p.currentSourceForTest() == source)
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return p.currentSourceForTest() == source;
}

} // namespace

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    std::printf("AuditionThreadingTest -- audition-buffer drain-guard stress test\n\n");

    // ── 1. Construct and prepare ──────────────────────────────────────────────
    PluginForgeProcessor processor;
    processor.prepareToPlay(kSampleRate, kBlockSize);

    // ── 2. Compile a patch so enterAudio() returns true ──────────────────────
    std::printf("  Compiling trivial patch...\n");
    processor.loadFaustCode(kPatch, "audition threading test");
    const bool ready = waitForReady(processor, kPatch);
    if (! ready)
    {
        std::printf("FAIL: compile did not finish in time\n");
        return 1;
    }
    std::printf("  Patch live.\n");

    // ── 3. Write the test WAV ────────────────────────────────────────────────
    auto tmp = juce::File::getSpecialLocation(juce::File::tempDirectory)
                   .getChildFile("pluginforge_audition_thread_test");
    tmp.createDirectory();
    const auto wav = writeTestWav(tmp);
    if (! wav.existsAsFile())
    {
        std::printf("FAIL: WAV write failed\n");
        return 1;
    }
    std::printf("  WAV written: %s\n", wav.getFullPathName().toRawUTF8());

    // ── 4. Pre-load the audition sample so the buffer is non-empty ────────────
    processor.loadAuditionSample(wav);
    processor.setAuditionActive(true);
    std::printf("  Audition active.\n");

    // ── 5. Stress: audio thread + message thread ─────────────────────────────
    std::atomic<bool> stop { false };

    // Audio-thread stand-in: hammer processBlock as fast as possible.
    std::thread audioThread ([&]
    {
        juce::AudioBuffer<float> buffer(2, kBlockSize);
        juce::MidiBuffer midi;

        while (! stop.load(std::memory_order_relaxed))
        {
            buffer.clear();
            processor.processBlock(buffer, midi);
        }
    });

    // Message-thread stand-in: repeatedly load the same WAV, which triggers
    // the store(false) -> drain -> move -> store(true) handshake.
    std::thread msgThread ([&]
    {
        while (! stop.load(std::memory_order_relaxed))
            processor.loadAuditionSample(wav);
    });

    std::printf("  Stressing for %d seconds...\n", kRunSeconds);
    const auto t0 = std::chrono::steady_clock::now();
    std::this_thread::sleep_for(std::chrono::seconds(kRunSeconds));
    stop.store(true, std::memory_order_relaxed);
    audioThread.join();
    msgThread.join();
    const int elapsed = static_cast<int>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - t0).count());
    std::printf("  Stress complete (%d ms).\n", elapsed);

    // ── 6. Sanity: the processor must still be usable ─────────────────────────
    // Under ASan, a use-after-free from the pre-fix race would crash before
    // reaching here.  Under TSan, a data race would halt_on_error.  This
    // final assertion catches any corruption that managed to avoid both
    // sanitizers (e.g., a race that happened to land on a still-allocated
    // slab).
    bool pass = true;
    auto check = [&](bool cond, const char* what)
    {
        std::printf("  [%s] %s\n", cond ? "OK" : "FAIL", what);
        if (! cond) pass = false;
    };

    processor.setAuditionActive(true);
    {
        juce::AudioBuffer<float> buf(2, kBlockSize);
        juce::MidiBuffer midi;
        buf.clear();
        processor.processBlock(buf, midi);
        const float peak = buf.getMagnitude(0, 0, kBlockSize);
        check(peak > 0.0f && peak < 10.0f,
              "output is finite and non-silent after stress");
    }
    check(! processor.isOutputMuted(),
          "output guard did not trip during stress");

    tmp.deleteRecursively();

    std::printf("\n%s\n", pass ? "PASS" : "FAIL");
    std::printf("PF_SUMMARY harness=%s checks=1 failures=%d\n",
                "AuditionThreadingTest", pass ? 0 : 1);
    return pass ? 0 : 1;
}
