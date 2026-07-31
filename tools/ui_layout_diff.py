#!/usr/bin/env python3
"""Semantic diff between two UiDesignGallery manifests.

A raw `diff` on manifest.json is unreadable — one control moving reshuffles a
JSON array and every following line reports as changed. This compares the things
a designer actually decided: which controls exist, in what order, as what
widget, showing what text, and how big the window ended up.

Exit 0 when nothing changed, 1 when something did. Called by tools/ui_iterate.sh,
which deliberately ignores that code — drift is the point of the loop, not a
failure. The code exists so the script is useful on its own and in a hook.

Usage: ui_layout_diff.py <reference.json> <current.json>
"""
import json
import sys


def load(path):
    with open(path) as fh:
        return {r["name"]: r for r in json.load(fh)["records"]}


def fmt_seq(seq, limit=8):
    """Render a list compactly — long param lists are noise past the first few."""
    shown = ", ".join(seq[:limit])
    return shown + (f", … (+{len(seq) - limit})" if len(seq) > limit else "")


def diff_record(name, ref, cur, out):
    """Append human-readable differences for one fixture. Returns True if any."""
    before = len(out)

    if ref["controls"] != cur["controls"]:
        out.append(f"    controls   {ref['controls']} → {cur['controls']}")

    if ref["window"] != cur["window"]:
        rw, rh = ref["window"]
        cw, ch = cur["window"]
        out.append(f"    window     {rw}×{rh} → {cw}×{ch}")

    if ref["grid"] != cur["grid"]:
        rc, rr = ref["grid"]
        cc, cr = cur["grid"]
        out.append(f"    grid       {rc}×{rr} → {cc}×{cr}")

    if ref["labels"] != cur["labels"]:
        # Distinguish a reorder from a membership change — they mean very
        # different things. A reorder is a layout decision; a membership change
        # means the patch or the capture changed underneath the layout.
        if sorted(ref["labels"]) == sorted(cur["labels"]):
            out.append("    order      reordered (same controls)")
            out.append(f"                 was  {fmt_seq(ref['labels'])}")
            out.append(f"                 now  {fmt_seq(cur['labels'])}")
        else:
            gone = [x for x in ref["labels"] if x not in cur["labels"]]
            new = [x for x in cur["labels"] if x not in ref["labels"]]
            if gone:
                out.append(f"    removed    {fmt_seq(gone)}")
            if new:
                out.append(f"    added      {fmt_seq(new)}")

    # Per-control attributes, compared BY POSITION.
    #
    # These were keyed by label — dict(zip(labels, kinds)) — which silently
    # collapses duplicates to the last occurrence, in all three comparisons
    # below. Faust reuses control names across groups routinely, and group
    # capture is exactly what makes that the normal case:
    # vgroup("Filter", hslider("Level")) and vgroup("Fx", hslider("Level")) are
    # two distinct controls that both key as "Level". A change on a shadowed
    # duplicate produced no output at all and exited 0, "no change" — and the
    # group comparison, the one this instrument exists for, was keyed by the
    # very field it was trying to validate.
    #
    # Position is the honest key: index i in the manifest is index i on screen.
    # A pure reorder is already reported above as `order`, so comparing by index
    # cannot hide a membership change, it just also reports the shift — which is
    # correct, because after a reorder every position genuinely does hold a
    # different control.
    n = min(len(ref["labels"]), len(cur["labels"]))

    def at(rec, key, i):
        seq = rec.get(key) or []
        return seq[i] if i < len(seq) else ""

    for i in range(n):
        rl, cl = ref["labels"][i], cur["labels"][i]
        where = f"#{i} '{rl}'" if rl == cl else f"#{i} '{rl}'→'{cl}'"
        if at(ref, "kinds", i) != at(cur, "kinds", i):
            out.append(f"    widget     {where}: "
                       f"{at(ref, 'kinds', i)} → {at(cur, 'kinds', i)}")
        # Group path per control. This is Variant C's whole input: if capture
        # regresses, every sectioned layout silently collapses to a flat grid,
        # which looks like a design choice rather than a defect.
        if at(ref, "groups", i) != at(cur, "groups", i):
            was = at(ref, "groups", i) or "(none)"
            now = at(cur, "groups", i) or "(none)"
            out.append(f"    group      {where}: {was} → {now}")
        if at(ref, "texts", i) != at(cur, "texts", i):
            out.append(f"    readout    {where}: "
                       f"{at(ref, 'texts', i)!r} → {at(cur, 'texts', i)!r}")

    # declared_params vs controls: the DSP declared N, the grid built M. Nothing
    # compared this field before, so a regression in it was invisible even once
    # it was being measured.
    if ref.get("declared_params") != cur.get("declared_params"):
        out.append(f"    declared   {ref.get('declared_params')} → "
                   f"{cur.get('declared_params')}")
    if cur.get("declared_params") is not None and cur["declared_params"] != cur["controls"]:
        out.append(f"    MISMATCH   Faust declared {cur['declared_params']} params, "
                   f"grid built {cur['controls']}")

    if len(out) > before:
        out.insert(before, f"  {name}")
        return True
    return False


def main():
    if len(sys.argv) != 3:
        print(__doc__.strip().splitlines()[-1], file=sys.stderr)
        return 2

    ref, cur = load(sys.argv[1]), load(sys.argv[2])
    out, changed = [], False

    for name in sorted(set(ref) | set(cur)):
        if name not in cur:
            out.append(f"  {name}\n    FIXTURE GONE (was in the reference)")
            changed = True
        elif name not in ref:
            r = cur[name]
            out.append(f"  {name}\n    NEW FIXTURE — {r['controls']} controls, "
                       f"grid {r['grid'][0]}×{r['grid'][1]}")
            changed = True
        elif diff_record(name, ref[name], cur[name], out):
            changed = True

    if changed:
        print("\n".join(out))
        print("\n   Look at artifacts/ui_gallery/index.html before deciding whether")
        print("   this is the change you meant. 'tools/ui_iterate.sh --accept'")
        print("   adopts it as the new baseline.")
    return 1 if changed else 0


if __name__ == "__main__":
    sys.exit(main())
