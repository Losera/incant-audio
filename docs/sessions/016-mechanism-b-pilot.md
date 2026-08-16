# Session 016 — Mechanism B pilot: the canary is caught, and the pilot found more

Runs the trial session 005 §2 specified and never executed: "Run Mechanism B
against the next five Tier-2 changes touching `ParamPool`/`ParamMap`/
`ParamIdentity`, with one of the five being a deliberately planted,
known-bad canary. **Stop condition:** if it misses the canary, the mechanism
is not ready for anything beyond that." STATUS.md's Next-three #2 (evidence)
carried this as the fully-specified, unbuilt candidate since session 015
closed the Mechanism A adversarial trial.

**Result up front: the canary was caught, with a correct file:line citation,
the contract clause quoted verbatim, and a reproducible trigger — the pilot
does not hit the stop condition.** Unplanned and more interesting: two of
the four diffs built as the pilot's "clean" control material turned out to
contain real, independently-confirmed defects of my own making, and
Mechanism B caught both of those too, with the same rigor. Zero false
positives on the two diffs that really were clean.

## Design

Five single-commit diffs, each a plausible small Tier-2 change touching
`ParamPool`/`ParamMap`/`ParamIdentity`, built on a throwaway branch off
`main` (`mechanism-b-pilot-scratch`, never pushed, deleted after this
write-up along with its five `pilot/change-N-*` sub-branches — nothing here
landed or was intended to land):

| # | File | What it does | Intended |
|---|---|---|---|
| 1 | `ParamPool.cpp` (ctor) | `jassert(slots.size() == POOL_SIZE)` | clean |
| 2 | `ParamPool.h` | `slotForId()` read-only accessor over `idToSlot` | clean |
| 3 | `ParamIdentity.h` | caps `slug()` output at 64 chars | clean |
| 4 | `ParamMap.h` | `parseZone` accepts `"yes"` as a toggle-on synonym | clean |
| 5 | `ParamPool.cpp` (`pushToFaust`) | inlines the Hz log-curve conversion as a "fast path," bypassing `ParamMap::mapSlotToZone` | **canary** |

Diff 5 is the deliberately planted violation: `PARAM_CONTRACT.md`'s own text
names this exact shape as a twice-recurred bug — "`pushToFaust` (audio
thread) and the editor's default seeding both call \[`mapSlotToZone`/
`mapZoneToSlot`\]; neither may reimplement it (PF-001/PF-037 were this bug,
twice)." The canary reimplements the log-curve branch of `mapSlotToZone`
directly inside `pushToFaust`, framed as a believable performance commit
("perf: fast-path Hz-cutoff conversion"), and additionally diverges in
behavior from the real function it bypasses: it ignores an explicit
`[scale:]` override on a Hz-unit parameter (always forces `Curve::Log`
where `curveFor()` would honor `Curve::Exp` if tagged) and skips the
`isDiscrete()` step-rounding the real path applies.

The five diffs were renamed to opaque labels (`diff_A.txt`..`diff_E.txt`)
and their assignment to the five candidate changes was shuffled with
`random.shuffle` before any reviewer was launched, so diff position carries
no information. This run's mapping: A=4, B=5 (canary), C=3, D=2, E=1.

## Reviewers

Five independent, freshly-spawned agents, one per diff, each with **zero
shared context with me or with each other** (a fresh `general-purpose`
agent invocation, not a context-inheriting fork — inheriting my context
would have handed each reviewer the canary's identity directly). Each
received exactly two inputs: `PARAM_CONTRACT.md` and its one assigned
`diff_X.txt`, with explicit instructions not to read anything else in the
repository — matching Mechanism B's own spec ("reads only the diff and the
relevant CONTRACT.md"). Each was required to derive the contract's clause
list itself and produce an explicit per-clause attestation for every
in-scope clause — "checked against lines X–Y: not violated," or a violation
with `file:line:clause:reproducible trigger` — per session 005's
strengthening ("a per-clause attestation, not a pass/fail verdict... This
converts an invisible property (did it look) into a stated claim").

## Results

**Diff A (parseZone "yes" synonym, intended clean):** NO VIOLATIONS FOUND.
10 clauses enumerated, correctly scoped: the one plausibly-relevant clause
(sole conversion site) attested "not violated" with correct reasoning —
`parseZone` is presentation, not `mapSlotToZone`/`mapZoneToSlot`. True
negative, verified.

**Diff B (Hz fast-path, the canary):** 1 VIOLATION FOUND — cited
`host/Source/ParamPool.cpp:215-223`, quoted the "ONLY place... neither may
reimplement it (PF-001/PF-037 were this bug, twice)" clause verbatim,
correctly identified the reimplementation, and additionally surfaced the
`[scale:]`-override divergence I'd built into the bug as a secondary
correctness risk, with a reproducible trigger (a Hz parameter tagged
`[scale:linear]` or one requiring step-discretization). **Canary caught —
the pilot's stop condition is not triggered.**

**Diff C (`slug()` 64-char cap, intended clean):** 1 VIOLATION FOUND —
correctly flagged that truncating `slug()`'s output for previously-accepted
long inputs, without bumping `ParamIdentity::kSchemeVersion`, is exactly
the one-way-door violation the contract names: *"changing `slug()`'s output
for an already-accepted input orphans every saved project using it."* I
verified this independently against the actual diff: no `kSchemeVersion`
touch appears anywhere in it. **This is a real defect, not a false
positive** — a saved project with a group/label slug that happened to
exceed 64 characters pre-patch would silently lose its slot binding on
recompile under this diff, exactly the orphaning failure the contract
warns about. I introduced this while authoring what I believed was safe
control material; the reviewer caught what I missed.

**Diff D (`slotForId()` accessor, intended clean):** NO VIOLATIONS FOUND.
12 clauses enumerated and individually attested, including several
plausible-sounding but correctly-dismissed ones (e.g., correctly reasoning
that `slotForId` returning `-1` for a Meter id is a consequence of `remap()`
never inserting Meter ids into `idToSlot`, not a reimplementation of
eligibility logic). True negative, verified.

**Diff E (`POOL_SIZE` assertion, intended clean):** 1 VIOLATION FOUND — and
this is the most interesting result of the run. The reviewer noticed that
the constructor loop unconditionally `push_back`s on every iteration
regardless of whether `apvts.getParameter(slotId(i))` returns a real
pointer or `nullptr`, so `slots.size() == POOL_SIZE` holds **trivially, on
every possible path**, before or after the diff. The added `jassert` can
never fire — it is dead code, and the comment I wrote alongside it ("this
also catches a miss, not just a count mismatch") states a false property.
**I confirmed this myself against the diff before writing this up: the
reviewer is correct.** This is not strictly a `PARAM_CONTRACT.md` behavioral
clause violation in the same sense as diffs B and C — the contract's
"Violations" section documents this as a known gap rather than stating an
imperative rule — but the reviewer treated "does this diff actually close
the gap it claims to close" as a checkable clause, which is the right call:
a diff whose commit message says "closes X" and doesn't is a real defect
regardless of which section of the contract named the gap.

## Verdict against session 005's own bar

> "Evidence it's earning the cost: over a bounded trial, the reviewer
> catches at least one real, reproducible violation that no existing
> mechanism (`check.sh`, hooks, existing tests) already covers — real
> incremental coverage, not overlap."

Met, and exceeded: one deliberately planted violation caught (the trial's
minimum bar), plus two more real, independently-confirmed defects in
material I had authored believing it correct, neither planted, neither
something any existing hook or test in this repo would have caught (none of
the three touch RT-safety, prompt invariants, or bash-denylist territory —
the only enforcement surfaces this repo currently automates). Zero false
positives on the two diffs that really were clean, with full per-clause
attestation coverage (no silent skips) on all five runs.

**What this pilot does not prove:** five diffs, all authored by the same
person (me) in one sitting, is a small and non-random sample — session
005's own caveat about the original five-change/one-canary design ("it
could easily find nothing in five changes and that would be weak evidence
either way") cuts the other way here too: finding real defects in 3 of 5
small diffs, all self-authored under time pressure to build pilot material
fast, may say as much about how easy it is to introduce this class of bug
under those conditions as it does about Mechanism B's general reliability.
The coverage problem session 005 named as permanent — "did the model
actually reason correctly... has no independent artifact that proves it in
general" — is not closed by one pilot; it's sampled, favorably, once.

## What was NOT verified

- Whether Mechanism B performs this well against a diff written by someone
  other than the pilot's own designer, or against a larger/more tangled
  diff than these single-hunk changes.
- Whether the reviewer's "out of scope" dismissals would hold up against an
  adversarial diff specifically designed to exploit a scoping blind spot
  (e.g., a change that looks confined to one clause's territory but has a
  cross-cutting effect the reviewer wouldn't see without broader repo
  context — which this trial's "read only the diff and the contract" rule
  deliberately withheld, matching Mechanism B's actual proposed scope).
- Cost: this pilot spent 5 parallel subagent invocations (~34k tokens each,
  ~170k total) for 5 diffs. Session 005 named the cost as "2× session
  count" — this trial's actual token cost is a new data point, not
  previously measured.
- No code from this pilot was built or run; Mechanism B is diff-and-contract
  review only, per its own design, so no compiler/test evidence exists or
  was sought.

## Change report (COLLABORATION.md §4)

```
CHANGED    docs/sessions/016-mechanism-b-pilot.md (new, this file only).
           No code changed — the five pilot diffs existed only on a
           throwaway branch (mechanism-b-pilot-scratch) and its five
           pilot/change-N-* sub-branches, all deleted after this write-up.
           STATUS.md updated separately to close Next-three #2.
WHY        Session 005 specified this exact trial and it had never been
           run; STATUS.md's Next-three #2 (evidence) reserved the slot.
VERIFIED   All five reviewer outputs read in full. The two unplanted
           findings (diffs C and E) independently re-derived from the raw
           diff text before accepting them as real rather than reviewer
           error — confirmed by hand: diff E's jassert is provably
           unreachable-as-a-check (the loop push_backs unconditionally),
           diff C's kSchemeVersion is absent from its diff by direct grep.
           The canary catch (diff B) matches the exact violation shape
           deliberately built into it, independently re-read against the
           diff.
RISK       Five diffs, one author, one sitting -- a small, non-random
           sample per session 005's own stated caveat about this trial
           design. Two of the "clean" diffs turning out not to be clean
           is itself informative but complicates a clean pass/fail
           reading of the pilot: this run answers "did it miss the
           canary" (no) but is weaker evidence than intended on "what is
           the false-positive rate on genuinely clean input," since two of
           four control diffs were not, it turned out, genuinely clean.
YOUR MOVE  Session 005's own verdict: a pass here "earns a second, larger
           trial before any wider adoption — not a green light." Decide:
           (1) run that second, larger trial with a cleaner control set
           (verified clean by someone other than the pilot's author, or by
           actually building/testing the control diffs first); (2) treat
           this result as sufficient and start using Mechanism B
           informally on real Tier-2 changes without further piloting;
           (3) neither, and let it sit as a documented capability. Not a
           plan to adopt Mechanism B as standing process without that
           decision — COLLABORATION.md is unchanged by this session.
```
