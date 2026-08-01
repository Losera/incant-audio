# PluginForge — Status  (2026-08-01)

Rewritten each session per COLLABORATION.md §5. Single writer, no merge conflicts.
Narrative history lives in git.

**Start a session with `/orient`**, not by reading this file top to bottom. It injects live
repo state, **the CI line**, this file's open sections, and a staleness banner if it falls
behind HEAD.

---

## Works — and how we know

**This session made a generated patch playable, then adversarially reviewed the work that
made it playable and fixed nine defects in it.** Phase 0 (`fc89754`) landed the note path;
an audit of that commit against the C++ tree, the prompt layer and Faust's own headers found
that it shipped with a hanging note, a false threading invariant, two comments asserting the
opposite of the code beside them, an untested plugin target, and a runaway watchdog whose
constant did not denote a time. All fixed, **every one seen failing first**. Five commits
pushed, CI green (run 30686483382).

- **A held note can be released.** *(`fb733a3`.)* `allNotesOff()` existed and was called from
  nowhere but the MIDI walk, so a note held across a transport stop or a `prepareToPlay`
  re-prepare left `*gate == 1` with no events left to clear it — the patch droned forever.
  `FaustEngine::silenceVoice()` brackets internally with `enterAudio()`/`exitAudio()`;
  `audioBusy` is a counter (`FaustEngine.h:203,:212`) so an extra holder from the message
  thread is what the drain guard is built to tolerate. **Red case measured:** with the call
  commented out the assertion reports rms **0.3806881** — full sustain; restored, 0.0000002.
- **`en.ar` is a one-shot, and it had made an assertion vacuous.** `envelopes.lib:85-86`
  triggers release "when the envelope value reaches 1" — the gate's level is ignored entirely.
  The key/vel corpus patch used it, so its "DECAYS after note-off" check passed **whether or
  not note-off worked**. Proved directly: with `reset()` broken that patch's gate stayed at 1
  and the output still fell to zero. Only `en.adsr` sustains. This is a trap for the
  instrument prompt, which advertises both side by side.
- **The instrument target is no longer untested, in two directions.** *(`31c0ebc`.)* Every
  console-app harness compiles with `PF_IS_SYNTH == 0` (JUCE defines `JucePlugin_*` only for
  plugin targets), so the synth build's traits were asserted by **nothing**.
  `OfflineSynthRenderTest` is the same corpus at `-DPF_IS_SYNTH=1`, asserting `acceptsMidi()`,
  the release tail and the bus layout **in both directions** so the two builds cannot silently
  swap. Separately, `check.sh` built `PluginForgeSynth` and **CI did not** — PF-029 inverted,
  invisible to `TestLadderRunsWhatCIRuns` because its universe is filtered to names ending in
  `Test` and its relation runs CI→ladder only. `TestBothPluginTargetsAreGated` closes it.
- **A bus-layout gate, where JUCE's default was `return true` for any layout.**
  *(`28d5bfc`.)* `juce_AudioProcessor.h:1329` accepted 5.1 while `kMaxChannels` is 2 and
  `scratch` is sized to it — the host was told yes and then got its dry input back. Red case:
  with the override stubbed to `return true`, three assertions fail.
- **A release assertion that was passing by luck.** `tailRms` was the last block only; with
  note-off at block 40 and a 0.2 s release, that window's *leading edge* sat 3.7 ms **before**
  the release finished. Now 80 blocks, averaged over the last four (0.384–0.427 s after
  note-off), threshold tightened 10% → **1%**, measured margin four orders.
- **ADR-020: a loud synth is not a broken one.** *(`b4058df`.)* `os.square * gain` at unity
  sits at `|y| == 1.0` forever by construction, so a latching runaway watchdog muted the first
  loud square-wave synth permanently. Detection is now split from response: `Latch` for
  effects, `Report` for instruments. The **limiter, not the mute**, is what bounds the output,
  so `Report` gives up a backstop and not the guarantee. `NonFinite` latches under both. In
  the same code, `overScaleRun` was one `int` shared across the channel loop, so
  `kRunawaySeconds` was not a time — **measured: mono 47 blocks, stereo 24**. Per-channel:
  47 and 47. `OutputGuardTest` 17 → **25 checks**.
- **The prompt guards cover a directory, not one filename.** *(`09786ec`.)* Both controls
  named `system_prompt.txt` exactly, so Phase 1's `instrument_prompt.txt` would have been born
  with no invariant checking while every gate stayed green. Generalised **before** the file
  exists. **Verified end to end:** the hook was fed a `Write` payload for
  `llm/prompts/instrument_prompt.txt` — a file that does not exist — containing
  `os.totallyFake`; it exits 2 and names the fabrication. Under the old regex it exited 0.
- **Suite:** `check.sh full` green, 16 rungs. `OfflineRenderTest` and `OfflineSynthRenderTest`
  **158 checks each, 0 failures**; `OutputGuardTest` 25/0; `StatePersistenceTest` 33/0; 68
  control-wiring tests.

### Carried forward from 2026-07-31 — the arity session

- **The host is stereo and the patch may not be.** *(PF-049 critical, PF-050 high — both
  found and fixed today.)* `process()` handed JUCE's channel array straight to
  `dsp->compute` with no bounds check. `juce_AudioSampleBuffer.h:342` returns the raw
  `channels` array and **`:441` null-terminates it** at index `numChannels`, so on a
  2-channel buffer `io[2]` is `nullptr` and `io[3]` is past the allocation. `faust -lang cpp`
  on `process = _,_,_;` emits `FAUSTFLOAT* output2 = outputs[2];` then `output2[i0] = ...` —
  **a null dereference on the audio thread**, reachable from the current corpus. Nothing in
  `host/Source/` called `getNumInputs`/`getNumOutputs`; `llm/faust_validator.py:6-16` only
  checks faust's return code. Now: `runCompile` rejects `numOuts < 1 || numOuts > 2 ||
  numIns > 2` before publishing, through the existing failure path, so it surfaces as a
  compile error the user can read. **Rejected, not clamped** — silently dropping channels 3+
  produces plausible-sounding wrong audio, which is harder to diagnose than a refusal.
- **A mono patch now reaches both speakers.** *(PF-050.)* `process()` routes mismatched arity
  through a scratch buffer **sized in `prepare()`, never in `process()`**, and duplicates a
  1-output patch to both channels. Faust only contracts in-place `compute()` when the arities
  agree, so 1-in/2-out could not stay in place. The equal-arity fast path is byte-identical to
  the old code: the common case pays nothing.
- **Seen failing, with numbers.** `OfflineRenderTest`'s corpus was twelve entries, **all
  2-in/2-out** — nothing had ever exercised this. Six arity cases added; before the fix
  **five failed**: the three over-wide patches went live with an arity the host cannot serve,
  and the two mono patches came out at `max |L-R|` of **0.73** and **0.25** (RMS L 0.212 /
  R 0.262 — oscillator left, dry signal right). After: **L == R exactly, `max |L-R| = 0.0`**,
  and all three over-wide patches refused at compile. `OfflineRenderTest` went to **124
  checks, 0 failures**. `test_the_real_audio_path_passes_its_own_hook` still green, so the
  new code satisfies `check_rt_safety.py`.
- **The audio rung stops being a lottery.** *(PF-046 fixed.)* It gated on
  `bench/results/results.json` — the file `level_quota` overwrites — so its verdict was a
  property of the last model draw. It now gates on `bench/ladder_corpus.json`: 19 frozen
  records from the 2026-07-31 ollama run, same schema so the oracle takes either file.
  **Verified rather than assumed:** the old gate pointed at a different archived draw fails 1,
  and a *different* patch (PF-032's noise gate); replacing `results.json` with that draw
  leaves the new gate at **16 passed, 0 failed, exit 0**. The 3 zero-input generators are kept
  in deliberately, so every run prints a standing reminder that the generative category sits
  outside the gate.
- **PF-045 is excluded from the ladder on purpose, and stays open.** The sawtooth+ADSR record
  fails `dc_offset` and `never_decays` because the patch passes `releaseTime * ma.SR / 1000.0`
  to `en.adsr`, whose times are in **seconds** (`/usr/share/faust/envelopes.lib:192-202`).
  The ladder gates the render path and the oracle — what it can control. Generation quality
  belongs to `check.sh quota`, which still writes `results.json` and still surfaces it.
- **The UI is untouched and proven so.** `tools/ui_iterate.sh`: **15 rendered, 0 broken,
  `no change`** against the committed reference after the DSP work.
- **Carried forward, unchanged:** the control-style selector and its in-place restyle
  (PF-047); Faust group capture (`ParamInfo::group`) — still captured, still nothing lays out
  by it; the render oracle's `tail()` burst probe and `never_decays` gate; the prompt
  regenerated under Faust 2.85.9; the noise floor is provider-side (PF-031); PF-012's
  cross-model comparison; pluginval at strictness 10; the full audio-path invariant list
  (PF-001/002/005/006/015/018/019/020/021/022/023).
- **Suite: `tools/check.sh audio` green end to end** — every rung, including `audio`.

---

## Broken — ranked

Registry with IDs, severity and discovery dates: `docs/BUGS.md`.

**1. An instrument can be played, but not generated and not heard.** *(unfiled, high —
the honest answer to "how are the synthesizer features developing".)* **Substantially
changed 2026-07-31/08-01.** The machinery now exists and is asserted: a second plugin target
`PluginForgeSynth` (`IS_SYNTH TRUE`, `NEEDS_MIDI_INPUT TRUE`, category `Instrument`), an
optional input bus, a MIDI walk inside the `enterAudio()` bracket, voice-contract detection
read off the compiled DSP, monophonic last-note-priority notes, and `OfflineSynthRenderTest`
— the same corpus compiled with `PF_IS_SYNTH=1`, which is the only harness that exercises the
instrument build's traits at all. 158 checks per build, green in CI (run 30686483382).

**Two things still block anything audible, and they are peers — neither alone is enough:**

- **Generation cannot emit an instrument.** `system_prompt.txt:14` mandates `hslider()` and
  `:7` forbids anything outside the stdlib block, which contains no `button`, `checkbox` or
  `nentry`. So the model has **no legal way to produce a `gate`**; without one the voice
  contract fails, `isInstrument()` stays false, and the MIDI walk never runs. An
  `instrument_prompt.txt` is forced — the effects prompt has ~300 tokens of slack.
- **There is no way to send it notes.** No MIDI hardware is attached (`aconnect -i` shows
  only `System` and `Midi Through`), and the editor has no on-screen keyboard, no
  computer-keyboard input and no piano roll. ⚠️ The idiomatic JUCE fix is **not RT-safe
  here**: `MidiKeyboardState` holds a `CriticalSection` (`juce_MidiKeyboardState.h:182`) and
  `processNextMidiBuffer` takes it (`.cpp:140`), which `processBlock`'s no-locks rule forbids.
  Needs a lock-free ring from the message thread instead.

- **It has never been in a DAW.** *(Requirement stated 2026-08-01: the generated instrument
  must take MIDI in any DAW.)* The traits are right — `IS_SYNTH TRUE`, `VST3_CATEGORIES
  Instrument`, `acceptsMidi()`, an input-optional bus — and `PluginForge Synth.vst3` builds.
  But `COPY_PLUGIN_AFTER_BUILD FALSE` (`host/CMakeLists.txt:22`) means it has **never been
  installed** (`~/.vst3` holds only Vital), never been scanned, never been loaded, and
  **pluginval is not on PATH** so the instrument target has never been validated at all. Four
  concrete gaps behind the requirement, in the order they will bite:
  1. **Monophonic.** A DAW user plays chords immediately; Phase 0 is one note by design.
  2. **Block-granularity MIDI** — ~10.7 ms jitter at 48 k/512. Audible on tight rhythmic
     parts. Sample accuracy means splitting `compute()` at event offsets.
  3. **`getTailLengthSeconds()` is a hardcoded 2.0 s.** Hosts use it to decide when to stop
     calling `processBlock`; a 5 s pad release is truncated, a plucked patch wastes CPU.
  4. **No MIDI CC → parameter mapping.** `docs/goals_and_next_steps.md:64` has this as a
     long-term item, and it is a different feature from note input — worth disambiguating
     before either is called done.

So the only playable instruments in existence are the four hand-written patches inside
`kInstruments`. The benchmark corpus's `generative` category is still scored by
`first_try_compiles` alone and still unmeasurable by the Python oracle
(`UnsupportedPatch`, `render_oracle.py:161-174`).

**2. "Refine" does not refine.** *(unfiled, high.)* `LoadMode::Iterate` is a **knob-value
retention policy** (`PluginProcessor.h:59-63`). Ticking the toggle changes nothing about the
LLM call — the subprocess argv is `--prompt <text>` and nothing else
(`PromptPanel.cpp:367-370`). The model has never seen the prior patch, so "add a resonance
control" regenerates from imagination and **replaces** the live DSP. The same shape sits in
the retry loop: `generate.py:133` feeds back compiler stderr but **never the failed code**.
`currentSource()` already exists (`PluginProcessor.h:137-148`) and nothing reads it into the
generation path. Three findings from this session's design pass, all load-bearing:
  - **`juce::ChildProcess` cannot write to a child's stdin.** The complete API is
    `juce_ChildProcess.h:35-110` — no write method, no stdin handle, any platform. The POSIX
    implementation creates one pipe and `dup2`s it onto the child's stdout/stderr only
    (`juce_SharedCode_posix.h:1096-1155`); stdin is inherited from the host process. So
    `generate.py --json` over stdin **would hang in a DAW**. The transport must be
    `--request-file <path>` on argv.
  - **A refine payload can exceed groq's request ceiling.** groq admits a request only when
    `prompt_tokens + max_tokens <= 8000` (`llm/providers.py:68-72`), `max_tokens` cannot go
    below 4096 (`providers.py:764-766` × `:274`), and the prompt already leaves ~311 tokens
    of estimated slack. Generated patches measure p50 242 / p90 527 / max 1705 chars. So the
    tail **413s non-retryably** on the default provider. ollama has no such wall.
  - **The HUMAN-OWNED prompt gate no longer exists.** COLLABORATION.md Revision 2 §9 retired
    the three-mode protocol on 2026-07-21 and `protect_human_owned.py` was deleted in
    `cf1d8e8`. `docs/ux_roadmap.md:62-68` still describes it — a document that outlived its
    mechanism, the failure class COLLABORATION.md §5 exists to prevent. **Fix or delete that
    line.** What still binds is the evidence rule: a prompt edit owes a benchmark statement.

**3. One generation defect is actually evidenced; the rest is sampling.** *(PF-024/PF-032,
high.)* Karplus-Strong's `recursion_cycle` is the only failure reproducing prompt *and* class
across runs — four archives. The sidechain compressor fails every run with a *different*
error. **Fixing anything else on the strength of one run is fixing noise.**

**4. The noise gate still renders silent.** *(PF-032's surviving half, high.)*
`ef.gate_stereo(threshold, attack, hold, release, _, _)` — the signal written into the
argument list where `misceffects.lib:159` documents `_,_ : gate_stereo(t,a,h,r) : _,_`. The
prompt rule targeting it measured as not working on ollama.

**5. PF-045 — `en.*` time units.** *(medium, open, deliberately not spent today.)* The
two-line fix is a unit annotation at `tools/gen_stdlib_block.py:143-146`, the precedent one
group above. It costs prompt headroom (~124 tokens) and owes a benchmark statement, and the
**last two prompt rules aimed at this same class measured as doing nothing on ollama.**
Recorded as a decision, not an oversight.

**6. The DAW still sees raw slots.** *(follow-up to PF-037, open, unfiled.)*

**7. Knob ordering is Faust's own.** *(PF-038 low.)* Alphabetical per group and per widget;
`ParamGridPanel` does not sort.

**8. The only fidelity instrument is not interpretable.** *(PF-041 high, PF-042 medium.)*

**9. The declared ollama model cannot hold its own prompt.** *(PF-043, medium.)*

**10. `score_efficacy.py --judge` spends quota and takes no lock.** *(unfiled.)*

---

## Assumed, never checked

**Two claims. The number did not move on 2026-08-01 either.** The session landed five commits
of instrument machinery, an ADR and a guard generalisation — all real, none of it evidence
about generation quality. Said plainly, twice running now, because the metric exists so that a
productive session cannot disguise itself as a measured one. Note the shape of the debt: both
claims are blocked behind instruments, and instruments are now blocked behind a prompt and a
keyboard rather than behind the audio path.

- **The efficacy pilot generalizes to nothing.** *(PF-011)* 125 generations ≈ 437k tokens ≈
  **2.2 days** on groq. Needs sharding — **or ollama**, which is unmetered.
- **Semantic fidelity is unmeasured.** *(PF-013)* Blocked **on the instrument, not on quota**
  — PF-041 and PF-042.

## Next three things

1. **`instrument_prompt.txt` + a router.** The first of the two things blocking every audible
   instrument. Must permit `button("gate")`, teach the `gate`/`freq`/`gain` naming contract
   (case-sensitive, matched against Faust's own `extractPaths`), carry the unit contract
   (`ve.moog_vcf` takes Hz while `ve.moogLadder`/`diodeLadder`/`korg35LPF` take 0–1;
   `en.adsr` times are seconds — PF-045), and **warn that `en.ar` is a one-shot**:
   `envelopes.lib:85-86` releases when the envelope reaches 1 and ignores the gate entirely,
   so it cannot sustain a note. Router is keyword-based and local — a third LLM call does not
   fit the 100 s budget. The guards already cover a new file (`09786ec`).
2. **An on-screen + computer keyboard, on a lock-free path.** The second blocker, and useless
   without item 1 — as item 1 is without this. `MidiKeyboardComponent` gives clicking *and*
   QWERTY (`setKeyPressBaseOctave`, `:145`) for free, but its state class locks, so events
   must cross to the audio thread through a `juce::AbstractFifo` ring drained inside the
   existing `enterAudio()` bracket. Red case: TSan, plus an assertion that the audio thread
   never touches the `CriticalSection`. A preset-phrase library (triad, arpeggio,
   progression) is worth more than a drawing UI for A/B work and should come before it.
3. *(evidence)* **Fix the judge before using it — PF-041 first.** Still the cheapest route to
   moving the `assumed` number, and it has now failed to move for two sessions running. Note
   the coupling that makes this cheaper than it was: instruments are the first thing this
   project generates where "did it do what was asked" has an objective answer — play note 45,
   assert 110 Hz — so the pitch gate is a semantic check the effects corpus could never offer.

**Displaced, deliberately: Make Refine carry the source.** It was item 2 and is still the
highest-leverage gap in the product (`docs/competitive_landscape.md:107-109`), but the two
instrument blockers above are each one session and together unblock the listening pass, the
`assumed` number and the whole `generative` fifth of the corpus. Sequence when it returns: one
live groq generation measuring `usage.prompt_tokens` at `max_tokens=3000` (settles whether the
token wall is real), then `--request-file` + `prior_source`, then the token pre-flight, then
feed the failed code back into the retry. Red case is `scenario15_refineCarriesTheSource` in
`EditorSessionTest` — **and its negative**, that a Fresh request does not carry it, or "always
send the source" passes while doubling every request.

## Waiting on you

1. **The EFFECTS listening pass.**
   `host/build/PluginForgeHost_artefacts/Debug/Standalone/PluginForge Host` is current. The
   input bus is **required** — with no audio going in you hear nothing regardless of the
   patch. The **VST3 has never been installed** — `COPY_PLUGIN_AFTER_BUILD FALSE`
   (`host/CMakeLists.txt:22`), and `~/.vst3` holds only Vital — so a DAW test needs the bundle
   copied into a scan path first. Provider: groq is active and fastest; ollama is free but
   **CPU-only until the box reboots** (the NVIDIA driver mismatch is still live).
   The *instrument* listening pass is not available yet — see Broken #1 for why.

2. **Sequencing call: fold the keyboard into the instrument-prompt session, or after it?**
   The 2026-07-31 ruling on instruments is now largely executed — `IS_SYNTH`,
   `NEEDS_MIDI_INPUT`, `acceptsMidi()`, the input-optional bus layout, the MIDI dispatcher
   inside the bracket and a generator path in the C++ harness all landed. Two items from that
   ruling remain: the prompt contract with a synth few-shot, and polyphony (deliberately
   deferred — Phase 0 is monophonic on purpose, so that when per-voice zone fan-out goes wrong
   there is a working baseline to bisect against). What is genuinely open is whether the
   keyboard ships alongside the prompt: **neither is useful without the other**, since a
   generated instrument with no way to play it is as silent as a keyboard with no instrument
   to drive.

3. **Requested and not yet planned: a piano roll.** Drawing chords needs a note grid *and a
   clock* — the Standalone has no host transport, so it would own its own, which is the part
   that makes this larger than a widget. Recommended split: preset audition phrases first
   (they make A/B comparison meaningful, which a hand-drawn phrase does not), drawing UI
   after. Not scoped yet.

4. **`docs/ux_roadmap.md:62-68` describes a gate that no longer exists** (Broken #2). One-line
   fix, but it is exactly the drift class this project keeps paying for.

5. **`bench/results/.prompt_baseline.json` is still untouched**, deliberately. It records
   `0.88` for the deleted pre-unification prompt. Replacing it should record a *spread*.

6. **`UDHR.md` is a 1-byte stub** and `IDEAS.md` is your idea dump — both untracked, both left
   alone.
