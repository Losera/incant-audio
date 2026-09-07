import("stdfaust.lib");
rate = hslider("Rate [unit:Hz]", 5, 0.1, 20, 0.1);
depth = hslider("Depth", 0.5, 0, 1, 0.01);
mod = 1-depth + depth*(os.osc(rate)+1)/2;
process = _*mod, _*mod;
