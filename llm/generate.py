#!/usr/bin/env python3
"""
Entry point: natural language prompt -> validated Faust DSL string.

Run modes:
  python generate.py "a low-pass filter"   — CLI, prints Faust to stdout
  python generate.py --json                 — JSON mode, reads request from stdin,
                                             writes response to stdout (legacy)
  python generate.py --prompt "..."         — arg mode, outputs JSON to stdout;
                                             used by the C++ host via juce::ChildProcess
  python generate.py --request-file <path>  — arg mode, request JSON read from a file
                                             instead of a CLI arg; used by the host for
                                             requests too large/structured for argv,
                                             e.g. a refine carrying prior_source (A2)
"""
import datetime
import json
import os
import subprocess
import sys
import tempfile
import traceback
from pathlib import Path
from dotenv import load_dotenv

sys.path.insert(0, str(Path(__file__).parent))
import providers  # noqa: E402
import router  # noqa: E402
import generation_profiles  # noqa: E402
import error_classes  # noqa: E402

load_dotenv(Path(__file__).parent.parent / ".env")

# Resolved at import, AFTER load_dotenv — so PLUGINFORGE_PROVIDER in PluginForge/.env
# reaches the plugin (which shells out to this script) with no C++ change and no
# rebuild. Falls back to "anthropic" when unset, preserving the historical default.
DEFAULT_PROVIDER = providers.resolve_provider()
# Per-provider; see llm/providers.py PROVIDERS. For anthropic this is
# claude-opus-4-8 (bumped from opus-4-6 2026-07-21) — benchmark numbers recorded
# before that date are opus-4-6-era and not directly comparable, see
# docs/prompt_efficacy_study.md.
#
# NOTE: opus-4-7 and later reject temperature/top_p/top_k with a 400. This layer
# passes temperature=None (the parameter is omitted entirely), which is why the
# model bump was a plain string swap. providers.make_generator() raises with an
# actionable message if a caller ever passes a temperature to such a model.
DEFAULT_MODEL = providers.resolve_model(DEFAULT_PROVIDER)

# A/B selector for the effects prompt only (the instrument prompt has no
# presentation variant). Env var, matching PLUGINFORGE_PROVIDER/
# PLUGINFORGE_GENERATION_BUDGET/PLUGINFORGE_PROMPT_LOG's existing idiom here
# rather than a CLI flag, because SYSTEM_PROMPT is resolved once at import
# time and every one of those is read the same way. An unrecognised value
# falls back to "base", the same degrade-don't-fail rule select_prompt()
# uses for an unrecognised `kind`.
_PROMPT_VARIANT_FILES = {
    "base": "system_prompt.txt",
    "presentation": "system_prompt_presentation.txt",
}
PROMPT_VARIANT = os.environ.get("PLUGINFORGE_PROMPT_VARIANT", "base").strip().lower()
if PROMPT_VARIANT not in _PROMPT_VARIANT_FILES:
    PROMPT_VARIANT = "base"

SYSTEM_PROMPT = (Path(__file__).parent / "prompts"
                 / _PROMPT_VARIANT_FILES[PROMPT_VARIANT]).read_text()

# The instrument prompt, and the router that chooses between them.
#
# Two prompts rather than one because the shared file cannot hold both: it is
# 12,006 chars against groq's per-request `prompt_tokens + max_tokens <= 8000`
# ceiling, leaving ~300 tokens of slack, and it mandates hslider() while its
# stdlib block contains no button() -- so a model following it has NO LEGAL WAY
# to emit the `gate` that makes a patch playable. Splitting also buys budget
# rather than spending it: the ceiling is per REQUEST, so these are two
# independent 8,000-token budgets.
#
# SYSTEM_PROMPT keeps its name and its meaning. Every existing caller that reads
# it gets the effects prompt exactly as before.
INSTRUMENT_PROMPT = (Path(__file__).parent / "prompts" / "instrument_prompt.txt").read_text()

SYSTEM_PROMPTS_BY_KIND = {
    router.EFFECT: SYSTEM_PROMPT,
    router.INSTRUMENT: INSTRUMENT_PROMPT,
}


def select_prompt(user_prompt: str, kind: str | None = None) -> tuple[str, str]:
    """Choose the system prompt for a request. Returns (kind, prompt_text).

    `kind` is an explicit override -- the UI's instrument/effect toggle, or a
    benchmark pinning one side. When absent the router decides from the text.
    An unrecognised override falls back to the router rather than raising: a bad
    value on the wire should degrade to today's behaviour, not fail the request.
    """
    if kind not in SYSTEM_PROMPTS_BY_KIND:
        kind = router.classify(user_prompt)
    return kind, SYSTEM_PROMPTS_BY_KIND[kind]

# Default Anthropic client — kept at module level so existing tests can mock it.
# Verified 2026-07-20: anthropic.Anthropic() does not raise when no API key is
# present (it only stores None); the SDK's "Could not resolve authentication
# method" error is raised lazily, at request-send time. So this stays eager —
# no need for a get_client() indirection.
#
# SUBTLE: this is providers.anthropic_client()'s singleton, not a second client.
# Tests patch generate.client.messages.create; because it is the same object, the
# patch is still seen by calls dispatched through the providers module.
client = providers.anthropic_client()


# ── Generation budget (PF-019) ───────────────────────────────────────────────
# The wall-clock ceiling for ONE `--prompt` invocation, covering all attempts.
#
# This number's only real constraint is that it must sit COMFORTABLY BELOW the
# host's subprocess cap (kSubprocessTimeoutMs in host/Source/PromptPanel.cpp).
# Before PF-019 those two were both 120s, so whichever fired first was a race,
# and in the 2026-07-24 P6 battery the host won four times running — killing the
# child before it could emit the ADR-011 JSON that would have said *why*. The
# host cap is now a backstop for a wedged interpreter, not the normal exit path:
# generate.py finishes first and reports a typed reason.
#
# Margin arithmetic, so the next person changing either number can check it:
#   worst case here  = GENERATION_BUDGET_S (all LLM work, budget-enforced)
#                    + one FAUST_VALIDATE_TIMEOUT_S (the last attempt's compile)
#                    + interpreter startup/import
#                    = 140 + 15 + ~2  = ~157s
#   host cap         = 180s
#
# 100->140 (2026-08-13). Confirmed live (logs/prompts.jsonl) that groq's own
# Retry-After hints on a 429 (37s/25s/51s observed) routinely exceeded what a
# 100s budget could ever honour -- providers.Budget.can_sleep() requires
# delay+5s <= remaining, so a real, server-stated wait was refused almost
# every time and a recoverable throttle became a hard rate_limited failure.
# 140s does not fix the root cause (see providers.RateLimited's retry_after
# and PromptPanel::statusForReason() for the actual fix, and
# llm/prompt_builder.py's core-floor fix for reducing how often the bucket
# fills in the first place) -- it only widens the window this budget alone
# can absorb, while keeping the 23s of margin below the host cap the
# arithmetic above still requires.
_DEFAULT_GENERATION_BUDGET_S = 140.0
FAUST_VALIDATE_TIMEOUT_S = 15.0


def generation_budget() -> providers.Budget:
    """One Budget per generation, sized so max_retries attempts fit inside it."""
    try:
        total = float(os.environ.get("PLUGINFORGE_GENERATION_BUDGET",
                                     _DEFAULT_GENERATION_BUDGET_S))
    except ValueError:
        total = _DEFAULT_GENERATION_BUDGET_S
    # 3 attempts inside the total, with the faust compile of each budgeted out.
    per_attempt = max(10.0, (total / 3.0) - FAUST_VALIDATE_TIMEOUT_S / 3.0)
    return providers.Budget(total=total, per_attempt_cap=per_attempt)


# Re-exported so existing callers and tests can keep saying generate.MAX_OUTPUT_TOKENS.
# It lives in providers.py because the bench harnesses need it too and must not have
# to import this module (which resolves a provider and reads the prompt at import).
MAX_OUTPUT_TOKENS = providers.MAX_OUTPUT_TOKENS

# Sent instead of compiler stderr when the previous attempt was cut off. A
# truncated program never reached `faust`, so there is no compile error to feed
# back — and feeding one anyway is precisely the bug this replaces: the model was
# told it made a syntax error it had not made, so it rewrote a program of the same
# length, which truncated again.
_TRUNCATION_HINT = (
    "\n\nYour previous answer was CUT OFF at the output limit before it finished — "
    "it was not wrong, it was too long. Write a SHORTER program this time: no "
    "comments, fewer helper definitions, and prefer a stdlib function over "
    "reimplementing one. Emit only the Faust source."
)


def _classified_error_context(error: str) -> str:
    """Raw compiler error, plus a class-specific repair hint if one is wired.

    Same reasoning as _TRUNCATION_HINT above, for a different signal: raw
    stderr alone can name the defect without naming the fix. Confirmed live
    (logs/prompts.jsonl, 2026-08-13T04:37-04:46Z) that this actually
    matters — "a simple reverb" failed the IDENTICAL delay-range error 3
    attempts in a row, even though system_prompt.txt already has the
    bounded-delay rule, because raw stderr ("interval(0,2.14748e+09,0)")
    names no expression, no line, and no remedy. See
    llm/error_classes.py's RETRY_HINT for which classes are wired and why
    only one is so far — each needs its own evidence, not just a plausible
    guess. A class with no hint gets exactly today's behaviour: raw stderr,
    unchanged.
    """
    klass = error_classes.classify_error(error)
    return error + error_classes.RETRY_HINT.get(klass, "")

# A3: how a refine request's prior Faust source folds into the existing user
# message — no new prompt file, see docs/sessions/002-refine-loop-and-ui-redesign.md
# for the three reasons this was rejected. Mirrored (not imported — that script
# measures the payload shape this constant defines, deliberately without pulling
# in this module's provider/router import chain) in tools/measure_prompt_tokens.py,
# which is what settled that this framing fits the token budget (A1). Keep the two
# in sync if either changes.
_REFINE_PREAMBLE = (
    "The user is asking you to MODIFY the following existing Faust program, "
    "not write a new one from scratch. Preserve its structure and control "
    "names where the request does not require changing them. Return the "
    "COMPLETE modified program.\n\n```faust\n{prior}\n```\n\nApply this "
    "change: "
)

# refine_mode == "surgical" ("Add" in the host UI, PromptPanel.h's 3-mode
# ComboBox). A stricter framing than _REFINE_PREAMBLE above: it is what lets
# generate_json's preflight (below) HARD-FAIL instead of silently dropping to a
# full regen when the prior source doesn't fit the token budget — degrading
# silently would break the "minimal, surgical change" contract the user chose
# "Add" for. "Redo" (_CONTEXT_PREAMBLE) is the mode that degrades.
_SURGICAL_PREAMBLE = (
    "The user is asking you to MODIFY the following existing Faust program with "
    "a MINIMAL, SURGICAL change. Preserve its structure, signal routing, control "
    "names, and behavior EXACTLY — only add or modify the specific element the "
    "request describes. Return the COMPLETE modified program.\n\n```faust\n"
    "{prior}\n```\n\nApply this surgical change: "
)

# refine_mode == "context" ("Redo" in the host UI). The prior program is
# reference material, not a constraint on the shape of the answer — unlike
# surgical mode, restructuring or rewriting it from scratch is fine.
_CONTEXT_PREAMBLE = (
    "The following is the PREVIOUS version of a Faust program, given as "
    "reference only. You are free to REWRITE it from scratch if that better "
    "satisfies the request below — do not preserve its structure or control "
    "names unless doing so actually serves the request.\n\n```faust\n{prior}"
    "\n```\n\nNew request: "
)


def _refine_preamble_for(refine_mode: str | None) -> str:
    """Which preamble folds prior_source in, keyed by the host's refine_mode field.

    None (absent) is the legacy path: an older host, or any request that never
    sets refine_mode, gets exactly the pre-refine_mode _REFINE_PREAMBLE behavior
    — unchanged, so nothing already relying on it moves.
    """
    if refine_mode == "surgical":
        return _SURGICAL_PREAMBLE
    if refine_mode == "context":
        return _CONTEXT_PREAMBLE
    return _REFINE_PREAMBLE


def _call_api(content: str, provider: str, model: str | None = None,
              budget: "providers.Budget | None" = None,
              system_prompt: str | None = None) -> str:
    """Single API dispatch point — delegates to the llm/providers.py registry.

    `system_prompt` defaults to the EFFECTS prompt, so any caller that predates
    the instrument split behaves exactly as it did.
    """
    generate = providers.make_generator(
        provider, system_prompt=system_prompt or SYSTEM_PROMPT, model=model,
        max_tokens=MAX_OUTPUT_TOKENS, budget=budget,
    )
    return generate(content)


def generate_faust(user_prompt: str, error_context: str = "",
                   provider: str = DEFAULT_PROVIDER,
                   model: str | None = None,
                   budget: "providers.Budget | None" = None,
                   truncated: bool = False,
                   kind: str | None = None,
                   prior_source: str | None = None,
                   refine_mode: str | None = None,
                   system_prompt: str | None = None) -> str:
    """One generation attempt.

    `truncated` and `error_context` are mutually exclusive repair signals, and the
    caller must not send both: code that was cut off mid-token never reached the
    compiler, so any stderr in hand belongs to an earlier attempt and would
    misdirect the model.

    `kind` pins the system prompt; without it the router reads `user_prompt`.
    ⚠️ Every attempt in a retry loop must pass the SAME kind. Re-routing on the
    repair text -- which is compiler stderr, not a user request -- would let a
    retry silently switch prompts mid-generation and repair the code against
    rules the first attempt never saw.

    `prior_source`, when non-empty, is folded in ahead of `user_prompt` -- the
    whole point being that it reads as one user message, not a second
    conversation turn (A3). `refine_mode` ("surgical" | "context" | None) picks
    WHICH framing does the folding, via `_refine_preamble_for` — None is the
    legacy `_REFINE_PREAMBLE` path. Every attempt in a retry loop must pass the
    SAME prior_source and refine_mode too, for the same reason as `kind`:
    generate_json decides once, before the loop (A6's preflight), whether it
    fits the token budget at all, against the SAME preamble used here.
    """
    content = user_prompt
    if prior_source:
        content = _refine_preamble_for(refine_mode).format(prior=prior_source) + content
    if truncated:
        content += _TRUNCATION_HINT
    elif error_context:
        content += f"\n\nYour previous output had this compiler error — fix it:\n{error_context}"

    if system_prompt is None:
        _, system_prompt = select_prompt(user_prompt, kind)
    return _call_api(content, provider, model, budget, system_prompt)


def validate_faust(faust_code: str,
                   budget: "providers.Budget | None" = None,
                   profile: "generation_profiles.Profile | None" = None) -> tuple[bool, str]:
    """Returns (is_valid, error_message).

    PF-019: with a Budget, the compiler subprocess timeout is clamped to what is
    left. `faust` on a pathological generated patch is one more way to overrun the
    host's cap, so it counts against the same clock as the LLM calls.
    """
    # SUBTLE: encoding and errors are both load-bearing, and neither is a default.
    # An LLM reply can contain non-ASCII (curly quotes, em dashes, a degree sign in
    # a slider label). When it does, `faust` echoes the offending source line to
    # stderr but cuts it mid-character, emitting a lone 0xe2 — invalid UTF-8.
    # With the default errors="strict", subprocess.run(text=True) then RAISES
    # UnicodeDecodeError instead of returning, so this function never gets to
    # report a compile failure and the ADR-005 retry loop never engages: the whole
    # generation dies on what should have been a recoverable attempt.
    # Reproduced 2026-07-21 against faust on the Arch dev box; regression test in
    # tests/test_faust_validator_unit.py::test_non_utf8_compiler_output.
    timeout = FAUST_VALIDATE_TIMEOUT_S
    if budget is not None:
        timeout = max(2.0, min(timeout, budget.remaining()))
    with tempfile.NamedTemporaryFile(suffix=".dsp", mode="w", encoding="utf-8",
                                     delete=False) as handle:
        handle.write(faust_code)
        source = Path(handle.name)
    output = Path(str(source) + ".cpp")
    metadata_path = Path(str(source) + ".json")
    try:
        result = subprocess.run(
            ["faust", "-json", "-lang", "cpp", str(source), "-o", str(output)],
            capture_output=True, text=True, timeout=timeout,
            encoding="utf-8", errors="replace",
        )
        if result.returncode != 0:
            return False, result.stderr.strip()
        if profile is None:
            return True, ""
        metadata = json.loads(metadata_path.read_text(encoding="utf-8"))
        return _validate_profile_metadata(metadata, profile)
    except subprocess.TimeoutExpired:
        # A patch the compiler cannot finish is an invalid patch, not a crash.
        # Report it as a compile error so the retry loop can feed it back.
        return False, (f"faust compiler did not finish within {timeout:.0f}s — "
                       f"the generated patch is likely pathological "
                       f"(unbounded delay, runaway recursion).")
    except (OSError, ValueError, json.JSONDecodeError) as exc:
        return False, f"Faust metadata validation failed: {exc}"
    finally:
        for path in (source, output, metadata_path):
            if path.exists():
                os.unlink(path)


def _ui_labels(items: list[dict]) -> set[str]:
    labels: set[str] = set()
    for item in items:
        label = item.get("label")
        if isinstance(label, str):
            labels.add(label.strip().lower())
        children = item.get("items")
        if isinstance(children, list):
            labels.update(_ui_labels(children))
    return labels


def _validate_profile_metadata(metadata: dict,
                               profile: generation_profiles.Profile) -> tuple[bool, str]:
    """Deterministic intent checks on compiler-produced facts, never source regexes."""
    inputs = int(metadata.get("inputs", -1))
    outputs = int(metadata.get("outputs", -1))
    labels = _ui_labels(metadata.get("ui") or [])

    if outputs not in (1, 2):
        return False, (f"family {profile.id} requires one or two audio outputs; "
                       f"the compiled patch has {outputs}")
    if profile.kind == "effect" and inputs not in (1, 2):
        return False, (f"family {profile.id} must process one or two audio inputs; "
                       f"the compiled patch has {inputs}")
    if profile.kind == "instrument" and inputs != 0:
        return False, (f"family {profile.id} must generate sound with zero audio inputs; "
                       f"the compiled patch has {inputs}")

    if profile.id in ("synth", "drum_synth"):
        roles = (
            "gate" in labels,
            bool(labels.intersection({"freq", "key"})),
            bool(labels.intersection({"gain", "vel", "velocity"})),
        )
        if not all(roles):
            return False, (f"family {profile.id} requires the complete MIDI voice contract: "
                           "gate, freq or key, and gain, vel, or velocity")

    if profile.id == "granular_effect":
        groups = {
            "grain size": ("grain size", "size"),
            "density": ("density",),
            "position/delay": ("position", "delay"),
            "pitch/spray": ("pitch", "spray"),
            "feedback": ("feedback",),
            "mix": ("mix", "dry/wet", "dry wet"),
        }
        missing = [name for name, terms in groups.items()
                   if not any(any(term in label for term in terms) for label in labels)]
        if missing:
            return False, ("family granular_effect is missing required controls: "
                           + ", ".join(missing))

    return True, ""


def generate_with_retry(user_prompt: str, max_retries: int = 3,
                        provider: str = DEFAULT_PROVIDER,
                        model: str | None = None) -> str:
    """Returns validated Faust code string. Raises RuntimeError after exhausting retries."""
    error_ctx = ""
    truncated = False
    budget = generation_budget()
    for attempt in range(1, max_retries + 1):
        # See the matching comment in generate_json's loop: the classified
        # hint is for what the MODEL sees next, kept separate from error_ctx
        # itself (printed to the user above on failure, unhinted).
        repair_context = _classified_error_context(error_ctx) if error_ctx else ""
        try:
            code = generate_faust(user_prompt, repair_context, provider=provider, model=model,
                                  budget=budget, truncated=truncated)
        except providers.OutputTruncated as exc:
            print(f"[!] Attempt {attempt} was cut off at the output limit: {exc}\n")
            truncated, error_ctx = True, ""
            continue
        truncated = False
        valid, error = validate_faust(code, budget)
        if valid:
            print(f"[+] Valid Faust on attempt {attempt}")
            return code
        print(f"[!] Attempt {attempt} failed:\n{error}\n")
        error_ctx = error
    raise RuntimeError("Failed to generate valid Faust after retries.")


def generate_json(request: dict) -> dict:
    """
    JSON wire-mode entry point called by the C++ host via subprocess.
    Accepts the locked ADR-011 request schema; returns the response schema.

    ADR-011 response, with one ADDITIVE field as of PF-019:

        {"success": bool, "faust_code": str|None, "attempts": int,
         "error": str|None, "reason": str}

    `reason` is one of ok | invalid_faust | truncated | timeout | rate_limited | error.

    `truncated` means the model ran out of output budget mid-program on every
    attempt. It is deliberately distinct from invalid_faust: the code never reached
    the compiler, the model did nothing wrong, and the user-facing advice is
    "ask for something simpler", not "the generator produced bad Faust". Conflating
    the two is what made four consecutive P6 prompts look like generation failures.
    Additive because every existing consumer reads success/faust_code/error and is
    unaffected by an extra key; the host uses it to tell "the provider throttled
    you, wait a moment" apart from "something stalled", which was indistinguishable
    during the 2026-07-24 battery. Treating this as ungated under COLLABORATION.md
    §2 trigger-3: it extends the schema without changing any existing field's
    meaning, and both sides move in this commit.
    """
    prompt = request["prompt"]
    provider = request.get("provider", DEFAULT_PROVIDER)
    # None → providers.resolve_model() picks the selected provider's default, so a
    # request naming a provider but no model can't inherit another provider's model.
    model = request.get("model")
    max_retries = request.get("max_retries", 3)
    budget = request.get("budget") or generation_budget()

    # Routed ONCE, before the loop, from the user's text. Every retry then reuses
    # the same kind — see generate_faust's note on why re-routing per attempt
    # would repair code against rules the first attempt never saw.
    kind, system_prompt = select_prompt(prompt, request.get("kind"))
    try:
        profile, family_source = generation_profiles.resolve(
            prompt, kind, request.get("family")
        )
    except ValueError as exc:
        return _failure(0, "error", str(exc))

    # Dynamic stdlib token headroom optimization (PLUGINFORGE_DYNAMIC_STDLIB=1 default)
    if os.environ.get("PLUGINFORGE_DYNAMIC_STDLIB", "1") != "0":
        try:
            import prompt_builder
            system_prompt = prompt_builder.build_dynamic_prompt(
                prompt, base_system_prompt=system_prompt
            )
        except Exception:
            pass
    system_prompt = generation_profiles.append_brief(system_prompt, profile)

    # refine_mode ("surgical" | "context" | absent): picks which preamble folds
    # prior_source in, both here (for the preflight measurement) and in every
    # generate_faust call below — see _refine_preamble_for. Absent is the legacy
    # path: an older host that never sends this field gets exactly today's
    # _REFINE_PREAMBLE behavior, unchanged.
    refine_mode = request.get("refine_mode") or None

    # A6: decided ONCE, before the loop, same reasoning as `kind` above. Doesn't
    # fit the token budget → the two refine_mode values diverge here:
    #   "surgical" ("Add"): HARD-FAIL. Silently dropping to a full regen would
    #       break the "minimal, surgical change" contract the user chose Add
    #       for — the user must be told, not handed an unrequested rewrite.
    #   "context" ("Redo") or absent (legacy): dropped entirely, never
    #       truncated (a half program teaches the model a syntax error the user
    #       didn't make — the same failure class _TRUNCATION_HINT already
    #       exists to prevent in the other direction).
    prior_source = request.get("prior_source") or None
    prior_source_dropped = False
    if prior_source:
        candidate = _refine_preamble_for(refine_mode).format(prior=prior_source) + prompt
        if not providers.preflight_prior_source(system_prompt, candidate, MAX_OUTPUT_TOKENS):
            if refine_mode == "surgical":
                return _prior_source_refused_response()
            prior_source, prior_source_dropped = None, True

    error_ctx = ""
    truncated = False
    attempt = 0
    for attempt in range(1, max_retries + 1):
        # Classified hint appended only for what the MODEL sees next, not for
        # error_ctx itself -- error_ctx also feeds the final failure/timeout
        # messages below, which are user-facing, not LLM-directed repair
        # text. Same separation _TRUNCATION_HINT already keeps (it is never
        # part of a _failure() call, only ever appended inside
        # generate_faust's own content-building). A no-op when error_ctx is
        # empty (first attempt) or unclassified.
        repair_context = _classified_error_context(error_ctx) if error_ctx else ""
        try:
            code = generate_faust(prompt, repair_context, provider, model, budget,
                                  truncated=truncated, kind=kind,
                                  prior_source=prior_source,
                                  refine_mode=refine_mode,
                                  system_prompt=system_prompt)
        except providers.RateLimited as exc:
            return _failure(attempt, "rate_limited", str(exc), retry_after=exc.retry_after)
        except providers.BudgetExhausted as exc:
            return _failure(attempt, "timeout", str(exc))
        except providers.OutputTruncated as exc:
            # Recoverable, and specifically NOT a compile failure — the code never
            # reached the compiler. Retry asking for a shorter program rather than
            # feeding back stderr that belongs to nothing.
            truncated, error_ctx = True, ""
            if attempt == max_retries:
                return _failure(attempt, "truncated", str(exc))
            if budget.expired():
                return _failure(attempt, "timeout", str(exc))
            continue

        truncated = False
        valid, error = validate_faust(code, budget, profile)
        if valid:
            # `kind` is ADDITIVE, same treatment as `reason` under PF-019: every
            # existing consumer reads success/faust_code/error and is unaffected
            # by an extra key. It tells the host which prompt produced this patch,
            # which is what a UI needs to show the routing decision and let the
            # user override a misroute.
            #
            # NOT on the failure path yet — _failure() is shared by six call sites
            # and threading it through is a separate change. A failed instrument
            # request is currently indistinguishable from a failed effect one.
            response = {"success": True, "faust_code": code, "attempts": attempt,
                       "error": None, "reason": "ok", "kind": kind,
                       "family": profile.id, "family_source": family_source}
            if prior_source_dropped:
                # Additive, same treatment as `kind` above: every existing
                # consumer reads success/faust_code/error and is unaffected by
                # an extra key. Lets PromptPanel tell the user their refine
                # silently became a regeneration instead of staying quiet.
                response["prior_source_dropped"] = True
            return response
        error_ctx = error

        # Don't start an attempt there is no time to finish — returning the last
        # compiler error now beats being killed mid-request with nothing to show.
        if budget.expired() and attempt < max_retries:
            return _failure(attempt, "timeout",
                            f"generation budget exhausted after {attempt} attempt(s). "
                            f"Last compiler error:\n{error_ctx}")

    return _failure(max_retries, "invalid_faust", error_ctx)


def _failure(attempts: int, reason: str, error: str,
             retry_after: float | None = None) -> dict:
    result = {"success": False, "faust_code": None, "attempts": attempts,
              "error": error, "reason": reason}
    if retry_after is not None:
        # Additive, same treatment as `kind`/`prior_source_dropped` elsewhere
        # in this file (PF-019): every existing consumer reads
        # success/faust_code/error/reason and is unaffected by an extra key
        # present only on rate_limited failures. Lets PromptPanel show "try
        # again in 37s" instead of a dead end -- see providers.RateLimited.
        result["retry_after"] = retry_after
    return result


def _prior_source_refused_response() -> dict:
    """Surgical (Add) mode's preflight hard-fail (A6). attempts=0: this is a
    short-circuit before generate_faust is ever called, not a failed attempt.

    `reason` stays "error" -- ADR-011's reason enum is locked to
    ok|invalid_faust|truncated|timeout|rate_limited|error, and this is a new
    failure CLASS, not a new reason value. `prior_source_refused` is the
    additive discriminator, same precedent as `prior_source_dropped` and `kind`
    (PF-019): every existing consumer reads success/faust_code/error/reason and
    is unaffected by the extra key.
    """
    response = _failure(
        0, "error",
        "The prior source is too large to fit the token budget in surgical "
        '("Add") mode, which requires the whole prior program plus the new '
        'request to fit in one message. Choose "Redo" to regenerate with it as '
        "reference instead, or shorten the current patch first.")
    response["prior_source_refused"] = True
    return response


def _missing_api_key_response(message: str | None = None) -> dict:
    """ADR-011 failure shape for a missing/empty credential.

    SUBTLE: the default message is the anthropic one, kept verbatim — the host
    surfaces this string to the user and tests/test_generate_unit.py asserts it
    character-for-character as part of the ADR-011 wire contract. Other providers
    pass their own message (naming their own env var) explicitly.
    """
    return {
        "success": False,
        "faust_code": None,
        "attempts": 0,
        "error": message or ("ANTHROPIC_API_KEY is not set. Add it to PluginForge/.env or "
                             "the plugin's environment."),
        "reason": "no_credentials",
    }


def _exception_response(exc: BaseException) -> dict:
    """ADR-011 failure shape for an unexpected exception in a subprocess mode."""
    reason = "error"
    if isinstance(exc, providers.RateLimited):
        reason = "rate_limited"
    elif isinstance(exc, providers.BudgetExhausted):
        reason = "timeout"
    return {"success": False, "faust_code": None, "attempts": 0,
            "error": str(exc), "reason": reason}


def _prompt_log_path() -> Path | None:
    """Where real user prompts get recorded, or None when logging is off.

    `PLUGINFORGE_PROMPT_LOG` unset -> the default path. Set to a path -> that path.
    Set to 0/off/false/no/"" -> disabled.
    """
    raw = os.environ.get("PLUGINFORGE_PROMPT_LOG")
    if raw is None:
        return Path(__file__).parent.parent / "logs" / "prompts.jsonl"
    if raw.strip().lower() in {"", "0", "off", "false", "no"}:
        return None
    return Path(raw).expanduser()


def log_user_prompt(request: dict, response: dict) -> None:
    """Append one JSONL record per real user generation. PF-014.

    WHY: until 2026-07-28 nothing in this project had ever recorded what a person
    actually typed. Every one of the 25 benchmark prompts was invented by us, so the
    corpus measured our imagination of the product rather than its use, and no
    failure class could be weighted by how often it really occurs.

    WHERE: called only from _run_subprocess_mode, which is the sole path the C++ host
    invokes (--json / --prompt). The bench harnesses call generate_faust /
    generate_with_retry directly and never reach here, so benchmark prompts cannot
    contaminate the record of real ones.

    FAIL-OPEN, deliberately, unlike this project's hooks. A hook exists to stop the
    work; this exists to observe it. A full disk or a read-only path must cost the
    user a log line, never their generation — so every error is swallowed to stderr.
    """
    path = _prompt_log_path()
    if path is None:
        return
    try:
        record = {
            "ts": datetime.datetime.now(datetime.timezone.utc)
                          .isoformat(timespec="seconds").replace("+00:00", "Z"),
            "prompt": request.get("prompt", ""),
            "provider": request.get("provider", DEFAULT_PROVIDER),
            "model": request.get("model") or DEFAULT_MODEL,
            "success": bool(response.get("success")),
            "reason": response.get("reason", "error"),
            "attempts": response.get("attempts", 0),
            "faust_code": response.get("faust_code"),
            "error": response.get("error"),
        }
        path.parent.mkdir(parents=True, exist_ok=True)
        # One open/append/close per generation: generations are seconds apart and
        # arrive one process at a time, so a single O_APPEND write is atomic enough
        # and leaves no handle to lose on a crash.
        with path.open("a", encoding="utf-8") as fh:
            fh.write(json.dumps(record, ensure_ascii=False) + "\n")
    except Exception as exc:  # noqa: BLE001 — see FAIL-OPEN above
        print(f"[prompt-log] not recorded: {exc}", file=sys.stderr)


def _read_request_file(path: str) -> dict:
    return json.loads(Path(path).read_text(encoding="utf-8"))


def _run_subprocess_mode(build_request):
    """
    Shared body for the --json and --prompt ADR-011 subprocess entry points.

    `build_request` is a zero-arg callable that produces the request dict for
    generate_json() — called only after the API-key precheck passes, so a
    malformed stdin payload (--json mode) is caught by the try/except below
    rather than escaping as an uncaught exception before the precheck.

    Prints exactly one JSON line to stdout (the ADR-011 contract) and always
    exits 0 — the host parses the JSON regardless of exit code, and a nonzero
    exit would mask the structured error the JSON already carries. All
    diagnostics/tracebacks go to stderr, never stdout.
    """
    # Provider-aware precheck: names whichever credential the selected provider
    # actually needs, and is skipped entirely for local ollama (which needs none).
    credential_error = providers.check_credentials(DEFAULT_PROVIDER)
    if credential_error:
        legacy_text = DEFAULT_PROVIDER == "anthropic"
        print(json.dumps(_missing_api_key_response(None if legacy_text else credential_error)))
        return
    request = None
    try:
        request = build_request()
        response = generate_json(request)
        log_user_prompt(request, response)          # PF-014
        print(json.dumps(response))
    except Exception as exc:  # noqa: BLE001 - convert to ADR-011 JSON, never a stdout traceback
        traceback.print_exc(file=sys.stderr)
        response = _exception_response(exc)
        # A prompt that blew up is the most interesting kind to have recorded. Only
        # skipped when build_request() itself failed, i.e. there is no prompt to log.
        if request is not None:
            log_user_prompt(request, response)
        print(json.dumps(response))


if __name__ == "__main__":
    # Free-only guard. Lives here rather than in the library functions so that
    # every path that can actually spend money is covered (the plugin invokes this
    # script as a subprocess) without blocking unit tests that drive the functions
    # directly with a mocked transport. See llm/providers.py.
    _subprocess_mode = ("--json" in sys.argv or "--prompt" in sys.argv
                        or "--request-file" in sys.argv)
    try:
        providers.assert_free(DEFAULT_PROVIDER)
    except providers.PaidProviderError as exc:
        if _subprocess_mode:
            # ADR-011: exactly one JSON line on stdout, exit 0 — the host reads the
            # structured error and shows it as a status label.
            print(json.dumps(_exception_response(exc)))
            sys.exit(0)
        print(f"[!] {exc}", file=sys.stderr)
        sys.exit(1)

    if "--json" in sys.argv:
        _run_subprocess_mode(lambda: json.loads(sys.stdin.read()))
    elif "--prompt" in sys.argv:
        idx = sys.argv.index("--prompt")
        prompt_arg = sys.argv[idx + 1] if idx + 1 < len(sys.argv) else ""
        _run_subprocess_mode(lambda: {"prompt": prompt_arg})
    elif "--request-file" in sys.argv:
        idx = sys.argv.index("--request-file")
        path_arg = sys.argv[idx + 1] if idx + 1 < len(sys.argv) else ""
        _run_subprocess_mode(lambda: _read_request_file(path_arg))
    else:
        prompt = " ".join(a for a in sys.argv[1:] if not a.startswith("-")) \
                 or "a warm analog chorus with rate and depth controls"
        print(f"Prompt: {prompt}\n")
        result = generate_with_retry(prompt)
        print("\n── Generated Faust ──")
        print(result)
