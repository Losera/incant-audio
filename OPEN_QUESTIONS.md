# OPEN_QUESTIONS.md

## Q1 — Does I/O topology (channel count) belong in a PluginSpec, and if so where? — RESOLVED 2026-08-04

**Resolution:** option 1 below ("no PluginSpec field — read it live off the
compiled dsp instance"). Decided in `docs/decisions.md` ADR-021's 2026-08-04
addendum and implemented by Brief F's post-compile validation gate
(`FaustEngine.cpp`'s `validatePatch()`, called from `runCompile()` before the
atomic swap): `dsp->getNumInputs()`/`getNumOutputs()` are read at compile
time and checked against `FaustEngine::kMaxChannels`, with no new struct or
stored field. Left as originally written below for the record of what was
weighed.

**Raised by:** Brief A, `spec-evidence/structure-gaps.md` §1 and §4.

**The finding.** All 19 `bench/ladder_corpus.json` entries have an input/output
channel count (verified via `faust -json`), and none of it is captured by
`ParamInfo` (`host/Source/FaustEngine.h:66-101`) — which is a deliberately
per-parameter struct, so this was never a field it was missing so much as a
fact it was never asked to carry. Two entries (10, 18) compile to 4 inputs / 2
outputs, twice what a stereo host bus supplies; one entry (1) compiles to a
channel topology that is the reverse of its prompt. Nothing read in this brief
checks I/O topology against anything.

**Why this is a decision, not a task.** Answering it changes at least one of:
a public interface (would `FaustEngine` need to expose
`getNumInputs()`/`getNumOutputs()` beyond what it may already have
internally via libfaust?), a data format (does a `PluginSpec` exist at all,
and if so does it carry patch-level facts or only parameter-level ones?), and
possibly a module boundary (does I/O-topology validation belong in
`FaustEngine`, in the compile-callback path, in `ParamPool::remap`, or
somewhere new?).

**Options observed, not chosen:**
1. **No PluginSpec field at all — read it live off the compiled `dsp`
   instance.** `FaustEngine` already holds the compiled DSP and presumably can
   ask it for input/output counts at compile time (libfaust's `dsp` interface
   exposes `getNumInputs()`/`getNumOutputs()`); if so this needs no new
   storage, only a call site — e.g. failing/warning the compile callback when
   the count doesn't match the host's bus layout. Cheapest option if the
   accessor already exists; not verified against `FaustEngine.h`/`.cpp` beyond
   the parameter-capture code this brief was scoped to.
2. **A `PluginSpec` (or equivalent) that records I/O topology as a
   patch-level field alongside the `ParamInfo` list.** Only worth the type if
   there's a second consumer beyond a compile-time check (e.g. persisted
   state, the editor, a future MIDI/bus-mapping UI) — this brief found no
   second consumer in the files read.
3. **Leave it unrepresented and unchecked**, on the reasoning that entries 1,
   10, and 18 are prompt/generation-fidelity defects (the LLM should not have
   written 4-input Faust for a 2-in/2-out host), not a runtime-extraction gap
   — i.e. fix it upstream in the prompt/generation loop, not by adding
   spec surface to represent a state that shouldn't be generated in the first
   place.

**Not decided here.** This brief's job was evidence, not the schema.

## Q2 — Per-channel signal-graph relationships (entries 9, 14): worth a spec field?

**Raised by:** Brief A, `spec-evidence/structure-gaps.md` §1.

Entry 9 (chorus) derives its right-channel `rate` from the same `ParamInfo` as
the left channel, transformed (`rate * 1.13`) inside the signal graph — not in
any metadata `ParamInfo` carries. Entry 14 (expander) processes only its left
channel and passes the right through dry — a routing asymmetry, not a
parameter fact. Both are two entries out of 19.

**Why this is a decision, not a task.** Representing either would mean
`PluginSpec` describing signal-graph relationships between parameters and
channels, not just parameter metadata — a materially larger scope than what
`ParamInfo` does today, and arguably duplicative of what the Faust source
already expresses (see `spec-evidence/structure-gaps.md` §4's point that Faust
is already the algorithm-topology representation, per `CLAUDE.md`'s
"Faust chosen over JSON IR" decision).

**Not decided here.** Flagging because 2/19 is a thin evidence base either way
— thin enough to justify doing nothing, and thin enough that a larger corpus
could change the answer. Whoever owns the schema decision should treat this as
a "watch, don't build" item rather than a yes/no answered by this brief.

## Q3 — Should the validation gate's param-count overflow be fatal (as briefed) or a warning (as shipped)?

**Raised by:** Brief F, implementing `FaustEngine.cpp`'s post-compile
validation gate.

**The conflict.** Brief F's task list says plainly: "parameter count exceeds
the 64 macro slots → fatal, with the count." Implementing that literally
broke `host/tests/EditorSessionTest.cpp`'s existing, deliberate "a 70-param
patch compiled and settled at the pool ceiling" test — which pins PF-051
(`PluginProcessor.cpp`'s `loadFaustCode`): a patch with more controls than
the 64-slot pool holds already compiles and runs today, with the first 64
mapped and the rest logged as unreachable
(`remapResult.overflowed` → `juce::Logger::writeToLog`), not refused.
PF-051's own comment states the reasoning: "the DSP is live and correct, and
64 of its controls do work, so refusing the patch outright would be a worse
answer than a partial one the user is told about."

**What shipped.** `validatePatch()`'s `param-count` finding is marked
`fatal: false` — a warning, matching PF-051, not the brief's literal text.
This was a live behavioural conflict discovered by actually running the
existing test suite (`tools/check.sh full`), not a guess: making it fatal
turned a passing, documented test red. The overflow warning is logged
exactly as the check's log line already does elsewhere ("`PluginForge:
validation gate warning [param-count] ...`"), same idiom as the pre-existing
PF-051 line, so a user generating an oversized patch now sees the condition
reported twice, once from each of the two log sites.

**Why this is a decision, not a task.** Making param-count genuinely fatal
would mean *removing* PF-051's graceful-degradation behaviour (a > 64-control
patch stops compiling at all), which is a real product-behaviour change to a
tested, shipped feature — not something a validation-gate task should decide
by itself.

**Options observed, not chosen:**
1. **Keep it a warning (shipped).** Consistent with PF-051; no behaviour
   change; the redundant two-line log is the only cost.
2. **Make it fatal and retire PF-051's graceful degradation.** Simpler
   mental model (one control ⇒ one outcome), but it is a strictly worse
   experience for exactly the case PF-051 was written to soften, and
   requires deciding what to do with `RemapResult::overflowed` (dead code)
   and rewriting/deleting the EditorSessionTest fixture that currently
   documents the old behaviour as correct.
3. **Fatal only over some higher, separate ceiling** (e.g. 2x or 4x
   POOL_SIZE), on the theory that PF-051's soft landing is fine for "a few
   over" but a wildly oversized patch (hundreds of controls) is more likely
   a generation-quality failure than an ordinary overflow. No evidence
   gathered for where such a line would sit.

**Not decided here.** Whoever owns the PF-051 policy should choose.

## Q4 — Presentation checker's affordance 8 reads source text, against the brief's own method

**Raised by:** implementing `bench/presentation_checker.py` (Presentation
affordance checker brief).

**The conflict.** The brief's method is explicit: "compile and inspect the
JSON, do not regex the source." Seven of the eight affordances are fully
answerable from `faust -json`'s own UI tree. Affordance 8 ("every parameter
has a non-empty label distinct from its variable name") is not: the JSON's
"varname" field is a compiler-generated internal name (`fHslider0`, ...),
never the Faust DSL identifier the brief's "variable name" plainly means —
verified against a live compile (`probe.dsp`, scratch, not committed).
There is no JSON fact to compare a label against for this one check.

**What shipped.** `_labels_distinct_from_varnames()` reads the .dsp source
with a regex scoped to exactly one shape — `ident = hslider("label", ...)`
— extracting (identifier, label) pairs and stripping `[...]` metadata tags
the same way the compiler does, then compares. This is narrower than what
Brief A's methodology warned off (regexing GROUP/WIDGET-TYPE structure,
which Faust's syntax makes genuinely ambiguous — aliasing, `with{}`,
computed groups); a single assignment's shape is comparatively low-risk.
When the pattern matches fewer widgets than the JSON reports (an aliased
or `with{}`-local declaration), the result records
`labels_check_coverage` rather than silently claiming a verdict it can't
support.

**Options observed, not chosen:**
1. **Keep the narrow source read (shipped).** Answers the check as
   literally worded; the risk is confined to widgets whose declaration
   isn't a plain `ident = widget(...)` assignment.
2. **Redefine affordance 8 as JSON-only**, e.g. "label does not read as a
   bare identifier" (lowercase, no spaces/punctuation) — answerable purely
   from the JSON `label` field, but answers a DIFFERENT question than
   "distinct from its variable name" and was not chosen because it departs
   further from the brief's literal wording than option 1 does from its
   literal method.
3. **Drop affordance 8.** Rejected — the brief names it as one of the
   eight, and the corpus result (17/19 pass) is itself an unexpected,
   reportable finding (see the checker's own summary output), not evidence
   the check is unneeded.

**Not decided here.** A future session tightening this should read
`_labels_distinct_from_varnames`'s docstring first.

## Finding — the near-0/8 prediction did not hold, on two of eight affordances

`bench/presentation_baseline.json` (all 19 entries compile and score):
mean **1.74/8**, not near 0. Six of eight affordances ARE exactly 0/19,
confirming the prediction precisely: `any_hgroup`, `any_nested_group`,
`any_non_hslider_widget`, `any_checkbox_or_button`, `any_style_knob`,
`any_log_or_exp_scale`. Two are not: `any_unit_metadata` is **16/19**
(e.g. entry 3, `"Cutoff [unit:Hz]"`) and `labels_distinct_from_varnames`
is **17/19**. No single entry exceeds 2/8 (the brief's literal per-entry
falsification line), so that specific test survives — but the AGGREGATE
"near-0/8" claim, and specifically the "[unit:...] metadata: 0/19" row in
the pluginmaker.ai-derived affordance table, do not. The generator already
reliably tags units on at least one parameter per patch (usually the first
one); it never groups, styles, or log-tapers them. Reported per the
brief's own instruction — the checker was not adjusted to make this
disappear.

## Q5 — presentation_block.txt's canonical location: llm/prompts/ (as briefed) vs llm/ — RESOLVED 2026-08-04

**Resolution:** `llm/presentation_block.txt` (one level above `llm/prompts/`),
not `llm/prompts/presentation_block.txt`. Decided interactively with the user
in the session that did the move, immediately before this brief; this entry
records it for a reader who only has the brief.

**Raised by:** this brief's step 1, "Move the block to
`llm/prompts/presentation_block.txt` as the canonical hand-edited source."

**The conflict.** `.claude/hooks/check_prompt_invariants.py:58` runs on every
Write/Edit/MultiEdit to a path matching `llm/prompts/[A-Za-z0-9_-]+\.txt$` and
blocks (exit 2) unless the file contains the ADR-009 duplicate-symbol
sentence, a `process ... exactly once` clause, and intact `# BEGIN/END
GENERATED STDLIB REFERENCE` markers
(`check_prompt_invariants.py:200-227`). That directory-wide reach is
deliberate — its own docstring (lines 1-13) says it was generalised from one
hard-coded filename specifically so a new file placed under `llm/prompts/`
could not go unguarded. `presentation_block.txt` is a fragment: grouping/
widget/scale rules only, none of the three required elements. Writing it at
the briefed path fails closed on the first `Write`.

**Options observed:**
1. **Move the canonical file one level up, to `llm/presentation_block.txt`
   (chosen).** The hook's regex requires `llm/prompts/` immediately followed
   by the filename with no further `/`, so a path outside that directory
   never matches it — not a bypass of the regex's letter, a file placed
   where the invariant it enforces (every *prompt* teaches only real Faust)
   does not apply, because this file is never sent to a model on its own.
   The GENERATED consumer, `llm/prompts/system_prompt_presentation.txt`,
   still lives exactly where the brief asked, and passes the hook cleanly
   because it is base + block: `system_prompt.txt` already carries all three
   required elements, and the appended block was checked and contributes no
   fabricated `ns.func` token and no foreign-language construct
   (verified via `check_prompt_invariants.foreign_construct_problems` and
   `gen_stdlib_block.verify_prompt_references`, both returning `[]` against
   the generated file).
2. **Carve a fragment exemption into the hook** (e.g. a `*_block.txt` naming
   convention skipped for the full-prompt checks but still scanned for
   fabrication/foreign constructs). Not chosen — it edits a safety hook to
   accommodate one file, which is a larger, harder-to-reverse change than
   moving the file, for a task whose own rules say to stop at exactly this
   kind of decision rather than make it unilaterally.
3. **Make the block a self-sufficient mini-prompt** (add the ADR-009
   sentence, a process-once clause, and empty stdlib markers to
   `presentation_block.txt` itself). Not chosen — it would satisfy the hook
   syntactically while adding content to the canonical source that has
   nothing to do with control presentation, solely to appease a check meant
   for complete prompts.

**Not decided here beyond the file's location.** Whoever revisits the hook's
directory-wide reach (option 2) as a general capability, independent of this
one file, should treat that as a separate decision.

## Q6 — system_prompt_presentation.txt fails tests/test_prompt_headroom.py's guard — NOT RESOLVED, block not trimmed

**Raised by:** wiring the A/B selector brief's step 5 ("Report the token count
of both prompts and confirm the variant still passes
tests/test_prompt_headroom.py. If it does not, stop and report the numbers
rather than trimming the block.")

**The finding**, computed with `providers.estimate_tokens`/`headroom_tokens`
(the same functions `tests/test_prompt_headroom.py` asserts against; that
file itself parametrizes only `system_prompt.txt`, so it does not exercise
the variant directly):

| | base (`system_prompt.txt`) | presentation (`system_prompt_presentation.txt`) |
|---|---|---|
| chars | 11,992 | 13,528 |
| est. tokens | 3,698 | 4,171 |
| `MAX_OUTPUT_TOKENS` | 4,096 | 4,096 |
| slack vs groq's 8,000 TPM admission | **+206 (PASS)** | **-267 (FAIL)** |

The `llm/presentation_block.txt` addition costs 1,536 chars / ~473 estimated
tokens — enough to flip the request from admitted to a non-retryable groq 413
("Request too large") on every single request, not a slowdown. This is the
exact failure class `tests/test_prompt_headroom.py`'s module docstring warns
about, just triggered by a prompt edit instead of a Faust stdlib upgrade.

**Why this is a decision, not a task.** Fixing it means choosing one of:
lowering `MAX_OUTPUT_TOKENS` for this variant (reintroduces the truncation
confound `docs/research/truncation-confound-HANDOFF-S1.md` already fixed
once, per that same test file's warning), restricting the variant to a
provider with a larger context window (a different free-tier budget than
groq's), or trimming `llm/presentation_block.txt` (explicitly ruled out by
the brief this session was given). None of those is a code change with an
obvious right answer from inside this scoped task.

**What shipped.** The variant is generated, staleness-checked, and selectable
exactly as briefed — `PLUGINFORGE_PROMPT_VARIANT=presentation` / bench's
`--prompt-variant presentation` will select it — but running it against groq
specifically will fail with a 413 on essentially every request until this is
resolved. `tests/test_prompt_headroom.py` was deliberately NOT extended to
parametrize the variant and fail red on this: the brief said to report the
numbers, not add a failing gate.

**Not decided here.** Whoever runs the first `--prompt-variant presentation`
benchmark should read this before choosing a provider, or should resolve one
of the three options above first.
