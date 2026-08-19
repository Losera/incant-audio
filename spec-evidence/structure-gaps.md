# structure-gaps.md — Brief A: Structure extraction for PluginSpec

**Frozen point-in-time record, 2026-08-04. Not maintained.** Referenced by
`bench/presentation_checker.py` for provenance.

Read-only evidence pass over `bench/ladder_corpus.json` (19 recorded results),
cross-checked against what `host/Source/FaustEngine.h:66-101` (`ParamInfo`),
`FaustEngine.cpp:239-260` (`extractVoiceControls`), `ParamIdentity.h`,
`ParamPool.cpp` (`remap`), and `ParamGridLayout.h:20-38` actually extract and
represent today.

**Method note, stated per the Tier-2 evidence bar:** per-entry I/O counts and
widget structure below are not hand-parsed guesses — each entry's `code` field
was written to a `.dsp` file and compiled with the installed `faust -json`
(2.85.9, matches `CLAUDE.md`'s pinned toolchain) to get the compiler's own
`inputs`/`outputs`/`ui` metadata. That is the same JSON shape `ParamCapture`
consumes conceptually, so it is a faithful proxy for what the runtime would see
on `buildUserInterface`, not a re-derivation of Faust semantics from memory.
Scratch files: `/tmp/.../scratchpad/ladder/e00.dsp.json` … `e18.dsp.json`
(this session only, not part of the repo).

## Per-entry table

Columns: **I/O** = compiler-reported inputs→outputs. **Params** = count of
captured `hslider`s (100% of widgets in this corpus — see §2). **Voice labels**
= any of `gate`/`freq`/`key`/`gain`/`vel`/`velocity` present as an *exact*
param label (case-sensitive, matching `FaustEngine.cpp:252-257`). **Ungrouped**
= true unless the entry uses `vgroup()`/`hgroup()`/`tgroup()` inside the DSL
(the compiler-inserted outer `vgroup` named after the filename doesn't count —
`FaustEngine.h:83-87` already documents that box as not a real group).
**Gap** = structure found that none of `ParamInfo`'s fields (label, min, max,
step, kind, scale, unit, group, isMenu) can represent.

| # | category | I/O | Params | Voice labels | Ungrouped | Gap found |
|---|---|---|---|---|---|---|
| 0 | trivial | 2→2 | 1 | none | yes | none |
| 1 | trivial | 2→1 | 0 | none | yes | **I/O topology contradicts prompt** ("mono→stereo duplicator" compiles to stereo→mono) — see §4 |
| 2 | trivial | 1→1 | 0 | none | yes | none — zero-param patch is already representable (empty list) |
| 3 | filters | 2→2 | 2 | none | yes | none |
| 4 | filters | 1→1 | 1 | none | yes | none |
| 5 | filters | 1→1 | 2 | none | yes | none |
| 6 | filters | 2→2 | 2 | none | yes | none — same 2 `ParamInfo`s drive both channels; one capture, no gap |
| 7 | time-based | 2→2 | 3 | none | yes | none |
| 8 | time-based | 2→2 | 3 | none | yes | none — cross-feedback (`with{}` block, `<:`/`:>`) is DSP topology inside Faust, not param/UI metadata |
| 9 | time-based | 2→2 | 3 | none | yes | **per-channel derived-value divergence**: `chorusCh(rate), chorusCh(rate*1.13)` — R channel runs the *same* `rate` param through a `*1.13` offset baked into the signal graph. `ParamInfo` has one `rate`; nothing records that channel 2 detunes it. |
| 10 | time-based | **4→2** | 3 | none | yes | **I/O arity mismatch**: this "stereo flanger" needs 4 audio inputs, not 2 — see §4 |
| 11 | dynamics | 2→2 | 4 | none | yes | none |
| 12 | dynamics | 1→1 | 4 | none | yes | none — mono-only "mastering limiter" is a category-level I/O fact, not a param gap |
| 13 | dynamics | 1→1 | 2 | none | yes | none for `ParamInfo` — attack/hold constants (`20`, `100`) are hardcoded in Faust, never reach `buildUserInterface` at all, so there is nothing for `ParamInfo` to fail to capture (see §4, not counted as a gap) |
| 14 | dynamics | 2→2 | 4 | none | yes | **asymmetric channel routing**: `process = co.compressor_mono(...), _;` — L is compressed, R passes through dry. Same 4 params either way; the routing asymmetry itself is invisible to `ParamInfo`. |
| 15 | generative | 0→1 | 2 | none | yes | none for params; 0-input topology is an I/O fact (§4) |
| 16 | generative | 0→2 | 2 | none | yes | none for params; base frequencies (440/880) are hardcoded per channel, not params — same as #13, not a `ParamInfo` gap |
| 17 | generative | 0→1 | 1 | none | yes | none |
| 18 | generative | **4→2** | 3 | none | yes | **I/O arity mismatch**, likely missing `~` recursion (see §4) |

## 1. Gap frequency rollup

Rolling up only the column that answers the brief's actual question — structure
**present in the generated plugin and reachable at `buildUserInterface`/
`process` time, that `ParamInfo`'s field set cannot represent** — not
generation-quality complaints (hardcoded constants, wrong topology vs. prompt
intent), which are a different failure class entirely (see §4 for why those
are excluded).

| Gap category | Entries | Count / 19 |
|---|---|---|
| Per-channel derived-value divergence (same param, transformed differently per channel) | 9 | 1 |
| Asymmetric channel routing (one channel processed, one dry) | 14 | 1 |
| **None — fully representable by existing `ParamInfo`** | 0,2,3,4,5,6,7,8,11,12,13,15,16,17 | 14 |

That leaves **2 of 19 (11%)** with any real per-parameter/per-UI structural
gap against `ParamInfo`, and even those two are not fields `ParamInfo` is
missing — a per-channel routing/derivation relationship is not parameter
metadata at all; it is a statement about the *signal graph*, which is a
different kind of fact than "this control's min is 20."

**Separately, and not folded into the count above because it is not a
parameter-level fact:** input/output channel count (I/O topology) is not
carried by `ParamInfo` in any entry, because `ParamInfo` is per-parameter, not
per-patch — see §4. If a `PluginSpec` is meant to describe the *whole patch*
rather than just its parameters, I/O topology is the one piece of structure
every single one of the 19 entries has and none of them expose today. This is
the closest thing to a "real" gap in the corpus, and it is not a parameter
field — it does not belong on `ParamInfo` at all; it is a property `FaustEngine`
already computes (`dsp->getNumInputs()`/`getNumOutputs()`) but does not appear
to be captured or surfaced anywhere in the files read for this brief. **This is
an architectural question, not a schema field — flagged in
`OPEN_QUESTIONS.md`, not decided here.**

## 2. UI layout: anything beyond `columnsFor`/`rowsFor` grid arithmetic?

**None. Zero of 19 entries.** Checked directly against the compiled Faust
`-json` UI tree (not the source text, which can't prove what actually reaches
the runtime):

- **Grouping:** 0/19 use `vgroup()`/`hgroup()`/`tgroup()`. Every entry's UI
  tree is exactly one compiler-inserted outer `vgroup` (named after the
  filename) directly containing a flat list of `hslider`s — the case
  `FaustEngine.h:83-87` already documents as "a parameter declared outside any
  group... has an EMPTY path."
- **Control-type preference:** 0/19 use anything but `hslider`. No
  `vslider`, `nentry`, `button`, `checkbox`, or bargraph appears anywhere in
  the corpus (widget-type census across all 19 compiled UI trees: `{hslider,
  vgroup}` only). `ParamInfo::Kind`'s five other variants (`VSlider`,
  `NumEntry`, `Button`, `CheckButton`, `Meter`) and `isMenu` are entirely
  unexercised by real generated output in this sample.
- **Ordering:** source declaration order varies entry to entry (e.g. entry 11
  writes `thresh, ratio, attack, release` — a plausible "logical" order), but
  this cannot motivate a PluginSpec ordering field: `FaustEngine.h:103-107`
  already states Faust emits both groups and widgets **alphabetically**,
  and the editor preserves that order rather than the source order. Whatever
  ordering intent a prompt or the LLM's source layout might carry is already
  discarded before `ParamInfo` exists — it never reaches the runtime to be a
  gap in the first place.
- **Emphasis:** no `[style:...]` or comparable metadata tag appears in any
  entry (grepped `\[(scale|style):[^\]]*\]` against all 19 `code` fields: zero
  matches).

**This is the negative result the brief asked to flag if true.** Against this
19-entry sample, there is no UI-layout intent beyond count-driven grid
arithmetic for `columnsFor`/`rowsFor` to be missing. A grouping/ordering/
control-type field in a PluginSpec would currently be motivated by zero
observed entries — exactly the "naming a field no entry motivates" case the
brief says to stop on. Not stopping, because the honest report *is* the
negative result: nothing here justifies that part of a schema today. A larger
or differently-sourced corpus (this one is 19 outputs from one model,
`qwen2.5-coder:7b-16k`, all `ollama`) could change this; this brief's evidence
does not.

## 3. Compile-failure correlation

**No correlation to report — there are no failures.** All 19 entries have
`first_try_compiles: true` and an empty `error` string (verified by loading
the corpus and printing both fields for every entry). Every structural
category above, including the two I/O arity mismatches in §4, compiled
cleanly; `faust -json` produced no warnings or errors for entries 10 or 18
either, when recompiled directly. The corpus as given cannot answer "does a
structural category predict a compile failure," because it contains zero
compile failures to correlate against.

## 4. What was found but is explicitly NOT counted as a `ParamInfo` gap, and why

Four things surfaced during this pass that are real and worth recording, but
none of them are evidence for a `PluginSpec` schema field, because none of
them are something `ParamInfo` failed to capture — they never reach
`buildUserInterface` at all, or they are patch-level rather than
parameter-level facts:

- **I/O topology (input/output channel count) is not on `ParamInfo` anywhere**,
  because `ParamInfo` is a per-parameter struct — it was never going to carry
  a patch-level fact. Two entries make this concrete rather than academic:
  - **Entry 1** ("a mono-to-stereo converter that duplicates the channel")
    compiles to **2 inputs → 1 output** — the opposite of the prompt. This is
    a generation-correctness defect (out of this brief's scope per the task
    definition), but it also demonstrates that nothing downstream of Faust
    compilation currently checks I/O topology against anything — not a
    `ParamInfo` gap, but exactly the kind of fact a patch-level spec would
    need to carry if it carried anything beyond parameters.
  - **Entries 10 and 18** both compile to **4 inputs → 2 outputs** — twice
    what a stereo in/out effect or instrument needs. Reading the source: entry
    10's `flangerCh(x) = pf.flanger_mono(maxDel, curdel, depth, fb, 0)` and
    entry 18's `karplusStrong(x) = (x + feedback) * gain` (with `feedback`
    defined as a free-standing unary chain, `feedback = delayLine :
    *(pluckPos)`, never tied back with Faust's `~` recursion operator) each
    leave a signal input unconnected to anything a stereo host bus would fill.
    This reads as a missing `~` in entry 18 specifically — the "Karplus-Strong"
    patch has no feedback loop at all, so it is very unlikely to self-sustain
    as a plucked string. This is a DSP-correctness finding, not a schema gap;
    recorded here because it was surfaced by the same I/O check.
  - Whether a `PluginSpec` should carry I/O topology at all, and if so whether
    that lives on `FaustEngine` (which already has `dsp->getNumInputs()/
    getNumOutputs()` available, per the JIT wrapper's own libfaust interface)
    rather than on a new spec type, is an **architectural decision** — see
    `OPEN_QUESTIONS.md`.
- **Hardcoded constants that never became controls** (entry 13's attack=20ms/
  release=100ms baked into `ef.gate_mono(thresh, 20, hold, 100)`; entry 16's
  base frequencies 440/880 baked into `fmOsc(os.osc(440), ...)`,
  `fmOsc(os.osc(880), ...)`) are invisible to `ParamInfo` by construction —
  Faust's `buildUserInterface` never calls back for a bare numeric literal.
  There is nothing for the runtime extraction to fail at; the value simply
  isn't a widget. This is a prompt/generation-fidelity question (should the
  model have exposed these as sliders), not a structural-representation gap,
  so it is excluded from §1's rollup.
- **DSP algorithm topology** (the `with{}` local-function blocks in entries 8
  and 9, the `~` feedback recursion in entry 7, the split/merge `<:`/`:>`
  routing in entry 8) is exactly the kind of structure Faust-as-DSL exists to
  carry, and it already does — that's the whole point of generating Faust
  rather than an IR (`CLAUDE.md`, "Faust chosen over JSON IR"). None of it
  needs a parallel representation in a `PluginSpec`; the compiled `.dsp`
  source already is that representation.
- **Shared-parameter reuse across channels** (entries 3, 6, 7, 8, 11: the same
  Faust variable, hence the same captured `ParamInfo`, driving both L and R
  chains) is already handled correctly — Faust deduplicates by path, so
  `ParamCapture` emits exactly one `ParamInfo` regardless of how many places
  in the signal graph read it. Not a gap.

## Headline answer

**14 of 19 entries (74%) need nothing beyond what `ParamInfo` already gives.**
Of the remaining 5: 2 (entries 9, 14) show a real signal-graph-level
relationship (per-channel divergence / asymmetric routing) that is genuinely
outside `ParamInfo`'s scope by design, not a missing field on it; 2 (entries
10, 18) surfaced an I/O-arity anomaly that is a DSP-correctness question, not
a parameter-metadata one; and entry 1 surfaced a topology-vs-prompt mismatch,
also not a parameter-metadata question. Zero entries motivate a UI-layout
field beyond `columnsFor`/`rowsFor` (§2's negative result). The one recurring,
patch-wide fact absent everywhere is I/O channel count/topology — and it does
not belong on `ParamInfo`, because `ParamInfo` is deliberately per-parameter.
Whether that fact needs a `PluginSpec` at all, or is better read directly off
the live `FaustEngine`/`dsp` instance the way `getNumInputs()`/
`getNumOutputs()` presumably already can be, is the one open architectural
question this brief surfaced — see `OPEN_QUESTIONS.md`.
