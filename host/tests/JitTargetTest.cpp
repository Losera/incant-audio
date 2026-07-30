// JitTargetTest — proves the pf_cpu_shim actually controls what instruction set
// libfaust's LLVM JIT emits. This is the PF-036 regression.
//
// ── What is being tested, and why it is not the obvious thing ───────────────
// The obvious test would be "run the render harness and see it not crash". That
// is untestable here: the crash only occurs on a CPU that NAMES an ISA it
// cannot execute (Azure's EPYC 9V74 reports as znver4 with AVX-512 masked out),
// and no dev box or CI runner can be requested by CPU model. A green CI run
// proves only that the runner draw was lucky — which is precisely the inference
// that closed PF-027 prematurely.
//
// So this test asserts the mechanism instead, and does it by DISASSEMBLING THE
// JIT'D CODE, which is deterministic on every machine:
//
//   RED  : under a CPU name with AVX-512, the JIT emits AVX-512.
//   GREEN: under a conservative CPU name, it emits none.
//
// Both arms run in one process, because pf_cpu_shim re-reads the environment on
// every call rather than caching it.
//
// ── Why the factory is never instantiated ──────────────────────────────────
// createDSPFactoryFromString materialises the machine code; createDSPInstance +
// init() are what EXECUTE it. The red arm deliberately compiles AVX-512 and
// must never run it, or this test would itself SIGILL on exactly the runners it
// exists to protect. Verified 2026-07-30 that factory creation alone is enough
// to get the code into an executable mapping.
#include <faust/dsp/llvm-dsp.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace
{

int failures = 0;
int checks   = 0;

void check(bool ok, const std::string& what)
{
    ++checks;
    printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what.c_str());
    if (!ok) ++failures;
}

// ── JIT memory capture ──────────────────────────────────────────────────────
// The JIT'd code lands in a fresh ANONYMOUS executable mapping. The CI
// backtrace named it "<in-memory@0x5220001c8140-0x5220001c96d8>" with no
// symbol, which is the same thing seen from outside. Snapshot /proc/self/maps
// around factory creation and keep whatever executable mapping is new.
struct Region { unsigned long lo, hi; };

std::vector<Region> execRegions()
{
    std::vector<Region> out;
    std::ifstream f("/proc/self/maps");
    std::string line;
    while (std::getline(f, line))
    {
        std::istringstream is(line);
        std::string range, perms, off, dev, inode, path;
        is >> range >> perms >> off >> dev >> inode;
        std::getline(is, path);

        if (perms.size() < 4 || perms[2] != 'x') continue;
        if (path.find('/') != std::string::npos) continue;   // file-backed, not JIT

        const auto dash = range.find('-');
        out.push_back({ strtoul(range.substr(0, dash).c_str(), nullptr, 16),
                        strtoul(range.substr(dash + 1).c_str(),  nullptr, 16) });
    }
    return out;
}

// Instruction-set census of one JIT'd patch, compiled under `cpu`.
struct Census { int evex = 0; int kmask = 0; int vex = 0; bool compiled = false; };

Census censusFor(const char* name, const std::string& src, const char* cpu)
{
    Census c;
    setenv("PLUGINFORGE_FAUST_CPU", cpu, 1);

    std::set<unsigned long> before;
    for (const auto& r : execRegions()) before.insert(r.lo);

    std::string err;
    // NOTE: the `target` argument is "" deliberately. It is INERT on this path
    // (docs/BUGS.md PF-036) -- pf_cpu_shim is what selects the CPU. Passing a
    // target here would read as though it were doing the work.
    llvm_dsp_factory* f =
        createDSPFactoryFromString(name, src, 0, nullptr, "", err, -1);
    if (!f)
    {
        printf("  (patch '%s' failed to compile under %s: %s)\n", name, cpu, err.c_str());
        return c;
    }
    c.compiled = true;

    const std::string dump = std::string("/tmp/pf_jit_") + name + "_" + cpu + ".bin";
    {
        std::ofstream o(dump, std::ios::binary);
        for (const auto& r : execRegions())
            if (!before.count(r.lo))
                o.write(reinterpret_cast<const char*>(r.lo), r.hi - r.lo);
    }
    // Never createDSPInstance()/init() here -- see the header comment.
    deleteDSPFactory(f);

    // objdump is the decoder of record: hand-rolling EVEX prefix detection over
    // raw bytes misfires on immediates and displacements that happen to be 0x62.
    const std::string cmd =
        "objdump -b binary -m i386:x86-64 -D '" + dump + "' 2>/dev/null";
    FILE* p = popen(cmd.c_str(), "r");
    if (!p)
    {
        printf("  (objdump unavailable -- cannot decode JIT output)\n");
        c.compiled = false;
        return c;
    }

    char line[1024];
    while (fgets(line, sizeof line, p))
    {
        // Mnemonic-level signals, which is what the CI backtrace showed:
        //   kmovd %r9d,%k1   /  vmovss %xmm2,%xmm2,%xmm2{%k1}{z}
        if (strstr(line, "kmov") || strstr(line, "%k1") || strstr(line, "{k1}")) ++c.kmask;
        if (strstr(line, "zmm")) ++c.evex;
        // Raw EVEX prefix byte in the hex column.
        if (strstr(line, ":\t62 ") || strstr(line, ":  62 ")) ++c.evex;
        if (strstr(line, ":\tc5 ") || strstr(line, ":\tc4 ")) ++c.vex;
    }
    pclose(p);
    remove(dump.c_str());
    return c;
}

// ── Corpus ──────────────────────────────────────────────────────────────────
// `tremolo` is verbatim from OfflineRenderTest's kWellBehaved and is the patch
// that actually crashed CI: os.osc's phasor wrap compiles to `vroundss $0x9`,
// and the CI backtrace faulted on the `kmovd` immediately preceding it. The
// others are here so the red arm does not rest on a single patch's codegen
// surviving a Faust version bump.
//
// This is a purpose-built set, not a mirror of the render corpus, and does not
// need to be: pf_cpu_shim acts on the JIT globally, so the property is not
// per-patch.
struct Patch { const char* name; const char* src; };

const std::vector<Patch> kCorpus = {
    { "tremolo",
      "import(\"stdfaust.lib\");\n"
      "rate = hslider(\"Rate\",4,0.1,20,0.1);\n"
      "depth = hslider(\"Depth\",0.7,0,1,0.01);\n"
      "lfo = 1 - depth*(0.5+0.5*os.osc(rate));\n"
      "process = *(lfo), *(lfo);" },

    { "toggle",
      "import(\"stdfaust.lib\");\n"
      "on = checkbox(\"Bright\");\n"
      "voice = _ <: fi.lowpass(2,800), fi.highpass(2,2000) : ba.selectn(2, on);\n"
      "process = voice, voice;" },

    { "highpass",
      "import(\"stdfaust.lib\");\n"
      "hp = hslider(\"Corner[scale:log]\",1200,20,8000,1);\n"
      "trim = hslider(\"Trim[unit:dB]\",0,-24,6,0.1) : ba.db2linear;\n"
      "process = fi.highpass(2,hp)*trim, fi.highpass(2,hp)*trim;" },
};

} // namespace

int main()
{
    printf("JitTargetTest -- PF-036: does pf_cpu_shim control the JIT's ISA?\n\n");

    // ── Gate: the shim must actually be loaded ──────────────────────────────
    // Without this, a run with LD_PRELOAD missing would compile everything
    // host-native, find no AVX-512 on a non-AVX-512 dev box, and report PASS
    // while testing nothing.
    printf("shim presence\n");
    const bool shimLoaded = dlsym(RTLD_DEFAULT, "pf_cpu_shim_present") != nullptr;
    check(shimLoaded, "pf_cpu_shim is LD_PRELOADed");
    if (!shimLoaded)
    {
        printf("\n  This test is meaningless without the shim. Run it as:\n"
               "    LD_PRELOAD=<build>/libpf_cpu_shim.so JitTargetTest\n");
        return 1;
    }

    // ── GREEN: a conservative CPU emits no AVX-512 anywhere ─────────────────
    printf("\nconservative CPU (x86-64) -- the setting CI uses\n");
    int compiledGreen = 0;
    for (const auto& p : kCorpus)
    {
        const Census c = censusFor(p.name, p.src, "x86-64");
        if (!c.compiled) continue;
        ++compiledGreen;
        check(c.evex == 0 && c.kmask == 0,
              std::string(p.name) + ": no AVX-512 (evex=" + std::to_string(c.evex)
                  + " kmask=" + std::to_string(c.kmask) + ")");
        check(c.vex == 0,
              std::string(p.name) + ": no AVX either (vex=" + std::to_string(c.vex) + ")");
    }
    check(compiledGreen == (int) kCorpus.size(), "every corpus patch compiled");

    // ── RED: an AVX-512 CPU name emits AVX-512 ──────────────────────────────
    // This arm is what stops the green arm being vacuous. If the shim silently
    // stopped working -- wrong symbol, changed LLVM ABI, preload dropped from
    // the workflow -- both arms would produce identical host-native code and
    // this assertion is the one that fails.
    printf("\nAVX-512 CPU (znver4) -- the CI runner's misdetected identity\n");
    int sawAvx512 = 0;
    for (const auto& p : kCorpus)
    {
        const Census c = censusFor(p.name, p.src, "znver4");
        if (!c.compiled) continue;
        printf("  %-10s evex=%-3d kmask=%-3d vex=%-3d\n", p.name, c.evex, c.kmask, c.vex);
        if (c.evex > 0 || c.kmask > 0) ++sawAvx512;
    }
    check(sawAvx512 > 0,
          "at least one patch emits AVX-512 under znver4 (red case is live, "
          "shim is having an effect)");

    printf("\n%s -- %d failure(s)\n", failures ? "FAILED" : "PASSED", failures);
    // One machine-readable line for tools/health_report.py. The `checks` total is
    // the point: every harness here already carried an exact `failures` int and
    // threw the denominator away, so "0 failures" was indistinguishable from
    // "ran nothing". Format is fixed and shared by all seven harnesses.
    printf("PF_SUMMARY harness=%s checks=%d failures=%d\n", "JitTargetTest", checks, failures);
    return failures ? 1 : 0;
}
