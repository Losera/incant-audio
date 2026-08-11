#!/usr/bin/env bash
# ── ui_iterate.sh ───────────────────────────────────────────────────────────
# The design loop for docs/ui_design_plan.md §3: change the layout, run this,
# look at the contact sheet, read the diff. One command.
#
#   tools/ui_iterate.sh              build, render, diff against the reference
#   tools/ui_iterate.sh --accept     adopt the current manifest as the reference
#   tools/ui_iterate.sh --no-build   render with whatever is already built
#   tools/ui_iterate.sh --open       open the contact sheet when it is written
#   tools/ui_iterate.sh --widths=700,900,1280
#                                    render each fixture at several window widths
#
# ENV. PF_BUILD_DIR overrides where the harness binary is looked for
# (default host/build; CI sets build-ci).
#
# WHAT IT IS FOR. §3's B (LLM layout hints) and C (deterministic auto-layout)
# both change what the parameter grid looks like, and before this there was no
# way to see that change except by launching the Standalone by hand and running
# tools/screenshot_ui.sh against a focused window. This renders the real editor
# headless, against a fixture matrix spanning the §2 taxonomy, and writes both a
# picture and a diffable record.
#
# WHY IT DOES NOT FAIL ON DRIFT. Layout drift is the POINT — you are iterating.
# A loop that goes red every time a control moves is a loop that gets commented
# out, and this project's signature defect is a control believed to run that does
# not. So drift prints a diff and exits 0. The exit code is reserved for the
# harness genuinely breaking: a fixture that will not compile, or a grid that
# never settles.
#
# DISPLAY. Component::createComponentSnapshot needs no compositor, but the JUCE
# message loop needs a display connection (host/CMakeLists.txt:335-338). This
# wraps in xvfb-run when neither DISPLAY nor WAYLAND_DISPLAY is set, which is
# also how it would run in CI.
set -uo pipefail
cd "$(dirname "$0")/.."

BUILD=1
ACCEPT=0
OPEN=0
GALLERY_ARGS=()
for arg in "$@"; do
  case "$arg" in
    --accept)   ACCEPT=1 ;;
    --no-build) BUILD=0 ;;
    --open)     OPEN=1 ;;
    # Passed through to the harness. --widths renders the same fixture at more
    # than one window width, which is the only way the responsive behaviour of a
    # two-column split is visible to anything but a human dragging the corner.
    --widths=*) GALLERY_ARGS+=("$arg") ;;
    -h|--help)  sed -n '2,34p' "$0"; exit 0 ;;
    *) echo "ui_iterate.sh: unknown option '$arg'" >&2; exit 2 ;;
  esac
done

OUT=artifacts/ui_gallery
REF=host/tests/ui_fixtures/reference_manifest.json

# Where the harness binary lives. Defaults to the tree tools/check.sh configures;
# CI configures into build-ci instead (.github/workflows/test.yml) and sets this,
# so both callers share one implementation of the render + diff rather than CI
# growing a second copy that drifts.
BUILD_DIR="${PF_BUILD_DIR:-host/build}"

# ── 1. Build ────────────────────────────────────────────────────────────────
if [[ $BUILD -eq 1 ]]; then
  echo "── building UiDesignGallery"
  # CMAKE_BUILD_TYPE is not optional here. Ninja is single-config, so with no
  # build type JUCE's products_folder expands $<CONFIG> to the empty string
  # (JUCEUtils.cmake:1908-1917) and the binary lands in <Target>_artefacts/
  # rather than <Target>_artefacts/Debug/. This script used to omit the flag AND
  # hardcode the Debug path, so it aborted "not built" immediately after a
  # successful build on any tree whose cache had not already been set to Debug
  # by tools/check.sh. Matches check.sh:92-94.
  cmake -S host -B "$BUILD_DIR" -G Ninja -DCMAKE_BUILD_TYPE=Debug >/dev/null 2>&1 || {
    echo "  cmake configure FAILED" >&2; exit 1; }
  if ! ninja -C "$BUILD_DIR" UiDesignGallery 2>&1 | tail -n 5; then
    echo "  build FAILED" >&2; exit 1
  fi
fi

# Locate rather than assume: every other consumer in this repo resolves harness
# binaries with a bare find (check.sh:123, :140-141, :151; test.yml:271, :333),
# because the <Config> path segment varies with generator and build type.
BIN="$(find "$BUILD_DIR" -type f -name UiDesignGallery 2>/dev/null | head -n1)"
[[ -n "$BIN" && -x "$BIN" ]] || {
  echo "ui_iterate.sh: UiDesignGallery not found under $BUILD_DIR (build it first)" >&2
  exit 1; }

# ── 2. Render ───────────────────────────────────────────────────────────────
# Leak detection stays ON. This used to run under ASAN_OPTIONS=detect_leaks=0,
# justified by a comment in host/CMakeLists.txt that described a setting the
# target did not contain — and the two comments pointed at each other in a
# circle. Measured 2026-07-31: five fixtures, leak detection on, exit 0, no
# LeakSanitizer report. A claim a run contradicts is worse than no claim, so
# both are gone. If LSan ever does fire on JUCE's static caches, add a
# __lsan_default_options hook in UiDesignGallery.cpp (it travels with the binary
# and survives direct invocation, which an env var here does not) and paste the
# failing output next to it.
echo "── rendering fixtures"

# ── Rotate the previous run aside, for the A/B the contact sheet renders ─────
# You cannot judge "sleeker" from one picture; only against the one before it.
# The reference manifest answers "did anything MOVE", which is a different and
# narrower question than "does this look better" -- and the second question is
# the one a design pass actually has to answer. Keeping the prior PNGs costs
# nothing (artifacts/ is gitignored) and is the single biggest reviewer-speed
# win available here.
if [[ -d "$OUT" ]] && compgen -G "$OUT/*.png" >/dev/null; then
  rm -rf "$OUT/prev"
  mkdir -p "$OUT/prev"
  cp "$OUT"/*.png "$OUT/prev/" 2>/dev/null || true
fi

RUNNER=()
if [[ -z "${DISPLAY:-}" && -z "${WAYLAND_DISPLAY:-}" ]]; then
  if command -v xvfb-run >/dev/null 2>&1; then
    RUNNER=(xvfb-run -a)
    echo "  (no display — running under xvfb-run)"
  else
    echo "  no DISPLAY/WAYLAND_DISPLAY and no xvfb-run; the message loop needs one" >&2
    exit 1
  fi
fi

"${RUNNER[@]}" "$BIN" ${GALLERY_ARGS+"${GALLERY_ARGS[@]}"}
RENDER_RC=$?

if [[ $RENDER_RC -ne 0 ]]; then
  echo
  echo "HARNESS FAILURE (exit $RENDER_RC) — a fixture did not compile or the grid"
  echo "never settled. This is the one thing that is an error here; fix it before"
  echo "reading anything below."
  exit $RENDER_RC
fi

# ── 3. Diff first, so the contact sheet can carry it ────────────────────────
# Ordering matters: the sheet takes the diff as an argument, so the diff has to
# exist before the sheet is written. The diff is captured to a file rather than
# recomputed, so the page and the terminal cannot disagree about what changed.
DIFF_TXT="$OUT/layout_diff.txt"
: > "$DIFF_TXT"
if [[ $ACCEPT -eq 0 && -f "$REF" ]]; then
  python3 tools/ui_layout_diff.py "$REF" "$OUT/manifest.json" > "$DIFF_TXT" 2>&1 || true
fi

# ── 4. Contact sheet ────────────────────────────────────────────────────────
echo
echo "── contact sheet"
python3 tools/ui_contact_sheet.py "$OUT/manifest.json" "$OUT/index.html" "$DIFF_TXT" || {
  echo "  contact sheet FAILED" >&2; exit 1; }
echo "  $OUT/index.html"

if [[ $OPEN -eq 1 ]]; then
  # Best-effort: never let a missing opener fail a render that succeeded.
  command -v xdg-open >/dev/null 2>&1 && xdg-open "$OUT/index.html" >/dev/null 2>&1 &
fi

# ── 5. Report the diff ──────────────────────────────────────────────────────
echo
if [[ $ACCEPT -eq 1 ]]; then
  cp "$OUT/manifest.json" "$REF"
  echo "── reference updated: $REF"
  echo "   Commit it in the same change as the layout edit, so the diff that"
  echo "   justified it is in the same commit as the new baseline."
  exit 0
fi

if [[ ! -f "$REF" ]]; then
  echo "── no reference yet"
  echo "   Run 'tools/ui_iterate.sh --accept' to pin the current layout as the"
  echo "   baseline. Until then there is nothing to drift from."
  exit 0
fi

echo "── layout diff vs $REF"
# Replayed from the captured file rather than recomputed, so the terminal and
# the contact sheet are guaranteed to be reporting the same run.
# ui_layout_diff.py prints its own "look at the contact sheet / --accept" advice
# on a non-empty diff, so nothing is added here.
if [[ -s "$DIFF_TXT" ]]; then
  cat "$DIFF_TXT"
else
  echo "   no change."
fi
exit 0
