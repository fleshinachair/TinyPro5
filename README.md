# TinyPro5

A Prophet-5 **Rev 2** style virtual analog polysynth for the Ableton Move (Schwung), with **32 original factory presets included** (four groups of eight: basses and sequences, keys and plucks, pads and swells, stingers and drones) and room for your own banks.

| | |
|---|---|
| Module ID | `tinypro5` |
| Type | `sound_generator` (instrument) |
| Version | 1.0.0 |
| Target | AArch64 Cortex-A72, glibc, 44.1 kHz / 128-frame blocks |
| Polyphony | 5 voices, oldest-voice stealing, Unison mode with detune spread |

## Sound engine

* **Osc A** - saw / pulse (continuous blend) with PWM, **hard-synced to Osc B** (sub-sample-accurate reset, PolyBLEP corrected).
* **Osc B** - saw / triangle / pulse (continuous blend) with PWM, +-12 semitone pitch + +-50 cent fine tune, **LFO mode** (1-16 Hz) and an independent **keyboard-tracking** toggle.
* **Per-voice analog drift** - three independent ~1/f random walks per voice (pitch A, pitch B, cutoff) plus fixed per-voice "component tolerance" offsets, all scaled by *Drift*.
* **SSM 2040 style 4-pole low-pass** - zero-delay-feedback ladder with the global feedback loop solved instantaneously, soft saturation at the input and between stages (bounded and unconditionally stable), partial resonance gain compensation for the SSM's bass retention, self-oscillation at full resonance, 0-100 % keyboard tracking, bipolar ADSR envelope depth.
* **Poly-Mod** - sources *Filter Env* and *Osc B* -> destinations *Osc A frequency*, *Osc A pulse width*, *Filter cutoff* (any combination via `PM-Dest`).
* **Global LFO** - saw / triangle / square -> pitch, PWM, filter (seven routing combinations); depth = *LFOAmt* + mod wheel (CC 1).
* **Envelopes** - filter ADSR and amp ADSR, 1 ms - 10 s. Attack charges toward 1.3x and clips at 1.0 (the analog "snap").
* **Noise** - white / pink blend through the mixer (per voice).
* **Master** - calibrated headroom, DC blocker, **+6 dB makeup**, then a 1.5 ms look-ahead limiter that holds the output at or under **-6.1 dBFS** (it adds 1.5 ms of latency).
* **No zipper noise** - every continuously variable control is smoothed per sample (6 ms one-pole).
* **Real-time safe** - no `malloc`/`free`, no I/O, no locks anywhere in the engine; the shipped `dsp.so` is audited for allocation / file / lock imports.

## Controls (Move: 8 encoders, 4 pages)

Schwung draws these pages from the module's `ui_hierarchy` / `chain_params` (stepped parameters are declared `enum` with their option names, so Mode reads Poly / Unison / Mono / Legato and Order reads Bank order / A-Z). Besides the four encoder pages there are the **Presets** browser, **More 1 / More 2** pages for the remaining parameters (PM-Dest, LFORate, LFOWave, LFODest, Glide, Drift, APitch, BFine, NColor, BLFO, BKbd, ModWhl) and **Select Bank**; Preset and Bank are chosen on those pages, so their encoder slots stay with the browser. `src/ui.js` is the standalone-loader UI (Left/Right or Track buttons switch pages there).

Switch pages with **Left/Right** or the **Track 1-4** buttons. Labels are <= 7 characters.

| Page | E1 | E2 | E3 | E4 | E5 | E6 | E7 | E8 |
|---|---|---|---|---|---|---|---|---|
| 1 Filter | Cutoff | Reso | EnvAmt | KbdTrk | FAtk | FDecay | FSus | FRel |
| 2 Osc | AWave | APW | BWave | BPW | BPitch | Sync | PM-Fenv | PM-OscB |
| 3 Amp+Mix | AAtk | ADecay | ASus | ARel | AVol | BVol | Noise | Volume |
| 4 Voice+Sys | Mode | Voices | Detune | LFOAmt | Preset | Reload | Save | Order |

* **Presets** are addressed by group and program, `group-program name` (e.g. `3-1 Dark Brass Rise`: 4 groups x 8 programs). Turning **Preset (E5)** or the **jog wheel** pops up a scrolling list of the program names (turn fast to skip) and loads the highlighted program as you go; the list falls back to the dials about 1.5 s after the last scroll. **Reload** (E6) and **Save** (E7) are push buttons, and **Revert** is a list entry right after Save. Hold the knob and click the jog (in the list view just click the row) to open a full-screen "are you sure?" screen (from `canvas.js`) that names the program and what will happen; turn the jog to pick **No** or **Yes**, click to confirm, Back cancels. No is selected when it opens, and turning the knob does nothing, so a stray touch cannot reach them. If there is nothing to do (no unsaved change, no saved edit) the screen says so. Reload discards unsaved tweaks (keeping your saved edit), Save stores the current sound over the program in any bank (see Custom banks), Revert erases the saved edit and brings back the bank's own sound. In Schwung's own UI the module declares the host's preset browser (`list_param`/`count_param`/`name_param`), so the same list scrolls there; setting the `preset` parameter loads the program and `preset_name:N` returns any slot's label. A **jog click** toggles the list between bank order and alphabetical (A-Z, ordered by name only; the labels keep the `3-1` address), keeping the current program selected; the alphabetical index is built once at startup, and the host parameter `preset_sort` (0/1) does the same. Page 4 has no spare encoder, so the toggle sits on the jog click.
* **Voice modes** (Mode, shown as the words `Poly`, `Unison`, `Mono`, `Legato`): `Poly`, `Unison` (all `Voices` stacked on the last note, spread by Detune, +-30 cents at full Detune for five voices), `Mono` (one voice, last-note priority, envelopes retrigger and releasing returns to the held note) and `Legato` (as Mono, but overlapping notes change pitch without retriggering). **Voices** (1-5) is the polyphony in Poly and the stack size in Unison; it is a performance setting, not stored in presets. The old `unison` key still works (1 = Unison).
* Parameters not on a page are still exposed to the host as chain parameters, so Schwung can map or automate them: **PM-Dest, LFORate, LFOWave, Glide, Drift, LFODest**, Osc A pitch, Osc B fine / LFO mode / keyboard, noise colour, and the mod wheel.

## Custom banks (.zip, .syx, .h2p)

Drop third-party sound banks into the module's `banks/` folder (`/data/UserData/schwung/modules/sound_generators/tinypro5/banks/` on the Move) and reload the module; they are found once, when it is created. Nothing is converted offline.

* `Name.zip` - every `.h2p` (u-he **Repro-5** patches) and `.syx` (Prophet-5 `.syx` program dumps) inside becomes the bank `Name`; `Name.syx` is one bank; a folder (up to three levels, e.g. an extracted Repro-5 factory library) is one bank; loose `.h2p` files form `Loose`. Patches for other instruments (Repro-1, Zebra...) are skipped.
* **Select Bank** is its own page in Schwung's page menu (the same way TinyK does it): a list of every bank by name and size (`Factory (32)`, `Carbon for Repro-5 by Spekt (100)`, ...); click one to load it, and you land on that bank's preset list. A new bank loads its first program. Bank order / A-Z (**Order**, E8 on page 4) and the name list apply to the active bank. Save works on every bank, including imported ones: Saved edits are kept in `user/<bank>.tp5u` next to `banks/` (small text files, one per bank); the `.zip`, `.h2p` and `.syx` files are never modified. **Revert** (Voice+Sys, right after Save) drops the saved edit of the current program.
* `.h2p` patches are read from their text part (the `#cm=Section` / `Key=value` lines; the binary tail is ignored). Repro-5 dial values (0-100) map straight onto TinyPro5's calibrated dials; oscillator pitch uses Repro-5's measured law (octave, semitone, fine, -1.994 st). Keys missing from a patch keep the engine defaults. The mod matrix, LFO, effects, per-voice pan and velocity are not imported (TinyPro5 has no equivalents). `.syx` files are Prophet-5 program dumps that you supply; they use the Prophet-5 mapping described below, without any loudness trim (the limiter still guards the output).
* Your own bank files stay strictly local: `banks/`, `*.zip`, `*.h2p` and `*.syx` are in `.gitignore`. The package ships only a README in `banks/`.
* Scanning reads the files once at module creation; a 1,000-patch folder of loose files on a slow disk can take a while (one `.zip` is much faster). `bank_report` (host parameter) returns what was found.

Because the original spec lists some labels longer than 7 characters, they are shortened here (`Reso`, `FDecay`, `AWave`, `ADecay`...) to honour the "< 8 characters" rule.

## Poly-Mod / LFO destination values

* `PM-Dest` (bit mask): 0 Off, 1 FreqA, 2 PWA, 3 FreqA+PWA, 4 Filter, 5 FreqA+Filter, 6 PWA+Filter, 7 All.
* `LFODest`: 0 Pitch(A+B), 1 PWM(A+B), 2 Filter, 3 Pitch+Filter, 4 PWM+Filter, 5 Pitch+PWM, 6 All, 7 Off.

## Presets (32)

**The factory bank is 32 original programs, written for this engine** and compiled in (`src/presets_factory.c`): four groups of eight, styled after 1980s horror and sci-fi soundtrack synthesis. Nothing in it is copied or derived from another instrument's or library's patch data. Labels are `group-program name`, slot = (group-1) x 8 + (program-1).

| Group | Programs |
|---|---|
| **1 Basses and sequences** (mono, short filter decays, little release) | Night Drive Bass, Sync Stalker, Pulse Ostinato, Dead End Pluck, Square Chase, Heartbeat Sub, Detuned Chase, Grind Sequence |
| **2 Keys and plucks** | Glass Clap, Music Box Ghost, Poly-Mod Chime, Cold Keys, Hollow Fifths, Bright Stab, Whisper Pluck, Frozen Bell |
| **3 Pads and swells** (slow attacks) | Dark Brass Rise, Black Strings, Triangle Organ Bed, Hollow Choir, Cold Swell, Low Dread Pad, Stacked Fifths, Tritone Choir |
| **4 Stingers, drones and effects** | Falling Siren, Rising Alarm, Self-Osc Sweep, White Wind, Cross-Mod Drone, Sub Rumble, Machine Pulse, Dead Channel |

Group 1 is built for sequencing (Mono, Legato or a Unison stack); groups 2 and 3 are Poly for chords. The factory programs make use of the engine's character: hard sync (Sync Stalker), Osc B frequency-modulating Osc A through Poly-Mod (Poly-Mod Chime, Frozen Bell, Cross-Mod Drone), Osc B as a 6 Hz LFO into the filter (Machine Pulse), a self-oscillating filter with an empty oscillator mixer (Self-Osc Sweep), and the filter-envelope-to-pitch dive of Falling Siren. These are sounds written for TinyPro5's calibrated dials; they are not hardware-calibrated captures of any instrument.

Each program carries a small output gain so the bank sits within a few dB for a three-note chord at the default volume (`tests/test_audit.c` asserts it). User banks, edits and imports always take precedence: custom banks are separate banks after Factory, your Saved edits are kept per program (in `user/`, never in the bank files), and the last bank / program is restored from the saved state. A saved session that points past the end of the shorter factory list is clamped to the last program.

**Prophet-5 `.syx` banks you supply** are decoded by `src/h2p.c` (no such data ships with the module):

* **Scaling**: the 0-127 native values become dial positions (value / 127). Only the LFO rate/depth/destination mapping uses Melodee's published fits (rate `exp(-3.1005 + 0.05387 raw)`, depth `(raw/127)^1.737`), re-implemented here. Melodee's source is GPL-3.0 and none of it is included.
* **Caveat - this is not hardware calibration**: treat imported programs as faithful in *settings*, approximate in *absolute response*. The Rev 1/2 filter variants are not modelled separately.
* **Approximations**: TinyPro5 has one shared LFO (depth/routing is mapped from the dominant wheel destination), no noise-blend wheel source, no velocity switches, and a single Poly-Mod depth per source.
* **Engine change**: imported Osc A tuning needs a coarse pitch control, so `osca_pitch` (-12..+36 semitones) was added and `oscb_pitch` widened to -48..+60.

## Licence

TinyPro5 is MIT-licensed (`LICENSE`, which also carries the third-party attribution for the DEFLATE routine). See *Legal & Trademarks* at the end for the factory presets, trademarks and the LFO fits.

## Build, test, install

```sh
make test        # or scripts/test.sh  - native unit tests (gcc, clang or `zig cc`)
make arm         # or scripts/build.sh - AArch64 dsp.so + TinyPro5.tar.gz
```

`scripts/build.sh` picks `CROSS_PREFIX=...`, `aarch64-linux-gnu-gcc`, `zig cc -target aarch64-linux-gnu.2.28`, or Docker, in that order. Everything is compiled with `-std=c11 -Wall -Wextra -Werror -pedantic`.

Install on the Move (module directory per Schwung's `component_type` layout):

```sh
scp TinyPro5.tar.gz ableton@move.local:~/
ssh ableton@move.local 'mkdir -p /data/UserData/schwung/modules/sound_generators && tar -xzf ~/TinyPro5.tar.gz -C /data/UserData/schwung/modules/sound_generators/ && rm ~/TinyPro5.tar.gz'
```

then restart/refresh Schwung so it rescans modules.

## Layout

```
src/tinypro5.{h,c}   DSP engine (voices, filter, envelopes, poly-mod, LFO, state)
src/presets_factory.c  the 32 original factory programs (bank 0)
src/meta.{h,c}       chain_params / ui_hierarchy / module.json generated from the parameter table
src/plugin.c         Schwung plugin_api_v2 wrapper (static instance pool, no heap)
src/schwung_api.h    plugin/host ABI declarations
src/ui.js            Move OLED UI (4 pages x 8 encoders)
tests/test_prophet5.c
tools/               verify_presets.py (ctypes check), module.json generator, WAV renderer
scripts/             test.sh, build.sh, check_symbols.py
```

## Calibration notes

`TINYPRO5_OUTPUT_HEADROOM` (0.26) is set so that five voices, maximum resonance, both oscillators and noise at full level and master volume at maximum peak at **-7.6 dBFS before the limiter** from the engine defaults (the ceiling is -6.1 dBFS). That calibrated level (`diag_peak_pre`) is measured before the makeup and is checked for every factory program (five notes, full velocity, volume at maximum). After it comes `TINYPRO5_OUTPUT_MAKEUP` (2.0, +6 dB), and then a look-ahead limiter (1.5 ms look-ahead, 80 ms release, gain exactly 1 below the ceiling) that guarantees the Schwung master buffer never sees more than -6.1 dBFS even for pathological settings such as five coherent unison voices. On top of the makeup, each factory program carries its own **level gain** (the `PREG` factors and the `LIFT` constant in `src/presets_factory.c`, chosen from the engine's own renders): closed-filter, single-note mono and short plucked programs are lifted toward the loudness of the open polyphonic ones, and the whole bank is lifted 3 dB so its loudest three-note chord peaks near -8 dBFS. All 32 programs sit within 4 dB of the median loudest-30 ms RMS for a three-note chord at the default volume. Custom banks have no gain table (their programs play at their own level). Only dense, loud chords, or Volume above the default, make the limiter work. The test-suite asserts the pre-makeup calibrated peak, the limiter's behaviour (transparent below the ceiling, never over it, releases afterwards) and the int16 output peaks.

Single-note and low-cutoff patches are naturally much quieter than the worst case - that is the price of guaranteeing the worst case never clips.

## Legal & Trademarks

* The 32 factory presets in `src/presets_factory.c` are original works released under the MIT license. No proprietary patch data is distributed with TinyPro5.
* Prophet-5 and Sequential are registered trademarks of their respective owners. TinyPro5 is an independent project and is not affiliated with or endorsed by Sequential.
* The LFO rate and depth curves used when importing Prophet-5 `.syx` banks are derived from public numeric fits documented by the Melodee project (GPL-3.0) and independently re-implemented here; no Melodee source code is included.
* Banks you add yourself (`.syx`, `.h2p`, `.zip`) remain the property of their authors and stay on your device.
