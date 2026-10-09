# Upstream `grame-cncm/faust-rs` issue drafts — 2026-10-09

**Not filed by this session.** Drafts for Losera to review and post to
`grame-cncm/faust-rs`. Each repro was re-verified live, today, against both the `0.8.0` tag
and current `main` (`2199d069e462b34d960321da06d7b0ebbc08d0aa`, 2026-10-06) before drafting
— see `docs/records/efficacy-adversarial-review-2026-10-09.md` for how they were found
(the `frs_diag_coverage.py` sweep and the faust-rs provenance audit).

Delete this directory after posting, per the convention the issue-#26 correction drafts use.

- `issue1_version.md` — `--version` cannot distinguish the tag from any later commit
- `issue2_filter_order.md` — non-constant filter order misdiagnosed as a stack overflow
- `issue3_recursion_note.md` — a recursion-cycle diagnostic's cause note contradicts its headline

None of these are about the repair-loop study (PF-076) or the efficacy results — they're
independent, narrow, crate-scoped reports in the house style `grame-cncm/faust-rs`'s actual
external contributors (`shakfu`, `petersalomonsen`) use.
