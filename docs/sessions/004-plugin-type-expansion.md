# Session 004 — Expanding plugin-type coverage: instruments, effects, and beyond

**Status: proposal.** Nothing in this document is decided. It answers a direct
question (deterministic type selection) with high confidence, because the
mechanism mostly already exists; it reasons through a wider taxonomy (drum
machines, sequencers, arpeggiators, granular synths) with lower confidence,
because most of that is genuinely new ground. Where a recommendation would
hit COLLABORATION.md §2 (a new ADR, a new contract between components), it is
marked and left for agreement before anything lands — this document drafts,
it does not decide.

---

## 1. The direct question: can type selection be deterministic?

**Yes, and the mechanism to do it is already half-built.** This isn't a new
idea — `docs/sessions/001-playground-shell.md` designed it (§D4) on
2026-07-30 and it was never implemented. What follows extends that design
rather than replacing it.

**What exists today, verified by reading the code, not recalled:**

- `llm/router.py` classifies every prompt as `instrument` or `effect` by
  deterministic keyword scoring — no LLM call, no network, microseconds
  (`router.py:1-27` states why). `effect` is the default on a tie.
- `select_prompt(user_prompt, kind)` (`generate.py:85-95`) already accepts an
  **override**. Its own comment: *"`kind` is an explicit override — the UI's
  instrument/effect toggle, or a benchmark pinning one side."* The ADR-011
  wire schema already carries `kind` as a request field, read at
  `generate.py:331` (`request.get("kind")`) and echoed back additively on the
  success response (`generate.py:380`).
- **`host/Source/PromptPanel.{h,cpp}` never sends it.** Grepped both files for
  `kind`, `instrument`, `effect` — zero matches. The override exists
  end-to-end on the generation side and stops exactly at the UI. This is not
  a broken feature; it's an unbuilt one, and it's the cheap 80% of what you're
  asking for.

**What `docs/sessions/001` already designed, and why it's better than a bare
dropdown** (§D4, `001-playground-shell.md:447-469`):

- `kind` becomes a **persisted field**, written at generation time and stored
  in the state blob (today it isn't — reopening a project re-derives type
  from the recompiled DSP, so there's no record of what was *asked for*, only
  what the model *produced*, per `001:273-275`).
- A user override is **sticky across regeneration** (`kind_source: "user"`) —
  correcting a misroute once should not need correcting twice.
- The compiled DSP's voice-contract check (`extractVoiceControls`,
  `FaustEngine.cpp:239+`) stops being a silent decider and becomes a
  **disagreement detector**: if the user said "instrument" and the compiled
  patch has no voice contract, that's a named, actionable state ("this was
  meant to be playable and isn't — regenerate"), not the silence
  `STATUS.md`'s Broken #1 describes today.

**This session's addition to D4**, since the question was specifically about
a *selection box before the prompt*, not just an override after a misroute:
make the pre-prompt control the **primary** signal, not a correction path —
default it to whatever the router would have guessed from placeholder text
("Effect"), let the user change it before typing, and skip the router
entirely when a selection has been made (`kind` arrives already pinned, so
`select_prompt` never falls through to `router.classify`). The router remains
exactly as valuable as it is today for the case the selector doesn't cover:
typing a prompt without touching the selector first.

**Where the selector's option list comes from is §2 below** — this part
(persist `kind`, wire the UI control, sticky override, mismatch surfacing) is
buildable today for the two types that already exist, independent of whether
any new type ever ships.

---

## 2. Taxonomy: sorting "popular plugin types" by what they actually require

The mistake to avoid is treating "add plugin type X" as one kind of task. Three
architecturally distinct buckets fell out of reading the code, not a genre
list:

### Bucket A — new prompt content only, no new host mechanism

A type that is still "one Faust `process` function, audio-rate, driven by
sliders and (for instruments) the existing `gate`/`freq`/`gain` contract."
This is the shape every one of today's generations already has (ADR-021: 14
of 19 corpus entries need nothing beyond the parameter struct that already
exists). Extends via the pattern `tools/gen_stdlib_block.py` already
establishes — a curated per-profile stdlib subset plus few-shots, gated by
`--verify-prompt` so a fabricated function is a hard error, not a silent
regression.

- **More effect subtypes** (parametric EQ, more saturation/distortion
  flavors, stereo-imaging tools). Cheapest possible extension: more curated
  entries and few-shots in the *existing* effect profile, if there's token
  budget (`tests/test_prompt_headroom.py` is the gate that says whether there
  is — Session 6's `-267` overflow this session is exactly what happens when
  there isn't).
- **One-shot percussion instruments** (a kick/snare/hihat *voice*, externally
  triggered by MIDI note-on — not a self-sequencing drum machine). This is
  architecturally identical to today's instrument category. `router.py`
  already routes `kick`/`snare`/`drum`/`percussi(on)` to `instrument`
  (`router.py:36`) — the vocabulary exists; nothing about the DSP shape is
  new.
- **A granular *texture effect*** — granulating the live audio input (not a
  loaded sample): grain scheduling via multiple delay-line taps with
  independent envelopes and pitch/position jitter is ordinary Faust signal
  processing (`de.delay`/`de.fdelay` plus envelope generators, the same
  primitives the existing chorus/flanger few-shots already use for
  modulation). No new host contract. This is the cheapest "granulizer" to
  ship, and it is *not* what most users picture when they say granulizer —
  see Bucket B.

### Bucket B — needs a new contract between components (§2 trigger 3 — ADR required)

- **Tempo-synced anything** — a self-contained step sequencer, a drum machine
  that generates its own pattern, a tempo-locked delay, a synced
  arpeggiator-as-DSP. **Verified gap, not assumed:** grepped
  `host/Source/*.{cpp,h}` for `AudioPlayHead`, `getPlayHead`, `bpm`, `tempo`
  — zero matches. A Faust patch today has no access to host BPM, PPQ
  position, or transport state at all; every existing parameter arrives via
  a slider. Faust *can* express tempo-locked rhythmic patterns in one
  `process` (phasors + `rdtable` step tables are standard Faust idioms for
  exactly this) but the DSP would need a `bpm` control value from somewhere,
  and nothing supplies one today. This is a new data channel from JUCE's
  `AudioPlayHead` into the macro pool, same shape as the existing 64-slot
  contract but a different source than a knob — a real architectural
  addition, not a prompt change. Draft ADR sketch in §4.
- **Polyphony (chords).** Needed for most "popular" synth types people
  actually picture (pads, keys, anything voiced with more than one note at
  once). Already analyzed and deliberately deferred:
  `docs/sessions/001-playground-shell.md` §D5 — Faust's `dsp_poly` machinery
  is present in the installed libfaust (`poly-dsp.h`, `poly-llvm-dsp.h`,
  cited with line numbers there) but adopting it changes what type the
  atomic swap protocol swaps (`std::atomic<llvm_dsp*>` → a `dsp_poly`
  instance with a different lifecycle), which touches "the single most
  carefully-reasoned component in the repo" (001's words, not softened here
  — it fixed ~1,100 errors once already, per CLAUDE.md). **That analysis
  still holds; this document does not reopen it**, beyond noting that most
  Bucket-B/C instrument types implicitly assume polyphony and inherit this
  same prerequisite.
- **Sample-based granular synthesis** (drag in a WAV, granulate *that*). Not
  as far out of reach as it looks: Faust's own `soundfile` primitive
  (verified in `/usr/include/faust/dsp/libfaust-signal.h:368-377` and
  `libfaust-box.h:408-426`, both present in the installed libfaust) exists
  precisely to reference an audio file from Faust source. But the *host*
  side has nothing: no file picker, no waveform display, no convention for
  where a generated patch's referenced file would even live relative to the
  plugin's state. This is a smaller version of the tempo problem — a new
  contract (how does a generated patch's `soundfile()` reference resolve,
  and how does the user supply the audio) — not a prompt-only change.

### Bucket C — not a Faust-generation problem at all

- **Arpeggiators, as most people mean the word**: hold a chord, the plugin
  retriggers individual notes on a clock. This is **MIDI note scheduling**,
  not signal processing — Faust's `process` model has no concept of
  "receive several simultaneous note-events, decide which to play when."
  The natural home for this is **host-side C++**, sitting between incoming
  MIDI and whatever single-voice instrument DSP is loaded (a producer into
  `NoteRing`, the same lock-free path the still-unbuilt keyboard widget would
  use — `STATUS.md`'s own "Next three things" #1). Built this way, an
  arpeggiator is a *host feature* orthogonal to which generated instrument is
  playing, not a new prompt category, and it does not wait on this document's
  Bucket A/B work at all.
- **Chord memory, MIDI-effect-style processing generally** — same bucket,
  same reasoning.
- **A full pattern-grid step sequencer / drum machine as a product surface**
  (multiple tracks, a pattern editor, transport controls) is a different
  *product*, not "one more Faust prompt." It plausibly needs a MIDI-output
  plugin target — worth noting that the instrument/effect split is *already*
  baked into two separate CMake build targets today (`PluginForgeHost` vs
  `PluginForgeSynth`, differing in `JucePlugin_IsSynth` and three related
  traits, `001-playground-shell.md:267-271` citing `host/CMakeLists.txt:28-140`)
  — a MIDI-effect type would plausibly need a *third* target, which is a
  build-system change (§2 trigger 4) on its own. **Not sized in this
  document** — flagged as its own decision, likely its own session.

---

## 3. Recommended sequencing

Ordered so nothing is built on a prerequisite that doesn't exist yet.

1. **Wire the deterministic selector (§1), for the two types that exist
   today.** No new contract, no ADR needed for the mechanism itself (it's an
   additive wire field and a UI control, the same class of change `kind` and
   `reason` already were under PF-019) — cheap, and it's the thing that makes
   every later addition to the taxonomy *choosable* rather than
   router-guessed. Ships value immediately: today's instrument/effect
   misroutes (`STATUS.md`, `router.py`'s own documented failure modes) get a
   one-click fix instead of a rephrase-and-hope.
2. **Bucket A additions** (more effect subtypes, one-shot percussion, a
   granular texture effect) — cheapest, reuses the existing generator
   pattern exactly, gated by the existing token-headroom test per addition.
3. **The keyboard widget** (`STATUS.md` Next-three #1, already scoped,
   already blocked only on `PluginEditor.*` calling `pushKeyboardNote()`) —
   not new work this document invents, but it's the actual prerequisite for
   *any* instrument type mattering outside `--capture` on the CLI. Generating
   five more instrument flavors nobody can play in the product is the same
   mistake `001-playground-shell.md:102-106` already named once this
   session's history: *"the prior session's work was real but invisible."*
4. **Polyphony** (`dsp_poly` adoption, per D5) — its own session, its own
   TSan run, gates any chord-capable Bucket A/B/C instrument type.
5. **The tempo/transport contract** (Bucket B) — needs an ADR (draft below)
   before any tempo-synced type is attempted.
6. **Tempo-synced DSP types** (self-contained sequencers/drum machines),
   built on step 5.
7. **Arpeggiator / chord memory as host features** (Bucket C) — independent
   track, can start any time after the keyboard widget's `NoteRing` producer
   pattern exists to plug into; does not wait on 2, 4, 5, or 6.
8. **Sample-based granular synthesis** (Bucket B) — its own smaller ADR for
   the file-reference contract, sequenced whenever, not blocking or blocked
   by 6/7.

---

## 4. Draft ADR text (proposed only — not written to `docs/decisions.md`)

Per the architecture-planning skill: drafted here for agreement, not
committed. Two decisions in this document are big enough to warrant one;
everything else in §2/§3 is either Tier 1 (a prompt/docs change) or explicitly
deferred without a decision needed yet.

<details>
<summary>ADR draft — Persisted, user-overridable generation `kind`</summary>

```markdown
## ADR-XXX — Persisted, user-overridable generation kind

Status: Proposed
Date: <today>

Context
Instrument/effect classification exists in three places today that agree by
luck (docs/sessions/001-playground-shell.md §1.4): the router's keyword
score, the compiled DSP's voice-contract detection, and two separate CMake
build targets. None is persisted. A misroute is silent — STATUS.md Broken #1
— and correcting it means rephrasing the prompt and hoping the router reads
it differently.

Decision
Add a UI selector before the prompt box, defaulting to the router's guess and
overridable before or after typing. The selected kind is sent as the
existing ADR-011 `kind` field (llm/generate.py:85-95 already accepts this;
only the host side is new), persisted in the state blob, and marked sticky
(kind_source: "user") across regeneration. The compiled DSP's voice-contract
check becomes a disagreement detector against the persisted kind rather than
the sole decider, surfacing a named, actionable mismatch instead of silence.

Reasons
- The override mechanism already exists end-to-end except the UI control
  (generate.py:85-95, :331, :380) — this is completing a designed feature,
  not inventing one.
- Removes the single largest source of the "generated nothing / silent
  patch" failure class named in router.py's own docstring.

Consequences
- New persisted field in the state blob (a contract between PluginProcessor
  and PromptPanel — COLLABORATION.md §2 trigger 3, hence this ADR).
- Every future type added to the taxonomy (§2 of session 004) becomes a
  selector option, not a new router keyword list to maintain in parallel.

Revisit if
The router's accuracy against the persisted-override data (once it exists)
turns out high enough that a selector adds friction without correcting real
misroutes — measurable only after this ships.
```
</details>

<details>
<summary>ADR draft — Host tempo/transport exposed to generated DSP</summary>

```markdown
## ADR-XXX — Expose host tempo/transport as a Faust control input

Status: Proposed
Date: <today>

Context
No generated patch has access to host BPM, PPQ position, or transport state
today (verified: zero references to AudioPlayHead/getPlayHead/bpm/tempo
anywhere in host/Source). Every tempo-synced plugin type (step sequencers,
self-contained drum machines, tempo-locked delay/mod effects) is blocked on
this regardless of prompt or stdlib content.

Decision
[Not drafted in detail here — this is the one item in session 004 that needs
its own design pass before an ADR is ready, not just a proposal. Candidate
shape: a reserved control-label convention analogous to voice_contract.json's
gate/freq/gain — e.g. a `bpm` label FaustEngine recognises and drives from
JUCE's AudioPlayHead on the audio thread, published the same RT-safe way
ParamPool::pushToFaust already publishes slider values today. Needs the same
canonical-source -> generated-consumer -> staleness-gate pattern Brief D
established for the voice contract, once the label convention is actually
decided.]

Consequences
Touches the audio thread's read path (AudioPlayHead::getCurrentPosition() is
called from processBlock or a thread feeding it) — Tier 2 evidence bar
applies in full: primary source citations against JUCE's AudioPlayHead
header, a runnable check, explicit unverified remainder.

Not decided here. Flagging the gap and the shape of a plausible answer;
someone should own the actual design pass before this ADR is filled in.
```
</details>

---

## 5. What I'd push back on

Two things worth saying plainly rather than routing around silently
(COLLABORATION.md §10):

- **"Any popular plugin type" is doing a lot of work in the request.**
  Sequencers, arpeggiators, and pattern-based drum machines are, by the
  taxonomy above, not really "more things for the LLM to generate" — they're
  either host features (Bucket C) or need architecture PluginForge doesn't
  have yet (Bucket B). Treating all of §2 as one backlog risks spending
  effort generating instrument *flavors* nobody can play (no keyboard
  widget) or DSP *categories* that can't function without tempo access that
  doesn't exist. The sequencing in §3 exists specifically to avoid that.
- **Polyphony is the real gate on "popular," more than any new type would
  be.** Most of what makes a synth feel like a real plugin (chords, pads,
  arpeggiated chords) needs it, and `001-playground-shell.md` already called
  it "the closest thing here to a one-way door." Expanding the type taxonomy
  before that lands produces more things that are technically instruments
  and practically toys.

## 6. Open questions for you

1. Does the deterministic selector (§1, step 1 of §3) get built next, ahead
   of the keyboard widget that's already STATUS.md's top item? They're
   independent, but only one can be next.
2. For the tempo/transport ADR draft (§4) — worth a dedicated design session
   before it's ADR-ready, or should that wait until a concrete Bucket-B type
   is actually being scoped?
3. Is the Bucket-C arpeggiator worth prioritizing precisely *because* it
   doesn't block on polyphony or the keyboard widget's completion — i.e., is
   it the cheapest "popular plugin type" win available right now?

---

## Change report (COLLABORATION.md §4)

```
CHANGED    docs/sessions/004-plugin-type-expansion.md (new, this file only)
WHY        User asked for a plan covering more plugin types and a
           deterministic pre-prompt type selector; this is that plan.
VERIFIED   router.py:1-95 (read in full) · generate.py:85-95,331,380 (kind
           override exists, unused by the host) · PromptPanel.{h,cpp}
           grepped for kind/instrument/effect, zero hits · host/Source
           grepped for AudioPlayHead/getPlayHead/bpm/tempo, zero hits ·
           libfaust-signal.h:368-377 and libfaust-box.h:408-426 (soundfile
           primitive exists) · docs/sessions/001-playground-shell.md §D4-D5,
           §1.4 (prior classification/polyphony analysis, cited throughout)
RISK       The Bucket B/C split is this session's own reasoning, not
           previously reviewed — it could be wrong about where an item
           belongs, particularly the granular-effect-vs-granular-synth
           split in §2, which rests on Faust capability I have not
           hand-verified by writing and compiling a grain-scheduling patch.
           The tempo ADR draft is deliberately incomplete (§4) — it names
           the gap accurately but does not design the solution.
YOUR MOVE  Answer §6, or tell me which of §3's numbered steps to start on.
           Nothing here is committed to docs/decisions.md yet.
```
