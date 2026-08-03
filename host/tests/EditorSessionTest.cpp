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
        editor.setSize(480, 460);
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
    check(h > 460, juce::String("window grew past its default for 40 params (now ")
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

} // namespace

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    std::printf("EditorSessionTest -- a simulated human session against the real editor\n");
    std::printf("  12 scenarios, no network, no quota, snapshots to artifacts/images/\n");

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
