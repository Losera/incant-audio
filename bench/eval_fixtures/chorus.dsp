import("stdfaust.lib");
rate = hslider("Rate [unit:Hz]", 0.5, 0.1, 5, 0.1);
depth = hslider("Depth [unit:ms]", 4, 1, 10, 0.1);
mix = hslider("Mix", 0.5, 0, 1, 0.01);
vary = int((12 + depth*(os.osc(rate)+1)/2) * ma.SR / 1000);
wet(x) = de.delay(48000, vary, x);
chorus(x) = (1-mix)*x + mix*wet(x);
process = chorus, chorus;
