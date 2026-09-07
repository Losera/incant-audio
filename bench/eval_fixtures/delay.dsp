import("stdfaust.lib");
time = hslider("Delay Time [unit:ms]", 100, 10, 1000, 1);
feedback = hslider("Feedback", 0.6, 0, 0.9, 0.01);
echo = + ~ (de.delay(48000, int(time * ma.SR / 1000)) : *(feedback));
process = echo, echo;
