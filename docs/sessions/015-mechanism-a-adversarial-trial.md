# Session 015 — Mechanism A's second trial: a genuine adversarial run

**Status: data point two, not a ratification.** STATUS.md's "Next three things" reserved
this as the evidence slot: session 006 (`docs/sessions/006-multi-agent-trial-results.md`)
ran four briefs and found zero under-declarations — favorable, but explicitly flagged as
insufficient, because "a trial where nothing under-declares cannot show whether the gate
would have caught an under-declaration." Session 006 §4 named the fix: *"a second trial,
ideally one where a brief pair with a plausible-looking-but-actually-coupled `touches`
disjointness is run in parallel for real (not sequenced around by hand)."* This session ran
that trial.

---

## 1. Trial design

Two briefs, each handed to a fresh, independent `general-purpose` subagent in its own
isolated `git worktree` — no shared context, no knowledge of the other brief's existence or
content. Both work areas were chosen because they hook into `PluginEditor.cpp`'s
`onFaustCompileSuccess` lambda (the single callback that runs after every successful
generation) via **existing call sites neither brief needed to touch**, in a region with **no
`CONTRACT.md`** — matching session 005 §1's own stated precondition for where this failure
mode lives ("an area with no `CONTRACT.md` yet is not parallel-safe by declaration alone").

**Brief P-CODEVIEW** (`host/Source/CodeEditorPanel.{h,cpp}` only): make the read-only Faust
code view keyboard-navigable immediately after a new generation, by having `showSource()` —
already called unconditionally from `onFaustCompileSuccess` — grab keyboard focus when new
source arrives.

**Brief P-KEYBOARD** (`host/Source/KeyboardPanel.{h,cpp}` only): show a one-shot onboarding
hint ("Press keys to play — QWERTY maps to piano") the first time the on-screen piano gains
keyboard focus after becoming playable, by overriding `focusGained()` on the component
`focusForPlaying()` — already called unconditionally from the same callback, for instruments
— grabs focus onto.

Neither brief was told about the other, about `PluginEditor.cpp`'s call order, or that this
was a coupling trial. Both were instructed to implement, build, and verify their own change
in isolation before reporting back.

---

## 2. Declared vs. actual

| Brief | Declared `touches` | Actual (`git diff --name-only`, each worktree) | Verdict |
|---|---|---|---|
| P-CODEVIEW | `CodeEditorPanel.h`, `CodeEditorPanel.cpp` | same two files | **Honest** — exact match |
| P-KEYBOARD | `KeyboardPanel.h`, `KeyboardPanel.cpp` | same two files | **Honest** — exact match |

`touches(P-CODEVIEW) ∩ touches(P-KEYBOARD) = ∅`, confirmed twice: by the declared/actual
match above, and mechanically — `git apply` of both diffs onto a fresh worktree off `main`
(`f9cc349`) produced **zero conflicts**. A raw Mechanism-A touches-disjointness check would
say this pair is parallel-safe, correctly, at the file level. Two for two honest, same as
session 006's four for four — under-declaration is still not the failure mode either trial
has produced. That was never the target of this trial; see §5.

---

## 3. The coupling, mechanically verified

Both agents built and "verified" their own change in isolation — both builds succeeded, and
both explicitly noted (independently, unprompted) that neither could observe real focus
behavior: `grabKeyboardFocus()` is a documented no-op without a real desktop peer
(`host/tests/EditorSessionTest.cpp` scenario 34's own comment, predating this session,
already states this for the whole harness). Both agents' own verification is therefore
silent on the one property their features actually depend on.

**What neither brief's author could see:** `PluginEditor.cpp:208` calls
`codeEditorPanel.showSource(...)` — now, after P-CODEVIEW, a focus grab —
**before** `PluginEditor.cpp:233`'s `keyboardPanel.focusForPlaying()` — now, after
P-KEYBOARD, itself a focus grab plus a side effect. Both calls already existed;
neither brief added a new call site to `PluginEditor.cpp`, which is exactly why neither
would have had cause to read it. This file was already the seam session 006 found for the
*first* trial (`PluginEditor.h`'s construction-order case); this is a second, independent
coupling through the same uncontracted file, discovered by two agents who never touched it
at all.

**Mechanical verification, not reasoning.** The existing `EditorSessionTest.cpp` harness
cannot observe this — no scenario in it gives the editor a real desktop peer. This session
added one new scenario (`scenarioMechanismATrial_focusOwnershipAfterMerge`, appended after
scenario 40) that does: `editor.addToDesktop(0); editor.setVisible(true);` against this
machine's real Hyprland session (`DISPLAY=:1`, `WAYLAND_DISPLAY=wayland-1` — a real, not
virtual, display; CI's `xvfb` would not have helped, since the harness never calls
`addToDesktop()` anywhere else either). Two small test-only forwarders were added to
`PluginEditor.h`/`KeyboardPanel.h` by the orchestrator (not either brief) to read real
focus state. Result, running the full 41-scenario suite (`f9cc349` + both diffs applied,
zero manual reconciliation):

```
[INFO] real desktop peer acquired: yes
[RESULT] after onFaustCompileSuccess settles: codeView focus=false, keyboard focus=true, onboarding hint visible=true
PASS (314 checks, 0 failures)
```

`focusForPlaying()` runs *after* `showSource()` in the untouched, existing call order, so it
wins the race. The keyboard correctly ends up with final focus, and P-KEYBOARD's onboarding
hint correctly fires. **P-CODEVIEW's entire stated feature — "keyboard-navigable
immediately after a new generation... without an extra click" — is silently, completely
inert for every instrument generation**, the exact case its own brief's worked example would
most likely be exercised against by a human tester. Not a crash, not a compile error, not a
test failure in either brief's own build, not something either author had a `touches` or
`depends` declaration that could have named — it is invisible to Mechanism A's file-level
check by construction, because the coupling runs *through* a third file neither brief
touches.

---

## 4. A second, independent finding: a real leak, invisible to both individual builds

The first run of the new scenario did not print a result at all — the process aborted
partway through the suite with `LeakSanitizer: detected memory leaks`, tracing to
`KeyboardPanel::handleKeyboardFocusGained()`'s `juce::Timer::callAfterDelay(2000, ...)`
(`juce_Timer.cpp:395`). The one-shot hint's delayed hide callback, and the `WeakReference`/
lambda closure it captures, were still live when the process exited — because the harness's
`main()` returns almost immediately after this session's new scenario, well under the 2s
delay, and nothing cancels the pending timer. `ASAN_OPTIONS=detect_leaks=0` was used to get
past this **for this session's mechanical read of the focus result only** — the leak itself
is not fixed and is not a false positive: LeakSanitizer traced a genuine, specific
allocation stack, not a suppressed/known pattern.

**This required BOTH the real peer and P-KEYBOARD's code to manifest, and neither
individual agent's own build+verify step could have found it**, for the same underlying
reason as §3: neither ever exercised a real desktop peer. It is a P-KEYBOARD-only defect
(P-CODEVIEW's presence is incidental to it), but it is a second, independent instance of
this trial's actual finding: **this project's automated verification path — for either a
single honestly-scoped brief or two merged ones — cannot observe real focus/peer-dependent
behavior at all**, which is a gap bigger than Mechanism A itself. Not filed as a numbered
`PF-` bug: this is throwaway trial code in a merge worktree, never landed, never intended to
land as-is (see §6).

---

## 5. What this demonstrates and what it does not

**Demonstrates:**
- Mechanism A's `touches`-disjointness check is, again, provably correct at the file level —
  it said "safe," and it *is* true that neither file conflicts. The check does exactly what
  it claims to do.
- That "safe" verdict does **not** mean the merged behavior matches either brief's stated
  intent. A real, reproducible, mechanically-confirmed coupling exists, running through a
  third file neither brief's `touches` set could have named, in an area with no
  `CONTRACT.md` — precisely session 005 §1's predicted failure shape, now observed for
  real rather than reasoned about by hand (session 006's own stated limitation, closed
  here).
- The coupling was found only because the orchestrator (not either brief, not the existing
  test suite) ran the *merged* result under conditions — a real desktop peer — neither
  individual verification pass reached. This is the second time in this project a real
  environment gap (headless harness, no peer) hid a defect until someone deliberately
  worked around it (the first being `dsp_poly`/pluginval's PF-062 finding this same
  session, a different mechanism, same shape: a build/test path that "passed" was actually
  never exercising the real condition).

**Does not demonstrate:**
- That an under-declared `touches` set would be caught or missed — neither this trial nor
  session 006's produced a genuine under-declaration; both authors, honestly narrowly
  scoping their own file pair, happened to be exactly right about which files they'd write
  to. Two for two, now six for six across both trials. That remains untested.
- That a `provides`/`depends` hook, had one existed, would have caught this. No such hook
  exists; this coupling was found by the orchestrator manually recognizing the shared
  `onFaustCompileSuccess` seam from context (both briefs mention "called from the shell
  after a successful compile" in their own doc comments) and deliberately designing a
  verification scenario to check it — the same "a human read it and reasoned from first
  principles" shape session 006 §3 found for its own refusal case, not a mechanically
  triggered catch.
- A general answer to "how often does this happen." N=1 coupling found in N=1 deliberately
  designed adversarial pair, chosen by the orchestrator specifically because both briefs
  hooked into the same existing callback. A brief pair with no shared callback/seam would
  likely show nothing, as session 006's original four did.

---

## 6. Recommendation

**Still not "build the hook yet," but for a different reason than session 006's.** The
`touches`-disjointness half continues to check out on every real trial run so far (6/6
honest declarations across two sessions) — nothing here argues against eventually
hook-enforcing it, per session 005's original verdict. What this trial adds is sharper
evidence for the **`provides`/`depends` half's known limit**, already named in session 005
§1: *"this only catches under-declaration against a contract that is already written
down... nothing for a coupling nobody has documented yet."* `onFaustCompileSuccess`'s call
order is exactly that undocumented coupling, now demonstrated twice (session 006's
construction-order case, this session's focus-ownership case) in two independent trials,
by two different mechanisms (object lifetime vs. UI focus), both through files neither
paired brief touched.

**Concrete, bounded next step, if this project wants a third trial:** write
`host/Source/PluginEditor.h`'s `onFaustCompileSuccess` call sequence into a `CONTRACT.md`
entry (it does not have one; `host/Source/CONTRACT.md` covers `processBlock`/`runCompile`/
thread split only) naming the ordering invariant explicitly, then re-run a similar
adversarial pair and see whether a `provides`/`depends` check against that *newly written*
contract would have caught it. That is the concrete test of session 005 §1's own claimed
mitigation — untested by either trial so far, because in both cases the relevant contract
did not exist yet.

**Also worth a separate, smaller follow-up, unrelated to Mechanism A:** the real-peer gap
itself (`grabKeyboardFocus()`, and evidently `Timer`-driven focus side effects, are
unverifiable by every current automated harness) is a standing blind spot independent of
multi-agent coordination — it would hide the same class of bug in an ordinary single-session
change touching focus behavior, no parallel briefs required.

---

## Change report (COLLABORATION.md §4)

```
CHANGED    docs/sessions/015-mechanism-a-adversarial-trial.md (new, this file only).
           No code landed on main: both briefs' implementations and the
           orchestrator's verification scenario exist only in throwaway
           worktrees (.claude/worktrees/agent-a0fb7ea6daad139ca,
           agent-aefae0f3ac4cd072a, mechanism-a-trial-merge), removed after
           this write-up per this session's own scope (a trial, not a
           feature landing).
WHY        STATUS.md's reserved evidence slot: session 006 named the exact
           next trial needed (real coupling, real parallel agents,
           mechanically checked, not reasoned about by hand) before any
           Mechanism A hook gets built. This session ran it.
VERIFIED   Two independent subagents, isolated worktrees, no shared context,
           each briefed on only their own feature. Declared vs. actual
           touches sets confirmed via git diff --name-only in each worktree.
           File-level disjointness confirmed twice: matching declarations,
           and a conflict-free `git apply` of both diffs onto a fresh main
           worktree. The coupling itself confirmed by a new EditorSessionTest
           scenario giving the editor a genuine desktop peer under this
           machine's real Hyprland session (not simulated, not CI's xvfb,
           which would not have helped -- the harness never calls
           addToDesktop() anywhere else) -- full 41-scenario suite: PASS,
           314 checks, 0 failures, including the two new assertions this
           session added. The independent leak finding traced to a specific
           allocation stack via LeakSanitizer (juce_Timer.cpp:395), not
           inferred.
RISK       N=1 designed coupling case, chosen by the orchestrator because
           both briefs happened to hook into the same existing callback --
           not a randomly sampled pair, so this cannot speak to how common
           this failure mode is across arbitrary brief pairs. The
           LeakSanitizer finding is real but was not investigated further
           or filed as a numbered PF- bug: it lives only in a now-removed
           worktree's diff, described here for the record. Neither trial to
           date (this one or session 006's) has produced a genuine
           under-declared touches set, so that half of Mechanism A remains
           exactly as untested as session 006 left it.
YOUR MOVE  Decide whether the concrete next step in §6 (write the
           onFaustCompileSuccess contract, re-run a third adversarial trial
           against it) is worth doing before any Mechanism A hook gets
           built, or whether two trials showing the same
           provides/depends-without-a-contract gap is sufficient evidence to
           leave the touches-only hook build unblocked on it (session 005's
           original split verdict) while treating provides/depends as
           advisory, exactly as scoped there.
```
