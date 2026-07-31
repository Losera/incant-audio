// Effect band WITH Faust group structure. Groups ARE captured as of 2026-07-31:
// ParamCapture keeps a groupStack (FaustEngine.cpp:15-42), pushes on
// openHorizontalBox/openVerticalBox/openTabBox (:163-165), pops on closeBox
// (:167-173), and stamps info.group at :105. This fixture pins that: three
// vgroups -> Filter, Drive, Output, visible in the manifest's `groups` array.
// It is also the duplicate-label case tools/ui_layout_diff.py keys by index for
// -- one rename here puts two `Level` controls in different groups.
import("stdfaust.lib");
filt = vgroup("Filter", fi.resonlp(hslider("Cutoff [unit:Hz]", 800, 20, 20000, 1),
                                   hslider("Resonance", 1, 0.5, 10, 0.01), 1));
sat  = vgroup("Drive", *(hslider("Amount [unit:dB]", 0, 0, 24, 0.1) : ba.db2linear) : ma.tanh);
lvl  = vgroup("Output", hslider("Level [unit:dB]", 0, -24, 6, 0.1) : ba.db2linear
                        : *(1 - checkbox("Mute")));
chain = filt : sat;
process = _,_ : chain,chain : *(lvl),*(lvl);
