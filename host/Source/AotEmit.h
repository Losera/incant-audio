#pragma once
#include <faust/dsp/libfaust.h>

#include <cstdio>
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
// CORRECTED this session: an earlier version of this file switched to the
// sibling `generateAuxFilesFromString2` (same header, returns the source
// directly as a std::string, no `-o`/no file needed) after confirming it
// works on this dev machine's Faust 2.85.9. It does not exist on
// `.github/workflows/test.yml`'s CI runner, which installs Ubuntu Noble's
// packaged `faust 2.70.3+ds` (see that workflow's own "Install system
// dependencies" comment) — CI's build-host job failed outright:
// "'generateAuxFilesFromString2' was not declared in this scope." Reverted to
// the file-based variant below, which both environments have. The dev-machine
// vs. CI Faust version gap this exposed (2.85.9 vs. 2.70.3+ds) is a real,
// pre-existing fact about this repo's toolchain, not something this file
// fixes — worth a STATUS.md line, not a workaround here.
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

// Removes the file at `path` on destruction, unless `release()` is called
// first. Collapses what used to be three duplicated `std::remove()` calls (one
// per early return in emitHeader()) into one declaration, and — unlike the
// hand-placed calls it replaces — still fires if a future edit adds a new
// early return and forgets to instrument it.
class ScopedTempFile
{
public:
    explicit ScopedTempFile(std::string path) : path_(std::move(path)) {}
    ~ScopedTempFile() { if (! released_) std::remove(path_.c_str()); }
    ScopedTempFile(const ScopedTempFile&) = delete;
    ScopedTempFile& operator=(const ScopedTempFile&) = delete;
    void release() { released_ = true; }

private:
    std::string path_;
    bool released_ = false;
};

// Turns accepted Faust source into a compilable AOT header via libfaust's
// generateAuxFilesFromString. `className` must match what a consuming
// PluginProcessor instantiates (ADR-023 amendment step 1); `appName` is
// libfaust's `name_app` and only affects the header's `metadata()` comment.
//
// argv is explicitly null-terminated even though argc is also passed
// correctly: the only confirmed real-world caller of this function family,
// `/usr/share/faust/cmajor/cmajor-tools.h:108`, does the same despite already
// passing a correct argc — matching that convention rather than relying on
// this build's specific behaviour (empirically, running this file's own test
// under ASan/UBSan showed no out-of-bounds read either way on this dev
// machine's Faust, so this is defensive alignment with the known-good
// reference, not a fix for an observed bug — and CI runs an older Faust this
// session had no way to ASan-test directly).
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
    ScopedTempFile tempFile(tmpTemplate);

    std::vector<const char*> argv = { "-lang", "cpp", "-cn", className.c_str(), "-o", tmpTemplate, nullptr };
    const int argc = static_cast<int>(argv.size()) - 1;    // exclude the null terminator
    std::string errorMsg;
    const bool generated = generateAuxFilesFromString(
        appName, faustSource, argc, argv.data(), errorMsg);

    if (!generated)
    {
        result.error = errorMsg;
        return result;
    }

    std::ifstream in(tmpTemplate, std::ios::binary);
    if (!in)
    {
        result.error = "generateAuxFilesFromString reported success but " + std::string(tmpTemplate)
                        + " could not be read back";
        return result;
    }
    std::ostringstream buf;
    buf << in.rdbuf();
    result.header = buf.str();
    result.ok = true;
    return result;
}

} // namespace AotEmit
