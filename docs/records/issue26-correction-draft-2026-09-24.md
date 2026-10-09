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
3. The very first number I gave you — 51/51 accept/reject agreement, with the 9/15 (later
   8/15) vs 15/15 source-location table — needs two corrections of its own, found re-auditing
   this week. First: that table's 36-program half (the hand-built corpus behind "51") was built
   in a scratchpad I never committed, and it's gone — I can only re-derive 15 of the 51 from
   what's on disk, so I can no longer stand behind the full figure, only the 15. Second: the
   "C++: 0/15 stable error codes" cell in that table wasn't counted from the runs — the script
   that produces it hard-codes a 0 instead of checking, on the (near-certain, but unverified)
   assumption that `faust -lang cpp`'s stderr never carries a stable code. I'd rather tell you
   that than have you treat it as measured. I'm restating the table on just the 15 I can
   reproduce, with that caveat on the code-count cell, in the linked repo.
4. One more, and it's the one I'd flag hardest: my harness strips faust-rs's own
   repair-suggestion list out of the feedback text before the model ever sees it, on every
   program where faust-rs's parser offers one. I checked how often that matters on the 202-
   program corpus — for the `syntax`-class failures specifically (52 of the 192 screened
   programs, one of the subgroups where arm B's result is statistically significant), 87% of
   them had their diagnostic reduced to the bare parse error plus a caret, nothing else, e.g.:

   ```
   The Faust compiler rejected your program. [FRS-PARSE-0001] Parsing error at line 3 column 35
     at line 3, column 35
        3 | process = _,_ : select2(flip, _, -_);
          |                                   ^
   Fix this and re-emit the complete program.
   ```

   So on that subgroup, the comparison was never really "C++ stderr vs faust-rs's diagnostic"
   — it was "C++ stderr vs faust-rs's diagnostic with its own suggested fix deleted." I don't
   yet know whether putting that list back changes the result; it's queued alongside the
   visibility experiment above.

Thanks for pushing on this — the counter-intuitive result held up, but my explanation for it
didn't, and I'd rather you have the corrected version than let the first one stand
uncorrected on a public thread.
