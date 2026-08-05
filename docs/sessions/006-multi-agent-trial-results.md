# Session 006 — Mechanism A's first trial: results

**Status: data point one, not a ratification.** This session ran four parallel/sequential
briefs under session 005's proposed `touches`/`depends`/`provides` declaration scheme
(`docs/sessions/005-multi-agent-safety-review.md` §1) as its first live trial, per that
document's own open `YOUR MOVE`. No hook was built. No COLLABORATION.md amendment was
made — the §11 draft there stays a draft.

---

## 1. Trial design

Four briefs (`docs/sessions/006-briefs/p{1,2,3,4}-*.md`), each declaring `touches`/
`depends`/`provides` at the top before any agent read the rest of its own file. Three ran
in parallel (P2, P3, P4); one (P1) was deliberately sequenced after P2, by hand, on the
strength of session 005's own stated limit — not run in parallel and observed to fail.
That is this trial's biggest limitation, named up front: **the refusal case below is a
correct application of the rule, not an instance of the rule catching a mistake that was
actually attempted.** A stronger trial would have run P1 ∥ P2 anyway and watched it break.
That was not done here, because the cost of a genuinely broken `PluginEditor` mid-session
was judged not worth it for a demonstration. Worth stating plainly rather than burying it.

## 2. Declared vs. actual, per brief

| Brief | Declared `touches` | Actual (`git diff --name-only` / `git status --porcelain`) | Verdict |
|---|---|---|---|
| P1 | `host/tests/EditorSessionTest.cpp` | `host/tests/EditorSessionTest.cpp` | **Honest** — exact match |
| P2 | `host/Source/ForgeLookAndFeel.h` (new), `PluginEditor.h`, `PluginEditor.cpp` | same three files | **Honest** — exact match |
| P3 | new files under `bench/` only; explicitly never `.prompt_baseline.json` | `bench/check_layered_voice_generalization.py`, `bench/results/layered_voice_generalization_20260805_172721.json`; `.prompt_baseline.json` confirmed untouched | **Honest** — exact match, negative constraint also honored |
| P4 | `STATUS.md`, `docs/ux_roadmap.md` | `STATUS.md` only | **Over-declared** — `ux_roadmap.md` was found already correct (a prior commit, `5090b55`, had fixed it before this session), so the brief's declared superset was never exercised. Harmless: the brief adjusted in place rather than forcing an edit to make the declaration retroactively accurate. |

Four for four honest at the file-set level. Zero under-declarations — the failure mode
session 005 §1 named as the "weak joint" (a brief that touches more than it declared,
producing a false safe) did not occur in this trial. That is a small, favorable data
point, and also exactly the kind of result one session cannot generalize from: four
briefs, all written by the same authoring pass in the same session, is a controlled
sample, not a representative one.

## 3. The refusal case, in full

P1 (`touches: host/tests/EditorSessionTest.cpp`) and P2 (`touches: host/Source/
ForgeLookAndFeel.h, PluginEditor.h, PluginEditor.cpp`) have **textually disjoint**
`touches` sets. A raw set-intersection rule — `touches ∩ touches = ∅` — reads this as
parallel-safe. It is not:

- P2's brief changed `PluginEditor`'s construction/destruction order: a new
  `ForgeLookAndFeel` member added, declared before the three child panels (destruction
  order requirement), plus a new first line in `~PluginForgeEditor()`
  (`setLookAndFeel(nullptr)`, required before `~LookAndFeel()` asserts).
- Every one of P1's new test scenarios constructs a `PluginForgeEditor` (via
  `EditorSessionTest.cpp`'s `struct Session`), so P1 **depends** on that construction/
  destruction order being what P2 leaves it as.
- No `CONTRACT.md` in this repo covers `PluginEditor`. Confirmed by grep before this
  session started: `host/Source/CONTRACT.md` covers `processBlock`, `runCompile`, and the
  message/audio thread split — not the editor. The only editor-related clause anywhere is
  one incidental mention in `INTERFACE.md:31` (editor destruction mid-generation), which
  does not cover construction order or member lifetime.
- Session 005 §1's own stated limit: *"an area with no `CONTRACT.md` yet is not
  parallel-safe by declaration alone... if the coupling isn't written down, the check that
  would catch a false safe doesn't exist."*

So P1's brief was written, before either agent ran, to sequence strictly after P2 landed
and `check.sh full` was confirmed green on it — not because the `touches` sets collided,
but because the `provides`/`depends` half of Mechanism A, applied by a human reading the
rule rather than by a hook, said the raw disjointness was not sufficient here.

**What this demonstrates and what it does not.** It demonstrates that the rule, applied
by hand, produces the correct call on a real case in this repository — the same shape of
near-miss session 005 §1 reconstructed from the `FaustEngine.cpp` Brief D/F history. It
does **not** demonstrate that a hook would have caught this automatically: no hook exists,
the sequencing decision was made by the orchestrator reading session 005's text and
applying judgment, not by a mechanical check running against declared `provides`/`depends`
sets. A `provides`/`depends` declaration for P2 that failed to name "PluginEditor
construction order" — plausible, since no contract exists to prompt the declaration — would
have produced a false safe under an automated version of this same mechanism. The rule
worked here because a human read it and reasoned from first principles about JUCE object
lifetime, not because the declared sets alone were sufficient input.

## 4. Recommendation on session 005's `YOUR MOVE`

Session 005 asked: build Mechanism A's `touches` hook and `provides`/`depends` check now,
with red cases, before using it for anything real?

**Not yet, on this trial's evidence — and the reason is the trial's own design, not a
verdict on the mechanism.** n=4 briefs, one deliberate sequential case decided by hand
rather than observed failing in parallel, all authored in a single pass by one agent in
one session. That is not enough data to justify building a `PreToolUse` hook, for two
concrete reasons pulled from this trial specifically:

1. **The one interesting case (§3) was not actually run unsafely and caught.** It was
   reasoned about and avoided. A hook's value is proven by catching a mistake that was
   genuinely attempted, per CLAUDE.md's "a control counts only once it has been seen
   failing." This trial has not produced that observation yet.
2. **All four `touches` declarations were honest.** Good news for the humans who wrote
   these briefs in this session; no evidence at all about whether that holds under
   adversarial or careless brief-writing, which is the actual failure mode the hook exists
   to guard against. A trial where nothing under-declares cannot show whether the gate
   would have caught an under-declaration.

**What would change this recommendation:** a second trial, ideally one where a brief pair
with a plausible-looking-but-actually-coupled `touches` disjointness is run in parallel
for real (not sequenced around by hand), and the `touches ∩ touches = ∅` / `provides ∩
depends` checks are computed mechanically against the landed diffs rather than eyeballed
by the orchestrator as this session did. That is the trial session 005 originally
specified for Mechanism B's canary pilot, adapted to Mechanism A: run it for real, watch
it either catch or miss something, then decide.

This document does not amend COLLABORATION.md. Session 005 §11 remains a draft.

---

## Change report (COLLABORATION.md §4)

```
CHANGED    docs/sessions/006-multi-agent-trial-results.md (new, this file only)
WHY        Session 005 left open whether Mechanism A (touches/depends/provides
           parallel-safety declarations) should become process. This session's
           own multi-agent structure was the first live trial; this records it.
VERIFIED   Declared touches sets read directly from docs/sessions/006-briefs/
           p{1,2,3,4}-*.md. Actual sets from `git diff --name-only` and
           `git status --porcelain` run against the landed worktree/main-tree
           state for each brief, not recalled. host/Source/CONTRACT.md and
           INTERFACE.md re-read to confirm no PluginEditor contract exists.
           docs/sessions/005-multi-agent-safety-review.md §1 re-read in full
           before writing §3-4 above.
RISK       This is one session's data, explicitly flagged as insufficient for a
           build decision in §4 above — the main risk is a future reader citing
           "four honest declarations" as stronger evidence than it is. The
           refusal case (§3) was a human judgment call, not a mechanically
           observed catch, which is the central limitation of this trial and is
           stated as such, not softened.
YOUR MOVE  Decide whether a second, adversarial trial (§4's proposal) is worth
           running before any hook gets built, or whether this stays parked
           until a real under-declaration happens in the wild.
```
