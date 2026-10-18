# PF-079 prompt-fix draft — 2026-10-18

Not landed. Per COLLABORATION.md §3, `llm/prompts/*` is Tier 2, and editing a prompt while
leaving the benchmark baseline stale is itself a Tier 2 violation — proving this rule
actually changes model output needs a benchmark re-run, which spends quota. This doc is the
proposed diff and its evidence, for a session with quota authorization to review and land.

What *did* land this session, free and offline: two deterministic red cases in
`bench/render_oracle.py`'s `_SELF_TEST` (`runaway_expander`, `inverted_limiter`) on
`fix/pf079-oracle-red-cases`, proving the oracle catches both mechanisms. See that branch's
commit for the full detail; this doc is only the prompt-side half.

## The two mechanisms (docs/BUGS.md:117, root-caused 2026-09-24)

1. **Unbounded upward-expander makeup gain.** `ba.linear2db`'s floor
   (`max(ma.MIN, g)`, `basics.lib:163`) is `-758.6 dB` at single precision
   (`ma.MIN = 1.175494351e-38`, `maths.lib:276`), not `-inf`. An expander computing
   `gain_db = select2(level_db < threshold, 0, (threshold-level_db)*(ratio-1))` reaches a
   *finite* +728.6 dB of makeup gain on literal silence — finite, so nothing in the
   compile-error retry loop ever sees it.
2. **Inverted `select2` arms in a hand-written limiter.** `basics.lib:1888`:
   `if(cond,then,else) = select2(cond,else,then)` — i.e. `select2(c,a,b)` returns `a` when
   `c` is **false**. A limiter written as `select2(x > thresh, thresh, x)` (the naive
   left-to-right reading: "if over threshold, clip; else pass through") is backwards: every
   sub-threshold sample becomes the threshold constant, every above-threshold sample passes
   unclipped.

## Proposed `system_prompt.txt` addition

Current headroom (`python3 tests/test_prompt_headroom.py`, this session): **85 tokens of
slack against groq's 4096-token budget.** Any addition this size or larger needs either a
larger-context provider for this check, or something else trimmed first — flagging rather
than guessing which.

Proposed rule, written for minimum tokens given the headroom:

```
DYNAMICS SAFETY (expanders/limiters — do not derive from first principles):
- Expander/gate makeup gain: clamp gain_db to a sane ceiling (e.g. min(gain_db, 60)) AFTER
  computing it from level_db. ba.linear2db floors at ~-758 dB on silence, not -inf, so an
  unclamped (threshold-level_db)*(ratio-1) can reach +700 dB and is NOT caught by compile.
- Limiter via select2: select2(cond, whenFalse, whenTrue) — NOT whenTrue-first. Prefer
  ba.if(cond, then, else) (reads in the intuitive order) over a hand-written select2 for
  any threshold/clip logic.
```

That is ~75-85 tokens by the same char/4 estimate the headroom script uses — i.e. it would
consume most or all of the remaining slack on groq. **Options, not a decision:**
- Land it and accept the groq-budget prompt moves from 85 slack to roughly 0-10 — risky,
  one future addition away from `test_prompt_headroom.py` going red.
- Land it only in `instrument_prompt.txt`'s dynamics-adjacent section if effects and
  instruments share less of the budget there (not checked this session).
- Trim something else first (OPEN_QUESTIONS.md Q6 already flags the presentation variant
  failing this same check by ~267 tokens against groq — this is the same underlying
  constraint, not a new one).

## What proving the fix needs (not run — spends quota)

- A fresh benchmark pass biased toward the `dynamics` category (the two failure prompts
  named in PF-079: "upward expander", "brick-wall limiter for mastering") on whichever
  provider is being measured.
- Re-run `bench/render_oracle.py` over the new generations; the two red cases above are the
  pass/fail bar — a successful fix should show prior-failure-shaped patches now landing in
  `basics.lib` territory without the floor/inversion bug, i.e. `runaway_gain`/`dc_offset`
  codes stop appearing on dynamics-category outputs.
- Per COLLABORATION.md §3, state explicitly whether the existing baseline
  (`bench/results/.prompt_baseline.json`, untouched by design) is now stale relative to
  this prompt change, rather than silently leaving it be.
