#!/usr/bin/env python3
"""Assemble a UiDesignGallery manifest into a single HTML contact sheet.

The point is to see every fixture at once. Iterating on a layout means asking
"does the 2-param case still look deliberate now that I have fixed the 40-param
case", and that question needs both on one screen.

Images are referenced relatively, not inlined — the sheet sits next to the PNGs
in artifacts/ui_gallery/ and opening it locally is the whole workflow.

Usage: ui_contact_sheet.py <manifest.json> <out.html>
"""
import html
import json
import sys
from pathlib import Path

CSS = """
:root {
  --bg:#12121c; --panel:#1b1b28; --rule:#2c2c3d; --ink:#e4e4f0;
  --dim:#8a8aa3; --accent:#94e2d5; --warn:#f9e2af;
  color-scheme: dark;
}
* { box-sizing:border-box }
body { margin:0; background:var(--bg); color:var(--ink);
  font:14px/1.55 ui-sans-serif,system-ui,sans-serif; padding:28px }
h1 { font-size:19px; margin:0 0 4px; font-weight:600 }
.sub { color:var(--dim); font-size:13px; margin-bottom:24px }
.sheet { display:flex; flex-wrap:wrap; gap:20px; align-items:flex-start }
.card { background:var(--panel); border:1px solid var(--rule); border-radius:6px;
  padding:14px; width:min(100%,516px) }
.card h2 { font-size:14px; margin:0 0 2px; font-weight:600 }
.meta { color:var(--dim); font-size:12px; margin-bottom:10px;
  font-variant-numeric:tabular-nums }
.card img { display:block; width:100%; height:auto; border:1px solid var(--rule);
  border-radius:3px; background:#000 }
.controls { margin-top:11px; font-size:12px; color:var(--dim);
  max-height:150px; overflow-y:auto }
.controls table { border-collapse:collapse; width:100% }
.controls td { padding:2px 8px 2px 0; border-bottom:1px solid var(--rule);
  white-space:nowrap }
.controls td:first-child { color:var(--ink) }
.controls td:last-child { text-align:right; font-variant-numeric:tabular-nums }
.k { color:var(--accent) }
.broken { color:var(--warn); font-weight:600 }
footer { margin-top:30px; padding-top:14px; border-top:1px solid var(--rule);
  color:var(--dim); font-size:12px }
code { background:#00000055; padding:1px 5px; border-radius:3px }
"""


def card(rec, base):
    name = html.escape(rec["name"])
    if not rec["settled"]:
        return (f'<div class="card"><h2>{name}</h2>'
                f'<div class="meta broken">did not settle — no snapshot</div></div>')

    w, h = rec["window"]
    cols, rows = rec["grid"]
    img = base / rec["png"]
    img_tag = (f'<img src="{html.escape(rec["png"])}" alt="{name} editor snapshot">'
               if img.exists() else
               '<div class="meta broken">PNG missing</div>')

    rowsq = "".join(
        f"<tr><td>{html.escape(l)}</td><td class='k'>{html.escape(k)}</td>"
        f"<td>{html.escape(t)}</td></tr>"
        for l, k, t in zip(rec["labels"], rec["kinds"], rec["texts"]))

    return f"""<div class="card">
  <h2>{name}</h2>
  <div class="meta">{rec['controls']} controls &middot; window {w}&times;{h} &middot; grid {cols}&times;{rows}</div>
  {img_tag}
  <div class="controls"><table>{rowsq}</table></div>
</div>"""


def main():
    if len(sys.argv) != 3:
        print(__doc__.strip().splitlines()[-1], file=sys.stderr)
        return 2

    manifest_path, out_path = Path(sys.argv[1]), Path(sys.argv[2])
    with open(manifest_path) as fh:
        data = json.load(fh)

    base = manifest_path.parent
    records = data["records"]
    broken = data.get("broken", 0)

    note = (f'<span class="broken">{broken} fixture(s) failed to render.</span> '
            if broken else "")

    body = "\n".join(card(r, base) for r in records)
    doc = f"""<!doctype html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>PluginForge UI gallery</title><style>{CSS}</style></head><body>
<h1>PluginForge — parameter grid across the fixture matrix</h1>
<div class="sub">{note}{len(records)} fixtures, rendered from the real editor via
Component::createComponentSnapshot. Regenerate with <code>tools/ui_iterate.sh</code>.</div>
<div class="sheet">
{body}
</div>
<footer>
Fixtures live in <code>host/tests/ui_fixtures/*.dsp</code> and span the
docs/ui_design_plan.md &sect;2 taxonomy: utility, effect (flat and grouped),
generator, and the 40-param overflow case. The control table under each snapshot
is the same data <code>tools/ui_layout_diff.py</code> compares.
</footer>
</body></html>
"""
    out_path.write_text(doc)
    return 0


if __name__ == "__main__":
    sys.exit(main())
