import("stdfaust.lib");
time = hslider("Delay Time [unit:ms]", 250, 10, 1000, 1);
feedback = hslider("Feedback", 0.4, 0, 0.95, 0.01);
process = _, _;
