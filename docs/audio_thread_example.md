# Audio Thread Reference: libfaust JIT Swap in FaustEngine

**Status:** PAIR-mode reference. Read this; write the version that goes in `FaustEngine.cpp`.  
**API verified against:** Arch Linux faust 2.85.5 (`/usr/include/faust/dsp/llvm-dsp.h`, `/usr/include/faust/gui/MapUI.h`)

---

## The invariant

The audio thread calls `process()` on every block. The compile thread (triggered by the
editor button) creates a new DSP and swaps it in without pausing the audio thread.

Three things must always be true after the swap:
1. `activeDSP` points to an initialised `llvm_dsp` whose sample rate matches `sr`.
2. `activeUI` contains pointers into **that same DSP's** internal memory.
3. The old DSP is freed **off the audio thread** (heap free is not RT-safe).

---

## Reference implementation

```cpp
// ─── FaustEngine.h additions ────────────────────────────────────────────────
// Add to private section alongside existing atomic members:
//
//   std::atomic<llvm_dsp_factory*> pendingFactory { nullptr };
//   std::mutex                     compileMutex;   // guards compile() re-entrancy
//
// NOTE: compileMutex is held only on the compile thread; never on the audio thread.
// ────────────────────────────────────────────────────────────────────────────

// ─── compile() — compile thread ─────────────────────────────────────────────
void FaustEngine::compile(const juce::String& faustCode, CompileCallback cb)
{
    std::thread([this, code = faustCode.toStdString(), cb]() mutable
    {
        // SUBTLE: createDSPFactoryFromString is not thread-safe per the Faust docs.
        // compileMutex ensures only one compile runs at a time.
        std::lock_guard<std::mutex> lock(compileMutex);

        std::string errorMsg;
        llvm_dsp_factory* f = createDSPFactoryFromString(
            "dsp", code, 0, nullptr, "", errorMsg, -1);

        if (!f) { cb({}, errorMsg); return; }

        llvm_dsp* dsp = f->createDSPInstance();
        if (!dsp) { deleteDSPFactory(f); cb({}, "createDSPInstance returned null"); return; }

        dsp->init(static_cast<int>(sr));

        // Capture parameter metadata (min/max/default) for the ParamPool.
        ParamCapture capture;
        dsp->buildUserInterface(&capture);

        // Build the MapUI that the audio thread will use via setParamValue().
        // SUBTLE: newUI holds raw float* pointers into dsp's internal memory.
        // It is valid only as long as dsp is alive — they must be swapped together.
        MapUI newUI;
        dsp->buildUserInterface(&newUI);

        // ── Atomic swap ──────────────────────────────────────────────────────
        //
        // Step 1: mark not-ready so the audio thread skips compute() during the swap.
        // SUBTLE: store-release pairs with load-acquire in process(). This ensures
        // that the audio thread sees ready==false before reading activeDSP.
        ready.store(false, std::memory_order_release);

        // Step 2: swap the DSP pointer. acq_rel: acquire the old pointer value
        // (so we can safely delete it), release the new value to the audio thread.
        llvm_dsp* old = activeDSP.exchange(dsp, std::memory_order_acq_rel);

        // Step 3: swap the UI. This is NOT atomic.
        // It is safe here because ready==false ensures process() has returned (or
        // will return early) by the time we write activeUI. The audio thread only
        // reads activeUI through setParamValue(), which is also called before
        // process() in processBlock(). With ready==false, pushToFaust() also skips.
        // VERIFY: confirm processBlock() order: pushToFaust() then process(),
        //         and that pushToFaust() checks isReady() before calling setParamValue().
        activeUI = std::move(newUI);

        // Step 4: swap the factory.
        llvm_dsp_factory* oldFactory = factory;
        factory = f;

        // Step 5: mark ready. release pairs with acquire in process() and isReady().
        ready.store(true, std::memory_order_release);

        // Step 6: delete old DSP and factory OFF the audio thread.
        // SUBTLE: delete old AFTER ready=true so the audio thread is already using
        // the new DSP. The old pointer is dead from the audio thread's perspective
        // once the exchange (step 2) completed.
        delete old;
        if (oldFactory)
            deleteDSPFactory(oldFactory);

        cb(capture.params, "");
    }).detach();
}

// ─── process() — audio thread ────────────────────────────────────────────────
void FaustEngine::process(juce::AudioBuffer<float>& buffer)
{
    // load-acquire: pairs with ready.store(true, release) in compile thread.
    if (!ready.load(std::memory_order_acquire))
        return;

    // SUBTLE: activeDSP is loaded after the ready acquire. The acquire ordering
    // guarantees that all writes the compile thread did before storing ready=true
    // (including the activeDSP.exchange and activeUI move) are visible here.
    llvm_dsp* dsp = activeDSP.load(std::memory_order_relaxed);

    // In-place: JUCE getArrayOfWritePointers() is valid for both input and output
    // because the audio thread owns the buffer during processBlock().
    // Alternative: const_cast<FAUSTFLOAT**>(reinterpret_cast<const FAUSTFLOAT* const*>(
    //     buffer.getArrayOfReadPointers())) for a strict input→output copy.
    float** io = buffer.getArrayOfWritePointers();
    dsp->compute(buffer.getNumSamples(), io, io);
}
```

---

## What to verify before committing

- [ ] Confirm `compileMutex` is never locked on the audio thread.
- [ ] Confirm `pushToFaust()` checks `isReady()` before calling `setParamValue()`.
- [ ] Confirm `processBlock()` order: `pushToFaust()` then `process()`.
- [ ] Run under ThreadSanitizer (`-fsanitize=thread`) with concurrent compile + process.
- [ ] Test with sample-rate change mid-session (`prepare()` after a compile).

---

## What changes from the current FaustEngine.h stub

1. Add `std::mutex compileMutex` to prevent concurrent compiles (not RT — compile thread only).
2. Change `ready.load()` in `process()` to use `std::memory_order_acquire`.
3. Change `activeDSP.load()` in `process()` to `std::memory_order_relaxed` (safe: the acquire
   on ready already synchronised).
4. The `pendingFactory` field mentioned above is optional if `compileMutex` serialises compiles.
