import("stdfaust.lib");
comb = + ~ de.delay(48000, 4800);
process = comb, comb;
