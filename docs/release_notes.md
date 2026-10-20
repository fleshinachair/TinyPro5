# TinyPro5 release notes

## 1.2.1

* **Mono mode:** holding one key, pressing a second, and then letting go of the first no longer retriggers the amp and filter envelopes of the note that is sounding.
  Only letting go of the sounding key moves anywhere (Mono still retriggers when it returns to a key that is still held). Legato and Unison are unaffected in sound.

## 1.2.0

* **Stereo spread:** the five voices have always been panned at fixed positions (voice 1 left, voice 2 right, and so on), which is why successive Poly notes moved left and
  right and why Mono and Legato sat slightly to the left. Mono and Legato are now centred, and a new **Spread** control (More 2 page, 0 to 1, default 1 = the pan as before)
  scales the pan of the voices; 0 centres everything. Ten of the factory programs use Mono or Legato, so their stereo image is now centred (their mono sum is unchanged).
* **Pad pressure (Rev 4):** channel and polyphonic pressure now share one response curve made for the Move pads: pressure up to 12 is ignored, 112 and above is full scale, and in
  between the response is squared, so a light touch is a gentle swell (MIDI 64 opens the cutoff by 0.8 octave, 112 by the full 3 octaves).

## 1.1.1

Click fixes for repeated and arpeggiated notes.

* **Declick** (new, internal): the amp and filter envelopes get their corners rounded over about 1.5 ms, which removes the ticks that fast attacks and the attack -> decay
  corner cause on a resonant or self-oscillating filter (a self-oscillating pluck that ticked on every repeated note now measures clean). It is on for
  every program from an imported bank and off in the factory bank, so the 32 factory programs sound exactly as before. Imported presets with very fast envelopes lose a little snap.
* **Rev 4 attack:** the attack ramp is never shorter than 3 ms (a 0.6 ms ramp clicked on every repeat of a low bass note; the click is about 11 dB smaller).
  Rev 4 decay and release are never faster than Rev 2's fastest setting.
* **Retriggering a sounding voice** eases the note's velocity over about 2 ms instead of stepping the level.
* The self-oscillation key-down click is no longer made bigger by Rev 4's input gain.

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
