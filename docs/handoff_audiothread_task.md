# Handoff: libfaust JIT Threading Reference Implementation

## Section 1: Task Statement

Produce a PAIR-mode reference implementation of the background-thread JIT compilation
pattern for `FaustEngine`. Deliverable: `docs/audio_thread_example.md` — not integrated
into `FaustEngine.cpp`.

The reference must demonstrate:
1. Spawning a one-shot background thread for libfaust JIT compilation
2. Atomic pointer swap of the resulting `llvm_dsp*` with correct memory orderings
3. Safe lifecycle: old DSP deleted off the audio thread, factory owned correctly

Engagement mode: **PAIR**. Claude Code produces the reference. The human reads it,
verifies the API against installed headers, then writes the version that goes in the
codebase. The reference is an artifact to read, not to commit verbatim.

Success: the human can follow the reference to implement `compile()` and `process()`
correctly without guessing at threading or memory semantics.

## Section 2: Required Project Context

PluginForge is a JUCE 7 (C++17) VST3/AU plugin. `FaustEngine` wraps libfaust LLVM JIT.
The audio callback runs on a real-time thread; `compile()` must never block it.

`FaustEngine.cpp` is a broken stub — correct structure, wrong API and syntax. The
reference should be correct; this file is what the human will rewrite after reading it.

**`host/Source/FaustEngine.h` — current contents:**
```cpp
#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <faust/dsp/llvm-dsp.h>
#include <faust/gui/MapUI.h>
#include <functional>
#include <string>
#include <vector>
#include <atomic>
#include <memory>

class FaustEngine
{
public:
    struct ParamInfo { std::string label; float defaultValue, min, max, step; };
    using ParamList       = std::vector<ParamInfo>;
    using CompileCallback = std::function<void(const ParamList&, const std::string& error)>;

    void setParamValue(const std::string& label, float value);
    FaustEngine()  = default;
    ~FaustEngine();
    void prepare(double sampleRate, int blockSize);
    void release();
    void process(juce::AudioBuffer<float>& buffer);
    void compile(const juce::String& faustCode, CompileCallback cb); // async
    bool isReady() const { return ready.load(); }

private:
    double sr = 44100.0;  int block = 512;
    std::atomic<bool>      ready     { false };
    llvm_dsp_factory*      factory   = nullptr;
    std::atomic<llvm_dsp*> activeDSP { nullptr };
    MapUI                  activeUI;
};
```

**`host/Source/FaustEngine.cpp` — current contents (broken stub):**
```cpp
#include "FaustEngine.h"
#include <thread>

void FaustEngine::prepare(double sampleRate, int blockSize) { sr = sampleRate; block = blockSize; }
void FaustEngine::release() { ready.store(false); }

void FaustEngine::process(juce::AudioBuffer<float>& buffer)
{
    if (!ready.load()) return;
    activeDSP.load()->compute(buffer.getNumSamples(),
                              buffer.getArrayOfReadPointers(),   // BUG: const float** vs float**
                              buffer.getArrayOfWritePointers());
}

void FaustEngine::compile(const juce::String& faustCode, CompileCallback cb)
{
    juce::Logger::writeToLog("FaustEngine::compile stub received:\n" + faustCode);
    std::string errorMsg;
    llvm_dsp_factory* f = createDSPFacotryFromString(   // BUG: typo in function name
        "dsp", faustCode.toStdString(), 0, nullptWhr, "", errorMsg, -1); // BUG: nullptWhr
    llvm_dsp*  f->createDSPInstance();  // BUG: invalid syntax
    dsp->init(sampleRate);              // BUG: sampleRate not in scope (member is sr)
    dsp->~llvm_dsp();                   // BUG: direct destructor call, wrong pattern
}

void FaustEngine::~FaustEngine() {
    delete activeDSP.load();
    deleteDSPFactory(factory);
}                                       // BUG: extra closing brace
```

## Section 3: Required External Knowledge

**libfaust API (`<faust/dsp/llvm-dsp.h>`):**
```
createDSPFactoryFromString(name, code, argc, argv, target, errorMsg, optLevel)
  → llvm_dsp_factory*  (nullptr on failure; errorMsg populated)
  argc/argv: compiler flags — pass 0/nullptr for defaults
  target: "" for native CPU; optLevel: -1 for default

factory->createDSPInstance() → llvm_dsp*
  Must be deleted before the factory is deleted.

dsp->init(int sampleRate)
  Call once after createDSPInstance, before compute().

dsp->buildUserInterface(UI* ui)
  Populates a UI object with parameter metadata. MapUI implements UI.
  After calling: MapUI::getParamCount(), getParamAddress(i), getParamMin/Max/Init(i).

dsp->compute(int count, float** inputs, float** outputs)
  Both inputs and outputs are non-const float**.
  JUCE getArrayOfReadPointers() returns const float** — use getArrayOfWritePointers()
  for both args (safe for in-place effect), or cast explicitly.

deleteDSPFactory(llvm_dsp_factory*)  — frees factory; call after all instances deleted
delete dsp                            — frees a DSP instance
```

**JUCE threading — `std::thread` vs `juce::Thread`:**
Use `std::thread` for one-shot fire-and-forget (one compile per user click).
Use `juce::Thread` (subclass, override `run()`) for persistent workers needing
cooperative cancellation via `threadShouldExit()`.
JIT compilation is one-shot: `std::thread` is the right tool.

**Atomic pointer swap — correct memory orderings:**
```cpp
// Compile thread (writer):
activeDSP.store(newDsp, std::memory_order_release);
ready.store(true,       std::memory_order_release);

// Audio thread (reader):
llvm_dsp* dsp = activeDSP.load(std::memory_order_acquire);
```
Release/acquire pairing ensures DSP initialisation writes are visible to the audio
thread before it calls compute(). Relaxed ordering is a data race on DSP internals.

Delete the old DSP pointer **on the compile thread after the swap** — never on the
audio thread. Heap deallocation is not real-time safe.

**Real-time safety — audio thread must not:**
- Allocate or free heap memory (`new`, `delete`, `malloc`, `free`)
- Lock a mutex (even uncontended)
- Make system calls, log, or do file I/O
The `acquire` atomic load is lock-free and RT-safe.

## Section 4: Constraints from COLLABORATION.md

Mode: **PAIR**. Claude Code drafts the reference; human reads it and writes the final
version. Reference is not committed verbatim.

Code style for this reference:
- Illustrative: prefer named intermediate variables over one-liners
- `// SUBTLE: [condition]` — wherever a correctness condition would surprise a reader
- `// TODO: VERIFY API: [function]` — any API call reasoned about rather than read from headers
- `// TODO: VERIFY: [claim]` — any factual claim that should be checked

Deliverable: `docs/audio_thread_example.md` with a short prose header then the code.
Do **not** touch `FaustEngine.cpp` or `FaustEngine.h`.

Pre-task protocol required: before writing code, state mode + reasoning, scope,
assumptions about libfaust API, then wait for confirmation.

## Section 5: Next Session Prompt

```
Read these files and nothing else before responding:
  docs/handoff_audiothread_task.md
  CLAUDE.md
  COLLABORATION.md
  host/Source/FaustEngine.h
  host/Source/FaustEngine.cpp

Your task is in docs/handoff_audiothread_task.md. Before writing any code, follow the
pre-task protocol from COLLABORATION.md Section 2: state the engagement mode and your
reasoning, state scope (what file you will produce, what it demonstrates), state
assumptions about the libfaust API, then wait for my confirmation.

Deliverable: docs/audio_thread_example.md — a reference implementation showing:
background-thread JIT compilation with std::thread, atomic pointer swap with correct
acquire/release orderings, and safe old-DSP cleanup off the audio thread. PAIR mode:
illustrative and clearly commented. Mark any libfaust API call you are reasoning about
rather than reading from installed headers with the VERIFY markers from
COLLABORATION.md Section 5.
```
