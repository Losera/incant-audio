import("stdfaust.lib");

rate  = hslider("Rate [unit:Hz]", 1.0, 0.01, 10.0, 0.01);
depth = hslider("Depth", 0.5, 0.0, 1.0, 0.01);
mix   = hslider("Mix", 0.5, 0.0, 1.0, 0.01);

lfo       = os.osc(rate) * depth;
delSamp   = int((20.0 + lfo * 10.0) * ma.SR / 1000.0);
chorused  = de.delay(4096, delSamp);

process = _ <: *(1.0 - mix), (chorused : *(mix)) :> _;
