# TinyPro5 user guide

A Prophet-5 style, 5-voice virtual analog synth for the Ableton Move (Schwung). Two oscillators with hard sync, an SSM-style
4-pole low-pass filter, Poly-Mod, one LFO, two ADSRs, analog-style drift, 32 original factory programs, and room for
your own Repro-5 (`.h2p`) and Prophet-5 (`.syx`) banks.

## Pages and encoders

Schwung draws the pages from the module's parameter description. There are four pages of eight encoder slots, plus a preset
browser, two "More" pages and a bank list. Switch pages with Left/Right or the Track buttons; Shift+Click jumps to any page.

| Page | E1 | E2 | E3 | E4 | E5 | E6 | E7 | E8 |
|---|---|---|---|---|---|---|---|---|
| 1 Filter | Cutoff | Reso | EnvAmt | KbdTrk | FAtk | FDecay | FSus | FRel |
| 2 Osc | AWave | APW | BWave | BPW | BPitch | Sync | PM-Fenv | PM-OscB |
| 3 Amp+Mix | AAtk | ADecay | ASus | ARel | AVol | BVol | Noise | Volume |
| 4 Voice+Sys | Mode | Voices | Detune | LFOAmt | Preset | Reload | Save | Order |

On page 4 the **Preset** slot is the **Presets** page (the host's preset browser), so the page itself shows the other seven
controls. On the device page 4 therefore reads **Mode, Voices, Detune, LFOAmt, Reload, Save, Order, Revert** on E1-E8.

* **EnvAmt** is the filter-envelope depth (negative values invert it); **KbdTrk** is key tracking of the cutoff.
* **PM-Fenv / PM-OscB** are the Poly-Mod depths from the filter envelope and from oscillator B. The destinations (PM-Dest) are on **More 1**.
* **Reload**, **Save** and **Revert** are push buttons that ask before they act: see *Reload, Save and Revert* below.
* **More 1**: PM-Dest, LFORate, LFOWave, LFODest, Glide, Drift, APitch, BFine. **More 2**: NColor, BLFO, BKbd, ModWhl.

## Reload, Save and Revert

Three buttons on page 4 manage the sound you are editing. They never act on a single press: each opens an "are you sure?" screen first.

| Button | Cell reads | What it does |
|---|---|---|
| **Reload** | `RELD` | Throws away every change you made since the program loaded and puts the sound back as it was: the bank's program, or your saved version if you have saved one. It also puts **Voices** back to the count it had when the program loaded. **Volume** and the mod wheel are not part of a sound and are left alone. |
| **Save** | `SAVE` | Keeps the sound you have now as the saved version of this program, in any bank (factory, `.syx`, `.h2p`, `.zip`). The bank files are never modified: the saved version goes to `user/<bank>.tp5u` next to `banks/`. |
| **Revert** | `REVT` | Erases the saved version of this program, so the bank's own sound comes back. It sits right after Save in the Voice+Sys list and is the last cell (E8) on the page. |

A button whose cell reads **`NONE`** has nothing to do: Reload and Save need an unsaved change, Revert needs a saved version.

**Opening a button.** On the knob grid, **hold the knob** (touch it) and **click the jog**. Turning the knob does nothing, and a click with no knob held opens the
section picker instead. In the list view (Param View: List) just click the row.

**The screen.** It shows the question (for example `SAVE OVER PROGRAM?`), what will happen and the program's name, with two buttons. **No** is selected when it opens.
Turn the jog right for **Yes**, left for **No**, and **click** to confirm. **Back** (or Shift+jog) cancels without doing anything. If there is nothing to do
the screen says so and you press the click or Back to close it.

## Presets and A-Z order

Turn the Presets page to scroll; the program loads as you go. Factory programs are listed as `group-program name`:
`3-1 Dark Brass Rise` is group 3, program 1. The default **Bank order** follows those addresses.

| Group | What is in it |
|---|---|
| 1 Basses and sequences | Mono programs for ostinatos and sequences: Night Drive Bass, Sync Stalker, Pulse Ostinato, Dead End Pluck, Square Chase, Heartbeat Sub, Detuned Chase, Grind Sequence. |
| 2 Keys and plucks | Poly: Glass Clap, Music Box Ghost, Poly-Mod Chime, Cold Keys, Hollow Fifths, Bright Stab, Whisper Pluck, Frozen Bell. |
| 3 Pads and swells | Poly, slow attacks: Dark Brass Rise, Black Strings, Triangle Organ Bed, Hollow Choir, Cold Swell, Low Dread Pad, Stacked Fifths, Tritone Choir. |
| 4 Stingers, drones and effects | Falling Siren, Rising Alarm, Self-Osc Sweep (the filter sings by itself), White Wind, Cross-Mod Drone, Sub Rumble, Machine Pulse, Dead Channel. |

Set **Order** (page 4) to **A-Z** to list the same programs alphabetically by name; the `3-1` address stays in front of each
name. Switching order keeps the current program selected. In the standalone loader UI the same switch is a **jog click** on page 4.

## Voice modes

| Mode | What it does |
|---|---|
| Poly | Up to **Voices** (1-5) notes at once; when all are busy the quietest releasing, then the oldest, voice is taken. |
| Unison | **Voices** voices stack on the last note you played; **Detune** spreads them (plus or minus 30 cents at full Detune with five voices). Level is trimmed as voices are added. |
| Mono | One voice, last-note priority. Every new note retriggers the envelopes; releasing the top note returns to the one still held. |
| Legato | Like Mono, but overlapping notes only change pitch (no retrigger); a note after silence triggers normally. **Glide** is on More 1. |

Changing mode releases held notes. **Voices** and the mode are performance settings: loading a preset never changes Voices.

## Custom banks (.zip, .syx, .h2p)

Copy files into the module's `banks/` folder (`/data/UserData/schwung/modules/sound_generators/tinypro5/banks/`) and reload the
module; banks are found once, when the module starts.

* **`.zip`**: every `.h2p` (u-he Repro-5 patches) and `.syx` (Prophet-5 `.syx` program dumps) inside becomes one bank named after the zip.
* **`.syx`**: one Prophet-5 program dump file is one bank.
* **Folder**: an extracted bank (up to three folder levels) is one bank named after the folder.
* **Loose `.h2p` files** form a bank called `Loose`.

Open **Select Bank** (the last page in the menu) to see every bank by name and size, click one to load it, and you land on its
preset list. Bank 1 is always the 32 factory programs. Patches for other instruments (for example Repro-1) are skipped.

What is imported from a `.h2p`: oscillators (octave, semitone, fine, waves, pulse width, sync), mixer, filter, both envelopes,
glide, Poly-Mod. Not imported: the modulation matrix, LFO, effects, per-voice pan and velocity. Missing values keep the engine
default. Save works in every bank: Saved edits are kept in `user/<bank>.tp5u` next to `banks/` (small text files, one per bank); the `.zip`, `.h2p` and `.syx` files are never modified. **Revert** (Voice+Sys, right after Save) drops the saved edit of the current program. If a bank file later changes so that a program has another name, its old saved edit is ignored. Imported patches are not loudness-trimmed, so the output
limiter does the protecting.

Your bank files stay on the device and out of version control (`banks/` is git-ignored).

## Factory bank and attribution

Bank 1 ("Factory") is 32 original programs written for TinyPro5 and compiled into the module; no third-party patch data ships with it,
and the `banks/` folder of the package holds only a README. Your edits (Save) and your own banks never overwrite it. Prophet-5 is a
trademark of Sequential; TinyPro5 is not affiliated with or endorsed by Sequential. If you load a Prophet-5 `.syx` or a Repro-5 `.h2p`
bank yourself, that data stays its owner's property.

## Notes

* This is a software emulation, not a hardware calibration.
* Factory programs are level-matched: each carries a gain so a three-note chord lands within a few dB of the same loudness whichever you pick (custom banks play at their own level).
* The output is limited to a ceiling of -6.1 dBFS by a look-ahead limiter that sits after a +6 dB makeup gain. It does nothing at normal playing levels; very dense, loud chords (or Volume turned up above its default) are pulled down smoothly instead of clipping. It adds 1.5 ms of latency.
