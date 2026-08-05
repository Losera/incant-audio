# PluginForge — Status  (2026-08-05)

Rewritten each session per COLLABORATION.md §5. Single writer, no merge conflicts.
Narrative history lives in git.

**Start a session with `/orient`**, not by reading this file top to bottom. It injects live
repo state, **the CI line**, this file's open sections, and a staleness banner if it falls
behind HEAD.

---

## Works — and how we know

**This session ran four briefs (three parallel, one sequenced) as a live trial of the
touches/depends/provides parallel-safety mechanism session 005 left as an open decision,
closed all three remaining STATUS.md Broken items about the keyboard widget, shipped
`ForgeLookAndFeel.h`, and moved one Assumed claim into Works with a measured number.**

- **`ForgeLookAndFeel.h` exists and is wired in.** `host/Source/ForgeLookAndFeel.h` (new,
  header-only per the seven-`target_sources`-lists constraint in `host/CMakeLists.txt`),
  installed via `setLookAndFeel(&lnf)` in `PluginForgeEditor`'s constructor (never
  `setDefaultLookAndFeel`), `lnf` declared before the three child panels for correct
  destruction order, `setLookAndFeel(nullptr)` as the first line of `~PluginForgeEditor()`.
  Colour-scheme-first: every candidate `LookAndFeel_V4`/`LookAndFeel_V2` virtual was read
  against its real JUCE 7.0.9 implementation before deciding whether to override it — most
  are already flat and `findColour()`-driven, so a `Theme::`-built `ColourScheme` covers
  them; `getLabelFont`/`drawLabel` were deliberately left alone because overriding them
  would stomp B1's per-label `Theme::Type` calls. Only two virtuals overridden
  (`getTextButtonFont`, `createSliderTextBox`), both purely typographic gaps `ColourScheme`
  can't reach. **Verified, not self-reported**: `tools/check.sh full` run twice
  independently (once by the building agent, once by this session directly after merge) —
  both green, 25/25 steps, `EditorSessionTest` passing with no leaks. `tools/ui_iterate.sh`
  run without `--accept`: the only geometry diff (+72px on every fixture) traced to
  pre-existing baseline staleness from the keyboard widget landing after the last accepted
  baseline (`3ed3dbc`, 2026-07-31, vs. `e64867f`, 2026-08-05) — unrelated to this change,
  not accepted as a side effect. Visually confirmed by reading the rendered gallery PNGs:
  dark Catppuccin palette, teal/pink two-tone knobs and sliders reusing the existing
  level-meter gradient motif, styled tick box — not stock JUCE grey. Closes Broken #3 and
  Next-three #2 below.
- **Both keyboard test-coverage gaps the previous session's adversarial review found are
  closed, one with a genuine red-case proof.** `EditorSessionTest.cpp` scenarios 21 and 22
  (163 → 176 checks). Scenario 21 proves `KeyboardPanel::setPlayable(false)`'s
  `allNotesOff(0)` actually releases a note held across an instrument→effect swap — not by
  inspecting audio, but by ring-occupancy counting (`NoteRing`'s 255-slot fill loop; the
  release either occupies one slot before the loop starts or it doesn't, and the loop is
  sized to distinguish the two). **Genuinely seen red**, not just written and trusted:
  `allNotesOff(0)` deleted from `KeyboardPanel.cpp:86`, rebuilt, scenario 21 failed (176
  checks / 2 failures, both "got 0" where 1 was expected); line restored, rebuilt, passed
  again (176/0); `git diff` on `KeyboardPanel.cpp` confirmed empty afterward. Scenario 22 is
  honestly scoped, not oversold: a true keypress-to-note round trip needs a synthetic-input
  tool this machine doesn't have (wtype/ydotool/xdotool all absent), so it checks the
  *static* contract instead — `setKeyPressBaseOctave(4)` against `setAvailableRange(36,
  96)` against JUCE's own default QWERTY offset table — with an explicit "NOT COVERED" block
  stating real keypress firing is untested here and untested anywhere else in this repo.
  Closes Broken #1 (partially — see Broken below, the remainder is named honestly) and
  Broken #2 (fully). `tools/check.sh full`: green, 25/25 steps.
- **The layered-voice control-exposure fix (`bc061ff`) was checked against a new,
  independently-sampled batch and holds: 10/10.** `bench/check_layered_voice_generalization.py`
  (new) ran 10 new layered-voice instrument prompts — distinct in both primary-voice type
  (pad/bell/pluck/bass/organ/lead/brass/chiptune) and added-layer type (unison,
  sub-oscillator, noise texture, octave-up, drawbar harmonic, ring-mod, fifth harmony) from
  the 2 prompts `bc061ff` itself was verified on — through the real product code path
  (`generate.generate_faust`, `kind` pinned to `instrument` every attempt) against
  groq/`openai/gpt-oss-120b`, the same free provider `bc061ff`'s own verification used.
  10/10 compiled and exposed a dedicated `hslider` for every added layer, checked
  mechanically against a detector derived from `bc061ff`'s actual diff (not its commit
  message) — split on top-level `+` to correctly attribute a control to its own layer even
  when two layers share one summing statement, the normal rule-compliant shape. Two real
  bugs in the detector itself (inline unnamed-layer statements; a composite layer built from
  a sum of primitives) were found and fixed by reading its output against the actual
  generated code before trusting the number, re-derived offline with no new LLM calls; the
  corrected result was then independently spot-checked by hand against one generation's raw
  Faust source (the brass-synth entry: `os.sawtooth(freq*0.5) * subMix`, a genuine
  dedicated-control sub-oscillator the original detector missed only because it was inline
  in the `process =` statement). **Narrowed, not unconditional**: single provider/model
  (groq/gpt-oss-120b — gemini and local ollama untested), only explicit "add a
  layer/doubler/sub-osc" phrasing, one added layer per prompt, all instrument-routed. Moved
  out of Assumed into this section — see `bench/results/layered_voice_generalization_20260805_172721.json`.
- **Mechanism A (session 005's touches/depends/provides parallel-safety proposal) got its
  first live trial, not another proposal.** Four briefs this session each declared
  `touches`/`depends`/`provides` up front; three ran in parallel (the ForgeLookAndFeel
  work above, the layered-voice study above, a doc-truth sweep below), one was sequenced
  by hand specifically because no `CONTRACT.md` covers `PluginEditor` and the keyboard-test
  brief's `Session` helper constructs one in every scenario — the exact "an area with no
  written contract is not parallel-safe by declaration alone" case session 005 §1
  predicted. All four declared `touches` sets matched what actually landed exactly (one
  brief over-declared harmlessly — it named a file that turned out to already be correct).
  Full writeup, including the explicit limits of a 4-brief single-session sample and a
  scoped recommendation for a real adversarial trial, in
  `docs/sessions/006-multi-agent-trial-results.md`. **Does not amend COLLABORATION.md** —
  session 005 §11 stays a draft; this is data point one.
- **Four stale claims in this file corrected against the actual repo state**, not
  re-trusted from the prior rewrite: the "uncommitted work" claim (contradicted by four
  already-landed, already-pushed commits), Broken #13's "takes no lock" half (the lock is
  at `bench/score_efficacy.py:558,569`), two citations pointing `ForgeLookAndFeel.h`'s scope
  at the wrong document section, and a `Waiting on you` item asking for a `ux_roadmap.md`
  fix an earlier commit (`5090b55`) had already made. This project's named recurring defect
  is a claim that outlived the thing it described — four instances of it, found by rereading
  rather than recomputing, are fixed inline above and in Broken/Waiting below.

### Carried forward from 2026-08-04 and earlier — see git log for the full narrative

CI-red diagnosis and fix (`5948c48`), router accuracy measurement and its corpus-vintage
limit, the on-screen + computer-keyboard widget landing, the adversarial-review pilot that
found the two coverage gaps closed above, the octaver generation-defect fix, and ADR-022's
acceptance are all unchanged this session and not re-derived here. Read `git log -- 
STATUS.md` for that record rather than trusting a summary of a summary. Everything further
back (pitch gate, `NoteRing`'s SPSC design, `ParamIdentity`'s named-not-numbered slugs, the
stereo/mono arity fix, ADR-020's latch/report split) is unchanged and carried the same way.

---

## Broken — ranked

Registry with IDs, severity and discovery dates: `docs/BUGS.md`.

**1. The keyboard's QWERTY/computer-keyboard path has a static-contract test, not an
end-to-end one.** *(unfiled, medium — narrowed this session, not fully closed.)* Scenario
22 proves `setKeyPressBaseOctave(4)` and `setAvailableRange(36, 96)` agree with each other
and with JUCE's default QWERTY offset table. It does **not** prove a real keypress produces
a note — no synthetic-input tool exists on this machine (wtype/ydotool/xdotool all absent),
and this remains true everywhere else in the repo too. A regression that broke real keypress
handling while leaving the static constants untouched would still pass all 176 current
checks undetected. Closing this fully needs either a compositor-level input tool on this
machine or a different verification strategy entirely.

**2. It has never been in a DAW.** *(Unchanged.)* `COPY_PLUGIN_AFTER_BUILD FALSE`
(`host/CMakeLists.txt`) means the VST3 has never been installed or scanned, and pluginval
is not on PATH. Four concrete gaps behind "must take MIDI in any DAW": monophonic by
design, block-granularity MIDI (~10.7 ms jitter), a hardcoded 2.0 s tail length, no MIDI CC
mapping.

**3. "Refine" does not refine.** *(unfiled, high — unchanged this session.)*
`LoadMode::Iterate` is a knob-retention policy; the LLM never sees the prior patch.

**4. One generation defect is actually evidenced; the rest is sampling.** *(PF-024/PF-032,
high, unchanged.)* Karplus-Strong's `recursion_cycle` reproduces across four archives; the
sidechain compressor fails every run with a *different* error.

**5. The noise gate still renders silent.** *(PF-032's surviving half, high, unchanged.)*

**6. PF-045 — `en.*` time units on the frozen ladder record.** *(medium, open, deliberately
not spent.)*

**7. The DAW still sees raw slots.** *(follow-up to PF-037, open, unfiled.)*

**8. Knob ordering is Faust's own.** *(PF-038 low.)*

**9. The only fidelity instrument is not interpretable.** *(PF-041 high, PF-042 medium,
unchanged.)*

**10. The declared ollama model cannot hold its own prompt.** *(PF-043, medium, unchanged.)*

**11. `score_efficacy.py --judge` spends quota.** *(unfiled, unchanged.)* Takes a lock
(`bench/score_efficacy.py:558,569`); the quota cost itself is the remaining, real half.

---

## Assumed, never checked

**One claim**, down from two — this session moved the layered-voice claim into Works with
a measured 10/10 (see above) and added nothing new in its place.

- **The efficacy pilot generalizes to nothing.** *(PF-011, unchanged.)* 125 generations ≈
  437k tokens ≈ 2.2 days on groq. Needs sharding — or ollama, unmetered but CPU-only until
  the box reboots.

## Next three things

1. *(evidence)* **PF-041/PF-042 — the spectral/timbral judge.** Unchanged. Still the thing
   standing between "compiles and plays the right note" and "sounds like what was asked
   for" for the instrument corpus. `render_oracle.py`'s `features()` already computes
   `band_gain_db` and `centroid_shift_oct` for every render — nothing consumes them for a
   verdict yet. The gap is a missing consumer, not a missing measurement.
2. **Make Refine carry the source.** Promoted from "displaced" — repeatedly named the
   highest-leverage gap in the product (`docs/competitive_landscape.md:107-109`) and
   repeatedly bumped. `LoadMode::Iterate` today is a knob-retention policy; the LLM never
   sees the prior Faust source, so "refine" cannot mean what a user expects it to mean.
3. **Get it into a DAW.** Broken #2, long-standing, blocks the one validation this project
   cannot claim yet: does a generated plugin actually load and behave in a real host. Needs
   `COPY_PLUGIN_AFTER_BUILD` on, `pluginval` installed, and the four MIDI-fidelity gaps
   named in Broken #2 at minimum triaged, not necessarily fixed, before a first real scan.

**Displaced, not urgent.** **A generation-refinement architecture-planning conversation.**
ADR-021 (2026-08-04) named "acceptance criteria — capturing what a generation was asked for
so the result can be checked against it" as a real, deliberately deferred, different-from-
PluginSpec need. The octaver investigation and this session's layered-voice study are two
data points in that direction, both closed with prompt fixes rather than new architecture —
the general question (does single-pass generation reliably meet stated intent, and what
would a refinement/critique pass cost) is still open and larger than either prompt edit.
Also displaced: `docs/sessions/006-multi-agent-trial-results.md`'s recommendation for a
second, adversarial Mechanism A trial (one where a `touches`-disjoint-but-actually-coupled
brief pair is run in parallel for real and either catches or misses something), before any
`PreToolUse` hook gets built on today's single, favorable-but-thin sample.

## Waiting on you

1. **Commit today's work, or hold it.** `host/Source/ForgeLookAndFeel.h` (new),
   `host/Source/PluginEditor.{h,cpp}`, `host/tests/EditorSessionTest.cpp` (scenarios 21-22),
   `bench/check_layered_voice_generalization.py` (new) + its results JSON, this file's four
   corrections plus this rewrite, and `docs/sessions/006-briefs/*` +
   `docs/sessions/006-multi-agent-trial-results.md` are all verified (`tools/check.sh full`
   green, red-case-proven where applicable) but sit uncommitted on disk pending your
   decision — per this session's plan, that decision is yours before anything gets pushed.
2. **The keyboard widget now has a styled look-and-feel to react to.** Open
   `artifacts/ui_gallery/index.html` (or the two PNGs cited in Works above) and confirm the
   Catppuccin/teal-pink direction is what was wanted — "does this look right" per
   COLLABORATION.md §1 is a human judgment this session's own verification cannot make.
3. **The EFFECTS listening pass.** Unchanged from prior rewrites.
   `host/build/PluginForgeHost_artefacts/Debug/Standalone/PluginForge Host` is current.
   Input bus required. VST3 still never installed (`COPY_PLUGIN_AFTER_BUILD FALSE`).
4. **Requested and not yet planned: a piano roll.** Unchanged. Needs a note grid *and* a
   clock (no host transport in Standalone). Recommended split: preset audition phrases
   first, drawing UI after.
5. **`bench/results/.prompt_baseline.json` is still untouched**, deliberately. Records
   `0.88` for the deleted pre-unification prompt.
6. **`UDHR.md` and `IDEAS.md`** — still untracked, still yours, still left alone.
   `sesh_new.md` was removed this session at your instruction (copied to session scratchpad
   first, since it was untracked and unrecoverable via git otherwise).
