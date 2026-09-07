import("stdfaust.lib");
freq = hslider("freq [unit:Hz]", 440, 20, 20000, 0.01);
gain = hslider("gain", 0.5, 0, 1, 0.01);
gate = button("gate");
process = os.osc(freq) * gain * en.adsr(0.01, 0.1, 0.7, 0.2, gate) <: _,_;
