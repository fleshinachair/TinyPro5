# TinyPro5

A Prophet-5 **Rev 2** style virtual analog polysynth for the Ableton Move (Schwung). It ships with **32 original factory presets** and can load your own banks.

| | |
|---|---|
| Module ID | `tinypro5` (sound generator) |
| Version | 1.2.1 |
| Target | AArch64 Cortex-A72, glibc, 44.1 kHz / 128-frame blocks |
| Polyphony | 5 voices, oldest-voice stealing; Poly, Unison, Mono and Legato modes |

## Sound engine

* **Osc A** - saw / pulse blend with PWM, hard-synced to Osc B.
* **Osc B** - saw / triangle / pulse blend with PWM, +-12 semitone pitch, fine tune, LFO mode and a keyboard-tracking toggle.
* **Filter** - SSM 2040 style 4-pole low-pass that self-oscillates at full resonance, with keyboard tracking and a bipolar envelope depth.
* **Poly-Mod** - Filter Env and Osc B can modulate Osc A frequency, Osc A pulse width and filter cutoff.
* **LFO** - saw / triangle / square to pitch, PWM and filter; the mod wheel (CC 1) adds depth.
* **Envelopes** - filter and amp ADSR, 1 ms to 10 s.
* **Noise** - white / pink blend.
* **Analog feel** - per-voice drift and component tolerance, scaled by *Drift*.
* **Rev 2 / Rev 4 voicing** - the `rev` switch (More 2 page) selects the original Rev 2 engine (default) or a Rev 4 flavour: tighter filter bass, faster envelopes, velocity and aftertouch response. *Vintage* sets how much per-voice variation there is. Not hardware-calibrated.
* **Output** - DC blocker, +6 dB makeup and a 1.5 ms look-ahead limiter that holds the output at or under -6.1 dBFS (adds 1.5 ms latency).
* **Real-time safe** - no `malloc`, I/O or locks in the audio path; the shipped `dsp.so` is audited for this.

## Controls

Switch pages with **Left/Right** or the **Track 1-4** buttons. Parameters not on a page (PM-Dest, LFORate, LFOWave, LFODest, Glide, Drift, APitch, BFine, NColor, BLFO, BKbd, ModWhl, rev) are on the **More 1 / More 2** pages and are also exposed to Schwung for mapping and automation.

| Page | E1 | E2 | E3 | E4 | E5 | E6 | E7 | E8 |
|---|---|---|---|---|---|---|---|---|
| 1 Filter | Cutoff | Reso | EnvAmt | KbdTrk | FAtk | FDecay | FSus | FRel |
| 2 Osc | AWave | APW | BWave | BPW | BPitch | Sync | PM-Fenv | PM-OscB |
| 3 Amp+Mix | AAtk | ADecay | ASus | ARel | AVol | BVol | Noise | Volume |
| 4 Voice+Sys | Mode | Voices | Detune | LFOAmt | Preset | Reload | Save | Order |

* **Presets** are named `group-program name` (4 groups x 8 programs, e.g. `3-1 Dark Brass Rise`). Turn **Preset (E5)** or the **jog wheel** to scroll the list; programs load as you scroll. A jog click switches between bank order and A-Z.
* **Reload** (E6) discards unsaved tweaks. **Save** (E7) stores the current sound over the program. **Revert** (the list entry after Save) restores the bank's own sound. Each asks for a Yes/No confirmation (No is preselected).
* **Voice modes**: `Poly`; `Unison` (all **Voices** stacked on the last note, spread by **Detune**); `Mono` (last-note priority, envelopes retrigger); `Legato` (as Mono, but overlapping notes don't retrigger). **Voices** (1-5) is a performance setting and isn't stored in presets.
* **Rev 2 / Rev 4** (`rev`, More 2 page): Rev 2 is the original engine and the default. Rev 4 gives the filter less bass drop under resonance, a higher saturation knee, faster envelopes, and responds to note velocity (filter envelope and level) and channel aftertouch (LFO depth and cutoff). Switching mid-note crossfades over a few ms. **Vintage** (0-1) sets per-voice drift, oscillator micro-detune and envelope variation; choosing a revision resets it to that revision's default (Rev 2 0.65, Rev 4 0).
* `PM-Dest` (bit mask): 0 Off, 1 FreqA, 2 PWA, 3 FreqA+PWA, 4 Filter, 5 FreqA+Filter, 6 PWA+Filter, 7 All.
* `LFODest`: 0 Pitch, 1 PWM, 2 Filter, 3 Pitch+Filter, 4 PWM+Filter, 5 Pitch+PWM, 6 All, 7 Off.

## Factory presets

32 original programs written for this engine and compiled in (`src/presets_factory.c`), styled after 1980s horror and sci-fi soundtrack synthesis. None are copied or derived from another instrument's patch data, and each carries a small level gain so the bank sits within a few dB.

1. **Basses and sequences** - mono, short filter decays
2. **Keys and plucks** - poly
3. **Pads and swells** - poly, slow attacks
4. **Stingers, drones and effects**

## Custom banks

Drop sound banks into the module's `banks/` folder (`/data/UserData/schwung/modules/sound_generators/tinypro5/banks/` on the Move) and reload the module. They are scanned once, when the module is created.

* **`.zip`** - every `.h2p` (u-he Repro-5 patch) and `.syx` (Prophet-5 program dump) inside becomes one bank named after the zip. A `.syx` file is one bank, a folder (up to three levels) is one bank, and loose `.h2p` files form `Loose`. Patches for other instruments are skipped.
* **Select Bank** is its own page in Schwung's page menu. Picking a bank loads its first program. **Order** and the preset list apply to the active bank.
* **Saving** works on every bank. Edits go in `user/<bank>.tp5u`; the bank files are never modified.
* **`.h2p`**: Repro-5 dial values map straight onto TinyPro5's dials. Anything TinyPro5 has no equivalent for (mod matrix, effects, per-voice pan) is not imported.
* **`.syx`**: the 0-127 values become dial positions. TinyPro5 has one shared LFO and a single Poly-Mod depth per source, so some settings are approximate. Treat imports as faithful in settings, approximate in absolute response.
* Your banks stay local: `banks/`, `*.zip`, `*.h2p` and `*.syx` are git-ignored. `bank_report` (host parameter) lists what was found.

## Build, test, install

```sh
make test        # or scripts/test.sh  - native unit tests
make arm         # or scripts/build.sh - AArch64 dsp.so + TinyPro5.tar.gz
```

`scripts/build.sh` tries `CROSS_PREFIX`, `aarch64-linux-gnu-gcc`, `zig cc` and Docker, in that order. Everything builds with `-std=c11 -Wall -Wextra -Werror -pedantic`.

Install on the Move, then restart or refresh Schwung so it rescans modules:

```sh
scp TinyPro5.tar.gz ableton@move.local:~/
ssh ableton@move.local 'mkdir -p /data/UserData/schwung/modules/sound_generators && tar -xzf ~/TinyPro5.tar.gz -C /data/UserData/schwung/modules/sound_generators/ && rm ~/TinyPro5.tar.gz'
```

## Layout

```
src/tinypro5.{h,c}     DSP engine
src/presets_factory.c  the 32 factory programs (bank 0)
src/bank.{h,c}         bank registry; h2p.c and bankscan.c read custom banks
src/meta.{h,c}         module.json / chain_params / ui_hierarchy
src/plugin.c           Schwung plugin API v2 wrapper
src/ui.js              Move OLED UI
tests/                 native unit tests
tools/, scripts/       ctypes preset check, WAV renderer, build and test scripts
```

## Output level

The engine is calibrated so five voices at maximum resonance with everything full peak at -7.6 dBFS before the makeup stage. The limiter then guarantees the output never exceeds -6.1 dBFS, even for extreme settings. The tests check this for every factory program. Custom banks have no level table and play at their own level.

## Licence and trademarks

* MIT-licensed (`LICENSE`, which also carries the attribution for the DEFLATE routine). The factory presets are original works under the same licence; no proprietary patch data is distributed.
* Prophet-5 and Sequential are registered trademarks of their respective owners. TinyPro5 is independent and not affiliated with or endorsed by Sequential.
* The LFO rate and depth curves used for `.syx` import come from public numeric fits documented by the Melodee project (GPL-3.0), independently re-implemented here. No Melodee source code is included.
* Banks you add yourself remain the property of their authors and stay on your device.
