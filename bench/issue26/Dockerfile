# bench/issue26/Dockerfile — a pinned, single-purpose environment for reproducing
# the issue-#26 repair-loop A/B (https://github.com/Losera/incant-audio/issues/26).
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
#   docker build -f bench/issue26/Dockerfile -t incant-issue26 .
#   # or just: make -C bench/issue26 docker
# VERIFY THE PUBLISHED NUMBERS (no model, no network, ~1s)
#   docker run --rm incant-issue26 verify
# REPLAY THE A/B WITH YOUR OWN MODEL
#   # host ollama:
#   docker run --rm --add-host host.docker.internal:host-gateway \
#     -v "$PWD/out:/out" incant-issue26 replay \
#     --backend ollama --endpoint http://host.docker.internal:11434 \
#     --model qwen2.5-coder:3b --arms A,B,C --out /out/run.json
#   # hosted endpoint:
#   docker run --rm -e GROQ_API_KEY -v "$PWD/out:/out" incant-issue26 replay \
#     --backend openai --endpoint https://api.groq.com/openai/v1 \
#     --model llama-3.3-70b-versatile --api-key-env GROQ_API_KEY \
#     --arms A,B --samples 5 --out /out/run.json

# Arch matches the machine the corpus was generated on; `faust` in the Arch repo
# is currently the pinned 2.85.9. The base image (archlinux:base-devel) is a
# rolling tag and is NOT digest-pinned — a digest-pinned old base running
# `pacman -Syu` against current mirrors is the classic partial-upgrade break, so
# it would make this image LESS reproducible, not more. Instead the two compiler
# versions are asserted below: if Arch moves `faust` past FAUST_VERSION the build
# fails loudly rather than silently swapping the accept/reject oracle. Override
# with `--build-arg FAUST_VERSION=...` if you deliberately want a different one.
#
# Platform: archlinux publishes no arm64 image. On Apple Silicon build with
# `--platform linux/amd64` (emulated — fine for `verify`, slow for a full replay).
FROM archlinux:base-devel

ARG FAUST_VERSION=2.85.9
ARG FAUST_RS_REF=0.8.0
ARG FAUST_RS_REPO=https://github.com/grame-cncm/faust-rs
# Provenance — the Makefile fills these from `git`; harmless if built by hand.
ARG SOURCE_COMMIT=unknown
ARG SOURCE_DIRTY=unknown

RUN pacman -Syu --noconfirm \
        faust rust cargo git \
        python python-scipy python-matplotlib \
    && pacman -Scc --noconfirm

# faust-rs: pure Rust, no LLVM / libfaust. Package `compiler` → binary `faust-rs`
# (crates/compiler/Cargo.toml at tag 0.8.0). Pinned to the tag the corpus used.
RUN cargo install --git "${FAUST_RS_REPO}" --tag "${FAUST_RS_REF}" --locked \
        --root /usr/local compiler

# Assert both compilers are the versions the published corpus was measured with.
# A mismatch here means the numbers in the README no longer describe this image,
# so fail the build rather than let the oracle drift silently.
RUN set -eu; \
    faust --version; faust-rs --version; \
    if ! faust --version | grep -qF "${FAUST_VERSION}"; then \
        echo "FATAL: faust is not ${FAUST_VERSION} (Arch moved ahead — see Dockerfile header)" >&2; exit 1; \
    fi; \
    if ! faust-rs --version | grep -qF "${FAUST_RS_REF}"; then \
        echo "FATAL: faust-rs is not ${FAUST_RS_REF}" >&2; exit 1; \
    fi

# The reproduction package + the shared modules it imports. Copied, not cloned,
# so the image is built from exactly the tree you build it in.
WORKDIR /work
COPY bench/issue26/            /work/bench/issue26/
COPY bench/repair_ab_core.py bench/frs_check.py bench/score_repair_ab.py bench/frs_rederive_issue26.py  /work/bench/
COPY bench/corpora/            /work/bench/corpora/
COPY bench/results/repair_ab/  /work/bench/results/repair_ab/
# frs_rederive_issue26.py reads this archived efficacy run (the 15 never-compiled cells).
COPY bench/results/efficacy/efficacy_ollama_20260828.json  /work/bench/results/efficacy/
COPY llm/error_classes.py      /work/llm/
COPY llm/prompts/system_prompt.txt  /work/llm/prompts/

# Provenance: which repo state this image was built from.
RUN printf 'source_commit=%s\nsource_dirty=%s\nfaust_rs_ref=%s\nbuilt=%s\n' \
      "${SOURCE_COMMIT}" "${SOURCE_DIRTY}" "${FAUST_RS_REF}" "$(date -u +%FT%TZ)" \
      > /work/bench/issue26/BUILD_PROVENANCE \
    && cat /work/bench/issue26/BUILD_PROVENANCE

ENV PLUGINFORGE_ROOT=/work
ENTRYPOINT ["/work/bench/issue26/docker-entrypoint.sh"]
CMD ["verify"]
