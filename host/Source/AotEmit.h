#pragma once
#include <faust/dsp/libfaust.h>

#include <string>
#include <vector>

// ── AotEmit — in-process Faust source → compilable AOT C++ header ────────────
// ADR-023's 2026-08-13 amendment (docs/decisions.md), sequenced as ADR-038's E1
// (docs/sessions/020-generated-faces-v2.md, Track E). The amendment established
// that `generateAuxFilesFromString` is exported and linked by the host's own
// `libfaust.so` (confirmed via `nm -D`) but never exercised its actual output —
// this is that exercise.
//
// Uses `generateAuxFilesFromString2` (`/usr/include/faust/dsp/libfaust.h:132`),
// not the plain `generateAuxFilesFromString` the ADR-023 amendment originally
// checked for: `...2` takes the identical name_app/dsp_content/argc/argv shape
// but returns the generated source directly as a `std::string` ("or an empty
// string in case of failure") instead of writing it to a path named by an `-o`
// argv — confirmed empirically this session (no in-tree usage example of `...2`
// existed to confirm against, unlike the file-based variant): called against a
// real effect source with no `-o` arg, it returned a complete
// `class mydsp : public dsp { ... }` header with no file ever touched; called
// against deliberately invalid source, it returned an empty string plus a
// populated error message. An earlier version of this file used the
// file-based variant with a `mkstemp`/reopen/read-back/remove round trip —
// pure overhead once `...2` is confirmed to do the same thing in memory, and
// it also removed a `/tmp`-path TOCTOU window and three duplicated cleanup
// call sites this file no longer has.
//
// KNOWN SIDE EFFECT, verified empirically this session: omitting `-o` from
// argv (required to get the string back at all — see below) makes
// `generateAuxFilesFromString2` also echo the full generated header to
// **stdout** as a side effect of every call. Confirmed both ways: passing an
// `-o <path>` argv suppresses the stdout echo but also makes the function
// return an empty string instead of the header (the two behaviours are
// mutually exclusive, not independent flags) — so there is no argv
// combination that gets the in-memory string without the stdout write.
// Harmless today (AotEmitTest's stdout isn't asserted on), but whoever wires
// E2 should decide whether to redirect stdout around the call or accept it —
// don't let it surface unnoticed in the actual export/plugin path.
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
// generateAuxFilesFromString2. `className` must match what a consuming
// PluginProcessor instantiates (ADR-023 amendment step 1); `appName` is
// libfaust's `name_app` and only affects the header's `metadata()` comment.
//
// argv is explicitly null-terminated even though argc is also passed
// correctly: the only confirmed real-world caller of this function family,
// `/usr/share/faust/cmajor/cmajor-tools.h:108`, does the same despite already
// passing a correct argc — matching that convention rather than relying on
// this build's specific behaviour (empirically, running this file's own test
// under ASan/UBSan showed no out-of-bounds read either way, so this is
// defensive alignment with the known-good reference, not a fix for an
// observed bug).
//
// OPEN QUESTION, BLOCKING for E2: thread-safety vs. FaustEngine's JIT path
// was not checked. FaustEngine.cpp:827 notes createDSPFactoryFromString "is
// not thread-safe (per llvm-dsp.h header comment)" — generateAuxFilesFromString2
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

    std::vector<const char*> argv = { "-lang", "cpp", "-cn", className.c_str(), nullptr };
    const int argc = static_cast<int>(argv.size()) - 1;    // exclude the null terminator
    std::string errorMsg;
    result.header = generateAuxFilesFromString2(appName, faustSource, argc, argv.data(), errorMsg);

    if (result.header.empty())
    {
        result.error = errorMsg.empty()
            ? "generateAuxFilesFromString2 returned an empty header with no error message"
            : errorMsg;
        return result;
    }

    result.ok = true;
    return result;
}

} // namespace AotEmit
