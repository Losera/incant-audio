# Architectural Decisions

**The living ADR log is `docs/decisions.md`** — 22 ADRs (ADR-001 through ADR-027, plus
amendments), append-only, never deleted, superseded in place. That is where a current status
lives; do not trust a status recorded here.

This directory holds four early decisions as standalone files, from before the log at
`docs/decisions.md` consolidated: [ADR-007](ADR-007-faust-vs-cmajor.md) (Faust vs Cmajor),
[ADR-008](ADR-008-claude-vs-gemini.md) (Claude vs Gemini provider, superseded by ADR-012's
free-only policy), [ADR-009](ADR-009-faust-duplicate-symbol-prompt-fix.md) (Faust duplicate-
symbol prompt fix), and [ADR-011](ADR-011-ipc-argv-subprocess.md) (editor↔LLM IPC — its
"Locating `generate.py`" hardening row was reopened 2026-08-19, PF-065). Every ADR numbered
after these, including amendments to ADR-009 and ADR-011 themselves, lives only in
`docs/decisions.md`.
