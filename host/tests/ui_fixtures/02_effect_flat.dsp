// Effect band: 4-16 params, ungrouped. The case the flat sqrt grid serves well.
import("stdfaust.lib");
cutoff = hslider("Cutoff [unit:Hz]", 800, 20, 20000, 1);
res    = hslider("Resonance", 1, 0.5, 10, 0.01);
drive  = hslider("Drive [unit:dB]", 0, 0, 24, 0.1);
mix    = hslider("Mix", 1, 0, 1, 0.01);
out    = hslider("Output [unit:dB]", 0, -24, 6, 0.1);
byp    = checkbox("Bypass");
amt    = ba.db2linear(out) * (1 - byp);
proc   = fi.resonlp(cutoff, res, ba.db2linear(drive)) : *(mix);
process = _,_ : proc,proc : *(amt),*(amt);
