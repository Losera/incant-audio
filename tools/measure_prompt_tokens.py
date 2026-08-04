#!/usr/bin/env python3
"""
Live token-budget probe for the refine loop. Settles ONE question with ONE
number: does a system prompt plus a prior Faust source (the refine payload)
fit inside groq's admission rule before any of the request-file/prior_source
plumbing gets built.

    prompt_tokens + max_tokens <= 8000        (llm/providers.py:68)

`make_generator()` cannot answer this directly: `max_tokens` is floored at
`spec.min_max_tokens` (4096 for groq, llm/providers.py:766) before it ever
reaches the transport, so a caller asking for less silently gets 4096 anyway.
This script posts directly, matching the exact payload shape
`_make_openai_compat` builds (llm/providers.py:521-538), so it can report the
real `usage.prompt_tokens` at whatever `max_tokens` is actually asked for --
including deliberately low values, to see whether trimming the OUTPUT budget
for a refine response (likely a smaller diff than a from-scratch generation)
buys headroom for the prior-source INPUT.

Spends real free-tier groq quota (prompt tokens + up to --max-tokens
completion tokens), against the ~57-generation/day TPD ceiling recorded in
llm/providers.py's groq notes. Not zero-cost; run deliberately, not in a loop.

Usage:
    python tools/measure_prompt_tokens.py --prompt-file llm/prompts/system_prompt.txt
    python tools/measure_prompt_tokens.py --prompt-file llm/prompts/system_prompt.txt \\
        --prior-source path/to/patch.dsp
    python tools/measure_prompt_tokens.py --prompt-file llm/prompts/instrument_prompt.txt \\
        --prior-source path/to/patch.dsp
"""
import argparse
import os
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "llm"))

from dotenv import load_dotenv  # noqa: E402

load_dotenv(ROOT / ".env")

import providers  # noqa: E402

# Mirrors the exact framing Part A's generate.py change will prepend to a
# refine request (A3) -- measuring anything else would measure the wrong
# payload shape.
_REFINE_PREAMBLE = (
    "The user is asking you to MODIFY the following existing Faust program, "
    "not write a new one from scratch. Preserve its structure and control "
    "names where the request does not require changing them. Return the "
    "COMPLETE modified program.\n\n```faust\n{prior}\n```\n\nApply this "
    "change: "
)

_DELTA_REQUEST = "make the resonance stronger"


def build_user_message(prior_source: str | None) -> str:
    if prior_source:
        return _REFINE_PREAMBLE.format(prior=prior_source) + _DELTA_REQUEST
    return _DELTA_REQUEST


def main() -> int:
    ap = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--prompt-file", required=True, type=Path,
                    help="e.g. llm/prompts/system_prompt.txt")
    ap.add_argument("--prior-source", type=Path, default=None,
                    help="a .dsp file to fold in as the refine payload; omit for a "
                         "plain (non-refine) baseline measurement")
    ap.add_argument("--max-tokens", type=int, default=3000,
                    help="requested max_tokens, sent RAW to the API -- bypasses "
                         "make_generator's spec.min_max_tokens floor on purpose "
                         "(default 3000: below groq's 4096 production floor, to see "
                         "whether a smaller refine output budget buys input headroom)")
    args = ap.parse_args()

    system_prompt = args.prompt_file.read_text(encoding="utf-8")
    prior_source = (args.prior_source.read_text(encoding="utf-8")
                    if args.prior_source else None)
    user_message = build_user_message(prior_source)

    spec = providers.get_spec("groq")
    api_key = os.environ.get(spec.env_var, "") if spec.env_var else ""
    if not api_key:
        print(f"error: {spec.env_var} not set (check .env)", file=sys.stderr)
        return 2

    url = f"{spec.base_url}/chat/completions"
    headers = {"Authorization": f"Bearer {api_key}", "Content-Type": "application/json"}
    payload = {
        "model": spec.default_model,
        "max_tokens": args.max_tokens,
        "messages": [
            {"role": "system", "content": system_prompt},
            {"role": "user", "content": user_message},
        ],
    }

    # budget=None: this is a one-shot manual probe, not a subprocess racing a
    # cap -- same reasoning make_generator's own docstring gives for bench
    # harnesses (llm/providers.py:750-753).
    data = providers._post_with_backoff(url, headers, payload, budget=None)
    usage = data.get("usage", {})
    prompt_tokens = usage.get("prompt_tokens", 0)
    ceiling = 8000

    print(f"prompt_file          {args.prompt_file}  ({len(system_prompt)} chars)")
    print(f"prior_source         {args.prior_source or '(none)'}"
          + (f"  ({len(prior_source)} chars)" if prior_source else ""))
    print(f"requested max_tokens {args.max_tokens}")
    print(f"prompt_tokens        {prompt_tokens}")
    print(f"completion_tokens    {usage.get('completion_tokens')}")
    print(f"total_tokens         {usage.get('total_tokens')}")
    print(f"slack vs {ceiling} @ this max_tokens   "
          f"{ceiling - (prompt_tokens + args.max_tokens)}")
    print(f"slack vs {ceiling} @ production 4096   "
          f"{ceiling - (prompt_tokens + 4096)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
