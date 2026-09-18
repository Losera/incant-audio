// ---------------------------------------------------------------------------
// AotEmitTest — ADR-038 E1: exercise generateAuxFilesFromString end to end.
//
// The 2026-08-13 amendment to ADR-023 (docs/decisions.md) verified the symbol
// exists and links but never ran it. This test does three things the
// amendment left open:
//
//   1. Calls AotEmit::emitHeader on real Faust source and asserts it produced
//      a `class <name> : public dsp` header, not just a non-empty string.
//   2. ROUND-TRIPS: writes that header to disk, shells out to the same
//      compiler this build uses (a real `g++ -c`, not `-fsyntax-only`) against
//      a stub main.cpp, and asserts the compile actually succeeds. This is
//      the check the E1 "done when" (session 020) names explicitly.
//   3. Asserts the failure path: invalid Faust source reports ok=false with a
//      non-empty error and no header, rather than silently emitting nothing.
//
// Same convention as JitTargetTest.cpp: no JUCE, links only ${LIBFAUST_LIB},
// -fno-rtti to match libfaust.so's own build (see host/CMakeLists.txt's
// comment on the other libfaust harnesses for why).
//
// NOT covered here (deliberately, per ADR-038 E1's scope): wiring an emitted
// header into `processBlock` (E2), running the exported plugin under
// pluginval or producing sound (E3), and whether `generateAuxFilesFromString`
// is safe to call concurrently with `FaustEngine`'s own JIT compiles —
// `createDSPFactoryFromString`'s header comment says it is not thread-safe
// (FaustEngine.cpp's own comment at the call site) and nothing here checked
// whether that extends to this function; E1's helper is not called from
// anywhere the JIT compile path runs, so no lock was added.
//
// Run: ./AotEmitTest — exits 0 on success, 1 with a failure list. Requires
// `g++` on PATH (checked at startup; skips the round-trip check, not the
// whole binary, if absent).
// ---------------------------------------------------------------------------
#include "../Source/AotEmit.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <unistd.h>

// libfaust leaks its parser buffers on every compile (FAUST_scan_buffer, inside
// libfaust.so) — same defect StatePersistenceTest.cpp already suppresses for
// FaustEngine's JIT path; generateAuxFilesFromString shares the same parser.
// Third-party and not reachable from anything this repo can free. Matched on
// the library so a leak in OUR code still fails the test.
extern "C" const char* __lsan_default_suppressions()
{
    return "leak:libfaust\n";
}

namespace
{
int failures = 0;
int checks   = 0;

void expectTrue(bool cond, const std::string& what)
{
    ++checks;
    if (! cond)
    {
        ++failures;
        std::cerr << "FAIL  " << what << "\n";
    }
}

void expectContains(const std::string& haystack, const std::string& needle, const std::string& what)
{
    ++checks;
    if (haystack.find(needle) == std::string::npos)
    {
        ++failures;
        std::cerr << "FAIL  " << what << "\n"
                  << "      expected to find: " << needle << "\n";
    }
}

// A minimal but non-trivial effect: import + a gain multiply, matching the
// shape of tests/test_export_repo.py's EFFECT_SOURCE so both test suites are
// exercising the same kind of accepted patch.
const std::string kEffectSource =
    "import(\"stdfaust.lib\");\n"
    "gain = hslider(\"gain\", 0.5, 0, 1, 0.01);\n"
    "process = _ * gain;\n";

const std::string kInvalidSource = "process = this is not faust;\n";

void testEmitProducesCompilableClass()
{
    auto result = AotEmit::emitHeader(kEffectSource, "mydsp", "AotEmitTest");
    expectTrue(result.ok, "emitHeader succeeds on valid Faust source");
    expectTrue(result.error.empty(), "no error on success");
    expectContains(result.header, "class mydsp : public dsp", "header declares the requested class");
    expectContains(result.header, "compute(", "header defines compute()");
}

void testEmitFailsCleanlyOnInvalidSource()
{
    auto result = AotEmit::emitHeader(kInvalidSource, "mydsp", "AotEmitTest");
    expectTrue(! result.ok, "emitHeader reports failure on invalid Faust source");
    expectTrue(! result.error.empty(), "failure carries a non-empty error message");
    expectTrue(result.header.empty(), "no header content on failure");
}

// The round-trip: an emitted header must actually compile, not just look
// plausible. This is what ADR-023's amendment left unverified.
void testEmittedHeaderCompiles()
{
    if (std::system("g++ --version > /dev/null 2>&1") != 0)
    {
        std::cout << "SKIP  round-trip compile check: g++ not on PATH\n";
        return;
    }

    auto result = AotEmit::emitHeader(kEffectSource, "mydsp", "AotEmitTest");
    expectTrue(result.ok, "emitHeader succeeds before round-trip compile");
    if (! result.ok)
        return;

    char headerTemplate[] = "/tmp/aotemittest_header_XXXXXX";
    int headerFd = mkstemp(headerTemplate);
    expectTrue(headerFd >= 0, "mkstemp for the emitted header succeeds");
    if (headerFd < 0)
        return;
    {
        std::ofstream out(headerTemplate, std::ios::trunc);
        out << result.header;
    }
    close(headerFd);
    const std::string headerPath(headerTemplate);

    char mainTemplate[] = "/tmp/aotemittest_main_XXXXXX";
    int mainFd = mkstemp(mainTemplate);
    expectTrue(mainFd >= 0, "mkstemp for the stub main succeeds");
    if (mainFd < 0)
    {
        std::remove(headerPath.c_str());
        return;
    }
    {
        std::ofstream out(mainTemplate, std::ios::trunc);
        // Include order matters: the emitted header assumes dsp/UI/meta are
        // already declared, confirmed by compiling each combination in
        // isolation while writing this test (see AotEmit.h's header comment).
        out << "#include <faust/dsp/dsp.h>\n"
               "#include <faust/gui/UI.h>\n"
               "#include <faust/gui/meta.h>\n"
               "#include <faust/gui/MapUI.h>\n"
               "#include \""
            << headerPath
            << "\"\n"
               "int main() {\n"
               "    mydsp d;\n"
               "    MapUI ui;\n"
               "    d.init(48000);\n"
               "    d.buildUserInterface(&ui);\n"
               "    return 0;\n"
               "}\n";
    }
    close(mainFd);
    const std::string mainPath(mainTemplate);

    const std::string objPath = mainPath + ".o";
    // -x c++: mainPath has no .cpp suffix (mkstemp's template can't carry one
    // without a GNU-extension mkstemps call), so the language must be forced.
    const std::string cmd = "g++ -std=c++17 -x c++ -c " + mainPath + " -o " + objPath + " 2>&1";
    FILE* pipe = popen(cmd.c_str(), "r");
    std::string compilerOutput;
    if (pipe != nullptr)
    {
        char buf[256];
        while (fgets(buf, sizeof(buf), pipe) != nullptr)
            compilerOutput += buf;
    }
    const int rc = pipe != nullptr ? pclose(pipe) : -1;

    expectTrue(rc == 0, "round-trip: g++ compiles the emitted header against a stub main");
    if (rc != 0)
        std::cerr << "      compiler output:\n" << compilerOutput << "\n";

    std::remove(headerPath.c_str());
    std::remove(mainPath.c_str());
    std::remove(objPath.c_str());
}

} // namespace

int main()
{
    std::cout << "AotEmitTest — ADR-038 E1, generateAuxFilesFromString end to end\n";

    testEmitProducesCompilableClass();
    testEmitFailsCleanlyOnInvalidSource();
    testEmittedHeaderCompiles();

    std::cout << checks - failures << "/" << checks << " checks passed\n";

    if (failures != 0)
    {
        std::cerr << "\n" << failures << " FAILED\n";
        return 1;
    }

    std::cout << "PASS\n";
    return 0;
}
