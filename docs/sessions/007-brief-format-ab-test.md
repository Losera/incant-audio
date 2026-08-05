# Session 007 — A/B test: lean briefs vs. heavy pre-digested briefs

**Status: design pre-registered, not yet run.** Everything below this line is committed
before either arm launches, so a reader can confirm from `git log` that the rubric and
decision rule predate the outcomes.

Motivated by [claude.com's context-engineering
article](https://claude.com/blog/the-new-rules-of-context-engineering-for-claude-5-generation-models)
(principles 2, 3, 10: interface design over worked examples, progressive disclosure,
pointers over transcription). Session 006's brief-authoring pass spent ~50k tokens
pre-digesting five briefs — exact `file:line` citations, JUCE virtual signatures, and
trap explanations transcribed in full — on the premise that a Claude 5-generation
executing agent needs that pre-digestion the way older models did. This session tests
that premise directly, on a real task, rather than assuming either way.

## 0. Hypothesis, stated so it can lose

**H1** (the article's claim, applied here): a lean brief — declarations, a gap statement,
`file:line` pointers, and open discovery questions — produces work of equal correctness,
scope discipline, and evidence quality to a heavy pre-digested brief, at materially lower
combined token cost.

**H0** (the incumbent): heavy pre-digestion earns its cost, because the executing agent
will otherwise miss a non-obvious trap that the heavy brief would have transcribed.

**The pre-registered falsifier:** if the lean arm ships a fix that gets silently destroyed
by a later status-label write (§1.2), H1 loses on this task outright, independent of any
token-cost numbers.

## 1. The subject task

### 1.1 The gap

`llm/generate.py:338-342` drops `prior_source` when `providers.preflight_prior_source`
fails a token check hardcoded to groq's ceiling (`llm/providers.py:121,140-162`). On
success it adds `"prior_source_dropped": true` to the response
(`llm/generate.py:381-386`), specifically so the host can tell the user
(`docs/decisions.md:296-302`). `grep -rn prior_source_dropped host/` returns nothing —
`PromptPanel.cpp:526-533`'s response parser never reads it. A user ticks Refine, silently
gets a full regeneration, and the UI reports an ordinary success.

This is a real, correctly-scoped, currently-open defect — not the task I originally
assumed. STATUS.md's Broken #3 / former Next-three #2 ("Refine does not carry the
source") is **stale**: the source-carrying mechanism was fully built and wired
end-to-end on 2026-08-04 (`5090b55`). That claim gets corrected as part of this write-up's
outcome (§6), not as a separate task.

### 1.2 The trap (verified directly, both citations read in full this session)

`PromptPanel.cpp:579` sets a "JIT compiling…" status. `PluginEditor.cpp:92-113`'s
`onFaustCompileSuccess` callback later overwrites it: `"Ready — DSP live, " + numParams +
" params mapped."` (confirmed at `:108-111`). Two more writers of the same label exist —
`:79-87` (compile failure), `:279` (output-guard mute). A fix that writes the drop notice
straight into the status label from the response handler is destroyed within a few
message-thread hops and is invisible in the running app. A correct fix needs the notice to
survive past "DSP live" — the same lifetime `clearError()` already implements for
`errorBox` (`PromptPanel.cpp:602-606`, PF-021: persist across a later success, clear on
the next submit).

### 1.3 Identical declarations, pinned for both arms

```
touches:  host/Source/PromptPanel.h, host/Source/PromptPanel.cpp,
          host/tests/FakeGenerator.h, host/tests/EditorSessionTest.cpp
depends:  llm/CONTRACT.md:3-13 (generate.py's response schema, additive keys)
provides: nothing new
```

`FakeGenerator.h` is in scope for both arms because `writeSuccessCapturing`
(`FakeGenerator.h:142-170`) hardcodes a five-property response object with no way to emit
`prior_source_dropped` — omitting it from either arm's `touches` would force
arm-specific scope drift, exactly the confound this experiment exists to avoid.

## 2. The two briefs

Committed alongside this file:
- `docs/sessions/007-briefs/lean-prior-source-dropped.md` (34 lines)
- `docs/sessions/007-briefs/heavy-prior-source-dropped.md` (~120 lines)

Both pinned to the identical scope in §1.3. The heavy brief transcribes the trap (§1.2)
and prescribes the fix; the lean brief asks a discovery question ("who else writes that
surface after a successful generation, and what does that mean for how long your notice
survives?") pointing at the same grep, without naming the answer. This is a deliberate,
named concession: the honest test is *pointers vs. transcription*, not *withheld vs.
given* information — a brief that concealed the trap's existence would be testing
sabotage, not interface design.

## 3. Protocol

1. **Pre-warm both worktrees** (`.claude/worktrees/exp-007-heavy`, `exp-007-lean`,
   branched from the post-commit `HEAD`) with `tools/check.sh full` before either agent
   launches — isolates the cold-JUCE-build cost as noise common to both arms, not part of
   either agent's measured cost.
2. **Sequential, heavy first**, from the same verbatim wrapper prompt (differing only in
   which brief path it names), written before either launches:

   > Execute the brief at `docs/sessions/007-briefs/<X>-prior-source-dropped.md`. It is
   > self-contained; read it first and follow it. You are in a git worktree — work only
   > here. Do not read any other file under `docs/sessions/007*`. End with the
   > COLLABORATION.md §4 change report in your final message.

   No mid-run intervention; a stalled agent gets `Proceed using your own judgment; the
   brief is the whole spec.`, logged as a deviation. Parallel execution is rejected on a
   concrete ground: both arms run `check.sh full` (JUCE + TSan), and running two
   concurrently makes wall time — a reported metric — uninterpretable.
3. **Blind adjudication**: one further fresh agent scores the subjective rubric items
   against the two diffs, labeled A/B with brief-identifying text stripped. The
   orchestrator's own scoring is recorded alongside, not reconciled away on disagreement.

## 4. Pre-registered rubric

### Gate C — correctness (6 items; C1 and C3 are load-bearing)

| # | Item |
|---|---|
| C1 | `tools/check.sh full` green in that worktree |
| C2 | Notice reaches the user for a `prior_source_dropped:true` response, through the real editor |
| C3 | **Notice still present after "DSP live" appears** — the trap check, and the hypothesis test itself |
| C4 | Absent key ⇒ no notice (negative case present, not just the positive) |
| C5 | Red case run and reported: something broken, seen failing, restored |
| C6 | No regression: scenario16 still passes; scenario-count string and `PF_SUMMARY` line stay consistent |

C1 false ⇒ that arm fails outright. **C3 false ⇒ that arm cannot win regardless of any
other score** — it is the falsifier from §0, not an ordinary point.

### Block S — scope discipline (3 items)

| # | Item |
|---|---|
| S1 | `git diff --name-only` ⊆ declared `touches` |
| S2 | Zero edits under `llm/`, `PluginEditor.*`, `STATUS.md`, `docs/decisions.md` |
| S3 | The provider-aware-preflight question, if raised, appears in prose (change report), never in a diff |

### Block E — evidence quality (4 items)

| # | Item |
|---|---|
| E1 | All five COLLABORATION.md §4 fields present and non-boilerplate |
| E2 | ≥1 `file:line` citation that **resolves** when checked against the real file |
| E3 | RISK non-empty and names a specific uncovered case (not "none"/"low"/"minimal") |
| E4 | Explicit unverified remainder stated |

### Block K — cost (reported, not scored)

Executing-agent tokens, wall time, turns/resumptions, any orchestrator intervention
(each flagged as a deviation), and authoring cost — **flagged explicitly as an estimate,
not a clean measurement**, since both briefs were authored in one pass sharing one
investigation.

## 5. Decision rule (fixed now)

- Either arm failing Gate C ⇒ the other wins on quality outright.
- Both pass ⇒ compare C+S+E (max 13). Δ ≥ 3 ⇒ quality win. Δ ≤ 2 ⇒ quality tie, broken on
  K1+K4 (lower cost wins).
- **Recommend switching this project's default brief format to lean only if** lean
  ties-or-wins on quality **and** its K1+K4 ≤ 60% of heavy's.
- **Ceiling, regardless of result:** n=1 on one task shape. At most a provisional default
  with a second trial named — never a COLLABORATION.md amendment. Same posture
  `docs/sessions/006-multi-agent-trial-results.md` §4 took toward Mechanism A, for the
  same reason: one session's data is not a ratification.
- The losing arm's branch is **kept, not deleted**, so its diff stays inspectable.

## 6. Threats to validity, named before the result exists

1. n=1 per arm, one task shape (UI response-handling with a lifetime trap) — not
   representative of doc sweeps, prompt edits, or DSP work.
2. I authored both briefs and already knew the trap before writing either. The heavy
   brief transcribes it; the lean brief's discovery question is shaped by that same
   knowledge. A blinder author would be cleaner — named, not fixed, this trial.
3. Experimenter bias toward a preferred outcome — mitigated by pre-registration (this
   commit), blind adjudication (§3.3), and C3 as a named falsifier, not eliminated.
4. K4 (authoring cost) is an estimate, not a measurement, for the reason stated in §4.
5. K1 rises with how much the agent reads — that's the mechanism under test, but it means
   K1 and "did the agent research enough" are not independent variables.
6. Same model, tool set, permission config, and base SHA for both arms — recorded in §7
   once run, not assumed.

## 7. Protocol as executed

*(Filled in after both arms run.)*

## 8. Results

*(Filled in after scoring.)*

## 9. What landed and why

*(Filled in after the decision rule is applied.)*

## 10. Recommendation

*(Filled in last — opens with the sample size, per §5's fixed ceiling.)*
