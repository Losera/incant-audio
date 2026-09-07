// Effect band, MID-RANGE grouped (docs/ui_design_plan.md §2): eight params in
// two real vgroups of four -- the range between 03_effect_grouped (6 params) and
// 05_overflow_40. Exercises row() packing at the wide end: do two packed rows of
// four knobs read as one deliberate panel, and are the per-row cell widths
// consistent between a 4-control section and its sibling? The 700px column in
// --widths is the narrow case -- four knobs across a small window.
import("stdfaust.lib");
tone = vgroup("Tone",
         *(hslider("Bass [unit:dB]",     0, -15, 15, 0.1) : ba.db2linear)
       : *(hslider("Mid [unit:dB]",      0, -15, 15, 0.1) : ba.db2linear)
       : *(hslider("Treble [unit:dB]",   0, -15, 15, 0.1) : ba.db2linear)
       : *(hslider("Presence [unit:dB]", 0, -15, 15, 0.1) : ba.db2linear));
dyn = vgroup("Dyn",
        co.compressor_mono(hslider("Ratio", 3, 1, 20, 0.1),
                           hslider("Threshold [unit:dB]", -18, -60, 0, 0.1),
                           hslider("Attack [unit:ms]", 10, 0.1, 100, 0.1) * 0.001,
                           hslider("Release [unit:ms]", 120, 1, 1000, 1) * 0.001));
chain = tone : dyn;
process = _,_ : chain, chain;
