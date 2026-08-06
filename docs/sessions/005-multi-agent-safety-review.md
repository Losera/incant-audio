# Session 005 — Multi-agent safety review: proposal and critique

**Status: proposal and critique only.** No code, no agent definitions, no
new root-level file. This is a COLLABORATION.md §2 consult-gate
conversation — the human decides at the end.

**Conceded up front, not re-argued:**
1. "Parallelize the decision" was imprecise language for a sequential
   classification step.
2. A self-grading orchestrator agent has the shape of the §9 three-mode
   protocol's `Mode signal` field. Dropped.
3. A merge-conflict-resolution agent near `FaustEngine.cpp` / the atomic
   swap protocol is unacceptable — semantic conflicts there are a human's
   job. Dropped.
4. An agent that writes agent definitions amplifies its own defects across
   everything it spawns. Dropped.
5. Process belongs in COLLABORATION.md. No `AGENTS.md`.

Two mechanisms survive the principle "multiple agents for generation and
verification, never for adjudication." This document attacks both.

---

## 1. Mechanism A — deterministic parallel-safety check

**The claim:** each task brief declares `touches`, `depends`, `provides`.
Two briefs are parallel-safe iff `touches ∩ touches = ∅`,
`provides_A ∩ depends_B = ∅`, `provides_B ∩ depends_A = ∅`. Set
intersection in Python.

### The §9 test

Split the claim in two, because the two halves fail differently.

**`touches ∩ touches = ∅`, given honest input, is not self-graded.** The
check and the thing it checks are different artifacts: `git diff
--name-only` on a landed change is ground truth about what was actually
written, independent of anything the mechanism itself produced. This is the
same shape as `check_bash_denylist.py` (COLLABORATION.md §7) — a fail-closed
gate over an enumerable set, no model in the loop. It can be made
**preventive, not just retrospective**: a `PreToolUse` hook that rejects a
`Write`/`Edit` outside a session's declared `touches` set is mechanically
identical in kind to `check_doc_naming.py` (§7) blocking a `Write` on a
declared property of the path. This half passes the §9 test outright.

**The declared sets themselves are not verified, and this is the weak
joint the task asks me to stress.**

> A depends on briefs declaring `depends` and `provides` honestly. A brief
> that under-declares produces a false safe. Is there a deterministic check
> on the declaration itself, or does this just relocate the self-grading
> problem into the brief author?

Partial answer, not a clean one. `touches` under-declaration is fully
closable — see above, a hook can prevent it outright, so under-declaring
`touches` simply means the write is refused, not that it silently succeeds.
`depends`/`provides` under-declaration is **not** fully closable the same
way, because a "contract" isn't an enumerable file path — it's an
assumption about behavior. But it is *narrowable*, using something this
repo already built and I read in full before writing this: the four
`CONTRACT.md`/`INTERFACE.md` files landed in commit `54957b5`
(`docs/sessions/003-summary.md`), each of which cites specific `file:line`
anchors for the mechanism it describes — e.g. `host/Source/PARAM_CONTRACT.md`
names `ParamPool.cpp:38-160` for `remap()` and `ParamMap.h:83,135` for
`mapSlotToZone`/`mapZoneToSlot`. A landed diff that touches lines inside a
cited contract range **without that contract appearing in the brief's
declared `provides`** is mechanically detectable — line-range intersection
against a document that already exists, no model judgment. This is buildable
today, not aspirational.

**The residual risk, stated plainly rather than buried:** this only catches
under-declaration against a contract that is *already written down*. It does
nothing for a coupling nobody has documented yet — which is precisely the
case that actually happened in this repository, not a hypothetical one.

### The concrete scenario — this already happened here

`host/Source/FaustEngine.cpp`'s Brief D (voice-contract refactor) and Brief F
(post-compile validation gate) were developed as if independent, and landed,
when finally split, as **233 inserted / 46 deleted lines in one diff,
sharing a single hunk** — the `#include "VoiceContract.generated.h"` line
(Brief D) sat directly adjacent to the `#include "ParamPool.h"` line (Brief
F) with no blank line between them (`docs/sessions/003-summary.md`'s commit
history; I split this by hand two sessions ago and verified the shared hunk
directly with `git diff`). If these had been run as two parallel briefs under
Mechanism A:

- Both correctly declaring `touches: [host/Source/FaustEngine.cpp]` would
  have been caught — `touches ∩ touches ≠ ∅`, refused, correctly. This is
  Mechanism A working as designed.
- But a brief for "add the post-compile validation gate" could plausibly
  have been scoped by its author as `touches: [host/tests/ValidationGateTest.cpp,
  host/CMakeLists.txt]` only — reasonable if the author assumed the gate
  function could live in a new file, not realizing it has to be called from
  inside `runCompile()`, replacing the existing inline arity check in the
  same function `extractVoiceControls` sits beside. That is a false safe,
  and it is not a contrived one — CONTRACT.md didn't exist yet when this
  work was actually done, so even the strengthened check above would not
  have caught it: there was no cited contract range for `runCompile()`'s
  swap protocol to cross-reference against until Brief B wrote
  `host/Source/CONTRACT.md` afterward.

This is the honest limit of Mechanism A: it is provably correct on
`touches`, and it is a real but bounded improvement on `provides`/`depends`
— bounded specifically to contracts someone already wrote down. The practical
consequence, not a workaround: **splitting a task into parallel briefs
touching an area with no `CONTRACT.md` yet should itself be treated as
unsafe by default**, because the mechanism that would catch the failure mode
doesn't exist until the contract is written.

### Red-case discipline

Matching `invariant-hook-writer`'s red/green self-test requirement:
- Red: two briefs declaring the same path in `touches` → must refuse.
- Red: a fixture brief under-declaring `touches` against a fixture diff that
  touches more → the retrospective check must flag it.
- Red: a fixture diff touching a line inside a fixture `CONTRACT.md`'s cited
  range, with that contract absent from `provides` → must flag it.
- Green: two briefs with genuinely disjoint `touches`/`provides`/`depends`,
  verified against real fixture diffs → must pass.

None of this exists yet. Per CLAUDE.md's "a control counts only once it has
been seen failing" (the 2026-07-25 finding that all five hooks had never
run), this mechanism is not real until these four cases are written and
shown red-then-green, the same discipline `check_prompt_invariants.py` was
built under (COLLABORATION.md §7: "shipped with six red cases and seven
green ones").

### Verdict: keep, narrowed

`touches`-disjointness as a **hard, hook-enforced, preventive** gate.
`provides`/`depends` as an **advisory** check, cross-referenced against
existing `CONTRACT.md` anchors, with the explicit rule that an area with no
written contract does not get parallelized on the strength of a declared-safe
`provides`/`depends` alone.

---

## 2. Mechanism B — independent review pass

**The claim:** after landing, a separate session reads only the diff and the
relevant `CONTRACT.md`, reports violations as `file, line, clause,
reproducible trigger`, never fixes or commits.

### The §9 test

Split again, because — as the task predicted — the two halves are not
equally sound.

**A single finding is genuinely checkable, and by a real independent
artifact.** `file:line:clause:trigger` is exactly COLLABORATION.md §3's Tier
2 evidence bar already in force for every consequential change in this repo
("A primary source, cited by file:line... A test or a runnable check... An
explicit statement of what was NOT verified"). A finding that names a
reproducible trigger is falsifiable by running the trigger — the independent
artifact is the reproduction, not the reviewer's say-so. This half passes.

**Coverage is not checkable, and the task's framing of this is exactly
right:** a review that reports nothing is indistinguishable from a review
that looked at nothing. This is not a hypothetical failure mode in this
project — it is the literal, already-recorded failure mode of every hook
that silently never ran (CLAUDE.md, 2026-07-25: `.claude/settings.json` had
`PreToolUse` at the wrong nesting level and "Claude Code ignores a
wrongly-shaped file silently"). A silent Mechanism-B pass is the same defect
wearing an LLM instead of a config file.

### Strengthening, and its limit

Two changes narrow the gap, neither closes it:

1. **Require a per-clause attestation, not a pass/fail verdict.** The
   reviewer must output "checked clause N of CONTRACT.md against lines
   X–Y: not violated" for every clause in scope, not just silence-means-OK.
   This converts an invisible property (did it look) into a stated claim
   (which a human, or a later check, can at least confirm is
   well-formed — did it enumerate every clause the contract has, or skip
   some). It does not prove the *reasoning* behind each attestation was
   correct.
2. **Canary trials.** Periodically feed the reviewer a diff with a
   deliberately planted, known violation and confirm it's caught — the
   same red-case-before-trust discipline as Mechanism A, applied to a
   process that can't be proven correct by construction, only sampled.

The residual, stated plainly: unlike Mechanism A's `touches` check,
Mechanism B's core activity — did the model actually reason correctly about
a diff against a contract — has no independent artifact that proves it in
general. Canary trials catch *known* failure shapes. They do not prove the
absence of unknown ones. This is a permanent property of the mechanism, not
a gap to be engineered away.

### The concrete scenario — where silence would cost something real

`host/Source/PARAM_CONTRACT.md`'s own text: *"`pushToFaust` (audio thread)
and the editor's default seeding both call \[`mapSlotToZone`/`mapZoneToSlot`\];
neither may reimplement it (PF-001/PF-037 were this bug, twice)."* This
project has already shipped this exact defect **twice**. A third
recurrence — a new call site that inlines its own slot-to-Hz conversion
instead of calling `ParamMap.h` — is precisely the kind of thing Mechanism B
exists to catch, and precisely the kind of thing a reviewer that skimmed and
reported nothing would look identical to a reviewer that caught it and found
it absent. The stakes of the coverage problem are not abstract here; they're
named in the contract document itself.

Worth stating in Mechanism B's favor, not just against it: this project has
direct prior evidence that the underlying *activity* — independent close
reading against a primary source — finds what other mechanisms miss.
COLLABORATION.md §9, on why the three-mode protocol was replaced: *"What
eventually found \[the ParamPool denormalization bug\] was reading `MapUI.h`
line by line against the calling code. Verification behavior caught it;
authorship ceremony did not."* That is a human doing exactly what Mechanism
B proposes to delegate. The activity is proven valuable here; what's unproven
is whether an agent, instead of the human who did it in §9, does it reliably
enough to trust silence.

### Cost: doubles session count. What would earn it, and the smallest trial

**Evidence it's earning the cost:** over a bounded trial, the reviewer
catches at least one real, reproducible violation that no existing
mechanism (`check.sh`, hooks, existing tests) already covers — real
incremental coverage, not overlap.

**Evidence it isn't:** zero findings that can't be distinguished from "didn't
look," or findings that are all things `check.sh`/hooks already catch, or
findings that don't reproduce when the trigger is run (false positives
spending a human's time — COLLABORATION.md §1: "the scarcest resource in
this project").

**Smallest trial:** pick `PARAM_CONTRACT.md` specifically, because it names
a bug class with two prior recurrences — the highest prior probability of a
real third catch, and the cheapest to canary (the reimplemented-conversion
shape is simple to plant). Run Mechanism B against the next five Tier-2
changes touching `ParamPool`/`ParamMap`/`ParamIdentity`, with one of the
five being a deliberately planted, known-bad canary. **Stop condition:** if
it misses the canary, the mechanism is not ready for anything beyond that —
matching `invariant-hook-writer`'s red-case-before-trust discipline exactly,
applied as a gate before adoption rather than a description of intent.

### Verdict: neither adopt nor drop — a bounded, falsifiable pilot only

Not "keep both because both were proposed" (the task's own named failure).
Mechanism B's per-finding mechanism is sound and grounded in this project's
own §9 precedent; its coverage problem is real and only sampled, never
proven, by canaries. That combination doesn't justify standing process on
every task — the cost is real (2× session count) and the residual risk is
permanent, not a bug to be fixed later. It justifies exactly the five-change,
one-canary trial above, with the explicit understanding that a pass earns a
*second*, larger trial before any wider adoption — not a green light.

---

## 3. Summary verdict

| Mechanism | Passes §9 test? | Verdict |
|---|---|---|
| A — `touches` disjointness | Yes, provably, given real diffs | Adopt as a hard, hook-enforced gate |
| A — `provides`/`depends` | Partially — narrowed, not closed | Adopt as advisory, bounded to documented contracts only |
| B — per-finding | Yes, matches existing Tier 2 bar | Sound in isolation |
| B — coverage | No — silent-pass is unfalsifiable by construction | Pilot only, with a stated kill condition; not standing process |

---

## 4. COLLABORATION.md amendment — draft only, not written to the file

Everything below is proposed text for a new section, appended after §10
(nothing existing renumbered). Mechanism B has no amendment: it did not
survive to standing process, only to a scoped pilot, and a pilot doesn't
belong in the protocol document until it has results.

```diff
--- a/COLLABORATION.md
+++ b/COLLABORATION.md
@@ (end of file, after "## 10. Changing this document")
+
+---
+
+## 11. Parallel task safety
+
+*Added <date>, session 005.* Applies only when more than one agent runs
+against this repository at once — most sessions are one agent, sequential,
+and this section does not apply to them.
+
+**Every task brief that might run alongside another declares three sets:**
+`touches` (paths it may write), `depends` (contracts it reads and assumes
+stable), `provides` (contracts it may change). "Contract" means a named
+section of an existing `CONTRACT.md`/`INTERFACE.md` — not free text.
+
+**Two briefs may run in parallel only if**, for the pair: `touches ∩
+touches = ∅`, and `provides ∩ depends = ∅` in both directions.
+
+**`touches`-disjointness is enforced, not requested.** A `PreToolUse` hook
+rejects a `Write`/`Edit` to a path outside the running session's declared
+`touches` set, the same fail-closed shape as `check_bash_denylist.py` (§7).
+
+**`provides`/`depends` is advisory, not a guarantee**, and is bounded to
+contracts that already have a written `CONTRACT.md` entry with cited
+`file:line` anchors. A landed diff touching lines inside a cited range
+without that contract declared in `provides` is a mechanically detected
+violation, checked after landing.
+
+**An area with no `CONTRACT.md` yet is not parallel-safe by declaration
+alone.** If the coupling isn't written down, the check that would catch a
+false safe doesn't exist. Write the contract first, or don't parallelize
+that area yet.
+
+**This mechanism is not real until its red cases are shown red, then
+green** — matching `invariant-hook-writer`'s discipline (§7). See
+`docs/sessions/005-multi-agent-safety-review.md` §1 for the four required
+cases. Until then, this section describes an intention, not a control —
+say so, per CLAUDE.md's "a control counts only once it has been seen
+failing."
```

---

## Change report (COLLABORATION.md §4)

```
CHANGED    docs/sessions/005-multi-agent-safety-review.md (new, this file
           only). No code, no agent definitions, no root-level file,
           COLLABORATION.md not touched — draft only, per the task.
WHY        Settle whether either surviving multi-agent mechanism
           (deterministic parallel-safety check; independent review pass)
           should become process, per the human's narrowed proposal.
VERIFIED   COLLABORATION.md read in full this session (md5 unchanged
           since, confirmed via git diff/status before writing) — §2, §3,
           §4, §7, §8, §9 cited by section throughout, not paraphrased.
           CLAUDE.md's "a control counts only once it has been seen
           failing" (2026-07-25 hook finding) cited. All four
           CONTRACT.md/INTERFACE.md files read in full for the concrete
           scenarios, not assumed. The FaustEngine.cpp shared-hunk claim
           is this session's own prior verified work (docs/sessions/003),
           not a new claim taken on faith.
RISK       The `provides`/`depends` cross-reference-against-CONTRACT.md
           strengthening is my own design, proposed here for the first
           time — unbuilt, untested, and its red cases (§1) don't exist
           yet, so "adopt as a hard gate" for the touches half is a
           recommendation to build something, not a report that something
           already works. The PARAM_CONTRACT.md pilot for Mechanism B is
           sized on judgment (five changes, one canary), not on any prior
           data about how often a real violation of that contract
           actually recurs in new work — it could easily find nothing in
           five changes and that would be weak evidence either way.
YOUR MOVE  Decide: (1) build Mechanism A's touches-gate and
           provides/depends check, with red cases, before it's used for
           anything real; (2) run or skip the five-change/one-canary
           Mechanism B pilot on PARAM_CONTRACT.md; (3) approve, amend, or
           reject the §11 draft above before anything is written to
           COLLABORATION.md. Not a plan to implement without that
           decision.
```
