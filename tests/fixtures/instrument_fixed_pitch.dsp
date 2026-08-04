import("stdfaust.lib");
freq = hslider("freq",440,20,2000,0.01);
gain = hslider("gain",0.5,0,1,0.01);
gate = button("gate");
// `freq` is referenced -- as a filter corner well above the tone -- so the
// control SURVIVES into the UI and the voice contract still holds. It just
// does not set the pitch, which is the defect: the shape a model produces when
// it wires the envelope and forgets the pitch.
process = os.osc(440) * gain * en.adsr(0.01,0.1,0.7,0.2,gate)
          : fi.lowpass(1, freq * 20) <: _,_;
