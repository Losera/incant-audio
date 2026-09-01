# ADR-034 (DRAFT — not accepted) — a single-purpose reproduction container for issue #26

**Status:** DRAFT. Proposed by Claude 2026-09-01, pending human decision.
Drafting is ungated (COLLABORATION.md §2 trigger 2); the *decision* to adopt is
not. If accepted, move this into `docs/decisions.md` as ADR-034 and delete this
file. If rejected, delete `bench/issue26/Dockerfile` + `docker-entrypoint.sh` and
the `docker*` targets in `bench/issue26/Makefile`; nothing else depends on them.

## Context

Stéphane Letz (GRAME) asked, on Losera/incant-audio#26, to see the corpus +
harness behind our result that faust-rs diagnostics lower the repair-loop
success rate. He is an external collaborator and will want to re-run it, likely
against a different model.

The scripts and data are already committed. The reproduction barrier is the
**two compilers**: Faust (C++) for arm A and the compile gate, and faust-rs
(Rust) for arms B/C. Building both from scratch on an arbitrary machine is the
part most likely to stop someone.

CLAUDE.md / AGENTS.md §12 are explicit that Docker is not introduced without a
demonstrated need, and COLLABORATION.md §2 trigger 4 gates "distribution". This
ADR is the consult.

## Decision

Add **one** Dockerfile, `bench/issue26/Dockerfile`, that builds a pinned
environment for reproducing issue #26 and nothing else:

- `archlinux` base (the machine the corpus was built on; `faust` in the Arch
  repo is currently the pinned 2.85.9), `faust-rs` built from the `0.8.0` tag,
  `python` + `scipy` + `matplotlib`, and the `bench/issue26/` package.
- The **LLM is not in the image.** Models are multi-GB; the harness points at a
  host ollama or any OpenAI-compatible endpoint at run time.
- Entrypoint dispatches `verify` / `rederive` / `replay` / `score` / `shell`.

**It is not wired into anything.** Not `tools/check.sh`, not CI, not
`host/CMakeLists.txt`, not the product build. No repo test, hook, or skill may
depend on it (same rule ADR-031 put on Obsidian). The project remains
un-containerised.

## Alternatives considered

1. **Documentation only — point at the committed files + a `requirements.txt`.**
   Cheapest. Still leaves the re-runner to build Faust and faust-rs. Kept as the
   primary path anyway (`verify.py` needs only scipy; the README step 1–3 don't
   need the container). The container is an *additional* on-ramp, not the only
   one.
2. **A GitHub Release tarball with a build script.** Same build barrier, plus a
   second artifact to keep in sync with the repo. A release of the *pointer*
   (README + commit pin) is still worth doing; a release that bundles a build
   script is strictly worse than a Dockerfile.
3. **Bake a small model into the image.** Rejected: even a 3B is ~2 GB, it fixes
   the model choice (defeating the point of an external re-run), and it drags in
   an ollama runtime.
4. **Nix flake instead of Docker.** Cleaner pinning, but Stéphane's audience is
   more likely to have Docker than Nix, and the repo has no Nix precedent
   either — same §12 cost, smaller reach.

## Consequences

- First container in the repo. A reader who finds it must not conclude the
  project containerises — the Dockerfile header and this ADR both say so
  explicitly.
- Maintenance surface: the pinned versions (`faust` via Arch, `faust-rs 0.8.0`)
  will drift. Mitigation: `verify.py` runs from committed data and does not need
  the container at all, so a broken image never blocks reproducing the numbers —
  only the `replay` convenience.
- Rollback is a file delete (see Status above). No schema, no build, no CI
  change.
- If a second reproduction container is ever proposed, that is the trigger to
  reconsider whether the project should have a `docker/` story at all — this ADR
  is deliberately scoped to exactly one.

## Adversarial critique

- *"You're adding infra for one external person."* — True, and if it were only
  convenience it wouldn't clear §12. The load-bearing reason is that the result
  is **published on a public issue** and a claim that cannot be independently
  re-run is weak; lowering the re-run cost for the compiler-toolchain half is
  proportionate to that.
- *"Arch base is not reproducible — `pacman -Syu` floats."* — Correct; the image
  is pinned only as tightly as Arch's current `faust`. For a hard pin we'd build
  Faust from source at a tag, which roughly triples image build time and size
  for a front-end-only use. If Stéphane needs bit-exact Faust, the source-build
  path is a follow-up, not v1.
- *"`cargo install --git` at build time can break."* — Yes. The `FAUST_RS_REF`
  build arg lets a re-runner pin a commit; `verify.py` (the load-bearing path)
  doesn't touch faust-rs.
