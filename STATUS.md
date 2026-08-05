# PluginForge — Status  (2026-08-05)

Rewritten each session per COLLABORATION.md §5. Single writer, no merge conflicts.
Narrative history lives in git.

**Start a session with `/orient`**, not by reading this file top to bottom. It injects live
repo state, **the CI line**, this file's open sections, and a staleness banner if it falls
behind HEAD.

---

## Works — and how we know

**This session closed STATUS.md's own Next-three #1 — the keyboard widget — and found two
new things along the way that neither the build nor the ladder could have caught on their
own: a real generation-quality defect (found by ear) and a CI blind spot (found by reading
the actual failing run, not assuming green-locally means green-on-push).**

- **CI was red at HEAD for a reason unrelated to any of this session's other work, and is
  now confirmed green.** `gh run view 30969225647 --log-failed`: the pytest job aborted
  collection on `ModuleNotFoundError: No module named 'numpy'` inside
  `tests/test_pitch_gate.py`, taking all 538 selected tests down with it — a collection
  error, not one test failing. Cause: `.github/workflows/test.yml`'s pytest job only ever
  installed `requirements.txt`, never `numpy`/`scipy`, which `test_pitch_gate.py` needs at
  module scope (and transitively, via `bench/render_oracle.py`'s `scipy.io.wavfile`
  import). Local `tools/check.sh` stayed green throughout because numpy/scipy happen to
  already be installed on this machine — PF-029's gap, reproduced exactly as described.
  Fixed (`5948c48`, pushed): both packages added to that job's install step, not to
  `requirements.txt` — a test-only dependency, not a product one. **Confirmed independently
  via `gh run view`, not the CI badge alone**: `test` job 36s, `build-host` (full JUCE +
  TSan build) 24m6s, both green.
- **Router accuracy measured, not assumed — and the measurement's own limit is the
  finding.** `bench/router_accuracy.json` (uncommitted): all 19 `bench/ladder_corpus.json`
  entries classified by `llm/router.py`. The router's verdict matches the corpus's own
  hand-set `category` field 19/19. But the specified ground-truth method (compile, check
  for a declared voice contract) reads "effect" for all 19 by construction: every entry was
  generated 2026-07-31, and `llm/prompts/instrument_prompt.txt` +
  `llm/voice_contract.json` didn't exist until 2026-08-01/08-04 — the stored `code` came
  from a pipeline with no way to emit a voice contract regardless of what was asked. **This
  corpus cannot answer whether the router misroutes**, and the report says so rather than
  publishing a 15/19 "accuracy" figure that would really be measuring corpus vintage.
  `llm/router.py` and `tools/check.sh` unchanged, confirmed by `git diff --stat`.
- **The on-screen + computer-keyboard widget exists.** `host/Source/KeyboardPanel.{h,cpp}`
  (new) + `PluginEditor.{h,cpp}` wiring drive `PluginForgeProcessor::pushKeyboardNote()`
  through a `juce::MidiKeyboardState::Listener` on the message thread — the producer that
  was the entire remaining gap in the `NoteRing` path (queue + drain already built and
  TSan-proven, carried forward below). `juce_audio_utils` was already linked everywhere
  needed; no new dependency. **Verified independently, not just self-reported**:
  `tools/check.sh full` rerun by this session directly — all green, including the 4 new
  `EditorSessionTest` scenarios (17 round-trip, 18 last-note-priority through the new
  producer specifically, 19 held-note-across-a-swap, 20 effect-disables-keyboard) — and
  each new test confirmed to fail when the production (non-test) part of the diff is
  reverted.
- **A separate, fresh-context adversarial review caught what this session's own testing
  didn't.** A second session, given only the uncommitted diff and `CONTRACT.md`/
  `PARAM_CONTRACT.md` — no transcript, no task brief — reviewed it cold. It caught a
  deliberately planted defect (a hardcoded `juce::Colour(0xff9399b2)` in
  `KeyboardPanel.cpp:48` duplicating the `Theme::subtext` token, in the same file that uses
  the token correctly two lines above) with no hint given — the first live test of this
  project's "independent review pass" mechanism from `docs/sessions/005-multi-agent-safety-
  review.md`, and it passed the pilot's stated kill condition (a miss would have meant the
  mechanism isn't ready for anything). It also found two **real, independent** coverage
  gaps this session's own tests do not close: the QWERTY/computer-keyboard half of
  `NoteRing.h`'s "the on-screen keyboard and computer-keyboard input are the same producer"
  contract is never exercised by any of the 4 new tests (all drive `MidiKeyboardState::
  noteOn/noteOff` directly, which is documented as equivalent to a mouse click, not a typed
  key); and `setPlayable(false)`'s `allNotesOff(0)` call — which exists specifically to
  release a note still held when a patch flips from instrument to effect — is never tested
  under that exact condition. Neither is fixed yet; see Broken below.
- **The human listening pass passes, for now.** A live subtractive synth with an octaver
  was generated and played through the new keyboard successfully, in the product, not
  through `--capture --note`. Two things came out of it: the keyboard itself was flagged as
  visually plain (expected — no custom `LookAndFeel` exists anywhere in `host/Source`, see
  Broken below), and the octaver surfaced a real generation-quality defect, investigated and
  fixed the same session (next bullet).
- **A generation defect, found by ear, investigated before being fixed, and fixed with
  evidence.** The reported symptom ("the octaver was a checkbox with no parameters") was
  checked against live generations before anything was changed: 3 fresh groq runs of
  similar prompts showed 1/3 well-parametrized (a genuine 0–1 mix knob) and **2/3 worse than
  a checkbox** — the octave/sub-oscillator layer was summed into `process` unconditionally,
  with no control of any kind, adjustable or not. Fixed with one rule + one new few-shot
  example in `llm/prompts/instrument_prompt.txt` ("every added oscillator/texture layer
  needs its own hslider — never sum a second source in with nothing governing its
  presence"). Re-ran both previously-bad prompts after the edit: both now produce a
  dedicated mix knob per added layer. `tests/test_prompt_stdlib.py`: 15 passed / 2 skipped,
  same profile as before the edit, new example auto-covered by the generic
  few-shot-compiles check (`test_every_few_shot_example_compiles`) with no test file
  changes needed. Token headroom checked, not assumed: 2842 estimated tokens, **1062 of
  slack** remaining against the 8000-token / `max_tokens=4096` groq ceiling.
  `tools/check.sh audio` reran green afterward, including "render oracle over frozen ladder
  corpus" — the edit does not regress the existing corpus. **Not verified**: the 25-prompt
  quota-tier benchmark (`tools/check.sh quota`) was not re-run — this is a targeted 2-prompt
  before/after check, not a full regression sweep. See Assumed below.
- **ADR-022 accepted.** Generated-plugin visual identity stays a heuristic native-widget
  palette derived post-compile from facts Faust already exposes (labels, groups, the voice
  contract) — not a new LLM-emitted design artifact, not a WebView surface. Both would
  re-tread ground ADR-021 and ADR-019 already closed on evidence: ADR-021's own revisit
  trigger ("a non-zero result reopens this") was checked again this session against the
  same 19-entry corpus and has still not fired (`0/19 any_hgroup`, `0/19 any_style_knob`,
  `0/19 any_log_or_exp_scale`, from this session's own `check.sh full`/`audio` runs, twice).
  Written to `docs/decisions.md`; `CLAUDE.md`'s "Key architectural decisions" points at it.
- ⚠️ **Nothing above except the CI fix is committed.** `host/Source/KeyboardPanel.{h,cpp}`,
  `PluginEditor.{h,cpp}`, `CMakeLists.txt`, `EditorSessionTest.cpp`,
  `llm/prompts/instrument_prompt.txt`, `docs/decisions.md`, `CLAUDE.md`, and
  `bench/router_accuracy.json` all sit uncommitted on disk — verified, per COLLABORATION.md
  §3's evidence bar, but **not landed** per §4 ("pushed and green, not committed"). See
  Waiting on you.

### Carried forward from 2026-08-03 and earlier — see git log for the full narrative

The instrument-audit and arity-session evidence this file has carried for several rewrites
(pitch gate, `NoteRing`'s SPSC design and its overflow policy, `ParamIdentity`'s named-not-
numbered slugs, the stereo/mono arity fix, ADR-020's latch/report split) is unchanged this
session and is not re-derived here. Read `git log` or the prior revision of this file
(`git log -- STATUS.md`) for that record rather than trusting a summary of a summary.

---

## Broken — ranked

Registry with IDs, severity and discovery dates: `docs/BUGS.md`.

**1. The keyboard's QWERTY/computer-keyboard path is untested.** *(unfiled, medium — found
this session by the adversarial review, not fixed yet.)* `NoteRing.h`'s contract states the
on-screen keyboard and computer-keyboard input are the same producer; nothing exercises the
computer-keyboard half. A regression that made key-press handling call the forbidden
`MidiKeyboardState::processNextMidiBuffer()` would pass all 163 current checks undetected.

**2. `setPlayable(false)`'s held-note release is untested under the condition it exists
for.** *(unfiled, medium — found this session, not fixed yet.)* Hold a note on an
instrument, then load an effect while it's held — no test exercises this, though the
production code (`allNotesOff(0)` in `KeyboardPanel::setPlayable`) explicitly handles it.

**3. The keyboard, like the rest of the editor, has zero custom visual styling.**
*(unfiled, low-medium — restated by this session's own listening pass, not new.)* No
`LookAndFeel` subclass exists anywhere in `host/Source`; `Theme.h` supplies named
color/font tokens applied via `setColour()`, not custom-drawn widget geometry. Scoped
already in `docs/ui_design_plan.md` §2 as `ForgeLookAndFeel.h` back in session 002, marked
"not started," never resumed.

**4. It has never been in a DAW.** *(Unchanged.)* `COPY_PLUGIN_AFTER_BUILD FALSE`
(`host/CMakeLists.txt`) means the VST3 has never been installed or scanned, and pluginval
is not on PATH. Four concrete gaps behind "must take MIDI in any DAW": monophonic by
design, block-granularity MIDI (~10.7 ms jitter), a hardcoded 2.0 s tail length, no MIDI CC
mapping.

**5. "Refine" does not refine.** *(unfiled, high — unchanged this session.)*
`LoadMode::Iterate` is a knob-retention policy; the LLM never sees the prior patch.

**6. One generation defect is actually evidenced; the rest is sampling.** *(PF-024/PF-032,
high, unchanged.)* Karplus-Strong's `recursion_cycle` reproduces across four archives; the
sidechain compressor fails every run with a *different* error.

**7. The noise gate still renders silent.** *(PF-032's surviving half, high, unchanged.)*

**8. PF-045 — `en.*` time units on the frozen ladder record.** *(medium, open, deliberately
not spent.)*

**9. The DAW still sees raw slots.** *(follow-up to PF-037, open, unfiled.)*

**10. Knob ordering is Faust's own.** *(PF-038 low.)*

**11. The only fidelity instrument is not interpretable.** *(PF-041 high, PF-042 medium,
unchanged.)*

**12. The declared ollama model cannot hold its own prompt.** *(PF-043, medium, unchanged.)*

**13. `score_efficacy.py --judge` spends quota and takes no lock.** *(unfiled, unchanged.)*

---

## Assumed, never checked

**Two claims**, up from one — this session closed nothing on this list but added one.

- **The efficacy pilot generalizes to nothing.** *(PF-011, unchanged.)* 125 generations ≈
  437k tokens ≈ 2.2 days on groq. Needs sharding — or ollama, unmetered but CPU-only until
  the box reboots.
- **NEW: the layered-voice control-exposure prompt fix generalizes beyond the 2 prompts it
  was tested on.** The fix was verified on the exact 2 prompts that were seen failing before
  it, plus the render oracle staying green on the unrelated frozen corpus. It has not been
  run against a larger or independently-sampled batch of "add a second oscillator/layer"
  prompts, and the quota-tier benchmark was deliberately not spent to check this. Moves out
  of this list only when someone runs that batch.

## Next three things

1. **Close the two keyboard test-coverage gaps the adversarial review found.** A
   computer-keyboard (QWERTY) round-trip test through `MidiKeyboardComponent`'s actual
   key-press handling, not `MidiKeyboardState::noteOn/noteOff` directly; and a
   held-note-then-swap-to-effect test that would fail if `allNotesOff(0)` were removed from
   `KeyboardPanel::setPlayable`. Both gaps are named with exact triggers already — this is
   writing the tests, not diagnosing anything.
2. **Resume `ForgeLookAndFeel.h`.** Scoped in `docs/ui_design_plan.md` §2 and session 002's
   notes since 2026-07-19-ish, never built. Freshly motivated by an actual human listening
   pass calling the keyboard "not visually pleasing," not stale speculation. Bounded,
   non-architectural, tool-wide (not per-generation — that's ADR-022's separate, larger
   question).
3. *(evidence)* **PF-041/PF-042 — the spectral/timbral judge.** Unchanged. Still the thing
   standing between "compiles and plays the right note" and "sounds like what was asked
   for" for the instrument corpus.

**Displaced, not urgent — two items now, not one.** (a) *Make Refine carry the source* —
still the highest-leverage gap in the product per `docs/competitive_landscape.md:107-109`,
displaced again. (b) **NEW: a generation-refinement architecture-planning conversation.**
ADR-021 (2026-08-04) named "acceptance criteria — capturing what a generation was asked for
so the result can be checked against it" as a real, deliberately deferred, different-from-
PluginSpec need, and nothing has touched it since. This session's octaver investigation is
one data point in that direction, closed with a prompt fix rather than new architecture —
but the general question (does single-pass generation reliably meet stated intent, and
what would a refinement/critique pass cost) is still open and larger than one prompt edit.
Needs its own evidence-gathering pass — a batch of generations checked against what was
asked for — before deciding whether it needs a new mechanism at all.

## Waiting on you

1. **Commit today's uncommitted work, or hold it.** `KeyboardPanel.*`, the `PluginEditor`/
   `CMakeLists.txt`/`EditorSessionTest.cpp` wiring, `llm/prompts/instrument_prompt.txt`,
   `docs/decisions.md` (ADR-022), `CLAUDE.md`, and `bench/router_accuracy.json` are all
   verified but uncommitted, per this session's instruction not to commit without being
   asked. The adversarial review found two real, if minor, coverage gaps in the keyboard
   diff specifically (Broken #1, #2) — your call whether those get closed first or the
   commit happens now with them tracked as follow-ups.
2. **The instrument listening pass has moved from CLI-only to the Standalone, and it
   passed.** No longer "waiting" in the sense the last two rewrites meant it — the keyboard
   widget is what this item was blocked on, and it landed (pending the commit decision
   above). Still worth more listening as new instruments get generated, now that it costs a
   click instead of a CLI invocation.
3. **The EFFECTS listening pass.** Unchanged from prior rewrites.
   `host/build/PluginForgeHost_artefacts/Debug/Standalone/PluginForge Host` is current.
   Input bus required. VST3 still never installed (`COPY_PLUGIN_AFTER_BUILD FALSE`).
4. **Requested and not yet planned: a piano roll.** Unchanged. Needs a note grid *and* a
   clock (no host transport in Standalone). Recommended split: preset audition phrases
   first, drawing UI after.
5. **`docs/ux_roadmap.md:62-68` describes a gate that no longer exists.** Unchanged,
   one-line fix.
6. **`bench/results/.prompt_baseline.json` is still untouched**, deliberately. Records
   `0.88` for the deleted pre-unification prompt.
7. **`UDHR.md` and `IDEAS.md`** — still untracked, still yours, still left alone.
