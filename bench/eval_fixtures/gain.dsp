import("stdfaust.lib");
gain = hslider("Gain [unit:dB]", -6, -60, 12, 0.1) : ba.db2linear;
process = _ * gain, _ * gain;
