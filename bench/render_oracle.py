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

Two layers, kept separate on purpose:

  measure()   — objective safety + signal statistics. No reference to the prompt.
                NaN/Inf, DC offset, silence, runaway gain, peak. This is the
                automatable half of the P6 battery (docs/p6_test_battery.md) and
                it is a pass/fail gate.

  features()  — spectral descriptors used to ask whether the patch matches its
                description: per-band gain vs the input, spectral centroid shift,
                and crest-factor change. This is evidence, not a verdict.

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
    else:
        raise ValueError(f"unknown test signal: {kind}")
    return x.astype(np.float32)


def render(dsp_source: str, signal: np.ndarray | None = None,
           sr: int = SR, timeout: int = 120) -> tuple[np.ndarray, np.ndarray]:
    """Compile `dsp_source` with faust2sndfile and render `signal` through it.

    Returns (input, output) as float64 arrays. Raises RenderError on any failure —
    callers must treat that as "no audio evidence", never as "silence".
    """
    if shutil.which("faust2sndfile") is None:
        raise RenderError("faust2sndfile not on PATH (part of the Faust install)")
    if signal is None:
        signal = test_signal()

    n_in, n_out = patch_arity(dsp_source)
    if n_in == 0:
        raise UnsupportedPatch(
            f"zero-input patch ({n_in} in, {n_out} out) — generators are outside "
            "this harness; see UnsupportedPatch")

    with tempfile.TemporaryDirectory() as td:
        d = Path(td)
        (d / "patch.dsp").write_text(dsp_source, encoding="utf-8")
        wav.write(str(d / "in.wav"), sr, signal)

        build = subprocess.run(["faust2sndfile", "patch.dsp"], cwd=d,
                               capture_output=True, text=True,
                               encoding="utf-8", errors="replace", timeout=timeout)
        binary = d / "patch"
        if not binary.exists():
            raise RenderError(
                f"build failed (exit {build.returncode}, no binary produced; "
                f"dir: {sorted(p.name for p in d.iterdir())}): "
                f"{_diagnostic_tail(build.stderr, build.stdout)}")

        run = subprocess.run([str(binary), "in.wav", "out.wav"], cwd=d,
                             capture_output=True, text=True,
                             encoding="utf-8", errors="replace", timeout=timeout)
        out = d / "out.wav"
        if not out.exists():
            raise RenderError(
                f"render failed (exit {run.returncode}, no out.wav): "
                f"{_diagnostic_tail(run.stderr, run.stdout)}")

        with _quiet_wav():
            _, y = wav.read(str(out))

    x = signal.astype(np.float64)
    y = np.atleast_2d(y.astype(np.float64))
    if y.ndim == 1 or y.shape[0] < y.shape[1]:
        y = y.reshape(-1, 1) if y.ndim == 1 else y.T
    return x, y


def measure(x: np.ndarray, y: np.ndarray) -> Measurement:
    """Objective gates. This is the automatable half of the P6 battery."""
    m = Measurement(ok=True, n_frames=int(y.shape[0]), n_channels=int(y.shape[1]))

    m.has_nan_inf = bool((~np.isfinite(y)).any())
    if m.has_nan_inf:
        # Everything downstream is meaningless once the buffer is poisoned.
        m.ok = False
        m.reasons.append("output contains NaN or Inf")
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
    # A patch may legitimately boost, but +40 dB on unity-ish input is runaway
    # feedback, which is the failure the P6 battery hit on unbounded delays.
    if m.rms_ratio_db > 40:
        m.ok = False
        m.reasons.append(f"runaway gain ({m.rms_ratio_db:+.1f} dB vs input)")
    if m.peak > 100:
        m.ok = False
        m.reasons.append(f"peak {m.peak:.1f} far outside plausible range")
    if m.dc_offset > 0.05:
        m.ok = False
        m.reasons.append(f"DC offset {m.dc_offset:.3f}")
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


def analyse(dsp_source: str, signal_kind: str = "noise") -> dict:
    """Full pipeline. Returns a JSON-serialisable report."""
    try:
        x, y = render(dsp_source, test_signal(signal_kind))
    except UnsupportedPatch as exc:
        return {"rendered": False, "unsupported": True, "error": str(exc)}
    except RenderError as exc:
        return {"rendered": False, "unsupported": False, "error": str(exc)}
    m, f = measure(x, y), features(x, y)
    return {"rendered": True, "measurement": asdict(m), "features": asdict(f)}


# --- self-test -------------------------------------------------------------
# Runs three patches with known-correct answers. If these drift, the oracle is
# lying and every number built on it is void.
_SELF_TEST = {
    "lowpass": ('import("stdfaust.lib");\n'
                'process = fi.resonlp(1000, 0.707, 1.0), fi.resonlp(1000, 0.707, 1.0);',
                "800-1200 near -3 dB, 4000-8000 strongly attenuated"),
    "gain": ('import("stdfaust.lib");\nprocess = _*0.5, _*0.5;',
             "flat: every band near -6 dB"),
    "silent": ('import("stdfaust.lib");\nprocess = _*0, _*0;',
               "must FAIL the silence gate"),
}


def self_test() -> int:
    failures = 0
    for name, (src, expect) in _SELF_TEST.items():
        r = analyse(src)
        print(f"\n=== {name} — expect: {expect}")
        if not r["rendered"]:
            print(f"  RENDER FAILED: {r['error']}")
            failures += 1
            continue
        m, f = r["measurement"], r["features"]
        print(f"  ok={m['ok']} reasons={m['reasons']} rms={m['rms_ratio_db']:+.1f} dB")
        print("  bands: " + "  ".join(f"{k}:{v:+.1f}" for k, v in f["band_gain_db"].items()))

        if name == "lowpass":
            corner, high = f["band_gain_db"]["800-1200"], f["band_gain_db"]["4000-8000"]
            good = m["ok"] and -5 < corner < -1 and high < -20
            print(f"  -> {'PASS' if good else 'FAIL'} (corner {corner:+.1f}, 4-8k {high:+.1f})")
            failures += not good
        elif name == "gain":
            good = m["ok"] and all(-7 < v < -5 for v in f["band_gain_db"].values())
            print(f"  -> {'PASS' if good else 'FAIL'}")
            failures += not good
        elif name == "silent":
            good = (not m["ok"]) and m["is_silent"]
            print(f"  -> {'PASS' if good else 'FAIL'} (silence gate must fire)")
            failures += not good
    print(f"\n{'ALL PASS' if not failures else f'{failures} FAILED'}")
    return 1 if failures else 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("dsp", nargs="?", help="path to a .dsp file")
    ap.add_argument("--json", action="store_true", help="emit JSON only")
    ap.add_argument("--signal", default="noise", choices=["noise", "sweep", "impulse"])
    ap.add_argument("--self-test", action="store_true")
    a = ap.parse_args()

    if a.self_test:
        return self_test()
    if not a.dsp:
        ap.error("give a .dsp path, or --self-test")

    r = analyse(Path(a.dsp).read_text(encoding="utf-8"), a.signal)
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
