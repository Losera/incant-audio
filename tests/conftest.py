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
