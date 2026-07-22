# PluginForge — Collaboration Protocol

CLAUDE.md answers "what is this project." This document answers "how do we build it."
`STATUS.md` answers "where is it right now." Load this file at the start of any
non-trivial session.

**Revision 2 (2026-07-21).** Replaces the three-mode (DELEGATE / PAIR / HUMAN-OWNED)
protocol. Rationale for the change is in §9 — read it once, then never again.

---

## 1. The core

**Claude writes the code. The human sets direction and reads reports.**

Claude has write access to every file in this repository, including the real-time audio
path and the generation prompts. Authorship is no longer gated by file category. What is
gated is **consequence**: a small set of acts that are hard to undo or that change where
the project is going require a conversation first (§2). Everything else Claude does, then
reports (§4).

Two rules override everything below:

> **A known defect is never gated.** If Claude finds a bug, Claude fixes it — on the audio
> thread, in the prompts, anywhere — or names it explicitly as deferred, with the reason,
> in the report and in `STATUS.md`. Silently leaving a known defect in place because it
> sits in a "sensitive" file is a protocol violation, not caution.

> **Claude does not change architectural direction unilaterally.** Fixing how something
> works is Claude's call. Deciding what the project *is* — a new ADR, reversing an
> accepted one, a new dependency, a new contract between components — is a conversation
> (§2).

---

## 2. The consult gate

Ask before acting when a change hits any of these four. Otherwise: act, then report.

**1. Irreversible or outward-facing.**
Force-push, history rewrite, deleting data or results, publishing anything, sending
anything to a third party, spending real money. Also: overwriting a benchmark baseline or
a results file — those are measurements, and a measurement you cannot reproduce is data
loss.

**2. Architectural direction.**
Writing a new ADR, reversing or superseding an accepted one, or making a change that
forecloses an option an open ADR is still holding open. Claude drafts the ADR text and
proposes it; the human agrees; then Claude commits it. The drafting is not gated — the
*decision* is.

**3. A contract between components.**
The ADR-011 stdout JSON schema, the `macro_N` parameter slot scheme, the persisted state
format, the `ProviderSpec` shape, the efficacy-record schema. These have more than one
consumer, and changing one silently breaks the other at a distance.

**4. Build, dependency, or distribution.**
A new library, a new Python package, a CMake change that alters what links or what ships,
anything touching packaging or install layout.

**Not gated, to be explicit:** `FaustEngine`, `ParamPool`, `processBlock`, atomics and
memory ordering, `llm/prompts/*`, every Python module, tests, bench harnesses, docs,
CMake edits that don't change dependencies. These carry a higher **evidence** bar (§3),
not a permission bar.

**When it's genuinely ambiguous,** make the call, do the work, and say in the report:
"I treated this as ungated because X — tell me if that's wrong." One sentence, moving on.
Do not stall a session on a classification question.

---

## 3. The evidence bar

The dial that replaced mode gating. What lands is governed by what was checked, not by
who was allowed to type it.

### Tier 1 — routine

Python, docs, bench harnesses, tests, CMake, non-realtime C++, tooling.

**To land:** the test suite passes, and the change is reported (§4). That's it. Move fast.

### Tier 2 — consequential

Anything that runs on or synchronizes with the audio thread; any use of `std::atomic` or
explicit memory ordering; parameter mapping between APVTS and Faust; wire contracts; the
generation prompts.

**To land, all three:**

- **A primary source, cited by `file:line`.** Not "JUCE does X" — `juce_ChildProcess.h:88`.
  Not "Faust clamps this" — `/usr/include/faust/gui/MapUI.h:150`, which is where reading
  the actual header revealed it does *not* clamp. Library behavior is read, never recalled.
- **A test or a runnable check**, added or extended. If a property genuinely cannot be
  tested (a shutdown race, an audible result), say so and name what a human would have to
  do instead.
- **An explicit statement of what was NOT verified.** Every Tier 2 report ends with the
  unverified remainder. An empty remainder is a claim in itself and had better be true.

**"Verified" is a banned word without a named artifact.** "Looks correct," "should be
fine," and "this is the standard pattern" are not evidence and do not land Tier 2 changes.

**Prompts are Tier 2** because generation quality is sensitive to exact wording and every
benchmark number depends on the prompt being what it was when the number was measured.
Editing a prompt requires: state what changed and why, and either re-run the affected
benchmark or state that the baseline is now stale. Changing a prompt and leaving a stale
baseline in place is a Tier 2 violation.

---

## 4. The change report

Every landed change gets this, in-session, immediately. Five lines. It is not a summary of
what Claude did — it is what the human needs in order to decide whether to look closer.

```
CHANGED    <files> <+added/-removed>
WHY        <the defect or need, in one sentence — not the solution>
VERIFIED   <what was read (file:line), what was run, with results>
RISK       <what could still be wrong, or what this doesn't cover>
YOUR MOVE  <what the human should do, or "nothing">
```

Worked example:

```
CHANGED    host/Source/ParamPool.{h,cpp} +18/-6
WHY        Slot values (0-1) were pushed into Faust zones with real-world ranges,
           pinning a 20-20000 Hz cutoff below 1 Hz on every generated patch.
VERIFIED   MapUI.h:150 — setParamValue does *zone = value, no clamp, no mapping.
           234 tests pass. Swept cutoff in the standalone build; filter now tracks.
RISK       Linear map only. Frequency and gain are conventionally log-scaled, so
           10 kHz sits at knob-centre. Log scaling is a slot-range decision -> ADR-013.
YOUR MOVE  Listen to one filter patch, or defer until log scaling is decided.
```

`RISK` is the load-bearing field. A report whose RISK line is empty or reads "none" on a
Tier 2 change is a report that has not been thought about.

**`YOUR MOVE` must be honest about cost.** "Read this 40-line diff" and "listen to a
patch" and "nothing" are all valid. Manufacturing review work to seem collaborative wastes
the human's attention, which is the scarcest resource in this project.

---

## 5. STATUS.md

One file at the repo root, **rewritten** at the end of any session that changed something —
not appended to. It answers "where is this project" in under two minutes, for a reader who
has been away a week.

```markdown
# PluginForge — Status  (YYYY-MM-DD)

## Works — and how we know
One line per capability, each naming the evidence. "Builds clean" is not a
capability; "generates and JIT-compiles a working filter, verified by ear
2026-07-22" is.

## Broken — ranked
Defects, worst first. Each: what breaks, where, and whether a fix is in flight.

## Assumed, never checked
The most important section. Things believed true that nobody has verified —
stale baselines, untested paths, numbers measured under conditions that have
since changed. Items move out of here only when someone produces evidence.

## Next three things
Exactly three. Not a backlog.

## Waiting on you
Only items genuinely blocked on the human. Empty is a good answer.
```

Rewriting rather than appending is deliberate: an append-only log accumulates until nobody
reads it, and stale entries are indistinguishable from current ones. Narrative history
lives in git.

**`docs/collaboration_log.md` is retired.** It is kept for its record of decisions made
between 2026-05 and 2026-07-21, and is not written to again. §9 explains why.

---

## 6. Fail-loud markers

These survive unchanged. They work — the two `TODO: VERIFY` markers in `PluginEditor.cpp`
were both resolved on 2026-07-19 precisely because they were visible.

- **`// TODO: VERIFY: <claim>`** — the statement may be wrong. Include how to check it.
- **`// TODO: VERIFY API: <function>`** — reasoning about a library instead of reading it.
  Always name the header. Under §3 this may not remain in a landed Tier 2 change; resolve
  it or the change doesn't land.
- **`// SUBTLE: <condition>`** — a correctness condition a competent reader would miss.
  Reserve for real traps. This codebase's existing `SUBTLE` comments — the seq_cst Dekker
  handshake in `FaustEngine.h:69`, the double-buffer publication in `ParamPool.cpp:25` —
  are the standard to match. Do not dilute them with general explanation.

---

## 7. Mechanical enforcement

Removing authorship gates means the mechanical checks matter more, not less. A hook does
not get tired, does not rubber-stamp, and does not grade its own work.

Registered in `.claude/settings.json`, all fail-closed:

| Hook | Enforces |
|---|---|
| `check_rt_safety.py` | No allocation, locking, or I/O inside `FaustEngine::process` / `processBlock`. Function-scoped by brace counting. |
| `check_adr009_prompt_sync.py` | The ADR-009 rule text stays present and identical across both prompt files. |
| `check_bash_denylist.py` | Blocks the CLAUDE.md "Do not" commands. |
| `protect_human_owned.py` | **Retired by this revision** — it blocked `llm/prompts/*` and `docs/decisions.md` on authorship grounds, which §1 no longer does. |

**Two hooks must be strengthened as a direct consequence of this revision** (tracked in
`STATUS.md`):

1. `check_rt_safety.py` now guards code Claude writes routinely rather than code Claude was
   forbidden to write. Its known limitation — it scopes to two named functions and cannot
   follow a call graph — is now load-bearing. At minimum it should also scope
   `ParamPool::pushToFaust` and anything else reachable from `processBlock`.
2. `check_adr009_prompt_sync.py` verifies one sentence, but the two prompt files have
   **already diverged substantially** in ways it cannot see (see the 2026-07-21
   architecture review §2.4). Now that Claude can edit both, it should enforce either full
   equality or an explicit, declared divergence.

**When a hook is added, its docstring states what it does NOT catch.** The prompt-sync hook
is the cautionary case: it did exactly what it documented, while the team believed it
guaranteed something stronger. A check on a proxy creates confidence proportional to the
invariant, not to the proxy — say the quiet part in the docstring.

---

## 8. Where information lives

| Location | Belongs there | Does not |
|---|---|---|
| **CLAUDE.md** | Stack, architecture, the "Do not" list, file map, pointers to ADRs. Durable facts. | Current status (→ `STATUS.md`), decision rationale (→ `docs/decisions.md`), process (→ here). |
| **STATUS.md** | Current state: what works, what's broken, what's assumed, next three, blocked-on-human. Rewritten, never appended. | Durable architecture (→ CLAUDE.md), rationale (→ ADRs). |
| **COLLABORATION.md** | The gate, the evidence bar, report formats, this table. | Project facts, ADR content, status. |
| **`docs/decisions.md`** (+ `docs/architectural_decisions/`) | Append-only ADR log. Superseded, never deleted. | Status (→ `STATUS.md`), process (→ here). |
| **`docs/` reviews & studies** | Point-in-time analysis with a date in the filename. Read-only after the fact; supersede with a new dated file rather than editing. | Anything expected to stay current. |
| **`.claude/hooks/`** | Deterministic enforcement of a decided invariant, plus what it does not catch. | The rationale (→ ADR). |
| **`.claude/agents/`, `.claude/skills/`** | Repeatable procedures with narrow scope. Point back at these docs. | Duplicated project knowledge. |
| **Claude's persistent memory** | Cross-session heuristics not yet stable enough to check in. | Anything durable — promote it here or to CLAUDE.md and delete the memory. |

CLAUDE.md's per-file status narrative should migrate to `STATUS.md` — it is the single
largest source of staleness in the repo today, and it is exactly the "current state"
category that now has a home.

---

## 9. Why this replaced the three-mode protocol

Kept short, because it only needs reading once.

Revision 1 classified every task as DELEGATE, PAIR, or HUMAN-OWNED, gating **authorship**
by file category. Four things went wrong.

**PAIR was fictional.** It specified that Claude drafts and the human writes the committed
version. CLAUDE.md records the actual outcome: `ParamPool.cpp — IMPLEMENTED (PAIR draft)`,
and `PluginEditor.cpp — IMPLEMENTED (PAIR draft landed) … Awaiting human read-through per
PAIR mode`. The drafts became the implementation; the read-through never happened. PAIR was
DELEGATE plus latency plus a false impression that review had occurred.

**The feedback loop never fired.** `docs/collaboration_log.md` has 22 entries. Its
`Mode signal` field was designed to surface wrong classifications. It says "Correct call,"
"Correct mode," "The protocol did its job" almost every time. A self-graded rubric that
never returns a failure is not a control.

**The gating did not catch the bug it existed for.** `ParamPool::pushToFaust` was
classified PAIR — it is COLLABORATION.md's own worked example, cited verbatim in the log.
The parameter-denormalization defect, which silently disabled the controls on essentially
every generated plugin, shipped through that gate and survived every one of 234 tests.
What eventually found it was reading `MapUI.h` line by line against the calling code.
**Verification behavior caught it; authorship ceremony did not.** §3 is the direct
consequence: mandate the behavior that worked, drop the ceremony that didn't.

**HUMAN-OWNED was overridden whenever it was load-bearing.** The log records "stated
deviation — threading fix executed directly on explicit human instruction" and
"HUMAN-OWNED, explicitly overridden by the human." A gate that opens whenever it matters
is a delay, not a control.

Meanwhile the parts that genuinely worked — the fail-closed hooks, the `SUBTLE`/`VERIFY`
markers, the ADR discipline — were the mechanical and evidentiary ones, not the
permission-based one. This revision keeps all of those and trades authorship gating for a
consequence gate plus a citation requirement.

---

## 10. Changing this document

This protocol has no inertia. If it is slowing the project down, say so and it changes in
the same session. If Claude thinks a rule here is wrong, Claude says so in the report
rather than silently routing around it — a protocol that gets quietly ignored is worse
than one that gets argued with.

Deviations are fine when stated: *"§2 trigger 3 arguably covers this, but the schema has
one consumer and I changed both sides in the same commit — treating as ungated."*
