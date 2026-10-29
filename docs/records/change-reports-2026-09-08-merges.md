# Change reports — the 2026-09-08 merge batch (#73 / #72 / #74 / #75)

Four PRs squash-merged to `main` on 2026-09-08 → 2026-09-09 (`1b43361` → `a778853`).
Their `/change-report`s (COLLABORATION.md §4) were owed but not written at merge time;
this file discharges that debt as part of the 2026-09-09 consolidation pass.

Written from each PR body plus the merged diff read at `a778853`. Where a line says
"PR body reports", this consolidation session did **not** re-run that check — it read
the merged diff and confirmed CI. Post-merge CI is green on the cumulative heads:
run `34292837792` on `14109a9` (covers #73+#72+#74) and run `34295993074` on `a778853`
(covers #75 on top). The `cancelled` runs on `2f03141` / `7970d66` are supersessions by
the rapid successive pushes, not failures.

---

## PR #73 — `2f03141` — docs: correct verified-false claims in session-loaded docs

```
CHANGED    CLAUDE.md +16/-19, INTERFACE.md +34/-30, PLUGIN_HEALTH_PLAN.md +10/-6.
           Prose only, no code or tests.
WHY        These three files load into every session's context and carried
           verified-false claims: wrong toolchain versions, a defended
           non-decision (the "Faust 2.85.5 stamp is a deliberate deferral"
           paragraph, when the regen had already happened), an advertised
           "VST3/AU" build with no AU target, and an INTERFACE.md citation set
           that had drifted ~700 lines off its line-number anchors.
VERIFIED   PR-body claims spot-checked against the merged tree at a778853:
           CLAUDE.md now reads kernel 7.1.8-arch1-3, CMake 4.4.2, Python 3.14.7,
           "VST3 + Standalone", stdlib stamp "2.85.9"; the 7-line "still stamped
           2.85.5" paragraph is gone; INTERFACE.md citations are symbol-anchored.
           check.sh fast green on the branch pre-merge (PR body) — it runs
           test_control_wiring.py's LIVE_DOC_FILES dead-reference check over
           these three files. Prose-only, so full was not run and is not needed.
           CI: cumulative green run 34295993074 on a778853.
RISK       Named out-of-scope and still stale (PR body): docs/phases/phase-3.md
           defect count, phase-4.md PF-065/PF-071 closure state,
           docs/decisions.md ADR-008/ADR-011 status rows, COLLABORATION.md:374
           md:code ratio, CLAUDE.md:141 "18 closed defects". Those need the
           STATUS.md reconciliation, not a typo fix. Nothing here touches code.
YOUR MOVE  Nothing — merged and green. The out-of-scope stale claims above are
           carried for a future docs-reconciliation session.
```

---

## PR #72 — `7970d66` — fix: ADR-011 credential precheck resolves the request's own provider

```
CHANGED    llm/generate.py +69/-15, tests/conftest.py +15/-0,
           tests/test_generate_unit.py +37/-3.
WHY        _run_subprocess_mode ran providers.check_credentials(DEFAULT_PROVIDER)
           before parsing the request, so it could only ever check the
           import-time default — never the per-request `provider` the ADR-032
           in-plugin picker sends. Consequence 1: provider X's request had X's
           credentials checked against DEFAULT_PROVIDER's. Consequence 2 (worse):
           assert_free(DEFAULT_PROVIDER) had the same blind spot, so a request
           explicitly naming paid `anthropic` bypassed the ADR-012 free-only
           guard whenever DEFAULT_PROVIDER was free.
VERIFIED   Reproduced without the C++ host (PR body): an ollama request under
           PLUGINFORGE_PROVIDER=groq reported a GROQ_API_KEY error. Fix parses
           the request first in its own try/except (malformed --json now returns
           ADR-011 JSON, not a stdout traceback), then
           resolve_provider(request.get("provider", DEFAULT_PROVIDER)) — the same
           fallback generate.py's other provider call sites use — then
           assert_free + check_credentials against that provider. __main__ keeps
           assert_free(DEFAULT_PROVIDER) for the plain-CLI path. PR body reports
           843 non-integration tests pass, 211 in the targeted suites, and the 2
           prior TestGenerateSubprocessModeWithFreeProvider failures fixed. This
           session read the merged diff at 7970d66 and confirmed the shape; did
           not re-run the suite. Human-reviewed 2026-09-07 (PR body). CI:
           cumulative green runs 34292837792 (14109a9) and 34295993074 (a778853).
RISK       Tier-2 — touches the ADR-012 / PLUGINFORGE_ALLOW_PAID contract
           (COLLABORATION.md §2.3). conftest.py now setdefault()s
           PLUGINFORGE_ALLOW_PAID=1 suite-wide, because assert_free was
           previously only reachable via __main__ (which no unit test calls) and
           moving it into _run_subprocess_mode makes every subprocess-mode test
           hit the guard. Net effect: the free-only guard is OFF by default in
           tests — a future free-only regression test must pop the key from its
           own env or it will silently not exercise the guard. Merged on the
           human's explicit instruction rather than via a filed review artifact.
YOUR MOVE  Nothing further — merged, human-reviewed, green. Remember the
           ALLOW_PAID=1 suite-wide default when writing any future free-only test.
```

---

## PR #74 — `14109a9` — bench: commit + score the completed groq efficacy grid (PF-011)

```
CHANGED    STATUS.md +34/-22,
           bench/results/efficacy/efficacy_groq_20260831.json +1778/-10.
WHY        The 125-cell groq / openai/gpt-oss-120b efficacy grid — the project's
           one steering metric — had run to completion (2026-08-31 → 09-07) but
           existed only as an uncommitted tracked file in the shared primary
           checkout, one `git restore` from data loss (COLLABORATION.md §2.1).
VERIFIED   No generation run, no quota spent, --judge not used (PR body). The
           committed JSON is byte-identical (sha256 05ebf06e…) to the working-
           tree checkpoint. bench/score_efficacy.py ($0, offline): retry-
           corrected compile L4 92% / L3 96% / L2 96% / L1 84% / L0 88%;
           first-try L2–L4 all 84%, L1 48%, L0 72%. Invariants asserted on the
           committed file: 125 cells, 5×25 tiers, single (groq, gpt-oss-120b),
           no empty code, no dup keys. check.sh fast green on branch. This
           session confirms STATUS.md at a778853 carries the closure: PF-011 in
           "Works", "Assumed, never checked" reads "*(none.)*", `check.sh
           assumed` prints 0. CI: green run 34292837792 on 14109a9.
RISK       n=1 per cell — PF-031's ≥3-run bar is unmet and the pass is unjudged,
           so the tier finding (no monotonic gradient; L1 metaphor-only is a
           sharp trough at 48%; dynamics weak at every tier) is a compile-rate
           result, not a fidelity result. After PR #75 the JSON's dynamics
           numbers are a *pre-fix* baseline (see #75). One L1 cell's 3rd
           corrective attempt was rate-limited and scored as failure →
           retry-corrected L1 is a lower bound, understated by ≤1/125.
YOUR MOVE  Nothing to merge. A listening pass on generated patches still outranks
           these compile numbers (COLLABORATION.md §1). The deeper n≥3 + judged
           bar is tracked as new evidence work, not a PF-011 re-open.
```

---

## PR #75 — `a778853` — prompt: dynamics few-shot + routing_arity retry hint (PF-024 mitigation)

```
CHANGED    llm/prompts/system_prompt.txt +9/-10,
           llm/prompts/system_prompt_presentation.txt +9/-10 (regenerated),
           llm/error_classes.py +26/-0, tests/test_routing_arity_hint.py +132/-0
           (new), STATUS.md +25/-8.
WHY        routing_arity is PF-024's largest first-attempt failure class and
           dominates the dynamics category (10/25 cells on the 2026-08-31 grid).
           Mechanism, reproduced byte-for-byte against faust 2.85.9: the model
           passes the audio signal as trailing `_` args to a stdlib dynamics
           effect — co.compressor_stereo(r,t,a,rl,_,_), ef.gate_stereo(...,_,_) —
           but gate_stereo/compressor_stereo reference x,y multiple times in the
           body (misceffects.lib:182, compressors.lib:1002), so `_` (identity,
           inlined per occurrence) balloons the input count to 6 while `_,_`
           supplies 2 → hard `:` arity error. The prompt carried the rule but had
           zero dynamics few-shots, and error_classes.ROUTING_ARITY had no
           RETRY_HINT, so the retry loop fed back only post-desugaring stderr
           that names no remedy; dynamics-01/L4 and dynamics-03/L4 each repeated
           the identical error on all 3 attempts.
VERIFIED   Tier-2 (a generation prompt). PR body: tools/check.sh full green
           (prompt grounding, few-shot compile in BOTH prompt variants,
           gen_presentation_prompt.py --check, build, TSan trio, OfflineRenderTest,
           EditorSessionTest); test_routing_arity_hint.py + test_delay_range_hint.py
           8/8; test_prompt_stdlib/claims/headroom + test_classify_failures +
           test_efficacy_unit 137 passed / 2 skipped. Arity mechanism reproduced
           byte-identical; the new few-shot compiles standalone to 2-in/2-out.
           Headroom: test_prompt_headroom.py slack 140 → 128 (net-neutral swap,
           tape-flanger few-shot removed to make room). This session read the
           merged diff at a778853. CI: green run 34295993074 on a778853.
RISK       Baseline statement (COLLABORATION.md §3): the prompt changed AFTER
           efficacy_groq_20260831.json was measured, so that file's dynamics
           numbers are now the pre-fix baseline and are stale for the current
           prompt. Whether a live model actually stops making the mistake is NOT
           verified — this is a hypothesized fix, PF-024 stays open, and
           confirmation needs a fresh groq grid (spends quota); the
           test_routing_arity_hint.py docstring and the RETRY_HINT comment both
           say so. The few-shot swap dropped the only worked flanger example;
           modulation scored 80–100% on the grid so the regression risk is judged
           low but is unmeasured. The 6 SYNTAX-class dynamics failures (*preamp
           prefix-op misuse, `with { main = _; side = _ }` input-naming) are a
           related but distinct confusion this does not touch. Reviewer
           alternative in the PR body: keep flanger, trim the curated stdlib list
           in gen_stdlib_block.py instead.
YOUR MOVE  Re-run the groq efficacy grid (STATUS.md Next-three #1, the *(evidence)*
           slot) to confirm the dynamics routing_arity first-attempt rate moves
           against the committed pre-fix baseline — then a listening pass on a
           compiled dynamics patch (compile ≠ musical, COLLABORATION.md §1).
```
