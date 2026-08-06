#!/usr/bin/env python3
"""
llm/export_prompt.py — BYO-LLM Prompt Payload Exporter.

FLEET cross-lane requirement #6 (authorized 2026-07-23, docs/byo_llm_plan.md).
Exports a self-contained, paste-ready prompt payload for external LLMs (ChatGPT, Claude, Gemini).

Guarantees:
- Zero network calls, zero API keys required.
- ADR-011 JSON format output when invoked with --json.
- Output uses deterministic ```faust ... ``` code fence instructions for reliable intake parsing.
"""

import argparse
import json
import os
import sys
from pathlib import Path

PROMPT_DIR = Path(__file__).resolve().parent / "prompts"
SYSTEM_PROMPT_PATH = PROMPT_DIR / "system_prompt.txt"

EXPORT_INSTRUCTION = """
USER REQUEST:
{user_request}

INSTRUCTIONS FOR THE MODEL:
Synthesize the Faust audio plugin requested above.
Return ONLY valid Faust DSP code inside a single ```faust ... ``` code block.
Do NOT include prose, explanations, or Markdown outside the code block.
The Faust program must include a valid `process = ...;` entry point.
"""


def build_payload(user_request: str) -> str:
    """Build a self-contained pasteable payload containing system prompt and user request."""
    # Attempt to load dynamic prompt builder for headroom optimization if available
    try:
        from prompt_builder import build_dynamic_prompt
        base_prompt = build_dynamic_prompt(user_request, SYSTEM_PROMPT_PATH)
    except Exception:
        base_prompt = SYSTEM_PROMPT_PATH.read_text(encoding="utf-8")
        
    formatted_instruction = EXPORT_INSTRUCTION.format(user_request=user_request.strip())
    return f"{base_prompt}\n\n{formatted_instruction}".strip()


def main():
    parser = argparse.ArgumentParser(description="Export self-contained BYO-LLM prompt payload.")
    parser.add_argument("--prompt", required=True, help="User prompt text describing the requested audio plugin.")
    parser.add_argument("--json", action="store_true", help="Output single-line ADR-011 JSON for host panel intake.")
    args = parser.parse_args()

    payload = build_payload(args.prompt)

    if args.json:
        print(json.dumps({"ok": True, "payload": payload}, ensure_ascii=False))
    else:
        print(payload)


if __name__ == "__main__":
    main()
