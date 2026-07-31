// Utility band (docs/ui_design_plan.md §2): 1-4 params, "the meter IS the UI".
import("stdfaust.lib");
gain = hslider("Gain [unit:dB]", 0, -60, 12, 0.1) : ba.db2linear : si.smoo;
pan  = hslider("Pan", 0.5, 0, 1, 0.01);
process = _,_ : *(gain),*(gain) : *(1-pan),*(pan);
