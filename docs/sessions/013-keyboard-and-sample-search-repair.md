# Session 013 handoff — 2026-08-13, sample search and keyboard-after-generation repair

**Objective.** User reported four grievances against the running plugin: sample search
fails on every query; generated plugins have no unique/professional design; the keyboard
is unplayable after generating a synth; generations should be exportable as standalone
VSTs. Asked for research first, fanned out across the codebase, then a plan distinguishing
confirmed bugs (fix now) from architecture programs (ADR + plan, not code this session).
Full plan derivation is in `.claude/plans/continue-development-of-pluginforge-gleaming-
hamster.md` — this handoff is a pointer to execution state, not a replacement.

**Branch.** `fix/sample-browser-and-keyboard`, off `main` at `5801358` (PR #8 merged at
session start). **Working tree: not clean** — see "Uncommitted state" below before doing
anything destructive.

**PR #8** (`fix/generation-reliability-phase0`, prior session's work) was merged before this
session's branch was cut, per the user's explicit go-ahead.

---

## 1. What shipped — 5 commits, all green

| Commit | What |
|---|---|
| `0694a32` | **Sample search fix (PF-054/055/056).** Three independent, fatal, live-confirmed bugs, zero prior tests. `SoundfetchClient` resolved a bare `"soundfetch"` name never on PATH on this machine; `juce::ChildProcess::start()` returns true even when `execvp()` fails, so the friendly error was dead code. Default `ChildProcess` stream flags merged soundfetch's own logging into stdout, corrupting JSON for the default provider. Fixed: `<python> -m soundfetch` resolution (mirrors `PromptPanel`'s existing pattern), stdout-only capture, exit-255 detection for the execvp-failure signature. New `SoundfetchClientTest`, red-then-green confirmed against the pre-fix implementation (git-stashed and rebuilt live), plus a live smoke test against the real `/home/losera/soundfetch` venv. Companion commit in that separate repo adds `__main__.py` so `python -m soundfetch` resolves at all. **Not fixed: PF-056**, Freesound returns HTTP 403 — needs a replacement key from freesound.org, outside this diff. |
| `410770b` | **PF-057.** `KeyboardPanel`'s constructor called `setPlayable(false)` against a `playable` member already reading `false` — an idempotent no-op, so the disabled/dimmed widget state never actually applied on construction. Scenario 20 extended with 6 widget-level assertions; confirmed red against the reproduced bug (temporarily reverted the ctor fix, rebuilt, ran), green against the fix. |
| `aac53ea` | New scenario 33, "generate a synth, then play a note" — the first `EditorSessionTest` scenario to drive a real (faked) generation AND the keyboard in one run. Its own comment is explicit about what it does and does NOT prove: checked directly (not assumed) that it would NOT have caught PF-057 on its own (a real transition through the 30Hz timer masks the ctor no-op), documented rather than left as an inflated claim. |
| `2b8d4e3` | **PF-059 (review stop).** `generate.py`'s voice-contract gate lowercased UI labels before the exact-case check `FaustEngine::extractVoiceControls` actually performs — `hslider("Freq", ...)` passed Python validation and was silently rejected by the host. New `voice_contract.py` reads `llm/voice_contract.json` directly (the same canonical source the C++ header is generated from) for an exact-case check. Red-then-green confirmed. |
| `addfd57` | **PF-058 (review stop).** Auto family resolution could route "a generative synth" to the mute `generator` family (kind instrument, zero voice contract by design) silently. Fixed data-driven: `generation_profiles.json` gains `overridden_by_synth_terms`/`synth_override_terms`, consumed identically by Python's `resolve()` and the generated C++ `resolveAuto()` (the live "Auto → X" preview text). New `GenerationProfilesAutoTest` — `EditorSessionTest` never builds with `-DPF_IS_SYNTH=1` and structurally cannot reach `resolveAuto`'s instrument branch. Confirmed red by restoring the pre-fix generated header from git history. |
| `d4c8329` | Amended ADR-022 (heuristic visual identity) and ADR-023 (export) with this session's research — documentation only, no code. See §3. |

**Verification performed:**
- Every fix above was confirmed red (against a temporarily reproduced pre-fix state)
  then green, per COLLABORATION.md's "a control counts once it has been seen failing" —
  not just asserted, actually run both ways this session.
- `tools/check.sh full` green, run three times across the session as work landed
  (most recently after the uncommitted QWERTY fix in §2, confirming the combined state).
- `python -m pytest tests/ -m "not integration"`: 600 passed, 2 skipped.
- Live smoke test of `python -m soundfetch archive search` against the real venv,
  separate from any stub: clean JSON on stdout, 52 lines of logging noise on stderr —
  confirms the fix's assumption about stream separation against production, not just
  a controlled test double.

**Not fixed, not this session's job:** PF-056 (Freesound 403 — needs a new key, external).

---

## 2. Uncommitted state — QWERTY-after-generation fix, implemented and verified, NOT committed

**Why uncommitted.** These five files had concurrent edits from elsewhere in the working
tree at the point this fix was ready — not this session's authorship, not reverted, left
alone per `AGENTS.md` §6 ("the human edits this repo at the same time, sharing one index").
Asked the user directly how to handle it; **they chose to leave it uncommitted** rather
than have it staged via precise hunk-splitting.

**What's implemented, in the working tree right now, not staged:**

- `host/Source/KeyboardPanel.h`/`.cpp`: new `focusForPlaying()` (grabs keyboard focus onto
  the piano; called from `onFaustCompileSuccess` for a successful instrument generation)
  and `focusForPlayingCallCountForTest()`.
- `host/Source/PluginEditor.cpp`: `keyStateChanged()` now suppresses forwarding while any
  `juce::TextEditor` holds focus. **The bug this fixes**: `juce::TextEditor::
  keyStateChanged` swallows key-DOWN transitions while focused but NOT key-UP
  (`juce_TextEditor.cpp:2189-2205`, `if (! isKeyDown) return false;` is its first line) —
  and `MidiKeyboardComponent::keyStateChanged` ignores its own `isKeyDown` parameter
  entirely, re-polling every mapped key's live state on every call
  (`juce_MidiKeyboardComponent.cpp:254-283`). So releasing any key while typing in the
  prompt box could fire a spurious note-on for a DIFFERENT letter the user was still
  physically holding (ordinary fast-typing rollover) — its own key-down had been
  swallowed, so `MidiKeyboardComponent` had no record of it, and the unrelated key-up's
  poll would see it "newly down."
- `onFaustCompileSuccess`'s callback also now calls `keyboardPanel.setPlayable(instrument)`
  synchronously, not waiting for the next 30Hz timer tick — `focusForPlaying()` needs the
  widget already enabled, since JUCE will not grant focus to a disabled component.
- `host/Source/PluginEditor.h`: new `static bool isTextEditorFocusTarget(juce::Component*)`
  — the suppression predicate, exposed as a pure static function specifically so a test
  can exercise the logic without a real window (this headless harness has no peer, so
  `grabKeyboardFocus()`/`getCurrentlyFocusedComponent()` cannot be exercised end-to-end;
  same category of limitation STATUS.md's Broken #1 already names for a physical
  keypress). Also corrected a wrong claim in the pre-existing comment above
  `keyStateChanged`'s declaration: it used to say the override was reached as a
  fallback when "nothing has explicit focus" — re-read against
  `juce_ComponentPeer.cpp:164-175`, that fallback is the PEER's top-level component, an
  ANCESTOR of the editor, and the walk only goes upward, so it never reaches this
  override in that case. Corrected in place, dated.
- `host/tests/EditorSessionTest.cpp`: new scenario 34, 11 checks. Confirmed red-then-green
  three separate ways: the predicate via inversion, the focus/timing checks via reverting
  the `onFaustCompileSuccess` addition. 280/280 checks green with everything restored.

**To resume:** `git status` on these five files, reconcile with whatever else is there,
then stage and commit (or ask for the fix to be re-derived cleanly against a settled
tree — it's a small, self-contained diff, easy to reproduce from this description if
re-deriving is cleaner than untangling).

---

## 3. ADR amendments (docs/decisions.md, commit `d4c8329`) — research only, no code

**ADR-022** (heuristic per-generation visual identity, Accepted 2026-08-05): its point 3
was authorized as ungated Tier-1 work and never built. This session found the data it
needs is already captured and thrown away — `FaustEngine`'s `ParamCapture` records full
Faust group nesting, and the `04_generator_grouped` fixture proves it (4 clean sections,
rendered as an undifferentiated flat grid). `ParamGridPanel::applyUiIr()`/
`layoutSectioned()` are complete and unreachable (zero callers); section headings compute
geometry and are discarded (`(void) heading;`) because no `paint()` override exists to
draw them. Amendment records four build tracks in dependency order: wire `applyUiIr` from
`ParamInfo::group`, add the missing `paint()`, render `Kind::Meter` (needs UI-direction
approval per `PLUGIN_HEALTH_PLAN.md` P1.10 — not granted by this amendment), then the
heuristic palette itself.

**ADR-023** (export, Proposed 2026-08-06): two findings. `tools/export_repo.py` cannot
compile, not merely "incomplete" — its `acceptsMidi()`/`getTailLengthSeconds()` render as
an undefined bare identifier compared against a string literal, and its CMake template
hardcodes the exact same `PLUGIN_CODE` as the shipping host target. Separately, and not
previously noticed by any doc: the `libfaust.so` the host already links exports
`generateAuxFilesFromString` (confirmed via `nm -D | c++filt`), enabling in-process AOT
C++ emission for a live patch — no subprocess, no new dependency, and critically, **no
libfaust needed in the exported project at all**, contrary to Phase 2a's original
JIT-in-the-export assumption. Amendment redesigns Phase 2a around this and states
explicit acceptance criteria (build/load/sound — none demonstrated for any version of
this feature yet), plus the real prerequisite: this project has never had a plugin in a
DAW (STATUS.md Broken #2), and validating an *exported* plugin needs that solved first.

Both amendments are plans, not commitments — next session should read them before
starting either track, not re-derive the research.

---

## 4. Grievance-by-grievance status

| Grievance | Status |
|---|---|
| Sample search fails on every query | **Fixed** (PF-054/055), committed. PF-056 (Freesound key) needs external action. |
| No unique/professional design | **Researched, planned.** ADR-022 amendment names 4 concrete tracks; none built yet. |
| Keyboard unplayable after generating a synth | **Fixed** (PF-057/058/059), committed, plus a fourth fix (QWERTY focus/symmetry) implemented and verified but **uncommitted** — see §2. |
| Export as standalone VSTs | **Researched, planned.** ADR-023 amendment names the AOT mechanism and acceptance criteria; none built yet. Real prerequisite: get the *shipping* plugin into a DAW first (Broken #2). |

---

## 5. Next recommended action

1. **Immediate**: reconcile and commit (or re-derive) the QWERTY fix in §2.
2. **When resumed**: ask the user which of ADR-022's four tracks or ADR-023's redesigned
   Phase 2a to build first — both are architecture-adjacent enough to warrant the user's
   sequencing call, not a unilateral pick. ADR-022 Track 1 (wiring `applyUiIr`) is the
   cheapest and has zero LLM/prompt cost; it's also a stated prerequisite for ADR-023's
   export work inheriting sectioned layout instead of needing a second renderer.
3. Get a Freesound API key replacement (PF-056) whenever convenient — code-side, this
   grievance is otherwise closed.
4. `docs/BUGS.md` has 6 new entries (PF-054 through PF-059); `STATUS.md` Broken #1 and #12
   were updated as dated addenda, not a full COLLABORATION.md §5 rewrite (the file was
   already stale relative to sessions 011/012 before this session started — a full
   reconciliation is its own task, not bundled into this one).
