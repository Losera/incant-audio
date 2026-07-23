#!/usr/bin/env python3
"""
BYO-LLM prompt exporter — "spend your own inference".

PluginForge never calls an LLM here. This module builds a self-contained,
paste-ready payload the user drops into *their own* ChatGPT/Claude/Gemini; they
paste the returned Faust back and we compile it. Zero API key, zero provider
call, zero network on our side — that is the entire point of the feature, and it
is enforced by tests/test_export_prompt.py.

Run modes (mirroring llm/generate.py's stdout discipline):
  python export_prompt.py --prompt "warm tape delay"
      — CLI, prints the payload to stdout for copy-paste.
  python export_prompt.py --prompt "warm tape delay" --json
      — ADR-011-shaped single JSON line on stdout: {"ok": true, "payload": "..."}
        (errors as {"ok": false, "error": "..."}), for the C++ host to shell out to.

The exported payload loads the SAME llm/prompts/system_prompt.txt that generate.py
uses (generate.py:41). It does NOT fork or re-author the prompt — one prompt, one
measurement.
"""
import argparse
import json
import sys
from pathlib import Path

# Match generate.py:21 — make sibling llm/ modules importable when this file is
# run as a script. Only strip_code_fence() (intake, not export) uses it, and it
# imports lazily, so building a payload never pulls in the provider stack.
sys.path.insert(0, str(Path(__file__).parent))

# Same load pattern and same file as generate.py:41. Read once at import.
SYSTEM_PROMPT = (Path(__file__).parent / "prompts" / "system_prompt.txt").read_text()

# The one-line instruction that makes downstream fence-stripping deterministic:
# the external LLM must reply with ONLY a Faust patch inside a single ```faust
# block, so strip_code_fence() has exactly one block to lift.
_REPLY_INSTRUCTION = (
    "Reply with ONLY the Faust patch — a single `process = ...;` definition plus "
    "any helper definitions it needs — inside ONE ```faust fenced code block. "
    "No prose, no explanation, nothing before or after the code block."
)

_SEPARATOR = "=" * 60


def build_payload(user_request: str) -> str:
    """Return a self-contained, paste-ready block for an external chat LLM.

    Structure: the full system prompt verbatim, a clearly delimited USER REQUEST
    section carrying the request verbatim, then the single-block reply
    instruction. Pure string assembly — deterministic for a fixed request, and it
    performs NO network/provider call and needs NO API key or provider env var.
    """
    return (
        f"{SYSTEM_PROMPT}\n"
        f"{_SEPARATOR}\n"
        f"USER REQUEST:\n"
        f"{user_request}\n"
        f"{_SEPARATOR}\n\n"
        f"{_REPLY_INSTRUCTION}\n"
    )


def strip_code_fence(text: str) -> str:
    """Strip ```faust / bare ``` fences from pasted LLM output; leave code intact.

    Intake helper for the paste-back half of BYO. REUSES the existing, already
    tested logic in llm/providers.py (providers.strip_code_fences) rather than
    forking it — the import is lazy so the export/build path never depends on the
    provider module. See the open decision in docs/byo_llm_plan.md: S1 may later
    move this helper to its own module; until then it lives at one source of truth.
    """
    from providers import strip_code_fences  # lazy: keeps build_payload provider-free
    return strip_code_fences(text)


def _payload_json(user_request: str) -> dict:
    """ADR-011-shaped result dict for --json mode."""
    try:
        return {"ok": True, "payload": build_payload(user_request)}
    except Exception as exc:  # noqa: BLE001 — convert to ADR-011 JSON, never a traceback
        return {"ok": False, "error": str(exc)}


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(
        description="Export a paste-ready BYO-LLM prompt (no API key, no network)."
    )
    parser.add_argument("--prompt", metavar="TEXT",
                        help="The natural-language effect request to wrap.")
    parser.add_argument("--json", action="store_true",
                        help="Emit one ADR-011 JSON line: {\"ok\": true, \"payload\": ...}.")
    args = parser.parse_args(argv)

    if args.json:
        # ADR-011: exactly one JSON line on stdout, exit 0 — the host parses the
        # structured result regardless of exit code.
        if args.prompt is None:
            print(json.dumps({"ok": False, "error": "no --prompt provided"}))
        else:
            print(json.dumps(_payload_json(args.prompt)))
        return 0

    if args.prompt is None:
        parser.error("--prompt is required (or use --json).")
    print(build_payload(args.prompt))
    return 0


if __name__ == "__main__":
    sys.exit(main())
