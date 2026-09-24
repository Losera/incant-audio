# `efficacy_groq_n3_rep1_20260915.json` + `efficacy_20260915T191027Z.json` — provenance

Rescued 2026-09-24 from `.worktrees/efficacy-pf031` (branch `bench/efficacy-groq-n3-20260915`),
which carried **zero commits** — these two files were its only content, untracked, at risk of loss
to any `git worktree prune` or clean. Committed here as-is, unscored.

## What this is

**A partial rep-1 of the n≥3 groq re-run** — the reserved `*(evidence)*` item in STATUS.md's
"Next three things" (PF-031). `efficacy_groq_n3_rep1_20260915.json`: 38 of the target 125 cells
(L4 8, L3 8, L2 8, L1 7, L0 7), groq / `openai/gpt-oss-120b`, 34/38 first-try compiles.
`efficacy_20260915T191027Z.json`: a single stray L4 record, same provider/model — almost certainly
a checkpoint or a one-off smoke record from the same run, not part of the 38-cell sequence.

## What this is NOT

- **Not comparable to the 125-cell headline archives** (`efficacy_groq_20260831.json` etc.). The
  38 cells are a non-random truncation of the 125-cell grid — the run stopped, was not resumed, and
  there is no record of why. Do not compute a rate off this file alone and compare it to the
  125-cell numbers.
- **Not schema-complete for ADR-042.** Written before that ADR (`docs/decisions.md:3286`,
  `a55f63f`) was drafted — no `rep`, `system_prompt_sha`, or `faust_version` field. Once
  `prepare_resume`/`score_efficacy.py` are updated to key on `(effect_id, tier, rep)`, this file's
  38 records still need a `rep=1` backfilled onto them before they can be pooled with a completed
  run.
- **Not scored.** No `score_efficacy.py` or `score_efficacy_render.py` pass has been run over it as
  part of this rescue — do it once ADR-042 lands (see `you-are-an-expert-jazzy-hippo.md` queue
  item 5), not before, so the collapse logic matches the real repetition count.

## Next action

Finish the remaining 87 cells at the same provider/model/prompt-set once ADR-042's schema and
`prepare_resume` keying land, then score all 125 as `rep=1` alongside a fresh `rep=0` (the existing
`efficacy_groq_20260831.json`, provenance-checked against the current `system_prompt.txt` first —
STATUS.md already flags that archive as pre-dating a dynamics prompt fix).
