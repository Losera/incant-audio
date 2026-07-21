#!/usr/bin/env python3
"""Standalone validator — call directly to check a .dsp file."""
import sys
import subprocess

def validate_file(path: str) -> tuple[bool, str]:
    result = subprocess.run(
        ["faust", "-lang", "cpp", path, "-o", "/dev/null"],
        capture_output=True, text=True, timeout=15,
    )
    return result.returncode == 0, result.stderr.strip()

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: python faust_validator.py <file.dsp>")
        sys.exit(1)
    ok, msg = validate_file(sys.argv[1])
    print("VALID" if ok else f"INVALID:\n{msg}")
    sys.exit(0 if ok else 1)
