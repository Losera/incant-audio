# Session 012 handoff — 2026-08-13, Phase 0 reliability fixes complete, PR #8 open

**Objective.** User reported constant rate-limit errors across multiple prompts ("a
simple reverb", "a subtractive synth", "an additive synth with reverb, chorus, and
delay") plus a recurring `invalid delay parameter range: interval(0,2.14748e+09,0)`
compile failure, and asked for a deep root-cause review with a fix plan — architectural
if needed, decreasing compile time if possible, plus a separate note that the UI/window
doesn't scale to arbitrary screens. Full plan (Explore-agent research, root causes,
sequenced Phase 0/1/2, three architectural forks with alternatives/tradeoffs) is at
`.claude/plans/you-are-the-lead-robust-pascal.md` — **read that file before touching
anything below**, this handoff is a pointer to execution state, not a replacement.

**Branch.** `fix/generation-reliability-phase0`, off `main` at `d166154`. **Working
tree: not clean** — see "Uncommitted state" below before doing anything destructive.

**PR.** [#8](https://github.com/Losera/incant-audio/pull/8), open, pushed, not merged.
Standalone (plain `gh pr create`, not part of a `gh stack` — stated as the exception per
`AGENTS.md`'s PR-workflow rule, same as PRs #4/#5/#7 before it).

**Development is paused here at the user's request** ("get ready for handoff... we will
then perform clear and continue"). Nothing further should be started until picked back
up.

---

## 1. What shipped — 5 commits, all on PR #8, `check.sh fast`+`full` green

| Commit | What |
|---|---|
| `837cea6` | `prompt_builder.py`'s stdlib-filter core-floor fix. `ns_map` had no `ve.`/`no.` entry in any domain, and "oscillator" domain (matched by "subtractive synth") mapped only to `os.` — stripping `fi.*` from a subtractive-synth prompt while the prompt still claimed "use ONLY functions from the reference below." `os./fi./en./no./si./ba./ma./ve.` now always survive filtering; filtering itself only engages when headroom is actually tight (`providers.headroom_tokens() <= 300`). New regression test generalizes `gen_stdlib_block.py --verify-prompt`'s resolution check to the **filtered** output. |
| `4f68e6a` | **Highest-value fix.** Wired the existing-but-bench-only `DELAY_RANGE` classifier into the production retry loop. Extracted the taxonomy to new `llm/error_classes.py` (`bench/classify_failures.py` now imports it, preserving the `llm/`←`bench/` dependency direction). `generate.py`'s retry loop now appends a classified, actionable hint to what the MODEL sees on retry (mirrors the existing `_TRUNCATION_HINT` pattern) — kept separate from the raw `error_ctx` that still feeds the user-facing failure message. Also: budget 100→140s (groq's real `Retry-After` hints, 37/25/51s observed live, routinely exceeded 100s), and `RateLimited.retry_after` threaded end-to-end into `PromptPanel::statusForReason()` ("try again in 37s" instead of a dead end). New `tests/test_delay_range_hint.py`; extended `tests/test_generation_budget.py`. |
| `b0ea17b` | Added delay guidance (`de.delay`/`de.fdelay`, MAXD rule, compile-tested few-shot) to `instrument_prompt.txt`, which had none. Also corrected a stale/false comment in `gen_stdlib_block.py` that had cited a nonexistent "ADR-020 two-stage pipeline" as the reason delay was excluded from instruments — verified via grep and direct testing that no such mechanism exists. |
| `3e0805b` | `.env.example`'s groq quota claim ("~14,400 requests/day") corrected to match `providers.py`'s already-corrected figures (200,000 tokens/day, 8,000/min, ~57 generations/day). |
| `b098652` | **Follow-up fix found by live measurement** (see §2). `re.stereo_freeverb`'s `spread` argument (last positional arg) is a hidden compile-time-constant buffer-sizer, exactly like `de.delay`'s first argument — but nothing taught this, and it's invisible from the function's name or its other three (genuinely ordinary 0-1) arguments. One-line rule added next to the existing delay-constant rule in `system_prompt.txt`, plus a stdlib-block annotation. `pf.vibrato2_mono` was cut then restored (see §2 — the prompt's headroom is now genuinely tight). |

**Verification performed:**
- `tools/check.sh fast` and `tools/check.sh full` both green (full C++ rebuild, every
  harness including TSan×3, `EditorSessionTest`, `PromptPanelThreadingTest`,
  `ValidationGateTest`), except one pre-existing, unrelated failure
  (`TestDigestReportsCI::test_green_on_an_older_commit_is_not_reported_as_a_pass`,
  confirmed via `git stash` to fail identically with none of this branch's changes present
  — git-history-shape-dependent, out of scope). That failure has since stopped
  reproducing on its own as HEAD moved forward with this branch's commits (consistent
  with it being repo-shape-dependent, not a real regression).
- `python -m pytest tests/ -m "not integration"`: 597 passed, 2 skipped, after the final
  commit.
- **Live "stop and measure" gate** (the plan's explicit pre-Phase-1 checkpoint): re-ran
  the exact three prompts from `logs/prompts.jsonl` that were actually failing
  (`2026-08-13T04:37-04:46Z`) against live groq, end to end through `generate.py`:

  | Prompt | Before this branch | After |
  |---|---|---|
  | "a simple reverb" | `invalid_faust` (delay-range), 3/3 attempts | **`ok`, 1st attempt** |
  | "Generate a simple subtractive synth" | `rate_limited` | **`ok`, 1st attempt, 2.4s** |
  | "...additive synth with reverb delay and chorus..." | `rate_limited` (37s wait, 27s budget) | still `rate_limited`, now with honest `retry_after: 33.0` surfaced |

  Full trace, including the actual generated Faust code across all 3 attempts of the
  reverb case (which is how the `stereo_freeverb`/`spread` root cause was found — the
  model was never calling `de.delay` at all), is in this session's transcript, not a
  file. Re-running it again means: `sys.path.insert(0, "llm"); import generate;
  generate.generate_json({"prompt": "..."})` — costs live groq quota (free tier).

**Not yet done:**
- [ ] **PR #8 not merged.** User has not yet been asked for final merge approval in this
      session — the live-verification results were posted as a PR comment
      (https://github.com/Losera/incant-audio/pull/8#issuecomment-5276989855) but merge
      itself was not requested or performed.
- [ ] The third live-measured prompt (additive synth w/ reverb+delay+chorus) is **still
      rate_limited** by design — this is the residual PF-019 case Phase 2's rate-limit
      ledger / provider-fallback forks target, not a gap in this fix. Flagged to the user
      as expected, not silently left broken.
- [ ] No manual/GUI verification this session — all verification is `generate.py`
      called directly (Python layer only), not through the built Standalone. The UI
      changes from PR #8 (`PromptPanel.cpp`'s `retry_after` message) are covered by
      `PromptPanelThreadingTest` + the full C++ build, but nobody has looked at the
      actual rendered status line in a running plugin this session.

---

## 2. Key discovery this session — a second root cause the plan didn't predict

The approved plan's root-cause section assumed the delay-range failure was purely about
`de.delay`/`de.fdelay`'s MAXD argument (both effect and instrument prompts already had,
or now have, that rule). Live measurement after landing that fix showed "a simple
reverb" **still** failing the identical error. Tracing the actual generated code across
all 3 retry attempts (not just reading the final error) showed the model never wrote
`de.delay` — it called stdlib `re.stereo_freeverb(fb1, fb2, damp, spread)`
(`reverbs.lib:797`) and passed `spread` a smoothed continuous slider. `spread` is
documented in the library's own usage comment (`reverbs.lib:792`,
`"stereo_freeverb(0.7, 0.5, 0.3, 30)"`) as a literal channel-offset in samples — it sizes
an internal delay line exactly like `de.delay`'s first argument, but the trap is
invisible from the function's name or its other three (genuinely ordinary) arguments.

**This is the general lesson worth carrying forward, not just this one instance**: the
`DELAY_RANGE` retry hint (commit `4f68e6a`) is specifically worded around
`de.delay`/`de.fdelay`/`ef.echo`'s first-argument convention. Any OTHER stdlib function
with a hidden compile-time-constant argument would produce the identical
"invalid delay parameter range" error and get the identical, unhelpful hint. Worth a
`grep` sweep of `reverbs.lib`/`effect.lib`/`misceffects.lib` for other functions with a
buffer-sizing argument that isn't obviously named `n`/`maxd`/`max*`, if this error class
resurfaces on a prompt this session didn't test.

**Prompt headroom is now genuinely near its ceiling.** `system_prompt.txt`'s own test
history (`tests/test_prompt_headroom.py`) already documented slack shrinking
483→382→206 tokens over past sessions. This session's reverb fix needed real
compression to fit: a full compile-tested few-shot example did not fit even after
several rounds of trimming (tried: shortening prose 6→2 lines, shortening the stdlib
description, cutting `pf.vibrato2_mono` from the curated list) — only the prose rule
alone, with `vibrato2_mono` restored, fit (182 tokens headroom remaining). **This is a
structural problem, not a one-off**: the next stdlib-trap fix this project needs may not
fit at all without either (a) a genuine curated-list trim (real capability cost,
evidenced per-entry, not done casually — see the `AGENTS.md` scope-control rule), or (b)
switching the effect prompt to a bigger-context provider (the presentation prompt
variant already fails headroom against groq for the same reason,
`OPEN_QUESTIONS.md` Q6), or (c) moving trap documentation out of the always-loaded
prompt into the classified-retry-hint system (`error_classes.py`) instead, so it's paid
only on the retry that needs it, not on every generation. **Worth raising with the user
before the next prompt edit that needs headroom**, not deciding unilaterally.

---

## 3. Architecture question raised, explicitly back-burnered

User asked whether a more robust generation architecture is needed: `Prompt →
Architectural Agent (IR) → (another IR) → Faust → C++ → JUCE`, and asked for
research/summary/recommendation, but said to keep it back-burnered unless it helps the
current task.

**What I told the user, for continuity:**
- `CLAUDE.md`'s own "Do not" list already closes this door: *"Do not suggest JSON IR as
  an intermediary (decision was made against this)"* — with a stated rationale (Faust's
  algebraic DSL is more LLM-reliable than IR). What the user described is a variant of
  exactly that. Reopening it means updating `CLAUDE.md`, not just building around it —
  flagged, not silently complied with or silently refused.
- Assessed it does **not** help the actual problems found this session: both real bugs
  (prompt-filter contradiction, `stereo_freeverb` hidden-constant trap) were root-caused
  and fixed with small, targeted prompt edits in a few hours, and both are now
  live-verified working — evidence the current direct-to-Faust approach is fixable
  incrementally, not structurally broken. The one still-failing case (rate limiting) is
  an infrastructure/quota problem an IR layer doesn't obviously address, unless the
  IR→Faust stage stopped being LLM-driven entirely (deterministic template codegen) —
  which is a much bigger, different idea than "add an IR stage" and would need its own
  dedicated session with the full alternatives/tradeoffs/adversarial-critique treatment.
- **Recommendation given and accepted implicitly by the pause**: keep back-burnered. No
  research was spawned; no plan file was written for it. If picked up later, it starts
  cold — nothing beyond the paragraph above exists on this yet.

---

## 4. Loose end noticed, not touched

`docs/sessions/011-handoff-2026-08-12.md` is **untracked** (sitting in the working tree
across at least this and the prior session, never committed) and describes an earlier
UI-redesign session (C4-C7) as "paused mid-C4, nothing further should be started." That
work is now fully complete and merged (PRs #4, #5, #7) — the file's content is stale and,
per this project's own stated principle ("a document that lies is worse than a missing
one"), probably should either be deleted or updated to say so. Left alone both times it
was noticed, since it isn't this session's work to unilaterally discard — worth asking
the user whether to commit it (updated) or remove it.

---

## 5. Next recommended action

1. **Immediate**: none required — this is a clean pause point. PR #8 is green, pushed,
   commented with live-verification evidence, awaiting the user's merge decision.
2. **When resumed**: ask the user whether to merge PR #8, then decide between:
   - Phase 1 items (per the plan: groq `max_output_tokens` ceiling, compile-time
     instrumentation, UI Chrome breakpoints, host-side JIT classification) — contained,
     no architecture decisions;
   - Phase 2 architectural forks (rate-limit ledger, provider fallback, LLVM opt-level)
     — explicitly human-gated, needs the user's call on Fork A/B/C in the plan file;
   - The back-burnered IR-architecture question (§3) — cold start, nothing exists yet;
   - The stale `docs/sessions/011` file (§4) — small, quick cleanup, needs a yes/no.
3. Full detail and citations for anything Phase 1/2 live in
   `.claude/plans/you-are-the-lead-robust-pascal.md` — read it before starting, this
   handoff is a summary of what happened against it, not a replacement.
