#!/usr/bin/env python3
"""
Render-and-measure oracle for generated Faust patches.

Closes the first half of PF-013 (semantic fidelity unmeasured). Today the only
oracle in this project is `faust -lang cpp -o /dev/null` — a patch that compiles
perfectly and sounds nothing like the request scores identically to one that nails
it. This module renders a compiled patch offline and returns numeric features, so
"did it do what the prompt asked" becomes a measurable quantity.

Deliberately dependency-free beyond numpy + scipy (both already present; see
bench/requirements.txt). No torch, no librosa, no soundfile, no network, no
provider quota. It shells out to `faust2sndfile`, which is part of the Faust
install this project already requires.

Three layers, kept separate on purpose:

  measure()   — objective safety + signal statistics. No reference to the prompt.
                NaN/Inf, DC offset, silence, runaway gain, peak. This is the
                automatable half of the P6 battery (docs/p6_test_battery.md) and
                it is a pass/fail gate.

  features()  — spectral descriptors used to ask whether the patch matches its
                description: per-band gain vs the input, spectral centroid shift,
                and crest-factor change. This is evidence, not a verdict.

  tail()      — time-domain evidence, added 2026-07-31: does the output outlive
                the input? Everything above is computed from a CONTINUOUS probe
                and therefore cannot see the failure it was added for — a delay,
                ping-pong or reverb that compiles, is not silent, has no NaN, no
                DC and no runaway gain, and whose output stops the instant the
                input does because the feedback path was dropped or the `~` arity
                was wrong. Nothing decays while the input is still going, so this
                needs its own BURST render. Also evidence, not a verdict, with one
                exception noted below.

The one gate that comes out of tail() is `never_decays` (see apply_tail_gate).
"Does this patch have a tail" is NOT a gate, because a gain plugin correctly has
none: it is answered against a caller-supplied expectation, and when no
expectation is stated the report omits the key entirely rather than reporting a
pass. Absence of a claim must not read as a passed claim.

WHAT THE TAIL CHECK CANNOT SEE, stated here so it is not rediscovered later:
  * Karplus-Strong and every other generator. Zero-input patches raise
    UnsupportedPatch (faust2sndfile is input-file-driven), so the corpus category
    `generative` is entirely outside this check — including the plucked-string
    case that motivated the `~` arity prompt rule it shipped alongside.
  * Patches whose defaults hide the mechanism. Every render is at declared
    defaults; a delay whose Dry/Wet defaults to 0 is legitimately tail-free. All
    four time-based corpus patches default mix to 0.5 today, but that is a
    property of what the model happened to emit, not of this check. The real fix
    is parameter-driving, not a better threshold.
  * The difference between feedback memory and resonant memory. This separates
    LONG memory from SHORT memory. A Q=10 resonator has t60 ~ 22 ms and reads as
    having a tail. Acceptable only because no `filters` prompt carries a tail
    expectation — do not claim more than that.

Why band-energy ratios and not a global spectral tilt: a single log-spectral slope
is a weak feature. Measured 2026-07-25 on fi.resonlp(1000, .707) vs
fi.resonhp(1000, .707) driven by white noise, the global tilt separated the
low-pass (-40.2 dB/decade) but barely moved for the high-pass (+1.4), because a
1 kHz high-pass passes most of the spectrum's energy and the fit is dominated by
its flat passband. Per-band ratios read correctly in both cases and recovered the
textbook answer for the low-pass: -0.0 dB at 100-400 Hz, -3.0 dB at the 1 kHz
corner, -30.0 dB at 4-8 kHz, -53.4 dB at 12-20 kHz.

CLI:
    python bench/render_oracle.py patch.dsp
    python bench/render_oracle.py patch.dsp --json
    python bench/render_oracle.py --self-test
"""
from __future__ import annotations

import argparse
import json
import shutil
import subprocess
import sys
import tempfile
from dataclasses import dataclass, asdict, field
from pathlib import Path

import numpy as np
import scipy.io.wavfile as wav

import warnings
from contextlib import contextmanager


@contextmanager
def _quiet_wav():
    """faust2sndfile writes a LIST/INFO chunk scipy does not recognise. The data
    chunk reads fine, so the warning is pure noise.

    Scoped to the call rather than set at import: pytest installs its own warning
    filters and resets module-level ones, so an import-time filterwarnings() call
    silently stops working the moment these renders run under the test suite.
    """
    with warnings.catch_warnings():
        warnings.simplefilter("ignore", wav.WavFileWarning)
        yield

SR = 48000
DUR = 2.0
# Octave-ish analysis bands. Chosen to straddle the cutoffs the prompt corpus
# actually asks for (most cite 200 Hz - 8 kHz) and to keep the top band below
# Nyquist with margin.
BANDS = [(50, 200), (200, 800), (800, 1200), (1200, 4000), (4000, 8000), (8000, 20000)]

# ── Tail-check constants ──────────────────────────────────────────────────────
# BURST_S: the drive window of the "burst" probe. The tail window is DUR-BURST_S.
#   A bare impulse was considered and rejected: it has no usable reference level,
#   so "-60 dB relative to what?" has no answer and what you end up measuring is
#   impulse-response length, dominated by the sharpest pole rather than by the
#   feedback memory this exists to find. Concretely, a Q=0.707 resonlp at 1 kHz
#   has t60 = 6.9/(pi*BW) ~ 1.6 ms and reads as "has a tail" against a
#   single-sample reference; against a burst's steady-state RMS it does not.
#   0.25 s also lets the dynamics category (5 of 25 prompts) reach its operating
#   point, so release time is measurable from the same render.
BURST_S = 0.25
# 240 samples at 48 kHz: ~0.3 dB standard error on an RMS estimate, tight against
# a 60 dB span, and 2x finer than the shortest LEGITIMATE memory in the corpus
# (the chorus few-shot's 10-14.5 ms delay line). A 10 ms frame would quantise
# chorus and a minimum-phase filter into adjacent buckets with no margin.
TAIL_FRAME_MS = 5.0
TAIL_FLOOR_DB = -60.0     # the RT60 convention; also well above the 1e-12 floor
# The gate. 1.75 s after the input stopped, the output is still >=70% of the level
# it held while driven — that needs an effective RT60 > 35 s, i.e. a loop gain at
# or above unity. No plausible reverb, delay or resonator reaches it. Deliberately
# far more conservative than "sounds too long": this gate must never be the reason
# a legitimate patch fails. -12 dB would start failing a 10 s reverb, which is a
# judgement call rather than a defect.
NEVER_DECAYS_DB = -3.0
# Two frames. Sits between every minimum-phase filter in the corpus (0-5 ms) and
# the shortest legitimate delay line (~15 ms). Descriptive only — `inert` is never
# itself a failure.
INERT_MS = 10.0

# ── Pitch ────────────────────────────────────────────────────────────────────
# The tolerance for "is this the note that was asked for". 50 cents = half a
# semitone, and it sits between the two things it must separate:
#   TOLERATED: detune and analog-style drift. The corpus asks for "warm"/"analog"
#     instruments; a supersaw detuned +-15 cents or an LFO drifting +-20 cents is
#     the patch working.
#   CAUGHT: an octave error is 1200 cents (24x the tolerance), a semitone is 100,
#     a patch that ignores `freq` and sits at 440 Hz against note 45 (110 Hz) is
#     2400, and a normalised-vs-Hz filter-argument mistake is off by orders of
#     magnitude.
# Nothing legitimate lands between 50 and 100 cents of the requested pitch, so
# this threshold is not a judgement call in the way NEVER_DECAYS_DB is.
PITCH_TOLERANCE_CENTS = 50.0
# Skip the attack before estimating: an ADSR attack is milliseconds, and 200 ms
# also clears a filter envelope still opening.
PITCH_SKIP_ATTACK_S = 0.2
# Lag search bounds. Below 40 Hz a ~1.7 s capture window holds too few periods to
# trust; above 2 kHz nothing in the corpus asks for a fundamental.
PITCH_F_MIN, PITCH_F_MAX = 40.0, 2000.0
# Periodicity floor: normalised ACF peak below this is NOT a pitch. Noise and
# percussion must report "not evaluated", never a number — absence of a claim
# must not read as a passed claim (the rule tail() already applies via
# `evaluated`).
PITCH_CONFIDENCE_MIN = 0.30


def _diagnostic_tail(stderr: str, stdout: str = "", limit: int = 900) -> str:
    """The LAST part of a toolchain's output, not the first.

    Compilers emit warnings before errors. Reporting `stderr[:400]` showed three
    screens of -Wformat-truncation noise from Faust's generated main() and hid the
    line that actually stopped the build — which cost a full CI round trip on
    2026-07-26 to notice. Take the tail, and fall back to stdout when stderr is
    empty, because some wrapper scripts report failure there.
    """
    text = (stderr or "").strip() or (stdout or "").strip()
    if not text:
        return "<no output on stderr or stdout>"
    lines = [ln for ln in text.splitlines() if ln.strip()]
    tail = "\n".join(lines[-12:])
    return tail[-limit:] if len(tail) > limit else tail


class RenderError(RuntimeError):
    """faust2sndfile failed to build, or the built binary failed to render."""


class UnsupportedPatch(RenderError):
    """The patch is outside this harness's coverage, and that is not the patch's fault.

    Specifically: zero-input patches (synths, oscillators, noise generators — the
    `generative` category, 5 of the 25 benchmark prompts). The faust2sndfile
    architecture is input-file-driven; given a 0-input DSP it exits 0 and writes a
    44-byte WAV header with an empty data chunk. Measured 2026-07-25 on Faust
    2.85.5; wrapping via `process = !, component("gen.dsp")` and an arity-matched
    variant both produce the same empty output, so there is no workaround at this
    layer. Generators need the C++ offline harness driven by the existing
    FaustEngine JIT — which is the better home for this anyway, since it exercises
    the real production path rather than a parallel binary.

    That harness now exists: `host/tests/OfflineRenderTest --capture <in.dsp>
    <out.wav> [--note <n>]` (default note 45 / 110 Hz) renders a 0-input patch
    through the real PluginForgeProcessor, plays a note if the patch declares the
    full voice contract, and prints an `instrument=`/`held_end=` CAPTURE_OK line
    that `pitch_of_wav()` above can measure directly. `bench/p6_capture.py` is
    the existing caller. Routing generators through it automatically from this
    module is a separate, deliberately deferred change — see the plan note on
    `render_many()`'s `(input, output)` contract having nothing to compare a
    0-input patch against.
    """


def patch_arity(dsp_source: str, timeout: int = 60) -> tuple[int, int]:
    """(inputs, outputs) via `faust -json`. Cheap, and it is the compiler's own answer."""
    with tempfile.TemporaryDirectory() as td:
        d = Path(td)
        (d / "p.dsp").write_text(dsp_source, encoding="utf-8")
        r = subprocess.run(["faust", "-json", "p.dsp", "-o", "/dev/null"], cwd=d,
                           capture_output=True, text=True, encoding="utf-8",
                           errors="replace", timeout=timeout)
        meta = d / "p.dsp.json"
        if not meta.exists():
            raise RenderError(f"faust -json failed: {r.stderr.strip()[:300]}")
        j = json.loads(meta.read_text(encoding="utf-8"))
    return int(j.get("inputs", 0)), int(j.get("outputs", 0))


@dataclass
class Measurement:
    """Objective, prompt-independent. Every field here is a hard gate."""
    ok: bool
    reasons: list[str] = field(default_factory=list)
    has_nan_inf: bool = False
    is_silent: bool = False
    peak: float = 0.0
    rms: float = 0.0
    rms_ratio_db: float = 0.0
    dc_offset: float = 0.0
    n_frames: int = 0
    n_channels: int = 0
    # Set by apply_tail_gate() from the burst render, not by measure(). Left at
    # the defaults when no burst probe was run, which reads as "not evaluated".
    never_decays: bool = False
    tail_gate_evaluated: bool = False
    env_db_final: float = -np.inf
    # Set by apply_pitch_gate() from a --capture instrument render, not by
    # measure(). Left at the defaults when no pitch probe was run.
    pitch_gate_evaluated: bool = False
    cents_error: float = np.inf
    out_of_tune: bool = False
    # Machine-readable siblings of `reasons`. `reasons` stays human text, and the
    # existing tests substring-match it, so it must not change shape. Consumers
    # that need to branch on a specific gate should use these instead of adding
    # yet another substring match.
    codes: list[str] = field(default_factory=list)


@dataclass
class Tail:
    """Time-domain evidence: does the output outlive the input? No verdict here.

    Peer to Features. Computed from a separate BURST render, because nothing
    decays while the input is still going. The single verdict derived from this
    is folded in by apply_tail_gate(), which is a named step rather than a side
    effect so that the evidence/verdict seam stays where measure()/features()
    put it.

    `evaluated` is False when the signal had no silent region (e.g. the
    continuous noise probe) or the patch was silent while driven. In that state
    no gate fires and no expectation is judged — the check is structurally
    incapable of misfiring on a signal it was not designed for.
    """
    evaluated: bool = False
    note: str = ""
    tail_ms: float = 0.0
    tail_truncated: bool = False      # still above the floor at the end of the window
    tail_ratio_db: float = -np.inf    # post-drive energy vs drive-window energy
    env_db_final: float = -np.inf
    ref_rms: float = 0.0
    burst_end_ms: float = 0.0
    inert: bool = True                # memoryless: output stops when the input does
    frame_ms: float = TAIL_FRAME_MS


@dataclass
class Pitch:
    """The one semantic gate this project has: did the instrument play the note
    it was sent? Peer to Tail, computed from a --capture WAV (a rendered
    instrument, not the noise/sweep/burst probes above — those never reach a
    zero-input synth; see UnsupportedPatch).

    `evaluated` is False when the ACF confidence never clears
    PITCH_CONFIDENCE_MIN — an unpitched or too-quiet signal is not judged, the
    same structural safety `Tail.evaluated` gives the tail check.
    """
    evaluated: bool = False
    why: str = ""
    midi_note: int = -1
    expected_hz: float = 0.0
    f0_hz: float = 0.0
    confidence: float = 0.0
    cents_error: float = float("inf")
    tolerance_cents: float = PITCH_TOLERANCE_CENTS
    in_tune: bool = False
    fft_peak_hz: float = 0.0   # evidence only, never gated on — see fft_peak_hz()


@dataclass
class Features:
    """Spectral descriptors. Evidence for semantic checks, not a pass/fail."""
    band_gain_db: dict[str, float] = field(default_factory=dict)
    centroid_in_hz: float = 0.0
    centroid_out_hz: float = 0.0
    centroid_shift_oct: float = 0.0
    crest_in_db: float = 0.0
    crest_out_db: float = 0.0


def test_signal(kind: str = "noise", sr: int = SR, dur: float = DUR) -> np.ndarray:
    """Deterministic stereo probe. Flat-spectrum noise by default, so any patch's
    frequency response is directly readable off the output spectrum."""
    n = int(sr * dur)
    if kind == "noise":
        # Seeded: two runs of the same patch must produce identical numbers, or
        # this is not an oracle.
        rng = np.random.default_rng(0)
        x = rng.standard_normal((n, 2)) * 0.1
    elif kind == "sweep":
        t = np.arange(n) / sr
        f0, f1 = 20.0, 20000.0
        phase = 2 * np.pi * f0 * (f1 / f0) ** (t / dur) * dur / np.log(f1 / f0)
        x = np.repeat((0.25 * np.sin(phase))[:, None], 2, axis=1)
    elif kind == "impulse":
        x = np.zeros((n, 2))
        x[0, :] = 1.0
    elif kind == "burst":
        # Deliberately the same seed and scale as "noise": the first BURST_S of
        # this signal is bit-identical to the first BURST_S of the spectral probe,
        # so the two renders of a patch are directly comparable and both stay
        # deterministic. Drive, then silence — the silence is the measurement.
        rng = np.random.default_rng(0)
        x = np.zeros((n, 2))
        k = int(sr * BURST_S)
        x[:k, :] = rng.standard_normal((k, 2)) * 0.1
    else:
        raise ValueError(f"unknown test signal: {kind}")
    return x.astype(np.float32)


def _orient(y: np.ndarray) -> np.ndarray:
    """faust2sndfile's WAV comes back frames x channels or its transpose."""
    y = np.atleast_2d(y.astype(np.float64))
    if y.ndim == 1 or y.shape[0] < y.shape[1]:
        y = y.reshape(-1, 1) if y.ndim == 1 else y.T
    return y


def render_many(dsp_source: str, signals: list[np.ndarray],
                sr: int = SR, timeout: int = 120) -> list[tuple[np.ndarray, np.ndarray]]:
    """Compile `dsp_source` ONCE and render every signal in `signals` through it.

    This is not an optimisation, it is the feasibility condition for the tail
    check. faust2sndfile is a C++ compile and link (seconds) followed by a binary
    invocation (tens of ms). Calling render() twice per patch would add a full
    build each time and take tools/check.sh audio from ~60 s to ~120 s; one build
    with N invocations costs a few seconds across the whole corpus.

    Returns [(input, output), ...] as float64 arrays, in the order given. Raises
    RenderError on any failure — callers must treat that as "no audio evidence",
    never as "silence".
    """
    if shutil.which("faust2sndfile") is None:
        raise RenderError("faust2sndfile not on PATH (part of the Faust install)")
    if not signals:
        return []

    n_in, n_out = patch_arity(dsp_source)
    if n_in == 0:
        raise UnsupportedPatch(
            f"zero-input patch ({n_in} in, {n_out} out) — generators are outside "
            "this harness; see UnsupportedPatch")

    results: list[tuple[np.ndarray, np.ndarray]] = []
    with tempfile.TemporaryDirectory() as td:
        d = Path(td)
        (d / "patch.dsp").write_text(dsp_source, encoding="utf-8")

        build = subprocess.run(["faust2sndfile", "patch.dsp"], cwd=d,
                               capture_output=True, text=True,
                               encoding="utf-8", errors="replace", timeout=timeout)
        binary = d / "patch"
        if not binary.exists():
            raise RenderError(
                f"build failed (exit {build.returncode}, no binary produced; "
                f"dir: {sorted(p.name for p in d.iterdir())}): "
                f"{_diagnostic_tail(build.stderr, build.stdout)}")

        for i, signal in enumerate(signals):
            in_wav, out_wav = f"in{i}.wav", f"out{i}.wav"
            wav.write(str(d / in_wav), sr, signal)
            run = subprocess.run([str(binary), in_wav, out_wav], cwd=d,
                                 capture_output=True, text=True,
                                 encoding="utf-8", errors="replace", timeout=timeout)
            out = d / out_wav
            if not out.exists():
                raise RenderError(
                    f"render failed (exit {run.returncode}, no {out_wav}): "
                    f"{_diagnostic_tail(run.stderr, run.stdout)}")
            with _quiet_wav():
                _, y = wav.read(str(out))
            results.append((signal.astype(np.float64), _orient(y)))

    return results


def render(dsp_source: str, signal: np.ndarray | None = None,
           sr: int = SR, timeout: int = 120) -> tuple[np.ndarray, np.ndarray]:
    """Compile `dsp_source` with faust2sndfile and render `signal` through it.

    Returns (input, output) as float64 arrays. Raises RenderError on any failure —
    callers must treat that as "no audio evidence", never as "silence".
    """
    if signal is None:
        signal = test_signal()
    return render_many(dsp_source, [signal], sr=sr, timeout=timeout)[0]


def measure(x: np.ndarray, y: np.ndarray) -> Measurement:
    """Objective gates. This is the automatable half of the P6 battery."""
    m = Measurement(ok=True, n_frames=int(y.shape[0]), n_channels=int(y.shape[1]))

    m.has_nan_inf = bool((~np.isfinite(y)).any())
    if m.has_nan_inf:
        # Everything downstream is meaningless once the buffer is poisoned.
        m.ok = False
        m.reasons.append("output contains NaN or Inf")
        m.codes.append("nan_inf")
        return m

    m.peak = float(np.abs(y).max())
    m.rms = float(np.sqrt((y ** 2).mean()))
    x_rms = float(np.sqrt((x ** 2).mean()))
    m.rms_ratio_db = float(20 * np.log10(m.rms / x_rms)) if m.rms > 0 and x_rms > 0 else -np.inf
    m.dc_offset = float(np.abs(y.mean(axis=0)).max())

    # -100 dBFS: below any dither floor, and safely under a legitimately quiet patch.
    m.is_silent = m.rms < 1e-5
    if m.is_silent:
        m.ok = False
        m.reasons.append(f"output is silent (rms {m.rms:.2e})")
        m.codes.append("silent")
    # A patch may legitimately boost, but +40 dB on unity-ish input is runaway
    # feedback, which is the failure the P6 battery hit on unbounded delays.
    if m.rms_ratio_db > 40:
        m.ok = False
        m.reasons.append(f"runaway gain ({m.rms_ratio_db:+.1f} dB vs input)")
        m.codes.append("runaway_gain")
    if m.peak > 100:
        m.ok = False
        m.reasons.append(f"peak {m.peak:.1f} far outside plausible range")
        m.codes.append("peak")
    if m.dc_offset > 0.05:
        m.ok = False
        m.reasons.append(f"DC offset {m.dc_offset:.3f}")
        m.codes.append("dc_offset")
    return m


def _rms_envelope(y: np.ndarray, frame: int) -> np.ndarray:
    """Frame-wise RMS with channels folded in. Trailing partial frame dropped."""
    n = (y.shape[0] // frame) * frame
    if n == 0:
        return np.zeros(0)
    z = y[:n].reshape(-1, frame, y.shape[1])
    return np.sqrt((z ** 2).mean(axis=(1, 2)))


def tail(x: np.ndarray, y: np.ndarray, sr: int = SR,
         frame_ms: float = TAIL_FRAME_MS) -> Tail:
    """Time-domain evidence from a burst render. Evidence only — no verdict.

    The burst end is derived from `x` rather than assumed from BURST_S, which
    gives the load-bearing safety property: on a continuous probe the drive
    region is the whole buffer, the tail region is empty, and `evaluated` stays
    False. This function cannot report anything about a signal that has no
    silence in it.
    """
    t = Tail(frame_ms=frame_ms)
    frame = max(1, int(sr * frame_ms / 1000.0))

    # Where does the drive stop? -60 dB relative to the input's own peak.
    peak_in = float(np.abs(x).max())
    if peak_in <= 0:
        t.note = "input is all zeros"
        return t
    live = np.nonzero((np.abs(x) > 1e-3 * peak_in).any(axis=1))[0]
    k = int(live[-1]) + 1 if live.size else 0
    t.burst_end_ms = float(k / sr * 1000.0)
    if k <= 0 or k >= x.shape[0]:
        t.note = "signal has no silent region — not a burst probe"
        return t

    # Reference is the LAST HALF of the drive window: it skips the fill/attack
    # transient, so this is the steady-state driven level rather than an average
    # over a ramp.
    ref = float(np.sqrt((y[k // 2:k] ** 2).mean())) if k >= 2 else 0.0
    t.ref_rms = ref
    if ref <= 0:
        # The existing is_silent gate already owns this failure; do not double-report.
        t.note = "output silent while driven"
        return t

    env = _rms_envelope(y, frame)
    if env.size == 0:
        t.note = "output shorter than one frame"
        return t
    env_db = 20 * np.log10(np.maximum(env, 1e-12) / ref)
    f0 = int(np.ceil(k / frame))
    if f0 >= env_db.size:
        t.note = "no whole frame after the drive window"
        return t

    post = env_db[f0:]
    # The LAST frame above the floor, not the first frame below it. A ping-pong
    # delay's envelope is not monotone — it has near-silent gaps between taps, and
    # a first-crossing rule reports ~5 ms for a two-second echo train.
    above = np.nonzero(post >= TAIL_FLOOR_DB)[0]
    t.tail_ms = float((above[-1] + 1) * frame / sr * 1000.0) if above.size else 0.0
    t.tail_truncated = bool(above.size and above[-1] == post.size - 1)
    t.env_db_final = float(env_db[-1])
    t.tail_ratio_db = float(10 * np.log10(
        ((y[k:] ** 2).sum() + 1e-30) / ((y[:k] ** 2).sum() + 1e-30)))
    t.inert = bool(t.tail_ms < INERT_MS)
    t.evaluated = True
    return t


def apply_tail_gate(m: Measurement, t: Tail) -> Measurement:
    """Fold the one expectation-free tail verdict into an existing Measurement.

    Only `never_decays` is a gate. "Has a tail" is not, and must not become one —
    a gain plugin correctly has none. Mutates and returns `m`.
    """
    if not t.evaluated:
        return m
    m.tail_gate_evaluated = True
    m.env_db_final = t.env_db_final
    m.never_decays = bool(t.env_db_final >= NEVER_DECAYS_DB)
    if m.never_decays:
        m.ok = False
        m.reasons.append(
            f"output never decays ({t.env_db_final:+.1f} dB vs drive level "
            f"{DUR - BURST_S:.2f}s after the input stopped) — loop gain at or "
            f"above unity")
        m.codes.append("never_decays")
    return m


# ── Tail expectations ─────────────────────────────────────────────────────────
# Minimum tail (ms) a patch must show, keyed by prompt first, category second.
# A NUMBER rather than a boolean because the legitimate tails in this corpus span
# two orders of magnitude: the chorus few-shot's delay line is ~15 ms and a plate
# reverb runs past the 1.75 s measurement window. One global "has a tail"
# threshold would have to sit at the chorus floor, which makes it near-vacuous for
# reverb. The category entry is a weak proxy and is all a benchmark record carries;
# the per-prompt overrides are what make this useful rather than toothless.
TAIL_EXPECT_MS: dict[str, float] = {
    "time-based": 10.0,
    "a stereo delay with time, feedback, and dry/wet": 120.0,
    "a ping-pong delay that bounces between left and right": 120.0,
    "a simple plate reverb with decay time and damping": 200.0,
}


def expectation_for(prompt: str | None, category: str | None) -> float | None:
    """Minimum expected tail in ms, or None when no expectation is stated."""
    if prompt and prompt in TAIL_EXPECT_MS:
        return TAIL_EXPECT_MS[prompt]
    return TAIL_EXPECT_MS.get(category or "")


def meets_tail_expectation(t: Tail, expect_ms: float) -> tuple[bool, str]:
    """Judge a Tail against a stated minimum. Returns (met, human reason)."""
    if not t.evaluated:
        return True, f"not evaluated ({t.note or 'no burst probe'})"
    if t.tail_ms >= expect_ms:
        return True, f"tail {t.tail_ms:.0f} ms >= {expect_ms:.0f} ms"
    if t.inert:
        return False, (f"no tail: output stops {t.tail_ms:.0f} ms after the input "
                       f"does, expected >= {expect_ms:.0f} ms")
    return False, f"tail {t.tail_ms:.0f} ms is under the expected {expect_ms:.0f} ms"


def note_hz(midi_note: int) -> float:
    """MIDI note -> Hz, the same equal-tempered formula FaustEngine::noteOn
    writes into the /freq zone. Note 45 is 110.0 exactly."""
    return 440.0 * 2.0 ** ((midi_note - 69) / 12.0)


def cents(f_hz: float, ref_hz: float) -> float:
    """Signed interval in cents; +inf when either side is non-positive (there is
    no interval to a frequency of zero)."""
    if f_hz <= 0 or ref_hz <= 0:
        return float("inf")
    return float(1200.0 * np.log2(f_hz / ref_hz))


def fundamental_hz(y: np.ndarray, sr: int = SR,
                   f_min: float = PITCH_F_MIN, f_max: float = PITCH_F_MAX,
                   peak_ratio: float = 0.9) -> tuple[float, float]:
    """(f0_hz, confidence) by normalised autocorrelation with parabolic peak
    interpolation. Returns (0.0, confidence) when nothing clears `peak_ratio`.

    NOT an FFT peak, deliberately: a saw or square whose 2nd harmonic dominates
    (or anything through a high-pass) puts the loudest FFT *bin* an octave above
    the true fundamental — exactly the error class this gate exists to catch.
    Autocorrelation peaks at the true period whether or not the fundamental is
    present at all. Its own failure mode is the opposite octave (the peak at 2T
    nearly matches the one at T), handled the standard way: take the SMALLEST
    lag that clears `peak_ratio` of the global max within the search window,
    not the tallest peak.
    """
    x = np.asarray(y, dtype=np.float64)
    x = x - x.mean()
    if x.size < 2 or not np.any(x):
        return 0.0, 0.0

    ac = np.correlate(x, x, mode="full")[x.size - 1:]
    if ac[0] <= 0:
        return 0.0, 0.0
    ac = ac / ac[0]

    lo = max(1, int(sr / f_max))
    hi = min(ac.size - 2, int(sr / f_min))
    if hi <= lo:
        return 0.0, 0.0
    seg = ac[lo:hi]

    peak = float(seg.max())
    if peak <= 0:
        return 0.0, 0.0
    threshold = peak_ratio * peak

    local_max = np.nonzero(
        (seg >= threshold) & (seg >= np.roll(seg, 1)) & (seg >= np.roll(seg, -1)))[0]
    # Exclude the array-wrap artefacts np.roll introduces at the two ends.
    local_max = local_max[(local_max > 0) & (local_max < seg.size - 1)]
    idx = int(local_max[0]) if local_max.size else int(seg.argmax())
    lag = idx + lo

    # Parabolic interpolation around the chosen lag for sub-sample precision.
    if 0 < lag < ac.size - 1:
        a, b, c = ac[lag - 1], ac[lag], ac[lag + 1]
        denom = a - 2 * b + c
        delta = float(np.clip(0.5 * (a - c) / denom, -1.0, 1.0)) if denom != 0 else 0.0
    else:
        delta = 0.0
    true_lag = lag + delta
    if true_lag <= 0:
        return 0.0, float(seg[idx])
    return float(sr / true_lag), float(seg[idx])


def fft_peak_hz(y: np.ndarray, sr: int = SR) -> float:
    """The loudest FFT bin, parabolically interpolated. EVIDENCE ONLY — reported
    so a disagreement with fundamental_hz() is visible, never gated on (see that
    function's docstring for why the FFT peak is the wrong instrument for the
    missing-fundamental case)."""
    x = np.asarray(y, dtype=np.float64)
    if x.size < 2:
        return 0.0
    mag = np.abs(np.fft.rfft(x * np.hanning(x.size)))
    if mag.size < 3:
        return 0.0
    idx = int(np.argmax(mag[1:])) + 1  # skip DC
    if 0 < idx < mag.size - 1:
        a, b, c = mag[idx - 1], mag[idx], mag[idx + 1]
        denom = a - 2 * b + c
        delta = float(np.clip(0.5 * (a - c) / denom, -1.0, 1.0)) if denom != 0 else 0.0
    else:
        delta = 0.0
    return float((idx + delta) * sr / x.size)


def pitch_of(y: np.ndarray, midi_note: int, sr: int = SR,
            held_end: int | None = None,
            skip_attack_s: float = PITCH_SKIP_ATTACK_S,
            tolerance_cents: float = PITCH_TOLERANCE_CENTS) -> Pitch:
    """Measure the fundamental of the HELD region and judge it against the note.

    `held_end` is the note-off sample index — exactly what --capture prints as
    `held_end=` on the CAPTURE_OK line — so this needs no copy of the capture
    binary's timing constants. The release is excluded on purpose: a decaying
    tail is where a pitch estimator is least reliable and it carries nothing
    this gate wants.
    """
    p = Pitch(midi_note=midi_note, expected_hz=note_hz(midi_note),
              tolerance_cents=tolerance_cents)

    x = np.asarray(y, dtype=np.float64)
    if x.ndim > 1:
        x = x.mean(axis=1)   # fold channels; the voice contract's output is
                              # mono duplicated to both, so this changes nothing
                              # for a real capture and is harmless otherwise.

    end = min(int(held_end), x.shape[0]) if held_end else x.shape[0]
    start = int(skip_attack_s * sr)
    if start >= end:
        p.why = "held region shorter than the attack skip"
        return p

    f0, confidence = fundamental_hz(x[start:end], sr)
    p.confidence = confidence
    if confidence < PITCH_CONFIDENCE_MIN or f0 <= 0:
        p.why = (f"not periodic enough (ACF confidence {confidence:.2f} < "
                 f"{PITCH_CONFIDENCE_MIN:.2f}) — unpitched, silent, or too short")
        return p

    p.f0_hz = f0
    p.fft_peak_hz = fft_peak_hz(x[start:end], sr)
    p.cents_error = cents(f0, p.expected_hz)
    p.in_tune = abs(p.cents_error) <= tolerance_cents
    p.evaluated = True
    return p


def pitch_of_wav(path, midi_note: int, held_end: int | None = None) -> Pitch:
    """Read a --capture WAV and measure it against the note that was sent."""
    with _quiet_wav():
        sr, y = wav.read(str(path))
    # 24-bit PCM: scipy repacks into int32 with the three bytes HIGH
    # (scipy.io.wavfile._read_data_chunk, the 'V1' path), so full scale is
    # 2**31, not 2**23. Pitch is scale-invariant; this matters only if a level
    # is ever read off the same array.
    return pitch_of(y.astype(np.float64), midi_note, sr=int(sr), held_end=held_end)


def apply_pitch_gate(m: Measurement, p: Pitch) -> Measurement:
    """Fold the pitch verdict into a Measurement. Mutates and returns it.

    Fires only when p.evaluated — an effect capture, or an unpitched output, is
    not judged. Same named-step shape as apply_tail_gate.
    """
    if not p.evaluated:
        return m
    m.pitch_gate_evaluated = True
    m.cents_error = p.cents_error
    m.out_of_tune = not p.in_tune
    if m.out_of_tune:
        m.ok = False
        m.reasons.append(
            f"pitch is {p.cents_error:+.0f} cents from note {p.midi_note} "
            f"({p.expected_hz:.2f} Hz expected, {p.f0_hz:.2f} Hz measured) — "
            f"outside the {p.tolerance_cents:.0f}-cent tolerance")
        m.codes.append("out_of_tune")
    return m


def _band_energy(sig: np.ndarray, sr: int, lo: float, hi: float) -> float:
    S = np.abs(np.fft.rfft(sig[:, 0])) ** 2
    f = np.fft.rfftfreq(sig.shape[0], 1 / sr)
    return float(S[(f >= lo) & (f < hi)].sum())


def _centroid(sig: np.ndarray, sr: int) -> float:
    S = np.abs(np.fft.rfft(sig[:, 0]))
    f = np.fft.rfftfreq(sig.shape[0], 1 / sr)
    tot = S.sum()
    return float((f * S).sum() / tot) if tot > 0 else 0.0


def _crest_db(sig: np.ndarray) -> float:
    r = np.sqrt((sig ** 2).mean())
    return float(20 * np.log10(np.abs(sig).max() / r)) if r > 0 else 0.0


def features(x: np.ndarray, y: np.ndarray, sr: int = SR) -> Features:
    """Spectral descriptors, output relative to input."""
    f = Features()
    for lo, hi in BANDS:
        ein, eout = _band_energy(x, sr, lo, hi), _band_energy(y, sr, lo, hi)
        f.band_gain_db[f"{lo}-{hi}"] = float(10 * np.log10((eout + 1e-20) / (ein + 1e-20)))
    f.centroid_in_hz = _centroid(x, sr)
    f.centroid_out_hz = _centroid(y, sr)
    if f.centroid_in_hz > 0 and f.centroid_out_hz > 0:
        f.centroid_shift_oct = float(np.log2(f.centroid_out_hz / f.centroid_in_hz))
    f.crest_in_db, f.crest_out_db = _crest_db(x), _crest_db(y)
    return f


def analyse(dsp_source: str, signal_kind: str = "noise", *,
            tail_probe: bool = True,
            expect_tail_ms: float | None = None) -> dict:
    """Full pipeline. Returns a JSON-serialisable report.

    `schema` is 2 from 2026-07-31. Version 1 reports (e.g.
    bench/results/oracle_20260725.json) carry no such key; readers must use .get()
    and treat its absence as 1. `features` is byte-identical between the two.

    When `expect_tail_ms` is None the "expectation" key is OMITTED rather than
    reported as met — absence of a claim must not read as a passed claim.
    """
    probes = [test_signal(signal_kind)]
    if tail_probe:
        probes.append(test_signal("burst"))
    try:
        rendered = render_many(dsp_source, probes)
    except UnsupportedPatch as exc:
        return {"schema": 2, "rendered": False, "unsupported": True, "error": str(exc)}
    except RenderError as exc:
        return {"schema": 2, "rendered": False, "unsupported": False, "error": str(exc)}

    x, y = rendered[0]
    m, f = measure(x, y), features(x, y)

    report: dict = {"schema": 2, "rendered": True}
    if tail_probe:
        bx, by = rendered[1]
        t = tail(bx, by)
        apply_tail_gate(m, t)
        report["tail"] = asdict(t)
        if expect_tail_ms is not None:
            met, why = meets_tail_expectation(t, expect_tail_ms)
            report["expectation"] = {"expect_tail_ms": float(expect_tail_ms),
                                     "met": bool(met), "reason": why}

    report["measurement"] = asdict(m)
    report["features"] = asdict(f)
    return report


def analyse_record(rec: dict, *, expectations: bool = True) -> dict:
    """analyse() one benchmark record, applying its category/prompt expectation."""
    expect = expectation_for(rec.get("prompt"), rec.get("category")) if expectations else None
    return analyse(rec["code"], expect_tail_ms=expect)


def analyse_corpus(records: list[dict], *, expectations: bool = True,
                   progress=None) -> dict:
    """Render every record and summarise. The ONE corpus driver.

    tools/check.sh and tools/health_report.py both used to inline their own loop
    over analyse(), recomputing the same gate logic in two places that were free
    to drift. There are already three implementations of "is this patch broken" in
    this repo (here, bench/p6_capture.verdict_for, and OutputGuard.cpp); this
    removes one of the two that were avoidable.
    """
    out = {"total": len(records), "passed": 0, "failed": 0, "unsupported": 0,
           "expectation_unmet": 0, "failures": [], "unmet": [], "reports": []}
    for rec in records:
        r = analyse_record(rec, expectations=expectations)
        out["reports"].append(r)
        label = (rec.get("prompt") or "?")[:60]
        if progress:
            progress(label, r)
        if not r["rendered"]:
            if r.get("unsupported"):
                out["unsupported"] += 1
            else:
                out["failed"] += 1
                out["failures"].append((label, r["error"]))
            continue
        m = r["measurement"]
        if m["ok"]:
            out["passed"] += 1
        else:
            out["failed"] += 1
            out["failures"].append((label, "; ".join(m["reasons"])))
        exp = r.get("expectation")
        if exp and not exp["met"]:
            out["expectation_unmet"] += 1
            out["unmet"].append((label, exp["reason"]))
    return out


# --- self-test -------------------------------------------------------------
# Patches with known-correct answers. If these drift, the oracle is lying and
# every number built on it is void.
#
# CLAUDE.md: "A control counts only once it has been seen failing." Two entries
# below are RED cases — `dead_delay` must fail a stated expectation and
# `never_decays` must fail the gate — and `gain` is the case that proves the tail
# check is not a universal gate: the same patch is green as an unconditional
# verdict and red only under an expectation it was never meant to meet.
#
# Sources compiled against Faust 2.85.9 before any measurement code was written;
# all four report 2 in / 2 out under `faust -json`.

def _check_lowpass(r: dict) -> tuple[bool, str]:
    m, f = r["measurement"], r["features"]
    corner, high = f["band_gain_db"]["800-1200"], f["band_gain_db"]["4000-8000"]
    return (m["ok"] and -5 < corner < -1 and high < -20,
            f"corner {corner:+.1f}, 4-8k {high:+.1f}")


def _check_gain(r: dict) -> tuple[bool, str]:
    m, t = r["measurement"], r["tail"]
    flat = all(-7 < v < -5 for v in r["features"]["band_gain_db"].values())
    # A correct patch that correctly has NO tail, and is still green.
    no_tail = t["evaluated"] and t["inert"] and t["tail_ms"] == 0.0
    # ...but fails a time-based expectation. This is the whole argument for the
    # expectation being separate from the gate.
    met, _ = meets_tail_expectation(Tail(**t), 10.0)
    return (m["ok"] and flat and no_tail and not m["never_decays"] and not met,
            f"flat={flat} inert={t['inert']} tail={t['tail_ms']:.0f}ms met_10ms={met}")


def _check_silent(r: dict) -> tuple[bool, str]:
    m = r["measurement"]
    return (not m["ok"]) and m["is_silent"], "silence gate must fire"


def _check_delay_tail(r: dict) -> tuple[bool, str]:
    m, t = r["measurement"], r["tail"]
    good = (m["ok"] and t["evaluated"] and not t["inert"]
            and 900 < t["tail_ms"] < 1750 and not t["tail_truncated"]
            and not m["never_decays"])
    return good, (f"tail={t['tail_ms']:.0f}ms truncated={t['tail_truncated']} "
                  f"inert={t['inert']}")


def _check_dead_delay(r: dict) -> tuple[bool, str]:
    """The bug this whole check exists for.

    It compiles, is not silent, has no NaN, no DC, unity gain — GREEN on every
    gate that existed before 2026-07-31 — and its output stops the instant the
    input does. Only a stated time-based expectation catches it.
    """
    m, t = r["measurement"], r["tail"]
    met, _ = meets_tail_expectation(Tail(**t), 120.0)
    return (m["ok"] and t["evaluated"] and t["inert"] and not met,
            f"ok={m['ok']} (must stay True) inert={t['inert']} met_120ms={met}")


def _check_never_decays(r: dict) -> tuple[bool, str]:
    """Unity loop gain: once excited it circulates forever at constant level.

    Asserting `green_before` is what makes this a demonstration rather than an
    assertion — it proves the new gate sees something the old battery could not,
    and it starts failing loudly if some future change makes an old gate catch it
    by accident.
    """
    m, t = r["measurement"], r["tail"]
    green_before = (not m["has_nan_inf"] and not m["is_silent"]
                    and m["rms_ratio_db"] <= 40 and m["peak"] <= 100
                    and m["dc_offset"] <= 0.05)
    good = green_before and m["never_decays"] and not m["ok"] and "never_decays" in m["codes"]
    return good, (f"green_on_old_gates={green_before} never_decays={m['never_decays']} "
                  f"env_final={t['env_db_final']:+.1f}dB")


_SELF_TEST = {
    "lowpass": ('import("stdfaust.lib");\n'
                'process = fi.resonlp(1000, 0.707, 1.0), fi.resonlp(1000, 0.707, 1.0);',
                "800-1200 near -3 dB, 4000-8000 strongly attenuated",
                _check_lowpass),
    "gain": ('import("stdfaust.lib");\nprocess = _*0.5, _*0.5;',
             "flat -6 dB, no tail, and correctly fails a 10 ms expectation",
             _check_gain),
    "silent": ('import("stdfaust.lib");\nprocess = _*0, _*0;',
               "must FAIL the silence gate",
               _check_silent),
    # t60 = 100 ms * ln(0.001)/ln(0.6) = 1350 ms, deliberately INSIDE the 1750 ms
    # window so the non-truncated path is the one under test.
    "delay_tail": ('import("stdfaust.lib");\n'
                   'echo = + ~ (de.delay(48000, 4800) : *(0.6));\n'
                   'process = echo, echo;',
                   "tail ~1350 ms, not truncated, not inert",
                   _check_delay_tail),
    "dead_delay": ('import("stdfaust.lib");\n'
                   'dtime = hslider("Time [unit:ms]", 250, 10, 1000, 1);\n'
                   'fb    = hslider("Feedback", 0.4, 0, 0.95, 0.01);\n'
                   'process = _, _;',
                   "RED: green on every old gate, but no tail — must miss 120 ms",
                   _check_dead_delay),
    "never_decays": ('import("stdfaust.lib");\n'
                     'comb = + ~ de.delay(48000, 4800);\n'
                     'process = comb, comb;',
                     "RED: green on every old gate, but the new gate must fire",
                     _check_never_decays),
}


def self_test() -> int:
    failures = 0
    for name, (src, expect, check) in _SELF_TEST.items():
        r = analyse(src)
        print(f"\n=== {name} — expect: {expect}")
        if not r["rendered"]:
            print(f"  RENDER FAILED: {r['error']}")
            failures += 1
            continue
        m, f = r["measurement"], r["features"]
        print(f"  ok={m['ok']} codes={m['codes']} rms={m['rms_ratio_db']:+.1f} dB")
        print("  bands: " + "  ".join(f"{k}:{v:+.1f}" for k, v in f["band_gain_db"].items()))
        t = r.get("tail")
        if t:
            print(f"  tail: {t['tail_ms']:.0f} ms  inert={t['inert']} "
                  f"truncated={t['tail_truncated']} env_final={t['env_db_final']:+.1f} dB")
        good, detail = check(r)
        print(f"  -> {'PASS' if good else 'FAIL'} ({detail})")
        failures += not good
    print(f"\n{'ALL PASS' if not failures else f'{failures} FAILED'}")
    return 1 if failures else 0


def corpus_main(results_path: Path, strict: bool = False) -> int:
    """Render every compiling patch in a benchmark results file.

    EXIT POLICY, and the asymmetry is deliberate: a failed gate exits 1, an unmet
    tail expectation only reports unless --strict. `measurement.ok` is a property
    of the patch. An expectation miss is a JOINT property of the patch, the
    model's chosen default parameter values, and the threshold table in
    TAIL_EXPECT_MS — and the corpus is regenerated by a nondeterministic model on
    every benchmark run. Breaking the ladder on that would make check.sh audio
    fail for reasons unrelated to the change under test, which is how ladders come
    to be skipped. The "seen failing" obligation is discharged by the dead_delay
    and never_decays entries in the deterministic, version-controlled self-test.
    Promote to --strict once two or three corpora have shown the real
    false-positive rate, rather than guessing it now.
    """
    records = json.loads(results_path.read_text(encoding="utf-8"))
    compiling = [r for r in records if r.get("first_try_compiles")]
    print(f"rendering {len(compiling)} compiling patches from {results_path.name}")
    s = analyse_corpus(compiling)

    print(f"\n{s['passed']} passed, {s['failed']} failed, "
          f"{s['unsupported']} unsupported (0-input generators), "
          f"{s['expectation_unmet']} tail expectation unmet")
    for label, why in s["failures"]:
        print(f"  FAIL {label} -- {why}")
    for label, why in s["unmet"]:
        print(f"  TAIL {label} -- {why}")
    if s["unmet"] and not strict:
        print("  (tail expectations are reported, not enforced -- see corpus_main)")
    return 1 if (s["failed"] or (strict and s["expectation_unmet"])) else 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("dsp", nargs="?", help="path to a .dsp file")
    ap.add_argument("--json", action="store_true", help="emit JSON only")
    ap.add_argument("--signal", default="noise",
                    choices=["noise", "sweep", "impulse", "burst"])
    ap.add_argument("--self-test", action="store_true")
    ap.add_argument("--corpus", metavar="RESULTS_JSON",
                    help="render every compiling patch in a benchmark results file")
    ap.add_argument("--strict", action="store_true",
                    help="with --corpus: also exit non-zero on unmet tail expectations")
    ap.add_argument("--expect-tail-ms", type=float, default=None,
                    help="minimum tail in ms this patch must show")
    a = ap.parse_args()

    if a.self_test:
        return self_test()
    if a.corpus:
        return corpus_main(Path(a.corpus), strict=a.strict)
    if not a.dsp:
        ap.error("give a .dsp path, --corpus, or --self-test")

    r = analyse(Path(a.dsp).read_text(encoding="utf-8"), a.signal,
                expect_tail_ms=a.expect_tail_ms)
    if a.json:
        print(json.dumps(r, indent=2))
        return 0 if r.get("rendered") and r["measurement"]["ok"] else 1
    if not r["rendered"]:
        print(f"RENDER FAILED: {r['error']}")
        return 1
    m, f = r["measurement"], r["features"]
    print(f"{'PASS' if m['ok'] else 'FAIL'}  {a.dsp}")
    for reason in m["reasons"]:
        print(f"  ! {reason}")
    print(f"  frames={m['n_frames']} ch={m['n_channels']} peak={m['peak']:.4f} "
          f"rms={m['rms_ratio_db']:+.1f} dB dc={m['dc_offset']:.5f}")
    print("  band gain (dB): " + "  ".join(f"{k}={v:+.1f}" for k, v in f["band_gain_db"].items()))
    print(f"  centroid {f['centroid_in_hz']:.0f} -> {f['centroid_out_hz']:.0f} Hz "
          f"({f['centroid_shift_oct']:+.2f} oct)")
    return 0 if m["ok"] else 1


if __name__ == "__main__":
    sys.exit(main())
