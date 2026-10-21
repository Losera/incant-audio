// Diagnostic-only driver for PF-074 (not part of the build; see
// ~/.claude/plans/i-was-trying-out-steady-graham.md Phase 2).
//
// Compiles against the real Faust output for a captured .dsp (via MapUI's
// named-zone lookup) and drives one named control across its declared range
// WHILE audio renders, one block at a time, with a note held throughout.
// Measures non-finite / peak / RMS directly on dsp::compute()'s output --
// i.e. BEFORE anything resembling OutputGuard touches it. This is the
// pre-guard measurement the plan calls for: it separates "the generated DSP
// itself went non-finite" from "a host-side latch misfired".
//
// Usage: ./sweep_driver <patch.cpp-generated-class-name header already
//                        #included below>
//        --param <label> --from <v0> --to <v1> --blocks <n> --blocksize <n>
//        --hold-note (default on)
//
// Kept deliberately standalone: no JUCE, no libfaust JIT, just the Faust
// compiler's own `-lang cpp` output plus the architecture headers every Faust
// install ships (/usr/include/faust/dsp/dsp.h, gui/MapUI.h). This is the
// same compute() a FaustEngine-hosted plugin would call; nothing in the host
// sits between it and this harness.

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "faust/dsp/dsp.h"
#include "faust/gui/meta.h"
#include "faust/gui/MapUI.h"

// The generated DSP class, produced by:
//   faust -lang cpp -cn mydsp -o <name>.cpp <name>.dsp
#include DSP_SOURCE_FILE

static double g_sampleRate = 48000.0;

struct Args {
    std::string param;
    double from = 0.0, to = 0.0;
    int blocks = 200;
    int blockSize = 256;
    bool sweep = false;   // false => hold `param` at `from` for the whole render (static case, H2/H5)
    std::string gateParam = "gate";
    std::string freqParam = "freq";
    std::string gainParam = "gain";
    double freq = 55.0;
    double gain = 0.8;
    std::vector<std::pair<std::string, double>> holds;  // other params held fixed, e.g. Resonance=5
};

static void printUsageAndExit(const char* prog)
{
    std::fprintf(stderr,
        "Usage: %s --param NAME --from V0 [--to V1 --sweep] [--blocks N] [--blocksize N] "
        "[--freq HZ] [--gain G]\n", prog);
    std::exit(2);
}

static Args parseArgs(int argc, char** argv)
{
    Args a;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        auto next = [&]() -> std::string {
            if (i + 1 >= argc) printUsageAndExit(argv[0]);
            return argv[++i];
        };
        if (arg == "--param") a.param = next();
        else if (arg == "--from") a.from = std::stod(next());
        else if (arg == "--to") { a.to = std::stod(next()); a.sweep = true; }
        else if (arg == "--blocks") a.blocks = std::stoi(next());
        else if (arg == "--blocksize") a.blockSize = std::stoi(next());
        else if (arg == "--freq") a.freq = std::stod(next());
        else if (arg == "--gain") a.gain = std::stod(next());
        else if (arg == "--hold") {
            std::string kv = next();
            auto eq = kv.find('=');
            if (eq == std::string::npos) printUsageAndExit(argv[0]);
            a.holds.emplace_back(kv.substr(0, eq), std::stod(kv.substr(eq + 1)));
        }
        else printUsageAndExit(argv[0]);
    }
    if (a.param.empty()) printUsageAndExit(argv[0]);
    return a;
}

int main(int argc, char** argv)
{
    Args a = parseArgs(argc, argv);

    mydsp dsp;
    MapUI ui;
    dsp.buildUserInterface(&ui);
    dsp.init(static_cast<int>(g_sampleRate));

    ui.setParamValue(a.freqParam, static_cast<FAUSTFLOAT>(a.freq));
    ui.setParamValue(a.gainParam, static_cast<FAUSTFLOAT>(a.gain));
    ui.setParamValue(a.gateParam, FAUSTFLOAT(1));   // hold the note the whole render
    for (const auto& kv : a.holds)
        ui.setParamValue(kv.first, static_cast<FAUSTFLOAT>(kv.second));

    const int numIn = dsp.getNumInputs();
    const int numOut = dsp.getNumOutputs();
    std::vector<std::vector<FAUSTFLOAT>> inBuf(static_cast<size_t>(std::max(numIn, 1)),
                                                std::vector<FAUSTFLOAT>(static_cast<size_t>(a.blockSize), FAUSTFLOAT(0)));
    std::vector<std::vector<FAUSTFLOAT>> outBuf(static_cast<size_t>(std::max(numOut, 1)),
                                                 std::vector<FAUSTFLOAT>(static_cast<size_t>(a.blockSize), FAUSTFLOAT(0)));
    std::vector<FAUSTFLOAT*> inPtrs, outPtrs;
    for (auto& v : inBuf) inPtrs.push_back(v.data());
    for (auto& v : outBuf) outPtrs.push_back(v.data());

    bool sawNonFinite = false;
    int firstNonFiniteBlock = -1;
    float peak = 0.0f;
    double sumSq = 0.0;
    long totalSamples = 0;

    // ADR-020 runaway replica: |y| >= 1.0 continuously, per-channel, for
    // kRunawaySeconds (0.5s) -> OutputGuard::Latch mutes an EFFECT (not an
    // instrument, which gets ::Report instead -- see OutputGuard.h). Tracked
    // here, pre-guard, so a sustained-loud verdict can be told apart from an
    // actual NaN.
    const int runawayThresholdSamples = static_cast<int>(0.5 * g_sampleRate);
    std::vector<int> runawayRun(static_cast<size_t>(std::max(numOut, 1)), 0);
    bool sawRunaway = false;
    int firstRunawayBlock = -1;

    std::printf("param=%s sweep=%d from=%g to=%g blocks=%d blockSize=%d sampleRate=%g\n",
                a.param.c_str(), a.sweep ? 1 : 0, a.from, a.to, a.blocks, a.blockSize, g_sampleRate);

    for (int b = 0; b < a.blocks; ++b) {
        double t = (a.blocks > 1) ? static_cast<double>(b) / static_cast<double>(a.blocks - 1) : 0.0;
        double value = a.sweep ? (a.from + t * (a.to - a.from)) : a.from;
        ui.setParamValue(a.param, static_cast<FAUSTFLOAT>(value));

        dsp.compute(a.blockSize, inPtrs.data(), outPtrs.data());

        bool blockNonFinite = false;
        bool blockRunaway = false;
        for (int ch = 0; ch < numOut; ++ch) {
            for (int i = 0; i < a.blockSize; ++i) {
                float y = outPtrs[static_cast<size_t>(ch)][i];
                if (!std::isfinite(y)) { blockNonFinite = true; }
                else {
                    float m = std::fabs(y);
                    if (m > peak) peak = m;
                    sumSq += static_cast<double>(y) * static_cast<double>(y);
                    ++totalSamples;
                    if (m >= 1.0f) {
                        if (++runawayRun[static_cast<size_t>(ch)] >= runawayThresholdSamples)
                            blockRunaway = true;
                    } else {
                        runawayRun[static_cast<size_t>(ch)] = 0;
                    }
                }
            }
        }
        if (blockNonFinite && !sawNonFinite) { sawNonFinite = true; firstNonFiniteBlock = b; }
        if (blockRunaway && !sawRunaway) { sawRunaway = true; firstRunawayBlock = b; }

        if (b % std::max(1, a.blocks / 20) == 0 || blockNonFinite || blockRunaway) {
            std::printf("block %4d  %s=%.6f  %s%s\n", b, a.param.c_str(), value,
                        blockNonFinite ? "NON-FINITE " : "finite ",
                        blockRunaway ? "RUNAWAY(>=0.5s @|y|>=1.0)" : "");
        }
        if (blockNonFinite) break;  // state is poisoned forever past this point; no point continuing
    }

    double rms = totalSamples > 0 ? std::sqrt(sumSq / static_cast<double>(totalSamples)) : 0.0;
    std::printf("---\nresult: %s%s\n",
                sawNonFinite ? "NON-FINITE OUTPUT " : "stayed finite ",
                sawRunaway ? "+ RUNAWAY (sustained |y|>=1.0 for >=0.5s)" : "");
    if (sawNonFinite) std::printf("first non-finite block: %d\n", firstNonFiniteBlock);
    if (sawRunaway) std::printf("first runaway-verdict block: %d\n", firstRunawayBlock);
    std::printf("peak(finite region)=%.6f  rms(finite region)=%.6f\n", peak, rms);
    return (sawNonFinite || sawRunaway) ? 1 : 0;
}
