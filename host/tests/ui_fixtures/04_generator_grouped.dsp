// Generator band: 12-32 params across four sections. §2 calls grouped sections
// the natural paradigm here and names this the heaviest ParamPool user, where
// "grouping metadata matters most". Zero inputs -- a synth, not an insert.
import("stdfaust.lib");
osc = vgroup("Osc",
        hslider("Pitch [unit:Hz]", 220, 20, 2000, 0.1) : _ <: os.sawtooth,
        (*(1 + hslider("Detune", 0.01, 0, 0.1, 0.001)) : os.sawtooth),
        (*(0.5) : os.square)
      : _*hslider("Saw A", 0.4, 0, 1, 0.01),
        _*hslider("Saw B", 0.3, 0, 1, 0.01),
        _*hslider("Sub", 0.2, 0, 1, 0.01) :> _);
flt = vgroup("Filter",
        fi.resonlp(hslider("Cutoff [unit:Hz]", 1200, 20, 20000, 1),
                   hslider("Resonance", 2, 0.5, 20, 0.01),
                   1)
      : *(hslider("Track", 1, 0, 2, 0.01)));
env = vgroup("Env",
        *(en.adsr(hslider("Attack [unit:ms]", 5, 0.1, 2000, 0.1) * 0.001,
                  hslider("Decay [unit:ms]", 120, 1, 4000, 0.1) * 0.001,
                  hslider("Sustain", 0.7, 0, 1, 0.01),
                  hslider("Release [unit:ms]", 300, 1, 8000, 0.1) * 0.001,
                  button("Gate"))));
fx  = vgroup("Fx",
        _ <: _, (de.delay(96000, hslider("Time [unit:ms]", 180, 1, 1000, 0.1) * 48)
                 : *(hslider("Feedback", 0.35, 0, 0.95, 0.01)))
      :> *(hslider("Level [unit:dB]", -6, -60, 6, 0.1) : ba.db2linear)
         : *(1 - checkbox("Bypass")));
voice = osc : flt : env : fx;
process = voice <: _,_;
