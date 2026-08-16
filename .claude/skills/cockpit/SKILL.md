# /cockpit — Launch the PluginForge dev-cockpit

Launches the localhost dev-cockpit server that mirrors the running PluginForge
Standalone with live screenshots and state polling.

## Usage

```
/cockpit
```

This skill:
1. Starts the dev-cockpit HTTP server at http://localhost:8765/
2. Opens the browser to the iterate surface
3. The cockpit reads state from `/tmp/pluginforge_state.json` (written by the
   running Standalone at ~10 Hz, IF armed -- see Requirements) and serves live
   screenshots via `tools/screenshot_ui.sh`

## Requirements

- PluginForge Standalone must be running **with
  `PLUGINFORGE_COCKPIT_STATE=/tmp/pluginforge_state.json` set in its
  environment**. Until 2026-08-12, `setCockpitStatePath()` (`PluginEditor.h`)
  had no caller anywhere in the repo -- the mirror was permanently off no
  matter what this doc said, and `/api/state` could only ever 503. It is now
  armed via that env var, read once in the editor's constructor, and stays OFF
  (unchanged shipping default) when the var is unset.
- Hyprland compositor (for screenshot capture via `tools/screenshot_ui.sh`)
- Python 3 with no additional dependencies (uses stdlib only)

## Endpoints

| Endpoint | Description |
|----------|-------------|
| `GET /` | Static iterate surface UI |
| `GET /api/state` | Current plugin state as JSON |
| `GET /api/screenshot` | Live UI screenshot as PNG |

## Notes

- The cockpit is a development tool, not a product feature
- It is not shipped with the plugin
- Screenshot capture requires Hyprland (not available in headless/CI environments)
- One plugin instance per cockpit session (state file is singleton)