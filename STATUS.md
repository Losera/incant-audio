# PluginForge — Status  (2026-08-03)

Rewritten each session per COLLABORATION.md §5. Single writer, no merge conflicts.
Narrative history lives in git.

**Start a session with `/orient`**, not by reading this file top to bottom. It injects live
repo state, **the CI line**, this file's open sections, and a staleness banner if it falls
behind HEAD.

---

## Works — and how we know

**This session closed the actual gap behind "instruments can be played, but not generated and
not heard" — measurement, not generation.** `/orient`'s digest opened pointing at "Next three
thing 1: `instrument_prompt.txt` + a router" as unbuilt; it was already built, one session
earlier (`d587665`, six commits behind where this session started, undocumented here because
that commit landed one commit *after* the last STATUS.md rewrite). What was still true and still
blocking: nothing in the repo could play a note at a generated instrument, so the prompt had
never produced a single measured patch. `--capture` built an empty `MidiBuffer` and reported
`rms=0.000000` as success. Fixed, and a new gate added on top of the fix.

- **`--capture` plays a note.** `runCapture` (`host/tests/OfflineRenderTest.cpp`) now reads
  `PluginForgeProcessor::isInstrumentForTest()` (new — the same forwarding idiom as
  `liveDspSampleRateForTest()`), and if the loaded patch declares the full voice contract, drives
  a MIDI note through the **same** `renderWithMidi()` the corpus battery uses rather than a second
  copy of the note loop — the file's own comment argues against that duplication, and a second
  copy is exactly the kind of thing that drifts and then agrees with itself about the wrong
  answer. `CAPTURE_OK` gained `instrument= note= vel= sr= held_end= held_rms= tail_rms=`, appended
  never reordered. **Effect capture is proven bit-identical**, not just argued: `md5sum` on three
  fixtures before/after matched exactly, and the diff on the first eight `CAPTURE_OK` fields is
  the WAV path only. `isInstrumentForTest()` also got its own assertion in the corpus loop, on
  **both polarities** — including the "sine with a `freq` knob but no gate" negative control,
  which is the direction that would otherwise make every effect capture start playing notes.
- **The first semantic gate this project has had.** Every existing check answers "is this patch
  broken" — NaN, silence, runaway gain, a dropped delay tail. None could tell a synth playing
  440 Hz from one playing 110 Hz; both are finite, non-silent, bounded audio. `bench/render_oracle.py`
  gained `pitch_of_wav()`: normalised autocorrelation with parabolic interpolation, **not** an FFT
  peak — verified directly, not assumed: on a signal built from harmonics 2–10 of 110 Hz with no
  110 Hz component at all, the FFT peak reads 220.03 Hz (wrong, an octave up — a saw or a
  high-passed tone routinely does this) while the autocorrelation estimator reads 110.09 Hz
  (correct). That comparison is now `TestEstimator::test_missing_fundamental_does_not_read_an_octave_up`
  in `tests/test_pitch_gate.py`, so the choice has a red case behind it, not just a docstring.
- **The red fixture, seen failing on the right axis and green on every other one.**
  `tests/fixtures/instrument_fixed_pitch.dsp` hardcodes `os.osc(440)` while referencing `freq`
  only as a filter corner (`fi.lowpass(1, freq*20)`) — the shape a model produces when it wires
  the envelope and forgets the pitch. Compiles, isn't silent, no NaN/Inf, sounds and releases —
  every gate that predates this one is green. The pitch gate reports **+2400.0 cents** against a
  **50-cent** tolerance (half a semitone; tolerates the ±15–20 cents of detune the corpus's
  "warm/analog" prompts legitimately ask for, and nothing legitimate lands between 50 and the next
  real failure at 100+).
- **Verified end to end on a real generation, not just fixtures.** First live generation ever run
  against `instrument_prompt.txt` (`d587665` shipped it eight commits ago and flagged this
  exact gap as the honest thing not yet verified): `llm/generate.py --prompt "a warm analog
  subtractive synth pad, playable from MIDI"` routed to `"kind": "instrument"`, compiled first
  try, and correctly used the Hz-ordered `ve.moog_vcf(res, cutoff)` (resonance first, Hz second —
  the unit contract the prompt teaches, distinct from `moogLadder`'s normalised 0–1). Captured and
  measured: **110.01 Hz against an expected 110.0, 0.13 cents of error, `in_tune=True`.** The FFT
  peak on that same render reads 112.06 Hz — the estimator choice above is not academic, it moved
  the number on the very first real patch it was run against.
- **`tools/check.sh assumed` went from 2 claims to 1.** "Semantic fidelity is unmeasured (PF-013)"
  is removed from Assumed below — not because all semantic fidelity is now measured (it isn't;
  see Broken #8, unchanged), but because the specific blocking claim was "there is no instrument
  to measure fidelity against," and there now is one, with a passing and a failing case both on
  record. This is the metric CLAUDE.md names as the one that cannot be improved by writing
  documentation; it moved by building a gate, not by asserting one.
- **Suite:** `check.sh full` green, 20 rungs (new: "pitch gate (note 45 -> 110 Hz through the
  capture binary)", marked `integration` so `fast`'s `-m "not integration"` excludes it honestly
  rather than silently skipping it — `level_fast` runs *before* `level_full`'s build step, so an
  unmarked test would skip once on a clean tree and never run again, the exact dead-control
  failure this file exists to stop recording after the fact).
  `check.sh audio` green. `OfflineRenderTest` **183 checks, 0 failures** (was 179).
  `tests/test_pitch_gate.py`: 8/8 (5 pure-numpy in `fast`, 3 `integration` needing the built
  binary).

### Carried forward from 2026-08-01 to 2026-08-03 — six commits this file never recorded

STATUS.md was last rewritten at `f260d53`, one commit before `d587665`. Everything below landed
in the gap `/orient`'s staleness banner was pointing at.

- **The instrument prompt and its router (`d587665`).** Two prompts, not one: the effects prompt
  had ~300 tokens of slack against groq's 8000-token per-request ceiling, and the voice contract
  plus a synth few-shot did not fit. `instrument_prompt.txt` (7,784 chars, its own generated
  stdlib profile — 19 curated entries vs the effects prompt's 46) teaches the `gate`/`freq`/`gain`
  contract, the Hz-vs-normalised filter split, and that `en.ar` is a one-shot. `llm/router.py` is
  keyword scoring, not an LLM call — a classifier round trip does not fit the 100 s budget and
  would fail exactly when the network is the problem — with 25 tests including the real
  `bench/prompts/prompts.json` 5/20 split. **Its own commit message flagged the gap this session
  closed**: not one live generation had been run against it, and there was still no way to send it
  notes or measure what came back.
- **A parameter is named, not numbered (`e7d7c20`).** `ParamIdentity.h` derives a slug from
  group+label instead of using ordinal position as identity, closing four defects that were one
  fact from four angles (PF-038 alphabetical knobs, raw slot numbers in the DAW, the forced-Fresh
  instrument-boundary hack, PF-051's 65th-control silent drop). State blob schema v2; v1 blobs fall
  back to positional restore. Versioned and pinned in the same commit that creates it
  (`ParamIdentityTest`, wired into `check.sh` and CI together).
- **The lock-free note path exists end to end (`a04c9e2`, `496c35e`).** `NoteRing.h` — SPSC,
  drop-newest-on-overflow (drop-oldest would need the producer to advance the consumer's cursor,
  which is a race, not a policy) — is drained **inline** inside `processBlock`'s `enterAudio()`
  bracket, because `check_rt_safety.py` scopes four function names and cannot follow a call graph
  to a helper. Two bugs found by writing the tests, both fixed in the same commit: the drain was
  originally inside the `isInstrument()` gate, so an *effect* patch never emptied the ring (it
  would fill to 255 and burst-fire on the next instrument the user generated — measured: 545
  drops with the bug reintroduced); and `NoteRing::reset()`'s comment claimed `prepareToPlay`
  called it, which would race a held key against a sample-rate change, and does not. Seen failing:
  with the drain loop deleted, every instrument in the corpus reports 0.0 rms from the keyboard
  against 0.39689–0.77979 from MIDI. **What this does NOT provide**: the on-screen/QWERTY widget
  itself. `pushKeyboardNote()`'s only caller anywhere in `host/Source/` is still the test corpus —
  confirmed by grep, not assumed. The queue exists; nothing produces into it from the UI yet.
- **The screenshot tool matched a terminal, not the plugin (`ca34955`).** Window selection was
  `'PluginForge' in title`, and a shell sitting in `~/PluginForge` has a matching title. Now
  matches on window class, which JUCE sets from the product name and no terminal can collide with.
- **A date in a filename is a claim, and it goes stale (`e0cd9a9`).** Living session docs move to
  `docs/sessions/NNN-topic.md`, numbered not dated. `check_doc_naming.py` blocks a new dated
  filename on `Write`; point-in-time records (`docs/records/`, `bench/results/`, `logs/`,
  `artifacts/`) are exempt because their date is content, not packaging.

### Carried forward from 2026-08-01 — the instrument audit session

Five commits, all seen failing before being fixed: a hanging note (`allNotesOff()` existed and
was called from nowhere), `en.ar`'s one-shot semantics making a corpus assertion vacuous
(`envelopes.lib:85-86`), the `PluginForgeSynth` build target going untested in both directions
(`OfflineSynthRenderTest` closes it), a bus-layout gate JUCE's own default left open (5.1 audio
into a 2-channel scratch buffer), a release-window assertion passing by luck (3.7 ms before the
release actually finished), ADR-020 splitting runaway *detection* from *response* so a loud
`os.square` synth stops being muted permanently, and the prompt guard hook generalised from one
filename to a directory glob **before** the file it would need to cover existed (`09786ec`).
`check.sh full` green, 16 rungs at the time; `OfflineRenderTest`/`OfflineSynthRenderTest` 158
checks each.

### Carried forward from 2026-07-31 — the arity session

- **The host is stereo and the patch may not be.** *(PF-049 critical, PF-050 high.)* A 3-output
  patch reached a null-terminated JUCE channel array past its bound — a null dereference on the
  audio thread, now rejected at compile with a readable error rather than silently truncated.
- **A mono patch now reaches both speakers**, through a scratch buffer sized in `prepare()`. The
  equal-arity fast path stays byte-identical.
- **Seen failing, with numbers.** Before the fix, mono patches came out at `max|L-R|` of 0.73 and
  0.25 (oscillator left, dry signal right); after, `L == R` exactly.
- **The audio rung stops being a lottery** *(PF-046)*: gates on a frozen 19-record corpus instead
  of whatever the last quota run happened to draw.
- **PF-045 stays open on purpose.** The sawtooth+ADSR ladder record still passes milliseconds
  where `en.adsr` wants seconds. Note this session's live instrument generation used
  `en.adsr` correctly, in seconds — the model gets it right when the prompt states it; PF-045 is
  about a specific frozen benchmark record, not about whether the contract can be taught.
- **Carried forward, unchanged:** the control-style selector, Faust group capture, the tail-check
  burst probe, the noise floor being provider-side (PF-031), PF-012's cross-model comparison,
  pluginval at strictness 10, the full audio-path invariant list.

---

## Broken — ranked

Registry with IDs, severity and discovery dates: `docs/BUGS.md`.

**1. An instrument can be played and generated and measured — but still not heard by a human, in
the product.** *(unfiled, high — narrower than it was a session ago.)* Three things used to block
this and they are no longer peers:

- ~~Generation cannot emit an instrument.~~ **Fixed, `d587665`.** `instrument_prompt.txt` +
  `llm/router.py` route real prompts to a prompt that can legally emit `button("gate")`, and a
  live generation this session compiled first try and played the correct pitch.
- ~~Nothing can measure whether a generated instrument works.~~ **Fixed, this session.**
  `--capture` plays a note; `pitch_of_wav()` judges it. See Works above.
- **There is still no way for a human to send it notes, inside the product.** *(Unchanged — this
  is now the whole of Broken #1.)* The lock-free path a keyboard would need exists and is proven
  (`NoteRing` + the inline drain in `processBlock`, carried forward above), but nothing produces
  into it: `pushKeyboardNote()`'s only caller in `host/Source/` is still the test corpus. No MIDI
  hardware is attached either (`aconnect -i` shows only `System` and `Midi Through`). A human can
  today reach a generated instrument's audio only through `--capture --note <n>` on the command
  line — real, but not the product experience. See **Waiting on you #2** for the exact commands.
  ⚠️ The idiomatic JUCE path is still not usable as-is: `MidiKeyboardState` holds a
  `juce::CriticalSection` (`juce_MidiKeyboardState.h:182`), forbidden on the audio thread.

- **It has never been in a DAW.** *(Unchanged.)* `COPY_PLUGIN_AFTER_BUILD FALSE`
  (`host/CMakeLists.txt:22`) means the VST3 has never been installed or scanned, and pluginval is
  not on PATH. Four concrete gaps behind "must take MIDI in any DAW": monophonic by design,
  block-granularity MIDI (~10.7 ms jitter), a hardcoded 2.0 s tail length, no MIDI CC mapping.

**2. "Refine" does not refine.** *(unfiled, high — unchanged this session.)* `LoadMode::Iterate`
is a knob-retention policy; the LLM never sees the prior patch. Three load-bearing findings from
the earlier design pass stand: `juce::ChildProcess` cannot write to a child's stdin (needs
`--request-file`), a refine payload can exceed groq's request ceiling non-retryably, and
`docs/ux_roadmap.md:62-68` still describes a HUMAN-OWNED gate that `cf1d8e8` deleted — fix or
delete that line.

**3. One generation defect is actually evidenced; the rest is sampling.** *(PF-024/PF-032,
high, unchanged.)* Karplus-Strong's `recursion_cycle` reproduces across four archives; the
sidechain compressor fails every run with a *different* error.

**4. The noise gate still renders silent.** *(PF-032's surviving half, high, unchanged.)*

**5. PF-045 — `en.*` time units on the frozen ladder record.** *(medium, open, deliberately not
spent.)* See the carried-forward note above: this is about one archived benchmark patch, not
about whether the unit contract can be taught — this session's live instrument generation used
`en.adsr` correctly.

**6. The DAW still sees raw slots.** *(follow-up to PF-037, open, unfiled.)*

**7. Knob ordering is Faust's own.** *(PF-038 low.)*

**8. The only fidelity instrument is not interpretable.** *(PF-041 high, PF-042 medium,
unchanged.)* Distinct from this session's pitch gate: PF-041/042 are about the general
spectral/timbral "does it sound like what was asked" judge for the *effects* corpus, which this
session did not touch. The pitch gate is narrower — one objective, numeric property (fundamental
frequency) for the *instrument* corpus — and does not generalize to "is this musical."

**9. The declared ollama model cannot hold its own prompt.** *(PF-043, medium, unchanged.)*

**10. `score_efficacy.py --judge` spends quota and takes no lock.** *(unfiled, unchanged.)*

---

## Assumed, never checked

**One claim, down from two.** "Semantic fidelity is unmeasured" is removed — see Works above for
what moved it and what it does and does not mean. Do not read this as "assumed" being close to
empty: it tracks claims nobody has checked, not claims nobody has thought of, and the pitch gate's
own honest limit (Broken #8) is a reminder that a narrow win here is not a broad one.

- **The efficacy pilot generalizes to nothing.** *(PF-011, unchanged.)* 125 generations ≈ 437k
  tokens ≈ 2.2 days on groq. Needs sharding — or ollama, which is unmetered (CPU-only until the
  box reboots; the NVIDIA driver mismatch is still live).

## Next three things

1. **The on-screen + computer-keyboard UI widget.** The lock-free path it needs — `NoteRing`,
   drained inline inside `enterAudio()`/`exitAudio()` — is built and TSan-proven
   (`a04c9e2`/`496c35e`). What is missing is strictly the producer: something in
   `host/Source/PluginEditor.*` that calls `pushKeyboardNote()`. `MidiKeyboardComponent` gives
   clicking and QWERTY (`setKeyPressBaseOctave`) for free but its underlying state class locks —
   drive it through a listener that forwards into the ring rather than letting it touch
   `processBlock` directly. This is the last piece between a generated instrument and a human
   hearing it played live in the product, rather than through `--capture --note <n>` on the CLI.
   A preset-phrase library (triad, arpeggio, progression) is worth more than a drawing UI for A/B
   work and should land first.
2. **The two follow-ups this session deliberately deferred.** (a) Route `render_oracle.py`'s
   zero-input `UnsupportedPatch` through the capture binary instead of refusing outright — blocked
   on deciding what `features()`/`tail()` report when there is no input signal to compare against,
   a design question, not wiring. (b) Extend `test_prompt_headroom.py`, `test_prompt_claims.py`,
   `test_project_structure.py` and `bench/check_prompt_regression.py` to cover
   `instrument_prompt.txt` — real gap (~1580 tokens of unguarded slack, currently fine, silently
   unguarded), not a live defect, and belongs with the other prompt-guard generalisations as one
   tidy commit.
3. *(evidence)* **PF-041/PF-042 — the spectral/timbral judge.** Still the next slice of the same
   debt the pitch gate paid down a piece of. Reframe from last session: the `assumed` number did
   move this session, via the pitch gate rather than this item — this is not "the cheapest route"
   anymore in the same sense, but it is still open, still unfiled as a fix, and still the thing
   standing between "compiles and plays the right note" and "sounds like what was asked for" for
   the instrument corpus specifically (the effects corpus's version of this question is Broken #8
   unchanged).

**Displaced, not urgent: Make Refine carry the source.** Was item 2 two sessions running; the
instrument work it was displaced for is now down to one item (the keyboard widget, above) instead
of two. Still the highest-leverage gap in the product per `docs/competitive_landscape.md:107-109`.
Sequence when it returns: one live groq generation measuring `usage.prompt_tokens` at
`max_tokens=3000`, then `--request-file` + `prior_source`, then the token pre-flight, then feed
failed code back into the retry. Red case: `scenario15_refineCarriesTheSource` in
`EditorSessionTest`, and its negative (a Fresh request must not carry it).

## Waiting on you

1. **The EFFECTS listening pass.** Unchanged from last session.
   `host/build/PluginForgeHost_artefacts/Debug/Standalone/PluginForge Host` is current. Input bus
   is required. VST3 still never installed (`COPY_PLUGIN_AFTER_BUILD FALSE`). groq is active and
   fastest; ollama is free but CPU-only until reboot.

2. **NEW: an INSTRUMENT listening pass now exists — CLI only, not yet in the Standalone.**
   ```
   python llm/generate.py --prompt "<what you want>"          # routes itself; look for "kind":"instrument"
   # paste faust_code to a .dsp, then:
   host/build/OfflineRenderTest_artefacts/Debug/OfflineRenderTest --capture in.dsp out.wav [--note N]
   ```
   Default note is 45 (A2, 110 Hz), held ~1.7 s then released. `CAPTURE_OK` reports
   `instrument=1 held_end=... held_rms=... tail_rms=...`; play the WAV. This is real audio through
   the shipping `PluginForgeProcessor::processBlock` path (JIT, swap protocol, ParamPool,
   OutputGuard all run exactly as in a DAW) — it is a genuine listening pass, just not yet reachable
   by clicking anything. The pad generated live this session
   (`import("stdfaust.lib"); ... ve.moog_vcf(res, cutoff) ...`, 7 knobs, in tune at note 45 to
   0.13 cents) is the first candidate worth your ears.

3. **Sequencing call on the keyboard is narrower now, not open.** Both things it was waiting on —
   the prompt and the note-queue plumbing — are done. What's left is purely the UI widget (Next
   three #1). No decision needed from you here anymore; flagging that this item can come off the
   list once the widget lands.

4. **Requested and not yet planned: a piano roll.** Unchanged. Needs a note grid *and* a clock (no
   host transport in Standalone). Recommended split: preset audition phrases first, drawing UI
   after.

5. **`docs/ux_roadmap.md:62-68` describes a gate that no longer exists** (Broken #2). Unchanged,
   one-line fix.

6. **`bench/results/.prompt_baseline.json` is still untouched**, deliberately. Records `0.88` for
   the deleted pre-unification prompt.

7. **`UDHR.md` and `IDEAS.md`** — still untracked, still yours, still left alone.
