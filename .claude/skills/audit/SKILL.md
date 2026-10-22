---
name: audit
description: Re-runs seven computed, falsifiable checks on PluginForge's own workflow machinery — does recorded state match the repo, do the defect registries agree, is finished work stranded unmerged, has HEAD seen CI, does every gate have a red case, does the one KPI read a real number, and what is blocked on a human and for how long. Use to measure process drift since the last time this ran, not to re-litigate it. Trigger on "audit the workflow", "check our own process", "has anything drifted", "/audit".
---

# Audit

Added 2026-10-20 (workflow audit), after finding that this project's own instruments —
`tools/health_report.py`'s product lane, the control-wiring test suite, the ADR log —
already computed most of what a workflow review would otherwise have to re-derive by
hand, but were wired into a lane that is rarely read and absent from `/orient`, the one
read every session is supposed to start with. `/orient` and this skill now both surface
that computed state; this skill exists for the slower, deliberate pass: run it when you
want to know whether the machinery itself has drifted, not when you want today's open
defects (that's `/orient`) or a cold-entry briefing (that's `/brief`, where present).

**Each item below is a question with a command that answers it and a stated pass
criterion** — not an open "how are we doing," which cannot be re-run to measure drift,
only re-argued. Run the commands, compare the real answer to the pass criterion, and
report any mismatch plainly rather than summarizing around it.

---

## T1 — Does recorded state match the repo?

**Command:** `tools/status_digest.sh` (what `/orient` already injects) — read the
staleness banner under `### STATUS.md`.

**Passes when:** no banner, or the gap between the stamped date and HEAD is explained by
what actually landed in that gap (check `git log --since=<stamped date>`).

**A failing answer means:** work landed that nobody wrote down. Everything the digest
prints under *Broken*/*Assumed* should be treated as provisional until you've looked at
what changed in the gap — this is COLLABORATION.md §5's own rule, not a new one.

## T2 — Do the defect registries agree?

**Command:**
```
python3 -c "import sys; sys.path.insert(0,'tools'); import health_report as h; \
import json; print(json.dumps(h._scan_bugs_registry()['counts'], indent=2))"
```
then compare the total against `grep -cE '^\| PF-' docs/BUGS.md` (the real row count).

**Passes when:** the counts sum to the row count. (They will not if `docs/BUGS.md`'s
`Conventions` line names a status `tests/test_bugs_registry_integrity.py`'s
`REGISTRY_STATUSES`/`health_report.py`'s parser doesn't recognize yet — this is exactly
how PF-011 and PF-076 were silently dropped from every count until 2026-10-20, found by
this audit's own first run.)

**A failing answer means:** a row is using a status word the parser doesn't know, and is
invisible to every tool that reuses that parser (not just this one — `_scan_bugs_registry`
is also what `tools/health_report.py --lane product` reports). Check `docs/BUGS.md:29`'s
own convention line against `tests/test_bugs_registry_integrity.py`'s
`REGISTRY_STATUSES` first.

## F1 — Is any finished work unmerged?

**Command:** `python tools/integration_report.py` (also surfaced by `/orient`'s
"Integration state" section).

**Passes when:** no row is simultaneously clean (`dirty: 0`), `behind_main: 0`, and
`PR: none` — that combination means a branch is fully ready to open as a PR and nobody
has.

**A failing answer means:** real, finished work is sitting in a worktree instead of being
reviewed. This is not hypothetical: at the time this skill was written, nine branches sat
in exactly that state, including a landed bugfix (PF-083) with its own regression test.
Report which branches qualify; do not push or open PRs yourself unless asked —
COLLABORATION.md §2's "gate the write, never the run" applies here as everywhere.

## F2 — Has HEAD seen CI?

**Command:** `gh run list --branch "$(git rev-parse --abbrev-ref HEAD)" --limit 5`, or
read `/orient`'s `### CI` section, which already does this.

**Passes when:** the most recent completed run's commit is HEAD, and its conclusion is
`success`.

**A failing answer means:** either CI is red (read it before trusting any local green),
or CI is green on a commit behind HEAD (meaning nothing has verified what you're actually
looking at). `tools/check.sh full` passing locally does not substitute for this — it runs
a smaller set of checks than CI does (CLAUDE.md, PF-029).

## I1 — Does every gate have a red case?

**Command:** `python -m pytest tests/test_control_wiring.py -q`

**Passes when:** green, **and** every hook/gate/generator added since this audit last ran
appears somewhere in that file. (Checking the second half requires reading
`tests/test_control_wiring.py`'s class list against `.claude/hooks/` and `tools/*.py`'s
`--check` entries by eye — there is no meta-meta-test for this, by design; see
COLLABORATION.md §7.)

**A failing answer means:** a declared control that has never been proven to fail. This
project's own recurring defect, named explicitly in CLAUDE.md: "a control counts only
once it has been seen failing." Three separate controls (PAIR mode, the ADR-009 sync
hook, five PreToolUse hooks silently misconfigured) were each found this way before being
fixed.

## I2 — Does the KPI report a real number?

**Command:** `tools/check.sh assumed`

**Passes when:** it exits 0 and prints a claim count with no `FORMAT DRIFT DETECTED`
banner.

**A failing answer means:** `STATUS.md`'s "Assumed, never checked" section has a claim
written as bare bold prose with no leading `- **` bullet, invisible to the counter even
though a human reading the section sees it immediately. This happened for real — PF-079's
claim sat uncounted for two days before this audit found it (2026-10-18 → 2026-10-20) —
which is why `tools/check.sh assumed` now cross-checks against
`tools/health_report.py`'s `_check_assumed_drift` and fails loud rather than silently.

## D1 — What is blocked on a human, and for how long?

**Command:** `tools/status_digest.sh`'s `### Decision queue` section (ADRs still
`Proposed`, plus any using a status value outside the four the template names — read,
don't assume, those are undecided too) and `### Waiting on you` (from `STATUS.md`).

**Passes when:** nothing in either list is older than about a month without a stated
reason it's deliberately parked.

**A failing answer means:** the project's actual throughput limit is human decision
latency, not agent output — and no amount of further agent work closes that gap. Naming
the oldest few items plainly, with their age, is more useful here than doing more
unrelated work around them.

---

## Using the results

Report mismatches plainly — a sentence per failing item, with the command that showed
it — rather than narrating the whole table every time. Something that passed last run and
fails this run is the signal worth raising first; something that has failed every run so
far (most of "Waiting on you," most Proposed ADRs) is already known and doesn't need
re-announcing, just re-dating.

This skill does not fix anything it finds. Per COLLABORATION.md §1, deciding what to do
about a stranded branch, an old ADR, or a format drift is explicitly not this skill's
job — it reports, the same way `tools/kg.py` and `tools/integration_report.py` do, and
for the same stated reason: a zero-assertion check wired into a pass/fail gate reports
healthy when it is actually just silent.
