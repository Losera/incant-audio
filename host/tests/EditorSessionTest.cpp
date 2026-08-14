// EditorSessionTest — a simulated human session against the real editor.
//
// THE GAP THIS FILLS. Nothing had ever constructed PluginForgeEditor. Not one
// test. ParamGridPanel, the shell's dynamic window sizing, and the output-guard
// mute edge-detect in timerCallback() had zero coverage between them, and
// PF-005's closing note has said "not confirmed by eye/runtime" since
// 2026-07-23 — the auto-layout grid's central promise, that a toggle-kind param
// renders as a ToggleButton and never a rotary, was verified by reading the
// switch statement that implements it. PromptPanelThreadingTest drives the
// PANEL; it never touches the editor that owns it, so everything the shell wires
// between processor and panels was untested by construction.
//
// WHAT "SIMULATED HUMAN" MEANS HERE, precisely. Synthetic input at the
// compositor level is NOT available on this machine — wtype, ydotool and
// xdotool are all absent — so this does not move a pointer or press a key. It
// drives the same entry points those events would reach: submitPromptForTest()
// is what the Generate button's onClick calls, loadFaustCode() is what a
// finished generation calls, and pumpMeterTickForTest() is what the 30Hz Timer
// calls. Then it asserts what the SCREEN says — status line text, control count,
// widget type, window height — rather than what the code did.
//
// The gap that remains, stated plainly: nothing here proves the Generate button
// is wired to submitPrompt(), or that the window is on screen at all. Those are
// eye checks, and the PNG snapshots below are what makes them a glance instead
// of a session.
//
// WHY THERE ARE SNAPSHOTS. Each scenario writes artifacts/images/session_NN.png
// via Component::createComponentSnapshot(), which renders through JUCE's
// software renderer with no peer and no compositor — so it works headless, in
// CI, and can be read back by whoever is reviewing. tools/screenshot_ui.sh
// (hyprctl + grim) still exists for the real Standalone; this is the mechanism,
// that is the confirmation.
//
// NO NETWORK, NO QUOTA. Generation scenarios run through the REAL PromptPanel
// subprocess bridge with FakeGenerator.h on the far end (see that header). The
// Faust in the fixed patches below is hand-written and compiles through the real
// libfaust JIT, so this exercises the audio path but says NOTHING about
// generation quality — that is bench/'s job.
//
// NOT COVERED, stated explicitly (COLLABORATION.md §3):
//   * Mouse and keyboard event routing. See above.
//   * Whether any patch SOUNDS like its prompt. PF-013, still open, still ears.
//   * DAW-driven teardown ordering. Approximated by destroying on the message
//     thread; a host that tears down from another thread cannot be exercised
//     without a host.
//   * Look-and-feel, colours, font metrics. The snapshots record them; nothing
//     asserts them, because a pixel assertion breaks on every theme change and
//     gets deleted by the first person it annoys.
//
// Build/run (ASan/UBSan wired in host/CMakeLists.txt; needs a display):
//   cmake --build build --target EditorSessionTest
//   ./build/EditorSessionTest_artefacts/Debug/EditorSessionTest

#include "../Source/PluginEditor.h"
#include "../Source/PluginProcessor.h"
#include "FakeGenerator.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <map>

// libfaust leaks its parser buffers on every compile (FAUST_scan_buffer, inside
// libfaust.so): third-party, not reachable from anything this repo can free.
// Matched on the library so a leak in OUR code still fails the test.
extern "C" const char* __lsan_default_suppressions()
{
    return "leak:libfaust\n";
}

namespace
{

// ── Reporting ───────────────────────────────────────────────────────────────
int failures = 0;
int checks   = 0;

void check(bool cond, const juce::String& what)
{
    ++checks;
    if (! cond) ++failures;
    std::printf("    [%s] %s\n", cond ? "OK" : "FAIL", what.toRawUTF8());
}

void scenario(const char* name, const char* what)
{
    std::printf("\n  %s\n    %s\n", name, what);
}

// ── Message-loop helpers ────────────────────────────────────────────────────
// Everything the editor does in response to a compile arrives via
// MessageManager::callAsync, so nothing is observable until the loop runs.

void pump(int ms)
{
    juce::MessageManager::getInstance()->runDispatchLoopUntil(ms);
}

// Pumps until `done` or the timeout. Returns whether `done` came true — a test
// asserts on the RESULT rather than sleeping a guessed interval, so a slow JIT
// makes the run slower and never flaky.
bool pumpUntil(std::function<bool()> done, int timeoutMs = 20000)
{
    const auto deadline = juce::Time::getMillisecondCounter() + (juce::uint32) timeoutMs;
    while (juce::Time::getMillisecondCounter() < deadline)
    {
        if (done()) return true;
        pump(20);
    }
    return done();
}

// ── Snapshots ───────────────────────────────────────────────────────────────
juce::File snapshotDir()
{
    auto dir = juce::File::getCurrentWorkingDirectory()
                   .getChildFile("artifacts").getChildFile("images");
    dir.createDirectory();
    return dir;
}

void snapshot(juce::Component& c, const juce::String& name)
{
    // createComponentSnapshot renders through the software renderer into an
    // Image — no peer, no compositor, no display server (juce_Component.h:1959).
    // That is the whole reason this works in CI.
    auto img = c.createComponentSnapshot(c.getLocalBounds(), true);
    if (! img.isValid())
    {
        std::printf("      (snapshot %s: image invalid, skipped)\n", name.toRawUTF8());
        return;
    }
    auto out = snapshotDir().getChildFile("session_" + name + ".png");
    out.deleteFile();
    juce::FileOutputStream stream(out);
    if (! stream.openedOk())
    {
        std::printf("      (snapshot %s: cannot write %s)\n",
                    name.toRawUTF8(), out.getFullPathName().toRawUTF8());
        return;
    }
    juce::PNGImageFormat png;
    png.writeImageToStream(img, stream);
    std::printf("      snapshot -> %s (%dx%d)\n",
                out.getFileName().toRawUTF8(), img.getWidth(), img.getHeight());
}

// ── Fixture ─────────────────────────────────────────────────────────────────
// Declaration order is the contract: the processor is declared FIRST so it is
// destroyed LAST. PromptPanel holds a raw PluginForgeProcessor& and its worker
// is joined in ~PromptPanel, so the processor must outlive the editor — the
// same argument PromptPanel.h:63-68 makes about the DAW's own teardown order.
struct Session
{
    PluginForgeProcessor processor;
    PluginForgeEditor    editor { processor };

    Session()
    {
        processor.prepareToPlay(48000.0, 512);
        // The editor sizes itself in its constructor, which lays out the panels.
        // Without a size the grid has zero bounds and every widget lands at 0x0 —
        // technically constructed, visually meaningless, and the snapshots would
        // be blank.
        editor.setSize(900, 500);
    }
};

// ── Fixed patches ───────────────────────────────────────────────────────────
// Hand-written, not generated. These are a fixed target for the UI, so this file
// cannot regress on generation quality — it has nothing to say about it.

// One param of each Faust UI kind, in a deliberate order, so the widget mapping
// can be asserted position by position.
// EVERY param must genuinely affect the output. Faust constant-folds, so a
// control multiplied by zero — `trig * 0`, which is what the first draft of this
// patch had — is optimised out of the DSP entirely and never reaches
// buildUserInterface. The patch still compiles; it just silently declares four
// params instead of five, and the scenario fails for a reason that has nothing
// to do with the widget mapping it is testing.
const char* kEveryKindPatch = R"(import("stdfaust.lib");
cutoff = hslider("Cutoff", 800, 20, 20000, 1);
level  = vslider("Level", 0.5, 0, 1, 0.01);
voices = nentry("Voices", 2, 1, 8, 1);
bypass = checkbox("Bypass");
trig   = button("Trigger");
amt = level * (1 - bypass) * (cutoff / 20000) * (voices / 8) + trig * 0.01;
process = _ * amt, _ * amt;
)";

const char* kFourParamPatch = R"(import("stdfaust.lib");
a = hslider("Alpha", 0.5, 0, 1, 0.01);
b = hslider("Beta",  0.25, 0, 1, 0.01);
c = hslider("Gamma", 0.75, 0, 1, 0.01);
d = hslider("Delta", 0.1, 0, 1, 0.01);
process = _ * (a+b+c+d) * 0.25, _ * (a+b+c+d) * 0.25;
)";

const char* kTinyPatch = R"(import("stdfaust.lib");
g = hslider("Gain", 0.5, 0, 1, 0.01);
process = _ * g, _ * g;
)";

// Latches OutputGuard's Runaway trip. Modelled on OfflineRenderTest's
// pathological controls: a constant at full scale for long enough to trip.
const char* kRunawayPatch = R"(import("stdfaust.lib");
process = _ * 1000000 + 100, _ * 1000000 + 100;
)";

// A full voice-contract instrument (gate/freq/gain), the same shape validated
// as OfflineRenderTest.cpp's "gated saw synth" corpus entry -- reused here
// rather than re-derived, since VoiceContract.generated.h's label matching
// (host/Source/VoiceContract.generated.h: "gate", "freq", "gain") is exact and
// case-sensitive. One user knob (Cutoff); freq/gain/gate are withheld from the
// grid as voice controls.
const char* kGatedSawSynthPatch = R"(import("stdfaust.lib");
freq = hslider("freq",440,20,2000,0.01);
gain = hslider("gain",0.5,0,1,0.01);
gate = button("gate");
cutoff = hslider("Cutoff",1200,50,8000,1);
process = os.sawtooth(freq) * gain * en.adsr(0.01,0.1,0.7,0.2,gate)
          : fi.resonlp(cutoff,2,1) <: _,_;
)";

// Builds a patch with N horizontal sliders, for the overflow scenario.
juce::String manyParamPatch(int n)
{
    juce::String s = "import(\"stdfaust.lib\");\n";
    juce::String sum;
    for (int i = 0; i < n; ++i)
    {
        s << "p" << i << " = hslider(\"P" << i << "\", 0.5, 0, 1, 0.01);\n";
        sum << (i == 0 ? "" : "+") << "p" << i;
    }
    s << "acc = (" << sum << ") / " << n << ";\n";
    s << "process = _ * acc, _ * acc;\n";
    return s;
}

// Loads a patch and waits for the EDITOR to have caught up — not merely for the
// compile to finish. The distinction matters: the compile callback fires on the
// compile thread and hops to the message thread, so a test that waited on the
// compile alone would race the grid rebuild it is about to assert on.
// Waits for the EDITOR to have caught up, which is a harder thing to observe
// than it looks, and this function got it wrong twice.
//
// Attempt 1 watched only the control count. That is not a wait at all when the
// count is ALREADY the expected value: loading a 1-param patch over a 1-param
// patch satisfied it before the compile was even queued, so the scenario then
// asserted against the PREVIOUS patch's state and "passed".
//
// Attempt 2 added the source of record. Better, but still wrong, and CI is what
// caught it. currentFaustSource is assigned on the COMPILE thread
// (PluginProcessor.cpp:180-181); the grid is rebuilt later, on the message
// thread, via callAsync. So a patch with an unchanged control count could match
// on source and still be showing the old labels. That raced green on this dev
// box and failed on a slower runner — the same dev-box-versus-runner shape as
// PF-027, in a test written the same day PF-027 was closed.
//
// The refresh counter is unambiguous: it is bumped once per refreshParamKnobs,
// on the message thread, after the widgets exist. Waiting for it to ADVANCE
// cannot be satisfied by any prior state.
bool loadAndSettle(Session& s, const juce::String& source, int expectedControls,
                   PluginForgeProcessor::LoadMode mode
                       = PluginForgeProcessor::LoadMode::Fresh)
{
    const int before = s.editor.gridRefreshCountForTest();
    s.processor.loadFaustCode(source, "editor session test", mode);
    const bool ok = pumpUntil([&] {
        return s.processor.currentSourceForTest() == source
            && s.editor.gridRefreshCountForTest() > before
            && s.editor.gridControlCountForTest() == expectedControls;
    });
    if (! ok)
        std::printf("      (load did not settle: source %s, refresh %d->%d, "
                    "%d controls, wanted %d)\n",
                    s.processor.currentSourceForTest() == source ? "matched" : "DID NOT match",
                    before, s.editor.gridRefreshCountForTest(),
                    s.editor.gridControlCountForTest(), expectedControls);
    return ok;
}

// Renders silence-free blocks through the real processBlock, so OutputGuard sees
// what it would see in a DAW.
void render(PluginForgeProcessor& p, int blocks)
{
    juce::AudioBuffer<float> buf(2, 512);
    juce::MidiBuffer midi;
    for (int b = 0; b < blocks; ++b)
    {
        for (int ch = 0; ch < buf.getNumChannels(); ++ch)
            for (int i = 0; i < buf.getNumSamples(); ++i)
                buf.setSample(ch, i, 0.25f * std::sin(0.05f * (float) i));
        p.processBlock(buf, midi);
    }
}

// Spins processBlock (deliberately NOT juce::MessageManager pumping) until the
// named source is the one live. Safe without a message-loop pump because
// FaustEngine compiles on its OWN persistent worker thread
// (PluginProcessor.h:217-219: "cb fires on that worker (NOT the message
// thread)") and currentFaustSource is assigned ON THAT THREAD
// (PluginProcessor.cpp:180-181) -- both the ready flag and the source of
// record flip independent of anything this loop does; it exists only to keep
// calling processBlock so a keyboard note queued before the DSP existed gets
// drained the instant it can be.
bool renderUntilReady(PluginForgeProcessor& p, const juce::String& source, int timeoutMs = 20000)
{
    juce::AudioBuffer<float> buf(2, 512);
    juce::MidiBuffer midi;
    const auto deadline = juce::Time::getMillisecondCounter() + (juce::uint32) timeoutMs;
    while (juce::Time::getMillisecondCounter() < deadline)
    {
        for (int ch = 0; ch < buf.getNumChannels(); ++ch)
            for (int i = 0; i < buf.getNumSamples(); ++i)
                buf.setSample(ch, i, 0.25f * std::sin(0.05f * (float) i));
        p.processBlock(buf, midi);
        if (p.currentSourceForTest() == source && p.isInstrumentForTest())
            return true;
    }
    return false;
}

using WidgetKind = ParamGridPanel::WidgetKind;

// ═══════════════════════════════════════════════════════════════════════════
//  Scenarios
// ═══════════════════════════════════════════════════════════════════════════

// 1 — Type a prompt, hit Generate, it works.
void scenario01_happyPath(const juce::File& tmp)
{
    scenario("1. generate a working patch",
             "type a prompt, click Generate, the subprocess returns valid Faust");

    FakeGenerator::install(
        FakeGenerator::writeSuccess(tmp, "gen_ok.sh", FakeGenerator::trivialPatch()));

    Session s;
    check(s.editor.gridControlCountForTest() == 0, "grid starts empty (nothing compiled yet)");

    s.editor.submitPromptForTest("a warm lowpass filter");

    const bool live = pumpUntil([&] {
        return s.editor.statusTextForTest().contains("DSP live");
    });
    check(live, "status reaches 'Ready - DSP live' after a successful generation");
    check(s.processor.currentPromptForTest() == "a warm lowpass filter",
          "the processor retained the prompt that produced the patch");
    check(s.processor.currentSourceForTest().isNotEmpty(),
          "the processor retained the generated source");
    snapshot(s.editor, "01_happy_path");
}

// 2 — Every Faust UI kind renders as the widget the header promises.
void scenario02_widgetKinds()
{
    scenario("2. widget kinds match the Faust kinds",
             "PF-005's central claim, unconfirmed by anything since 2026-07-23");

    Session s;
    const bool ok = loadAndSettle(s, kEveryKindPatch, 5);
    check(ok, "a 5-param patch (one of each kind) compiled and reached the grid");
    if (! ok) return;

    // Look each param up BY LABEL. Slot order is not declaration order — this
    // run reports Bypass, Cutoff, Level, Trigger, Voices for a patch that
    // declares Cutoff, Level, Voices, Bypass, Trigger, i.e. alphabetical. That
    // is worth knowing (a user's knobs are not in the order the patch names
    // them) but it is not what this scenario is about, and asserting position
    // would make the test fail whenever that ordering changed.
    struct Expect { const char* label; WidgetKind kind; };
    const Expect expected[] = {
        { "Cutoff",  WidgetKind::HorizontalSlider },
        { "Level",   WidgetKind::VerticalSlider   },
        { "Voices",  WidgetKind::IncDec           },
        { "Bypass",  WidgetKind::Toggle           },
        { "Trigger", WidgetKind::Toggle           },
    };

    for (const auto& e : expected)
    {
        int found = -1;
        for (int i = 0; i < s.editor.gridControlCountForTest(); ++i)
            if (s.editor.gridControlLabelForTest(i) == e.label)
                found = i;

        if (found < 0)
        {
            check(false, juce::String("a control labelled '") + e.label + "' exists");
            continue;
        }
        const auto got = s.editor.gridControlKindForTest(found);
        check(got == e.kind,
              juce::String("'") + e.label + "' renders as a "
                  + ParamGridPanel::widgetKindName(e.kind) + ", got "
                  + ParamGridPanel::widgetKindName(got));
    }

    // Record the ordering as a fact rather than asserting it, so a future change
    // shows up in the log without failing a test that is about widget kinds.
    juce::StringArray order;
    for (int i = 0; i < s.editor.gridControlCountForTest(); ++i)
        order.add(s.editor.gridControlLabelForTest(i));
    std::printf("      knob order on screen: %s\n", order.joinIntoString(", ").toRawUTF8());

    // The claim underneath the claim. ui_design_plan.md describes rotary as the
    // fallback widget, and the switch in refreshParamKnobs has a `default:` arm
    // that produces one — but FaustEngine::Kind has exactly five values, and all
    // five are handled explicitly. So a real Faust param can NEVER render as a
    // rotary. That is not a defect; it is dead code the docs describe as live,
    // and a reader deserves to know the editor shows no knobs.
    bool anyRotary = false;
    for (int i = 0; i < s.editor.gridControlCountForTest(); ++i)
        anyRotary = anyRotary || s.editor.gridControlKindForTest(i) == WidgetKind::Rotary;
    check(! anyRotary,
          "no param renders as a rotary -- the fallback arm is unreachable for real "
          "Faust kinds, whatever ui_design_plan.md says");

    snapshot(s.editor, "02_widget_kinds");
}

// 3 — A patch with more params than the window can show.
void scenario03_overflow()
{
    scenario("3. a 40-param patch",
             "grid caps at POOL_SIZE, the window caps at kMaxGridRows, the rest scrolls");

    Session s;
    const int n = 40;
    const bool ok = loadAndSettle(s, manyParamPatch(n), n);
    check(ok, juce::String("a ") + juce::String(n) + "-param patch reached the grid");
    if (! ok) return;

    check(s.editor.gridControlCountForTest() <= ParamPool::POOL_SIZE,
          "control count never exceeds ParamPool::POOL_SIZE (64)");

    // The window must have grown, and must have stopped growing. The cap is
    // kMaxGridRows (6) * kCellH (95) + chrome; past it the Viewport scrolls.
    const int h = s.editor.getHeight();
    check(h > 500, juce::String("window grew past its default for 40 params (now ")
                       + juce::String(h) + "px)");
    check(h <= 1200, juce::String("window stayed inside setResizeLimits' 1200px max (")
                         + juce::String(h) + "px)");
    snapshot(s.editor, "03_overflow_40_params");

    // ── PF-051: past 64, the surplus is BOUNDED, not silently infinite ───────
    // A patch declaring more controls than the pool has slots used to lose the
    // surplus with no error, no log and no count anywhere: remap() looped to
    // POOL_SIZE and never looked at params[64+], and pushToFaust's
    // min(infos.size(), slots.size()) hid it a second time. 70 controls compiled,
    // 6 of them were unreachable, and nothing in the product said so.
    //
    // What is asserted here is the INVARIANT, not the message: exactly POOL_SIZE
    // slots are occupied and the grid shows exactly that many. The overflow list
    // itself goes to the log today -- surfacing it in the UI is real work and is
    // deliberately not in this change, which is why this pins the boundary rather
    // than a status string that does not exist yet.
    const int tooMany = ParamPool::POOL_SIZE + 6;
    const bool loaded = loadAndSettle(s, manyParamPatch(tooMany), ParamPool::POOL_SIZE);
    check(loaded, juce::String("a ") + juce::String(tooMany)
                      + "-param patch compiled and settled at the pool ceiling");
    if (loaded)
    {
        check(s.processor.mappedSlotCountForTest() == ParamPool::POOL_SIZE,
              juce::String("exactly POOL_SIZE slots are occupied (")
                  + juce::String(s.processor.mappedSlotCountForTest()) + ")");
        check(s.editor.gridControlCountForTest() == ParamPool::POOL_SIZE,
              "the grid renders exactly the slots that exist, and no more");
    }
}

// 4 — Generation fails, and the error survives exactly as long as it should.
void scenario04_errorSurfacing(const juce::File& tmp)
{
    scenario("4. a generation failure, then a resubmit",
             "the full error is shown, and PF-021: the next submit clears it");

    // Real newlines. FakeGenerator builds the payload with juce::JSON, which owns
    // the escaping, so nothing here needs to anticipate a shell.
    const juce::String longError =
        "Faust compilation failed after 3 attempts.\n\n"
        "attempt 1: syntax error, unexpected IDENT\n"
        "attempt 2: endless evaluation cycle detected\n"
        "attempt 3: 2 outputs must equal 1 input";

    FakeGenerator::install(
        FakeGenerator::writeFailure(tmp, "gen_fail.sh", longError, "invalid_faust"));

    Session s;
    s.editor.submitPromptForTest("a ping-pong delay");

    const bool shown = pumpUntil([&] { return s.editor.errorTextForTest().isNotEmpty(); });
    check(shown, "the failure reached the error region");
    if (shown)
    {
        const auto text = s.editor.errorTextForTest();
        check(text.contains("attempt 3"),
              "the error is UNTRUNCATED -- the third attempt's text is present, not "
              "cut at the status line's 200-char cap");
        check(text.contains("endless evaluation cycle"),
              "the middle of a multi-line error survives too");
    }
    snapshot(s.editor, "04a_error_shown");

    // PF-021, through the editor this time. The human reported the stale error on
    // 2026-07-24: a previous failure sat on screen through the next generation,
    // including a successful one, and read as the current result.
    s.editor.submitPromptForTest("something else entirely");
    check(s.editor.errorTextForTest().isEmpty(),
          "PF-021: submitting again clears the previous run's error IMMEDIATELY, "
          "before the new run can finish");
    pumpUntil([&] { return s.editor.errorTextForTest().isNotEmpty(); }, 8000);
    snapshot(s.editor, "04b_error_cleared_on_submit");
}

// 5 — The LLM succeeds and the Faust compiler does not.
void scenario05_faustCompileFailure(const juce::File& tmp)
{
    scenario("5. valid JSON, invalid Faust",
             "the compile error reaches the status line, and PF-022 holds");

    Session s;

    // First establish a good patch, so there is a source of record worth protecting.
    const bool good = loadAndSettle(s, kTinyPatch, 1);
    check(good, "a good patch is live first");
    const auto goodSource = s.processor.currentSourceForTest();
    check(goodSource.contains("Gain"), "the good source is the source of record");

    // Now hand the processor Faust that cannot compile.
    s.processor.loadFaustCode("import(\"stdfaust.lib\");\nprocess = this is not faust;",
                              "a broken patch");
    const bool surfaced = pumpUntil([&] {
        return s.editor.statusTextForTest().contains("Faust compile error");
    });
    check(surfaced, "the JIT's failure reaches the status line");

    // PF-022. currentFaustSource is assigned only in the compile SUCCESS branch
    // (PluginProcessor.cpp:180-181), so a failed generate must leave the previous
    // good source, its labels and its values consistent with each other — a DAW
    // save in this window has to persist a triple that still restores.
    check(s.processor.currentSourceForTest() == goodSource,
          "PF-022: the failed compile did NOT overwrite the source of record");
    check(s.editor.gridControlCountForTest() == 1,
          "the previous patch's controls are still on screen (nothing was torn down)");
    snapshot(s.editor, "05_faust_compile_error");
}

// 6 — Generate A, move a knob, generate B.
void scenario06_freshResetsKnobs()
{
    scenario("6. a new patch does not inherit the old one's knob positions",
             "PF-020's LoadMode::Fresh, observed through the UI for the first time");

    Session s;
    check(loadAndSettle(s, kTinyPatch, 1), "patch A (Gain, default 0.5) is live");

    // Move the knob, the way a user would.
    if (auto* rp = s.processor.apvts.getParameter(ParamPool::slotId(0)))
        rp->setValueNotifyingHost(1.0f);
    pump(100);
    check(s.editor.gridControlValueForTest(0) > 0.9,
          "the knob moved to the top and the widget followed it");

    // Patch B declares a DIFFERENT default in the same slot. Under the pre-PF-020
    // behaviour the old 1.0 would survive and drive B's zone by slot index.
    const char* patchB = R"(import("stdfaust.lib");
w = hslider("Width", 0.2, 0, 1, 0.01);
process = _ * w, _ * w;
)";
    check(loadAndSettle(s, patchB, 1), "patch B (Width, default 0.2) replaced it");
    check(s.editor.gridControlLabelForTest(0) == "Width", "the label followed the new patch");
    check(s.editor.gridControlValueForTest(0) < 0.5,
          "PF-020: the slot was RESET to patch B's default, not left at patch A's 1.0");
    snapshot(s.editor, "06a_fresh_resets_knobs");

    // ── The other half of the affordance ────────────────────────────────────
    // Iterate is what "make the resonance stronger" needs, and it is now
    // reachable from the editor rather than only from code. This is also the red
    // case for the assertion above: if Fresh and Iterate produced the same
    // result, one of these two checks would have to fail.
    check(! s.editor.refineEnabledForTest(),
          "Refine is OFF by default -- making the mode visible must not change it");

    if (auto* rp = s.processor.apvts.getParameter(ParamPool::slotId(0)))
        rp->setValueNotifyingHost(1.0f);
    pump(100);

    s.editor.setRefineForTest(true);
    check(s.editor.refineEnabledForTest(), "Refine can be turned on");

    // Patch C keeps the control's NAME and changes the DSP around it -- which is
    // what a refine ("make it wider") actually is. `Width` therefore has the same
    // identity, reclaims its slot, and the user's 1.0 stands.
    //
    // This used to rename the control to "Depth" and still expect the 1.0 to
    // survive. That only worked because retention was positional: slot 0 kept its
    // value no matter which parameter landed there. Under identity-keyed
    // assignment a renamed control is a DIFFERENT control -- nothing in the source
    // says otherwise -- so it is seeded to its own declared default. The rename is
    // now asserted separately below, as a distinct outcome rather than an
    // accident.
    const char* patchC = R"(import("stdfaust.lib");
w = hslider("Width", 0.2, 0, 1, 0.01);
process = _ * w * 0.5, _ * w * 0.5;
)";
    check(loadAndSettle(s, patchC, 1, PluginForgeProcessor::LoadMode::Iterate),
          "patch C loaded in Iterate mode");
    check(s.editor.gridControlLabelForTest(0) == "Width", "the label still followed");
    check(s.editor.gridControlValueForTest(0) > 0.9,
          juce::String("Iterate KEPT the knob at 1.0 for the SAME control (got ")
              + juce::String(s.editor.gridControlValueForTest(0), 3) + ")");
    snapshot(s.editor, "06b_refine_keeps_knobs");

    // ── And the other outcome: a RENAMED control is a new control ────────────
    // Stated as its own assertion because it is a real product decision, not an
    // implementation detail. If a future change decides a rename should carry the
    // value over (via a similarity heuristic, say), this is the check that will
    // fail and force the decision to be made on purpose.
    const char* patchD = R"(import("stdfaust.lib");
w = hslider("Depth", 0.2, 0, 1, 0.01);
process = _ * w, _ * w;
)";
    check(loadAndSettle(s, patchD, 1, PluginForgeProcessor::LoadMode::Iterate),
          "patch D renames the control");
    check(s.editor.gridControlLabelForTest(0) == "Depth", "the label followed the rename");
    check(s.editor.gridControlValueForTest(0) < 0.5,
          juce::String("a RENAMED control is a new control: seeded to its own 0.2, "
                       "not handed the old control's 1.0 (got ")
              + juce::String(s.editor.gridControlValueForTest(0), 3) + ")");
}

// 7 — The output guard trips and the user is told.
void scenario07_outputGuardMute()
{
    scenario("7. a runaway patch mutes itself",
             "the shell's 30Hz edge-detect writes the mute to the status line");

    Session s;
    check(loadAndSettle(s, kRunawayPatch, 0), "the runaway patch compiled (0 params)");

    check(! s.processor.isOutputMuted(), "the guard has not tripped before any audio");
    render(s.processor, 200);           // ~2.1s at 48k — past the guard's half-second
    check(s.processor.isOutputMuted(), "the guard latched on the runaway output");

    // The edge-detect is what the user actually experiences. It is written on
    // TRANSITION only (PluginEditor.cpp:100-112) so a compile message is not
    // stomped 30 times a second — which also means a test that never ticks the
    // timer sees nothing at all.
    s.editor.pumpMeterTickForTest();
    const auto status = s.editor.statusTextForTest();
    check(status.contains("MUTED"),
          juce::String("the mute reached the status line (got: '") + status + "')");
    check(status.contains("ran away") || status.contains("NaN"),
          "the status names WHICH trip fired, not just that one did");

    // Written on transition only: a second tick must not rewrite it.
    s.editor.pumpMeterTickForTest();
    check(s.editor.statusTextForTest() == status,
          "a second timer tick does not rewrite the message (edge-detected, not polled)");
    snapshot(s.editor, "07_output_guard_mute");
}

// 8 — Five Generate clicks in a row.
void scenario08_rapidFire(const juce::File& tmp)
{
    scenario("8. five rapid Generate clicks",
             "one worker, the last one wins, nothing stacks (PF-006 through the editor)");

    // A slow generator, so the clicks genuinely overlap.
    FakeGenerator::install(
        FakeGenerator::writeSuccess(tmp, "gen_slow.sh", FakeGenerator::trivialPatch(), 2));

    Session s;
    for (int i = 0; i < 5; ++i)
    {
        s.editor.submitPromptForTest("prompt " + juce::String(i));
        pump(60);
    }

    const bool settled = pumpUntil([&] {
        return s.processor.currentPromptForTest().isNotEmpty();
    }, 30000);
    check(settled, "a generation eventually published");
    check(s.processor.currentPromptForTest() == "prompt 4",
          juce::String("only the LAST submit published (got '")
              + s.processor.currentPromptForTest() + "')");
    snapshot(s.editor, "08_rapid_fire");
}

// 9 — The window closes mid-generation.
void scenario09_teardownMidFlight(const juce::File& tmp)
{
    scenario("9. close the window mid-generation",
             "no crash, no wait for the 180s cap, attachments torn down in order");

    FakeGenerator::install(
        FakeGenerator::writeSuccess(tmp, "gen_slow2.sh", FakeGenerator::trivialPatch(), 30));

    const auto start = juce::Time::getMillisecondCounter();
    {
        Session s;
        // Give the grid live attachments first: teardown with widgets bound to
        // APVTS parameters is the ordering ParamGridPanel.h:45-57 documents, and
        // an attachment outliving its widget is undefined.
        loadAndSettle(s, kFourParamPatch, 4);
        check(s.editor.gridControlCountForTest() == 4, "four live attachments exist");

        s.editor.submitPromptForTest("something that will never finish in time");
        pump(300);                      // let the subprocess actually start
    }                                   // ← destructor runs here
    const auto elapsed = juce::Time::getMillisecondCounter() - start;

    check(true, "destroying the editor mid-generation did not crash");
    // The subprocess sleeps 30s and the panel's backstop is 180s. Returning
    // promptly is the whole point of killing the child rather than waiting.
    check(elapsed < 25000,
          juce::String("teardown returned promptly (") + juce::String((int) elapsed)
              + "ms) rather than waiting out the sleeping child");
}

// 10 — Save the session, reopen it.
void scenario10_stateRoundTrip()
{
    scenario("10. save and reopen",
             "the grid comes back with the RESTORED values, not the patch defaults");

    juce::MemoryBlock blob;
    {
        Session s;
        check(loadAndSettle(s, kFourParamPatch, 4), "a 4-param patch is live");

        // Move two knobs FAR from their defaults, then save. The patch declares
        // Alpha 0.5, Beta 0.25, Gamma 0.75, Delta 0.1, and slots are alphabetical
        // (see scenario 2), so slot 1 is Beta (default 0.25) and slot 3 is Gamma
        // (default 0.75). Both targets are chosen to be unreachable by accident:
        // a first draft moved slot 2 to 0.1, which is Delta's own default, so the
        // assertion passed whether the value was restored or reset.
        if (auto* rp = s.processor.apvts.getParameter(ParamPool::slotId(1)))
            rp->setValueNotifyingHost(0.95f);       // Beta: 0.25 -> 0.95
        if (auto* rp = s.processor.apvts.getParameter(ParamPool::slotId(3)))
            rp->setValueNotifyingHost(0.05f);       // Gamma: 0.75 -> 0.05
        // Also move the control style off its default before saving. It rides
        // the same blob (PluginProcessor.cpp:307, kUiStyleId) and its restore
        // path fires onUiStyleChanged BEFORE the recompile, so it belongs in the
        // scenario that owns the round trip rather than in a second one. Without
        // this the persistence half of the style feature had no assertion at all
        // — only the in-session flip did (scenario 13).
        s.processor.setUiStyle("rotary");
        pump(100);
        check(s.editor.controlStyleForTest() == "rotary",
              "the session was saved in the rotary style");
        std::printf("      saved with slot1=%.3f slot3=%.3f style=%s\n",
                    s.editor.gridControlValueForTest(1),
                    s.editor.gridControlValueForTest(3),
                    s.processor.uiStyle().toRawUTF8());
        s.processor.getStateInformation(blob);
        check(blob.getSize() > 0, "the session serialised to a non-empty blob");
        snapshot(s.editor, "10a_before_save");
    }

    // A fresh processor and a fresh editor — the reopened project.
    Session s2;
    s2.processor.setStateInformation(blob.getData(), (int) blob.getSize());

    const bool rebuilt = pumpUntil([&] { return s2.editor.gridControlCountForTest() == 4; });
    check(rebuilt, "the restore recompile rebuilt the grid in a fresh editor");
    if (! rebuilt) return;

    check(s2.processor.currentSourceForTest().contains("Alpha"),
          "the Faust source came back");

    const double got1 = s2.editor.gridControlValueForTest(1);
    const double got3 = s2.editor.gridControlValueForTest(3);
    std::printf("      restored slot1=%.3f (saved 0.950, patch default 0.250)\n", got1);
    std::printf("      restored slot3=%.3f (saved 0.050, patch default 0.750)\n", got3);

    // The reason state restore passes LoadMode::Iterate: replaceState() has just
    // written the saved values, and a Fresh reset would discard exactly what was
    // restored (PluginProcessor.cpp:65-69). But Iterate only governs the
    // PROCESSOR's reset. ParamGridPanel::refreshParamKnobs seeds every mapped
    // slot from the patch defaults UNCONDITIONALLY, on every compile, including
    // the restore recompile — so with the editor OPEN the seeding lands after the
    // restore and overwrites it. That is what these two assertions catch, and it
    // is the mirror image of PF-020: that bug was the UI-layer seeding failing to
    // run headless, this one is the same seeding running when it must not.
    check(got1 > 0.8,
          juce::String("slot 1 came back at its SAVED 0.95, not the patch default 0.25 "
                       "(got ") + juce::String(got1, 3) + ")");
    check(got3 < 0.2,
          juce::String("slot 3 came back at its SAVED 0.05, not the patch default 0.75 "
                       "(got ") + juce::String(got3, 3) + ")");

    // The style came back too, and — the part that is easy to get wrong — it is
    // the PANEL that is in it, not merely the processor's stored string. The
    // restore fires onUiStyleChanged before the recompile precisely so the
    // rebuilt widgets are styled on their first frame; asserting the panel is
    // what distinguishes that from a value that persisted but never applied.
    check(s2.processor.uiStyle() == "rotary",
          juce::String("the processor restored the saved style -- got '")
              + s2.processor.uiStyle() + "'");
    const bool styled = pumpUntil([&] {
        return s2.editor.controlStyleForTest() == juce::String("rotary");
    });
    check(styled,
          juce::String("the reopened editor's grid is actually in rotary -- got '")
              + s2.editor.controlStyleForTest() + "'");
    check(s2.editor.styleButtonTextForTest() == "Knobs: rotary",
          juce::String("the reopened button caption matches -- got '")
              + s2.editor.styleButtonTextForTest() + "'");

    snapshot(s2.editor, "10b_after_restore");
}

// 11 — Open the hood.
void scenario11_codeView()
{
    scenario("11. show the generated Faust",
             "ux_roadmap Phase 3a: read-only source view behind a disclosure");

    Session s;
    check(! s.editor.codeVisibleForTest(),
          "the code view starts HIDDEN -- a no-code tool must not open on a wall of DSL");

    const int hiddenH = s.editor.getHeight();

    // Reveal it before anything has compiled: it must say so, not sit blank.
    s.editor.setCodeVisibleForTest(true);
    pump(50);
    check(s.editor.codeVisibleForTest(), "the disclosure shows it");
    check(s.editor.codeTextForTest().contains("No patch compiled yet"),
          "with nothing compiled it explains itself instead of showing an empty box");
    check(s.editor.getHeight() > hiddenH,
          juce::String("the window grew to make room (") + juce::String(hiddenH)
              + " -> " + juce::String(s.editor.getHeight()) + "px)");
    snapshot(s.editor, "11a_code_view_empty");

    // Now compile something and confirm the view follows it.
    check(loadAndSettle(s, kEveryKindPatch, 5), "a 5-param patch compiled");
    pump(200);
    const auto shown = s.editor.codeTextForTest();
    check(shown.contains("hslider(\"Cutoff\""),
          "the view shows the ACTUAL source of the live patch");
    check(shown == s.processor.currentSource(),
          "what is on screen is exactly the processor's source of record, not a copy "
          "that can drift");
    check(s.editor.codeIsReadOnlyForTest(),
          "it is READ-ONLY -- Phase 3a is the view alone; an editable box with no "
          "Compile button is worse than a label");
    snapshot(s.editor, "11b_code_view_populated");

    // Hiding it gives the band back to the grid.
    s.editor.setCodeVisibleForTest(false);
    pump(50);
    check(! s.editor.codeVisibleForTest(), "it hides again");

    // A view revealed AFTER a compile must not be blank: showSource is pushed on
    // the way up, not only from the compile callback.
    s.editor.setCodeVisibleForTest(true);
    pump(50);
    check(s.editor.codeTextForTest().contains("hslider(\"Cutoff\""),
          "reopening it shows the live patch, not the placeholder");
    snapshot(s.editor, "11c_code_view_reopened");
}

// 12 — What the knobs actually SAY.
// 13 — Flipping the control style is PURELY visual: no rebuild, no value moves.
//
// The claim is that a style change restyles the existing widgets in place rather
// than recreating them, so the SliderAttachments survive and the parameters never
// see the change. That is easy to assert badly. Two traps this deliberately
// avoids:
//
//   * Asserting only that values are unchanged. A style switch that silently did
//     NOTHING would pass that. So the widget KINDS are asserted to have changed
//     in the same pass — the test fails if the restyle did not happen at all.
//   * Leaving every slot at its default. A rebuild resets slots to patch defaults
//     (that is what refreshParamKnobs' sibling path does), and a default-valued
//     slot is indistinguishable before and after such a reset. So every slot is
//     moved OFF its default first; only then is a rebuild detectable by value.
void scenario13_styleSwitchDoesNotThrash()
{
    scenario("13. changing control style does not rebuild or move anything",
             "one Slider + one SliderAttachment survive the flip; DSP untouched");

    Session s;
    check(loadAndSettle(s, kEveryKindPatch, 5), "the 5-param patch compiled");
    if (s.editor.gridControlCountForTest() != 5) return;

    const int n = s.editor.gridControlCountForTest();

    // ── Move every slot off its default ─────────────────────────────────────
    // Written through the APVTS, the way a DAW or a user gesture would, so the
    // attachments carry them to the widgets. Distinct per slot so a mix-up
    // between controls cannot pass.
    for (int i = 0; i < n; ++i)
        s.processor.apvts.getParameterAsValue(ParamPool::slotId(i))
            .setValue(0.3f + 0.07f * static_cast<float>(i));

    // Let the attachments propagate to the widgets before snapshotting.
    pump(60);

    struct Snap { juce::String label, text; double value; ParamGridPanel::WidgetKind kind; };
    std::vector<Snap> before;
    std::vector<float> beforeSlots;
    for (int i = 0; i < n; ++i)
    {
        before.push_back({ s.editor.gridControlLabelForTest(i),
                           s.editor.gridControlTextForTest(i),
                           s.editor.gridControlValueForTest(i),
                           s.editor.gridControlKindForTest(i) });
        beforeSlots.push_back(
            s.processor.apvts.getParameter(ParamPool::slotId(i))->getValue());
    }
    const int refreshBefore = s.editor.gridRefreshCountForTest();

    check(s.editor.controlStyleForTest() == "faithful",
          "starts in the shipped style (faithful)");

    // ── Flip through every style and back ───────────────────────────────────
    // Driven through the PROCESSOR, which is the real path: setUiStyle stores
    // and notifies, the editor's callback applies it after a callAsync hop.
    // Waiting on the panel's own style is what makes this deterministic.
    const char* cycle[] = { "rotary", "horizontal", "faithful" };
    for (const auto* want : cycle)
    {
        s.processor.setUiStyle(want);
        const bool landed = pumpUntil([&] {
            return s.editor.controlStyleForTest() == juce::String(want);
        });
        check(landed, juce::String("the panel reached style '") + want + "'");
        if (! landed) return;

        check(s.processor.uiStyle() == juce::String(want),
              juce::String("the processor stored '") + want + "' as the record");

        // The style must actually have taken effect somewhere, or every value
        // assertion below is vacuous. In rotary EVERY continuous control is a
        // Rotary; in horizontal every one is a HorizontalSlider.
        if (juce::String(want) != "faithful")
        {
            const auto expectKind = juce::String(want) == "rotary"
                                        ? ParamGridPanel::WidgetKind::Rotary
                                        : ParamGridPanel::WidgetKind::HorizontalSlider;
            int continuous = 0, matching = 0, togglesKept = 0;
            for (int i = 0; i < n; ++i)
            {
                const auto k = s.editor.gridControlKindForTest(i);
                if (k == ParamGridPanel::WidgetKind::Toggle) { ++togglesKept; continue; }
                ++continuous;
                if (k == expectKind) ++matching;
            }
            check(continuous > 0 && matching == continuous,
                  juce::String("every continuous control restyled to ")
                      + ParamGridPanel::widgetKindName(expectKind)
                      + " (" + juce::String(matching) + "/" + juce::String(continuous) + ")");
            // PF-005's promise, now under a style that could plausibly break it.
            // kEveryKindPatch declares one button and one checkbox.
            check(togglesKept == 2,
                  juce::String("both toggle-kind params stayed toggles, got ")
                      + juce::String(togglesKept));
        }

        // ── The actual no-thrash claim ──────────────────────────────────────
        check(s.editor.gridRefreshCountForTest() == refreshBefore,
              juce::String("no grid rebuild: refresh count still ")
                  + juce::String(refreshBefore));
        check(s.editor.gridControlCountForTest() == n,
              "control count unchanged");

        bool valuesHeld = true, slotsHeld = true, labelsHeld = true, textHeld = true;
        for (int i = 0; i < n; ++i)
        {
            if (std::abs(s.editor.gridControlValueForTest(i) - before[(size_t) i].value) > 1.0e-9)
                valuesHeld = false;
            if (std::abs(s.processor.apvts.getParameter(ParamPool::slotId(i))->getValue()
                         - beforeSlots[(size_t) i]) > 1.0e-9f)
                slotsHeld = false;
            if (s.editor.gridControlLabelForTest(i) != before[(size_t) i].label)
                labelsHeld = false;
            if (s.editor.gridControlTextForTest(i) != before[(size_t) i].text)
                textHeld = false;
        }
        check(valuesHeld, juce::String("every widget value survived the flip to ") + want);
        check(slotsHeld,  juce::String("every APVTS slot survived the flip to ") + want);
        check(labelsHeld, juce::String("every label survived the flip to ") + want);
        // The readout is denormalised through ParamMap; a rebuilt attachment
        // would reinstall JUCE's default text function and print raw slots again
        // (PF-037's exact regression), so this catches that specifically.
        check(textHeld,   juce::String("every real-unit readout survived the flip to ") + want);
    }

    check(s.editor.styleButtonTextForTest() == "Knobs: auto",
          "the button caption tracked the round trip back to faithful");

    snapshot(s.editor, "13_style_round_trip");
}

void scenario12_readout()
{
    scenario("12. the readout shows real units, not slot numbers",
             "PF-037: an 800 Hz cutoff used to read 0.04");

    Session s;
    check(loadAndSettle(s, kEveryKindPatch, 5), "the 5-param patch compiled");

    // kEveryKindPatch declares hslider("Cutoff", 800, 20, 20000, 1) with NO
    // [unit:Hz] and NO [scale:log] -- which is why the defect report's number
    // was 0.04 (the LINEAR slot) rather than 0.53 (the log one). This is the
    // exact patch shape the harness photographed, so it is the right one to
    // assert on.
    struct Expect { const char* label; const char* text; const char* why; };
    const Expect expected[] = {
        { "Cutoff",  "800",  "a 20..20000 control at its 800 default (was 0.04)" },
        { "Voices",  "2",    "a discrete 1..8 count (was 0.14)" },
        { "Level",   "0.50", "a 0..1 control keeps its 0.01 step's two decimals" },
        { "Bypass",  "Off",  "a checkbox reads its state, not a number" },
        { "Trigger", "Off",  "a momentary button likewise" },
    };

    for (const auto& e : expected)
    {
        int found = -1;
        for (int i = 0; i < s.editor.gridControlCountForTest(); ++i)
            if (s.editor.gridControlLabelForTest(i) == e.label)
                found = i;

        if (found < 0)
        {
            check(false, juce::String("a control labelled '") + e.label + "' exists");
            continue;
        }

        const auto got = s.editor.gridControlTextForTest(found);
        check(got == e.text,
              juce::String("'") + e.label + "' reads \"" + e.text + "\" -- " + e.why
                  + " (got \"" + got + "\")");
    }

    // The claim underneath: the SLOT is still 0..1 and still what the parameter
    // holds. Only the text is denormalised. If this ever starts reading 800, the
    // readout has been implemented by corrupting the parameter, and the DAW's
    // automation lane goes with it.
    int cutoffIdx = -1;
    for (int i = 0; i < s.editor.gridControlCountForTest(); ++i)
        if (s.editor.gridControlLabelForTest(i) == "Cutoff")
            cutoffIdx = i;
    if (cutoffIdx >= 0)
    {
        const double slot = s.editor.gridControlValueForTest(cutoffIdx);
        // PF-040's regression, asserted here because this is where it was found.
        // The slot for an 800 Hz default on a linear 20..20000 control is
        // 0.039039; with JUCE's hardcoded 0.01 interval it landed on 0.04 and
        // the readout said 819. Asserting the SLOT rather than only the text
        // means a return of the quantisation fails here even if the rounding
        // happens to still print "800".
        check(std::fabs(slot - 0.039039) < 0.0005,
              juce::String("the slot holds the patch's exact default, not a 1/100 snap "
                           "(PF-040) -- got ") + juce::String(slot, 6));
        check(slot >= 0.0 && slot <= 1.0,
              juce::String("the underlying slot is still 0..1 (") + juce::String(slot, 4)
                  + ") -- the text was denormalised, the parameter was not");
    }

    snapshot(s.editor, "12_readout");
}

// 14 — The enclosing group path Faust reports is captured, not dropped.
//
// ParamCapture's four box callbacks were empty bodies until 2026-07-31, so every
// parameter arrived as a flat list. They now maintain a groupStack
// (FaustEngine.cpp:15-42) and stamp ParamInfo::group (:105). Nothing LAYS OUT by
// that field yet -- it is the input a sectioned surface needs -- so without this
// scenario the capture had no assertion anywhere: tools/ui_iterate.sh records the
// groups into its manifest but makes no claims about them by design, which is
// exactly the shape of a control that is believed to run and does not.
//
// The expected values are not guessed. They are read off `faust -lang cpp` for
// this exact patch, whose buildUserInterface emits:
//     openVerticalBox("<filename>");     <- dropped: a filename, not a section
//       openVerticalBox("Amp");
//         addHorizontalSlider("Level", ...);        -> "Amp"
//       closeBox();
//       addHorizontalSlider("Loose", ...);          -> ""   (no enclosing group)
//       openVerticalBox("Osc");
//         openHorizontalBox("Tune");
//           addHorizontalSlider("Fine", ...);       -> "Osc/Tune"
//         closeBox();
//       closeBox();
//     closeBox();
// Note the ORDER: Faust sorts alphabetically at each level, which interleaves the
// ungrouped param between the two groups. That is Faust's ordering and not
// anything the grid does -- see the note on ParamInfo::group about PF-038.
void scenario14_groupCapture()
{
    scenario("14. the Faust group path is captured",
             "nesting joins outermost-first; the filename wrapper is dropped; "
             "an ungrouped param reports empty");

    // Three params spanning the three cases in one patch, so the ungrouped case
    // is exercised ALONGSIDE grouped ones rather than in a patch that has no
    // groups at all -- an implementation that dropped the stack entirely would
    // pass the latter.
    const char* kGroupedPatch = R"(import("stdfaust.lib");
loose = hslider("Loose", 0.5, 0, 1, 0.01);
inner = vgroup("Osc", hgroup("Tune", hslider("Fine", 0, -1, 1, 0.01)));
outer = vgroup("Amp", hslider("Level", 0.5, 0, 1, 0.01));
amt = (loose + inner + outer) * 0.3;
process = _ * amt, _ * amt;
)";

    Session s;
    check(loadAndSettle(s, kGroupedPatch, 3), "the 3-param grouped patch compiled");
    if (s.editor.gridControlCountForTest() != 3) return;

    // Keyed by LABEL, not by index: index order is Faust's alphabetical ordering,
    // and this scenario is about the group field, not about ordering. Asserting
    // positionally would make it fail for an unrelated reason if that ordering
    // ever changed.
    std::map<juce::String, juce::String> byLabel;
    for (int i = 0; i < 3; ++i)
        byLabel[s.editor.gridControlLabelForTest(i)] = s.editor.gridControlGroupForTest(i);

    const auto groupOf = [&byLabel](const char* label) -> juce::String
    {
        auto it = byLabel.find(juce::String(label));
        return it == byLabel.end() ? juce::String("<no such control>") : it->second;
    };

    check(groupOf("Level") == "Amp",
          juce::String("'Level' reports its enclosing vgroup -- got '")
              + groupOf("Level") + "'");
    check(groupOf("Fine") == "Osc/Tune",
          juce::String("'Fine' joins nested groups outermost-first -- got '")
              + groupOf("Fine") + "'");
    // The two cases most likely to regress together: the wrapper leaking in would
    // turn this into the .dsp filename rather than the empty string.
    check(groupOf("Loose").isEmpty(),
          juce::String("'Loose' is outside every group and reports empty -- got '")
              + groupOf("Loose") + "'");
    bool wrapperLeaked = false;
    for (const auto& kv : byLabel)
        if (kv.second.startsWith("pluginforge") || kv.second.contains(".dsp"))
            wrapperLeaked = true;
    check(! wrapperLeaked,
          "Faust's outermost filename box never appears in any path");

    // A patch with NO groups must report empty for every param -- the fallback a
    // sectioned layout has to handle, and the state every patch was in before the
    // callbacks were implemented.
    check(loadAndSettle(s, kFourParamPatch, 4), "an ungrouped 4-param patch compiled");
    int nonEmpty = 0;
    for (int i = 0; i < s.editor.gridControlCountForTest(); ++i)
        if (s.editor.gridControlGroupForTest(i).isNotEmpty())
            ++nonEmpty;
    check(nonEmpty == 0,
          juce::String("a patch with no groups reports no groups -- ")
              + juce::String(nonEmpty) + " non-empty");
}

// 15 — A regeneration that reorders, renames, adds and removes parameters must
//      move each user value with the PARAMETER it belongs to, not with its index.
//
// THE BUG THIS PINS. Until identity-keyed assignment, ParamPool::remap copied
// params[i] into slot i, so a parameter's identity WAS its ordinal position.
// Faust emits widgets alphabetically (FaustEngine.h, the ordering note on
// ParamInfo::group), so simply INSERTING a control whose name sorts early
// renumbered everything after it:
//
//     patch A alphabetical:  Cutoff(0)  Gain(1)  Res(2)
//     patch B alphabetical:  Cutoff(0)  Drive(1) Gain(2)
//
// "Gain" moved from slot 1 to slot 2 without the user touching anything, and
// under Iterate the value the user had dialled into Gain stayed at slot 1 --
// where the brand-new "Drive" now lived. The user's gain setting silently
// became a drive setting. That is what this scenario asserts is over.
void scenario15_identityKeyedRetention()
{
    scenario("15. a value follows its parameter across a regeneration",
             "reorder + rename + add + remove; identity-keyed remap, not positional");

    // Deliberately named so ALPHABETICAL order and DECLARATION order differ, and
    // so patch B's newcomer sorts between two of A's survivors. If the ids were
    // ordinal this could not pass.
    const char* patchA = R"(import("stdfaust.lib");
res    = hslider("Res",    0.3, 0, 1, 0.01);
gain   = hslider("Gain",   0.5, 0, 1, 0.01);
cutoff = hslider("Cutoff", 0.4, 0, 1, 0.01);
process = _ * gain * (cutoff + res), _ * gain * (cutoff + res);
)";

    Session s;
    check(loadAndSettle(s, patchA, 3), "patch A is live with 3 params");

    // Establish which slot each label occupies, rather than assuming Faust's
    // ordering. The test must assert RETENTION, not Faust's emission order --
    // pinning the latter would make it fail on a Faust upgrade for no reason.
    auto slotOfLabel = [&](const juce::String& want) {
        for (int i = 0; i < s.editor.gridControlCountForTest(); ++i)
            if (s.editor.gridControlLabelForTest(i) == want)
                return i;
        return -1;
    };

    const int gainSlotA = slotOfLabel("Gain");
    const int resSlotA  = slotOfLabel("Res");
    check(gainSlotA >= 0 && resSlotA >= 0, "patch A published both Gain and Res");

    // The user dials in two values by hand.
    if (auto* rp = s.processor.apvts.getParameter(ParamPool::slotId(gainSlotA)))
        rp->setValueNotifyingHost(0.90f);
    if (auto* rp = s.processor.apvts.getParameter(ParamPool::slotId(resSlotA)))
        rp->setValueNotifyingHost(0.80f);
    pump(100);

    // Patch B: Res is GONE, Drive is NEW (and sorts before Gain), Cutoff and Gain
    // survive. Under positional remap this renumbers Gain.
    const char* patchB = R"(import("stdfaust.lib");
drive  = hslider("Drive",  0.2, 0, 1, 0.01);
gain   = hslider("Gain",   0.5, 0, 1, 0.01);
cutoff = hslider("Cutoff", 0.4, 0, 1, 0.01);
process = _ * gain * (cutoff + drive), _ * gain * (cutoff + drive);
)";

    check(loadAndSettle(s, patchB, 3, PluginForgeProcessor::LoadMode::Iterate),
          "patch B replaced it in Iterate mode");

    const int gainSlotB  = slotOfLabel("Gain");
    const int driveSlotB = slotOfLabel("Drive");
    check(gainSlotB >= 0 && driveSlotB >= 0, "patch B published both Gain and Drive");

    // ── THE ASSERTION ───────────────────────────────────────────────────────
    check(gainSlotB == gainSlotA,
          juce::String("Gain RECLAIMED its slot (was ") + juce::String(gainSlotA)
              + ", now " + juce::String(gainSlotB) + ")");

    const float gainNow =
        s.processor.apvts.getParameter(ParamPool::slotId(gainSlotB))->getValue();
    check(std::abs(gainNow - 0.90f) < 0.01f,
          juce::String("Gain KEPT the user's 0.90 across the regeneration (got ")
              + juce::String(gainNow, 3) + ")");

    // ── THE NEGATIVE, and the reason RemapResult::newlyAssignedSlots exists ──
    // Drive is a newcomer that lands on the slot Res vacated. Iterate does not
    // reset values, so without per-slot seeding it would come up holding Res's
    // 0.80 -- a value the user set on a control that no longer exists. It must
    // show its OWN declared default of 0.2 instead.
    const float driveNow =
        s.processor.apvts.getParameter(ParamPool::slotId(driveSlotB))->getValue();
    check(std::abs(driveNow - 0.20f) < 0.01f,
          juce::String("Drive shows its OWN default, not the deleted Res's 0.80 (got ")
              + juce::String(driveNow, 3) + ")");

    snapshot(s.editor, "15_identity_keyed_retention");

    // ── The other direction: Fresh must still reset EVERYTHING ──────────────
    // Without this, "retention works" could be satisfied by a remap that simply
    // never resets -- which would silently break PF-020. One of these two checks
    // has to fail for any implementation that gets the distinction wrong.
    check(loadAndSettle(s, patchB, 3, PluginForgeProcessor::LoadMode::Fresh),
          "the same patch reloaded in Fresh mode");
    const float gainAfterFresh =
        s.processor.apvts.getParameter(ParamPool::slotId(slotOfLabel("Gain")))->getValue();
    check(std::abs(gainAfterFresh - 0.50f) < 0.01f,
          juce::String("Fresh RESET Gain to the patch default 0.50 (got ")
              + juce::String(gainAfterFresh, 3) + ")");
}

// 16 — Refine actually carries the prior Faust source to the LLM (A2-A6).
//
// STATUS.md's cited name for this test was scenario15_refineCarriesTheSource --
// that slot is scenario15_identityKeyedRetention above; this is 16, the next
// free slot. FakeGenerator::writeSuccessCapturing (FakeGenerator.h) is what
// makes this provable: it records argv and, when used, the --request-file
// payload PromptPanel actually wrote, rather than ignoring both the way
// writeSuccess()'s cat does.
void scenario16_refineCarriesTheSource(const juce::File& tmp)
{
    scenario("16. Refine carries the prior Faust source",
             "the request-file transport (A2), the fold (A3), and the "
             "first-generation degrade (A4)");

    // ── Transport note (kind selector, 2026-08-06) ───────────────────────────
    // The request-file transport is now UNCONDITIONAL: once the kind selector
    // exists, every generation carries kind, and kind has no argv transport
    // (the old --prompt path could not express it). So the assertions below live
    // at the JSON-content level -- what the payload CARRIES -- not at the
    // argv-shape level. The substance is unchanged: prior_source appears in the
    // request iff there is a prior source to carry (Refine on after a
    // generation), and is ABSENT when Refine is off or there is nothing yet.

    // A control name distinctive enough that finding it inside a captured
    // prior_source could not be an accident.
    const char* markerPatch = R"(import("stdfaust.lib");
z = hslider("Zzyzx", 0.5, 0, 1, 0.01);
process = _ * z, _ * z;
)";

    FakeGenerator::install(
        FakeGenerator::writeSuccessCapturing(tmp, "gen16", markerPatch));

    Session s;

    // ── Negative half: Refine OFF ────────────────────────────────────────────
    check(! s.editor.refineEnabledForTest(), "Refine starts off");
    s.editor.submitPromptForTest("a filter with a distinctive control");
    const bool firstLive = pumpUntil([&] {
        return s.editor.statusTextForTest().contains("DSP live");
    });
    check(firstLive, "the first (Refine-off) generation reached DSP live");

    auto argv1 = FakeGenerator::capturedArgv(tmp, "gen16");
    check(argv1.contains("--request-file"),
          "Refine off: request-file transport (kind has no argv form)");
    check(! argv1.contains("--prompt"),
          "Refine off: the old plain --prompt path is gone");
    auto req1 = FakeGenerator::capturedRequestJson(tmp, "gen16");
    check(req1.isNotEmpty(), "Refine off: a request.json was still written");
    check(req1.contains("\"kind\""),
          "Refine off: the request carries the kind selector value");
    check(! req1.contains("Zzyzx"),
          "Refine off: NO prior_source folded in -- Refine off must not send the "
          "previous patch as context");
    check(s.processor.currentSourceForTest().contains("Zzyzx"),
          "the processor's source of record carries the marker control -- what "
          "the next half's refine has to actually reuse");

    // ── Positive half: Refine ON, after a first successful generation ───────
    s.editor.setRefineForTest(true);
    check(s.editor.refineEnabledForTest(), "Refine turned on");

    s.editor.submitPromptForTest("make the resonance stronger");
    const bool secondLive = pumpUntil([&] {
        return s.editor.statusTextForTest().contains("DSP live");
    });
    check(secondLive, "the second (Refine-on) generation reached DSP live");

    auto argv2 = FakeGenerator::capturedArgv(tmp, "gen16");
    check(argv2.contains("--request-file"),
          "Refine on with a prior source: argv carries --request-file");
    check(! argv2.contains("--prompt"),
          "Refine on with a prior source: NOT the plain --prompt path");

    auto requestJson = FakeGenerator::capturedRequestJson(tmp, "gen16");
    check(requestJson.isNotEmpty(), "a request.json payload was actually written");
    check(requestJson.contains("Zzyzx"),
          "the captured prior_source carries the first patch's marker control -- "
          "the wiring STATUS.md's Broken #2 (\"Refine does not refine\") says did "
          "not exist before this change");
    check(requestJson.contains("make the resonance stronger"),
          "the request also carries the new prompt text, not just the prior source");
    check(requestJson.contains("\"kind\""),
          "Refine on: the request still carries the kind selector value");

    // ── The degrade half: Refine ticked before anything has ever generated ──
    // A fresh session -- currentSource() is empty -- so even with Refine ON at
    // submit time there is nothing yet to refine. A4's requirement: the PAYLOAD
    // degrades to plain --prompt; the MODE (Iterate) does not silently become
    // Fresh, which is what scenario06_freshResetsKnobs already covers and this
    // test must not re-litigate.
    FakeGenerator::install(
        FakeGenerator::writeSuccessCapturing(tmp, "gen16b", markerPatch));

    Session s2;
    s2.editor.setRefineForTest(true);
    check(s2.editor.refineEnabledForTest(),
          "second session: Refine ticked before any generation");

    s2.editor.submitPromptForTest("a first patch, but with Refine already ticked");
    const bool thirdLive = pumpUntil([&] {
        return s2.editor.statusTextForTest().contains("DSP live");
    });
    check(thirdLive, "the degrade-case generation still reached DSP live");

    auto argv3 = FakeGenerator::capturedArgv(tmp, "gen16b");
    check(argv3.contains("--request-file"),
          "first generation with Refine ticked: request-file transport (kind)");
    auto req3 = FakeGenerator::capturedRequestJson(tmp, "gen16b");
    check(req3.isNotEmpty(), "degrade case: a request.json was written");
    check(! req3.contains("Zzyzx"),
          "first generation with Refine ticked: NO prior_source in the request -- "
          "the PAYLOAD degrades (nothing to refine yet) even though the MODE is "
          "Iterate, so a hollow prior_source is never sent");

    snapshot(s.editor, "16_refine_carries_the_source");
}

// 17 — The on-screen keyboard producer: click a key, hear a note, release it.
//
// THE GAP THIS CLOSES. The NoteRing queue and its drain in processBlock were
// TSan-proven and fully wired (PluginProcessor.h:172-193, PluginProcessor.cpp's
// drain loop after the MIDI walk) but nothing in the editor ever called the
// producer side -- OfflineRenderTest.cpp exercised pushKeyboardNote() directly,
// which proves the QUEUE works but says nothing about whether a human clicking
// the editor can ever reach it. This scenario drives KeyboardPanel's own
// juce::MidiKeyboardState::Listener callback (via noteOnForTest/noteOffForTest,
// which call keyboardState.noteOn()/noteOff() -- see KeyboardPanel.h for why
// that is the same code path a real click or a computer-keypress reaches, not
// a shortcut around it), not FaustEngine or NoteRing directly.
void scenario17_keyboardNoteRoundTrip()
{
    scenario("17. the on-screen keyboard producer: a note plays and releases",
             "PluginProcessor.h:172-193 / KeyboardPanel -- the last piece of the "
             "NoteRing path, now reachable from the editor");

    Session s;
    check(loadAndSettle(s, kGatedSawSynthPatch, 1), "the instrument compiled and is live");

    s.editor.pumpMeterTickForTest();   // the 30Hz poll enables the keyboard
    check(s.editor.keyboardPlayableForTest(), "the keyboard enables for an instrument patch");

    render(s.processor, 10);
    check(s.processor.outputLevel.load(std::memory_order_relaxed) < 0.05f,
          "silent with no note held");

    s.editor.keyboardNoteOnForTest(69, 1.0f);
    render(s.processor, 40);
    const float held = s.processor.outputLevel.load(std::memory_order_relaxed);
    check(held > 0.02f,
          juce::String("the on-screen note SOUNDS (peak ") + juce::String(held, 5) + ")");
    check(s.processor.droppedKeyboardNotes() == 0, "no keyboard event was dropped");

    s.editor.keyboardNoteOffForTest(69);
    render(s.processor, 60);   // past en.adsr's 0.2s release
    const float tail = s.processor.outputLevel.load(std::memory_order_relaxed);
    check(tail < held * 0.1f,
          juce::String("the on-screen note RELEASES (tail ") + juce::String(tail, 6)
              + " vs held " + juce::String(held, 5) + ")");
    snapshot(s.editor, "17_keyboard_round_trip");
}

// 18 — Last-note-priority reaches the voice THROUGH the on-screen keyboard.
//
// FaustEngine.cpp:485-528 already implements the legato guard (freq/gain set
// before the gate; noteOff only releases the gate for the note that is still
// currentNote) -- that is proven elsewhere. What this proves is that
// KeyboardPanel forwards two DIFFERENT notes as two DIFFERENT events rather
// than, say, collapsing them, and adds no competing priority logic of its own.
// It would fail if KeyboardPanel's wiring were reverted (nothing would ever
// sound) and it would also fail if the panel mismapped which note-off belongs
// to which note (releasing A would then wrongly cut B short).
void scenario18_keyboardLastNotePriority()
{
    scenario("18. last-note-priority, proven through the on-screen keyboard",
             "note-on A, note-on B, note-off A -> B is still sounding");

    Session s;
    check(loadAndSettle(s, kGatedSawSynthPatch, 1), "the instrument compiled and is live");
    s.editor.pumpMeterTickForTest();
    check(s.editor.keyboardPlayableForTest(), "the keyboard is enabled");

    s.editor.keyboardNoteOnForTest(60, 1.0f);     // note A
    render(s.processor, 15);
    s.editor.keyboardNoteOnForTest(64, 1.0f);     // note B steals it (A never released)
    render(s.processor, 30);
    const float bothHeld = s.processor.outputLevel.load(std::memory_order_relaxed);
    check(bothHeld > 0.02f, "sounding after A then B, both through the on-screen keyboard");

    // Releasing A must be a no-op: currentNote is B.
    s.editor.keyboardNoteOffForTest(60);
    render(s.processor, 20);
    const float afterAOff = s.processor.outputLevel.load(std::memory_order_relaxed);
    check(afterAOff > bothHeld * 0.5f,
          juce::String("A's release did NOT cut B short (before ") + juce::String(bothHeld, 5)
              + ", after A-off " + juce::String(afterAOff, 5) + ")");

    // Releasing B is the real release.
    s.editor.keyboardNoteOffForTest(64);
    render(s.processor, 60);
    const float afterBOff = s.processor.outputLevel.load(std::memory_order_relaxed);
    check(afterBOff < bothHeld * 0.1f,
          juce::String("B's release DOES silence the voice (") + juce::String(afterBOff, 6) + ")");
    snapshot(s.editor, "18_keyboard_last_note_priority");
}

// 19 — A keyboard note queued before/through a compile swap is not lost.
//
// Fact #6 (PluginProcessor.h:182-187, PluginProcessor.cpp's early-return
// comment at the top of processBlock): a keyboard note-off queued while
// faustEngine.enterAudio() is false is NOT lost -- the ring is only ever
// DRAINED inside the enterAudio()/exitAudio() bracket, so an event queued
// while that bracket is refused simply waits, and the next successful block
// drains it in order. A hardware MIDI note-off in the same window IS dropped,
// because the MidiBuffer itself is gone by the next block -- NoteRing is not.
//
// WHAT THIS TEST ACTUALLY EXERCISES, stated precisely because it matters for
// the RISK line: it queues the note before ANY DSP has ever compiled, which
// is the SAME branch (`if (!faustEngine.enterAudio()) { ...; return; }`,
// PluginProcessor.cpp:155-169) a live recompile's ready=false window takes --
// not a race against an in-flight recompile's exact timing. That is
// deliberate: hitting the live-recompile window from a black-box test would
// depend on the JIT's wall-clock duration and would be flaky by construction,
// and FaustEngine.cpp's runCompile is explicitly out of scope to instrument
// for this task. The code path exercised (early return -> ring not drained ->
// drained on the next ready block) is identical either way; what is NOT
// proven here is the timing overlap with a SECOND, already-loaded patch's
// in-flight swap. See the report's RISK line.
void scenario19_keyboardSurvivesCompileSwap()
{
    scenario("19. a keyboard note queued before the DSP exists is not lost",
             "PluginProcessor.h:182-187 -- the ring waits; a hardware note-off would not");

    Session s;
    check(s.processor.currentSourceForTest().isEmpty(), "nothing compiled yet");

    // Queued while faustEngine.enterAudio() is unconditionally false: there is
    // no DSP at all, so every render() call below takes the early-return
    // branch until the compile started next actually finishes.
    s.editor.keyboardNoteOnForTest(69, 1.0f);

    s.processor.loadFaustCode(kGatedSawSynthPatch, "keyboard swap test");
    const bool ready = renderUntilReady(s.processor, kGatedSawSynthPatch);
    check(ready, "the compile finished and the instrument went live");
    if (! ready) return;

    check(s.processor.droppedKeyboardNotes() == 0,
          "the note queued before the DSP existed was not dropped");

    render(s.processor, 40);   // let the envelope build
    const float held = s.processor.outputLevel.load(std::memory_order_relaxed);
    check(held > 0.02f,
          juce::String("the note queued BEFORE the DSP existed is sounding now that it "
                       "is ready (peak ") + juce::String(held, 5) + ")");

    s.editor.keyboardNoteOffForTest(69);
    render(s.processor, 60);
    const float tail = s.processor.outputLevel.load(std::memory_order_relaxed);
    check(tail < held * 0.1f,
          juce::String("note-off through the same producer still releases the voice "
                       "after the swap (tail ") + juce::String(tail, 6) + ")");
    snapshot(s.editor, "19_keyboard_survives_compile_swap");
}

// 20 — An effect patch (no voice contract) disables the keyboard.
void scenario20_keyboardDisabledForEffect()
{
    scenario("20. an effect patch disables the keyboard",
             "no voice contract to play -- clicks must not silently accept notes "
             "nothing consumes");

    Session s;

    // Asserted BEFORE loading anything: this is the constructor no-op bug's
    // exact reproduction -- a fresh editor whose keyboard has never received
    // a real setPlayable() transition (isInstrument() defaults false, so the
    // 30Hz timer's setPlayable(false) call is ALSO a no-op the first time it
    // fires). Before KeyboardPanel.cpp's applyPlayableVisuals() fix, this
    // failed: the flag read false correctly, but the widgets stayed enabled,
    // full-alpha and unlabelled -- a keyboard that looks and feels playable
    // while every note is silently discarded.
    check(! s.editor.keyboardPlayableForTest(), "fresh editor: flag starts disabled");
    check(! s.editor.keyboardEnabledForTest(),
          "fresh editor: the keyboard widget itself starts disabled, not just the flag");
    check(s.editor.keyboardAlphaForTest() < 1.0f,
          "fresh editor: the keyboard is visibly dimmed");
    check(s.editor.keyboardDisabledLabelVisibleForTest(),
          "fresh editor: \"Load an instrument to play\" is showing");
    check(s.editor.keyboardOctaveUpIsHitTargetForTest(),
          "fresh editor: the disabled overlay does not block the enabled octave controls");

    check(loadAndSettle(s, kTinyPatch, 1), "a plain effect (Gain) compiled");
    s.editor.pumpMeterTickForTest();
    check(! s.editor.keyboardPlayableForTest(),
          "the keyboard reports disabled for a patch with no voice contract");
    check(! s.editor.keyboardEnabledForTest(),
          "effect patch: the keyboard widget stays disabled");
    check(s.editor.keyboardAlphaForTest() < 1.0f, "effect patch: the keyboard stays dimmed");
    check(s.editor.keyboardDisabledLabelVisibleForTest(),
          "effect patch: the disabled label stays visible");

    // Even a note that reaches the ring -- bypassing the disabled UI, the way
    // this test-only entry point does; see KeyboardPanel::noteOnForTest --
    // must not sound: processBlock's drain discards it because
    // faustEngine.isInstrument() is false for this patch (`if (! playable)
    // continue;` in PluginProcessor.cpp's drain loop).
    s.editor.keyboardNoteOnForTest(69, 1.0f);
    render(s.processor, 20);
    check(! s.processor.isOutputMuted(), "the effect keeps running normally, unaffected");
    check(s.processor.droppedKeyboardNotes() == 0,
          "the note was consumed by the unconditional drain, not left stuck in the ring");
    snapshot(s.editor, "20_keyboard_disabled_for_effect");
}

// 21 — allNotesOff(0) on setPlayable(false) actually releases a held note.
//
// THE GAP THIS CLOSES. Scenario 20 loads an EFFECT FIRST, so
// KeyboardPanel::setPlayable(false)'s keyboardState.allNotesOff(0) call
// (KeyboardPanel.cpp, inside setPlayable) always runs against an EMPTY
// held-note set there -- deleting that allNotesOff(0) call today breaks no
// existing scenario. This holds a note on an INSTRUMENT first, then swaps to
// an effect, so the transition itself is what setPlayable(false) has to clean
// up after -- the actual user story ("I was playing, then generated a new
// effect chain, and a key stayed stuck").
//
// HOW THE PROOF WORKS, since nothing exposes "is note 60 still held" (JUCE's
// own MidiKeyboardState::isNoteOn() exists, juce_MidiKeyboardState.h:39-45,
// but KeyboardPanel.h adds no accessor for it, and adding one is out of this
// brief's scope). juce::MidiKeyboardState::noteOff()
// (juce_MidiKeyboardState.cpp:79-91) is a NO-OP -- no listener call, nothing
// forwarded -- for a note that is not currently on; the whole body is guarded
// by `if (isNoteOn (midiChannel, midiNoteNumber))`. So the only way
// KeyboardPanel::handleNoteOff can fire for note 60 is if MidiKeyboardState
// genuinely still considered it held at the moment allNotesOff(0) ran.
//
// That handleNoteOff, if it fires, pushes exactly one NoteEvent into
// PluginForgeProcessor's ring (PluginProcessor.h:188-193,
// pushKeyboardNote()). Nothing else touches the ring between the note-on
// being drained (the render() call below) and this point: loadAndSettle()
// only pumps the MESSAGE loop (juce::MessageManager::runDispatchLoopUntil),
// never processBlock, so the ring is exactly as empty as the last
// processBlock call left it. That makes ring OCCUPANCY -- measured by how
// many more pushes fit before NoteRing.h's capacity (256 slots, one
// permanently unused, so 255 is the max occupancy before overflow) -- an
// honest, indirect proxy for "did the release actually get queued":
//   * if allNotesOff(0) queued the release, occupancy is 1 going into the
//     fill loop below, and 255 further pushes is one too many -- exactly the
//     LAST of them overflows.
//   * if allNotesOff(0) never ran (the deleted-line red case), occupancy is
//     0 going in, and all 255 further pushes succeed.
// This is also exactly why the loop below runs 255 times and not fewer: any
// smaller number cannot tell the two cases apart.
//
// (b) from the brief -- droppedKeyboardNotes() sane, not incrementing from
// this path -- is checked TWICE: once right after the swap (must still read
// 0, or the proof below has no clean baseline), and once as a cross-check
// against the fill loop's own local failure count.
void scenario21_allNotesOffReleasesHeldNoteOnDisable()
{
    scenario("21. setPlayable(false) releases a note held across an instrument -> effect swap",
             "KeyboardPanel::setPlayable's allNotesOff(0) path -- untouched by scenario 20, "
             "which always runs it against an empty held-note set");

    Session s;
    check(loadAndSettle(s, kGatedSawSynthPatch, 1), "the instrument compiled and is live");
    s.editor.pumpMeterTickForTest();
    check(s.editor.keyboardPlayableForTest(), "the keyboard enables for the instrument");

    // Hold note 60 and prove it is actually SOUNDING -- not just queued -- so
    // "held" means what a user would mean by it, the same bar scenario 17 sets.
    s.editor.keyboardNoteOnForTest(60, 1.0f);
    render(s.processor, 40);
    const float held = s.processor.outputLevel.load(std::memory_order_relaxed);
    check(held > 0.02f, juce::String("note 60 is actually sounding before the swap (peak ")
                             + juce::String(held, 5) + ")");
    check(s.processor.droppedKeyboardNotes() == 0, "nothing dropped getting the note held");

    // Swap to an EFFECT. loadAndSettle only pumps the message loop -- no
    // processBlock call happens here, so the ring is untouched by this step;
    // the render() above already drained it back to empty.
    check(loadAndSettle(s, kTinyPatch, 1), "an effect patch replaced the instrument");
    check(! s.processor.isInstrumentForTest(), "the live DSP no longer declares a voice contract");
    check(s.processor.droppedKeyboardNotes() == 0,
          "still nothing dropped -- the clean baseline the proof below depends on");

    // Drive the SAME mechanism a real session uses: the editor's 30Hz timer,
    // not a test-only setter (KeyboardPanel.h has none for this; pumping the
    // timer here IS PluginEditor::timerCallback(), the real call site).
    s.editor.pumpMeterTickForTest();
    check(! s.editor.keyboardPlayableForTest(),
          "the keyboard disables now that the live patch has no voice contract");

    // ── The proof: ring occupancy, not audio ────────────────────────────────
    // faustEngine.isInstrument() is now false, so even if this note-off HAD
    // reached processBlock's drain it would be discarded there (`if (!
    // playable) continue;`, PluginProcessor.cpp) -- the effect has no gate to
    // release. "Released" here can only mean "reached the ring", which is
    // exactly what the header comment above sets out to measure directly, via
    // PluginForgeProcessor::pushKeyboardNote() -- the same public entry point
    // OfflineRenderTest.cpp already uses directly (KeyboardPanel.h:57-68).
    int failedPushes = 0;
    for (int i = 0; i < 255; ++i)
        if (! s.processor.pushKeyboardNote(61 + (i % 30), 100, true))
            ++failedPushes;

    check(failedPushes == 1,
          juce::String("exactly one of 255 filler pushes overflowed (got ")
              + juce::String(failedPushes)
              + ") -- proof the ring already held ONE event (the auto-released note-off) "
                "before this loop started");
    check(s.processor.droppedKeyboardNotes() == 1,
          juce::String("the ring's own drop counter agrees (got ")
              + juce::String((int) s.processor.droppedKeyboardNotes()) + ")");

    snapshot(s.editor, "21_allnotesoff_releases_held_note");
}

// 22 — QWERTY / computer-keyboard mapping is internally self-consistent.
//
// WHY THIS IS SCOPED THE WAY IT IS. juce_MidiKeyboardComponent.cpp's
// keyStateChanged() (the private override that answers a real OS keypress)
// gates real note-firing on KeyPress::isCurrentlyDown() -- actual OS keyboard
// state that only a compositor-level input tool can fake. wtype, ydotool and
// xdotool are all absent on this machine -- this file's own header comment
// ("WHAT SIMULATED HUMAN MEANS HERE, precisely") already records that no
// synthetic input exists here. Calling keyStateChanged(true) directly to work
// around that was considered and rejected: that is a DIFFERENT branch than a
// real keypress takes and produces note-OFFs, not note-ons, for a key not
// already tracked as down -- exercising it would prove nothing but its own
// workaround.
//
// NOT COVERED: real keypress-to-note firing. Nothing on this machine can
// synthesize a keypress the compositor and JUCE's KeyPress::isCurrentlyDown()
// will see as real, so end-to-end QWERTY input is untested here and untested
// anywhere else in this repo. This scenario only checks that the mapping
// KeyboardPanel configures is internally self-consistent -- it does NOT
// prove that a keypress, on this or any machine, actually produces a note.
//
// REWIRED 2026-08-12 (C5): this used to read three literal constants copied
// by hand from KeyboardPanel.cpp, because juce::MidiKeyboardComponent keeps
// keyMappingOctave PRIVATE with no getter and KeyboardPanel exposed no
// accessor of its own -- adding one was explicitly out of scope for the
// session that wrote this scenario. C5 added exactly that accessor
// (KeyboardPanel::currentOctaveForTest(), availableRange{Low,High}ForTest(),
// reached through the editor's keyboardOctaveForTest()/
// keyboardAvailableLow/HighForTest() forwarders) to build the octave [<]/[>]
// controls, and those literals would have gone silently false the moment a
// user clicked one -- the old comment block said as much: "this only fails
// once a human rereads and rewrites the assertion". Now it reads the live
// object instead. What is still NOT introspected: JUCE's own default qwerty
// map ("awsedftgyhujkolp;", 17 characters, offsets 0..16 via
// setKeyPressForNote(), juce_MidiKeyboardComponent.cpp:36,38-39) has no
// accessor at all and remains a cited constant, per
// juce_MidiKeyboardComponent.h:120-127's documented formula
// "this value + (12 * the current base octave)".
void scenario22_qwertyMappingLiveContract()
{
    scenario("22. QWERTY mapping is internally self-consistent",
             "reads the live KeyboardPanel object (C5) -- runtime octave "
             "controls would otherwise make copied literals silently false "
             "instead of failing");

    // Session still constructed: this at least proves KeyboardPanel builds
    // and lays out under these exact settings, the same construction path a
    // real session takes, and leaves a snapshot for a human's eye.
    Session s;
    check(s.editor.getHeight() > 0, "the editor (and its KeyboardPanel) constructed");

    // ── The mapping arithmetic: two literals read live, one still cited ────
    const int availableLow   = s.editor.keyboardAvailableLowForTest();
    const int availableHigh  = s.editor.keyboardAvailableHighForTest();
    const int baseOctave     = s.editor.keyboardOctaveForTest();
    const int qwertyKeyCount = 17;                    // "awsedftgyhujkolp;" -- JUCE's fixed default table, not KeyboardPanel state
    const int lowestOffset   = 0;
    const int highestOffset  = qwertyKeyCount - 1;     // 16

    const int mappedLow  = 12 * baseOctave + lowestOffset;
    const int mappedHigh = 12 * baseOctave + highestOffset;
    std::printf("      QWERTY maps to MIDI %d..%d; visible/available range is %d..%d\n",
                mappedLow, mappedHigh, availableLow, availableHigh);

    check(mappedLow >= availableLow && mappedHigh <= availableHigh,
          juce::String("every QWERTY-mapped note (") + juce::String(mappedLow) + ".."
              + juce::String(mappedHigh) + ") falls inside the visible range ("
              + juce::String(availableLow) + ".." + juce::String(availableHigh)
              + ") -- setKeyPressBaseOctave() lines up with setAvailableRange(), "
                "read live so it cannot silently drift");

    // ── The other half: nothing on the construction path locks NoteRing's
    //    forbidden call ─────────────────────────────────────────────────────
    // Grepped by hand while writing this scenario, not at runtime (there is
    // no principled way to tell "calls it" from "mentions it in a comment
    // explaining why it must not" by regex -- KeyboardPanel.h itself contains
    // the string three times, all in prose):
    //   * host/Source/ -- zero call sites; every hit is a comment in
    //     KeyboardPanel.h, NoteRing.h or PluginProcessor.cpp explaining why
    //     the call is forbidden, not an actual call.
    //   * juce_MidiKeyboardComponent.cpp/.h in full (the ~/JUCE checkout this
    //     build compiles against) -- zero hits at all; the component's own
    //     mouse and keyStateChanged() handlers call state.noteOn()/noteOff()
    //     directly (the same two methods KeyboardPanel's Listener callback
    //     receives), never MidiKeyboardState::processNextMidiBuffer().
    check(true,
          "grepped host/Source/ and juce_MidiKeyboardComponent.cpp/.h in full: no call site "
          "for processNextMidiBuffer anywhere on the keyboard-widget construction path "
          "(NoteRing.h's whole reason for existing)");

    snapshot(s.editor, "22_qwerty_mapping_static_contract");
}

// 23 — The family selector reaches the LLM request with the build-fixed kind.
//
// `kind` existed in generate.py's request schema (select_prompt at :85-95, read
// at :331) but nothing in the host ever sent it: the workflow-step-2 type
// override was half-built with no wire. This scenario drives the selector and
// proves the request JSON carries the chosen kind. The default is the build
// target (PF_IS_SYNTH) so a synth build opens on "Instrument" without the user
// touching anything — the selector is an override, not a required choice.
void scenario23_kindSelectorReachesRequest(const juce::File& tmp)
{
    scenario("23. The kind selector's choice reaches the LLM request",
             "the generation-time type override (workflow step 2), wired to "
             "generate.py's existing kind field");

    FakeGenerator::install(
        FakeGenerator::writeSuccessCapturing(tmp, "gen23", kTinyPatch));

    Session s;

    check(s.editor.kindForTest() == juce::String(PF_IS_SYNTH ? "Instrument" : "Effect"),
          "kind is fixed to the build target (PF_IS_SYNTH)");

    const juce::String chosen = PF_IS_SYNTH ? "drum_synth" : "granular_effect";
    s.editor.setFamilyForTest(chosen);
    check(s.editor.familyForTest() == chosen, "family selector accepts a compatible profile");

    s.editor.submitPromptForTest("a small patch, kind must ride along");
    const bool live = pumpUntil([&] {
        return s.editor.statusTextForTest().contains("DSP live");
    });
    check(live, "the kind-carrying generation reached DSP live");

    // Parse the request rather than substring-match, so the assertion survives
    // any juce::JSON whitespace choice.
    auto req = FakeGenerator::capturedRequestJson(tmp, "gen23");
    check(req.isNotEmpty(), "a request.json was written");
    const auto parsed = juce::JSON::parse(req);
    check(parsed.getProperty("kind", "").toString()
              == juce::String(PF_IS_SYNTH ? "instrument" : "effect"),
          "the request JSON carries the build-fixed host role");
    check(parsed.getProperty("family", "").toString() == chosen,
          "the request JSON carries the selected generation family");

    snapshot(s.editor, "23_kind_selector_reaches_request");
}

// 24 — prior_source_dropped is surfaced, not swallowed (007 A/B subject).
//
// generate.py:381-386: a refine whose prior source overflowed the token budget
// becomes a fresh generation, and the response says so. Before this change the
// host ignored the field, so the user was silently handed a full regen. The
// conditional fake below is the honest shape: the flag must appear ONLY when the
// request actually carried a prior source (Refine on after a first generation),
// never when it didn't. The dispatch script greps the request JSON for the first
// patch's marker control — Zzyzx exists in the prior_source iff one was sent.
void scenario24_priorSourceDroppedSurfaced(const juce::File& tmp)
{
    scenario("24. prior_source_dropped is surfaced, not swallowed",
             "a refine that silently became a fresh generation must not be silent");

    const char* markerPatch = R"(import("stdfaust.lib");
z = hslider("Zzyzx", 0.5, 0, 1, 0.01);
process = _ * z, _ * z;
)";

    // Two response payloads: plain success, and success carrying the dropped flag.
    auto writeResponse = [&](const char* name, bool dropped) {
        auto* obj = new juce::DynamicObject();
        obj->setProperty("success", true);
        obj->setProperty("faust_code", markerPatch);
        obj->setProperty("attempts", 1);
        obj->setProperty("error", juce::var());
        obj->setProperty("reason", "ok");
        if (dropped)
            obj->setProperty("prior_source_dropped", true);
        auto f = tmp.getChildFile(name);
        f.replaceWithText(juce::JSON::toString(juce::var(obj), /* allOnOneLine */ true),
                          false, false, "\n");
        return f.getFullPathName();
    };
    const auto plain   = writeResponse("gen24_plain.json", false);
    const auto dropped = writeResponse("gen24_dropped.json", true);

    // Dispatch script: argv is `sh <script> --request-file <path>` (kind always
    // present → request-file always used), so $2 is the request JSON. Cat the
    // dropped response iff the request carried the marker (a prior source was
    // really sent), else the plain one.
    auto script = tmp.getChildFile("gen24");
    script.replaceWithText(
        juce::String("#!/bin/sh\n")
        + "if grep -q 'Zzyzx' \"$2\"; then cat '" + dropped + "'; else cat '"
        + plain + "'; fi\n",
        false, false, "\n");
    script.setExecutePermission(true);
    FakeGenerator::install(script);

    Session s;

    // ── First generation: Refine off, nothing to carry, no warning ──────────
    check(! s.editor.priorSourceDroppedForTest(), "dropped-source flag starts false");
    s.editor.submitPromptForTest("a filter with a distinctive control");
    const bool firstLive = pumpUntil([&] {
        return s.editor.statusTextForTest().contains("DSP live");
    });
    check(firstLive, "the first (Refine-off) generation reached DSP live");
    check(! s.editor.priorSourceDroppedForTest(),
          "Refine off: the dropped-source flag stays false (nothing was dropped)");
    check(! s.editor.statusTextForTest().contains("prior source dropped"),
          "Refine off: no dropped-source warning on the status line");

    // ── Second generation: Refine on, a prior source to carry — and it drops ──
    s.editor.setRefineForTest(true);
    s.editor.submitPromptForTest("make the resonance stronger");
    const bool secondLive = pumpUntil([&] {
        return s.editor.statusTextForTest().contains("DSP live");
    });
    check(secondLive, "the second (Refine-on) generation reached DSP live");
    check(s.editor.priorSourceDroppedForTest(),
          "Refine on + source dropped: the panel publishes the flag");
    check(s.editor.statusTextForTest().contains("prior source dropped"),
          "the status line warns the user the refine became a fresh generation");

    snapshot(s.editor, "24_prior_source_dropped_surfaced");
}

// 25 — Add/Redo unlock once a prior source exists, in-session AND across a
// project reopen (PromptPanel::setRefineModesAvailable).
//
// Before this change, refineSelector.setItemEnabled(2/3, false) ran once in the
// constructor and NOTHING ever re-enabled them: Add and Redo were selectable
// from test code (setSelectedId does not consult isItemEnabled -- only the
// popup's own selectIfEnabled does) but unreachable by any mouse click. This is
// the scenario that fails red against that state.
void scenario25_refineModesUnlock(const juce::File& tmp)
{
    scenario("25. Add/Redo unlock on first patch, and across a project load",
             "setRefineModesAvailable's two call sites: onFaustCompileSuccess "
             "within a session, and the PromptPanel constructor seed for a "
             "restore that completes before the editor -- and its callback -- "
             "even exist");

    const char* markerPatch = R"(import("stdfaust.lib");
z = hslider("Zzyzx25", 0.5, 0, 1, 0.01);
process = _ * z, _ * z;
)";

    FakeGenerator::install(
        FakeGenerator::writeSuccessCapturing(tmp, "gen25", markerPatch));

    Session s;
    check(! s.editor.refineModeAvailableForTest(),
          "a fresh session offers nothing to Add to or Redo from");

    s.editor.submitPromptForTest("a filter with a distinctive control");
    const bool live = pumpUntil([&] {
        return s.editor.statusTextForTest().contains("DSP live");
    });
    check(live, "the first generation reached DSP live");
    check(s.editor.refineModeAvailableForTest(),
          "Add/Redo unlock the moment a patch is actually live -- "
          "onFaustCompileSuccess's setRefineModesAvailable call");

    // ── Redo (mode 3) actually reaches the request, end to end ──────────────
    // Scenario 16 already proves mode 2 (Add) carries prior_source. Mode 3, and
    // the refine_mode STRING selection (PromptPanel.cpp's refineModeStr switch),
    // have zero end-to-end coverage before this.
    s.editor.setRefineModeForTest(3);
    check(s.editor.refineModeForTest() == 3, "Redo selected");
    s.editor.submitPromptForTest("make it sound completely different");
    const bool redoLive = pumpUntil([&] {
        return s.editor.statusTextForTest().contains("DSP live");
    });
    check(redoLive, "the Redo generation reached DSP live");
    auto redoReq = FakeGenerator::capturedRequestJson(tmp, "gen25");
    check(redoReq.contains("Zzyzx25"),
          "Redo: the prior source was carried in the request");
    // Space after the colon is not cosmetic here -- DynamicObject::writeAsJSON
    // (juce_DynamicObject.cpp) always emits "key": value, even allOnOneLine.
    check(redoReq.contains("\"refine_mode\": \"context\""),
          "Redo: refine_mode reached the request as \"context\", not just the "
          "prior source -- generate.py needs this to pick _CONTEXT_PREAMBLE "
          "over the surgical or legacy framing");

    // ── The restore-before-editor half ───────────────────────────────────────
    // A DAW calls setStateInformation BEFORE createEditor. Session (this file)
    // constructs processor and editor together and cannot express that order --
    // by the time any Session's editor exists, its onFaustCompileSuccess is
    // already wired, so a bug that broke ONLY the constructor seed (3b) would
    // still pass every Session-based assertion above. Hand-roll the real order.
    juce::MemoryBlock blob;
    s.processor.getStateInformation(blob);
    check(blob.getSize() > 0, "the live session serialised to a non-empty blob");

    PluginForgeProcessor p2;
    p2.prepareToPlay(48000.0, 512);
    p2.setStateInformation(blob.getData(), (int) blob.getSize());
    check(p2.currentSourceForTest().contains("Zzyzx25"),
          "the restored processor has the source BEFORE any editor exists -- "
          "setStateInformation commits currentFaustSource synchronously, "
          "ahead of the (async) restore recompile, same as a real DAW load");

    PluginForgeEditor e2 { p2 };
    e2.setSize(900, 500);
    // No pump. Whether or not the restore recompile has finished by now is
    // irrelevant to this assertion -- setRefineModesAvailable's constructor
    // seed reads processor.currentSource(), which was ALREADY populated above,
    // before e2 (and its onFaustCompileSuccess handler) existed at all. If
    // Add/Redo were unlocked ONLY by onFaustCompileSuccess, this check is false.
    check(e2.refineModeAvailableForTest(),
          "a freshly-opened editor over an ALREADY-restored project offers "
          "Add/Redo from its very first frame, with no generation and no pump");

    snapshot(s.editor, "25_refine_modes_unlock");
}

// 26 — Add refused: the FAILURE branch, not compile-success
// (llm/generate.py's _prior_source_refused_response via generate_json's
// surgical-mode preflight hard-fail).
//
// A refusal is success:false, so it can never reach loadFaustCode() or
// onFaustCompileSuccess. The direct assertion of that routing fact is below:
// the source of record must be UNCHANGED after a refusal.
void scenario26_priorSourceRefused(const juce::File& tmp)
{
    scenario("26. Add refused: the failure branch, not compile-success",
             "generate.py's surgical-mode hard-fail must read as a specific, "
             "actionable failure -- not \"LLM error\", and not a silent no-op");

    const char* markerPatch = R"(import("stdfaust.lib");
z = hslider("Zzyzx26", 0.5, 0, 1, 0.01);
process = _ * z, _ * z;
)";

    // ONE script for the whole scenario -- PromptPanel::generateScript resolves
    // the fake path once in its OWN constructor (PromptPanel.h), so reinstalling
    // mid-session (after `Session s` already exists) would be silently ignored,
    // same trap scenario24's comment names. Dispatch on a marker string in the
    // PROMPT (simpler and more robust here than grepping quoted JSON structure
    // like refine_mode) rather than reinstalling. writeFailure/writeSuccess are
    // called only for the "<name>.json" payload file each writes as a side
    // effect (detail::writeScript) -- their own wrapper scripts are unused and
    // overwritten below by the real dispatch script.
    FakeGenerator::writeSuccess(tmp, "gen26_success", markerPatch);
    FakeGenerator::writeFailure(tmp, "gen26_refused",
        "The prior source is too large to fit the token budget in surgical "
        "(\"Add\") mode. Choose \"Redo\" to regenerate with it as reference "
        "instead, or shorten the current patch first.",
        "error", 0, /* priorSourceRefused */ true);

    auto script = tmp.getChildFile("gen26");
    script.replaceWithText(
        juce::String("#!/bin/sh\n")
        + "if grep -q 'REFUSE_ME' \"$2\"; then cat '"
        + tmp.getChildFile("gen26_refused.json").getFullPathName() + "'; else cat '"
        + tmp.getChildFile("gen26_success.json").getFullPathName() + "'; fi\n",
        false, false, "\n");
    script.setExecutePermission(true);
    FakeGenerator::install(script);

    Session s;

    // ── First generation: succeed, so there is a real prior source to refuse ──
    s.editor.submitPromptForTest("a filter with a distinctive control");
    const bool firstLive = pumpUntil([&] {
        return s.editor.statusTextForTest().contains("DSP live");
    });
    check(firstLive, "the first generation reached DSP live");
    const auto sourceBeforeRefusal = s.processor.currentSourceForTest();
    check(sourceBeforeRefusal.contains("Zzyzx26"),
          "captured the live source before the refusal");
    check(! s.editor.priorSourceRefusedForTest(), "refused-flag starts false");

    // ── Second generation, in Add mode: the LLM layer refuses ───────────────
    check(s.editor.refineModeAvailableForTest(), "Add is selectable after a live patch");
    s.editor.setRefineModeForTest(2);   // Add
    s.editor.submitPromptForTest("REFUSE_ME add a chorus");
    const bool errShown = pumpUntil([&] { return s.editor.errorTextForTest().isNotEmpty(); });
    check(errShown, "the refusal reached the error region");

    // The direct assertion of the routing finding: nothing went live.
    check(s.processor.currentSourceForTest() == sourceBeforeRefusal,
          "a refusal must NOT change the source of record -- it never reaches "
          "loadFaustCode(), unlike a successful generation");

    check(s.editor.priorSourceRefusedForTest(),
          "the refused-source flag is published");
    check(! s.editor.statusTextForTest().contains("LLM error"),
          "the status line does NOT read the generic fallback -- a refusal has "
          "a specific, actionable fix, and statusForReason(\"error\") alone "
          "would have said exactly that");
    check(s.editor.statusTextForTest().contains("Redo"),
          "the status line names the actual way out");
    check(s.editor.errorTextForTest().contains("Redo"),
          "the error region still carries generate.py's own guidance text -- "
          "setError() runs unconditionally, same as any other failure");

    // ── Resubmit: PF-021's reset lesson, applied to the new flag ────────────
    s.editor.setRefineModeForTest(1);   // New -- no REFUSE_ME marker, so this
                                        // prompt dispatches to successPayload.
    s.editor.submitPromptForTest("something else entirely");
    check(! s.editor.priorSourceRefusedForTest(),
          "PF-021's lesson applied to the new flag: submitting again clears the "
          "stale refusal IMMEDIATELY, before the new run can even finish -- a "
          "stale refusal must not keep reading as this run's result");
    pumpUntil([&] { return s.editor.statusTextForTest().contains("DSP live"); }, 8000);

    snapshot(s.editor, "26_prior_source_refused");
}

// setCockpitStatePath() (PluginEditor.h) previously had no caller anywhere in
// the repo: cockpitEnabled defaulted false and stayed false forever, so
// dev-cockpit/server.py's /api/state could only ever 503. Armed 2026-08-12 via
// a PLUGINFORGE_COCKPIT_STATE env var read in the constructor. Confirms both
// halves: default OFF when the var is unset (unchanged shipping behaviour),
// and armed + actually writing valid JSON when it is set.
void scenario27_cockpitMirrorEnvVar(const juce::File& tmp)
{
    scenario("27. Dev-cockpit mirror: env-var opt-in, default off",
             "setCockpitStatePath() had zero callers before this session -- "
             "/api/state could only ever 503. The mirror must stay off by "
             "default and actually write when opted in.");

    // Default: no env var set (the ambient test process should not have it).
    {
        Session s;
        check(! s.editor.cockpitEnabledForTest(),
              "default OFF -- PLUGINFORGE_COCKPIT_STATE unset, no write ever "
              "armed, matching the shipping plugin's default behaviour");
        check(s.editor.cockpitStatePathForTest().isEmpty(),
              "no path recorded when never armed");
    }

    // Opt in: set the env var BEFORE constructing the editor -- it is read once,
    // in the constructor, same as PLUGINFORGE_PYTHON in PromptPanel.cpp.
    const auto statePath = tmp.getChildFile("cockpit_state.json");
    statePath.deleteFile();
#if JUCE_WINDOWS
    _putenv_s("PLUGINFORGE_COCKPIT_STATE", statePath.getFullPathName().toRawUTF8());
#else
    setenv("PLUGINFORGE_COCKPIT_STATE", statePath.getFullPathName().toRawUTF8(), 1);
#endif

    {
        Session s;
        check(s.editor.cockpitEnabledForTest(), "armed when the env var is set");
        check(s.editor.cockpitStatePathForTest() == statePath.getFullPathName(),
              "the recorded path is exactly the env var's value");

        // writeCockpitState() fires every 3rd tick (~10Hz at the 30Hz meter
        // timer) -- three direct calls is deterministic, no wall-clock wait.
        s.editor.pumpMeterTickForTest();
        s.editor.pumpMeterTickForTest();
        s.editor.pumpMeterTickForTest();

        check(statePath.existsAsFile(), "the state file was actually written, "
              "not just armed -- the earlier bug was silent: nothing called "
              "setCockpitStatePath, and nothing would have failed loudly either");

        auto parsed = juce::JSON::parse(statePath.loadFileAsString());
        check(parsed.isObject(), "the written file is valid JSON");
        check(parsed.getProperty("windowWidth", juce::var()).isInt(),
              "and has the shape writeCockpitState() actually produces, not "
              "just non-empty bytes");
    }

#if JUCE_WINDOWS
    _putenv_s("PLUGINFORGE_COCKPIT_STATE", "");
#else
    unsetenv("PLUGINFORGE_COCKPIT_STATE");
#endif
    statePath.deleteFile();
}

// Broken #1 (STATUS.md): PluginForgeEditor had no keyStateChanged() override
// at all before this session. KeyboardPanel's on-screen keyboard is a SIBLING
// of the prompt box and every toolbar button, not their ancestor, and JUCE's
// own key-state dispatch walks UP the parent chain from whatever currently
// holds keyboard focus (juce::ComponentPeer::handleKeyUpOrDown,
// juce_ComponentPeer.cpp:224-243) -- so a QWERTY key only ever reached the
// piano's own keyStateChanged() (the method that actually turns a held key
// into a note, juce_MidiKeyboardComponent.cpp:254-283) if the piano itself
// already held keyboard focus, which only happened if the user clicked it
// first. This scenario is a direct unit test of the new override: it proves
// the SHELL now asks the keyboard on every key transition, not that JUCE's
// own dispatch mechanics work (that is JUCE's contract, cited above by
// file:line, not ours to re-test) and not that a real physical keypress
// reaches this call -- see the WHAT THIS DOES NOT PROVE note below.
void scenario28_qwertyRoutingReachesKeyboard()
{
    scenario("28. Computer keyboard reaches the keyboard panel regardless of focus",
             "the piano is a sibling of the prompt box, not an ancestor -- "
             "PluginForgeEditor had no keyStateChanged() override at all, so "
             "JUCE's dispatch never reached the piano unless it was clicked "
             "first (Broken #1)");

    Session s;

    check(s.editor.keyboardRouteCallCountForTest() == 0,
          "nothing has routed yet");

    // Direct unit test of the override, not a simulation of JUCE's dispatch
    // walk (see the class-level comment above for why that walk is JUCE's
    // contract, not ours to re-verify): calling the public override exercises
    // exactly the same body a real dispatch would invoke once it reaches this
    // component, which -- per getTargetForKeyPress() falling back to the
    // top-level component when nothing has explicit focus
    // (juce_ComponentPeer.cpp:164-175) -- is precisely what happens on a
    // fresh window before the user has clicked anything at all.
    s.editor.keyStateChanged(true);
    check(s.editor.keyboardRouteCallCountForTest() == 1,
          "the down-transition is routed to the keyboard panel");
    s.editor.keyStateChanged(false);
    check(s.editor.keyboardRouteCallCountForTest() == 2,
          "the up-transition is routed too -- KeyboardPanel needs both to "
          "track held notes, not just the down edge");

    // WHAT THIS DOES NOT PROVE. Whether a REAL physical keypress reaches this
    // call. juce::KeyPress::isCurrentlyDown() (juce_KeyPress.cpp:59-64) reads
    // actual OS/compositor key state, unfakeable from process code -- no
    // synthetic-input tool exists on this machine (wtype/ydotool/xdotool all
    // absent). That hop is exactly as unverified after this commit as before
    // it; only the shell's OWN routing decision, upstream of that hop, is new
    // and tested here. See STATUS.md's Broken #1 entry for the precise
    // boundary.

    snapshot(s.editor, "28_qwerty_routing");
}

// 29 — Octave [<]/[>] controls shift the QWERTY base octave and the piano's
// visible range together, clamped to the 0-8 octave range
// docs/sessions/010-alpha-ui-architecture.md §4 specifies ("shifts the
// MidiKeyboardComponent range and setKeyPressBaseOctave() by +-1. Range
// clamped 0-8"). Drives KeyboardPanel::shiftOctave() through the editor's
// forwarder -- the SAME method the [<]/[>] buttons' onClick calls
// (KeyboardPanel.h), not a parallel test-only path.
void scenario29_octaveControlsShiftRangeTogether()
{
    scenario("29. Octave controls shift QWERTY base and visible range together, clamped 0-8",
             "KeyboardPanel::shiftOctave(), driven the same way the [<]/[>] "
             "buttons' onClick drives it");

    Session s;

    check(s.editor.keyboardOctaveForTest() == 4, "starts at the construction default, octave 4");
    check(s.editor.keyboardAvailableLowForTest() == 36 && s.editor.keyboardAvailableHighForTest() == 96,
          "starts at the construction default range, 36..96");

    s.editor.shiftKeyboardOctaveForTest(1);
    check(s.editor.keyboardOctaveForTest() == 5, "shiftOctave(+1) advances to octave 5");
    check(s.editor.keyboardAvailableLowForTest() == 48 && s.editor.keyboardAvailableHighForTest() == 108,
          "the visible range shifts by the same +12 semitones, staying in lockstep");

    // Drive to the top clamp (5 more ups: octave 5 -> 10, clamped to 8).
    for (int i = 0; i < 5; ++i)
        s.editor.shiftKeyboardOctaveForTest(1);
    check(s.editor.keyboardOctaveForTest() == 8, "clamps at octave 8, per session 010 section 4's spec");
    check(s.editor.keyboardAvailableLowForTest() == 84 && s.editor.keyboardAvailableHighForTest() == 127,
          "range clamps to MIDI's own ceiling (144 would be invalid) instead of overshooting");
    s.editor.shiftKeyboardOctaveForTest(1);
    check(s.editor.keyboardOctaveForTest() == 8, "one more up-shift past the clamp is a no-op, not an assert");

    // Drive to the bottom clamp (8 downs from octave 8 lands on 0).
    for (int i = 0; i < 8; ++i)
        s.editor.shiftKeyboardOctaveForTest(-1);
    check(s.editor.keyboardOctaveForTest() == 0, "clamps at octave 0");
    check(s.editor.keyboardAvailableLowForTest() == 0 && s.editor.keyboardAvailableHighForTest() == 48,
          "range clamps to MIDI's own floor (-12 would be invalid) instead of undershooting");
    s.editor.shiftKeyboardOctaveForTest(-1);
    check(s.editor.keyboardOctaveForTest() == 0, "one more down-shift past the clamp is a no-op, not an assert");

    snapshot(s.editor, "29_octave_controls");
}

// 30 — Faust compiler stderr reaches the error region untruncated, and the
// offending line is highlighted in the read-only code view (C6).
void scenario30_faustStderrWireAndLineHighlight()
{
    scenario("30. Faust stderr reaches the error region untruncated, offending line highlighted",
             "onFaustCompileFailure used to truncate into the 200-char status "
             "line only; PromptPanel.h's setError() was public and unused by "
             "the shell until this session");

    Session s;

    // Same fixture scenario 5 uses -- "process = this is not faust;" is line
    // 2 (line 1 is the import). Faust's exact error spacing/wording varies by
    // installed version (see parseFaustErrorLine()'s comment in
    // PluginEditor.cpp), so the checks below anchor on what is stable across
    // versions -- "dsp" + a line number, and the literal "ERROR" marker --
    // not the exact punctuation or message text this machine happens to emit.
    s.processor.loadFaustCode("import(\"stdfaust.lib\");\nprocess = this is not faust;",
                              "a broken patch");
    const bool surfaced = pumpUntil([&] {
        return s.editor.errorTextForTest().isNotEmpty();
    });
    check(surfaced, "the compile failure reaches the error region");

    // Faust's real message here happens to be short (~47 chars, well under
    // the 200-char status cap), so length alone cannot distinguish "the wire
    // exists" from "nothing got truncated because nothing was long enough
    // to". Assert the actual relationship instead: the status line is
    // EXACTLY a 200-char-capped prefix of what the error region holds, which
    // only holds regardless of message length if setError() is receiving the
    // same untruncated string setStatus()'s truncation is derived from.
    const auto errorText = s.editor.errorTextForTest();
    check(s.editor.statusTextForTest()
              == "Faust compile error: " + errorText.substring(0, 200),
          "the status line is exactly the 200-char-capped prefix of the full error text");
    // "ERROR" rather than the fuller "unexpected IDENT" wording: CI's
    // installed Faust produces a differently-spaced, shorter message for this
    // same syntax error ("dsp : 2 : ERROR : syntax error" vs this machine's
    // "dsp:2 : ERROR : syntax error, unexpected IDENT") -- discovered when
    // this exact assertion failed in PR #4's build-host run despite passing
    // locally. "ERROR" is Faust's own literal diagnostic marker in both.
    check(errorText.contains("ERROR"),
          "the error region carries the real Faust diagnostic, not a generic "
          "or empty string");
    check(s.editor.codeTextForTest().contains("this is not faust"),
          "the code view now shows the ATTEMPTED source -- PF-022 never gave "
          "this patch a source of record to fall back to");
    check(s.editor.codeHighlightedLineForTest() == 2,
          "the offending line (2, the process line) is highlighted");

    snapshot(s.editor, "30_faust_stderr_wire_and_highlight");
}

// 31 — Up-arrow cycles through prompt history, walking to progressively
// older entries and stopping at the oldest rather than wrapping (C6).
void scenario31_promptHistoryCycles(const juce::File& tmp)
{
    scenario("31. Up-arrow cycles through prompt history, oldest-clamped",
             "PromptPanel::onRecallHistory used to always return "
             "promptHistory[0] -- needed a walking index");

    // A 30s-sleeping fake, same pattern as scenario 9: this test only cares
    // about pushHistory()'s SYNCHRONOUS effect (it runs before the worker
    // thread is even started), never about the subprocess completing, so
    // teardown mid-flight (already proven safe by scenario 9) is fine here.
    FakeGenerator::install(
        FakeGenerator::writeSuccess(tmp, "gen31.sh", FakeGenerator::trivialPatch(), 30));

    Session s;

    s.editor.submitPromptForTest("first prompt");
    s.editor.submitPromptForTest("second prompt");
    s.editor.submitPromptForTest("third prompt");
    check(s.editor.promptHistoryCountForTest() == 3, "three prompts retained");

    // A REAL edit (sendNotification, like actual typing) resets the walking
    // index -- so the very first recall below demonstrably WRITES "third
    // prompt" rather than the box merely still holding it from the last submit.
    // TextEditor::textChanged() POSTS onTextChange as a command message
    // (juce_TextEditor.cpp:1299-1300) rather than firing it synchronously --
    // pump so it has actually landed before the recall that depends on it.
    s.editor.setPromptTextForTest("something the user is typing right now");
    pump(50);

    s.editor.recallPromptHistoryForTest();
    check(s.editor.promptTextForTest() == "third prompt", "first Up recalls the newest entry");

    s.editor.recallPromptHistoryForTest();
    check(s.editor.promptTextForTest() == "second prompt", "second Up walks one entry older");

    s.editor.recallPromptHistoryForTest();
    check(s.editor.promptTextForTest() == "first prompt", "third Up reaches the oldest entry");

    s.editor.recallPromptHistoryForTest();
    check(s.editor.promptTextForTest() == "first prompt",
          "a fourth Up stays at the oldest entry rather than wrapping");

    // A real edit between recalls resets the walk back to the newest entry,
    // not wherever it had reached.
    s.editor.setPromptTextForTest("editing again");
    pump(50);
    s.editor.recallPromptHistoryForTest();
    check(s.editor.promptTextForTest() == "third prompt",
          "an edit between recalls resets the walk to the newest entry");

    snapshot(s.editor, "31_prompt_history_cycles");
}

// 32 — Ctrl+Shift+C toggles the read-only code view (C6), a one-shot chord
// distinct from C4's continuous held-state routing.
void scenario32_ctrlShiftCTogglesCodeView()
{
    scenario("32. Ctrl+Shift+C toggles the code view",
             "PluginForgeEditor::keyPressed(), a DIFFERENT JUCE virtual from "
             "C4's keyStateChanged() -- a one-shot chord, not continuous "
             "held-state polling");

    Session s;
    check(! s.editor.codeVisibleForTest(), "starts hidden, same default as scenario 11");

    const juce::KeyPress chord('c',
        juce::ModifierKeys::ctrlModifier | juce::ModifierKeys::shiftModifier, 0);

    check(s.editor.keyPressed(chord), "the chord is consumed (returns true)");
    check(s.editor.codeVisibleForTest(), "first press shows the code view");

    check(s.editor.keyPressed(chord), "the chord is consumed again");
    check(! s.editor.codeVisibleForTest(), "second press hides it again");

    // A close cousin that must NOT trigger it: plain Ctrl+C (copy).
    // TextEditorKeyMapper matches 'c' against an EXACT modifier set
    // (juce_TextEditorKeyMapper.h:90) -- commandModifier alone, never
    // commandModifier|shiftModifier -- so this is a genuinely different
    // KeyPress, not a subset match.
    const juce::KeyPress plainCtrlC('c', juce::ModifierKeys::ctrlModifier, 0);
    check(! s.editor.keyPressed(plainCtrlC),
          "plain Ctrl+C is not consumed by this override -- it is not the toggle chord");
    check(! s.editor.codeVisibleForTest(), "...and does not toggle the code view");

    snapshot(s.editor, "32_ctrl_shift_c_toggle");
}

// 33 — "Generate a synth, then play a note": the end-to-end path NOTHING
// covered before this scenario.
//
// Every prior keyboard scenario (17-21, 28-29) drives the keyboard against a
// patch loaded DIRECTLY via processor.loadFaustCode() or
// noteOnForTest()/noteOffForTest(), never through the real prompt->subprocess
// ->compile pipeline. Every FakeGenerator scenario that DOES go through that
// pipeline (1, 4-5, 8-9, 16, 23-27, 31) generates an EFFECT patch and never
// touches the keyboard afterwards. This is the first scenario to drive both
// halves in one run, and it is a real regression guard on the WIRING between
// "a generation just completed" and "the keyboard responds" -- PluginEditor's
// 30Hz timer calling keyboardPanel.setPlayable(isInstrumentForTest()) after
// onFaustCompileSuccess, and KeyboardPanel forwarding into the SAME NoteRing
// path a direct loadFaustCode() call would use.
//
// BE HONEST ABOUT WHAT THIS DOES NOT CATCH, checked directly this session
// rather than assumed:
//   - NOT the setPlayable(false) constructor no-op (scenario 20's job).
//     Verified by temporarily reintroducing that bug: this scenario stayed
//     GREEN. The reason is structural, not an oversight -- a successful synth
//     generation drives a genuine playable:false->true transition through
//     the 30Hz timer, which is NOT idempotent (setPlayable's `canPlay ==
//     playable` guard only no-ops a call that changes nothing), so it applies
//     the widgets correctly regardless of whether the CONSTRUCTOR'S initial
//     application was ever a no-op. The constructor bug only matters for
//     states that stay false throughout (a fresh editor, an effect patch) --
//     exactly scenario 20's two cases, not this one's.
//   - NOT the "generator" family's silent unplayable-synth routing trap, and
//     NOT the generate.py/FaustEngine label-casing mismatch. Both live in the
//     REAL llm/generate.py (generation_profiles.py's family resolution and
//     _ui_labels()'s lowercasing, respectively) -- FakeGenerator substitutes
//     a canned response for the whole subprocess, so neither ever runs here.
//     Those need Python-level tests, added alongside their own fixes.
// What this scenario actually is: the previously-absent "does the wiring
// work end to end for a well-formed patch" position -- necessary, and worth
// having, but not a substitute for targeted regression tests on each of the
// three bugs it was written alongside.
void scenario33_generateThenPlay(const juce::File& tmp)
{
    scenario("33. canned generation bridge compiles, then plays a note",
             "canned subprocess response -> compile -> editor state -> NoteRing -> audio");

    FakeGenerator::install(
        FakeGenerator::writeSuccess(tmp, "gen33_synth.sh", kGatedSawSynthPatch));

    Session s;
    check(! s.editor.keyboardPlayableForTest(),
          "before generation: the keyboard starts unplayable (no patch loaded yet)");

    s.editor.submitPromptForTest("a plucky saw synth");
    const bool live = pumpUntil([&] {
        return s.editor.statusTextForTest().contains("DSP live")
            && s.processor.isDspReadyForTest();
    });
    check(live, "the canned generation bridge reached 'DSP live' and published a ready DSP");

    s.editor.pumpMeterTickForTest();   // the 30Hz poll that enables the keyboard
    check(s.editor.keyboardPlayableForTest(),
          "after generation: the keyboard reports playable for a real voice contract");
    check(s.editor.keyboardEnabledForTest(),
          "after generation: the keyboard WIDGET is enabled, not just the flag");
    check(s.editor.keyboardAlphaForTest() >= 1.0f,
          "after generation: the keyboard is full-alpha, not dimmed");
    check(! s.editor.keyboardDisabledLabelVisibleForTest(),
          "after generation: \"Load an instrument to play\" is gone");

    render(s.processor, 10);
    check(s.processor.outputLevel.load(std::memory_order_relaxed) < 0.05f,
          "silent with no note held");

    s.editor.keyboardNoteOnForTest(69, 1.0f);
    render(s.processor, 40);
    const float held = s.processor.outputLevel.load(std::memory_order_relaxed);
    check(held > 0.02f,
          juce::String("a note played after generation actually SOUNDS (peak ")
              + juce::String(held, 5) + ")");
    check(s.processor.droppedKeyboardNotes() == 0, "no keyboard event was dropped");

    s.editor.keyboardNoteOffForTest(69);
    render(s.processor, 60);   // past en.adsr's 0.2s release
    const float tail = s.processor.outputLevel.load(std::memory_order_relaxed);
    check(tail < held * 0.1f,
          juce::String("the note releases (tail ") + juce::String(tail, 6)
              + " vs held " + juce::String(held, 5) + ")");

    snapshot(s.editor, "33_generate_then_play");
}

// 34 — QWERTY-after-generation: symmetric key forwarding and immediate
// playability, the two remaining halves of the reported "keyboard is not
// playable after generating a synth" complaint that scenario 33 does not
// cover (33 proves the wiring works once the keyboard already has focus;
// this proves focus reaches it in the first place, and that a focused
// prompt box cannot leak spurious key-UP events to it while typing).
void scenario34_qwertyAfterGeneration(const juce::File& tmp)
{
    scenario("34. QWERTY works immediately after generation, and typing cannot leak spurious notes",
             "the two remaining halves of the keyboard-after-generation complaint: "
             "focus must move to the piano without an extra click, and a "
             "focused prompt box must not leak key-UP events (JUCE's "
             "TextEditor::keyStateChanged swallows key-down but not key-up)");

    // ── Part A: the suppression predicate itself ────────────────────────────
    check(! PluginForgeEditor::isTextEditorFocusTarget(nullptr),
          "nothing focused: not suppressed");
    {
        juce::TextEditor te;
        check(PluginForgeEditor::isTextEditorFocusTarget(&te),
              "a TextEditor focused: suppressed");
    }
    {
        juce::TextButton btn;
        check(! PluginForgeEditor::isTextEditorFocusTarget(&btn),
              "a non-TextEditor focused (e.g. a button): not suppressed");
    }

    // ── Part B: focusForPlaying() fires for an instrument, not an effect ───
    FakeGenerator::install(
        FakeGenerator::writeSuccess(tmp, "gen34_synth.sh", kGatedSawSynthPatch));
    Session synthSession;
    check(synthSession.editor.keyboardFocusForPlayingCallCountForTest() == 0,
          "nothing generated yet: focusForPlaying has not fired");
    // Expect a benign "JUCE Assertion failure in juce_Component.cpp"
    // (grabKeyboardFocus()'s own Debug-only "only be focused when it's
    // actually on the screen" check) on stderr here -- this harness has no
    // peer, so it fires every time, matches this file's other documented
    // debug-only assertions (SoundfetchClientTest's execvp-failure case is
    // the same shape), and is not a test failure; the checks below still
    // confirm the CALL happened even though the real focus grant no-ops.
    synthSession.editor.submitPromptForTest("a plucky saw synth");
    const bool synthLive = pumpUntil([&] {
        return synthSession.editor.statusTextForTest().contains("DSP live");
    });
    check(synthLive, "the synth generation reached DSP live");
    check(synthSession.editor.keyboardFocusForPlayingCallCountForTest() == 1,
          "an instrument generation calls focusForPlaying exactly once");
    // The synchronous setPlayable() call in onFaustCompileSuccess, NOT the
    // 30Hz timer -- pumpMeterTickForTest() is deliberately NOT called before
    // this check, unlike scenario 33. If focusForPlaying() ran before
    // setPlayable(true) had taken effect, it would try to focus a still-
    // disabled widget (JUCE never grants focus to one) -- the same ordering
    // hazard scenario 20's constructor no-op showed matters.
    check(synthSession.editor.keyboardPlayableForTest(),
          "the keyboard is already playable immediately after DSP live, "
          "with no timer tick pumped first");
    check(synthSession.editor.keyboardEnabledForTest(),
          "...and the WIDGET is already enabled too, not just the flag");

    FakeGenerator::install(
        FakeGenerator::writeSuccess(tmp, "gen34_effect.sh", kTinyPatch));
    Session effectSession;
    effectSession.editor.submitPromptForTest("a simple gain stage");
    const bool effectLive = pumpUntil([&] {
        return effectSession.editor.statusTextForTest().contains("DSP live");
    });
    check(effectLive, "the effect generation reached DSP live");
    check(effectSession.editor.keyboardFocusForPlayingCallCountForTest() == 0,
          "an effect generation never calls focusForPlaying -- nothing to play");

    snapshot(synthSession.editor, "34_qwerty_after_generation");
}

// 35 — ADR-022 Track 1.2: a heuristic sectioned layout, derived purely from
//      Faust group nesting already present in the compiled patch. Two things
//      this pins: canonical ordering (NOT Faust's alphabetical group order,
//      which reads Env before Osc -- wrong for how a musician scans a synth
//      panel), and the suppression threshold (a sparse or single-group patch
//      must render the ordinary flat grid, not a heading over a few knobs).
void scenario35_sectionedLayoutOrdersAndSuppresses()
{
    scenario("35. sectioned layout: canonical group order, suppressed when sparse",
             "deriveLayoutFromGroups ranks Osc before Filter before Env before Fx "
             "-- the reverse of Faust's own alphabetical report -- and returns no "
             "sections at all for <=1 non-empty group or <4 controls");

    // Four groups, one control each, declared (and therefore Faust-reported)
    // in ALPHABETICAL order: Env, Filter, Fx, Osc -- the exact case F4 names.
    const char* kFourGroupPatch = R"(import("stdfaust.lib");
oscFreq      = vgroup("Osc",    hslider("Freq",   0.5, 0, 1, 0.01));
filterCutoff = vgroup("Filter", hslider("Cutoff", 0.5, 0, 1, 0.01));
envAttack    = vgroup("Env",    hslider("Attack", 0.5, 0, 1, 0.01));
fxMix        = vgroup("Fx",     hslider("Mix",    0.5, 0, 1, 0.01));
amt = (oscFreq + filterCutoff + envAttack + fxMix) * 0.25;
process = _ * amt, _ * amt;
)";

    Session s;
    check(loadAndSettle(s, kFourGroupPatch, 4), "the 4-group patch compiled");

    const auto& sections = s.editor.gridActiveSectionsForTest();
    check(sections.size() == 4,
          juce::String("expected 4 sections, got ") + juce::String((int) sections.size()));
    if (sections.size() == 4)
    {
        const char* expected[] = { "Osc", "Filter", "Env", "Fx" };
        for (int i = 0; i < 4; ++i)
            check(juce::String(sections[(size_t) i].title) == expected[i],
                  juce::String("section ") + juce::String(i) + " expected '" + expected[i]
                      + "', got '" + juce::String(sections[(size_t) i].title) + "'");
    }

    // Threshold guard #1: kFourParamPatch (defined above, §2 in this file) has
    // FOUR controls but ZERO groups -- nonEmptyGroups == 0, under the <=1 cutoff.
    // Must render flat, same as before this track existed.
    check(loadAndSettle(s, kFourParamPatch, 4), "the ungrouped 4-param patch compiled");
    check(s.editor.gridActiveSectionsForTest().empty(),
          "an ungrouped patch gets no sections -- the flat grid is unchanged");

    // Threshold guard #2: a single group wrapping the WHOLE patch. One heading
    // over everything reads worse than no heading at all.
    const char* kOneGroupPatch = R"(import("stdfaust.lib");
a = vgroup("Everything", hslider("A", 0.5, 0, 1, 0.01) + hslider("B", 0.5, 0, 1, 0.01)
                        + hslider("C", 0.5, 0, 1, 0.01) + hslider("D", 0.5, 0, 1, 0.01));
process = _ * a * 0.25, _ * a * 0.25;
)";
    check(loadAndSettle(s, kOneGroupPatch, 4), "the single-group 4-param patch compiled");
    check(s.editor.gridActiveSectionsForTest().empty(),
          "one group wrapping the whole patch also suppresses to the flat grid");
}

// 36 — F3: a real use-after-free, not a hypothetical. refreshParamKnobs()
//      cleared `controls` without clearing `activeSections`/`irLookup`, and
//      layoutControls() branches on `activeSections` BEFORE applyUiIr() gets a
//      chance to rebuild either of them for the new controls -- so a SECOND
//      compile over a sectioned patch took the sectioned branch and
//      dereferenced Control* pointers into the vector controls.clear() had
//      just invalidated. Confirmed live under this binary's own ASAN build
//      (EditorSessionTest links -fsanitize=address,undefined, host/CMakeLists.
//      txt:649-650): reverting just the two clear() calls this scenario exists
//      to pin reproduces a heap-use-after-free abort here.
void scenario36_recompileOverSectionedPatchDoesNotUseAfterFree()
{
    scenario("36. a second compile over a sectioned patch does not use-after-free",
             "activeSections/irLookup must be cleared before layoutControls() runs "
             "inside refreshParamKnobs, or the sectioned branch dereferences "
             "Control* pointers the same call just freed");

    const char* kTwoGroupPatch = R"(import("stdfaust.lib");
osc  = vgroup("Osc",    hslider("Freq", 0.5, 0, 1, 0.01) + hslider("Shape",  0.5, 0, 1, 0.01));
filt = vgroup("Filter", hslider("Q",    0.5, 0, 1, 0.01) + hslider("Cutoff", 0.5, 0, 1, 0.01));
amt = (osc + filt) * 0.25;
process = _ * amt, _ * amt;
)";

    // Twelve controls, three groups, so the recompile below shrinks the
    // occupied range rather than growing it. `controls` (std::vector<Control>)
    // is `.reserve(POOL_SIZE)`d -- POOL_SIZE, unconditionally -- on EVERY
    // refreshParamKnobs call (params here is ParamPool's 64-slot per-slot
    // view, whose size() is always 64), so its capacity reaches 64 on the
    // FIRST compile of any Session and never reallocates again: a GROWING
    // recompile (confirmed: 4 -> 20 controls) never frees the old buffer, it
    // just placement-constructs new Controls over the same addresses --
    // stale irLookup pointers end up pointing at live, freshly (if WRONG)
    // constructed objects, not freed ones, so nothing crashes. A SHRINKING
    // recompile is what exposes it: indices the smaller patch never reaches
    // stay destructed-but-unconstructed, and the WIDGETS those old Controls
    // owned (the juce::Label/Slider each unique_ptr member pointed at) were
    // already deleted when clear() ran their destructors -- so the stale
    // irLookup pointer for a control at, say, index 9 dereferences a Control
    // whose `label` member still holds the pointer value it had before
    // destruction, now pointing at an ACTUALLY-FREED juce::Label.
    const char* kBigSectionedPatch = R"(import("stdfaust.lib");
osc  = vgroup("Osc",    hslider("A1",0.5,0,1,0.01)+hslider("A2",0.5,0,1,0.01)
                       + hslider("A3",0.5,0,1,0.01)+hslider("A4",0.5,0,1,0.01));
filt = vgroup("Filter", hslider("B1",0.5,0,1,0.01)+hslider("B2",0.5,0,1,0.01)
                       + hslider("B3",0.5,0,1,0.01)+hslider("B4",0.5,0,1,0.01));
env  = vgroup("Env",    hslider("C1",0.5,0,1,0.01)+hslider("C2",0.5,0,1,0.01)
                       + hslider("C3",0.5,0,1,0.01)+hslider("C4",0.5,0,1,0.01));
amt = (osc + filt + env) / 12.0;
process = _ * amt, _ * amt;
)";

    Session s;
    check(loadAndSettle(s, kBigSectionedPatch, 12), "the 12-control sectioned patch compiled");
    check(s.editor.gridActiveSectionsForTest().size() == 3,
          "sanity: it actually sectioned before the recompile under test");

    // The recompile under test. Under ASAN, a use-after-free aborts the whole
    // process right here -- there is no exception to catch; a crash IS the
    // failure mode, and simply reaching the check below is most of what this
    // scenario is pinning.
    check(loadAndSettle(s, kFourParamPatch, 4),
          "a much SMALLER, ungrouped patch compiled over the sectioned one "
          "without crashing");
    check(s.editor.gridActiveSectionsForTest().empty(),
          "and correctly settled on the flat grid, not stale sections");

    // One more round trip, sectioned again, to confirm the panel is not merely
    // un-crashed but still functional after the clear.
    check(loadAndSettle(s, kTwoGroupPatch, 4), "re-compiling a (differently) sectioned patch works");
    check(s.editor.gridActiveSectionsForTest().size() == 2,
          "sections rebuild correctly on the third compile");
}

// 37 — T3.4: a juce::TooltipWindow now exists. There was ZERO instances of
//      this class anywhere in host/ before this track, so PromptPanel's two
//      pre-existing setTooltip() calls (the family selector, and the Add/Redo
//      mode explainer -- the only in-app explanation of those two modes) were
//      dead code that could never render. TooltipWindow::getTipFor() itself
//      is not testable headlessly (it gates on WindowingHelpers::
//      isForegroundOrEmbeddedProcess(), juce_TooltipWindow.cpp:154 -- same
//      category of limitation as a real keypress, STATUS.md Broken #1). What
//      IS deterministic: the constructor's parentComp->addChildComponent(this)
//      when given a non-null parent (juce_TooltipWindow.cpp:37-38).
void scenario37_tooltipWindowExists()
{
    scenario("37. a TooltipWindow exists and is parented to the editor",
             "there was no juce::TooltipWindow anywhere in host/ before this -- "
             "PromptPanel's setTooltip() calls for the family selector and the "
             "Add/Redo explainer could never render");

    Session s;
    check(s.editor.tooltipWindowParentedForTest(),
          "PluginForgeEditor owns a TooltipWindow parented to itself, per "
          "juce_TooltipWindow.h's own documented plugin pattern");
}

// 38 — T5: the sample-browser status line's long-message problem. The
//      soundfetch-missing message alone runs to ~230 characters, routed into
//      a single-line 20px Label with no room to grow (SampleBrowserPanel.h's
//      comment on setStatusText has the full layout-budget argument). JUCE
//      already ellipsizes the overflowing VISIBLE text; what was missing was
//      any way to read the rest. Pins that setStatusText's tooltip always
//      carries the SAME string as the (possibly truncated) label text, for
//      both the default status and a long one that would actually ellipsize.
void scenario38_sampleBrowserStatusTooltipMatchesText()
{
    scenario("38. sample-browser status text is always also its own tooltip",
             "the soundfetch-missing message alone runs ~230 chars into a "
             "single-line 20px Label with no room to grow -- the tooltip is "
             "how the untruncated string stays reachable");

    Session s;
    // Default text, set in the constructor -- proves the wiring without
    // needing a real soundfetch round trip.
    const auto defaultText = s.editor.sampleBrowserStatusTextForTest();
    check(defaultText.isNotEmpty(), "the panel starts with a status message");
    check(s.editor.sampleBrowserStatusTooltipForTest() == defaultText,
          juce::String("tooltip must equal the status text -- text='") + defaultText
              + "' tooltip='" + s.editor.sampleBrowserStatusTooltipForTest() + "'");
}

// 39 — T5: the code view's copy button. Before this, the only way to get the
//      generated Faust out of the app was selecting it by hand -- workable
//      for a short patch, tedious for a 40-param one, and this IS the export
//      path for the BYO-LLM flow the panel's own header comment names (source
//      + compiler stderr pasted into someone else's model). Pins the
//      button-state contract headlessly (starts "Copy", becomes "Copied!" on
//      click) and, opportunistically, the real system clipboard -- this
//      machine's Wayland/Hyprland session and CI's xvfb both provide X11
//      clipboard selections JUCE's Linux backend uses, so it is checked
//      directly rather than assumed unreachable.
void scenario39_codeViewCopyButton()
{
    scenario("39. the code view's Copy button copies the real source, once",
             "before this, the only way out of a read-only view was selecting "
             "text by hand -- the BYO-LLM flow needs the export to be one click");

    Session s;
    s.editor.setCodeVisibleForTest(true);
    pump(50);
    check(loadAndSettle(s, kEveryKindPatch, 5), "a 5-param patch compiled");
    pump(200);

    check(s.editor.codeCopyButtonTextForTest() == "Copy",
          "the button starts in its resting state");

    const auto shownSource = s.editor.codeTextForTest();
    s.editor.clickCodeCopyButtonForTest();
    // Button::triggerClick() posts an async command message
    // (juce_Button.cpp:352) rather than calling onClick synchronously -- the
    // message loop has to run once before the click has actually happened.
    pump(50);
    check(s.editor.codeCopyButtonTextForTest() == "Copied!",
          "clicking gives an immediate, transient confirmation");
    check(juce::SystemClipboard::getTextFromClipboard() == shownSource,
          "the REAL system clipboard now holds exactly what was on screen, "
          "not a stale or partial copy");
    snapshot(s.editor, "37_code_view_copy_button");

    // No wait for the 900ms confirmation timer here. It used to be
    // juce::Timer::callAfterDelay, whose pending closure lives in JUCE's own
    // global timer queue independent of any component's lifetime -- ASAN's
    // leak check (which runs at process exit) saw that as leaked heap unless
    // a test waited out the full delay first. CodeEditorPanel now owns a
    // member Timer instead: ~Timer() calls stopTimer() unconditionally on
    // destruction, so Session's own teardown at the end of this function
    // cancels it immediately, regardless of how much of the 900ms has
    // elapsed. Nothing to wait for.
}

} // namespace

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    std::printf("EditorSessionTest -- a simulated human session against the real editor\n");
    std::printf("  39 scenarios, no network, no quota, snapshots to artifacts/images/\n");

    auto tmp = juce::File::getSpecialLocation(juce::File::tempDirectory)
                   .getChildFile("pluginforge_editor_session");
    tmp.deleteRecursively();
    tmp.createDirectory();

    scenario01_happyPath(tmp);
    scenario02_widgetKinds();
    scenario03_overflow();
    scenario04_errorSurfacing(tmp);
    scenario05_faustCompileFailure(tmp);
    scenario06_freshResetsKnobs();
    scenario07_outputGuardMute();
    scenario08_rapidFire(tmp);
    scenario09_teardownMidFlight(tmp);
    scenario10_stateRoundTrip();
    scenario11_codeView();
    scenario12_readout();
    scenario13_styleSwitchDoesNotThrash();
    scenario14_groupCapture();
    scenario15_identityKeyedRetention();
    scenario16_refineCarriesTheSource(tmp);
    scenario17_keyboardNoteRoundTrip();
    scenario18_keyboardLastNotePriority();
    scenario19_keyboardSurvivesCompileSwap();
    scenario20_keyboardDisabledForEffect();
    scenario21_allNotesOffReleasesHeldNoteOnDisable();
    scenario22_qwertyMappingLiveContract();
    scenario23_kindSelectorReachesRequest(tmp);
    scenario24_priorSourceDroppedSurfaced(tmp);
    scenario25_refineModesUnlock(tmp);
    scenario26_priorSourceRefused(tmp);
    scenario27_cockpitMirrorEnvVar(tmp);
    scenario28_qwertyRoutingReachesKeyboard();
    scenario29_octaveControlsShiftRangeTogether();
    scenario30_faustStderrWireAndLineHighlight();
    scenario31_promptHistoryCycles(tmp);
    scenario32_ctrlShiftCTogglesCodeView();
    scenario33_generateThenPlay(tmp);
    scenario34_qwertyAfterGeneration(tmp);
    scenario35_sectionedLayoutOrdersAndSuppresses();
    scenario36_recompileOverSectionedPatchDoesNotUseAfterFree();
    scenario37_tooltipWindowExists();
    scenario38_sampleBrowserStatusTooltipMatchesText();
    scenario39_codeViewCopyButton();

    tmp.deleteRecursively();

    std::printf("\n%s (%d checks, %d failures)\n",
                failures == 0 ? "PASS" : "FAIL", checks, failures);
    // One machine-readable line for tools/health_report.py. The `checks` total is
    // the point: every harness here already carried an exact `failures` int and
    // threw the denominator away, so "0 failures" was indistinguishable from
    // "ran nothing". Format is fixed and shared by all seven harnesses.
    std::printf("PF_SUMMARY harness=%s checks=%d failures=%d\n", "EditorSessionTest", checks, failures);
    return failures == 0 ? 0 : 1;
}
