"""bench/capture.py — interactive-session capture harness.

WHY THIS EXISTS: PF-072 (refine collapsed a full patch to a single dry/wet
knob), PF-073 (audible click on the 2nd-generation DSP swap), PF-074
(a generated instrument emitted NaN/Inf during play) and PF-075 (a chord
sounded like ~5 voices from a strictly-mono engine) are four medium-severity
findings from session 017's interactive REAPER pass, and all four are stuck
the same way: nobody wrote down which generation produced them, so there is
no patch to re-open and no repro to hand to GRAME or a test. One human sitting
at a DAW is the scarce resource (COLLABORATION.md §1's listening pass competes
for the same hour) — this exists so that hour produces more than one anecdote.

WHAT IT CAPTURES: on every real `generate` action (llm/generate.py's
subprocess mode calls `capture_generation()` right next to the existing PF-014
`log_user_prompt`), a JSON record — prompt, refine_mode, provider/model, the
accepted Faust source, its declared UI parameters, and a render-oracle
measurement of a short offline probe — plus (for non-instrument patches) the
probe's own WAV, to `bench/captures/<date>/NNN-<label>.json[.wav]`.

SCOPE NOTE: "param snapshot" here means the parameters DECLARED by the
accepted Faust source (label/init/min/max/step, read back via `faust -json`
— the same compiler pass validate_faust() already runs, just not discarded
this time), not live host-side runtime values. Getting the latter would mean
tapping PluginProcessor's parameter state, which is real-time audio-thread
adjacent territory and its own consult trigger (COLLABORATION.md §2 trigger-3,
ADR-041 step 6) — explicitly out of scope for this harness. Declared defaults
are what PF-072 (does the compiled patch even HAVE the controls it should)
needs; they cannot help PF-074/PF-075, which are about behaviour after a knob
moves during play. That gap is real and is not this module's to close.

The "rendered tail" is an OFFLINE re-render of the accepted source through
bench/render_oracle.py's existing noise probe — not a tap on the live audio
thread. Zero-input (instrument) patches raise UnsupportedPatch there by
design (see render_oracle.UnsupportedPatch); this harness records that as a
skip rather than treating it as a failure.

OFF BY DEFAULT (`PLUGINFORGE_CAPTURE`): unlike PF-014's prompt log, this does
a real faust2sndfile compile + render per generation, adding a few seconds to
every generate call. Fine for a deliberate interactive session; not something
every dev/test run should pay silently. FAIL-OPEN throughout, same contract as
log_user_prompt: a capture that cannot be written must never fail the
generation it was trying to record.
"""
from __future__ import annotations

import datetime
import json
import os
import re
import subprocess
import sys
import tempfile
import time
import traceback
from dataclasses import asdict
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import render_oracle  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
CAPTURE_ROOT = ROOT / "bench" / "captures"

# Short: this is a coarse liveness probe (NaN/silence/DC/gain), not a fidelity
# measurement — render_oracle's own 2 s default is for the corpus check, not
# for something running once per interactive generation on top of the LLM
# call it is already waiting on.
_PROBE_DUR_S = 0.5

# Below this many seconds of remaining budget, skip extract_params/_render_tail
# entirely rather than attempt a probe that can't safely finish — see
# capture_generation()'s `budget_s` parameter.
_MIN_CAPTURE_BUDGET_S = 3.0

# Bound on how long _next_id()'s id-selection lock is worth waiting for
# (see _acquire_lock below). Two concurrent interactive generations sharing
# one session_dir is this harness's own capture-session checklist's expected
# scenario, not a misuse — contention here should be brief.
_ID_LOCK_TIMEOUT_S = 2.0


def enabled() -> bool:
    raw = os.environ.get("PLUGINFORGE_CAPTURE", "")
    return raw.strip().lower() in {"1", "true", "on", "yes"}


def _slug(text: str, maxlen: int = 30) -> str:
    text = re.sub(r"[^a-z0-9]+", "-", text.strip().lower()).strip("-")
    return text[:maxlen].rstrip("-") or "capture"


def _session_dir(now: datetime.date | None = None) -> Path:
    d = CAPTURE_ROOT / (now or datetime.date.today()).isoformat()
    d.mkdir(parents=True, exist_ok=True)
    return d


def _pid_alive(pid: int) -> bool:
    """Same liveness check as bench/run_benchmark.py's _pid_alive() (PF-025,
    ~line 63) — reimplemented locally (six lines) rather than imported, so
    this module (imported on every capture-enabled generation) doesn't take
    on a benchmark script's argparse/provider-table import-time cost for it.
    """
    try:
        os.kill(pid, 0)
    except ProcessLookupError:
        return False
    except PermissionError:
        return True  # exists, owned by someone else
    return True


def _acquire_lock(lock_path: Path, timeout: float) -> int | None:
    """Best-effort exclusive lock. Same O_EXCL-create-plus-stale-pid-reclaim
    primitive as bench/run_benchmark.py's acquire_lock() (PF-025, ~lines
    73-109) — reused rather than reinvented. NOT reused: its "refuse
    outright" semantics, which fit excluding a whole second benchmark RUN;
    this guards a few lines of glob+reserve that two interactive generations
    submitted moments apart are expected to briefly contend for. So
    contention here is retried with a short bounded backoff instead of
    raising, and — this module's FAIL-OPEN contract (see module docstring) —
    a lock that cannot be acquired within `timeout` is silently skipped
    (returns None) rather than blocking a generation indefinitely: a missed
    lock here degrades to the pre-existing, visible, non-corrupting
    collision, never to a hang or a failed generation.
    """
    deadline = time.monotonic() + timeout
    while True:
        try:
            fd = os.open(lock_path, os.O_CREAT | os.O_EXCL | os.O_WRONLY, 0o644)
        except FileExistsError:
            try:
                held_pid = int(lock_path.read_text().strip() or -1)
            except (OSError, ValueError):
                held_pid = -1
            if held_pid > 0 and not _pid_alive(held_pid):
                lock_path.unlink(missing_ok=True)
                continue
            if time.monotonic() >= deadline:
                return None
            time.sleep(0.05)
            continue
        try:
            os.write(fd, str(os.getpid()).encode("ascii"))
        except OSError:
            pass
        return fd


def _release_lock(lock_path: Path, fd: int) -> None:
    try:
        os.close(fd)
    finally:
        lock_path.unlink(missing_ok=True)


def _next_id(session_dir: Path) -> str:
    """Zero-padded 3-digit sequence, highest existing + 1.

    Lock-protected: two generations running concurrently against the same
    session_dir — this harness's own capture-session checklist's expected
    scenario, not a misuse — used to be able to both read the same "highest
    existing" number and pick the same next one. Under the lock (best-effort;
    see _acquire_lock), the chosen number is reserved immediately, by
    touching a marker that matches this function's own glob pattern, before
    the lock is released — so even a caller that could not get the lock in
    time (FAIL-OPEN skip) still sees the reservation and picks the next one
    up. capture_generation() removes the marker once the real file for this
    id exists.
    """
    lock_path = session_dir / ".next_id.lock"
    fd = _acquire_lock(lock_path, _ID_LOCK_TIMEOUT_S)
    try:
        n = 0
        for p in session_dir.glob("[0-9][0-9][0-9]-*"):
            try:
                n = max(n, int(p.name[:3]))
            except ValueError:
                continue
        cap_id = f"{n + 1:03d}"
        (session_dir / f"{cap_id}-.reserved").touch(exist_ok=True)
        return cap_id
    finally:
        if fd is not None:
            _release_lock(lock_path, fd)


def extract_params(faust_code: str, timeout: float = 30.0) -> list[dict] | None:
    """Flatten `faust -json`'s UI tree into one list of param declarations.

    Returns None on any compiler failure — a capture with no param list is
    still useful; a capture harness that can crash the generation it is
    supposed to be observing helps nobody. See validate_faust() in
    llm/generate.py for the same compile step, not reused directly because
    it discards the metadata this needs.
    """
    try:
        with tempfile.TemporaryDirectory() as td:
            d = Path(td)
            src = d / "p.dsp"
            src.write_text(faust_code, encoding="utf-8")
            result = subprocess.run(
                ["faust", "-json", str(src), "-o", "/dev/null"],
                cwd=d, capture_output=True, text=True,
                encoding="utf-8", errors="replace", timeout=timeout,
            )
            meta_path = d / "p.dsp.json"
            if result.returncode != 0 or not meta_path.exists():
                return None
            meta = json.loads(meta_path.read_text(encoding="utf-8"))
    except Exception:
        return None

    items: list[dict] = []
    leaf_types = {"vslider", "hslider", "nentry", "button", "checkbox",
                  "vbargraph", "hbargraph"}

    def walk(nodes):
        for node in nodes or []:
            if node.get("type") in leaf_types:
                items.append({k: node[k] for k in
                             ("type", "label", "shortname", "address",
                              "init", "min", "max", "step", "meta")
                             if k in node})
            walk(node.get("items"))

    walk(meta.get("ui"))
    return items


def _render_tail(faust_code: str, out_wav_path: Path,
                  timeout_s: float | None = None) -> dict:
    """Offline re-render of the accepted source (render_oracle's noise probe),
    never a live audio-thread tap. Returns a dict describing what happened —
    a skip is a normal outcome (e.g. a zero-input instrument), not an error.

    `timeout_s`, when given, bounds the TOTAL time this call is allowed to
    spend — not render_oracle.render()'s own per-subprocess default.
    render_oracle.render_many() (bench/render_oracle.py:356-411) runs the
    faust2sndfile BUILD and the render RUN as two separate subprocess.run()
    calls that each get the SAME `timeout` value (~lines 386 and 399), so the
    worst case for one `timeout` value is 2x it. Halving `timeout_s` here
    keeps the combined worst case within the budget the caller actually has
    left before the host's kill deadline (see llm/generate.py's
    _capture_budget_s()), instead of inheriting render_oracle's own
    un-overridden 120s+120s standalone defaults.
    """
    try:
        signal = render_oracle.test_signal("noise", dur=_PROBE_DUR_S)
        if timeout_s is None:
            x, y = render_oracle.render(faust_code, signal)
        else:
            per_call_timeout = max(1.0, timeout_s / 2.0)
            x, y = render_oracle.render(faust_code, signal, timeout=int(per_call_timeout))
    except render_oracle.UnsupportedPatch as exc:
        return {"skipped": True, "reason": f"zero-input patch: {exc}"}
    except render_oracle.RenderError as exc:
        return {"skipped": True, "reason": f"render failed: {exc}"}
    except Exception as exc:  # noqa: BLE001 - fail-open, this is a bonus, not the point
        return {"skipped": True, "reason": f"unexpected: {exc}"}

    try:
        import scipy.io.wavfile as wav
        wav.write(str(out_wav_path), render_oracle.SR, y.astype("float32"))
        wav_written = True
    except Exception as exc:  # noqa: BLE001
        wav_written = False
        wav_error = str(exc)

    m = render_oracle.measure(x, y)
    result = {"skipped": False, "sr": render_oracle.SR,
              "duration_s": round(y.shape[0] / render_oracle.SR, 3),
              "measurement": asdict(m)}
    if wav_written:
        result["path"] = out_wav_path.name
    else:
        result["wav_error"] = wav_error
    return result


def capture_generation(request: dict, response: dict,
                        budget_s: float | None = None) -> str | None:
    """Entry point called from llm/generate.py's subprocess mode. Returns the
    capture id, or None when capture is off or the record could not be
    written (never raises — see module docstring's FAIL-OPEN contract).

    `budget_s`, when given by the caller (llm/generate.py's
    _capture_budget_s()), is the number of seconds left before the host's
    kill deadline — see that function's docstring for why this now matters,
    not just whether capture raises. Direct callers (tests, other scripts)
    that omit it get this module's own standalone defaults, unchanged.
    """
    if not enabled():
        return None
    try:
        return _capture_generation_unchecked(request, response, budget_s=budget_s)
    except Exception as exc:  # noqa: BLE001 - FAIL-OPEN, same contract as PF-014's log_user_prompt
        traceback.print_exc(file=sys.stderr)
        print(f"[capture] not recorded: {exc}", file=sys.stderr)
        return None


def _capture_generation_unchecked(request: dict, response: dict,
                                   budget_s: float | None = None) -> str:
    session_dir = _session_dir()
    prompt = request.get("prompt", "")
    refine_mode = request.get("refine_mode")
    # refine_mode reaches this call from the request JSON same as `prompt` --
    # unlike prompt (always run through _slug() below), refine_mode was
    # interpolated into the output filename RAW, a path-traversal-shaped bug
    # if an unexpected value (e.g. containing "/" or "..") ever reached it.
    # Sanitize it the same way prompt already is; the raw value is still kept
    # in `record["refine_mode"]` below for data fidelity.
    refine_mode_slug = _slug(refine_mode) if refine_mode else None
    label = "-".join(b for b in (_slug(prompt), refine_mode_slug) if b) or "capture"
    cap_id = _next_id(session_dir)
    stem = f"{cap_id}-{label}"[:80]

    record: dict = {
        "capture_id": cap_id,
        "ts": datetime.datetime.now(datetime.timezone.utc)
                      .isoformat(timespec="seconds").replace("+00:00", "Z"),
        "prompt": prompt,
        "kind": response.get("kind") or request.get("kind"),
        "refine_mode": refine_mode,
        "prior_source_present": bool(request.get("prior_source")),
        "provider": request.get("provider") or "?",
        "model": request.get("model") or "?",
        "success": bool(response.get("success")),
        "reason": response.get("reason"),
        "attempts": response.get("attempts"),
        "error": response.get("error"),
        "faust_source": response.get("faust_code"),
        "params": None,
        "param_count": None,
        "audio_tail": None,
    }

    code = response.get("faust_code")
    if code:
        if budget_s is not None and budget_s < _MIN_CAPTURE_BUDGET_S:
            # Not enough of the host's kill deadline left to safely run a
            # faust compile + render (see llm/generate.py's
            # _capture_budget_s()). Recording the rest of `record` without
            # params/audio is still useful and, critically, fast — the whole
            # point of this branch is to get this process back to printing
            # and exiting well inside the host's timeout.
            record["audio_tail"] = {
                "skipped": True,
                "reason": f"insufficient time remaining ({budget_s:.1f}s) to "
                          "safely probe within the host's kill deadline",
            }
        else:
            # Split whatever time we have between the two subprocess calls
            # extract_params()/_render_tail() make; when budget_s is None
            # (direct callers, e.g. this module's own tests) both keep their
            # standalone defaults (30s / render_oracle's 120s+120s).
            if budget_s is None:
                extract_timeout, render_timeout = 30.0, None
            else:
                extract_timeout = max(2.0, min(30.0, budget_s * 0.15))
                render_timeout = max(2.0, budget_s - extract_timeout)
            params = extract_params(code, timeout=extract_timeout)
            record["params"] = params
            record["param_count"] = len(params) if params is not None else None
            record["audio_tail"] = _render_tail(
                code, session_dir / f"{stem}.wav", timeout_s=render_timeout)

    json_path = session_dir / f"{stem}.json"
    json_path.write_text(json.dumps(record, indent=2, ensure_ascii=False), encoding="utf-8")
    # The id is now committed to a real file; drop _next_id()'s reservation
    # marker for it (best-effort — a leftover marker only ever costs one
    # extra, harmless glob match for the next call, never a collision).
    try:
        (session_dir / f"{cap_id}-.reserved").unlink(missing_ok=True)
    except OSError:
        pass
    try:
        shown_path = json_path.relative_to(ROOT)
    except ValueError:
        shown_path = json_path  # CAPTURE_ROOT overridden outside ROOT (e.g. tests)
    print(f"[capture] {shown_path}", file=sys.stderr)
    return cap_id
