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

### The human's irreplaceable contribution is listening

*Added 2026-07-28.* Nearly everything in this loop can be mechanized, and most of it now
has been: hooks catch the RT-safety and prompt-grounding violations, `tools/check.sh` runs
the ladder, the render oracle catches NaN, silence, DC offset and runaway gain, and the
digest reports CI. **One judgment has no instrument: whether a generated plugin sounds like
what was asked for.** The oracle can prove a patch is not broken; it cannot tell you the
filter is musical, that the delay sits right in the mix, or that the fuzz is the fuzz the
prompt described.

That is the human's actual job here, and it is not approving diffs. Approval is the
cheapest thing the human contributes and the easiest to automate away — which §9 is the
long story of. Taste is the expensive thing. When the schedule forces a choice between a
read-through and a listening pass, the listening pass wins, and `YOUR MOVE` lines should be
written on that assumption.

---

## 2. The consult gate

Ask before acting when a change hits any of these four. Otherwise: act, then report.

**1. An irreversible write, named by the artifact it destroys.**
Force-push, history rewrite, deleting data or results, publishing anything, sending
anything to a third party, spending real money. And specifically: **overwriting
`bench/results/.prompt_baseline.json`**, or any committed results file — a measurement you
cannot reproduce is data loss.

> **Gate the write, never the run.** *(Revised 2026-07-28.)* This trigger names artifacts,
> not activities, because it was previously read as covering the benchmark itself.
> `bench/run_benchmark.py --provider groq` costs $0, is repeatable, and destroys nothing —
> `tools/check.sh quota` already draws that line, refusing to touch the baseline while
> running the benchmark freely. The protocol did not, and six evidence defects (PF-009
> through PF-014) sat unmeasured for four days behind a gate that existed to protect one
> file none of them wrote to.
>
> Generalized: **a consult gate belongs on the destructive step, never on the step that
> produces information.** If an act only creates knowledge, it is ungated no matter how
> expensive it looks. When an act both measures and overwrites, split it — run, report the
> number, then ask before storing it.

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

### What "landed" means

*Added 2026-07-28.* **Pushed and green — not committed.**

A commit is a local act. Between commit and green CI sit failures the local ladder cannot
report — sometimes because the environment differs, and sometimes, as here, because **the
ladder never ran the test at all**. This project's four-day red streak was a SIGILL in
`OfflineRenderTest`, a harness `tools/check.sh` has never built or executed; only CI does
(PF-027, PF-029). The first two attempts to diagnose it both misread a gdb post-mortem that
had trapped on an unrelated benign assertion.

So: a change is not landed, and its report is not final, until it is pushed and CI has
concluded green on it. If CI is red for a reason the session did not cause, say so in the
report rather than treating the change as clean. `/orient` now opens with the CI line, so
"I didn't know" has stopped being available.

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
Exactly three. Not a backlog. **One of the three is reserved for evidence** and
carries the literal tag `*(evidence)*` — an item that moves a claim out of
"Assumed, never checked". Two slots compete on urgency; the third cannot.

## Waiting on you
Only items genuinely blocked on the human. Empty is a good answer.
```

**The reserved evidence slot** *(added 2026-07-28, enforced by
`tests/test_control_wiring.py`)*. Every one of this project's 18 closed defects is a code
defect, most closed within one to three days of being filed. Every one of its six
*evidence* defects — PF-009 through PF-014, all filed 2026-07-23 — was still open five days
later, and those six are precisely the "Assumed, never checked" list. The project's one
metric was made entirely of the work that never won a slot, because a list ranked by
urgency will never schedule work whose defining property is that it is not urgent.

Two slots for what broke. One slot only a measurement can fill.

Rewriting rather than appending is deliberate: an append-only log accumulates until nobody
reads it, and stale entries are indistinguishable from current ones. Narrative history
lives in git.

**`docs/collaboration_log.md` is retired and, as of 2026-07-27, deleted.** It recorded
decisions made between 2026-05 and 2026-07-21 and was not written to after that. It was kept
on disk for a while after retirement, which is exactly how a stale document gets read as a
current one — so it now lives only in git (`git log -- docs/collaboration_log.md`). §9 explains
why the protocol it served was retired.

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

Registered in `.claude/settings.json`, all fail-closed. **This table is mechanically
checked** against `settings.json` by `tests/test_control_wiring.py` — see the rule below
it. Edit one, and the test tells you to edit the other.

| Hook | Enforces |
|---|---|
| `check_rt_safety.py` | No allocation, locking, or I/O inside the hand-enumerated closure of `processBlock` — `enterAudio`/`exitAudio`, `ParamPool::pushToFaust`, `FaustEngine::process`, `OutputGuard::process`. Body extracted by brace counting from the qualified signature, so neighbours that legitimately allocate off-thread are excluded. Fails closed on a stale read. |
| `check_prompt_invariants.py` | Four decidable properties of `llm/prompts/system_prompt.txt`: the ADR-009 duplicate-symbol sentence is present, the "process … exactly once" clause is present, the generated-stdlib-block markers are intact, and **every `ns.func` resolves against the installed `/usr/share/faust/*.lib`**. Skips (exit 0) when Faust is absent. |
| `check_bash_denylist.py` | Blocks the CLAUDE.md "Do not" commands, including staging across the whole tree. |

**Retired, and deliberately not on disk:** `check_adr009_prompt_sync.py` (it verified one
sentence while the two prompt files diverged substantially around it; the second prompt file
was later deleted outright) and `protect_human_owned.py` (it gated `llm/prompts/*` and
`docs/decisions.md` on authorship, which §1 no longer does). Both went in `cf1d8e8`. They
are named here only so a reader of the git history knows they were removed on purpose.

### A document that describes a mechanism is checked against it

*Added 2026-07-28, because this section was the counter-example.*

Until that date the table above still listed `check_adr009_prompt_sync.py` and
`protect_human_owned.py` — retired six days earlier, neither on disk — and omitted
`check_prompt_invariants.py`, which was registered and running. Two of three live rows
wrong, in the one section whose job is telling a reader what is actually enforced. That is
this project's signature defect — a claim outliving the thing it described — reproduced
inside the document that diagnoses it.

The prompt already lives under the right rule: it cannot name a Faust function that does
not resolve. Prose about mechanisms gets the same rule. **Every process document is either
(a) mechanically checked against the mechanism it describes, or (b) dated and read-only.**
A document that is neither will be wrong before anyone notices, and rot rate scales with
page count — see §8.

**One hook still needs strengthening** (tracked in `STATUS.md`): `check_rt_safety.py`'s
scoped set is the transitive closure of `processBlock` *enumerated by hand*, because a
brace counter cannot build a call graph. Adding `myHelper()` to an already-scoped function
does not add `myHelper` to the scope. That gap is exactly how PF-015 happened —
`pushToFaust` moved onto the audio thread with `efbb5a5` and went unscoped for weeks. The
hook's docstring says all of this; keep it saying so.

Note also what these hooks are *not*: `check_prompt_invariants.py` proves the names in the
prompt exist, never that a few-shot compiles or that a claim about Faust is true. Those run
in `tools/check.sh full` (`test_prompt_stdlib.py`, `test_prompt_claims.py`), not at edit
time. A check on a proxy creates confidence proportional to the invariant, not to the
proxy.

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

**Every row above is either checked or frozen** (§7). As of 2026-07-28 the repository holds
7,299 lines of Markdown under `docs/` against 4,339 lines of product code in
`host/Source` + `llm`, `docs/` is the highest-churn directory in the tree, and 56% of all
commits touch nothing but documentation or `.claude/`. That volume is not automatically
waste — this project's method is partly its product, and the bug registry, the ADR log and
the assumed-claims register are why defects stay findable. But rot rate scales with page
count, and §7 had already gone wrong within six days. So the cap is on *function*, not
length: a document that describes a mechanism must be mechanically checked against it, and
a document that records a point in time must carry a date and then stop changing. Anything
that is neither should be deleted rather than maintained.

CLAUDE.md's per-file status narrative migrated to `STATUS.md` on 2026-07-25 for exactly
this reason, and the narrative itself was deleted rather than kept in a stale state.

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
