touches:  docs/sessions/006-*.md (a new session-numbered file this step creates; name TBD
          by whoever runs it, following the check_doc_naming.py no-date-in-filename rule)
depends:  none
provides: none

This is NOT an implementation brief for a code-writing agent. It is a short procedural note
for whoever (the orchestrating session) closes out the day. Do not spawn a fresh
code-writing agent against this file the way P1-P4 are spawned -- this step is analysis and
writeup, done by the orchestrator directly, after P1-P4 have landed.

## Background

This session is itself a live trial of "Mechanism A" -- the touches/depends/provides
parallel-safety declaration scheme proposed and left as an open decision in
docs/sessions/005-multi-agent-safety-review.md. Read that file IN FULL first, especially §1
and the §11 draft amendment at the end.

Briefs P1-P4 in docs/sessions/006-briefs/ each declared a touches/depends/provides block at
their top.

## What to do, once P1-P4 have landed

1. For each of P1, P2, P3, P4: run `git diff --name-only` (or the equivalent, against the
   commit(s) each brief produced) and compare the ACTUAL set of files touched against that
   brief's DECLARED touches set. Record, per brief: the declared set, the actual set, and a
   verdict -- honest (sets match), under-declared (touched more than declared -- a real
   safety miss), or over-declared (declared more than needed -- harmless but imprecise).

2. Record the specific case where the mechanism produced a REFUSAL rather than a pass: P1
   (keyboard tests, touches host/tests/EditorSessionTest.cpp) and P2 (ForgeLookAndFeel,
   touches host/Source/PluginEditor.h/.cpp among others) have textually disjoint touches
   sets, which would look parallel-safe by a raw set-intersection rule -- but P2 changes
   PluginEditor construction/destruction order (new member added, constructor/destructor
   edited) that P1's tests depend on by constructing a PluginEditor in every scenario, and
   no CONTRACT.md in this repo covers PluginEditor (grep confirms -- only INTERFACE.md has
   one incidental mention of editor destruction). Session 005 §1's own stated rule is: "an
   area with no CONTRACT.md yet is not parallel-safe by declaration alone." P1's brief
   (docs/sessions/006-briefs/p1-keyboard-coverage.md) was written to sequence strictly after
   P2 lands, specifically because of this rule. Write this up as the mechanism's first real
   test producing a correct refusal of a plausible-looking-but-actually-unsafe
   parallelization.

3. Write the verdict to a new file docs/sessions/006-<descriptive-name>.md (pick a name
   describing "multi-agent trial results" or similar -- no date, per check_doc_naming.py).
   Structure it like session 005's own document: state the trial's design, the per-brief
   honest/under/over-declared table, the P1/P2 refusal case in detail, and a recommendation
   for session 005's open YOUR MOVE -- specifically, does this one trial's data support
   building the PreToolUse touches-gate hook now, or is one trial (n=4 briefs, one
   deliberate sequential case) still too little data? Be honest about sample size -- this is
   one session, not a statistically meaningful trial, and the writeup should say so
   explicitly rather than overclaiming.

## Explicit non-goal

This step does NOT amend COLLABORATION.md. The §11 draft in session 005 stays a draft until
there is more than one session of trial data -- this is data point one, not a ratification.

## Out of scope

Do not touch COLLABORATION.md, CLAUDE.md, or any file under docs/sessions/006-briefs/. Do
not re-run or re-review P1-P4's actual code changes for correctness -- that is not this
step's job, only the declared-vs-actual file-set comparison and the refusal writeup are.
