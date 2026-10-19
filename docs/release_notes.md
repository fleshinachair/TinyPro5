# TinyPro5 release notes

## 1.1.0

* New **Rev** switch (More 2 page): Rev 2 is the engine as it was (the 32 factory programs sound exactly as before); Rev 4 gives the filter less bass drop under resonance
  and a higher saturation knee (CEM 3320 style), faster envelopes, note velocity -> filter envelope and amp level, and pressure -> cutoff and LFO depth.
  Switching mid-note crossfades over a few milliseconds.
* New **Vintage** control (More 2 page): extra per-voice drift, oscillator micro-detune and envelope-rate variance. Choosing Rev 2 sets it to 0.65, Rev 4 to 0.
* Aftertouch: channel pressure and the Move pads' polyphonic pressure both work in Rev 4 (up to +3 octaves of cutoff and +0.6 LFO depth).
* Velocity depths default to amp 0.60 / filter 0.70 when you choose Rev 4 with the switch.
* Banks: a Repro-5 patch's Filter Tweak picks the revision (Crispy, Driven, Poly -> Rev 4; Rounded -> Rev 2), Voice Detune becomes Vintage and the two
  VelDep values become velocity depths. A Prophet-5 .syx program with the Curtis filter switch set, or an extended dump, imports as Rev 4. Dumps longer than
  the standard size are now accepted.
* `module.json` leaves out the default float step to stay under the host's 8 KB limit.

## 1.0.5

* Factory bank: 32 original programs in four groups of eight.
* Reload, Save and Revert ask for confirmation before they act.

## 1.0.0

First release.
