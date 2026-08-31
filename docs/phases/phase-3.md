# Phase 3 — Audible & interaction defects

**Status:** Active backlog · **Last reviewed:** 2026-08-30 (main `55eda8c`)

Part of the six-phase plan — see [`README.md`](README.md). Defers to `STATUS.md` and
`docs/BUGS.md`.

## What this phase proves

Fewer "successful" patches that are silent, wrong, truncated, unplayable, or confusing.
The compiler passing is necessary but not sufficient.

## Where it stands

14 defects open in `STATUS.md`'s "Broken — ranked". Most are sampling noise from a single
125-cell grid run; a few are evidenced and reproducible and are the real targets. Three
plans are drafted and session-local.

## Evidence on record

- **PF-024** routing / arity mismatch is now the dominant first-attempt failure class —
  22 of 44 error strings on the 125-cell grid (`docs/BUGS.md`).
- **PF-032** generated noise gate renders silent; warm-LP renders silent 1 run in 4 at
  tier L4 (`bench/results/efficacy/efficacy_ollama_20260828_judged.json`).
- Karplus-Strong `recursion_cycle` fails to compile across every archive; the one tier
  that compiles renders +79.6 dB runaway.
- The sidechain compressor fails every run — with a different error each time.
- **PF-045** `en.*` envelope time-unit errors reproduce at tier L0 on the frozen ladder
  record.
- Five in-host findings from session 017: **PF-072** (degenerate 1-knob refine),
  **PF-073** (rough DSP swap), **PF-074** (instrument NaN during play), **PF-075** (mono
  engine sounded like ~5 voices) — `docs/BUGS.md`.

## Open work

- **PF-072 / PF-074** are currently unactionable — each needs the triggering patch source
  and the action immediately before it, captured. Until then they can only be re-observed
  (`STATUS.md`, "Next three").
- Execute the three drafted plans, each of which *leads* with a re-measurement work
  package: the prompt already carries the fix text — does the shipping model obey it? The
  plans are session-local at `~/.claude/plans/phase3-pf024-*`, `phase3-pf032-*`,
  `phase3-pf045-*`.
- **PF-043** — make an incompatible Ollama context / output budget fail *before*
  generation, not mid-run (`PLUGIN_HEALTH_PLAN.md` P1.6).
- **PF-073 / PF-075** — capture a repro; a brief recompile gap is expected, an audible
  click is not; a held five-note chord disambiguates the voice count.
- Reproduce or dismiss the `host/Source/FaustEngine.cpp` `prepare()` sample-rate
  synchronization finding with a targeted TSan scenario — Tier 2 (`PLUGIN_HEALTH_PLAN.md`
  P0.5).

## Done when

The offline corpus has no unexplained silence, NaN, runaway gain, arity, or tail failure,
and the evidenced defects (PF-024 / PF-032 / `recursion_cycle`) are closed with a corpus
regression each.

## Next action

Pick one drafted plan — PF-032 is the tightest — and run its re-measurement work package
first. It decides whether this is a prompt fix or a deeper generation problem.
