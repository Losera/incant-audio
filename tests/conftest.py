"""Shared pytest configuration and fixtures."""
import os
import sys
from pathlib import Path

PROJECT_ROOT = Path(__file__).parent.parent

# Must be set before any llm module is imported so anthropic.Anthropic() finds a key.
os.environ.setdefault("ANTHROPIC_API_KEY", "")

# SUBTLE: llm/generate.py calls load_dotenv() at import, so PluginForge/.env leaks
# into the test process. Without this pin, a developer whose .env selects a live
# free provider would have the unit tests — which only mock the *anthropic* client —
# silently dispatch real network calls and burn free-tier quota. setdefault, not
# a hard set, so `PLUGINFORGE_PROVIDER=groq pytest ...` still works deliberately.
os.environ.setdefault("PLUGINFORGE_PROVIDER", "anthropic")
os.environ.setdefault("PLUGINFORGE_MODEL", "")

# Make llm/ importable from all test modules.
sys.path.insert(0, str(PROJECT_ROOT / "llm"))

# Make the project root importable, so `from bench import run_eval` resolves.
#
# WHY THIS LINE EXISTS: tests/test_run_eval.py is the first test in the repo to import
# from bench/, and it could never have passed in CI without this. The line above puts
# llm/ on the path but not the root, bench/ has no __init__.py on this branch or on main,
# and pytest's rootdir insertion reaches tests/ rather than its parent. So CI failed at
# collection with `ModuleNotFoundError: No module named 'bench'` while the author's local
# run resolved it from the cwd. Not a regression — the import was never satisfied here.
sys.path.insert(0, str(PROJECT_ROOT))
