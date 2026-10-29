# faust-rs delta audit — 2026-09-23

Point-in-time record. Not maintained after today — see COLLABORATION.md §8 ("a document that
records a point in time must carry a date and then stop changing").

**Why.** Session task: continue the efficacy study and adversarially review it, and check
whether upstream faust-rs (grame-cncm/faust-rs) has moved in a way that affects the issue-#26
repair-loop A/B (PF-076) or its repro package (ADR-034, `bench/repair_ab_repro/`). It has, and
the audit surfaced a live reproducibility defect independent of any content drift: **the
Dockerfile's version guard cannot tell the pinned tag from a later build of `main`, because
both self-report the identical version string.** That defect is demonstrated here with three
real binaries, not inferred from source alone. All numbers below are $0, no LLM calls, no
network beyond `cargo install --git` and the GitHub API.

## 1. The provenance defect, demonstrated on binaries

`bench/repair_ab_repro/Dockerfile:46-71` pins `FAUST_RS_REF=0.8.0`, builds via
`cargo install --git https://github.com/grame-cncm/faust-rs --tag 0.8.0 --locked`, and asserts
the result with `if ! faust-rs --version | grep -qF "${FAUST_RS_REF}"`.

Three binaries were built this session and compared directly (not via source grep):

| Build | Command | `--version` output | Has `--dump-sig-dag`? |
|---|---|---|---|
| **True tag** | `cargo install --git … --tag 0.8.0` | `faust-rs 0.8.0` | **No** |
| **Installed production binary** (`~/.local/bin/faust-rs`, built 2026-08-30 per its mtime, and the binary this project's repair-A/B numbers were actually measured against) | *(built previously, this session's own history has no record of its exact invocation — the source checkout that built it is gone, per its own session log)* | `faust-rs 0.8.0` | **Yes** |
| **Current `main`** | `cargo install --git … --rev d8bdcf8e` (upstream tip, 2026-09-22) | `faust-rs 0.8.0` | **Yes** |

`git log 0.8.0..main` on `grame-cncm/faust-rs` shows **216 commits**, the tag dated 2026-08-13.
`--dump-sig-dag` landed at `3822718e` (2026-08-16, three days after the tag) and
`--dump-sig-dag-prepared` at `f8b0e9cc` (2026-08-28). Upstream never bumped the Cargo package
version past `0.8.0` on `main` in that span (checked directly against the built binaries, not
assumed), so **`--version` cannot distinguish a genuine tag build from any build up to and
including today's tip.** The Dockerfile's guard would pass for all of them.

This means the *installed* binary — self-reporting `0.8.0` and carrying `--dump-sig-dag` — was
never actually the tag `0.8.0` the ADR-034 repro package and `bench/BUGS.md`'s published figures
claim. It is some later, unpinned point on `main`, at least 88 commits ahead of the tag
(`0e32f8b0` "Raise the release eval depth budget to 32,768 on a 512 MiB stack", dated 2026-08-30
— the same day the binary's mtime records — through to whatever `main` was at on 2026-08-30). The
exact commit cannot be recovered; the build's own source checkout is gone (a prior session's
`.bash_history` and a stale nvim swapfile are the only fossils, and neither pins a SHA).

**Disposition: fix the guard (Track 3c below).** This is not evidence the published PF-076 /
issue-#26 numbers are wrong — see §3 — it is evidence the package **cannot currently prove**
what compiler produced them, which is precisely what ADR-034 was written to let an external
reader (Stéphane Letz, GRAME) do for himself.

## 2. What moved upstream that could plausibly touch the study

`0.8.0...main` is 216 commits. Full enumeration was not the goal; these are the ones a domain
review flags as plausibly touching accept/reject or diagnostic content on the corpus in
question, each checked against the commit's actual diff, not just its message:

| Commit | Date | What | Relevance |
|---|---|---|---|
| `0e32f8b0` | 2026-08-30 | Eval depth budget 512→32,768 on a 512 MiB stack | Could change accept/reject on deeply-nested expressions; not observed to move on this corpus (§3) |
| `2d477161` | 2026-09-07 | Deep acyclic expr no longer aborts the process (issue #16) | Same class — a program that used to crash the process now gets a diagnostic |
| `c56d54a3` | (in-window) | Constant division by zero is a diagnostic, not a panic | Same class |
| `765e5a4c` | 2026-09-21 | `FRS-PROP-0005` added | New code in the registry — could in principle split what `FRS-PROP-0002` currently catches; checked directly (§3): the six `routing_arity` cells this study cites still map 1:1 to `FRS-PROP-0002` on all three builds |
| `cf417dc0` | 2026-09-18 | "Parser: a syntax error's repair suggestions in one order at every parse" | Real pre-fix nondeterminism, confirmed by reading the commit in full — but resolved as **not a confound**: this project's own `frs_check._clean_message` already strips the affected list before it reaches the model. See §4. |
| `35fd8a66`, `9dda64fc` | 2026-08-25/28 | Table clamp moved to signal level, `-ct` end-to-end | Changes what type-checks near table bounds |
| `766d4b64`…`ef859edd`, `1e88ef91` | 2026-08-14/17 | `faustprobe` (render / freqresp / sweep / determinism / `--error-format json`) | A second render/measurement instrument now exists upstream — worth knowing about for any future cross-check of `bench/render_oracle.py`, out of scope to adopt this session |
| `crates/compiler/src/cli/source_mode.rs` | throughout | Modified across the window | Checked (§3): `frs_check.py:26-29`'s "faust-rs does not embed source text in the JSON, `SourceTextPolicy` is code-only" limitation still holds on the `main` build — `render()` still has to splice the source line itself |

## 3. Re-derivation: does any of it move the published numbers?

**3a. Diagnostic-quality figures (`bench/frs_rederive.py`), all three builds, identical input**
(the 15 never-compiled cells from `efficacy_ollama_20260828.json`):

| | tag 0.8.0 | installed (Aug-30) | main (d8bdcf8e) |
|---|---|---|---|
| accept/reject agreement vs C++ | 15/15 | 15/15 | 15/15 |
| source location present | 15/15 | 15/15 | 15/15 |
| stable error code present | 15/15 | 15/15 | 15/15 |
| `help` remedy present | 10/15 | 10/15 | 10/15 |
| `routing_arity` → `FRS-PROP-0002` | 6/15, same 6 cells | 6/15, same 6 cells | 6/15, same 6 cells |

**Byte-identical output across 216 commits of upstream history, on this corpus.** The `main`
build's new `FRS-PROP-0005` code does not fire on any of these 15 programs; none of the
eval-depth or table-clamp changes move accept/reject here either.

**3b. Full repair corpus (513 raw rows, 192 screened distinct programs — `corpus_screen.py`,
matching `docs/BUGS.md`'s published 192 figure), C++ 2.85.9 vs each faust-rs build:**

| | tag 0.8.0 | installed (Aug-30) | main (d8bdcf8e) |
|---|---|---|---|
| accept/reject agreement vs C++ | 191/191 | 191/191 | 191/191 |
| stable error code present | 191/191 | 191/191 | 191/191 |

(1 of the 192 screened programs times out the C++ compiler itself at >20s — unrelated to
faust-rs, excluded from all three columns identically, so the comparison is apples-to-apples on
191.) **Full agreement, byte-for-byte, on the entire screened corpus, across all three builds.**
Reproducible: `bench/frs_build_compare.py label=/path/to/faust-rs …` (committed this session).

**Disposition:** on the corpus this study actually used, the 216-commit drift is a
**reproducibility defect, not a correctness defect** — the guard that should prove which
compiler ran cannot, but the compiler that actually ran (whatever unpinned point on `main` the
2026-08-30 build was) produced the same diagnostic content the tag would have, on both the
15-cell diagnostic-quality subset (§3a) and the full 192-program corpus (§3b). This is not a
proof that *no* upstream change anywhere in 216 commits could move a number on some other input;
it is what this specific, already-published corpus shows.

## 4. `cf417dc0`, read in full — resolved, not a confound

Read directly (`gh api repos/grame-cncm/faust-rs/commits/cf417dc0`), not inferred from the
commit title. The commit message is unambiguous about the defect it fixed: *"The numbered list a
syntax error prints (`1: Insert INT`, `2: Insert CUT`, ...) came in **another order at every run
of the same binary**. The cause is in lrpar 0.13.10: `cpctplus::simplify_repairs` deduplicates
the repair sequences through a `HashSet` and sorts them by length alone with `sort_unstable_by`
... The grammar declares no `%avoid_insert`, so length was the only criterion."* Its own test
suite proves the pre-fix instability directly: *"sixteen parses in one process give one text,
which fails on the old code."* So yes — before 2026-09-18, faust-rs's syntax-error
repair-suggestion ordering was genuinely nondeterministic, run to run, even holding the binary
fixed.

**But it never reached PF-076's arm-B feedback text.** `bench/frs_check.py:185-193`
(`_clean_message`) truncates a diagnostic's message at the first occurrence of `"Repair
sequences found"` / `"No repair sequences found"` and returns only what comes *before* it — the
`render()` docstring says so explicitly (`frs_check.py:215-216`: *"Drops: the box-expression
dumps and the LR-parser repair-sequence list"*). That drop happens in THIS project's own wrapper,
independent of faust-rs's version, and it existed before `cf417dc0` was ever written. The exact
list `cf417dc0` made deterministic is exactly the list this project already discards before the
model ever sees it.

**Disposition: not a confound for PF-076.** The commit is real, its fix is real, but it touches
content this project's harness structurally never sends to the repair model. No re-run, no
retraction needed on this specific question.

## 4a. The new guard, verified both ways — and one unrelated pre-existing defect found

The corrected Dockerfile guard (`bench/repair_ab_repro/Dockerfile`, Track 3c) was checked two
ways:

- **Directly against both real binaries**, outside Docker (the guard's own shell logic,
  extracted and run against each `faust-rs --help`): passes silently on the true tag build,
  fails loudly — `"FATAL: faust-rs has --dump-sig-dag -- this build is later than the pinned
  commit"` — on the `main` build. Both directions demonstrated, the same "seen passing and seen
  failing" standard this project already holds every other control to.
- **A full `docker build`**, attempted this session: it did not reach the new guard. Arch's
  rolling `faust` package has moved to **2.88.0**, past the image's *pre-existing, unrelated*
  `FAUST_VERSION=2.85.9` pin, so the Dockerfile's **already-existing** C++ version assertion
  fired first and failed the build exactly as its own comment says it should ("Arch moved ahead
  ... fail rather than let the oracle drift silently"). The `cargo install --git --rev
  47dfb3e89b589ccf0c626129d14034621a4f70c0` step immediately above it **did succeed** and
  installed exactly the pinned commit (confirmed in the build log: `` Installed package `compiler
  v0.8.0 (…?rev=47dfb3e89b589ccf0c626129d14034621a4f70c0#47dfb3e8)` ``).

**Disposition:** the Arch `faust` drift is a separate, pre-existing staleness issue in the image
(bumping `FAUST_VERSION` to 2.88.0 would itself need re-verifying every number in this record
against the newer C++ compiler — out of scope for this session, which is about faust-rs
provenance, not re-pinning the C++ half). Left as-is; noted here rather than fixed opportunistically
(AGENTS.md §12 scope control). The faust-rs guard itself is verified correct independent of it.

## 5. Disposition and what happens next

- **Track 3c (this session, separate commit):** re-pin the Dockerfile to a commit SHA, not a
  tag, and replace the version-string guard with a flag-presence probe
  (`--dump-sig-dag` existing ⇒ post-2026-08-16) that the tag genuinely cannot pass — the same
  "seen failing" discipline CLAUDE.md already applies to every other control in this repo.
- **Not done this session:** re-running the PF-076 model-in-the-loop repair A/B at the new pin —
  out of scope (no model calls this session), and per §4, not indicated by anything found.
- **The GRAME issue thread:** this record is drafted for that purpose but **not posted** —
  publishing to a public issue is COLLABORATION.md trigger 1 and needs the human's explicit go.
