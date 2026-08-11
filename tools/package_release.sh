#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="${1:-$repo_root/host/build}"
output_dir="${2:-$repo_root/dist}"
version="${PLUGINFORGE_VERSION:-$(sed -n 's/^project(PluginForge VERSION \([^)]*\)).*/\1/p' "$repo_root/host/CMakeLists.txt")}"
if [[ -z "$version" ]]; then
  printf 'could not determine PluginForge version\n' >&2
  exit 1
fi
package_name="pluginforge-${version}-linux-x86_64"
stage="$(mktemp -d)"
trap 'rm -rf "$stage"' EXIT

root="$stage/$package_name"
mkdir -p "$root/plugins" "$root/standalone" "$root/runtime" "$output_dir"

copy_required() {
  local source="$1"
  local destination="$2"
  if [[ ! -e "$source" ]]; then
    printf 'required release artifact not found: %s\n' "$source" >&2
    exit 1
  fi
  cp -a "$source" "$destination"
}

copy_required "$build_dir/PluginForgeHost_artefacts/Release/VST3/PluginForge Host.vst3" "$root/plugins/"
copy_required "$build_dir/PluginForgeSynth_artefacts/Release/VST3/PluginForge Synth.vst3" "$root/plugins/"
copy_required "$build_dir/PluginForgeHost_artefacts/Release/Standalone/PluginForge Host" "$root/standalone/"
copy_required "$build_dir/PluginForgeSynth_artefacts/Release/Standalone/PluginForge Synth" "$root/standalone/"
copy_required "$repo_root/llm" "$root/runtime/"
copy_required "$repo_root/LICENSE" "$root/"
copy_required "$repo_root/README.md" "$root/"
copy_required "$repo_root/requirements.txt" "$root/runtime/"
copy_required "$repo_root/.env.example" "$root/runtime/"
copy_required "$repo_root/tools/install_release.sh" "$root/install.sh"
chmod +x "$root/install.sh"
find "$root/runtime/llm" -type d -name __pycache__ -prune -exec rm -rf {} +

archive="$output_dir/$package_name.tar.gz"
tar -C "$stage" -czf "$archive" "$package_name"
(
  cd "$output_dir"
  sha256sum "$(basename "$archive")" > "$(basename "$archive").sha256"
)
printf '%s\n' "$archive"
