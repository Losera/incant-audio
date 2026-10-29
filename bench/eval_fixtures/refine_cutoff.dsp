import("stdfaust.lib");
cutoff = hslider("Cutoff [unit:Hz]", 2000, 20, 20000, 1);
q = hslider("Q", 0.707, 0.1, 10, 0.01);
mix = hslider("Mix", 0.5, 0, 1, 0.01);
filtered = fi.resonlp(cutoff, q, 1);
wet(x) = mix*filtered(x) + (1-mix)*x;
process = wet, wet;
