# Session 002 — Refine that actually refines, and a UI that stops looking unstyled

**Frozen point-in-time record, 2026-08-07. Not maintained — "in progress" below is stale; check
`STATUS.md` and `git log` for what actually landed, not this line.** Part A (A1–A8, the refine
wiring) is landed and committed —
`tools/check.sh full` is green, including the new `EditorSessionTest::
scenario16_refineCarriesTheSource`. Part B (B1–B6, the design-system pass) is starting:
`Theme.h` exists on disk but is not yet wired into any panel.

**This file is the living record for this session — keep editing it, do not fork a new one.**

Two things below have different lifetimes, and the difference is deliberate:

- **The execution record** (this section) is current and gets updated as work lands.
- **The plan text**, from "## 1. Plan, as approved" down, is **frozen exactly as approved**
  in plan mode. Do not silently correct it as work proceeds — if something in it turns out
  to be wrong, the execution record says so, and the plan text stays as evidence of what was
  actually decided going in.

Detailed task list (same content, plan-mode's own copy):
`~/.claude/plans/lively-wishing-otter.md`. That file lives outside the repo and is not
guaranteed to survive across machines/sessions — this document is the durable copy.

---

## Execution record

### Landed and pushed
*(none yet — three commits landed locally this session, not yet pushed to origin. See
"landed, not yet pushed" below.)*

### Landed, not yet pushed

**Commit 1 (`d85ae37`) — A1.** The token wall is real, measured live, tighter than any
prior estimate on record. `tools/measure_prompt_tokens.py` (new) posts directly to groq,
bypassing `make_generator`'s `max_tokens` floor, so it can report real
`usage.prompt_tokens` at whatever `max_tokens` is actually asked for. Three live
measurements:

| payload | prompt_tokens | slack @ max_tokens=4096 |
|---|---|---|
| `system_prompt.txt` alone | 3522 | 382 |
| `system_prompt.txt` + a 725-char prior source | 3899 | **5** |
| `instrument_prompt.txt` + the same prior source | 2736 | 1168 |

**The 725-char case is a small real patch, not a worst-case one** — this project's own
`providers.py` records an observed maximum prior-source length around 2,200 chars, which
would blow well past the ceiling. **Conclusion: A6 (the pre-flight/drop-not-truncate
policy) is not an optional follow-up. It must land in the same change as A4**, not
deferred as the original plan text below allowed for if the wall turned out not to be
real — the plan text's branch below is frozen as approved; this paragraph is the
correction. `tests/test_prompt_headroom.py` was re-calibrated against the fresh anchor
(`MEASURED_CHARS=11992`, `MEASURED_PROMPT_TOKENS=3522`) and the stdlib-growth bounds
re-bracketed from 20%/50% to 5%/20% (actual threshold ~14.7%, under the old mild bound).

**Commit 2 (`3a94080`) — A2/A3/A6, the python side.** `llm/generate.py` gained
`--request-file <path>` (A2, with the `_subprocess_mode` trap closed), an in-code
`_REFINE_PREAMBLE` constant folding `prior_source` into the existing user message (A3, no
new prompt file), and `generate_json` now calls `providers.preflight_prior_source(...)`
once before the retry loop (A6) — fits → sent whole, doesn't fit → dropped entirely
(never truncated) with an additive `"prior_source_dropped": true` on the success
response. The estimator itself moved from the test file into `llm/providers.py`
(`estimate_tokens`, `request_ceiling`, `headroom_tokens`, `preflight_prior_source`) so the
runtime decision and the CI guard read the same calibration; `test_prompt_headroom.py` now
aliases its old names to `providers.py` rather than duplicating them. Tier 2 evidence:
`TestGenerateFaustPriorSource` (message assembly, positive+negative),
`TestRequestFileMode` (the transport, including a real subprocess run of the A2 trap),
`TestGenerateJsonPriorSourcePreflight`, `TestTokenEstimation` — 149 tests green.

**Commit 3 (`5090b55`) — A4/A5/A8, the C++ side plus docs.** `PromptPanel::submitPrompt()`
reads `processor.currentSource()` on the message thread, before `jobMutex`, into a new
`pendingPriorSource` member. `runGeneration()` writes it, when non-empty, to a
`juce::TemporaryFile(".json")` request with forced `"\n"` line endings and passes
`--request-file`; an empty prior source or a write failure degrades the *payload* to plain
`--prompt`, never `pendingMode` (`scenario06_freshResetsKnobs` still owns that
distinction). `host/tests/FakeGenerator.h` gained `writeSuccessCapturing()`, which records
argv and any `--request-file` payload instead of ignoring both; `EditorSessionTest::
scenario16_refineCarriesTheSource` exercises the positive, negative and first-generation
degrade cases through the real subprocess bridge (STATUS.md's cited name, `scenario15`, was
already taken). `docs/ux_roadmap.md:62-67`'s stale DELEGATE/PROPOSED/HUMAN-OWNED language
was replaced, and a dated ADR-011 amendment landed in `docs/decisions.md`. `tools/check.sh
full` is green (143 `EditorSessionTest` checks, every sanitizer/TSan/render-oracle/pitch-gate
harness).

**Part A's stated unverified remainder, unchanged since A1: whether the model actually
honours a folded-in prior source.** No test in this repo can prove it — `FakeGenerator`
proves the transport, not model behaviour. One live groq run with a distinctive marker
control (e.g. `hslider("Zzyzx", …)`, the same name `scenario16` uses for its fake) is the
next thing to spend on this, not attempted in this session.

**Follow-up, a later session (2026-08-06): the single ON/OFF toggle this session shipped
became a 3-mode ComboBox** (New/Add/Redo — ADR-011's second amendment, `docs/decisions.md`).
Part A's `_REFINE_PREAMBLE` is untouched and still the legacy path (`refine_mode` absent);
Add and Redo select between two NEW preambles via `refine_mode: "surgical" | "context"`.
This doubles rather than closes the unverified remainder above — two preambles now, neither
validated against a live model. Tracked in STATUS.md, not re-litigated here; `scenario16`
(this session) and `scenario25`/`scenario26` (the follow-up) together cover the transport
for all three modes.

### Left to do

- **A7** deferred (feeding failed code back into the retry loop — separate change, per the
  plan text below; not reopened this session).
- **B1** `Theme.h` — landed on disk (this session) but **not yet wired into any panel**.
  Next: replace the 15 call sites in `PluginEditor.cpp`, `PromptPanel.cpp`,
  `CodeEditorPanel.cpp` with `Theme::`/`Theme::Type::` tokens.
- **B2–B6** — not started. `ForgeLookAndFeel.h`, sectioned layout from group metadata,
  snapshot/manifest verification, full `tools/check.sh` run. See plan text below for the
  complete, cited design.

### What was learned

**The prior session's work was real but invisible**, and that gap is why this session
exists. `--capture` playing a note and the pitch gate (previous session, commit `9341ea6`)
never touched `PluginEditor.*`/`PromptPanel.*`/`ParamGridPanel.*` — it answered "can the
pipeline be measured," not "does the product look and feel like what we're building
toward." Worth remembering before scoping the *next* session's first move too.

**ADR-019 (native JUCE, WebView deferred) was reconsidered and kept**, after research
rather than by default. Amorph — the closest architectural peer (same in-DAW JIT approach)
— auto-generates a **native** UI and has the identical "weak auto-generated interface"
complaint we're trying to fix, which is evidence the lever is design investment, not
rendering technology. This repo also vendors JUCE 7.0.9, not JUCE 8, so it doesn't have
JUCE 8's improved native WebView component either — reopening ADR-019 would mean a JUCE
upgrade too. Confirmed with the user directly before proceeding (COLLABORATION.md §2
trigger 2: reversing/reconsidering an accepted ADR is a consult item, not Claude's call
alone) — full reasoning and the web research behind it is in this session's conversation
transcript, not reproduced here; the ADR itself is unchanged in `docs/decisions.md`.

**One flagged risk from the Plan agent's pass was checked directly against JUCE source and
resolved, not left as a caveat.** Faust `NumEntry` params (`IncDecButtons` slider style)
were flagged as possibly going invisible under a custom `drawLinearSlider` override.
Read `juce_Slider.cpp:1219` directly: `paint()` explicitly skips `drawLinearSlider` for
`IncDecButtons` and renders real child `Button`s via `createSliderButton`
(`juce_Slider.cpp:611-612`) instead. Nothing was ever at risk of going invisible — but
`createSliderButton` needed adding to the override list for visual consistency, and the
plan text below already has it.

**The token-budget numbers drift across dated comments in this repo faster than they get
re-measured**, and this session found real disagreement: STATUS.md said "483 tokens of
slack," `providers.py`'s own comment said "621," `test_prompt_headroom.py` said "185" at
one point in its history. All were true on their own measurement day. The fresh live
number (382 baseline, 5 with even a small prior source) is now the only one that matters
for A6's design — a reminder that this class of number should probably be re-measured
before being trusted, not just before being cited, on any future session that touches the
prompt.

---

## 1. Plan, as approved

*(Frozen. This is the plan-mode plan approved at the start of this session, copied here
verbatim so it survives independently of `~/.claude/plans/lively-wishing-otter.md`. Do not
edit below this line to reflect new findings — use the Execution record above for that.)*

### Context

The prior session built real, verified infrastructure (a note-aware `--capture`, a pitch
gate) that never touched the shipping product surface — which is why, after seeing it
running, it looked identical to the day before. That was a legitimate but mis-scoped
choice: it answered "can the pipeline be measured" when the actual ask was "does the
product look and feel like what we're building toward."

The user's stated goal is explicit: a UI overhaul, plus an iteration feature so a generated
plugin can be refined toward a desired vision — benchmarked against Amorph and
pluginmaker.ai. Research this session found:

- **Both gaps are real and already diagnosed in this repo's own docs.**
  `docs/competitive_landscape.md:107-112` ranks them P1: *"Our refine loop is our moat #1
  (self-correction) made user-visible"* and *"'Beautiful UIs' is a stated competitor
  strength and our weakest lane."*
- **"Refine" is a UI affordance that does nothing.** The checkbox only changes a
  knob-retention policy (`LoadMode::Iterate` vs `Fresh`); the prior Faust source is never
  sent to the LLM. Confirmed by reading `PromptPanel.cpp:367-370` — the subprocess argv is
  unconditionally `{python, generate.py, --prompt, prompt}` regardless of the toggle.
- **The UI has zero custom styling.** No `LookAndFeel` subclass anywhere in `host/Source`
  (grep, zero hits). Every knob, button, and text box renders through JUCE's unmodified
  default chrome on a single inline-hex background.
- **Architecture question settled.** ADR-019 (native JUCE widgets, WebView compiled out)
  stays. Verified against fresh research: Amorph — the closest architectural peer, same
  in-DAW JIT approach — auto-generates a **native** UI and has our identical "weak
  auto-generated interface" complaint from reviewers, which is evidence the fix is design
  investment, not rendering technology. This repo also vendors JUCE 7.0.9, not JUCE 8, so
  it doesn't even have JUCE 8's improved native WebView component; reopening ADR-019 would
  mean a JUCE upgrade too. None of what's being asked for (professional look, working
  iteration, eventual keyboard input) actually requires WebView. Confirmed with the user
  directly.

**Scope, confirmed:** (A) make Refine carry the prior Faust source to the LLM, and (B) a
real native custom-LookAndFeel + sectioned-layout design-system pass. Keyboard/MIDI
playability (NoteRing exists, nothing produces into it from the UI) is a separate,
already-understood gap, explicitly deferred — this plan must not break it, but does not
build it.

Independent research and a deep code-read (Plan agent + spot-verification against the
actual JUCE 7.0.9 headers under `/home/losera/JUCE/modules/`) produced the plan below. One
flagged risk from that pass was checked directly against `juce_Slider.cpp` and resolved:
Faust `NumEntry` params (`IncDecButtons` style) render through real child `Button`s via
`createSliderButton`, not through `drawLinearSlider` — nothing goes invisible, but
`createSliderButton` needs its own override for visual consistency.

### Part A — make Refine carry the source

**A1. Settle the token budget first — before building anything.**

Groq's ceiling is `prompt_tokens + max_tokens <= 8000`, `max_tokens` floored at 4096
(`llm/providers.py:68`, `:104`, `:274`, `:766`). At the last calibrated ratio, a realistic
~2,200-char prior patch (`providers.py:57`'s recorded observed max) costs roughly 660
tokens against ~483 tokens of recorded slack — the arithmetic already predicts the wall is
real; the job of this step is to confirm or refute that with one live measurement, not to
design around a guess.

New `tools/measure_prompt_tokens.py` (~50 lines): rebuilds the payload
`_make_openai_compat` builds (`providers.py:528-535`), calls `providers._post_with_backoff`
directly (`:683`, whose `response.json()` at `:711` still carries `usage` — the production
adapter discards it at `:546`), prints
`usage.prompt_tokens/completion_tokens/total_tokens`. Cannot use the normal generation path
for this: `make_generator` clamps `max_tokens = max(requested, spec.min_max_tokens)`
(`:766`), so asking for 3000 silently sends 4096.

Run three ways, each one live groq call: (1) `system_prompt.txt` alone, to re-calibrate
`tests/test_prompt_headroom.py:82-84`'s `MEASURED_*` constants (its own docstring already
asks for this); (2) `system_prompt.txt` + a real prior patch, the refine number; (3)
`instrument_prompt.txt` + prior patch, since that prompt has no headroom guard at all
today.

Branch on the result: fits → land A2–A5 as designed below, file A6 (pre-flight) as a
recorded follow-up with the measured number. Does not fit → A6 lands in the same change as
A4, and the policy is drop the prior source entirely, never truncate — a half-program
teaches the model a syntax error the user didn't make, the same failure class
`_TRUNCATION_HINT` (`generate.py:132-137`) already exists to prevent in the other
direction.

*(Execution record above: measured. Wall is real. A6 required, not conditional.)*

**A2. `llm/generate.py` — a `--request-file <path>` mode.**

No argparse exists in this file (confirmed: three positional-lookup modes at `:464-493`);
match that style rather than introducing one. New:

```python
def _read_request_file(path: str) -> dict:
    return json.loads(Path(path).read_text(encoding="utf-8"))
```

and a third branch in `__main__` reading `--request-file <path>` into
`_run_subprocess_mode`. The real trap: `:469`'s
`_subprocess_mode = "--json" in sys.argv or "--prompt" in sys.argv` must gain
`or "--request-file" in sys.argv` — verified this is the exact line. Missing it means a
credential error prints a bare `[!]` to stderr and `sys.exit(1)` instead of the ADR-011
JSON, and the host reports a generic "no result" instead of the real problem.

**A3. `prior_source` folds into the existing user message — no new prompt file.**

`generate_json` reads an optional `prior_source` from the request and threads it to
`generate_faust`, which prepends a short in-code preamble (a new module-level constant,
sibling to `_TRUNCATION_HINT`) plus the fenced prior source to the existing `content` before
the retry loop, rather than routing to a third prompt file. Three reasons, each checked
against the code:

1. A refine prompt file would replace the system prompt, not add to it — `select_prompt()`
   returns exactly one file (`generate.py:59-75`) — so it would need to duplicate the
   stdlib block, ADR-009 rules and few-shots (~11,400 chars) to keep output compiling,
   which is the worst possible use of ~483 tokens of slack (now measured ~382, tighter
   still).
2. Admission cost is real, not hypothetical: `check_prompt_invariants.py:59` already globs
   `llm/prompts/*.txt` and blocks the write unless the file carries the ADR-009 sentence,
   the "process … exactly once" clause, and the stdlib markers; `test_prompt_stdlib.py:47-50`
   requires a `PROFILE_FOR` entry. A 50-word instruction would legally need a full generated
   stdlib block to land.
3. All three provider adapters take one `user_message: str`
   (`providers.py:471,503,527`). A genuinely separate conversation turn means changing
   three adapters plus every caller (`bench/run_benchmark.py`, `bench/run_efficacy_study.py`,
   `bench/p6_capture.py`). Folding into the existing message changes nothing below
   `generate_faust`.

Tier 2 evidence, three separate artifacts because they prove three different things:
message assembly (patched transport, asserts the prior source appears verbatim in the
framing, plus the negative — no `prior_source` leaves `content` byte-identical to today),
the `--request-file` transport itself (temp file through `_run_subprocess_mode`, one JSON
line out, a bad path yields the ADR-011 failure shape not a traceback), and — the one thing
no test can prove — that the model actually honours it: one live groq run with a
distinctive marker control (e.g. `hslider("Zzyzx", …)`) surviving into the returned patch.
That third one is the stated unverified remainder, not something to fake with a mock.

**A4. `host/Source/PromptPanel.{h,cpp}` — wiring.**

New member, guarded by the existing `jobMutex`, beside `pendingMode`
(`PromptPanel.h:192-193`): `juce::String pendingPriorSource;`. Read in `submitPrompt()` on
the message thread, before taking `jobMutex` (it doesn't need to nest with the processor's
own `currentSource()` lock — no reason to invent a lock order):

```cpp
const bool refine = refineToggle.getToggleState();
const auto prior  = refine ? processor.currentSource().trim() : juce::String();
```

Refine ticked with nothing yet to refine (first generation) degrades the payload only, not
the mode: `prior` is empty, so argv falls back to plain `--prompt`, never sending an
empty/garbage `prior_source`. `pendingMode` stays `Iterate` exactly as it does today — this
is what `scenario06_freshResetsKnobs` already covers and must keep covering.

`runGeneration()` gains a `priorSource` parameter and, when non-empty, writes a
`juce::TemporaryFile(".json")` (built via `juce::DynamicObject` + `juce::JSON::toString`,
one escaping layer, not hand-built strings) and passes `--request-file <path>` instead of
`--prompt <text>`. Line endings must be forced to `"\n"` — `juce_File.h`'s
`replaceWithText` defaults to `"\r\n"`, a trap `host/tests/FakeGenerator.h:27-30` already
records paying for once. The `TemporaryFile` is declared before `child` so it outlives
every exit path through `runGeneration` (all of them are plain `return`s — nothing skips a
destructor); a write failure degrades to plain `--prompt` rather than failing the run.

Audio-thread statement: none of this touches `PluginProcessor.cpp`, `FaustEngine.cpp`,
`ParamPool.cpp`, `OutputGuard.cpp`, or `NoteRing.h`. `PromptPanel.cpp` is entirely
message-thread/worker-thread, outside `check_rt_safety.py`'s scoped closure.

**A5. The red case — `scenario16_refineCarriesTheSource`.**

STATUS.md cites `scenario15_refineCarriesTheSource` as the test to write when this work
lands — that name doesn't exist (slot 15 is `scenario15_identityKeyedRetention`,
`EditorSessionTest.cpp:1138`); use 16, the next free slot.

`host/tests/FakeGenerator.h` gains a capturing variant that writes `argv.txt` (one arg per
line) and copies any `--request-file` payload to `request.json`, so the test can see
exactly what `PromptPanel` invoked. The scenario: Refine off → `argv.txt` is plain
`--prompt`, no `request.json` (negative half); Refine on after a first successful
generation → argv starts with `--request-file`, and the captured JSON's `prior_source`
contains the first patch's distinctive marker (positive half); a fresh session with Refine
ticked before any generation → plain `--prompt` again (the degrade case, A4's requirement,
seen exercised not just claimed).

**A6. Token pre-flight — now required, not conditional (see execution record).**

Move the estimator out of the test file and into `llm/providers.py` as the production
source of truth (`estimate_tokens`, `request_ceiling`), so the runtime decision and the CI
guard can't drift apart. `generate_json` calls a `preflight_prior_source(...)` before the
retry loop: fits → send whole, doesn't fit → drop and mark `"prior_source_dropped": true`
in the response (additive, same treatment `kind` got in `d587665`) so `PromptPanel` can
tell the user their refine silently became a regeneration instead of staying quiet about
it.

**A7 / A8 — explicitly deferred / documentation.**

Feeding the failed code back into the retry loop (not just stderr) is real but touches
every generation, not just refines — separate change, after A1's number is known (now
known). Fix `docs/ux_roadmap.md:62-67`'s stale HUMAN-OWNED gate language (COLLABORATION.md
§9 retired that vocabulary; prompt wording is Tier 2 evidence-bar work now, not an
authorship gate) and add a dated amendment to ADR-011 in `docs/decisions.md` recording the
new additive input channel. Judgment on record, not assumed: this is an additive input
channel with one consumer, touching no existing schema field — same shape as `d587665`'s
additive `kind`, which wasn't treated as a consult-gate item. Called ungated here; reversal
is one `elif` and one argv arm if that's wrong.

### Part B — a real native design system

**B1. `host/Source/Theme.h` — new, header-only.**

A `namespace Theme` (matching the existing `ParamGridLayout`/`ParamIdentity` header-only
convention) replacing the 15 scattered inline-hex call sites across `PluginEditor.cpp`,
`PromptPanel.cpp`, `CodeEditorPanel.cpp` with named tokens (`inline const juce::Colour` —
`juce::Colour(uint32)` is `explicit` and not `constexpr`, verified against
`juce_Colour.h:57`), plus a real type scale
(`Theme::Type::{caption,body,label,heading,title}`) — the actual visual problem today is
that four of the five existing font call sites are the identical bare `12.0f`, not the
typeface itself.

**B2. `host/Source/ForgeLookAndFeel.h` — new, header-only, `LookAndFeel_V4` subclass.**

Header-only deliberately: `PluginEditor.cpp` already appears in seven separate
`target_sources` lists in `host/CMakeLists.txt`; a `.cpp` companion means seven edit sites
and a silent link-error risk on the harness nobody notices that day.

Virtuals to override, each checked against the actual JUCE 7.0.9 header, not recalled:
`drawRotarySlider`, `drawLinearSlider` (handles both `LinearHorizontal` and
`LinearVertical`), `getSliderThumbRadius`, `createSliderTextBox` (restyles the readout
without touching `ParamGridPanel::applyPresentation`'s PF-037 text lambdas),
`drawToggleButton` + `drawTickBox`, `drawButtonBackground`, `getTextButtonFont`,
`drawLabel` + `getLabelFont`, `fillTextEditorBackground` + `drawTextEditorOutline`,
`drawScrollbar` + `getDefaultScrollbarWidth`, and `createSliderButton` (added after
verifying `juce_Slider.cpp:1219,611-612` directly this session), so Faust `NumEntry`
params' +/- buttons match the new chrome instead of falling back to default JUCE styling.

Installation, and the one real trap: `setLookAndFeel(&lnf)` in `PluginForgeEditor`'s
constructor — not `setDefaultLookAndFeel`, which is process-global and a plugin shares its
process with the DAW and other instances of itself. `Component::setLookAndFeel` propagates
to every child with no override the moment it's `addAndMakeVisible`d, so dynamically-created
knobs in `ParamGridPanel` inherit it automatically — no per-widget call needed. Declare
`lnf` as a member before the three child panels in `PluginEditor.h` (destroyed after them),
and call `setLookAndFeel(nullptr)` as the first line of `~PluginForgeEditor` (currently an
empty body) — `~LookAndFeel` asserts if anything still points at it. `EditorSessionTest`
and `UiDesignGallery` both construct the real editor under ASan/UBSan, so a lifetime bug
here surfaces before it reaches a DAW.

**B3. Sectioned layout from group metadata that's already captured and already unused.**

`ParamGridPanel.h:110-111` states plainly: Faust group paths are captured
(`controlGroupForTest`) but "nothing lays out by it yet." This is the highest-leverage,
lowest-risk lever for "feels professionally designed" — the data already exists.

Grouping is not a fourth `ControlStyle`. Style decides widget type; group decides
placement — orthogonal, and grouping should apply under all three existing styles rather
than add a persisted, parsed fourth enum value for no user-facing gain.

Trigger rule: zero distinct groups → today's flat sqrt-grid, byte-identical (the majority
case — `reference_manifest.json` shows empty groups for four of six fixtures). One or more
groups → sectioned, with a trailing implicit section for any ungrouped controls (covers the
mixed case in `scenario14`, `EditorSessionTest.cpp:1066-1071`). Section key is the first
path segment (`"Osc/Tune"` → `"Osc"`); section order is first-appearance in existing slot
order, not alphabetical, so a param reclaiming its slot on regeneration keeps its section
position.

Critical implementation constraint: a new `Section` structure holds member indices, never
reorders `controls` itself — every `*ForTest` accessor, the gallery manifest's
`labels`/`kinds`/`texts`/`groups` arrays, and `EditorSessionTest` scenario 2's by-position
widget-kind assertions all depend on index stability. Reordering would make the layout diff
tool report spurious "order changed" on every grouped fixture for a change that's purely
geometric. `ParamGridLayout.h` gains a pure, JUCE-free `sectionsFor(...)` plus height
helpers; `ParamGridPanel` gains section-heading `Label`s and a sectioned branch in
`contentHeightForCurrentMode()` and `layoutControls()`. `applyPresentation()` — which
decides widget type, the PF-039-corrected logic — is not touched by this change at all.

**B4. Fonts — recommend the system stack; flag embedding as a build-dependency question.**

Font embedding needs a new `juce_add_binary_data` CMake target linked into every plugin
target plus a font-licence question — that's squarely COLLABORATION.md's build/dependency
consult trigger, not something to do silently inside a UI plan. The system stack (a real
`Theme::Type` scale with actual weight/size hierarchy instead of five near-identical
`12.0f` calls) recovers most of the perceived-quality gap on its own. If embedding is
wanted later, the shape is scoped in the plan's working notes but not started here.

**B5. What the existing snapshot infra absorbs for free vs. what moves on purpose.**

Colour/font/knob-drawing changes are free: `EditorSessionTest.cpp:46-47` states outright
that nothing asserts look-and-feel, and `tools/ui_layout_diff.py` only compares semantic
fields (`controls`/`window`/`grid`/`labels`/`kinds`/`texts`/`groups`). Two things move on
purpose and must be checked, not assumed: `tools/ui_iterate.sh` (run without `--accept`
first) should show `window` changing only for the two `*_grouped` fixtures — predict that,
then confirm it before accepting; and `PluginEditor.cpp:301`'s
`static_assert(chromeHeight(Chrome{}) == 350)` stays unchanged in this pass (recommend
leaving `Chrome`'s band heights alone entirely, so both existing layout-sensitive
`EditorSessionTest` assertions at `:400-401` and `:786-796` need no edit). New
`host/tests/ParamGridLayoutTest.cpp` — pure, JUCE-free unit tests for `sectionsFor` — also
closes a note already sitting in `ParamGridLayout.h:10-15` asking for exactly this test by
name.

**B6. Sequencing.**

Parts A and B touch disjoint files except two shared ones (`PromptPanel.cpp`,
`EditorSessionTest.cpp`), in non-overlapping regions. Recommend A first, then B, as
separate commits regardless of order chosen: A is already Broken #2 on STATUS.md's ranked
list; A1 is a measurement worth taking while this context is loaded (done); B's diff
deliberately moves the snapshot baseline, and landing it after A means a wrong-looking
`scenario16` snapshot can't be mis-attributed to whichever change moved the baseline.

### Verification

**Part A:** `tools/measure_prompt_tokens.py` against live groq settles the budget question
with a number, not a guess (done). `tests/test_generate_unit.py` proves the request-file
transport and the message assembly (positive and negative). `EditorSessionTest::
scenario16_refineCarriesTheSource` proves the UI wiring, including the first-generation
degrade. Existing `scenario06_freshResetsKnobs` and `PromptPanelThreadingTest` must stay
green unmodified. Not verifiable by any test in this repo: whether the model actually
honours the prior source — one live groq run with a marker control, stated as the explicit
unverified remainder.

**Part B:** `UiDesignGallery` PNGs show the new chrome reaching every dynamically-created
knob (a missed `setLookAndFeel` propagation is visible, not silent). `EditorSessionTest` +
`UiDesignGallery` under ASan/UBSan catch a `LookAndFeel` lifetime bug via its own
destructor assertion. New `ParamGridLayoutTest` proves `sectionsFor`'s logic directly.
`tools/ui_iterate.sh` (no `--accept`, diff checked against the written prediction, then
accepted) proves the flat path is untouched and only the grouped fixtures move. Manifest
`kinds` arrays staying byte-identical proves PF-039's per-kind widget-type correctness
isn't regressed. Not verifiable by any test: whether it actually looks professional —
that's the contact sheet (`tools/ui_contact_sheet.py`) and the Standalone, by eye.

Full gate for both parts: `tools/check.sh full`.

### Files touched

**Part A:** `tools/measure_prompt_tokens.py` (new) · `tests/test_prompt_headroom.py` ·
`llm/generate.py` · `llm/providers.py` (A6) · `host/Source/PromptPanel.h` ·
`host/Source/PromptPanel.cpp` · `host/tests/FakeGenerator.h` ·
`host/tests/EditorSessionTest.cpp` · `tests/test_generate_unit.py` · `docs/ux_roadmap.md` ·
`docs/decisions.md`

**Part B:** `host/Source/Theme.h` (new) · `host/Source/ForgeLookAndFeel.h` (new) ·
`host/Source/ParamGridLayout.h` · `host/Source/ParamGridPanel.h` ·
`host/Source/ParamGridPanel.cpp` · `host/Source/PluginEditor.h` ·
`host/Source/PluginEditor.cpp` · `host/Source/CodeEditorPanel.cpp` ·
`host/Source/PromptPanel.cpp` (3 colour lines only) · `host/tests/ParamGridLayoutTest.cpp`
(new) · `host/CMakeLists.txt` (one new test target) ·
`host/tests/ui_fixtures/reference_manifest.json` (via `--accept`) · `tools/check.sh`

No staging or committing across the whole tree — explicit paths only, per CLAUDE.md.
