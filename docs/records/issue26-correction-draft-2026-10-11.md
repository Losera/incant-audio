<!--
DRAFT ONLY — for Losera's review and posting. Not posted by Claude Code (COLLABORATION.md
§2 trigger 1: "a human reviews the wording and posts it"). Target: comment on
Losera/incant-audio#26, addressed to Stéphane Letz (GRAME). Supersedes
docs/records/issue26-correction-draft-2026-09-24.md in full — post this one, not that one.

Edit freely before posting — this is a starting draft, not a final text. Delete this file
after posting, per the same convention the prior (already-posted) draft used.
-->

Hi Stéphane — an update on the mechanism question you asked about on 09-08, and three
smaller corrections while I'm at it.

**The real answer, finally.** You asked whether faust-rs's better error shortens the repair
loop, and I told you no — feeding faust-rs's diagnostic back to the model made it worse
(74%→43% on the 3B, 73%→49% on the 7B). I then had to walk back my explanation for *why*:
I'd said the model "treats the caret-quoted line as correct and rewrites around it," but the
harness never actually shows the model a visible program to rewrite around — that claim was
wrong, and I flagged it as withdrawn, mechanism unknown.

I ran the follow-up experiment I mentioned was queued: a 5-arm factorial holding the
correction wrapper fixed across every arm and making source-line visibility an explicit,
controlled toggle (faust-rs's full diagnostic with/without the source line spliced in;
faust-rs's minimal diagnostic likewise). Result: **faust-rs's diagnostic content alone,
fairly presented, shows no effect** — a model shown faust-rs's full diagnostic with no
source line scores statistically indistinguishable from a model shown raw C++ stderr
(p = 0.545, n = 192). What actually drives the whole result is **showing the model one
caret-quoted line of its own broken program** — true whether that line arrives via faust-rs's
rich renderer (p = 5.3e-10) or its minimal one (p = 1.9e-13). It isn't about faust-rs. It's
about handing a weak local model a line of its own source and asking it to fix the program —
that alone roughly halves its repair rate, independent of where the line came from.

So: the original counter-intuitive result holds, your instinct that it sounded odd was
right both times, and now I can actually tell you why instead of walking it back a second
time with "still don't know."

Three smaller things, while I'm at it:

1. Both reproduction paths are fixed. The Docker path (`make docker-verify`) now pins the
   package repository itself to a fixed snapshot instead of asserting a version string
   after an unpinned install. `bench/issue26/` (my original path) is `bench/repair_ab_repro/`
   now; a stub at the old path points to the new one.
2. The very first number I gave you — 51/51 accept/reject agreement — needs a correction.
   The 36-program half of that corpus was built in a scratchpad I never committed, and it's
   gone; I can only reproduce 15 of the 51 from what's on disk. I'm restating that table on
   just the 15 I can reproduce. Separately, the "C++: 0 stable error codes" cell in that
   table was a hard-coded constant in my script, not a measured count — near-certainly true,
   but I hadn't actually checked it, and I'd rather tell you that than have you treat it as
   observed.
3. My harness strips faust-rs's own repair-suggestion list out of the feedback before the
   model ever sees it, on every program where faust-rs's parser offers one. Given the result
   above, this matters less than I first thought — content alone is already shown to be
   inert on this corpus, so whether the stripped list would help is a smaller, separate
   question than "does faust-rs's diagnostic hurt," not an explanation for the original
   finding. Still worth knowing: for the `syntax`-class failures specifically, 87% had this
   happen.

Corpus + harness + this run are all committed and reproducible from a clean checkout
(`bench/repair_ab_repro/score_wp3.py` re-derives the numbers above from the committed data,
no model/compiler needed). Thanks again for pushing on this one — it took two corrections to
get to an actual answer, but I think it's the right one now.
