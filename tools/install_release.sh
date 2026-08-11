#!/usr/bin/env bash
set -euo pipefail

package_root="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
vst3_dir="${PLUGINFORGE_VST3_DIR:-$HOME/.vst3}"
data_dir="${XDG_DATA_HOME:-$HOME/.local/share}/pluginforge"
bin_dir="${PLUGINFORGE_BIN_DIR:-$HOME/.local/bin}"

mkdir -p "$vst3_dir" "$data_dir" "$bin_dir"
cp -a "$package_root/plugins/PluginForge Host.vst3" "$vst3_dir/"
cp -a "$package_root/plugins/PluginForge Synth.vst3" "$vst3_dir/"
cp -a "$package_root/runtime/." "$data_dir/"
install -m 0755 "$package_root/standalone/PluginForge Host" "$bin_dir/pluginforge-host"
install -m 0755 "$package_root/standalone/PluginForge Synth" "$bin_dir/pluginforge-synth"

printf 'Installed VST3 bundles in %s\n' "$vst3_dir"
printf 'Installed standalone launchers in %s\n' "$bin_dir"
printf 'Set this in the environment used to launch your DAW:\n'
printf '  export PLUGINFORGE_LLM_SCRIPT=%q\n' "$data_dir/llm/generate.py"
