#!/usr/bin/env bash
# Screenshot the PluginForge Standalone window into artifacts/images/plugin_ui.png.
#
# Run this yourself with the app open and its window visible/unobstructed —
# grim captures screen pixels, so anything overlapping the window ends up in the
# shot. Best flow:
#   1) ./host/build/PluginForgeHost_artefacts/Debug/Standalone/"PluginForge Host" &
#   2) click the window so it's on top (type a prompt / play audio for a live meter)
#   3) ./tools/screenshot_ui.sh
set -euo pipefail
cd "$(dirname "$0")/.."

GEO=$(hyprctl clients -j | python3 -c "
import json, sys
c = [w for w in json.load(sys.stdin) if 'PluginForge' in w.get('title', '')]
if not c:
    sys.exit('PluginForge Host window not found — is the Standalone running?')
w = c[0]
print(f\"{w['at'][0]},{w['at'][1]} {w['size'][0]}x{w['size'][1]}\")
")

mkdir -p artifacts/images
grim -g "$GEO" artifacts/images/plugin_ui.png
echo "wrote artifacts/images/plugin_ui.png ($GEO)"
