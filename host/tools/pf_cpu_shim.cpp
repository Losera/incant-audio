// pf_cpu_shim — LD_PRELOAD interposer for llvm::sys::getHostCPUName().
//
// ⚠️ CI-ONLY. This is never loaded by the shipping plugin. On a user's machine
// the host CPU detection is correct and real-time DSP wants the best available
// instruction set; see the PF-036 entry in docs/BUGS.md for why the product path
// deliberately stays host-native.
//
// ── The problem ─────────────────────────────────────────────────────────────
// libfaust's LLVM JIT picks its target CPU by NAME (llvm::sys::getHostCPUName(),
// which reads CPUID family/model) and then enables that CPU's DEFAULT feature
// set. It never rechecks whether the running guest can actually execute those
// instructions. On GitHub's AMD EPYC 9V74 runners — Azure's custom Genoa — the
// name resolves to "znver4", whose defaults include AVX-512, while the
// hypervisor masks AVX-512 out of the guest. The JIT then emits EVEX/opmask
// instructions the CPU traps on:
//
//     Thread 1 "OfflineRenderTe" received signal SIGILL, Illegal instruction.
//     0x00007ffff6a7e27d in computemydsp ()
//     => 0x7ffff6a7e27d <computemydsp+93>:  kmovd  %r9d,%k1
//
// GitHub's hosted fleet mixes EPYC 7763 (Zen 3, no AVX-512 — safe), Xeon 8573C
// (AVX-512 present AND enabled — safe) and EPYC 9V74 (AVX-512 named but masked
// — SIGILL), so CI presented as ~1-in-5 flaky rather than as a hard failure.
//
// ── Why this and not the documented knob ────────────────────────────────────
// createDSPFactoryFromString takes a `target` parameter documented at
// /usr/include/faust/dsp/llvm-dsp.h:226-228 as "the LLVM machine target ... and
// i386-apple-macosx10.6.0:generic kind of syntax for a generic processor".
// **That parameter is inert on the JIT path.** Measured against libfaust 2.85.5
// on 2026-07-30: asking for "x86_64-pc-linux-gnu:i486" — a CPU with no SSE at
// all — still emitted 28 VEX-prefixed AVX instructions, byte-identical to the
// host-native build, and "totally-bogus-triple:nonexistent-cpu" was accepted
// without error. Pinning that parameter would have been a placebo.
//
// libfaust leaves getHostCPUName UNDEFINED and resolves it from libLLVM.so at
// load time:
//
//     $ nm -D --undefined-only /usr/lib/libfaust.so.2.85.5 | grep -i hostcpu
//                      U _ZN4llvm3sys14getHostCPUNameEv@LLVM_22.1
//
// so it is interposable, and interposing it DOES control codegen: the tremolo
// corpus patch emits 1 EVEX + 1 kmov under "znver4" and zero of either under
// "x86-64". That is the property host/tests/JitTargetTest.cpp asserts.
//
// ── ABI ─────────────────────────────────────────────────────────────────────
// llvm::sys::getHostCPUName() returns llvm::StringRef BY VALUE (it changed from
// std::string in LLVM 19). StringRef is { const char* Data; size_t Length; } —
// a 16-byte trivially-copyable aggregate, so the SysV AMD64 ABI returns it in
// rax:rdx. StringRefABI below reproduces that layout exactly.
//
// If a future LLVM changes this signature, the symptom is a wrong-looking CPU
// name rather than a silent no-op — and JitTargetTest fails, because it asserts
// the two CPU settings produce DIFFERENT code. A shim that stopped working
// would make them identical.
#include <cstdlib>
#include <cstring>

namespace { struct StringRefABI { const char* data; unsigned long size; }; }

// Deliberately NOT cached. getHostCPUName is called once per factory creation,
// so re-reading the environment lets a single process compile under several CPU
// settings — which is exactly how JitTargetTest gets its red case and its green
// case without re-exec'ing itself.
extern "C" StringRefABI _ZN4llvm3sys14getHostCPUNameEv()
{
    const char* e = std::getenv("PLUGINFORGE_FAUST_CPU");
    const char* cpu = (e && *e) ? e : "x86-64";
    return { cpu, std::strlen(cpu) };
}

// Presence marker. JitTargetTest dlsym()s this so that a run with the preload
// missing FAILS LOUDLY instead of passing vacuously — the shim asserting
// nothing looks identical to the shim working, and this project has been bitten
// three times by a control that was configured but not running (CLAUDE.md,
// "A control counts only once it has been seen failing").
extern "C" int pf_cpu_shim_present() { return 1; }
