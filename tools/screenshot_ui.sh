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

# Matched on CLASS, not title.
#
# It matched `'PluginForge' in title` until 2026-08-02, and that silently
# captured the WRONG WINDOW: a terminal sitting in ~/PluginForge has the title
# "~/PluginForge/host/tools", which contains the string. The shot came out as a
# picture of a shell, the script printed a success line with a plausible
# geometry, and nothing anywhere said it had failed. Every screenshot taken with
# a project terminal open and earlier in the window list was suspect.
#
# The class is set by JUCE from the Standalone's product name and is not
# something a passing terminal can collide with. Title is kept as a fallback for
# a compositor that does not report class, but the class match wins.
GEO=$(hyprctl clients -j | python3 -c "
import json, sys
ws = json.load(sys.stdin)
c = [w for w in ws if 'PluginForge' in (w.get('class') or '')]
if not c:
    c = [w for w in ws if 'PluginForge' in (w.get('initialClass') or '')]
if not c:
    sys.exit('PluginForge Host window not found — is the Standalone running?')
if len(c) > 1:
    names = ', '.join(repr(w.get('title')) for w in c)
    print(f'note: {len(c)} PluginForge windows open, shooting the first: {names}',
          file=sys.stderr)
w = c[0]
print(f\"{w['at'][0]},{w['at'][1]} {w['size'][0]}x{w['size'][1]}\")
")

mkdir -p artifacts/images
grim -g "$GEO" artifacts/images/plugin_ui.png
echo "wrote artifacts/images/plugin_ui.png ($GEO)"
