## Title
`compiler`: `--version` reports `0.8.0` for every commit since the tag, so a downstream pin cannot be verified

## Body

`faust-rs --version` self-reports `0.8.0` identically for the `0.8.0` tag (`47dfb3e8`,
2026-08-13) and for current `main` (`2199d069`, 2026-10-06 — 216+ commits later). The
workspace `Cargo.toml` package version was never bumped past `0.8.0` in that span.

Confirmed live today against two fresh builds:

```
$ cargo install --git https://github.com/grame-cncm/faust-rs --tag 0.8.0 --locked --root /tmp/tag compiler
$ cargo install --git https://github.com/grame-cncm/faust-rs --rev 2199d069 --locked --root /tmp/main compiler

$ /tmp/tag/bin/faust-rs --version
faust-rs 0.8.0

$ /tmp/main/bin/faust-rs --version
faust-rs 0.8.0

$ /tmp/tag/bin/faust-rs --help | grep -c dump-sig-dag
0
$ /tmp/main/bin/faust-rs --help | grep -c dump-sig-dag
4
```

So a `faust-rs --version | grep 0.8.0` guard — the natural way for a downstream project to
pin a build — cannot distinguish the tagged release from any later commit on `main` that
still carries `0.8.0` in `Cargo.toml`. We hit this directly: a binary we'd been measuring
against for weeks, self-reporting `0.8.0`, turned out (via the `--dump-sig-dag` tell) to be
some unpinned point on `main` well past the tag, and we can no longer recover which commit
it actually was.

Would it be reasonable to bump the workspace version on `main` after a tagged release (even
a `-dev` suffix), or document a different recommended pin mechanism (`--rev` hash
explicitly, a build-info string, something exposed via `--check --error-format json`'s
`compiler` block)? Happy to send a PR for whichever shape you'd prefer.
