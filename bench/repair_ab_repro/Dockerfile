# bench/repair_ab_repro/Dockerfile — a pinned, single-purpose environment for reproducing
# the faust-rs repair-loop A/B (https://github.com/Losera/incant-audio/issues/26).
#
# WHY THIS EXISTS  (see docs/decisions.md — ADR-034)
#   The one hard part of reproducing this result is building the two compilers:
#   Faust (C++, for arm A + the compile gate) and faust-rs (Rust, for arms B/C).
#   This image builds both, pinned to the versions the published corpus used,
#   plus the analysis deps. It is NOT part of PluginForge's build or CI, the
#   project is not containerised, and nothing in the repo depends on it.
#
#   The LLM is deliberately NOT in the image (models are multi-GB). Point the
#   harness at a host ollama or any OpenAI-compatible endpoint at run time — see
#   docker-entrypoint.sh and the README.
#
# BUILD  (context = repo root, so the shared modules are in scope)
#   docker build -f bench/repair_ab_repro/Dockerfile -t incant-repair-ab .
#   # or just: make -C bench/repair_ab_repro docker
# VERIFY THE PUBLISHED NUMBERS (no model, no network, ~1s)
#   docker run --rm incant-repair-ab verify
# REPLAY THE A/B WITH YOUR OWN MODEL
#   # host ollama:
#   docker run --rm --add-host host.docker.internal:host-gateway \
#     -v "$PWD/out:/out" incant-repair-ab replay \
#     --backend ollama --endpoint http://host.docker.internal:11434 \
#     --model qwen2.5-coder:3b --arms A,B,C --out /out/run.json
#   # hosted endpoint:
#   docker run --rm -e GROQ_API_KEY -v "$PWD/out:/out" incant-repair-ab replay \
#     --backend openai --endpoint https://api.groq.com/openai/v1 \
#     --model llama-3.3-70b-versatile --api-key-env GROQ_API_KEY \
#     --arms A,B --samples 5 --out /out/run.json

# Arch matches the machine the corpus was generated on; `faust` at the pinned
# snapshot date below is 2.85.9. This image used to run `pacman -Syu` against
# whatever Arch's rolling repos currently hold, plus a post-hoc version
# assertion — the intent being "fail loudly if Arch moved `faust` past
# FAUST_VERSION". 2026-09-24: Arch's rolling `extra` repo moved to faust
# 2.88.0 (published 2026-09-10), and the assertion did exactly what it was
# built to do — failed the build — except that "fail the build" is not a
# usable outcome for an external reader trying to reproduce a result; it just
# means `make docker-verify` has been dead for every such reader since Arch
# moved. A version assertion after an unpinned install can only ever detect
# drift, never prevent it.
#
# Fixed by pinning the PACKAGE REPOSITORY, not just checking a version string
# after the fact: `FAUST_ARCHIVE_DATE` points pacman at a fixed
# archive.archlinux.org snapshot (2026-07-05, four days after faust 2.85.9 was
# published 2026-07-01 and over two months before 2.88.0 landed 2026-09-10 —
# https://archive.archlinux.org/packages/f/faust/), so dependency resolution
# runs against one frozen, internally-consistent repo state instead of a
# single downloaded package file against whatever the rolling base image
# happens to have. `pacman -Syu` against a PINNED mirror is the standard Arch
# archive-reproducibility pattern (unlike `-Syu` against a live mirror, which
# is the classic partial-upgrade break this repo's own machine-versions
# section in CLAUDE.md already warns about). The base image
# (archlinux:base-devel) is still a rolling tag and is NOT digest-pinned —
# only the package repo used to populate it is pinned — but every package
# installed, not only `faust`, now comes from the same snapshot, which is a
# stronger guarantee than pinning `faust` alone. The version assertion below
# is kept as a second check (belt-and-suspenders: if the archive snapshot
# itself is ever repointed, this still catches it), not as the primary
# reproducibility mechanism. Override `FAUST_ARCHIVE_DATE`/`FAUST_VERSION`
# together with `--build-arg` if you deliberately want a different pin.
#
# Platform: archlinux publishes no arm64 image. On Apple Silicon build with
# `--platform linux/amd64` (emulated — fine for `verify`, slow for a full replay).
FROM archlinux:base-devel

# The Arch Linux Archive snapshot date pacman resolves every package against
# (see the header comment above). Confirmed reachable and holding
# faust-2.85.9-1-x86_64.pkg.tar.zst this session:
# https://archive.archlinux.org/repos/2026/07/05/extra/os/x86_64/
ARG FAUST_ARCHIVE_DATE=2026/07/05
ARG FAUST_VERSION=2.85.9
# A COMMIT, not a tag. 2026-09-23 audit (docs/records/faust-rs-delta-2026-09-23.md):
# `cargo install --git --tag 0.8.0` and a build of `main` 216 commits later both
# self-report `faust-rs 0.8.0` — the Cargo package version was never bumped past
# 0.8.0 on `main` in that span, so `--version` cannot tell a genuine tag build
# from any later one. This is not hypothetical: the binary this project actually
# measured the published corpus with (built 2026-08-30, its source checkout now
# gone) ALSO reports `0.8.0` and, checked directly, carries `--dump-sig-dag` — a
# flag that does not exist at the tag (landed 3822718e, three days after the
# tag). That build was therefore never actually the pinned tag, and the OLD
# guard below would have passed it anyway. `--rev` pins an exact commit;
# `47dfb3e89b` is the tag 0.8.0's own commit, re-verified this session to
# reproduce the published diagnostic-quality figures bit-for-bit
# (docs/records/faust-rs-delta-2026-09-23.md §3) — the one commit this image can
# now defensibly claim, since the actual historical build's SHA cannot be
# recovered.
ARG FAUST_RS_REF=47dfb3e89b589ccf0c626129d14034621a4f70c0
ARG FAUST_RS_REPO=https://github.com/grame-cncm/faust-rs
# Provenance — the Makefile fills these from `git`; harmless if built by hand.
ARG SOURCE_COMMIT=unknown
ARG SOURCE_DIRTY=unknown

# Point pacman at the frozen archive snapshot BEFORE any install, so every
# package (not just faust) resolves against one consistent, dated repo state.
# `Server =` lines replace the base image's rolling mirrorlist entirely.
RUN set -eu; \
    for repo in core extra multilib; do \
        echo "Server = https://archive.archlinux.org/repos/${FAUST_ARCHIVE_DATE}/\$repo/os/\$arch"; \
    done > /etc/pacman.d/mirrorlist \
    && pacman -Syu --noconfirm \
        faust rust cargo git \
        python python-scipy python-matplotlib \
    && pacman -Scc --noconfirm

# faust-rs: pure Rust, no LLVM / libfaust. Package `compiler` → binary `faust-rs`
# (crates/compiler/Cargo.toml at this commit). `--rev`, not `--tag` — see the
# ARG comment above for why a tag alone cannot prove what got built.
RUN cargo install --git "${FAUST_RS_REPO}" --rev "${FAUST_RS_REF}" --locked \
        --root /usr/local compiler

# Assert both compilers are the versions the published corpus was measured with.
# For faust this is now a SECOND check behind the archive-snapshot pin above —
# it should always pass, and existing to catch the case where the pin itself is
# ever repointed to a different date. A mismatch here means the numbers in the
# README no longer describe this image, so fail the build rather than let the
# oracle drift silently.
#
# faust-rs is checked by FLAG PRESENCE, not `--version` — the whole point of
# this audit is that the version string does not move when the binary does.
# `--dump-sig-dag` landed at 3822718e (2026-08-16), three days after the
# 47dfb3e8 commit this image pins; a build that is genuinely at-or-before that
# commit must NOT have the flag. Demonstrated failing both ways this session
# (docs/records/faust-rs-delta-2026-09-23.md): the true tag build lacks it, a
# `main`-tip build has it — so this check can actually catch the drift the old
# `--version` grep could not.
RUN set -eu; \
    faust --version; faust-rs --version; \
    if ! faust --version | grep -qF "${FAUST_VERSION}"; then \
        echo "FATAL: faust is not ${FAUST_VERSION} (Arch moved ahead — see Dockerfile header)" >&2; exit 1; \
    fi; \
    if faust-rs --help 2>&1 | grep -q -- "--dump-sig-dag"; then \
        echo "FATAL: faust-rs has --dump-sig-dag, which does not exist at" >&2; \
        echo "       ${FAUST_RS_REF} (the pinned commit) -- this build is" >&2; \
        echo "       from a later point in faust-rs history than requested." >&2; \
        exit 1; \
    fi

# The reproduction package + the shared modules it imports. Copied, not cloned,
# so the image is built from exactly the tree you build it in. Everything here
# is openly licensed — see /LICENSE exceptions (a) + (b). The proprietary
# llm/prompts/ tree and the efficacy archive are NOT copied; the harness uses
# the vendored bench/repair_ab_repro/system_prompt.txt and frs_rederive_cells.json.
WORKDIR /work
COPY bench/repair_ab_repro/            /work/bench/repair_ab_repro/
COPY bench/repair_ab_core.py bench/corpus_screen.py bench/frs_check.py \
     bench/score_repair_ab.py bench/frs_rederive.py bench/fidelity_gate.py  /work/bench/
COPY bench/corpora/            /work/bench/corpora/
COPY bench/results/repair_ab/  /work/bench/results/repair_ab/
COPY llm/error_classes.py      /work/llm/

# Provenance: which repo state this image was built from.
RUN printf 'source_commit=%s\nsource_dirty=%s\nfaust_rs_ref=%s\nbuilt=%s\n' \
      "${SOURCE_COMMIT}" "${SOURCE_DIRTY}" "${FAUST_RS_REF}" "$(date -u +%FT%TZ)" \
      > /work/bench/repair_ab_repro/BUILD_PROVENANCE \
    && cat /work/bench/repair_ab_repro/BUILD_PROVENANCE

ENV PLUGINFORGE_ROOT=/work
ENTRYPOINT ["/work/bench/repair_ab_repro/docker-entrypoint.sh"]
CMD ["verify"]
