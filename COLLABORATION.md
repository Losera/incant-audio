# PluginForge — Collaboration Protocol

This document governs how you (Claude Code) and the human build this project together.
CLAUDE.md answers "what is this project." This document answers "how do we build it."
Load both at the start of every non-trivial session.

---

## 1. Engagement Modes

Every code task belongs to one of three modes. Classify before starting.

---

### DELEGATE

You generate. The human reviews the diff. You are trusted to produce the right answer;
correctness is verifiable by compile output, test results, or a direct read.

**Applies to:**
- Mechanical refactors with no subtle behavioral change
- CMakeLists.txt edits, Ninja/CMake configuration, compiler flags
- Test scaffolding and pytest infrastructure (fixture setup, file creation, mock wiring)
- Documentation, markdown, and inline comments
- Diagnosing error messages and explaining compiler or linker output
- Dependency management, `.env` templates, CI configuration
- Anything where "did this work?" has an objective, tool-verifiable answer

**PluginForge examples:**
- Add `@pytest.mark.integration` to `tests/test_llm_output.py` so the live-API tests are
  skipped in CI without a key — mechanical marker addition, verifiable with `pytest --co`.
- Add a `gemini` provider flag to `bench/run_benchmark.py`'s argument parser — a bounded
  change, success confirmed by `--dry-run`.
- Write `tests/test_param_pool.py` scaffolding with a mocked `AudioProcessorValueTreeState`
  — test boilerplate, no subtle domain logic.

---

### PAIR

You produce a first draft as a research artifact. The human reads it, verifies it against
primary sources (headers, API docs, running code), and writes the version that gets committed.
The draft exists to show the shape of a solution, not to be committed verbatim.

**Applies to:**
- Domain-specific code the human must understand deeply for future debugging
- Code that touches load-bearing system properties but is not catastrophic if the first
  version requires revision
- Any new pattern introduced to the codebase for the first time
- Code the human will iterate on and own for months

**PluginForge examples:**
- `FaustEngine::compile()` with libfaust JIT: You draft the `createDSPFactoryFromString` /
  `createPolyDSPInstance` call sequence from docs; the human verifies the signature against
  `faust/dsp/llvm-dsp.h`, then writes the version that goes into the codebase. You are not
  the author of record.
- `ParamPool::pushToFaust()` parameter remapping: the slot-index-to-Faust-label mapping has
  enough subtle indexing behavior that the human must build the mental model themselves, or
  they will not be able to debug it when DAW automation lanes misbehave.
- The retry loop's error-context threading in `generate.py`: You draft the message
  concatenation; the human verifies the anthropic SDK multi-turn message structure and writes
  the final version.

---

### HUMAN-OWNED

You do not produce the deliverable. You may explain similar patterns elsewhere in the
codebase, point to relevant library documentation, or critique a draft the human writes.
The code that gets committed is written by the human.

**Applies to:**
- Real-time audio thread code: anything that runs in `processBlock` or is called from it
- Atomic synchronization patterns, including `std::atomic<llvm_dsp*>` DSP swaps
- Anything where confident-sounding wrong code causes hard-to-debug user-facing failures
  (silent audio, DAW crashes, parameter corruption, undefined behavior under concurrency)
- `llm/prompts/system_prompt.txt` and any file under `llm/prompts/` — these are product IP
  and generation quality is sensitive to exact wording
- Architectural decisions that foreclose options currently open in `docs/decisions.md`
- ADR entries in `docs/decisions.md` — you may draft proposed text; the human authors it

**PluginForge examples:**
- `FaustEngine::process()` on the audio thread: you do not write the buffer processing loop,
  the `std::atomic<llvm_dsp*>` load-acquire/store-release pattern, or the `processAudio()`
  call. You may explain how comparable JUCE/JIT engines structure this; the human writes it.
- `llm/prompts/system_prompt.txt`: adding the ADR-009 duplicate-symbol rule is a
  prompt-engineering decision about product behavior. You may suggest the rule's content; the
  human decides the exact wording and commits it.
- Any new ADR entry: you may draft proposed text for the human to review, but the human
  is the author. Do not write to `docs/decisions.md` directly.

---

## 2. The Pre-Task Protocol

For any non-trivial task (more than a few minutes of work), before writing any code:

1. **State the mode.** Name which mode applies and give one sentence of reasoning.
2. **State the scope.** Name which files change and describe the entry and exit points of
   the task.
3. **State assumptions.** List anything you're assuming that the human has not confirmed
   (e.g., "I'm assuming `libfaust.so` is at `/usr/lib`; correct if wrong").
4. **Wait for PAIR and HUMAN-OWNED tasks.** Do not proceed until the human confirms. For
   PAIR, wait for them to indicate they want the draft. For HUMAN-OWNED, wait for them to
   specify what kind of help they need: explanation, pointer to docs, critique of their draft.
5. **Proceed for DELEGATE, but state the mode.** The human can override before reviewing
   the diff.

Skip this protocol for trivial tasks: a typo fix, a single command, a one-line explanation.

---

## 3. The Learning-vs-Shipping Balance

**When the human is learning a domain** — first time touching libfaust's C API, first time
wiring a JUCE `AudioProcessorValueTreeState`, first time working with LLVM JIT — default
toward PAIR even when DELEGATE might technically be safe. Produce code that is illustrative:
named intermediate variables, explicit type annotations, short functions that expose each
step. Add `// SUBTLE:` comments where a reader would miss why something is structured the
way it is. Explain what you're doing and why — the goal is to build the mental model
alongside the artifact.

**When the human is in shipping mode** — implementing a known pattern in a new file, adding
a test for behavior already understood, fixing a mechanical CI issue — default to DELEGATE.
Produce concise, idiomatic code without tutorial overhead. Trust that the mental model
already exists.

**When the distinction is ambiguous, ask before starting:**

> "Are you trying to understand this domain or ship this feature?"

The answer changes the output format, the level of commentary, and often the mode.

---

## 4. Stop Conditions

Stop and ask the human rather than proceeding when any of the following is true:

**Threading or atomics.** A build error or required change touches threading,
`std::atomic`, memory ordering, or anything involving the JUCE audio callback. Do not
attempt a fix without a confirmed mental model of the invariant at stake.

**A HUMAN-OWNED file must change.** Name the file, describe what change is needed and why,
then wait.

**An API call requires guessing.** A library function is needed and you cannot locate its
signature in documentation or headers you have in context. Do not reason from plausible
argument names. State: "I need to call `[function]` but cannot confirm its signature. Check
`[header or doc location]`."

**A decision in `docs/decisions.md` would be foreclosed.** A proposed change would rule out
an option still open in an ADR. Name the ADR, describe the foreclosure, ask whether it is
intentional.

**The change is large.** More than 100 lines across more than 3 files in a single step.
Decompose the task and confirm scope before continuing.

**Tests are failing without a clear hypothesis.** If you cannot state in one sentence why
a test is failing — a hypothesis testable with one targeted change — stop and ask.

**The phrase "try things" is itself a stop condition.** If you catch yourself thinking
"I'll try X and see if it helps," that thought is the signal to stop and ask instead.

---

## 5. The Fail-Loud Principle

Confident-looking code with hidden uncertainty is worse than no code. When uncertainty
exists, make it visible with these markers. Treat them as actionable items the human will
review, not decoration.

**`// TODO: VERIFY: [claim]`** — You are not certain the statement is correct.
```cpp
// TODO: VERIFY: returns nullptr on compile failure, not throws
llvm_dsp_factory* factory = createDSPFactoryFromString("score", code, ...);
```

**`// TODO: VERIFY API: [function]`** — You are reasoning about a library API rather than
reading it from a header or documentation you have in context. Always include a pointer.
```cpp
// TODO: VERIFY API: check faust/dsp/llvm-dsp.h for createDSPFactoryFromString signature
// and confirm argc/argv are nullable when no optimization flags are needed.
```

**`// SUBTLE: [condition]`** — A pattern has a correctness condition a reader would miss.
Reserve this for real traps, not general explanation.
```cpp
// SUBTLE: store-release here pairs with load-acquire in process(); relaxed would be a
// data race on the DSP pointer across the audio thread boundary.
activeDsp.store(newDsp, std::memory_order_release);
```

---

## 6. The "What I Would Have Done Differently" Log

At the end of any non-trivial task, append one entry to `docs/collaboration_log.md`
(create the file if it does not exist). Each entry:

```
### YYYY-MM-DD — [Task name]
**Mode:** DELEGATE / PAIR / HUMAN-OWNED
**Task:** [One sentence describing what was asked]
**Would do differently:** [One thing, even if the task succeeded]
**Mode signal:** [Evidence the mode choice was right or wrong — e.g., "human accepted
diff without changes" or "human rewrote 60% of draft; should have been PAIR"]
```

Keep entries short. The log exists to refine this protocol, not to narrate work history.

---

## 7. What This Protocol Is Not

This protocol makes the human's judgment operational; it does not replace it.

You may propose deviations when you have good reason, but you must be explicit:
"I would normally classify this as PAIR because it touches libfaust, but the change is a
single well-documented option flag. Treating as DELEGATE — override if you disagree."

The protocol can be edited. If the human wants to change a rule, they edit COLLABORATION.md
directly. Follow the new rule immediately. The protocol has no inertia; it exists to serve
the project.

---

## 8. Where Information Lives

As the project grows, so does the number of places a fact could plausibly go —
CLAUDE.md, this file, the ADR log, the collaboration log, Claude's own persistent
memory, and now `.claude/skills/`, `.claude/agents/`, and `.claude/hooks/`. Without a
convention, facts drift out of sync (CLAUDE.md already went stale once — see the
2026-05-20 entry in `docs/collaboration_log.md` — and ADR-009's rule text has to be
kept in sync by hand across two prompt files today). Use this table before writing
a new fact down; if you're unsure which row applies, that uncertainty is itself a
sign to ask rather than guess.

| Location | Belongs there | Does NOT belong there |
|---|---|---|
| **CLAUDE.md** | Stack, current per-file implementation status, the "Do not" list, the file map, one-line pointers to key ADRs (not their rationale). | Decision rationale/trade-offs (→ `docs/decisions.md`), process rules (→ this file), session narrative (→ `docs/collaboration_log.md`). |
| **COLLABORATION.md** (this file) | Engagement-mode definitions, the pre-task protocol, stop conditions, fail-loud markers, the collaboration-log entry format, this table. | Project facts (→ CLAUDE.md), ADR content (→ `docs/decisions.md`), one-off task history (→ `docs/collaboration_log.md`). |
| **`docs/decisions.md`** (+ `docs/architectural_decisions/ADR-XXX-*.md` for longer ones) | Append-only ADR log: Status → Context → Decision → Reasons → Consequences. Status updated to "Superseded," never deleted. | Current implementation status (→ CLAUDE.md), process rules (→ this file), narrative of *how* a decision session went (→ `docs/collaboration_log.md`). |
| **`docs/collaboration_log.md`** | Short per-task entries: mode, task, what would be done differently, mode-signal evidence — narrative history only. | ADR content, durable facts, process rules. |
| **Claude's persistent memory** | Cross-session heuristics too granular or provisional to check in yet (a lesson from one session, not yet promoted). | Anything that has proven stable and applies to all future sessions — promote that into this file or CLAUDE.md and delete the memory entry, so there is only one source of truth. |
| **`.claude/skills/architecture-planning/`** | The *procedure* for routing a new architectural decision to the right place (this table's logic, operationalized) — not project facts. | Actual ADR text, hook code, or project status — those live in the rows above; the skill only routes to them. |
| **`.claude/hooks/`** | Mechanical, deterministic enforcement of an already-decided invariant (RT-safety, HUMAN-OWNED file protection, a CLAUDE.md "Do not" rule). | The decision itself or its rationale (→ `docs/decisions.md`) — a hook enforces a rule, it doesn't explain why the rule exists. |
| **`.claude/agents/`** | Repeatable, specialized procedures with narrow tool access (e.g. `invariant-hook-writer` turning a described invariant into a tested hook). | General-purpose project knowledge — an agent's system prompt should point back at CLAUDE.md/this file, not duplicate them. |

When a fact could arguably live in two rows, prefer the more durable, less
narrative one — e.g. "we decided to cap the retry loop at 3 attempts" is an ADR
fact (`docs/decisions.md`), not a collaboration-log entry, even though it was
*decided* during a logged session.
