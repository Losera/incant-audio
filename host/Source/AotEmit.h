#pragma once
#include <faust/dsp/libfaust.h>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <unistd.h>
#include <vector>

// ── AotEmit — in-process Faust source → compilable AOT C++ header ────────────
// ADR-023's 2026-08-13 amendment (docs/decisions.md), sequenced as ADR-038's E1
// (docs/sessions/020-generated-faces-v2.md, Track E). The amendment established
// that `generateAuxFilesFromString` is exported and linked by the host's own
// `libfaust.so` (confirmed via `nm -D`) but never exercised its actual output —
// this is that exercise.
//
// `generateAuxFilesFromString` does not return the generated source directly:
// confirmed against its only in-tree usage example,
// `/usr/share/faust/cmajor/cmajor-tools.h`, which calls it for a side effect
// (a file written at the path named by its `-o` argv) and reads that file back.
// "In-process, no subprocess" therefore means no `faust` CLI process is
// spawned — libfaust does the compilation inside this process — not that no
// file is touched; a real temp path is still required for the `-o` target.
//
// The emitted header is intentionally NOT self-contained: `faust -lang cpp`
// output assumes `faust/dsp/dsp.h`, `faust/gui/UI.h`, `faust/gui/meta.h` (and,
// if the caller will build a UI, `faust/gui/MapUI.h`) are already included by
// whatever includes it — confirmed by compiling a generated header with each
// combination in isolation; only that ordered set compiles clean. See
// AotEmitTest.cpp's round-trip stub_main for the exact include order.
namespace AotEmit
{

struct EmitResult
{
    bool ok = false;
    std::string header;    // emitted "class <className> : public dsp" C++ source
    std::string className;
    std::string error;     // libfaust's error_msg; meaningful only when !ok
};

// Turns accepted Faust source into a compilable AOT header via libfaust's
// generateAuxFilesFromString. `className` must match what a consuming
// PluginProcessor instantiates (ADR-023 amendment step 1); `appName` is
// libfaust's `name_app` and only affects the header's `metadata()` comment.
//
// OPEN QUESTION, BLOCKING for E2: thread-safety vs. FaustEngine's JIT path
// was not checked. FaustEngine.cpp:827 notes createDSPFactoryFromString "is
// not thread-safe (per llvm-dsp.h header comment)" — generateAuxFilesFromString
// runs the same Faust parser/compiler internals and may share the same global
// state (this file's own AotEmitTest run showed the parser leaking process-
// global buffers on every call, consistent with shared statics). Nothing
// today calls emitHeader() from anywhere concurrent with FaustEngine::compile(),
// so no lock was added. Before E2 (or anything else) calls this from a path
// that can run alongside a live FaustEngine compile, that must be resolved —
// either confirm the two are safe to interleave, or serialize them (e.g. under
// FaustEngine's own compileMutex, FaustEngine.cpp:826) before wiring it in.
inline EmitResult emitHeader(const std::string& faustSource,
                              const std::string& className = "mydsp",
                              const std::string& appName = "PluginForgeExport")
{
    EmitResult result;
    result.className = className;

    char tmpTemplate[] = "/tmp/pluginforge_aot_XXXXXX";
    int fd = mkstemp(tmpTemplate);
    if (fd < 0)
    {
        result.error = "mkstemp failed for AOT header temp file";
        return result;
    }
    ::close(fd);
    const std::string outPath(tmpTemplate);

    std::vector<const char*> argv = { "-lang", "cpp", "-cn", className.c_str(), "-o", outPath.c_str() };
    std::string errorMsg;
    const bool generated = generateAuxFilesFromString(
        appName, faustSource, static_cast<int>(argv.size()), argv.data(), errorMsg);

    if (!generated)
    {
        result.error = errorMsg;
        std::remove(outPath.c_str());
        return result;
    }

    std::ifstream in(outPath, std::ios::binary);
    if (!in)
    {
        result.error = "generateAuxFilesFromString reported success but " + outPath
                        + " could not be read back";
        std::remove(outPath.c_str());
        return result;
    }
    std::ostringstream buf;
    buf << in.rdbuf();
    result.header = buf.str();
    result.ok = true;

    std::remove(outPath.c_str());
    return result;
}

} // namespace AotEmit
