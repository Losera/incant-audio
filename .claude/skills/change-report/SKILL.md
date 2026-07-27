---
name: change-report
description: Write the COLLABORATION.md §4 five-line change report for a change that just landed — CHANGED / WHY / VERIFIED / RISK / YOUR MOVE. Use immediately after landing any change, before moving to the next task. Trigger on "change report", "write the report", "I landed X", "/change-report", and after finishing any implementation work in this repo.
---

# Change report (COLLABORATION.md §4)

Every landed change gets this, in-session, immediately. It is not a summary of what you
did — it is what the human needs in order to decide whether to look closer.

```
CHANGED    <files> <+added/-removed>
WHY        <the defect or need, in one sentence — not the solution>
VERIFIED   <what was read (file:line), what was run, with results>
RISK       <what could still be wrong, or what this doesn't cover>
YOUR MOVE  <what the human should do, or "nothing">
```

## The two fields that get faked

**`RISK` is load-bearing.** §4: *"A report whose RISK line is empty or reads 'none' on a
Tier 2 change is a report that has not been thought about."* If you cannot name a way the
change could be wrong, you do not understand it well enough to have landed it. "Linear
map only, so 10 kHz sits at knob-centre" is a risk. "Minimal risk" is not.

**`VERIFIED` needs artifacts, not adjectives.** §3: *"'Verified' is a banned word without
a named artifact."* A file:line you actually read, a command you actually ran with its
result. "Looks correct", "should be fine", and "this is the standard pattern" are not
evidence and do not land a Tier 2 change.

**`YOUR MOVE` must be honest about cost.** "Read this 40-line diff", "listen to one
patch", and "nothing" are all valid answers. Manufacturing review work to seem
collaborative wastes the human's attention, which is the scarcest resource in this
project.

## Before writing it

1. **Get the real diff.** `git diff --stat -- <paths>` for the CHANGED line — do not
   estimate line counts from memory.
2. **Decide the tier** (§3). Tier 2 is anything on or synchronizing with the audio
   thread, any `std::atomic` or explicit memory ordering, APVTS↔Faust parameter mapping,
   a wire contract, or a generation prompt. Tier 2 owes all three of: a primary source
   cited by file:line, a test or runnable check, and an explicit statement of what was
   **not** verified.
3. **Check the §2 gate.** If the change turned out to touch something irreversible,
   architectural, a cross-component contract, or build/dependency/distribution, say so —
   ideally you asked first; if you did not, say that plainly rather than burying it.
4. **Prompt changes owe a baseline statement.** §3: editing a prompt requires either
   re-running the affected benchmark or stating the baseline is now stale. Leaving a
   stale baseline unmentioned is a Tier 2 violation.

## Worked example (from §4)

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

## Deviations

§2: when classification is genuinely ambiguous, make the call, do the work, and add one
sentence — *"I treated this as ungated because X — tell me if that's wrong."* Do not
stall a session on a classification question, and do not silently route around a rule you
think is wrong; §10 says argue with it in the report instead.
