import("stdfaust.lib");
threshold = hslider("Threshold [unit:dB]", -20, -60, 0, 0.1);
ratio     = hslider("Ratio", 4.0, 1.0, 20.0, 0.1);
attack    = hslider("Attack [unit:ms]", 10, 1, 100, 1) / 1000.0;
release   = hslider("Release [unit:ms]", 100, 10, 1000, 1) / 1000.0;
process   = co.compressor_stereo(ratio, threshold, attack, release);
