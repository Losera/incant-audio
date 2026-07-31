// ── UiDesignGallery ─────────────────────────────────────────────────────────
// A design instrument, not a test. It renders the REAL editor against a matrix
// of real Faust patches and writes one PNG plus one machine-readable record per
// fixture, so a layout change can be looked at and diffed instead of argued
// about. docs/ui_design_plan.md §3 (the "AI-centered design loop") is the thing
// it exists to serve: §3's B (LLM layout hints) and C (deterministic auto-layout)
// both change what the grid looks like, and until now nothing in this repo could
// show that difference.
//
// WHY THIS IS NOT A TEST, AND MUST NOT BECOME ONE.
// EditorSessionTest asserts; this reports. A gallery that fails the build the
// first time a designer moves a control by 4px is a gallery nobody runs, and
// this project's recurring defect is controls that are believed to run and do
// not — a check that gets disabled is strictly worse than one that never
// existed. So: exit 0 unless the harness itself broke (a fixture that does not
// compile, a grid that never settles). Drift is REPORTED, by tools/ui_iterate.sh
// diffing manifest.json against the committed reference.
//
// It prints no PF_SUMMARY line, deliberately. tools/health_report.py counts
// assertions, and this harness makes none; emitting a summary of zero checks
// would land in the health report as a lane that passed, which is exactly the
// silent-zero-reads-as-health failure that file warns about.
//
// HEADLESS. Component::createComponentSnapshot renders through the software
// renderer into an Image with no peer and no compositor
// (juce_Component.h:1959) — the same mechanism EditorSessionTest's snapshots
// use. The message loop still needs a display, for the reason
// host/CMakeLists.txt:335-338 gives about PromptPanelThreadingTest; run it under
// xvfb-run where there is no session.
//
// Usage:  UiDesignGallery [fixtureDir] [outDir]
//   defaults: host/tests/ui_fixtures, artifacts/ui_gallery  (relative to CWD)

#include "../Source/PluginProcessor.h"
#include "../Source/PluginEditor.h"
#include "../Source/ParamGridPanel.h"
#include "../Source/ParamGridLayout.h"

#include <cstdio>
#include <functional>

namespace
{

// ── Message-loop pumping ────────────────────────────────────────────────────
// Lifted verbatim in spirit from EditorSessionTest: assert on the RESULT, never
// sleep a guessed interval, so a slow JIT makes the run slower and never flaky.
void pump(int ms)
{
    juce::MessageManager::getInstance()->runDispatchLoopUntil(ms);
}

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

// ── Fixture session ─────────────────────────────────────────────────────────
// Declaration order is the contract: the processor is declared FIRST so it is
// destroyed LAST, because PromptPanel holds a raw PluginForgeProcessor& and
// joins its worker in ~PromptPanel (PromptPanel.h:63-68).
struct Session
{
    PluginForgeProcessor processor;
    PluginForgeEditor    editor { processor };

    Session()
    {
        processor.prepareToPlay(48000.0, 512);
        // Without a size the grid has zero bounds and every widget lands at 0x0:
        // constructed, but visually meaningless, and the snapshot comes out blank.
        editor.setSize(480, 460);
    }
};

juce::String jsonEscape(const juce::String& s)
{
    juce::String out;
    for (auto c : s)
    {
        if      (c == '"')  out << "\\\"";
        else if (c == '\\') out << "\\\\";
        else if (c == '\n') out << "\\n";
        else if (c == '\t') out << "\\t";
        else                out << juce::String::charToString(c);
    }
    return out;
}

bool writeSnapshot(juce::Component& c, const juce::File& out, int& wOut, int& hOut)
{
    auto img = c.createComponentSnapshot(c.getLocalBounds(), true);
    if (! img.isValid())
        return false;

    wOut = img.getWidth();
    hOut = img.getHeight();

    out.deleteFile();
    juce::FileOutputStream stream(out);
    if (! stream.openedOk())
        return false;

    juce::PNGImageFormat png;
    return png.writeImageToStream(img, stream);
}

// One fixture's worth of what the screen actually shows.
struct Record
{
    juce::String name;
    juce::String png;
    bool         settled = false;
    int          declaredParams = 0;   // what Faust reported
    int          controls = 0;         // what the grid built
    int          windowW = 0, windowH = 0;
    int          cols = 0, rows = 0;
    juce::StringArray labels, kinds, texts, groups;
};

juce::String recordToJson(const Record& r)
{
    juce::String j;
    j << "    {\n";
    j << "      \"name\": \"" << jsonEscape(r.name) << "\",\n";
    j << "      \"png\": \"" << jsonEscape(r.png) << "\",\n";
    j << "      \"settled\": " << (r.settled ? "true" : "false") << ",\n";
    j << "      \"declared_params\": " << r.declaredParams << ",\n";
    j << "      \"controls\": " << r.controls << ",\n";
    j << "      \"window\": [" << r.windowW << ", " << r.windowH << "],\n";
    j << "      \"grid\": [" << r.cols << ", " << r.rows << "],\n";

    auto arr = [&j](const char* key, const juce::StringArray& a, bool last)
    {
        j << "      \"" << key << "\": [";
        for (int i = 0; i < a.size(); ++i)
            j << (i ? ", " : "") << "\"" << jsonEscape(a[i]) << "\"";
        j << "]" << (last ? "\n" : ",\n");
    };
    arr("labels", r.labels, false);
    arr("kinds",  r.kinds,  false);
    arr("texts",  r.texts,  false);
    arr("groups", r.groups, true);

    j << "    }";
    return j;
}

// Renders one fixture. Returns false only on a HARNESS failure — a patch that
// will not compile or a grid that never settles — never on a layout the author
// happens to dislike.
bool renderFixture(const juce::File& dsp, const juce::File& outDir, Record& rec,
                   const juce::String& styleName = "faithful")
{
    rec.name = dsp.getFileNameWithoutExtension()
             + (styleName == "faithful" ? juce::String() : "__" + styleName);
    rec.png  = rec.name + ".png";

    const auto source = dsp.loadFileAsString();
    if (source.isEmpty())
    {
        std::printf("  %-24s SKIP (empty or unreadable)\n", rec.name.toRawUTF8());
        return false;
    }

    Session s;
    // Set BEFORE the compile, the way a reopened project does it, so this also
    // exercises the path where a style is already stored when widgets are built.
    s.processor.setUiStyle(styleName);
    const int before = s.editor.gridRefreshCountForTest();
    s.processor.loadFaustCode(source, "ui design gallery: " + rec.name,
                              PluginForgeProcessor::LoadMode::Fresh);

    // Wait for the EDITOR, not the compile. The compile callback fires on the
    // compile thread and hops to the message thread, so waiting on the compile
    // alone races the grid rebuild this is about to photograph. The refresh
    // counter advances once per refreshParamKnobs, on the message thread, after
    // the widgets exist — see EditorSessionTest.cpp:225-247 for the two ways
    // this was got wrong before.
    rec.settled = pumpUntil([&] {
        return s.processor.currentSourceForTest() == source
            && s.editor.gridRefreshCountForTest() > before
            && s.editor.gridControlCountForTest() > 0;
    });

    if (! rec.settled)
    {
        std::printf("  %-24s FAIL (never settled; refresh %d -> %d, %d controls)\n",
                    rec.name.toRawUTF8(), before,
                    s.editor.gridRefreshCountForTest(),
                    s.editor.gridControlCountForTest());
        return false;
    }

    // What Faust declared, vs what the grid actually built. This field was
    // declared and serialized from the start but never assigned, so every
    // manifest reported declared_params: 0 and the baseline froze that zero in.
    // The delta is the entire point of 03_effect_grouped, and it is this
    // project's signature defect class -- a control the DSP declares and the UI
    // never shows is invisible to `controls` alone, because `controls` only ever
    // counts what got built.
    rec.declaredParams = s.processor.currentLabelsForTest().size();
    rec.controls = s.editor.gridControlCountForTest();
    rec.windowW  = s.editor.getWidth();
    rec.windowH  = s.editor.getHeight();
    rec.cols     = ParamGridLayout::columnsFor(rec.controls);
    rec.rows     = ParamGridLayout::rowsFor(rec.controls);

    for (int i = 0; i < rec.controls; ++i)
    {
        rec.labels.add(s.editor.gridControlLabelForTest(i));
        rec.kinds.add(ParamGridPanel::widgetKindName(s.editor.gridControlKindForTest(i)));
        rec.texts.add(s.editor.gridControlTextForTest(i));
        rec.groups.add(s.editor.gridControlGroupForTest(i));
    }

    // Group structure is the input demo Variant C needs, shown so the loop tells
    // you at a glance whether a patch's sections survived capture. COMPUTED here,
    // PRINTED below the header -- printing it at this point put it on stdout
    // before its own record's header line (which cannot be printed until the
    // snapshot has supplied w/h), so every group line visually attached to the
    // PREVIOUS record. Read literally, the loop reported 03__horizontal as having
    // 04's sections and 04__horizontal as having none, while manifest.json -- same
    // rec, same run -- had all three styles correct. A design instrument that
    // misattributes its own evidence is worse than one that omits it.
    juce::StringArray distinct;
    for (const auto& g : rec.groups)
        if (g.isNotEmpty() && ! distinct.contains(g))
            distinct.add(g);

    int w = 0, h = 0;
    const auto png = outDir.getChildFile(rec.png);
    if (! writeSnapshot(s.editor, png, w, h))
    {
        std::printf("  %-24s FAIL (snapshot could not be written)\n", rec.name.toRawUTF8());
        return false;
    }

    std::printf("  %-24s %2d controls  %dx%d px  grid %dx%d  -> %s\n",
                rec.name.toRawUTF8(), rec.controls, w, h, rec.cols, rec.rows,
                rec.png.toRawUTF8());
    std::printf("      order: %s\n", rec.labels.joinIntoString(", ").toRawUTF8());
    std::printf("      groups: %s\n",
                distinct.isEmpty() ? "(none — flat)"
                                   : distinct.joinIntoString(", ").toRawUTF8());
    return true;
}

} // namespace

int main(int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    const auto cwd = juce::File::getCurrentWorkingDirectory();
    const auto fixtureDir = argc > 1 ? juce::File(argv[1])
                                     : cwd.getChildFile("host/tests/ui_fixtures");
    const auto outDir     = argc > 2 ? juce::File(argv[2])
                                     : cwd.getChildFile("artifacts/ui_gallery");

    if (! fixtureDir.isDirectory())
    {
        std::printf("UiDesignGallery: no fixture directory at %s\n",
                    fixtureDir.getFullPathName().toRawUTF8());
        return 2;
    }
    outDir.createDirectory();

    auto fixtures = fixtureDir.findChildFiles(juce::File::findFiles, false, "*.dsp");
    fixtures.sort();

    if (fixtures.isEmpty())
    {
        std::printf("UiDesignGallery: no .dsp fixtures in %s\n",
                    fixtureDir.getFullPathName().toRawUTF8());
        return 2;
    }

    std::printf("UiDesignGallery — %d fixtures from %s\n",
                fixtures.size(), fixtureDir.getFullPathName().toRawUTF8());
    std::printf("  out: %s\n\n", outDir.getFullPathName().toRawUTF8());

    juce::Array<Record> records;
    int broken = 0;

    // Every fixture in every style. The style axis is the point: a layout change
    // is only judgeable next to the alternatives it is competing with.
    const char* kStyles[] = { "faithful", "rotary", "horizontal" };
    for (const auto& f : fixtures)
        for (const auto* st : kStyles)
        {
            Record r;
            if (! renderFixture(f, outDir, r, st))
                ++broken;
            records.add(r);
        }

    // ── manifest.json ───────────────────────────────────────────────────────
    // The diffable half. tools/ui_iterate.sh compares this against the committed
    // reference so a layout change shows up as a text diff, not as "the picture
    // looks different to me".
    juce::String j;
    j << "{\n";
    j << "  \"harness\": \"UiDesignGallery\",\n";
    j << "  \"fixtures\": " << records.size() << ",\n";
    j << "  \"broken\": " << broken << ",\n";
    j << "  \"records\": [\n";
    for (int i = 0; i < records.size(); ++i)
        j << recordToJson(records.getReference(i)) << (i + 1 < records.size() ? ",\n" : "\n");
    j << "  ]\n";
    j << "}\n";

    const auto manifest = outDir.getChildFile("manifest.json");
    manifest.replaceWithText(j);

    std::printf("\n  manifest -> %s\n", manifest.getFullPathName().toRawUTF8());
    std::printf("  %d rendered, %d broken\n", records.size() - broken, broken);

    // Non-zero ONLY when the harness itself could not do its job. A layout you
    // dislike is not an error here; that is what the diff and your eyes are for.
    return broken > 0 ? 1 : 0;
}
