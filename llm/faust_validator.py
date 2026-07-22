#!/usr/bin/env python3
"""Standalone validator — call directly to check a .dsp file."""
import sys
import subprocess

def validate_file(path: str) -> tuple[bool, str]:
    # errors="replace": faust emits invalid UTF-8 on stderr when the source has
    # non-ASCII in it (the echoed source line gets cut mid-character). Strict
    # decoding raises instead of returning a compile error. See
    # llm/generate.py::validate_faust for the full note.
    result = subprocess.run(
        ["faust", "-lang", "cpp", path, "-o", "/dev/null"],
        capture_output=True, text=True, timeout=15,
        encoding="utf-8", errors="replace",
    )
    return result.returncode == 0, result.stderr.strip()

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: python faust_validator.py <file.dsp>")
        sys.exit(1)
    ok, msg = validate_file(sys.argv[1])
    print("VALID" if ok else f"INVALID:\n{msg}")
    sys.exit(0 if ok else 1)
