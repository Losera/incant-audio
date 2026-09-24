# Repair-loop context correction — 2026-09-24

Point-in-time record. Not maintained after today — see COLLABORATION.md §8 ("a document that
records a point in time must carry a date and then stop changing").

**Why.** Continuation of the 2026-09-23 faust-rs/efficacy review session
(`docs/records/efficacy-adversarial-review-2026-09-23.md`,
`docs/records/faust-rs-delta-2026-09-23.md`). The human supplied an external adversarial
review of that work (source: a separate conversation, not on this machine) whose central
objection — that the repair-loop A/B never shows the model the program it is asked to fix —
was verified directly against source before being accepted, per this project's own "inspect
primary evidence for a decision-changing claim" standard. It is correct, and it falsifies a
mechanism claim already posted to GRAME. This record is the trace of that verification and
everything landed because of it.

## 1. The falsified claim, and what verifying it directly showed

`bench/repair_ab_core.py:107`:
```python
user_message = prompt + template.format(feedback=feedback_text)
```
`prompt` is `entry["prompt"]` — the original natural-language request. `code` is used only at
`:105`, to *compute* the diagnostic passed to `feedback_for`. It is never placed in
`user_message`, and there is no conversation history across attempts (`generate: GenerateFn`
is a stateless `(user_message) -> new_code` callable, `repair_ab_core.py:28`). Every corrective
attempt therefore **regenerates from the original spec** — it cannot be "editing around" a
program it has never seen.

`feedback_for` (`:74-84`) does pass `code` into `frs_check.render()`'s `source` argument for
arms B/C, and `render()` uses it to splice the offending line under a caret
(`frs_check.py:209-222`, `_source_caret`). So the asymmetry is real, but inverted from the
published claim: **arm A gets zero visibility into the program; arm B/C gets one quoted line
of it, inside the feedback text, never the program itself.**

This directly falsifies the mechanism posted to GRAME's Stéphane Letz on 2026-09-08 (issue
`Losera/incant-audio#26`, comment 4):
> *"I believe the caret is precise enough that the small model treats the quoted line as
> correct and rewrites around it, so it breaks again on the same spot the line faust-rs points
> at comes back unchanged 58% of the time vs 4% with the plain C++ error."*

There is no "the same spot" to rewrite around, because there is no visible program to locate a
spot in. The 58%/4% figure (verified still correct — `verify.py`, checked, McNemar
*p* ≈ 2e-22) measures **whether a line the harness itself supplied gets copied into the next
attempt**, not whether the model anchors on a caret inside inspectable code.

## 2. What is NOT affected

- **The A/B outcome.** Arm A (raw C++ stderr) still repairs more often than arm B/C (faust-rs
  diagnostics) — 75%→44% raw / 74%→43% screened on `qwen2.5-coder:3b`, 72%→50% raw / 73%→49%
  screened on the 7B, McNemar p<1e-3 on every headline cell. Nothing in this correction touches
  those numbers; `tests/test_repair_ab_readme_numbers.py` (5/5) still passes unmodified.
- **The provenance audit.** The 2026-09-23 Dockerfile/build-comparison work already ruled out
  "the compiler build changed" as an explanation; that finding stands independently.
- **faust-rs's diagnostic quality.** 15/15 stable codes, 15/15 source locations vs C++'s 0/15
  and 8/15 — unaffected, and still a clear human-facing win.

## 3. What IS corrected

| Location | Change |
|---|---|
| `docs/BUGS.md` PF-076 detail block | New "Mechanism correction (2026-09-24…)" paragraph; the original causal claim struck through in the registry row |
| `docs/BUGS.md` PF-079 | "Not root-caused" replaced with the two named defect classes (§4 below) |
| `docs/BUGS.md` PF-011 registry row | Flipped `open`→`closed`, matching STATUS.md and the 2026-09-08 detail entry (was drifted 16 days) |
| `docs/BUGS.md` new rows | **PF-080** (both external repro paths broken — filed and fixed same session), **PF-081** (the retry loop's blindness is a live product gap, not benchmark-only) |
| `bench/repair_ab_repro/METHODOLOGY.md` | New **L12** (source visibility differs by arm — this finding) and **L13** (faust-rs's own repair-sequence list is stripped from every arm, never tested); **L3/WP3** updated to require varying source visibility alongside the wrapper |
| `bench/repair_ab_repro/README.md` | "Why" section's opening mechanism paragraph replaced with the correction and a pointer here; the "Caveats" paragraph leads with source-visibility, not just wrapper wording |
| `bench/repair_ab_repro/Dockerfile` | Re-pinned to a frozen `archive.archlinux.org` snapshot instead of asserting a version string after an unpinned `pacman -Syu` (§5) |
| `bench/issue26/README.md` | New stub — the posted comment's path 404s on the default branch since PR #71's rename; this points readers at the current location |
| `STATUS.md` | "Broken" gets a new unranked PF-079 entry at the top; "Assumed, never checked" no longer reads `*(none.)*`; "Waiting on you" #1 corrected — the reply was already posted 2026-09-08, and what's actually owed now is a correction, not a first post; worktree inventory gets the two untracked worktrees this pass found |

## 4. PF-079 root cause (found this session, independent of the C1 correction)

**Upward expander, +715 dB measured** (`dynamics-04`, L4, `efficacy_groq_20260831.json`):
```faust
gain_db = select2(level_db < threshold, 0, (threshold - level_db)*(ratio-1));
```
`ba.linear2db(g) = 20*log10(max(ma.MIN, g))` (`/usr/share/faust/basics.lib:163`); at this
project's installed single-precision Faust, `ma.MIN = 1.175494351e-38`
(`/usr/share/faust/maths.lib:276`), so the envelope floors at **−758.6 dB**. At
`threshold=−30, ratio=2` (this patch's defaults), predicted gain is
`(−30 − (−758.6)) × (2−1) = +728.6 dB`; measured +715 dB. No envelope floor, no gain ceiling.

**Brick-wall limiter, DC 0.95–0.98 measured** (`dynamics-02`, L3):
```faust
limit(x) = y with { y1 = select2(x > thresh, thresh, x); ... };
```
`ba.if(cond,then,else) = select2(cond,else,then)` (`basics.lib:1888`), so `select2(c,a,b)`
returns `a` when `c` is **false**. Every sample at or below threshold is therefore replaced
by the threshold *constant* — 0.98855 at the −0.1 dB default. Measured 0.95–0.98. Silent
because it compiles and type-checks perfectly; the arm order is simply backwards.

Both are promptable and both are render-oracle-detectable. The actual prompt-rule change and
`_SELF_TESTS` red cases are **not** part of this record — queued as the next session's work.

## 5. The Dockerfile fix, verified end to end

Old mechanism: `pacman -Syu --noconfirm faust` against Arch's live rolling repos, then assert
`faust --version` contains `2.85.9`. Arch moved to faust 2.88.0 (published 2026-09-10) and the
assertion did exactly what it was built to do — fail the build — for every reader since.

New mechanism: pin pacman's mirror to `https://archive.archlinux.org/repos/2026/07/05/$repo/os/$arch`
before any install (four days after faust 2.85.9 was published 2026-07-01, over two months
before 2.88.0). Verified this session, real `docker build` + `docker run … verify`, not
inferred:
```
$8 0.210 FAUST Version 2.85.9
$8 0.213 faust-rs 0.8.0
...
REPRODUCED — 367 checks, every count and p-value bound holds.
```
The version assertion is kept as a secondary check behind the pin.

## 6. Not done this pass — the queue

- **PF-079's prompt fix** (two rules, two few-shots) and its `render_oracle.py --self-test`
  red cases. Root cause is in hand; the fix is not landed.
- **WP3's actual re-run** (matched wrapper AND matched source visibility) — the experiment
  that would tell us whether the A/B effect survives once the confound is controlled, not just
  named. Spends model time.
- **PF-081's product fix** — carrying `prior_source` into Fresh-mode retries.
- **ADR-042 implementation**, the n≥3 grid completion, wiring the two new render-safety/
  leakage instruments into `check.sh audio`.
- **ADR-006 amendment/supersession** — drafted as a recommendation, not landed, since it is an
  Accepted architectural decision and this session's mandate is correction, not redesign.
- **The GRAME correction itself** — drafted for the human to review and post
  (COLLABORATION.md trigger 1); this session does not call `gh issue comment`.

## Addendum, same day — §1's "zero visibility" claim for arm A was itself an overclaim

Per this project's own rule that a point-in-time record carries a date and then stops
changing, the text above is untouched; this addendum documents what a follow-up check found
before anything was built on top of §1.

Before pre-registering the WP3 re-run this record's §6 queues, the correction itself was
re-audited independently (fresh session, no access to the reasoning that produced §1). It
confirmed §1's core claim — `user_message` never contains the failing program in either arm —
but found the follow-on claim, that arm A therefore has "zero visibility into the program,"
does not hold uniformly. Measured against the full 207-record failing corpus
(`bench/corpora/repair_corpus_20260830.json`): **37% of arm A's raw C++ stderr contains a
literal widget-constructor string** (label, numeric range — the model's own written
arguments), via Faust's box-expression dump. By class: **`routing_arity` 73%** (the class
driving the strongest measured effect, McNemar *p* ≈ 5e-6) — but as an unreadable fragment
buried in desugared internal notation, not a source line; **`duplicate_symbol` 93%**, where
the compiler reprints **two full, syntactically valid Faust lines verbatim** — genuinely more
raw source than arm B/C's single spliced line.

**Disposition:** §1's headline claim (the A/B result is unaffected, the caret-anchoring
*mechanism* is withdrawn) is unaffected by this. What changes is the replacement framing this
record proposed — "arm A: 0 lines, arm B/C: 1 line" is not a clean controlled binary and
cannot be asserted as one in the WP3 experimental design or in any GRAME-facing correction.
Full breakdown: `bench/repair_ab_repro/METHODOLOGY.md` L14. The WP3 pre-registration written
after this addendum measures per-record visibility rather than assuming it by arm.

