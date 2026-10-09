# faust-rs study — adversarial review and upstream-value judgment, 2026-10-09

Point-in-time record. Not maintained after today — see COLLABORATION.md §8. Written against
`bench/repair_ab_repro/`, `bench/corpora/`, `docs/BUGS.md`'s PF-076 entry, and the live
`grame-cncm/faust-rs` GitHub repo, from the standpoint of a senior DSP researcher judging
whether the study's results would interest or help that project.

**Task.** Recover the faust-rs efficacy study session, adversarially review it, and judge
whether it's useful to `faust-rs`. If not, retarget it. Full findings and the resulting plan
are in the session's plan file; this record is the durable repo artifact.

**Headline.** `grame-cncm/faust-rs` is not a third party — it's Stéphane Letz's own project
(1,905/1,987 commits), and the study exists because he asked for it on
`Losera/incant-audio#26`. Against that fact, as framed, the study is not useful to faust-rs,
and one of its published claims needed correcting. The useful material is small, real, and
had never been sent upstream before this session.

## Findings

**F1 — the conformance half is three orders of magnitude smaller than upstream's own
testing.** `grame-cncm/faust-rs` runs 2,996 in-repo tests, a C++-generated impulse-response
oracle at 2e-6 tolerance across 8 backends (133/133), a 6,624-comparison backend matrix, and
had already run `xtask examples-compare --per-symbol` over the Faust standard library (1,110
cases, finding 216 faust-rs false-rejects vs 0 false-accepts, `porting/journal/2026-08-07.md`).
"51/51 accept/reject agreement" on 51 LLM-generated toy programs isn't a contribution against
that.

**F2 — the posted "51/51" is 71% unreproducible, and two of its three columns are softer than
published.** The 36-program hand-built corpus is gone; only 15/51 re-derives
(`bench/frs_rederive.py`, `docs/BUGS.md`). The "C++: 0/15 stable error codes" cell is a
hard-coded `+= 0` in the re-derivation script, not a count. The 9/15→8/15 location erratum is
Faust 2.85.5-vs-2.85.9 drift inside the dataset, not re-measurement noise.

**F3 — the conformance claim was one-sided; closed this session.** 0 of 306 C++-accepted
corpus records ever had faust-rs run on them (`frs_codes` empty on all of them) — the claim
only ever tested false *accepts*, never false *rejects*, the failure mode upstream's own
examples-compare found 216 of. Ran the missing arm: 0 false rejects / 0 timeouts on 248
distinct accepted programs. Clean null, recorded in
`docs/records/faust-rs-diagnostic-coverage-2026-10-09.md`.

**F4 — L13 sized for the first time: 87% of the `syntax` class had faust-rs's only remedy
deleted before the model saw it.** `frs_check._clean_message` truncates every message at
`"Repair sequences found"` — for `FRS-PARSE-0001`, that list *is* faust-rs's suggested fix.
53/61 `syntax`-class failures (87%) lost it entirely. `syntax` is the #2 strongest
significant per-class cell in the repair-loop A/B (McNemar p≈5.65e-06) — a material share of
that significance measured the harness's own deletion, not faust-rs's diagnostic as shipped.
Added as METHODOLOGY.md L15; a new WP3 arm (B2r) tests it with the list kept.

**F5 — the posted repair-loop mechanism was withdrawn (prior session, `dd79b0a`), and the
correction sat 15+ days unposted.** Compounded by L3 (unmatched wrappers), L12 (source
visibility differs by arm), L14 (arm A leaks more raw source than arm B on `duplicate_symbol`).
WP3 — the pre-registered factorial that would settle it — was implemented and never run.

**F6 — the stale claim was still live on `main`/`pr-92-review`; the fix existed only on an
unmerged branch.** PR opened this session (`#95`) to merge it.

**F7 — three items are genuinely upstream-actionable, in the house style upstream actually
accepts** (crate-prefixed, one mechanism, exact trigger — `shakfu` #16/#17/#19): the
`--version` pinning defect (0.8.0 identical across 216+ commits), the `FRS-EVAL-0099`
non-constant-filter-order misdiagnosis, and a contradictory cause-note on a recursion-cycle
diagnostic. All three re-verified live against current `main` (`2199d069`, 2026-10-06) before
drafting — see this session's issue drafts (not filed; outward-facing action needs explicit
go-ahead).

## Disposition

- PR #95 (this branch → `main`): quantifies L13/L15/L16, recounts L7, adds WP3 arm B2r, adds
  a `schema_version` guard to `frs_check.py`, flags the two soft numbers in the "51/51" claim
  to the unposted GRAME correction draft, marks the repair-loop headline "qualified — pending
  WP3" in the README.
- `bench/frs_diag_coverage.py` (new) — the replacement upstream-facing deliverable: a
  diagnostic-coverage report over the 202-program corpus, offered as a fixture donation
  candidate, not a study.
- Three upstream issue drafts, verified fresh, awaiting posting by a human.
- Not done this session: running WP3, posting the GRAME correction, filing the upstream
  issues, merging PR #95 — all explicitly deferred to the human per COLLABORATION.md /
  AGENTS.md §7.
