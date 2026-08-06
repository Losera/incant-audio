#!/usr/bin/env python3
"""
Dev-cockpit server for PluginForge.

A localhost HTTP server that mirrors the running PluginForge Standalone:
- Serves a static HTML iterate surface at http://localhost:8765/
- Exposes /api/screenshot to capture live UI via tools/screenshot_ui.sh
- Exposes /api/state to read plugin state from /tmp/pluginforge_state.json

This is a development tool, not a product feature. It is not shipped with the plugin.

Usage:
    python dev-cockpit/server.py [--port PORT] [--host HOST]

Default: http://localhost:8765/
"""

import argparse
import json
import os
import subprocess
import sys
import threading
from http.server import HTTPServer, SimpleHTTPRequestHandler
from pathlib import Path
from typing import Optional

# Repository root (two levels up from this script)
REPO_ROOT = Path(__file__).resolve().parent.parent
STATE_FILE = Path("/tmp/pluginforge_state.json")
SCREENSHOT_SCRIPT = REPO_ROOT / "tools" / "screenshot_ui.sh"


class CockpitHandler(SimpleHTTPRequestHandler):
    """HTTP handler for dev-cockpit endpoints."""

    # Serve from dev-cockpit/static/ directory
    static_dir = REPO_ROOT / "dev-cockpit" / "static"

    def __init__(self, *args, **kwargs):
        super().__init__(*args, directory=str(self.static_dir), **kwargs)

    def do_GET(self):
        """Handle GET requests."""
        if self.path == "/api/state":
            self._handle_state()
        elif self.path == "/api/screenshot":
            self._handle_screenshot()
        else:
            # Serve static files
            super().do_GET()

    def _handle_state(self):
        """Return current plugin state from the state file."""
        if not STATE_FILE.exists():
            self.send_error(503, "State file not found — is the plugin running?")
            return

        try:
            state = json.loads(STATE_FILE.read_text())
            self._send_json(state)
        except json.JSONDecodeError as e:
            self.send_error(500, f"Invalid state JSON: {e}")

    def _handle_screenshot(self):
        """Capture a screenshot via tools/screenshot_ui.sh and return PNG.
        On failure, return a placeholder SVG showing the failure reason."""
        if not SCREENSHOT_SCRIPT.exists():
            self._send_placeholder(f"Script not found:\n{SCREENSHOT_SCRIPT}")
            return

        # Create a temp file for the screenshot
        import tempfile
        with tempfile.NamedTemporaryFile(suffix=".png", delete=False) as tmp:
            tmp_path = Path(tmp.name)

        try:
            result = subprocess.run(
                [str(SCREENSHOT_SCRIPT), str(tmp_path)],
                capture_output=True,
                text=True,
                timeout=10,
            )

            if result.returncode != 0:
                reason = result.stderr.strip() or result.stdout.strip() or "Unknown error"
                self._send_placeholder(reason)
                return

            if not tmp_path.exists():
                self._send_placeholder("Script succeeded but no file created")
                return

            # Serve the PNG
            png_data = tmp_path.read_bytes()
            self.send_response(200)
            self.send_header("Content-Type", "image/png")
            self.send_header("Content-Length", str(len(png_data)))
            self.send_header("Cache-Control", "no-cache")
            self.end_headers()
            self.wfile.write(png_data)

        except subprocess.TimeoutExpired:
            self._send_placeholder("Screenshot timed out after 10s")
        except Exception as e:
            self._send_placeholder(f"Unexpected error:\n{e}")
        finally:
            if tmp_path.exists():
                tmp_path.unlink()

    def _send_placeholder(self, reason: str):
        """Return a placeholder SVG image when screenshot capture fails."""
        # Escape XML entities in the reason text
        safe = reason.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")
        lines = safe.split("\n")
        text_lines = "\n".join(
            f'<tspan x="20" dy="{20 if i else 0}">{line}</tspan>'
            for i, line in enumerate(lines)
        )
        svg = f'''<svg xmlns="http://www.w3.org/2000/svg" width="480" height="260">
  <rect width="480" height="260" fill="#313244" rx="8"/>
  <text x="20" y="30" fill="#f38ba8" font-family="monospace" font-size="14"
        font-weight="bold">Screenshot unavailable</text>
  <text x="20" y="60" fill="#a6adc8" font-family="monospace" font-size="12">
    {text_lines}
  </text>
  <text x="20" y="230" fill="#6c7086" font-family="monospace" font-size="11">
    Start the PluginForge Standalone and bring its window to the foreground.
  </text>
</svg>'''
        body = svg.encode("utf-8")
        self.send_response(200)
        self.send_header("Content-Type", "image/svg+xml")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-cache")
        self.end_headers()
        self.wfile.write(body)

    def _send_json(self, data: dict):
        """Send a JSON response."""
        body = json.dumps(data, indent=2).encode("utf-8")
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-cache")
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, format, *args):
        """Log HTTP requests to stderr with a prefix."""
        sys.stderr.write(f"[cockpit] {args[0]}\n")


def main():
    parser = argparse.ArgumentParser(
        description="PluginForge dev-cockpit server",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__,
    )
    parser.add_argument(
        "--port",
        type=int,
        default=8765,
        help="Port to listen on (default: 8765)",
    )
    parser.add_argument(
        "--host",
        default="localhost",
        help="Host to bind to (default: localhost)",
    )
    args = parser.parse_args()

    server_address = (args.host, args.port)
    httpd = HTTPServer(server_address, CockpitHandler)

    print(f"[cockpit] Dev-cockpit server running at http://{args.host}:{args.port}/")
    print("[cockpit] Endpoints:")
    print(f"[cockpit]   GET /             — Static iterate surface")
    print(f"[cockpit]   GET /api/state    — Plugin state JSON")
    print(f"[cockpit]   GET /api/screenshot — Live UI screenshot (PNG)")
    print("[cockpit] Press Ctrl+C to stop")

    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        print("\n[cockpit] Shutting down...")
        httpd.shutdown()


if __name__ == "__main__":
    main()
