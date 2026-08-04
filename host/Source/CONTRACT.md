# host/Source/ Contract — the audio-thread closure

`docs/audio_thread_example.md` predates 2026-08-03; re-derived from source.

## The closure (PluginProcessor.cpp:146-281, `processBlock`)
Everything the audio thread touches sits inside one bracket:
`faustEngine.enterAudio()` (:155) ... `pushToFaust`(:266) ...
`faustEngine.process(buffer)`(:267) ... `outputGuard.process(buffer)`(:275)
... `faustEngine.exitAudio()`(:277). No allocation, lock, log, or I/O
anywhere in this closure (`check_rt_safety.py` enforces it, but only by
NAME on 4 functions — FaustEngine.h says it cannot follow a call graph).
`enterAudio()` false means a compile is mid-swap: input passes through
untouched, MIDI is DROPPED (:160-165) — SPECIFIED, not an accident.

## Compile-to-swap protocol (FaustEngine.cpp:900-961, `runCompile`)
Off the audio thread entirely — the compile worker. Seven steps: (1)
`ready=false` seq_cst — no NEW audio section may start; (2) spin-drain
`audioBusy` to 0 — waits out in-flight sections; (3) swap `activeDSP`; (3a)
publish `VoiceControls`; (3b) publish arity; (4) swap `MapUI`; (5) swap the
factory and call `cb()` — labels publish HERE, before ready; (6)
`ready=true` release; (7) delete the old DSP/factory. Steps 1-2 are a
Dekker store→load handshake with `enterAudio()` (FaustEngine.h:261-270) —
weaker than seq_cst lets both threads observe stale values.

## Message-thread / audio-thread split
`PluginProcessor`'s meta state (`currentFaustSource`, `currentSlotIds`, …)
is `metaMutex`-guarded, NEVER touched by the audio thread. `ParamPool` is
double-buffered (`remap()`/`pushToFaust()`, ParamPool.cpp) so the compile
thread writes the INACTIVE buffer and release-publishes the index — no
lock on the read side. `OutputGuard::reset()`/`setRunawayPolicy()` run on
the compile thread ONLY inside the audioBusy-drained window (Step 5/3),
never elsewhere — the one place writing guard state isn't a race, and it
is undocumented anywhere but the call sites themselves.

## Violations
- The audioBusy drain (`while (...) yield()`, :911) is an unbounded spin
  from the compile thread's side — bounded in practice by "one audio
  callback", not by any timeout. UNTESTED under adversarial block sizes.
- `check_rt_safety.py` cannot see `OutputGuard::process` is reachable from
  `processBlock` via a member call — it scans by function NAME only.
