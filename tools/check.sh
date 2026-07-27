#!/usr/bin/env bash
#
# The one entry point. Four cost-ordered levels, cheapest first.
#
# WHY THIS EXISTS
#   This project's recurring defect is not bad code — it is believing a control
#   runs when it does not. Hooks that had never fired, CI green but 17 commits
#   behind, 276 tests passing in under two seconds with nothing making them run.
#   Every check below already existed. None of them were connected to a moment.
#
#   So this file invents no new verification. It only wires up what is here, and
#   orders it by what it costs to run, so there is never a reason to skip a level.
#
# USAGE
#   tools/check.sh fast     ~2 s     before every commit
#   tools/check.sh full     ~2 min   before every push
#   tools/check.sh audio    ~1 min   after touching llm/ or the prompt   ($0)
#   tools/check.sh quota    minutes  only with explicit authorization    (quota)
#
#   Levels are cumulative: full runs fast, audio runs full.
#
# THE ONE NUMBER
#   tools/check.sh assumed   prints the size of STATUS.md's "Assumed, never
#   checked" list. Every iteration of work must move at least one claim out of
#   it. That number cannot be improved by writing documentation, which is the
#   entire point of using it.
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

if [[ -t 1 ]]; then R=$'\e[31m'; G=$'\e[32m'; Y=$'\e[33m'; B=$'\e[1m'; N=$'\e[0m'
else R=""; G=""; Y=""; B=""; N=""; fi

FAILED=()
step() { printf '\n%s── %s%s\n' "$B" "$1" "$N"; }
ok()   { printf '%s  PASS%s  %s\n' "$G" "$N" "$1"; }
bad()  { printf '%s  FAIL%s  %s\n' "$R" "$N" "$1"; FAILED+=("$1"); }
skip() { printf '%s  SKIP%s  %s (%s)\n' "$Y" "$N" "$1" "$2"; }

# run <label> <command...>
run() {
  local label="$1"; shift
  local out
  if out="$("$@" 2>&1)"; then ok "$label"
  else
    bad "$label"
    printf '%s\n' "$out" | tail -25 | sed 's/^/        /'
  fi
}

have() { command -v "$1" >/dev/null 2>&1; }

# --------------------------------------------------------------------- levels

level_fast() {
  step "fast — unit tests and control wiring"
  # test_control_wiring.py is the one that matters most here: it asserts the
  # hooks are still shaped correctly AND still exit 2 on their red cases. A
  # green suite with dead hooks is what this project already shipped once.
  run "pytest (unit, no network, no faust)" \
      python -m pytest tests/ -m "not integration" -q
}

level_full() {
  level_fast
  step "full — prompt grounding, build, sanitizers"

  if have faust; then
    run "stdlib block resolves against installed faust" \
        python tools/gen_stdlib_block.py --check
    run "every ns.func in the system prompt exists" \
        python tools/gen_stdlib_block.py --verify-prompt
    run "few-shot examples compile" \
        python -m pytest tests/test_prompt_stdlib.py -q
    # The completeness half (fixture <-> prompt) needs no compiler and already ran
    # in `fast`; this line is the half that does, named so the ladder shows it.
    run "the prompt's prose claims hold against the compiler" \
        python -m pytest tests/test_prompt_claims.py::TestClaimsHoldAgainstTheCompiler -q
  else
    skip "prompt grounding" "faust not on PATH"
  fi

  if have faust2sndfile; then
    run "render oracle self-test (known-answer patches)" \
        python bench/render_oracle.py --self-test
  else
    skip "render oracle self-test" "faust2sndfile not on PATH"
  fi

  if have cmake && [[ -d "${JUCE_PATH:-$HOME/JUCE}" ]]; then
    run "configure host" \
        cmake -S host -B host/build -G Ninja -DCMAKE_BUILD_TYPE=Debug \
              -DJUCE_PATH="${JUCE_PATH:-$HOME/JUCE}"
    run "build all four targets" \
        cmake --build host/build --target PluginForgeHost PluginForgeHost_Standalone \
              PluginForgeHost_VST3 ParamPoolTsanTest
    local tsan
    tsan="$(find host/build/ParamPoolTsanTest_artefacts -type f -name ParamPoolTsanTest 2>/dev/null | head -n1)"
    if [[ -n "$tsan" ]]; then
      # halt_on_error makes any data race a non-zero exit. Expected stderr noise
      # (JUCE Timer assertions, LeakedObjectDetector) is not failure — the exit
      # code is. See host/tests/ParamPoolConcurrencyTest.cpp's header.
      run "ParamPool under ThreadSanitizer" \
          env TSAN_OPTIONS=halt_on_error=1 timeout 300 "$tsan"
    else
      skip "ThreadSanitizer" "ParamPoolTsanTest binary not built"
    fi
  else
    skip "host build" "cmake or JUCE_PATH missing"
  fi
}

level_audio() {
  level_full
  step "audio — does the generated DSP actually produce usable sound? (\$0, no quota)"
  if ! have faust2sndfile; then
    skip "render oracle over corpus" "faust2sndfile not on PATH"
    return
  fi
  # Renders every compiling patch in the benchmark corpus offline and asserts no
  # NaN/Inf, no silence, no DC runaway, no runaway gain. This is the automatable
  # half of the P6 battery. The subjective half still needs ears.
  run "render oracle over benchmark corpus" python - <<'PY'
import json, sys
sys.path.insert(0, "bench")
import render_oracle as ro

recs = [r for r in json.load(open("bench/results/results.json")) if r["first_try_compiles"]]
bad, unsupported, passed = [], 0, 0
for r in recs:
    a = ro.analyse(r["code"])
    if not a["rendered"]:
        if a.get("unsupported"):
            unsupported += 1          # zero-input generators: cannot measure, not broken
        else:
            bad.append((r["prompt"][:60], a["error"][:80]))
        continue
    if a["measurement"]["ok"]:
        passed += 1
    else:
        bad.append((r["prompt"][:60], "; ".join(a["measurement"]["reasons"])))

print(f"{passed} passed, {len(bad)} failed, {unsupported} unsupported (0-input generators)")
for p, why in bad:
    print(f"  FAIL {p} -- {why}")
sys.exit(1 if bad else 0)
PY
}

level_quota() {
  # Deliberately NOT cumulative and deliberately hard to run by accident. Free-tier
  # quota is this project's binding constraint: gemini allows ~20 requests/DAY.
  if [[ "${1:-}" != "--i-authorize-spend" ]]; then
    printf '%s\n' \
      "" \
      "  ${B}quota${N} spends free-tier provider requests and will not run without consent." \
      "" \
      "    tools/check.sh quota --i-authorize-spend" \
      "" \
      "  Runs: python bench/run_benchmark.py --provider groq" \
      "  25 prompts, first-try compile rate, \$0 on the groq free tier." \
      "  Writes bench/results/results.json. Does NOT touch .prompt_baseline.json —" \
      "  overwriting the baseline is a separate, deliberate act." \
      ""
    return 2
  fi
  step "quota — benchmark re-run (spends free-tier requests)"
  run "25-prompt benchmark on groq" python bench/run_benchmark.py --provider groq
}

level_assumed() {
  # The project's tracked metric. Counts bullet items under STATUS.md's
  # "Assumed, never checked" heading.
  python - <<'PY'
import re, pathlib
txt = pathlib.Path("STATUS.md").read_text()
m = re.search(r"^##+\s*Assumed.*?$(.*?)(?=^##\s|\Z)", txt, re.S | re.M)
if not m:
    print("Assumed-claims section not found in STATUS.md"); raise SystemExit(1)
claims = re.findall(r"^\s*[-*]\s+\*\*", m.group(1), re.M)
print(f"Assumed, never checked: {len(claims)} claims")
for line in re.findall(r"^\s*[-*]\s+\*\*(.+?)\*\*", m.group(1), re.M):
    print(f"  - {line}")
PY
}

# ----------------------------------------------------------------------- main

usage() {
  # The header comment block is the help text — print it until the first line
  # that is not a comment, so the two can never drift apart.
  awk 'NR<3 {next} /^#/ {sub(/^# ?/,""); print; next} {exit}' "${BASH_SOURCE[0]}"
}

case "${1:-}" in
  fast)    level_fast ;;
  full)    level_full ;;
  audio)   level_audio ;;
  quota)   shift; level_quota "${1:-}"; exit $? ;;
  assumed) level_assumed; exit $? ;;
  -h|--help|help|"") usage; exit 0 ;;
  *) printf 'unknown level: %s\n\n' "$1"; usage; exit 64 ;;
esac

printf '\n'
if (( ${#FAILED[@]} )); then
  printf '%sFAILED%s (%d): %s\n' "$R" "$N" "${#FAILED[@]}" "${FAILED[*]}"
  exit 1
fi
printf '%sall green%s\n' "$G" "$N"
