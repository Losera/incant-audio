# This directory moved

The issue-#26 (faust-rs repair-loop A/B) reproduction package now lives at
**[`bench/repair_ab_repro/`](../repair_ab_repro/)**, renamed by PR #71 (`6fe6679`,
2026-09-09) for a more descriptive path.

If you followed a link from GitHub issue
[grame-cncm/faust-rs#26](https://github.com/grame-cncm/faust-rs/issues/26) — or from
`Losera/incant-audio`'s own issue #26 — that instructed you to run
`python3 bench/issue26/verify.py`: that file no longer exists at this path on the default
branch. Two options:

1. **Use the current path** (recommended, gets fixes since the original post):
   ```
   python3 bench/repair_ab_repro/verify.py
   ```
   or the Docker path:
   ```
   make -C bench/repair_ab_repro docker-verify
   ```
2. **Check out the exact commit the comment linked** (`704963a`), where
   `bench/issue26/verify.py` genuinely exists, if you need byte-for-byte reproduction of
   what was posted:
   ```
   git checkout 704963a -- bench/issue26/
   ```

See [`bench/repair_ab_repro/README.md`](../repair_ab_repro/README.md) for the full write-up,
including a 2026-09-24 correction to the originally-posted repair mechanism (the headline
A/B result is unchanged; the causal explanation for it was wrong — see that file's
"Why — corrected 2026-09-24" section).
