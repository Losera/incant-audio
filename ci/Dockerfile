# CI/dev environment. Provides the faust system binary (used by llm/generate.py
# for validation) and the C++ toolchain for building the host plugin.
# This is NOT a compilation server — the production pipeline uses in-process
# libfaust JIT (FaustEngine.cpp). See ADR-010 in docs/decisions.md.
FROM archlinux:latest

RUN pacman -Syu --noconfirm && \
    pacman -S --noconfirm base-devel cmake ninja clang faust git python && \
    pacman -Sc --noconfirm

WORKDIR /build
