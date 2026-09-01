#!/usr/bin/env bash
set -euo pipefail

# PluginForge / Incant Audio local installer.
#
# Installs the VST3 bundles, the Standalone launchers, and a self-contained
# Python generation runtime, then writes ~/.config/pluginforge/config.json so the
# plugin can find generate.py and its interpreter WITHOUT any PLUGINFORGE_*
# environment variable. That last part is the point: a DAW started from a desktop
# launcher inherits none of this shell's environment, which is PF-065 / PF-071 —
# generation failed as an installed VST3 because the old install only PRINTED an
# `export PLUGINFORGE_LLM_SCRIPT=...` line for a shell the DAW never sees.
#
# config.json is a non-secret preferences file (ADR-032 §4). Credentials stay in
# .env, which this script only ever seeds from the example — it never writes a key.

command -v python3 >/dev/null 2>&1 || {
  printf 'error: python3 is required (it runs the generation backend).\n' >&2
  exit 1
}

package_root="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
vst3_dir="${PLUGINFORGE_VST3_DIR:-$HOME/.vst3}"
data_dir="${XDG_DATA_HOME:-$HOME/.local/share}/pluginforge"
bin_dir="${PLUGINFORGE_BIN_DIR:-$HOME/.local/bin}"
config_dir="${XDG_CONFIG_HOME:-$HOME/.config}/pluginforge"

mkdir -p "$vst3_dir" "$data_dir" "$bin_dir" "$config_dir"
cp -a "$package_root/plugins/PluginForge Host.vst3" "$vst3_dir/"
cp -a "$package_root/plugins/PluginForge Synth.vst3" "$vst3_dir/"
cp -a "$package_root/runtime/." "$data_dir/"
install -m 0755 "$package_root/standalone/PluginForge Host" "$bin_dir/pluginforge-host"
install -m 0755 "$package_root/standalone/PluginForge Synth" "$bin_dir/pluginforge-synth"
printf 'Installed VST3 bundles in %s\n' "$vst3_dir"
printf 'Installed standalone launchers in %s\n' "$bin_dir"

generate_script="$data_dir/llm/generate.py"

# ── Python runtime ───────────────────────────────────────────────────────────
# A dedicated venv so the plugin depends on a known interpreter with known deps,
# not on whatever python3 (if any) a launcher-started DAW inherits.
venv_dir="$data_dir/venv"
python_bin=""
if python3 -m venv --help >/dev/null 2>&1; then
  python3 -m venv "$venv_dir"
  if "$venv_dir/bin/python3" -m pip install --quiet -r "$data_dir/requirements.txt"; then
    python_bin="$venv_dir/bin/python3"
    printf 'Created a Python venv with the generation dependencies at %s\n' "$venv_dir"
  else
    python_bin="$venv_dir/bin/python3"
    printf 'WARNING: pip install failed (offline?). The venv interpreter is recorded;\n'
    printf '         finish it later:  %q -m pip install -r %q\n' \
           "$venv_dir/bin/python3" "$data_dir/requirements.txt"
  fi
else
  python_bin="$(command -v python3)"
  printf 'WARNING: "python3 -m venv" is unavailable; recording the system python3 (%s).\n' "$python_bin"
  printf '         Install its deps yourself:  python3 -m pip install -r %q\n' "$data_dir/requirements.txt"
fi

# ── Credentials file ─────────────────────────────────────────────────────────
# generate.py loads <data_dir>/.env (Path(__file__).parent.parent / ".env").
# Seed it from the example; never overwrite a file that already has real keys.
env_file="$data_dir/.env"
if [[ -f "$env_file" ]]; then
  printf 'Kept your existing %s\n' "$env_file"
elif [[ -f "$data_dir/.env.example" ]]; then
  cp "$data_dir/.env.example" "$env_file"
  printf 'Wrote a starter %s\n' "$env_file"
fi

# ── config.json — the file the plugin reads ──────────────────────────────────
# Merge into any existing file: never clobber active_provider / active_model /
# soundfetch_interpreter_path that the in-plugin picker may already have written.
config_file="$config_dir/config.json"
CFG_FILE="$config_file" GEN_SCRIPT="$generate_script" PY_BIN="$python_bin" python3 - <<'PY'
import json, os, pathlib

path = pathlib.Path(os.environ["CFG_FILE"])
cfg = {}
if path.exists():
    try:
        loaded = json.loads(path.read_text())
        if isinstance(loaded, dict):
            cfg = loaded
    except ValueError:
        pass

cfg.setdefault("schema", 1)
cfg["generate_script_path"] = os.environ["GEN_SCRIPT"]
if os.environ["PY_BIN"]:
    cfg["python_path"] = os.environ["PY_BIN"]

path.parent.mkdir(parents=True, exist_ok=True)
path.write_text(json.dumps(cfg, indent=2) + "\n")
print(f"Updated {path} (generate_script_path, python_path; other keys preserved)")
PY

printf '\nDone.\n'
printf 'Generation now works from any DAW, including one started from a desktop\n'
printf 'launcher — no PLUGINFORGE_* environment variables required.\n'
printf 'The default provider is free-tier; if it needs an API key, add it to:\n  %s\n' "$env_file"
