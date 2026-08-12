# PluginForge — Status  (2026-08-07)

Rewritten each session per COLLABORATION.md §5. Single writer, no merge conflicts.
Narrative history lives in git.

**Start a session with `/orient`**, not by reading this file top to bottom. It injects live
repo state, **the CI line**, this file's open sections, and a staleness banner if it falls
behind HEAD.

---

## Works — and how we know

**This session ran the first live verification of either refine preamble — the Assumed
claim #2 and Next-three #1 open since 2026-08-06: one Add-mode (surgical) and one Redo-mode
(context) groq run on the same starting patch, through the exact product path the host
uses. Both were honoured, with a committed evidence artifact (first bullet below). All
bullets before it are carried from the previous session, unchanged.**

- **The model honours either refine preamble, verified live — 2/2.**
  `bench/check_refine_preamble_live.py` (new) ran one Add-mode (`refine_mode:"surgical"` →
  `_SURGICAL_PREAMBLE`) and one Redo-mode (`"context"` → `_CONTEXT_PREAMBLE`) groq run on the
  SAME starting patch — a 4-line stereo scaler carrying a marker control
  `hslider("ZzyzxLive",…)` and a named helper `core` — through the exact product path the
  host uses (`llm/generate.py --request-file`, cwd=llm, `PLUGINFORGE_PROVIDER=groq` from
  `.env`, `kind` pinned to `effect`), differing only in `refine_mode` and prompt. Both
  requests, ADR-011 responses and faust outputs are recorded verbatim in
  `bench/results/refine_preamble_live_20260807_201935.json`. **Add**: the model preserved
  `ZzyzxLive` AND `core` exactly and added exactly one line — `filtered = core :
  fi.lowpass(2,1000); process = filtered, filtered;` — minimal, surgical, structure
  untouched. **Redo**: asked to "rewrite this from scratch as a stereo chorus with rate and
  depth controls", the model produced a from-scratch chorus (`Rate [unit:Hz]` and `Depth`
  sliders, `process = chorusCh(rate, _), chorusCh(rate * 1.13, _);`) with zero trace of the
  prior's marker or helper. Both compiled (`faust` exit 0, re-checked independently after
  `generate_json`'s own `validate_faust`). Closes Assumed #2 and Next-three #1.
  `tools/check.sh fast` green; no product code touched. **Narrowed, not unconditional**:
  one run per mode is a single stochastic draw, not a rate; single provider/model
  (groq/`openai/gpt-oss-120b`), the effects prompt, one prompt per mode, one specific prior
  patch; "preserve exactly" is judged by marker/helper survival plus a successful compile,
  not a semantic diff. The full unverified remainder is recorded inside the artifact.
- **The spectral/timbral judge (PF-041/PF-042) now consumes the features render_oracle
  computes and produces a per-prompt verdict.** `bench/spectral_judge.py` (new) turns
  `band_gain_db`, `centroid_shift_oct`, `crest_in/out_db` and `tail_ms` into a score
  (0–1) against the prompt's category. `render_oracle.analyse()` accepts `prompt=` and
  emits `acoustic_compliance` when a prompt is provided; `analyse_record()` and the CLI
  (`--prompt`) both plumb it through. Fixed the PF-042 regression: the old time-based
  branch read a `t60_s` key render_oracle never emits, defaulting 0.0 and penalizing every
  reverb/delay render. **Verified**: 21 pure-signal unit tests + 3 integration cases in
  the oracle test file; corpus run 16 passed / 0 failed with a verdict per compiling
  record; `check.sh fast` green. The judge is **report-only** this round — no `check.sh`
  scoring gate — until a larger corpus shows the false-positive rate. Closes Broken #9.
  Next-three #1 below.
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
  already-landed, already-pushed commits), Broken #11's "takes no lock" half (the lock is
  at `bench/score_efficacy.py:558,569`), two citations pointing `ForgeLookAndFeel.h`'s scope
  at the wrong document section, and a `Waiting on you` item asking for a `ux_roadmap.md`
  fix an earlier commit (`5090b55`) had already made. This project's named recurring defect
  is a claim that outlived the thing it described — four instances of it, found by rereading
  rather than recomputing, are fixed inline above and in Broken/Waiting below.
- **Six more instances of the same recurring defect were found and fixed across the docs,
  plus two grossly-stale files deleted.** The plan's cleanup table named README's dead
  `scripts/` commands and the ADR-008 status mismatch; the actual sweep found more. Read against
  current code, not re-trusted: the "8 of 64 param cap" claim (gone — the grid renders all 64
  slots scrollably, `PluginEditor.cpp:256-262`), three "blocked on state persistence" claims
  (landed, `PluginProcessor.cpp:608`), "ParamGridPanel unbuilt" (built + styled,
  `ForgeLookAndFeel.h`), refine "cannot carry source" (landed, `3a94080`/`5090b55`), and the
  retired DELEGATE/PAIR/HUMAN-OWNED ritual still described as live in README (retired,
  `COLLABORATION.md` §9). All fixed in `docs/competitive_landscape.md` (five claims refreshed +
  ghost `FLEET.md`/P10 citations), `README.md`, `docs/architectural_decisions/README.md`
  (ADR-008 "Accepted" → provisional + ADR-012), `docs/byo_llm_plan.md` + `docs/s3_plan_next.md`
  (fleet-era lane framing fixed), and `docs/ui_design_plan.md` (P10 survey retirement noted).
  `START_HERE.md` (pre-JIT stub) and `docs/handoff_s1_codex.md` (operated the retired fleet
  board) were deleted — nothing live linked to the latter except the fleet's own retrospective.
  `docs/BUGS.md` and `docs/next_steps.md` were checked and already reconciled (BUGS.md names
  the retirement explicitly; next_steps points at `git log` of the deleted survey).

- **The editor is a two-panel authoring screen, not a vertical stack (Track 1.1).** The
  window is now a full-width title bar, a split region (left preview/grid column | right
  prompt column, `kLeftFraction = 0.65`, `dividerW = 4`), an optional full-width code band,
  and a full-width keyboard band. `setSize(900, 500)`, `setResizeLimits(700, 400, 1600, 1200)`.
  **65/35, updated 2026-08-12 (session 010 §3, answering Waiting-on-you #2 below): the grid
  column holds the sectioned UiIr preview and was judged to deserve more than half. This
  went through you directly this session, not a screenshot read — see the plan review that
  landed it.** `keyboardH` also grew 64→72 (room for session 010 §4's inline octave/scale
  controls, not yet built).
  **The band-sum pin was replaced, not relaxed**: the old
  `static_assert(chromeHeight(Chrome{}) == 422)` described a single-column window that no
  longer exists, so it was retired in favour of two narrower assertions that survived the
  build — `rightColumnHeight(Chrome{}) == 276` and `verticalChrome(Chrome{}) == 144`.
  Both are consumed by `resized()` and summed by `updateWindowSizeForParams()`, keeping the
  one-source property the `Chrome` struct exists for.
  **One deliberate deviation from the approved plan, for a contract reason.** The plan put
  the code editor "in the left column"; it is instead a full-width band *below* the split.
  In the left column the code band would be absorbed whenever the right column is the taller
  of the two, and revealing code would leave the window height unchanged — silently breaking
  scenario 11's grow-on-show contract. Verified against the real numbers rather than
  reasoned about: scenario 11 measures `500 → 668px` (`144 + 276 + 8 + 240`), scenario 3
  measures `714px` (`144 + 6 × 95`, the `kMaxGridRows` cap). Both arithmetic identities hold
  exactly. `tools/check.sh full` green, 25/25; `EditorSessionTest` 192 checks / 0 failures.
  **Visually confirmed, not inferred from a passing test** — the suite asserts no column
  geometry, so the rendered snapshots were read directly: `session_03_overflow_40_params.png`
  (900×714) shows the scrolling 40-param grid left, prompt column right, visible divider
  seam; `session_11a_code_view_empty.png` (900×668) shows the code band spanning full width
  below the split and above the keyboard. **Not verified**: never opened in a DAW (Broken #2
  is unchanged by this). **Known defect at 65/35, found 2026-08-12, not yet fixed as of this
  entry**: the right column's own control rows (the disclosure row — code/style toggle,
  audition selector — and `PromptPanel`'s button row) were sized for the old 50/50 column
  and no longer fit; at the default 900px window the refine-mode selector disappears
  entirely. Fix in flight — see the corresponding item below.

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
handling while leaving the static constants untouched would still pass all 213 current
checks undetected. Closing this fully needs either a compositor-level input tool on this
machine or a different verification strategy entirely.

**2. It has never been in a DAW.** *(Unchanged.)* `COPY_PLUGIN_AFTER_BUILD FALSE`
(`host/CMakeLists.txt`) means the VST3 has never been installed or scanned, and pluginval
is not on PATH. Four concrete gaps behind "must take MIDI in any DAW": monophonic by
design, block-granularity MIDI (~10.7 ms jitter), a hardcoded 2.0 s tail length, no MIDI CC
mapping.

**3. ~~"Refine" is a crude binary, not a refinement architecture.~~** *(unfiled, medium,
closed 2026-08-06 — ADR-011's second amendment.)* The single toggle became a 3-mode
`ComboBox` (New/Add/Redo); `llm/generate.py` gained `refine_mode` and two new preambles,
with Add hard-failing on a token-budget overflow instead of silently degrading (Redo's
policy). **Found and fixed in the same session, not part of the original scope: Add and
Redo were disabled at construction and NOTHING re-enabled them** — the whole feature was
selectable only from test code (`ComboBox::setSelectedId` bypasses `isItemEnabled`), never
from any real mouse click, until `setRefineModesAvailable`'s two call sites landed
(`EditorSessionTest` scenario 25 is the red-then-green proof). `tools/check.sh full` green;
213 `EditorSessionTest` checks. Uncommitted — see Waiting on you #1.

**4. One generation defect is actually evidenced; the rest is sampling.** *(PF-024/PF-032,
high, unchanged.)* Karplus-Strong's `recursion_cycle` reproduces across four archives; the
sidechain compressor fails every run with a *different* error.

**5. The noise gate still renders silent.** *(PF-032's surviving half, high, unchanged.)*

**6. PF-045 — `en.*` time units on the frozen ladder record.** *(medium, open, deliberately
not spent.)*

**7. The DAW still sees raw slots.** *(follow-up to PF-037, open, unfiled.)*

**8. Knob ordering is Faust's own.** *(PF-038 low.)*

**9. ~~The only fidelity instrument is not interpretable.~~** *(PF-041 high, PF-042 medium,
closed 2026-08-06 — spectral judge now produces a per-prompt verdict; report-only, not
yet gated.)*

**10. The declared ollama model cannot hold its own prompt.** *(PF-043, medium, unchanged.)*

**11. `score_efficacy.py --judge` spends quota.** *(unfiled, unchanged.)* Takes a lock
(`bench/score_efficacy.py:558,569`); the quota cost itself is the remaining, real half.

---

## Assumed, never checked

**One claim** — the refine-preamble claim moved into Works this session with a live 2/2
measurement (see the first Works bullet above); the efficacy pilot remains.

- **The efficacy pilot generalizes to nothing.** *(PF-011, unchanged.)* 125 generations ≈
  437k tokens ≈ 2.2 days on groq. Needs sharding — or ollama, unmetered but CPU-only until
  the box reboots.

## Next three things

1. **Get it into a DAW.** Broken #2, long-standing, blocks the one validation this project
   cannot claim yet: does a generated plugin actually load and behave in a real host. Needs
   `COPY_PLUGIN_AFTER_BUILD` on, `pluginval` installed, and the four MIDI-fidelity gaps
   named in Broken #2 at minimum triaged, not necessarily fixed, before a first real scan.
2. *(evidence)* **Adversarial Mechanism A trial.** Session 006's recommendation: run a
   `touches`-declared-disjoint but actually-coupled brief pair in parallel and measure
   whether the mechanism catches the coupling. Today's Mechanism A data is one favorable
   4-brief sample; this trial tests the failure path before any `PreToolUse` hook is built
   on it.
3. **The generation-refinement architecture-planning conversation.** ADR-021 named
   "acceptance criteria — capturing what a generation was asked for so the result can be
   checked against it" as a real, deliberately deferred need; the spectral judge (PF-041/
   PF-042) partially addresses category-level compliance, and this session's live 2/2
   refine-preamble result (Works above) is the first datum on the refinement side. Promoted
   from Displaced: with the model now shown to respect both refine modes, the broader
   question — does single-pass generation reliably meet stated intent, and what would a
   refinement/critique pass cost — is the natural next planning conversation.

**Displaced, not urgent.** **A piano roll** (requested, unplanned; needs a note grid and a
clock). The generation-refinement architecture-planning conversation was promoted to
Next-three #3 this session.

## Waiting on you

1. **Superseded 2026-08-12** — the refine two-mode file list this item used to carry is
   long since committed. Session 010's alpha UI pass is now the live uncommitted work on
   `feat/ui-design-system`; see the plan at `.claude/plans/you-are-a-lead-steady-cake.md`
   for the current step-by-step (palette done, 65/35 split landing this session, keyboard/
   prompt/cockpit reconciled and queued behind it).
2. **Resolved 2026-08-12: 65/35, not 50/50.** Session 010 §1/§3 made the call — the grid
   column holds the sectioned UiIr preview and deserves more than half — and it landed this
   session (see the "editor is a two-panel authoring screen" entry above). That resolves
   this item's first question. The second — **is the dark palette (now Tokyo Night, not
   Catppuccin) what was wanted** — is still open; look at `artifacts/ui_gallery/index.html`
   after the next `tools/ui_iterate.sh --accept`. "Does this look right" per COLLABORATION.md
   §1 is yours.
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
