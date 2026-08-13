#!/usr/bin/env python3
"""
Generate the STDLIB REFERENCE block for llm/prompts/system_prompt.txt from the
*installed* Faust standard library.

Why this exists
---------------
On 2026-07-21 both prompt files were found to teach functions that do not exist:
`ef.ping_pong`, `ef.chorus`, and `ef.flanger`. Two of the four few-shot examples in
the production prompt did not compile. This directly produced the two failure modes
that had been recorded as "persistent model failures" for two months:

  * the flanger prompt failed with `undefined symbol : flanger_mono` -- the model
    recalled the CORRECT name (phaflangers.lib really does export flanger_mono) and
    used the wrong namespace, because the prompt taught a fictitious `ef.flanger`.
  * the ping-pong prompt failed with a circular `with{}` definition -- told that
    `ef.ping_pong` was a one-liner that does not exist, the model wrote its own.

A hand-copied mirror of an external library's API drifts from that library silently.
This script removes the possibility: the NAME of every function in the block is
curated by a human below, but its SIGNATURE is read out of the installed .lib file.
An entry that does not resolve is a hard error, not a silent omission.

Division of responsibility
--------------------------
  CURATED (editorial, human judgement)  -- which functions are worth showing the
      model, how they are grouped, and the one-line description of each.
  MECHANICAL (this script)              -- the namespace prefix, the exact argument
      list, and the guarantee that the function exists.

Usage
-----
    python tools/gen_stdlib_block.py            # print the block to stdout
    python tools/gen_stdlib_block.py --check    # exit 1 if any entry is missing
    python tools/gen_stdlib_block.py --write    # splice into the system prompt

`--check` is what CI runs (tests/test_prompt_stdlib.py); it is how a Faust upgrade
that renames or drops a function turns into a failing build instead of a silent
regression in generation quality.
"""
import argparse
import re
import subprocess
import sys
from pathlib import Path

FAUST_LIB_DIR = Path("/usr/share/faust")
STDFAUST = FAUST_LIB_DIR / "stdfaust.lib"

PROMPT_DIR = Path(__file__).resolve().parent.parent / "llm" / "prompts"

# The prompt this operates on when --prompt is not given. Kept so every existing
# invocation -- check.sh, the hook's advice text, the tests -- behaves exactly as
# it did; --prompt is purely additive, for the second prompt file Phase 1 adds.
PROMPT_FILE = PROMPT_DIR / "system_prompt.txt"


def prompt_files() -> list[Path]:
    """Every prompt the guards should cover, sorted for stable output."""
    return sorted(PROMPT_DIR.glob("*.txt"))

BEGIN_MARKER = "# BEGIN GENERATED STDLIB REFERENCE"
END_MARKER = "# END GENERATED STDLIB REFERENCE"

# ── Curated selection ─────────────────────────────────────────────────────────
# (namespace, function, description). Names verified present 2026-07-21; the
# script re-verifies on every run. Descriptions are editorial and are the only
# free text here -- a wrong description misleads, a wrong signature does not
# compile, which is why only the latter is mechanically enforced.
#
# TRIMMED 2026-07-31 to buy prompt headroom for the `~` arity rule. The prompt had
# 36 characters before tests/test_prompt_headroom.py's drift assertion and 407
# before the real groq TPM guard, so the rule could not be written without paying
# for it first. Thirteen entries were removed on evidence, not taste: they appear
# in ZERO of ~134,000 characters of generated code across every archive in
# bench/results/ and bench/results/efficacy/ (22 entries met that test) AND each
# had a sibling still listed that demonstrates the same call shape --
#   ve.korg35LPF, ve.diodeLadder      (ve.moogLadder keeps the normalised-vs-Hz contrast)
#   de.sdelay                          (de.delay, de.fdelay)
#   pf.flanger_stereo, pf.phaser2_stereo  (the _mono siblings)
#   ef.softclipQuadratic, ef.wavefold  (ef.cubicnl)
#   re.mono_freeverb, re.zita_rev1_stereo (re.stereo_freeverb)
#   ef.stereo_width                    (trivially hand-written as M/S)
#   os.lf_saw                          (os.lf_triangle)
#   no.pink_noise                      (no.noise)
#   en.asr                             (en.adsr is a superset)
# Unused-in-corpus is NOT sufficient on its own: ve.moog_vcf, ef.transpose and
# ba.midikey2hz were kept despite never appearing, because nothing else here shows
# their shape and an absent entry invites an invented one. ef.echo was kept because
# the prose rule at system_prompt.txt:55 names it.
# Re-check the evidence before trimming further; do not extend this list by feel.
CURATED: list[tuple[str, list[tuple[str, str, str]]]] = [
    ("Filters", [
        ("fi", "lowpass",   "Butterworth low-pass; order is a constant"),
        ("fi", "highpass",  "Butterworth high-pass; order is a constant"),
        ("fi", "bandpass",  "Butterworth band-pass"),
        ("fi", "resonlp",   "resonant low-pass -- the workhorse for 'filter with resonance'"),
        ("fi", "resonhp",   "resonant high-pass"),
        ("fi", "notchw",    "notch filter, width in Hz"),
        ("fi", "peak_eq",   "peaking EQ band, gain in dB"),
        ("fi", "lowshelf",  "low shelf, gain in dB"),
        ("fi", "highshelf", "high shelf, gain in dB"),
        ("fi", "dcblocker", "DC blocker; no arguments"),
    ]),
    ("Virtual-analog filters", [
        # PF-032: the generated warm low-pass wrote `cutoff : *(1.0/ma.SR)` and
        # rendered silent at rms 2.5e-08. moog_vcf takes Hz (vaeffects.lib:71) and
        # res is normalised (:69); the sibling three take normalised frequency
        # (:184,:326,:391). The distinction is the whole trap, so it is stated.
        ("ve", "moog_vcf",   "Moog ladder VCF; fr in Hz, res normalised 0-1"),
        ("ve", "moogLadder", "Moog ladder; normFreq is 0-1, NOT Hz"),
    ]),
    ("Delays", [
        ("de", "delay",  "integer-sample delay; first arg is the MAXIMUM delay"),
        ("de", "fdelay", "fractional-sample delay (interpolated) -- use for modulated delays"),
    ]),
    ("Modulation effects", [
        ("pf", "flanger_mono",   "mono flanger -- NOTE the pf. namespace, not ef."),
        ("pf", "phaser2_mono",   "mono phaser"),
        ("pf", "vibrato2_mono",  "vibrato"),
    ]),
    ("Dynamics", [
        # PF-032: the generated noise gate pre-converted thresh with ba.db2linear
        # and rendered EXACTLY 0.0. thresh is dB (misceffects.lib:129,164;
        # compressors.lib:140) and the library converts internally at
        # misceffects.lib:188 -- so a pre-conversion is applied twice.
        ("co", "compressor_mono",         "mono compressor; thresh in dB, pass it raw"),
        ("co", "compressor_stereo",       "stereo compressor, linked; thresh in dB, raw"),
        ("co", "limiter_1176_R4_mono",    "1176-style limiter, mono"),
        ("co", "limiter_1176_R4_stereo",  "1176-style limiter, stereo"),
        ("ef", "gate_mono",               "noise gate, mono; thresh in dB, pass it raw"),
        ("ef", "gate_stereo",             "noise gate, stereo; thresh in dB, pass it raw"),
        ("an", "amp_follower_ar",         "envelope follower with attack/release"),
    ]),
    ("Distortion / saturation", [
        ("ef", "cubicnl",            "cubic nonlinearity -- soft saturation / drive"),
    ]),
    ("Reverb", [
        ("re", "stereo_freeverb",   "Freeverb, stereo"),
    ]),
    ("Echo / utility effects", [
        ("ef", "echo",         "feedback echo; first arg is the MAXIMUM duration"),
        ("ef", "dryWetMixer",  "wraps an effect with a dry/wet control"),
        ("ef", "transpose",    "pitch shifter (windowed)"),
    ]),
    ("Oscillators and noise", [
        ("os", "osc",       "sine oscillator"),
        ("os", "sawtooth",  "band-limited sawtooth"),
        ("os", "square",    "band-limited square"),
        ("os", "triangle",  "band-limited triangle"),
        ("os", "lf_triangle", "low-frequency triangle -- use for LFOs"),
        ("no", "noise",     "white noise; no arguments"),
    ]),
    ("Envelopes", [
        ("en", "ar",   "attack/release envelope"),
        ("en", "adsr", "full ADSR envelope"),
    ]),
    ("Conversions and helpers", [
        ("ba", "db2linear",  "decibels to linear gain"),
        ("ba", "linear2db",  "linear gain to decibels"),
        ("ba", "midikey2hz", "MIDI note number to Hz"),
        ("ba", "sec2samp",   "seconds to samples"),
        ("ba", "bypass1",    "bypass switch around a mono effect"),
        ("ba", "bypass2",    "bypass switch around a stereo effect"),
        ("si", "smoo",       "one-pole smoothing -- use on slider values to avoid zipper noise"),
        ("ma", "SR",         "current sample rate; no arguments"),
        ("ma", "PI",         "pi; no arguments"),
    ]),
]


# ── Instrument selection ──────────────────────────────────────────────────────
# A SEPARATE, SMALLER set for llm/prompts/instrument_prompt.txt.
#
# Not a superset of the effects list and not a subset of it. Reverb, ping-pong,
# and the modulation family (flanger/phaser/vibrato) stay out -- an instrument
# request that also wants those routes to EFFECT anyway (router.py's scored
# classify() is a single binary decision; verified there is no second
# generation stage that separately wires an effect chain onto an instrument's
# output -- an EARLIER version of this comment claimed one existed under
# "ADR-020's process + effect wire format", which does not appear anywhere
# else in this repo and was wrong; corrected 2026-08-13). Leaving the
# modulation family out is still what makes room for the voice contract
# without spending headroom the shared prompt does not have (~300 tokens on
# the effects file).
#
# de.delay/de.fdelay ARE included (added 2026-08-13, see "Delays" below) --
# an instrument's OWN voice can legitimately want one (a plucked/karplus
# voice with a slap-back or feedback delay is a real, single-stage instrument
# request, not an effect-chain request), and this prompt previously taught
# NOTHING about the bounded-delay rule at all while router.py could still
# route a delay-mentioning prompt here. Confirmed this is not what caused the
# 2026-08-13 live delay-range failures (both reproduced prompts routed to
# EFFECT, which already had the rule -- see llm/error_classes.py's
# RETRY_HINT for the fix that actually addressed those), but the gap was
# real and worth closing regardless of which specific prompt exposed it.
#
# What it must carry that the effects list does not:
#   - the full oscillator family, as SOURCES rather than as LFOs;
#   - both envelopes, with en.ar's one-shot behaviour called out at the point of
#     use, because envelopes.lib:85-86 releases when the envelope reaches 1 and
#     ignores the gate -- a model reaching for it as a sustaining envelope gets a
#     patch whose note-off does nothing;
#   - ba.midikey2hz, which is how a "/key"-spelled voice converts to Hz.
CURATED_INSTRUMENT: list[tuple[str, list[tuple[str, str, str]]]] = [
    ("Oscillators and noise -- the SOUND SOURCE of an instrument", [
        ("os", "osc",       "sine oscillator"),
        ("os", "sawtooth",  "band-limited sawtooth -- the subtractive workhorse"),
        ("os", "square",    "band-limited square"),
        ("os", "triangle",  "band-limited triangle"),
        ("os", "lf_triangle", "low-frequency triangle -- for LFOs, NOT as a source"),
        ("no", "noise",     "white noise; no arguments"),
    ]),
    ("Envelopes -- read the note on en.ar before using it", [
        ("en", "adsr", "full ADSR. SUSTAINS while gate is 1. Times in SECONDS"),
        ("en", "ar",   "attack/release ONE-SHOT: releases on its own, IGNORES gate"),
    ]),
    ("Filters -- shaping the source", [
        ("fi", "resonlp",   "resonant low-pass -- the workhorse for 'filter with resonance'"),
        ("fi", "resonhp",   "resonant high-pass"),
        ("fi", "lowpass",   "Butterworth low-pass; order is a constant"),
        ("fi", "highpass",  "Butterworth high-pass; order is a constant"),
    ]),
    ("Virtual-analog filters -- note which argument is Hz", [
        ("ve", "moog_vcf",   "Moog ladder VCF; fr in Hz, res normalised 0-1"),
        ("ve", "moogLadder", "Moog ladder; normFreq is 0-1, NOT Hz"),
    ]),
    ("Delays -- only if the VOICE itself wants one (slap-back, feedback pluck)", [
        ("de", "delay",  "integer-sample delay; first arg is the MAXIMUM delay"),
        ("de", "fdelay", "fractional-sample delay (interpolated) -- use for modulated delays"),
    ]),
    ("Conversions and helpers", [
        ("ba", "midikey2hz", "MIDI note number to Hz -- use when the control is 'key'"),
        ("ba", "db2linear",  "decibels to linear gain"),
        ("si", "smoo",       "one-pole smoothing -- use on slider values, NOT on gate"),
        ("ma", "SR",         "current sample rate; no arguments"),
        ("ma", "PI",         "pi; no arguments"),
    ]),
]

# Which curated set a prompt is generated from. Effects is the default so every
# existing invocation is unchanged.
PROFILES = {"effect": CURATED, "instrument": CURATED_INSTRUMENT}


class MissingFunction(Exception):
    """A curated entry does not exist in the installed Faust library."""


def load_namespace_map() -> dict[str, Path]:
    """Parse stdfaust.lib's `xx = library("yy.lib");` lines into {prefix: path}."""
    if not STDFAUST.exists():
        raise FileNotFoundError(
            f"{STDFAUST} not found -- is Faust installed? "
            "This script reads the real library, by design."
        )
    mapping = {}
    pattern = re.compile(r'^\s*(\w+)\s*=\s*library\("([^"]+)"\)')
    for line in STDFAUST.read_text().splitlines():
        m = pattern.match(line)
        if m:
            mapping[m.group(1)] = FAUST_LIB_DIR / m.group(2)
    return mapping


def _params_of(text: str, func: str) -> list[str] | None:
    """Explicit parameter list of `func` if it is defined as `func(a,b) = ...`."""
    m = re.search(rf"^{re.escape(func)}\s*\(([^)]*)\)\s*=", text, re.MULTILINE)
    if not m:
        return None
    return [a.strip() for a in m.group(1).split(",") if a.strip()]


def _alias_of(text: str, func: str) -> tuple[str, int] | None:
    """If `func` is a point-free alias, return (target_name, n_args_applied).

    Faust libraries define many public names by aliasing or partially applying
    another function, so the arity is not written at the definition site:

        osc = oscsin;                                    -> ("oscsin", 0)
        compressor_mono = compressor_lad_mono(0);        -> ("compressor_lad_mono", 1)
        limiter_1176_R4_mono = compressor_mono(4,-6,...) -> ("compressor_mono", 4)

    Emitting these as zero-argument would be its own kind of fabrication -- the
    model would write `os.osc` bare and hit an arity error. Returns None when the
    right-hand side is a real expression rather than a simple (partial) call.
    """
    m = re.search(rf"^{re.escape(func)}\s*=\s*([^;]+);", text, re.MULTILINE)
    if not m:
        return None
    rhs = m.group(1).strip()

    call = re.fullmatch(r"(\w+)\s*\((.*)\)", rhs, re.DOTALL)
    if call:
        inner = call.group(2)
        # Count top-level commas only; nested calls may contain their own.
        depth, applied = 0, 1 if inner.strip() else 0
        for ch in inner:
            if ch in "([":
                depth += 1
            elif ch in ")]":
                depth -= 1
            elif ch == "," and depth == 0:
                applied += 1
        return call.group(1), applied

    if re.fullmatch(r"\w+", rhs):
        return rhs, 0

    return None


def find_signature(lib_path: Path, func: str, all_libs: list[Path],
                   _depth: int = 0) -> str:
    """Return the rendered argument list for `func`, following alias chains.

    Returns "" only when the definition is genuinely point-free with no resolvable
    arity (`no.noise`, `fi.dcblocker`) -- those are used bare, piped with `:`.
    Raises MissingFunction if no top-level definition exists anywhere.

    Only column-0 definitions are matched: that is the Faust library convention for
    a public export, and it avoids matching a same-named local inside a `with{}`.
    """
    if not lib_path.exists():
        raise MissingFunction(f"library {lib_path} not found (for {func})")

    text = lib_path.read_text()

    params = _params_of(text, func)
    if params is not None:
        return "(" + ", ".join(params) + ")"

    if _depth < 4:
        alias = _alias_of(text, func)
        if alias:
            target, applied = alias
            # The alias target usually lives in the same library; fall back to a
            # sweep so cross-library aliases still resolve.
            for candidate in [lib_path] + [p for p in all_libs if p != lib_path]:
                try:
                    sig = find_signature(candidate, target, all_libs, _depth + 1)
                except MissingFunction:
                    continue
                if not sig:
                    return ""
                remaining = [a.strip() for a in sig[1:-1].split(",") if a.strip()]
                remaining = remaining[applied:]
                return "(" + ", ".join(remaining) + ")" if remaining else ""
            # Alias target not found anywhere: fall through to the bare check.

    if re.search(rf"^{re.escape(func)}\s*=", text, re.MULTILINE):
        return ""

    raise MissingFunction(f"{func} is not defined at top level in {lib_path.name}")


def resolve_all(curated=None) -> tuple[list[tuple[str, list[str]]], list[str]]:
    """Resolve every curated entry. Returns (rendered_groups, errors)."""
    ns_map = load_namespace_map()
    all_libs = sorted(set(ns_map.values()))
    groups, errors = [], []

    for group_name, entries in (CURATED if curated is None else curated):
        lines = []
        for ns, func, desc in entries:
            lib = ns_map.get(ns)
            if lib is None:
                errors.append(f"{ns}.{func}: namespace {ns!r} not in stdfaust.lib")
                continue
            try:
                sig = find_signature(lib, func, all_libs)
            except MissingFunction as exc:
                errors.append(f"{ns}.{func}: {exc}")
                continue
            lines.append(f"{ns}.{func}{sig}|{desc}")
        groups.append((group_name, lines))

    return groups, errors


_VERSION_LINE_RE = re.compile(r"^# Faust version at generation:\s*(\S+)\s*$", re.M)
_ENTRY_NAME_RE = re.compile(r"^ {2}([a-z]{2,3}\.[A-Za-z_][A-Za-z0-9_]*)", re.M)


def faust_version() -> str:
    """Version of the installed faust, or "unknown" if it cannot be determined.

    Recorded in the generated block because the block's TEXT is version-dependent
    while its CONTENT is not: the same curated entry renders as
    `ef.softclipQuadratic` under faust 2.70.3 (Ubuntu noble) and
    `ef.softclipQuadratic(x)` under 2.85.5 (Arch), because the two releases
    declare it differently. A byte-exact comparison against a freshly generated
    block is therefore only meaningful when the generating version matches --
    which is what let CI run 29883328421 fail on a prompt that was correct.
    """
    try:
        out = subprocess.run(["faust", "--version"], capture_output=True,
                             text=True, timeout=15)
    except (OSError, subprocess.SubprocessError):
        return "unknown"
    match = re.search(r"Version\s+(\S+)", out.stdout or "")
    return match.group(1) if match else "unknown"


def block_faust_version(block_text: str) -> str:
    """The faust version recorded in an existing block, or "unknown"."""
    match = _VERSION_LINE_RE.search(block_text)
    return match.group(1) if match else "unknown"


def entry_names(block_text: str) -> set[str]:
    """The `ns.func` names a block declares, ignoring signature/description text.

    This is the version-INDEPENDENT content of the block: which functions the
    prompt teaches. Argument lists and column alignment vary between faust
    releases; the set of names does not, so this is what a cross-version check
    can legitimately assert.
    """
    return set(_ENTRY_NAME_RE.findall(block_text))


def render(groups: list[tuple[str, list[str]]]) -> str:
    """Format as an aligned two-column reference block."""
    # Cap the alignment column: one outlier (pf.phaser2_mono has 10 parameters)
    # would otherwise push every description off to the right and waste tokens.
    # Longer signatures get their description on a continuation line instead.
    all_sigs = [ln.split("|", 1)[0] for _, lines in groups for ln in lines]
    width = min(max(len(s) for s in all_sigs) + 2, 46)

    out = [BEGIN_MARKER,
           "# Generated by tools/gen_stdlib_block.py from the installed stdfaust.lib.",
           "# Do not hand-edit: run the script. Signatures are read from the library,",
           "# so a function that does not exist cannot appear here.",
           f"# Faust version at generation: {faust_version()}",
           ""]
    for group_name, lines in groups:
        out.append(f"{group_name}:")
        for ln in lines:
            sig, desc = ln.split("|", 1)
            if len(sig) < width:
                out.append(f"  {sig.ljust(width)}{desc}")
            else:
                out.append(f"  {sig}")
                out.append(f"  {' ' * width}{desc}")
        out.append("")
    out.append(END_MARKER)
    return "\n".join(out)


def splice(prompt_text: str, block: str, where: Path = PROMPT_FILE) -> str:
    """Replace the region between the markers, preserving everything else."""
    start = prompt_text.find(BEGIN_MARKER)
    end = prompt_text.find(END_MARKER)
    if start == -1 or end == -1:
        raise ValueError(
            f"markers not found in {where}. Expected {BEGIN_MARKER!r} and "
            f"{END_MARKER!r}. Add them where the reference block should go."
        )
    return prompt_text[:start] + block + prompt_text[end + len(END_MARKER):]


def verify_prompt_references(prompt_text: str) -> list[str]:
    """Check that EVERY `ns.func` token in the prompt exists in the installed library.

    This is the invariant the 2026-07-21 incident actually needed. The generated
    block cannot contain a fabricated function, but the hand-written parts of the
    prompt -- the rules, the prose, and above all the few-shot EXAMPLES -- still
    can, and that is precisely where `ef.ping_pong` and `ef.chorus` lived.

    Only tokens whose prefix is a real stdfaust namespace are checked, so local
    helper names in the examples (echoCh, lfo, voice, chorusCh) are ignored.

    Returns a list of human-readable problems; empty means clean.
    """
    ns_map = load_namespace_map()
    all_libs = sorted(set(ns_map.values()))

    problems, seen = [], set()
    for ns, func in re.findall(r"\b([a-z]{2})\.([A-Za-z_]\w*)", prompt_text):
        if ns not in ns_map or (ns, func) in seen:
            continue
        seen.add((ns, func))
        try:
            find_signature(ns_map[ns], func, all_libs)
        except MissingFunction:
            problems.append(
                f"{ns}.{func} does not exist in {ns_map[ns].name} "
                f"(namespace {ns!r})"
            )
    return sorted(problems)


def iter_prompt_examples(prompt_text: str) -> list[tuple[int, str]]:
    """Extract every few-shot Faust example as (index, code).

    An example runs from a line that is exactly `FAUST:` to the next blank line
    that is followed by a non-indented `USER:` or end of text. Faust programs here
    have no blank lines inside them, so splitting on the `USER:`/`FAUST:` markers
    is sufficient and avoids a fragile parser.
    """
    examples = []
    blocks = prompt_text.split("\nFAUST:\n")[1:]
    for i, block in enumerate(blocks, 1):
        code = block.split("\nUSER:")[0].rstrip()
        if code.strip():
            examples.append((i, code))
    return examples


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[1])
    parser.add_argument("--check", action="store_true",
                        help="Verify every curated entry resolves; exit 1 if not.")
    parser.add_argument("--verify-prompt", action="store_true",
                        help="Verify every ns.func reference in EVERY prompt under "
                             "llm/prompts/ exists in the installed library; exit 1 "
                             "if not. Narrow to one file with --prompt.")
    parser.add_argument("--write", action="store_true",
                        help="Splice the block into the prompt (default: "
                             "llm/prompts/system_prompt.txt; see --prompt).")
    parser.add_argument("--profile", choices=sorted(PROFILES), default="effect",
                        help="Which curated set to render (default: effect). "
                             "'instrument' is the smaller synth-oriented set for "
                             "llm/prompts/instrument_prompt.txt.")
    parser.add_argument("--prompt", type=Path, default=None, metavar="PATH",
                        help="Operate on this prompt file instead of the default. "
                             "Without it, --verify-prompt covers every prompt in "
                             "llm/prompts/ and --write targets system_prompt.txt.")
    args = parser.parse_args(argv)

    if args.verify_prompt:
        # Defaults to ALL prompts, not just system_prompt.txt. That default is the
        # point: when Phase 1 adds instrument_prompt.txt, check.sh's existing
        # `--verify-prompt` invocation covers it with no edit to the ladder. A
        # guard that has to be remembered is one that will be forgotten.
        targets = [args.prompt] if args.prompt else prompt_files()
        if not targets:
            print(f"no prompt files found in {PROMPT_DIR}", file=sys.stderr)
            return 1

        failed = False
        for target in targets:
            problems = verify_prompt_references(target.read_text())
            if problems:
                failed = True
                print(f"FABRICATED STDLIB REFERENCES in {target}:", file=sys.stderr)
                for p in problems:
                    print(f"  {p}", file=sys.stderr)
            else:
                print(f"OK -- every stdlib reference in {target.name} resolves")
        return 1 if failed else 0

    # --check verifies EVERY profile, not just the selected one. A curated entry
    # that stopped existing after a Faust upgrade is a defect whichever prompt it
    # would have been written into, and --check is the gate check.sh runs.
    if args.check:
        total = 0
        for name, curated in PROFILES.items():
            groups, errors = resolve_all(curated)
            if errors:
                print(f"STDLIB REFERENCE IS STALE in the {name!r} profile:", file=sys.stderr)
                for e in errors:
                    print(f"  MISSING  {e}", file=sys.stderr)
                return 1
            n = sum(len(lines) for _, lines in groups)
            print(f"OK -- all {n} curated {name} entries resolve against {FAUST_LIB_DIR}")
            total += n
        print(f"OK -- {total} entries across {len(PROFILES)} profiles")
        return 0

    groups, errors = resolve_all(PROFILES[args.profile])

    if errors:
        print("STDLIB REFERENCE IS STALE -- these curated entries do not exist in the "
              "installed Faust library:", file=sys.stderr)
        for e in errors:
            print(f"  MISSING  {e}", file=sys.stderr)
        print("\nEither the function was renamed/removed by a Faust upgrade, or the "
              "curated list in this file names something that never existed (this is "
              "exactly how ef.chorus / ef.ping_pong / ef.flanger got into the prompt). "
              "Fix CURATED, do not weaken the check.", file=sys.stderr)
        return 1

    block = render(groups)

    if args.write:
        target = args.prompt or PROMPT_FILE
        text = target.read_text()
        target.write_text(splice(text, block, target))
        print(f"Wrote generated block into {target}", file=sys.stderr)
        return 0

    print(block)
    return 0


if __name__ == "__main__":
    sys.exit(main())
