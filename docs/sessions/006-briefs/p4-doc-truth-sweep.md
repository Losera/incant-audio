touches:  STATUS.md, docs/ux_roadmap.md
depends:  none
provides: none

Pure documentation corrections. No code, no new files, no other files touched. Line numbers
below are as last read and may have shifted -- search for the cited text, do not assume the
exact line number is still accurate, and fix minimally in place.

## Correction 1 -- STATUS.md: stale "uncommitted" claim

STATUS.md's "Waiting on you" section currently has an item (originally around line 217, and
a related claim around line 102) saying today's keyboard/KeyboardPanel work is uncommitted.
This is FALSE: it landed as commit e64867f ("Play a generated instrument from the editor:
on-screen + QWERTY keyboard"), followed by bc061ff, 91bfa4a, d4235e8.

Verify with `git log --oneline -10` and `git show e64867f --stat`, then remove or correct
this claim so STATUS.md stops asserting uncommitted work that is actually committed and
pushed.

## Correction 2 -- STATUS.md: half-false "Broken" item

STATUS.md's "Broken" section item 13 (originally: "score_efficacy.py --judge spends quota
and takes no lock") is HALF false. Read bench/score_efficacy.py: the lock IS present
(acquire_lock/release_lock imported from run_benchmark, taken around the judge call --
look for the lock-acquisition block, roughly lines 538-569 as of last read, but verify by
reading the file). The "spends quota" half is still true.

Correct the item to say only "spends quota" (still true, still worth listing if still an
open concern) and remove or correct the "no lock" claim, citing the actual line where the
lock is taken.

## Correction 3 -- STATUS.md: wrong citation for ForgeLookAndFeel.h scope

STATUS.md has two citations (near lines 138 and 194 as last read -- verify current location)
pointing to docs/ui_design_plan.md §2 as the place ForgeLookAndFeel.h was scoped. This is
WRONG. Read docs/ui_design_plan.md and confirm its §2 is actually the plugin-type taxonomy
(Generator/Effect/Utility/Hybrid), not anything about LookAndFeel.

The real scope for ForgeLookAndFeel.h (B2) lives in
docs/sessions/002-refine-loop-and-ui-redesign.md lines 355-380. Fix both citations in
STATUS.md to point to the correct location.

## Correction 4 -- docs/ux_roadmap.md: stale gate description

docs/ux_roadmap.md lines 62-68 (verify current location) describes a gate that STATUS.md's
"Waiting on you" section already flags as no-longer-existing. Read that STATUS.md item
first -- it should state exactly what is wrong with the ux_roadmap.md text -- then make the
one-line fix STATUS.md says is needed in docs/ux_roadmap.md to match reality.

## Discipline

Do NOT rewrite STATUS.md wholesale. This brief makes exactly these four surgical
corrections, in place, and nothing else. A full STATUS.md rewrite happens later, by someone
with the full session's outcomes in hand, not by this brief.

## End state

`git diff STATUS.md docs/ux_roadmap.md` shows exactly these four corrections and nothing
else changed -- no other claims touched, no reformatting, no unrelated edits.

## Out of scope

Do not touch any file other than STATUS.md and docs/ux_roadmap.md. Do not touch code. Do not
create new files.
