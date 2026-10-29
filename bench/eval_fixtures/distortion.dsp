import("stdfaust.lib");
drive = hslider("Drive", 4, 1, 12, 0.1);
mix = hslider("Mix", 1, 0, 1, 0.01);
dist(x) = mix * ma.tanh(drive * x) / ma.tanh(drive) + (1-mix) * x;
process = dist, dist;
