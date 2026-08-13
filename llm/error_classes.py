#!/usr/bin/env python3
"""llm/error_classes.py — Shared Faust-compile-error classification rules.

Extracted from bench/classify_failures.py (2026-08-13) so llm/generate.py's
production retry loop and the benchmark harness classify errors with the
SAME rule table, rather than two copies that can drift apart -- exactly the
argument bench/classify_failures.py's own module docstring already makes
against reimplementing score_efficacy.is_transport_error. bench/ imports
from here, not the other way around: bench/score_efficacy.py already
inserts llm/ onto sys.path (llm/ has never depended on bench/), so this
keeps the existing dependency direction rather than inverting it.

Only the taxonomy (class names, fix hints, the regex rule table) lives
here. The bench-specific mechanics -- Failure records, transport detection,
undefined-symbol program-aware splitting, reporting -- stay in
bench/classify_failures.py, which is bench's job, not generate.py's.
"""
from __future__ import annotations

import re

# ── The taxonomy ──────────────────────────────────────────────────────────────
#
# Ordered most-specific first. A Faust error line can satisfy more than one rule
# ("invalid delay parameter range" also contains "invalid"), so order is load-bearing
# and the first match wins.
#
# Each class names a DISTINCT prompt defect, because the point of this module is to
# tell you what to change. If two classes would be fixed by the same edit, they should
# be one class.

OK = "ok"
TRANSPORT = "transport"          # never reached the compiler; not a generation outcome
TRUNCATED = "truncated"          # cut off at the output limit; an output-budget defect
HALLUCINATED_SYMBOL = "hallucinated_symbol"
UNBOUND_VARIABLE = "unbound_variable"
ROUTING_ARITY = "routing_arity"
DELAY_RANGE = "delay_range"
RECURSION_CYCLE = "recursion_cycle"
DUPLICATE_SYMBOL = "duplicate_symbol"
SYNTAX = "syntax"                # carries the unexpected token in .detail
TYPE_MISMATCH = "type_mismatch"
INCOMPLETE = "incomplete"
UNCLASSIFIED = "unclassified"

# Fix hints are part of the taxonomy, not decoration: a class whose fix nobody can state
# is a class that will never be acted on. Terse, benchmark-table labels for a human
# reading bench/classify_failures.py's report -- NOT what gets fed back to the LLM;
# see RETRY_HINT below for that.
FIX_HINT = {
    HALLUCINATED_SYMBOL: "invented/mis-namespaced stdlib function — namespace rule",
    UNBOUND_VARIABLE: "used a name it never bound — function-parameter rule",
    ROUTING_ARITY: "split/merge (<: :>) arity — routing-algebra section",
    DELAY_RANGE: "delay max must be a compile-time constant — bounded-delay rule",
    RECURSION_CYCLE: "~ loop with no delay in the feedback path — recursion example",
    DUPLICATE_SYMBOL: "symbol defined twice — ADR-009 duplicate-process rule",
    SYNTAX: "malformed program — often a construct the prompt recommends but never shows",
    TYPE_MISMATCH: "signal/number confusion",
    INCOMPLETE: "empty or near-empty program",
    TRUNCATED: "output budget, not the model's competence",
    TRANSPORT: "request never produced a program — exclude from compile rate",
    UNCLASSIFIED: "unrecognised — add a rule if it recurs",
}

_RULES: list[tuple[str, re.Pattern]] = [
    # Arity FIRST: "2 outputs must equal 1 input" would otherwise be caught by nothing
    # specific and fall through to unclassified, and it is one of PF-024's four.
    # Two wordings, both real. BUGS.md paraphrases the P6 failure as "2 outputs must
    # equal 1 input"; Faust 2.85.5 actually emits "The number of outputs [2] of A must
    # be equal to the number of inputs [1] of ...". Both are matched — the paraphrase
    # because it is what the registry recorded, the real one because it is what the
    # compiler says.
    (ROUTING_ARITY, re.compile(
        r"outputs?\s+must\s+equal|inputs?\s+must\s+equal"
        r"|number of (outputs|inputs)|sequential composition|i/?o mismatch", re.I)),
    # Likewise two wordings. The P6 run recorded "invalid delay parameter range:
    # interval(0,2.1e9,0)". Reproducing an unbounded delay on 2.85.5 gives instead
    # "possible negative values of : int(min(Delay(...)))  used in delay expression".
    # Same defect — a non-constant or unclamped delay argument — so same class. Missing
    # the second form would have silently filed it as `unclassified`.
    (DELAY_RANGE, re.compile(
        r"invalid delay parameter range|invalid delay"
        r"|used in delay expression|negative values of\s*:\s*int\(min\(Delay", re.I)),
    (RECURSION_CYCLE, re.compile(r"endless evaluation cycle|evaluation cycle", re.I)),
    (DUPLICATE_SYMBOL, re.compile(r"multiple definitions of symbol|redefinition", re.I)),
    (HALLUCINATED_SYMBOL, re.compile(
        r"undefined symbol|cannot find symbol|no such (function|symbol)", re.I)),
    (TYPE_MISMATCH, re.compile(r"type mismatch|cannot (implicitly )?convert|incompatible types", re.I)),
    # SYNTAX last among compiler rules: it is the least informative, and several of the
    # rules above also emit the word "error".
    (SYNTAX, re.compile(r"syntax error|parse error|unexpected", re.I)),
]


def classify_error(error: str) -> str:
    """Return the failure class for a raw compiler/validator error string.

    A narrower sibling of bench/classify_failures.py's classify(): no
    transport detection (needs the full request record, which generate.py's
    compile-error path doesn't have and doesn't need -- a transport failure
    never reaches this function, it returns before validate_faust() runs),
    no undefined-symbol program-aware splitting (bench's
    _split_undefined_symbol needs the generated code to distinguish
    HALLUCINATED_SYMBOL from UNBOUND_VARIABLE; that distinction has no
    retry hint wired yet, so this always reports HALLUCINATED_SYMBOL for
    both, same as this rule table did before the split existed). Good
    enough to answer "is this class X" for RETRY_HINT's classes; anything
    else classifies fine for bookkeeping but has no hint, which
    generate.py treats as today's status quo (raw stderr only).
    """
    err = (error or "").strip()
    if not err:
        return UNCLASSIFIED
    low = err.lower()
    if "cut off at the output limit" in low or "truncat" in low:
        return TRUNCATED
    for klass, pattern in _RULES:
        if pattern.search(err):
            return klass
    return UNCLASSIFIED


# ── Retry-feedback hints ────────────────────────────────────────────────────
#
# DELIBERATELY separate from FIX_HINT above: FIX_HINT is a terse label for a
# human reading a benchmark table ("delay max must be a compile-time
# constant"); a retry hint needs the concrete REWRITE the model can act on,
# pinned to the error it just made -- the same pattern generate.py's own
# _TRUNCATION_HINT already proved for a different failure class. Ship a
# class here only once there is evidence the raw compiler stderr alone
# isn't enough for the model to self-correct.
#
# DELAY_RANGE is the first, evidenced live (logs/prompts.jsonl,
# 2026-08-13T04:46:48Z): "a simple reverb" failed the IDENTICAL delay-range
# error 3 attempts in a row, even though system_prompt.txt already carries
# the bounded-delay rule (system_prompt.txt:60-63) and four correctly-
# bounded few-shots (:171-228) — the model saw the rule and ignored it
# three times because the retry feedback was raw, unclassified stderr
# ("interval(0,2.14748e+09,0)": no expression, no line, no remedy). Text
# below restates the rule from system_prompt.txt:60-63 plus the bounded
# pattern from its stereo-delay few-shot (:172-176), not a paraphrase.
RETRY_HINT = {
    DELAY_RANGE: (
        "\n\nYour previous program had an UNBOUNDED delay — the compiler "
        "rejected it with an invalid parameter range. The FIRST argument to "
        "de.delay/de.fdelay/ef.echo is the MAXIMUM delay, and it MUST be a "
        "compile-time constant (a literal, not ma.SR or an expression), "
        "because it sizes the buffer — e.g. MAXD = 192000. If the delay "
        "TIME is user-controllable, clamp it inside that constant: "
        "dtime : min(MAXD-1) : max(1). Fix this and re-emit the full "
        "program."
    ),
}
