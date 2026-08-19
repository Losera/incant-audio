# Change report — documentation purge and context protocol, 2026-08-19

```
CHANGED    27 files deleted (-4,160 lines: docs/records/doc-purge-2026-08-19.md has the
           full list + reasons), 36 files modified (+355/-100), 1 file added
           (docs/records/doc-purge-2026-08-19.md, 52 lines) + this report.
           Plus 2 test files (test_control_wiring.py +86, test_project_structure.py),
           2 hooks retargeted (check_rt_safety.py, check_doc_naming.py), CLAUDE.md,
           COLLABORATION.md, STATUS.md, docs/BUGS.md (PF-065 added), ADR-011, .gitignore.
           Untouched, as scoped: the pre-existing llm/providers.py +
           tests/test_providers_unit.py working-tree diff (unrelated free-provider fix).

WHY        85 tracked markdown files (~152k words) carried staleness and dead
           cross-references that were actively misdirecting agents, not just burning
           context: `/orient` was silently running a different project's rules for an
           unknown span of sessions, INTERFACE.md's every line citation had drifted,
           and ADR-011 claimed a defect was "Closed 2026-07-19" that was still live and
           just surfaced as a real REAPER bug report.

VERIFIED   `/orient` shadowing: confirmed live in this session's own skill roster
           (showed "orient: Soundfetch orientation" before the fix) —
           ~/.claude/skills/orient/SKILL.md had no `---` frontmatter block at all
           (`head -12 | cat -A`). Fixed by adding proper frontmatter naming it
           `soundfetch-orient`; new regression test
           tests/test_control_wiring.py::TestDigestReportsCI::test_orient_skill_declares_its_own_name
           asserts this project's own skill still declares `name: orient` — PASSED.
           REAPER bug: root-caused via a dedicated Explore agent to
           host/Source/PromptPanel.cpp:143-154 (upward search from the loaded .so, ≤10
           levels; `~/.vst3/PluginForge Host.vst3/...` confirmed on this machine to have
           no repo above it) and :306 (empty-path error message). Filed as PF-065 in
           docs/BUGS.md (registry row + detail section, matching
           tests/test_bugs_registry_integrity.py's parsing rules), ADR-011's "Closed" row
           corrected. NOT fixed — the resolution mechanism is a distribution-architecture
           decision, COLLABORATION.md §2 territory.
           Reference graph (what's load-bearing vs orphaned) built by a second Explore
           agent, cross-checked by hand for the 25 deletion candidates and 13 freeze
           candidates before acting — e.g. confirmed docs/architecture.md wrong four ways
           against current code before deleting it.
           Deletions: none of the 27 removed files had a functional (non-comment) code
           reference; verified via `grep -rn <basename>` per file before `git rm`.
           Mechanical: `tools/check.sh fast` — PASS. Full suite
           (`pytest tests/ -q -x --ignore=tests/test_generate_unit.py`, includes the
           C++/JUCE-building export-repo integration test) — exit code 0, all passed.
           Targeted: `pytest tests/test_control_wiring.py tests/test_project_structure.py
           tests/test_bugs_registry_integrity.py tests/test_providers_unit.py -q` — 252
           passed, 4 skipped (pre-existing, unrelated skips). The two widened checks
           (TestLiveDocsDoNotReferenceDeletedFiles) and the new orient-frontmatter test
           run individually and pass.
           `git status --short` after `.gitignore` change confirms `.worktrees/` and
           `bench/.worktrees/` (a full second PluginForge checkout + an unrelated repo,
           soundfetch, both un-ignored before this session) no longer appear untracked.

RISK       The widened dead-reference/retired-mode test
           (TestLiveDocsDoNotReferenceDeletedFiles) covers only 7 explicitly curated files
           (CLAUDE.md, COLLABORATION.md, STATUS.md, docs/BUGS.md, PLUGIN_HEALTH_PLAN.md,
           INTERFACE.md, README.md), deliberately excluding docs/decisions.md, the
           standalone ADR files, docs/sessions/*, and docs/research/* as append-only or
           point-in-time by design (COLLABORATION.md §8) — those still carry real dead
           pointers (docs/decisions_reconstructed.md, llm/prompts/system_faust.txt,
           docs/pair_draft_editor_llm_bridge.md in docs/decisions.md and ADR-009/ADR-011)
           that this session found but did not fix, on the reasoning that editing old
           append-only entries to satisfy a mechanical check would itself violate the
           append-only principle. If that reasoning is wrong, those five references are
           where to look. Separately: 13 "frozen" docs got a dated header but were not
           swept for every internal dead link, only the ones this session's own edits
           touched or the graph flagged as high-signal — a frozen doc is explicitly not
           promised current. The `bench/.worktrees/provider-resilience/` worktree (16
           modified tracked files on `feat/provider-resilience`, zero commits ahead of
           origin/main — i.e. real, uncommitted work) was left entirely alone per the
           plan; it is now merely `.gitignore`d, not evaluated or landed.

YOUR MOVE  Skim docs/records/doc-purge-2026-08-19.md (52 lines) — it's the one page that
           replaces the 27 deleted files' worth of judgment calls. If any deletion looks
           wrong, `git show <sha>:<path>` recovers it (SHAs are in the manifest) — nothing
           is unrecoverable. PF-065 (docs/BUGS.md) needs its actual architecture decision
           made in a separate session — bundle the script into the plugin, ship a
           config file the installer writes, or something else; this session
           deliberately stopped at filing it. `bench/.worktrees/provider-resilience/`
           still holds real uncommitted work with no upstream commits — worth a look
           independent of this report.
```
