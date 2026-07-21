# DRAFT — ADR-009 verdict addendum

**Status: draft for human review. Not committed to the ADR.**

`docs/architectural_decisions/` and `docs/decisions.md` are HUMAN-OWNED under
COLLABORATION.md §2 (Claude may draft, the human authors). This file is the drafted text;
if you accept it, paste it into
`docs/architectural_decisions/ADR-009-faust-duplicate-symbol-prompt-fix.md` and delete this
file.

Drafted 2026-07-21. Two changes are proposed: a **Verdict** section (the ADR still carries
an unfalsified prediction) and a **correction** (the ADR names a file that does not exist).

---

## Proposed change 1 — append a Verdict section

The ADR currently ends at "Consequences" with an open action item — *"Re-run the benchmark
after applying the fix to confirm ≥96% on Faust"* — and `Status | Accepted`. That re-run
happened on 2026-07-19 and the prediction did not hold, but the ADR was never updated, so
it still reads as though the ≥96% claim is pending confirmation rather than falsified.
Suggested text to append:

```markdown
## Verdict (2026-07-19)

**The rule worked. The ≥96% prediction did not hold.** Both halves matter, and they are
easy to conflate.

Full 25-prompt Faust re-run measured **22/25 (88%)** first-try, against the ≥96% estimated
in "Reasons" above. The committed baseline moved 0.84 → 0.88
(`bench/results/.prompt_baseline.json`). Detail: `docs/prompt_efficacy_study.md` §6.

What the rule actually did: **the duplicate-symbol error class is gone.** None of the three
remaining failures is a `multiple definitions of symbol` error — the failure this ADR
targeted no longer appears anywhere in the suite. Of the three prompts named in "Context"
(sidechain compressor, brick-wall limiter, ping-pong delay), the limiter now passes, and
the other two fail for unrelated reasons:

| Prompt | Class | Error | Relation to ADR-009 |
|---|---|---|---|
| ping-pong delay | SEMANTIC | endless evaluation cycle (circular `with{}`) | Different root cause; repeat of 2026-05 |
| tape-style flanger | HALLUCINATION | `undefined symbol: flanger_mono` | Never an ADR-009 target; repeat of 2026-05 |
| sidechain compressor | SYNTAX | `syntax error, unexpected ARROW` | Different root cause; new failure |

So the estimate was wrong in its arithmetic, not its mechanism. "Reasons" assumed that
removing the duplicate-symbol error would make those prompts *pass* — but two of the three
had a second, independent defect underneath, and the estimate did not account for failure
modes outside the duplicate-symbol class at all.

**Durability of the remaining failures.** Two of the three (ping-pong SEMANTIC, flanger
HALLUCINATION) are exact repeats of the 2026-05 run — the same errors on the same prompts
two months and one prompt-revision apart. These are stable, reproducible failure modes, not
sampling noise, which makes them good targets for a follow-up rule or few-shot example.
Authoring that is HUMAN-OWNED (it touches `llm/prompts/*`) and is tracked as **P17**.

**Lesson for future ADRs.** A predicted rate should state which failure classes it covers
and assume nothing about the ones it does not. Predicting a suite-level number from a
single-error-class fix over-counts whenever a failing item has more than one defect.

**Status change:** the decision stands (the rule is correct and stays), but the ≥96%
prediction in "Reasons" is **falsified** — read it as a historical estimate, not a
target. The "Consequences" action item ("Re-run the benchmark … to confirm ≥96%") is
**closed: re-ran 2026-07-19, measured 88%, did not confirm.**
```

---

## Proposed change 2 — correct the prompt-file path

**"Decision" says the rule goes in `llm/prompts/system_faust.txt`. That file does not
exist** and, as far as the tree shows, never did. The rule text actually lives in two
files:

- `llm/prompts/system_prompt.txt` — the product path used by `generate.py`
- `bench/prompts/system_faust.txt` — the benchmark path

Suggested replacement for the "Decision" preamble:

```markdown
Add a single rule to **both** prompt files that carry the Faust system prompt —
`llm/prompts/system_prompt.txt` (product path, used by `llm/generate.py`) and
`bench/prompts/system_faust.txt` (benchmark path):
```

and a matching line under "Consequences", replacing the current single-file entry:

```markdown
- Both `llm/prompts/system_prompt.txt` and `bench/prompts/system_faust.txt` gain one
  constraint rule, and must be kept in sync. The sync is enforced mechanically by
  `.claude/hooks/check_adr009_prompt_sync.py`.
```

This matters beyond tidiness: the two-file duplication is exactly the drift risk that
motivated the `check_adr009_prompt_sync.py` hook, and an ADR pointing at a third,
non-existent path undercuts the hook's rationale for anyone reading the decision record
first.

---

## Note on a related stale record

`docs/decisions.md`'s ADR-011 hardening table still lists **ready-state UX** as `Open`. It
was implemented 2026-07-19 (`PluginForgeProcessor::onFaustCompileSuccess`; the status label
now reports "Ready — DSP live, N params mapped" on JIT swap). Also HUMAN-OWNED — flagging
it here rather than editing, since you will be in that file anyway.
