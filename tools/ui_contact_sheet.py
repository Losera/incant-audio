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

/* ── A/B against the previous run ───────────────────────────────────────────
   The prior PNG is stacked UNDER the current one and revealed on hover, so a
   design change is judged by flicking between two images in the same position
   rather than by scrolling between two tabs. Pure CSS: this file has to open
   from file:// with no server and no build step. */
.ab { position:relative }
.ab .prev { position:absolute; inset:0; width:100%; height:100%; opacity:0;
  transition:opacity .08s linear; pointer-events:none }
.ab:hover .prev { opacity:1 }
.ab:hover::after { content:"previous"; position:absolute; right:6px; top:6px;
  background:#000000cc; color:var(--warn); font-size:11px; padding:1px 6px;
  border-radius:3px; letter-spacing:.03em }
.ab.hasprev::before { content:"A/B"; position:absolute; left:6px; top:6px;
  background:#000000cc; color:var(--accent); font-size:11px; padding:1px 6px;
  border-radius:3px; letter-spacing:.03em; z-index:1 }

/* ── Grouping ──────────────────────────────────────────────────────────────
   Flat flex-wrap put fixture 01 and fixture 06 fifteen cards apart. Grouping by
   fixture keeps a fixture's style variants — the things you are actually
   comparing — adjacent and on one row. */
.group { margin:0 0 26px }
.group > h2 { font-size:13px; font-weight:600; letter-spacing:.06em;
  text-transform:uppercase; color:var(--dim); margin:0 0 10px;
  padding-bottom:6px; border-bottom:1px solid var(--rule) }
.group .sheet { gap:16px }

/* ── The diff, folded into the page ─────────────────────────────────────────
   The picture and the delta were previously two artifacts in two places
   (terminal scrollback + a browser tab). One artifact is reviewable; two is a
   thing people do once. */
.diff { background:var(--panel); border:1px solid var(--rule); border-radius:6px;
  padding:12px 14px; margin:0 0 24px; font:12px/1.5 ui-monospace,monospace;
  white-space:pre-wrap; color:var(--ink); max-height:320px; overflow:auto }
.diff .none { color:var(--accent) }
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

    # The previous run's copy of this exact fixture, rotated aside by
    # ui_iterate.sh before rendering. Absent on a first run, and that is the
    # normal case rather than an error -- render the current image alone.
    prev = base / "prev" / rec["png"]
    if img.exists():
        cur_tag = f'<img src="{html.escape(rec["png"])}" alt="{name} editor snapshot">'
        if prev.exists():
            img_tag = (f'<div class="ab hasprev">{cur_tag}'
                       f'<img class="prev" src="prev/{html.escape(rec["png"])}"'
                       f' alt="{name}, previous run"></div>')
        else:
            img_tag = f'<div class="ab">{cur_tag}</div>'
    else:
        img_tag = '<div class="meta broken">PNG missing</div>'

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


def fixture_of(rec):
    """The fixture a record belongs to, stripped of its style/width suffixes.

    Record names are `<fixture>[__<style>][__w<width>]` (UiDesignGallery.cpp).
    Splitting on the first `__` therefore groups every variant of one fixture
    together, which is the comparison a reviewer is actually making.
    """
    return rec["name"].split("__", 1)[0]


def main():
    if len(sys.argv) not in (3, 4):
        print(__doc__.strip().splitlines()[-1], file=sys.stderr)
        return 2

    manifest_path, out_path = Path(sys.argv[1]), Path(sys.argv[2])
    diff_path = Path(sys.argv[3]) if len(sys.argv) == 4 else None
    with open(manifest_path) as fh:
        data = json.load(fh)

    base = manifest_path.parent
    records = data["records"]
    broken = data.get("broken", 0)

    note = (f'<span class="broken">{broken} fixture(s) failed to render.</span> '
            if broken else "")

    # The semantic diff, when the caller captured one. Optional by design: the
    # sheet must still render on a first run, when there is no reference yet.
    diff_block = ""
    if diff_path is not None and diff_path.exists():
        text = diff_path.read_text().strip()
        inner = (html.escape(text) if text
                 else '<span class="none">no layout change vs the reference.</span>')
        diff_block = f'<div class="diff">{inner}</div>'

    groups = {}
    for r in records:
        groups.setdefault(fixture_of(r), []).append(r)

    body = "\n".join(
        f'<div class="group"><h2>{html.escape(g)}</h2><div class="sheet">'
        + "\n".join(card(r, base) for r in recs)
        + "</div></div>"
        for g, recs in groups.items())
    doc = f"""<!doctype html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>PluginForge UI gallery</title><style>{CSS}</style></head><body>
<h1>PluginForge — parameter grid across the fixture matrix</h1>
<div class="sub">{note}{len(records)} records across {len(groups)} fixtures, rendered from the
real editor via Component::createComponentSnapshot. Hover a snapshot to A/B it against the
previous run. Regenerate with <code>tools/ui_iterate.sh</code>.</div>
{diff_block}
{body}
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
