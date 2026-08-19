# Fix Plan — pushToFaust label mismatch & activeUI TOCTOU (compile/swap window)

Status: EXECUTED 2026-07-19. This document is the plan that was carried out; kept as the
reference explanation of the swap protocol now in `FaustEngine.cpp`.

## Symptom

First `ParamPoolTsanTest` run (2026-07-18): TSan-clean **PASS**, but ~1,100 libfaust lines of

```
ERROR : setParamValue 'gain' not found
ERROR : setParamValue 'cutoff' not found
```

clustered around every compile/swap transition, in runs of 30–60 consecutive failures — several
audio blocks per transition, not a single-instant glitch.

## Root cause — two related bugs in `FaustEngine::compile()`

**Bug 1 — callback fires too late (the label mismatch).** The old ordering was:

```
swap DSP → swap MapUI → ready=true → delete old DSP → delete old LLVM factory → cb(params)
```

`cb` is what runs `ParamPool::remap()` (wired in `PluginProcessor::loadFaustCode()`), i.e. the
new labels are only published *after* `ready=true` **and** after LLVM factory destruction, which
takes milliseconds. In that window `pushToFaust()` happily pushes the *previous* patch's labels
into the *new* DSP → MapUI rejects them → the error spam. The harness alternates a `gain` patch
with a `cutoff` patch, which is why the two names alternate in blocks.

**Bug 2 — no drain before mutating `activeUI` (the TOCTOU reported 2026-07-18).**
`ready=false` stops *new* entries into `process()`/`pushToFaust()`, but nothing waited for a call
already past its `isReady()` check. `activeUI = std::move(newUI)` then mutates `MapUI`'s internal
maps while the audio thread may still be inside `setParamValue()` — a real data race that TSan
simply hadn't caught in one 5s run. The same reasoning applies to `delete old` racing a
`compute()` still in flight.

## Fix

One coherent change to the swap protocol: a **drain guard** plus a **callback reorder**.

### Drain guard (`audioBusy`)

`FaustEngine` gains `std::atomic<int> audioBusy` and an `enterAudio()`/`exitAudio()` pair.
`processBlock()` brackets its entire use of the engine (both `pushToFaust()` and `process()`):

```cpp
if (!faustEngine.enterAudio()) return;   // fetch_add(1) THEN check ready — see below
paramPool.pushToFaust(faustEngine);
faustEngine.process(buffer);
faustEngine.exitAudio();
```

The compile thread, after `ready=false`, spins `while (audioBusy != 0) yield()` before touching
`activeDSP` / `activeUI`. Spinning is fine there — it is a detached background thread, never the
audio thread, and the wait is bounded by one audio callback.

**Memory-ordering (the SUBTLE part):** this is a classic two-flag store→load handshake (Dekker).
Audio thread: `audioBusy.fetch_add` then `ready.load`. Compile thread: `ready.store(false)` then
`audioBusy.load`. With anything weaker than `seq_cst` on those four operations, both threads may
observe the other's *old* value (store-load reordering) — audio sees `ready==true` while compile
sees `audioBusy==0` — and the race returns. Hence `seq_cst` on exactly that handshake; the
publish side (`ready.store(true)`, `exitAudio()`'s decrement) stays release as before.
Cost on the audio thread: two uncontended atomic RMWs per block — RT-safe, no locks, no
allocation.

### Callback reorder

New ordering in `compile()`:

```
ready=false (seq_cst)
drain: while (audioBusy != 0) yield
swap DSP (exchange, acq_rel)
swap MapUI (plain move — provably unobserved now)
swap factory
cb(params) → ParamPool::remap() publishes the NEW labels     ← moved before ready=true
ready=true (release)
delete old DSP, delete old factory                            ← after ready: shortest silence
```

Labels and DSP are now never observable in a mismatched state: any block that sees `ready==true`
sees both the new DSP and the new label buffer. The old objects are unreachable after the swap
and are deleted off the audio thread, after audio resumes, so the not-ready gap no longer
includes LLVM factory teardown.

Error paths (`cb({}, errorMsg)` before any swap) are unchanged.

## Verification protocol

1. Rebuild all targets — clean link.
2. Re-run `ParamPoolTsanTest`: expect PASS **and** `grep -c "setParamValue.*not found"` == 0
   (baseline was ~1,100).
3. `pytest -m "not integration"` — still 99 passed (regression check; no Python touched).
4. Brief Standalone launch to confirm runtime behavior unchanged (also verifies the
   generate.py path-resolution fix landed in the same session — see ADR-011 hardening).

## Result

Executed 2026-07-19 (`docs/collaboration_log.md`, retired and deleted 2026-07-27 — see
`git log -- docs/collaboration_log.md` for the entry).
