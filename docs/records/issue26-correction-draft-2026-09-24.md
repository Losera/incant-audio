<!--
DRAFT ONLY — for Losera's review and posting. Not posted by Claude Code (COLLABORATION.md
§2 trigger 1: "a human reviews the wording and posts it"). Target: comment on
Losera/incant-audio#26, addressed to Stéphane Letz (GRAME), following up on the
2026-09-08 comment this corrects.

Edit freely before posting — this is a starting draft, not a final text. Delete this file
after posting, per the same convention the prior (already-posted) draft used.
-->

Hi Stéphane — a correction to what I told you on 08-09.

I said the mechanism was that faust-rs's caret is precise enough that the model "treats the
quoted line as correct and rewrites around it." That's wrong, and I want to flag it rather
than let it stand. Checked against the harness directly: neither arm's request to the model
actually contains the program being repaired — each corrective attempt regenerates from the
original prompt, not from a visible copy of the failing code. So there's no program for the
model to "rewrite around."

What *is* true, and asymmetric: faust-rs's diagnostic quotes one source line under the caret,
and that line gets folded into the feedback text I send back, every time. The raw C++ error
sometimes echoes fragments of the program too — Faust's box-expression dump on an arity error
leaks the model's own widget declarations — but buried in desugared internal notation a model
can't read as source, not a clean line. So the 58%/4% figure I gave you is real, but it
measures whether a *readable* line the harness itself handed to the model gets copied
forward — not whether the model is anchoring on a caret inside code it can actually see. Your
instinct that this sounded "somewhat counter-intuitive" was the right read.

The result itself is unchanged — fix-within-2-attempts is still 74%→43% on the 3B and
73%→49% on the 7B, same significance. Just not for the reason I gave.

Two other things, while I'm correcting the record:

1. Both reproduction paths I gave you are fixed as of today. The Docker path
   (`make docker-verify`) had started failing its own version assertion once Arch moved past
   the pinned Faust version — now pins the package repository itself to a fixed snapshot
   instead of asserting a version string after an unpinned install, so it's not exposed to
   that again. And `bench/issue26/` (the path in my original comment) got renamed a few weeks
   back to `bench/repair_ab_repro/`; there's now a stub at the old path pointing to the new
   one.
2. The obvious next experiment is separating "does the model see the failing source" from
   "which diagnostic format it gets" as two independent factors, rather than the single
   diagnostic-format comparison I ran. That's queued on our side; no numbers to report yet.

Thanks for pushing on this — the counter-intuitive result held up, but my explanation for it
didn't, and I'd rather you have the corrected version than let the first one stand
uncorrected on a public thread.
