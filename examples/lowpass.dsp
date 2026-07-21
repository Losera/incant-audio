import("stdfaust.lib");
cutoff = hslider("Cutoff [unit:Hz]", 1000, 20, 20000, 1);
q = hslider("Q", 0.707, 0.1, 10, 0.01);
process = fi.resonlp(cutoff, q, 1.0), fi.resonlp(cutoff, q, 1.0);
