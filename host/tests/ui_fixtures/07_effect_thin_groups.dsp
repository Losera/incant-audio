// Effect band, PATHOLOGICAL grouping (docs/ui_design_plan.md §2): four vgroups
// of ONE control each -- the exact shape a live "warm analog tape saturation
// effect with input drive, tone, output level, and a wet/dry mix" generation
// produced (STATUS.md; ArchetypeLayout.h header). Before fix/pedal-layout-packing
// this rendered as four headings over one knob apiece, half the panel dead by
// construction. The Phase-2 merge (average < 2 controls per group ->
// deriveLayoutFromGroups collapses to one "Controls" section) is what this
// fixture exists to show.
import("stdfaust.lib");
drive  = vgroup("Drive", *(hslider("Drive [unit:dB]", 0, 0, 24, 0.1) : ba.db2linear) : ma.tanh);
tone   = vgroup("Tone",  fi.lowpass(2, hslider("Tone [unit:Hz]", 4000, 200, 12000, 1)));
outlvl = vgroup("Out",   *(hslider("Output [unit:dB]", 0, -24, 6, 0.1) : ba.db2linear));
mixamt = vgroup("Mix",   hslider("Wet/Dry", 0.5, 0, 1, 0.01));
wet    = drive : tone : outlvl;
proc(x) = x * (1 - mixamt) + (x : wet) * mixamt;
process = _,_ : proc, proc;
