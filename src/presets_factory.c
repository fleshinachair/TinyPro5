/*
 * Factory bank (bank 0): 32 original programs, compiled in, in four groups of eight. They are written for this engine and are
 * styled after 1980s horror / sci-fi soundtrack synthesis (hard-sync and pulse basses, cold plucks and bells, slow analog
 * swells, self-oscillating sweeps and cross-modulated drones). Nothing here is copied from, or derived from, any other
 * instrument's or library's patch data.
 *
 * Values are in the engine's own units (src/tinypro5.h, tp5_params): knobs 0..1, pitches in semitones, switches 0/1, PM-Dest
 * 0..7 (bit 1 = Osc A freq, 2 = Osc A pulse width, 4 = filter), LFO-Dest 0..7 (0 pitch, 1 PWM, 2 filter, 7 off), voice mode
 * 0 Poly / 1 Unison / 2 Mono / 3 Legato. A parameter that is not listed keeps the engine default (tp5_params[].def).
 * Times follow the engine's laws: AAtk .3 = 15 ms ramp, .5 = 133 ms, .7 = 1.2 s; ADecay / ARel .3 = 9 / 11 ms, .5 = 72 / 94 ms,
 * .7 = 0.6 / 0.8 s. The filter envelope is 1.45x slower for the same knob. Per-program loudness is set by the mixer levels
 * (tests/test_audit.c keeps the bank within a few dB and under the output ceiling).
 */
#include "tinypro5.h"

#define K(id, val) { (uint8_t)P_##id, (val) }
/* Program gain = LIFT x the per-program factor. The factors level the bank against itself (a closed filter, a single mono note or a short
 * pluck needs more than an open polyphonic pad); LIFT then moves the whole bank up 3 dB so its loudest three-note chord peaks near -8 dBFS.
 * The look-ahead limiter still holds the -6.1 dBFS ceiling. */
#define LIFT 1.4f
#define PREG(nm, g, ...) { nm, (const tp5_kv[]){ __VA_ARGS__ }, \
    (int)(sizeof((const tp5_kv[]){ __VA_ARGS__ }) / sizeof(tp5_kv)), LIFT * (g) }
#define PRE(nm, ...) PREG(nm, 1.0f, __VA_ARGS__)       /* factor 1: the mixer levels alone set the loudness */

const tp5_preset_def tp5_presets[TINYPRO5_PRESET_COUNT] = {
    /* ---- 1: driving basses and sequences ------------------------------------------------------------------------------ */
    /* 1-1 saw over a detuned saw an octave down, snappy filter decay, mono */
    PRE("Night Drive Bass", K(UNISON,2.f), K(A_WAVE,0.f), K(A_VOL,0.75f), K(B_WAVE,0.f), K(B_VOL,0.65f), K(B_SEMI,-12.f), K(B_FINE,4.f),
        K(CUTOFF,0.38f), K(RESO,0.22f), K(ENVAMT,0.40f), K(F_ATK,0.f), K(F_DEC,0.34f), K(F_SUS,0.12f), K(F_REL,0.25f),
        K(A_ATK,0.02f), K(A_DEC,0.45f), K(A_SUS,0.85f), K(A_REL,0.24f), K(KBD,0.5f), K(DRIFT,0.25f)),
    /* 1-2 hard-sync lead-bass: Osc A is synced to B and its pitch dives from a Poly-Mod envelope */
    PREG("Sync Stalker", 2.0f, K(UNISON,2.f), K(SYNC,1.f), K(A_WAVE,0.f), K(A_VOL,0.8f), K(A_SEMI,5.f), K(B_VOL,0.f),
        K(PM_FENV,0.20f), K(PM_DEST,1.f), K(CUTOFF,0.55f), K(RESO,0.20f), K(ENVAMT,0.25f), K(F_DEC,0.38f), K(F_SUS,0.10f),
        K(A_ATK,0.02f), K(A_DEC,0.45f), K(A_SUS,0.85f), K(A_REL,0.24f), K(KBD,0.6f)),
    /* 1-3 two detuned pulses with a slow pulse-width sweep */
    PRE("Pulse Ostinato", K(UNISON,2.f), K(A_WAVE,1.f), K(A_PW,0.38f), K(A_VOL,0.7f), K(B_WAVE,1.f), K(B_PW,0.62f), K(B_VOL,0.6f),
        K(B_FINE,7.f), K(LFO_DEST,1.f), K(LFO_WAVE,1.f), K(LFO_RATE,0.28f), K(LFO_AMT,0.45f),
        K(CUTOFF,0.46f), K(RESO,0.20f), K(ENVAMT,0.30f), K(F_DEC,0.32f), K(F_SUS,0.20f),
        K(A_ATK,0.02f), K(A_DEC,0.40f), K(A_SUS,0.75f), K(A_REL,0.24f), K(KBD,0.6f)),
    /* 1-4 very short plucked bass for sixteenth notes: no sustain, minimal release */
    PREG("Dead End Pluck", 2.4f, K(UNISON,2.f), K(A_WAVE,0.f), K(A_VOL,0.8f), K(B_WAVE,0.f), K(B_VOL,0.55f), K(B_SEMI,-12.f),
        K(CUTOFF,0.30f), K(RESO,0.30f), K(ENVAMT,0.60f), K(F_DEC,0.27f), K(F_SUS,0.f), K(F_REL,0.22f),
        K(A_ATK,0.01f), K(A_DEC,0.45f), K(A_SUS,0.f), K(A_REL,0.20f), K(KBD,0.5f)),
    /* 1-5 square plus a triangle sub */
    PRE("Square Chase", K(UNISON,2.f), K(A_WAVE,1.f), K(A_PW,0.5f), K(A_VOL,0.7f), K(B_WAVE,0.5f), K(B_VOL,0.7f), K(B_SEMI,-12.f),
        K(CUTOFF,0.50f), K(RESO,0.10f), K(ENVAMT,0.20f), K(F_DEC,0.30f), K(F_SUS,0.30f),
        K(A_ATK,0.02f), K(A_DEC,0.40f), K(A_SUS,0.85f), K(A_REL,0.25f), K(KBD,0.7f)),
    /* 1-6 a low pulse of sub bass, closed filter */
    PREG("Heartbeat Sub", 1.6f, K(UNISON,2.f), K(A_WAVE,0.f), K(A_VOL,0.35f), K(A_SEMI,-12.f), K(B_WAVE,0.5f), K(B_VOL,0.9f), K(B_SEMI,-12.f),
        K(CUTOFF,0.40f), K(RESO,0.05f), K(ENVAMT,0.20f), K(F_DEC,0.35f), K(F_SUS,0.20f),
        K(A_ATK,0.02f), K(A_DEC,0.50f), K(A_SUS,0.55f), K(A_REL,0.30f), K(KBD,0.3f)),
    /* 1-7 stacked, detuned unison saws */
    PRE("Detuned Chase", K(UNISON,1.f), K(DETUNE,0.40f), K(A_WAVE,0.f), K(A_VOL,0.7f), K(B_WAVE,0.f), K(B_VOL,0.7f), K(B_FINE,9.f),
        K(CUTOFF,0.45f), K(RESO,0.20f), K(ENVAMT,0.40f), K(F_DEC,0.32f), K(F_SUS,0.15f),
        K(A_ATK,0.02f), K(A_DEC,0.40f), K(A_SUS,0.75f), K(A_REL,0.25f), K(KBD,0.6f)),
    /* 1-8 resonant, slightly gliding saw/pulse grind with a breath of noise */
    PREG("Grind Sequence", 3.2f, K(UNISON,2.f), K(A_WAVE,0.f), K(A_VOL,0.7f), K(B_WAVE,1.f), K(B_PW,0.30f), K(B_VOL,0.5f), K(B_SEMI,-12.f),
        K(NOISE,0.05f), K(NOISE_COL,0.2f), K(GLIDE,0.12f), K(CUTOFF,0.33f), K(RESO,0.55f), K(ENVAMT,0.50f), K(F_DEC,0.28f), K(F_SUS,0.f),
        K(A_ATK,0.01f), K(A_DEC,0.42f), K(A_SUS,0.30f), K(A_REL,0.22f), K(KBD,0.5f)),

    /* ---- 2: eerie keys and plucks ------------------------------------------------------------------------------------- */
    /* 2-1 noise burst over a square: a bright, dry clap-pluck */
    PREG("Glass Clap", 2.0f, K(NOISE,0.45f), K(NOISE_COL,0.f), K(A_WAVE,1.f), K(A_PW,0.5f), K(A_VOL,0.5f), K(A_SEMI,12.f),
        K(CUTOFF,0.60f), K(RESO,0.40f), K(ENVAMT,0.45f), K(F_DEC,0.38f), K(F_SUS,0.f), K(F_REL,0.30f),
        K(A_ATK,0.f), K(A_DEC,0.47f), K(A_SUS,0.f), K(A_REL,0.35f), K(KBD,1.f)),
    /* 2-2 thin pulse and triangle a twelfth up, resonant: a music box */
    PREG("Music Box Ghost", 1.75f, K(A_WAVE,1.f), K(A_PW,0.20f), K(A_VOL,0.55f), K(A_SEMI,12.f), K(B_WAVE,0.5f), K(B_VOL,0.55f), K(B_SEMI,19.f),
        K(CUTOFF,0.72f), K(RESO,0.55f), K(ENVAMT,0.20f), K(F_DEC,0.50f), K(F_SUS,0.f), K(F_REL,0.45f),
        K(A_ATK,0.f), K(A_DEC,0.66f), K(A_SUS,0.f), K(A_REL,0.50f), K(KBD,1.f), K(DRIFT,0.15f)),
    /* 2-3 Osc B (triangle, a detuned fifth up) frequency-modulates Osc A: an inharmonic chime */
    PRE("Poly-Mod Chime", K(A_WAVE,0.f), K(A_VOL,0.65f), K(A_SEMI,12.f), K(B_WAVE,0.5f), K(B_VOL,0.f), K(B_SEMI,7.f), K(B_FINE,11.f),
        K(PM_OSCB,0.40f), K(PM_DEST,1.f), K(CUTOFF,0.80f), K(RESO,0.20f), K(ENVAMT,0.f),
        K(A_ATK,0.f), K(A_DEC,0.68f), K(A_SUS,0.f), K(A_REL,0.55f), K(KBD,1.f)),
    /* 2-4 cold, hollow electric-piano style keys */
    PRE("Cold Keys", K(A_WAVE,1.f), K(A_PW,0.30f), K(A_VOL,0.40f), K(B_WAVE,0.f), K(B_VOL,0.30f), K(B_FINE,5.f),
        K(CUTOFF,0.52f), K(RESO,0.20f), K(ENVAMT,0.35f), K(F_DEC,0.50f), K(F_SUS,0.20f), K(F_REL,0.40f),
        K(A_ATK,0.01f), K(A_DEC,0.58f), K(A_SUS,0.30f), K(A_REL,0.42f), K(KBD,0.8f)),
    /* 2-5 two squares a fifth apart */
    PRE("Hollow Fifths", K(A_WAVE,1.f), K(A_PW,0.5f), K(A_VOL,0.6f), K(B_WAVE,1.f), K(B_PW,0.5f), K(B_VOL,0.55f), K(B_SEMI,7.f),
        K(CUTOFF,0.50f), K(RESO,0.30f), K(ENVAMT,0.30f), K(F_DEC,0.45f), K(F_SUS,0.15f), K(F_REL,0.40f),
        K(A_ATK,0.01f), K(A_DEC,0.55f), K(A_SUS,0.25f), K(A_REL,0.40f), K(KBD,0.8f)),
    /* 2-6 bright saw stab, octave-doubled */
    PRE("Bright Stab", K(A_WAVE,0.f), K(A_VOL,0.7f), K(B_WAVE,0.f), K(B_VOL,0.6f), K(B_SEMI,12.f), K(B_FINE,6.f),
        K(CUTOFF,0.66f), K(RESO,0.15f), K(ENVAMT,0.25f), K(F_DEC,0.38f), K(F_SUS,0.10f),
        K(A_ATK,0.f), K(A_DEC,0.48f), K(A_SUS,0.10f), K(A_REL,0.30f), K(KBD,0.8f)),
    /* 2-7 pink noise rung by a high-resonance filter: a breathy tuned pluck */
    PRE("Whisper Pluck", K(NOISE,0.55f), K(NOISE_COL,0.75f), K(A_WAVE,1.f), K(A_PW,0.5f), K(A_VOL,0.25f),
        K(CUTOFF,0.58f), K(RESO,0.72f), K(ENVAMT,0.35f), K(F_DEC,0.45f), K(F_SUS,0.f),
        K(A_ATK,0.f), K(A_DEC,0.50f), K(A_SUS,0.f), K(A_REL,0.40f), K(KBD,1.f)),
    /* 2-8 a slowly dying bell: Osc B detuned far from Osc A, deep cross-modulation */
    PRE("Frozen Bell", K(A_WAVE,0.f), K(A_VOL,0.55f), K(A_SEMI,12.f), K(B_WAVE,0.5f), K(B_VOL,0.f), K(B_SEMI,14.f), K(B_FINE,-23.f),
        K(PM_OSCB,0.55f), K(PM_DEST,1.f), K(CUTOFF,0.85f), K(RESO,0.15f), K(ENVAMT,0.f),
        K(A_ATK,0.f), K(A_DEC,0.76f), K(A_SUS,0.f), K(A_REL,0.60f), K(KBD,1.f), K(DRIFT,0.40f)),

    /* ---- 3: atmospheric pads and swells ------------------------------------------------------------------------------- */
    /* 3-1 a slow, dark brass-style swell that opens its filter */
    PRE("Dark Brass Rise", K(A_WAVE,0.f), K(A_VOL,0.7f), K(B_WAVE,0.f), K(B_VOL,0.7f), K(B_FINE,8.f),
        K(CUTOFF,0.30f), K(RESO,0.20f), K(ENVAMT,0.60f), K(F_ATK,0.62f), K(F_DEC,0.60f), K(F_SUS,0.60f), K(F_REL,0.50f),
        K(A_ATK,0.55f), K(A_DEC,0.60f), K(A_SUS,0.85f), K(A_REL,0.50f), K(KBD,0.7f), K(DRIFT,0.35f)),
    /* 3-2 two detuned saws with heavy drift and a slow filter drift: string wash */
    PRE("Black Strings", K(A_WAVE,0.f), K(A_VOL,0.43f), K(B_WAVE,0.f), K(B_VOL,0.40f), K(B_FINE,11.f),
        K(LFO_DEST,2.f), K(LFO_WAVE,1.f), K(LFO_RATE,0.20f), K(LFO_AMT,0.10f),
        K(CUTOFF,0.45f), K(RESO,0.12f), K(ENVAMT,0.20f), K(F_ATK,0.50f), K(F_DEC,0.60f), K(F_SUS,0.60f), K(F_REL,0.60f),
        K(A_ATK,0.62f), K(A_DEC,0.60f), K(A_SUS,0.90f), K(A_REL,0.66f), K(KBD,0.7f), K(DRIFT,0.50f)),
    /* 3-3 square plus a triangle octave up, slow vibrato */
    PRE("Triangle Organ Bed", K(A_WAVE,1.f), K(A_PW,0.5f), K(A_VOL,0.28f), K(B_WAVE,0.5f), K(B_VOL,0.40f), K(B_SEMI,12.f),
        K(LFO_DEST,0.f), K(LFO_WAVE,1.f), K(LFO_RATE,0.45f), K(LFO_AMT,0.12f),
        K(CUTOFF,0.62f), K(RESO,0.08f), K(ENVAMT,0.f),
        K(A_ATK,0.30f), K(A_DEC,0.50f), K(A_SUS,0.92f), K(A_REL,0.38f), K(KBD,0.8f)),
    /* 3-4 two pulses with opposed width modulation: a hollow, breathing choir */
    PRE("Hollow Choir", K(A_WAVE,1.f), K(A_PW,0.5f), K(A_VOL,0.30f), K(B_WAVE,1.f), K(B_PW,0.5f), K(B_VOL,0.28f), K(B_FINE,8.f),
        K(LFO_DEST,1.f), K(LFO_WAVE,1.f), K(LFO_RATE,0.20f), K(LFO_AMT,0.50f),
        K(CUTOFF,0.48f), K(RESO,0.20f), K(ENVAMT,0.15f), K(F_ATK,0.50f),
        K(A_ATK,0.62f), K(A_SUS,0.90f), K(A_REL,0.66f), K(KBD,0.7f)),
    /* 3-5 a closed filter that takes seconds to open */
    PRE("Cold Swell", K(A_WAVE,0.f), K(A_VOL,0.7f), K(B_WAVE,0.f), K(B_VOL,0.6f), K(B_SEMI,7.f),
        K(CUTOFF,0.22f), K(RESO,0.25f), K(ENVAMT,0.80f), K(F_ATK,0.78f), K(F_DEC,0.75f), K(F_SUS,0.60f), K(F_REL,0.65f),
        K(A_ATK,0.68f), K(A_DEC,0.60f), K(A_SUS,0.90f), K(A_REL,0.70f), K(KBD,0.8f)),
    /* 3-6 an octave-down saw and triangle, slow filter movement */
    PRE("Low Dread Pad", K(A_WAVE,0.f), K(A_VOL,0.7f), K(A_SEMI,-12.f), K(B_WAVE,0.5f), K(B_VOL,0.6f), K(B_SEMI,-12.f),
        K(LFO_DEST,2.f), K(LFO_WAVE,1.f), K(LFO_RATE,0.12f), K(LFO_AMT,0.25f),
        K(CUTOFF,0.32f), K(RESO,0.30f), K(ENVAMT,0.25f), K(F_ATK,0.60f),
        K(A_ATK,0.72f), K(A_SUS,0.90f), K(A_REL,0.74f), K(KBD,0.5f)),
    /* 3-7 open fifths on every key */
    PRE("Stacked Fifths", K(A_WAVE,0.f), K(A_VOL,0.65f), K(B_WAVE,0.f), K(B_VOL,0.65f), K(B_SEMI,7.f), K(B_FINE,4.f),
        K(LFO_DEST,2.f), K(LFO_WAVE,1.f), K(LFO_RATE,0.15f), K(LFO_AMT,0.10f),
        K(CUTOFF,0.55f), K(RESO,0.18f), K(ENVAMT,0.25f), K(F_ATK,0.60f), K(F_DEC,0.70f), K(F_SUS,0.50f),
        K(A_ATK,0.66f), K(A_SUS,0.90f), K(A_REL,0.68f), K(KBD,0.7f), K(DRIFT,0.45f)),
    /* 3-8 a tritone above every key: an uneasy choir */
    PRE("Tritone Choir", K(A_WAVE,0.35f), K(A_PW,0.35f), K(A_VOL,0.6f), K(B_WAVE,0.5f), K(B_VOL,0.6f), K(B_SEMI,6.f), K(B_FINE,5.f),
        K(LFO_DEST,2.f), K(LFO_WAVE,1.f), K(LFO_RATE,0.12f), K(LFO_AMT,0.20f),
        K(CUTOFF,0.50f), K(RESO,0.25f), K(ENVAMT,0.20f), K(F_ATK,0.50f),
        K(A_ATK,0.60f), K(A_SUS,0.90f), K(A_REL,0.66f), K(KBD,0.7f)),

    /* ---- 4: stingers, drones and Poly-Mod effects --------------------------------------------------------------------- */
    /* 4-1 the pitch starts nearly two octaves up and falls away (Poly-Mod from the filter envelope into Osc A) */
    PREG("Falling Siren", 2.4f, K(UNISON,3.f), K(A_WAVE,0.f), K(A_VOL,0.7f), K(A_SEMI,12.f), K(PM_FENV,0.75f), K(PM_DEST,1.f),
        K(F_ATK,0.f), K(F_DEC,0.72f), K(F_SUS,0.f), K(CUTOFF,0.75f), K(RESO,0.25f), K(ENVAMT,0.f),
        K(A_ATK,0.02f), K(A_DEC,0.50f), K(A_SUS,0.90f), K(A_REL,0.55f), K(KBD,0.8f)),
    /* 4-2 a rising saw LFO on the pitch: a two-tone alarm */
    PRE("Rising Alarm", K(UNISON,3.f), K(A_WAVE,1.f), K(A_PW,0.5f), K(A_VOL,0.7f), K(B_WAVE,1.f), K(B_PW,0.5f), K(B_VOL,0.45f), K(B_SEMI,7.f),
        K(LFO_DEST,0.f), K(LFO_WAVE,0.f), K(LFO_RATE,0.50f), K(LFO_AMT,0.85f),
        K(CUTOFF,0.60f), K(RESO,0.25f), K(ENVAMT,0.f), K(A_ATK,0.05f), K(A_SUS,0.90f), K(A_REL,0.40f), K(KBD,0.8f)),
    /* 4-3 the mixer is silent: the filter sings by itself and its envelope sweeps the pitch */
    PRE("Self-Osc Sweep", K(UNISON,2.f), K(A_VOL,0.f), K(B_VOL,0.f), K(NOISE,0.03f),
        K(CUTOFF,0.32f), K(RESO,1.f), K(ENVAMT,0.55f), K(F_ATK,0.55f), K(F_DEC,0.65f), K(F_SUS,0.35f), K(F_REL,0.50f),
        K(A_ATK,0.30f), K(A_DEC,0.50f), K(A_SUS,0.85f), K(A_REL,0.50f), K(KBD,1.f)),
    /* 4-4 white noise through a slowly moving resonant filter */
    PREG("White Wind", 1.5f, K(NOISE,1.f), K(NOISE_COL,0.f), K(A_VOL,0.f),
        K(LFO_DEST,2.f), K(LFO_WAVE,1.f), K(LFO_RATE,0.10f), K(LFO_AMT,0.50f),
        K(CUTOFF,0.56f), K(RESO,0.60f), K(ENVAMT,0.f), K(A_ATK,0.74f), K(A_SUS,0.90f), K(A_REL,0.76f), K(KBD,0.4f)),
    /* 4-5 a stacked tritone drone with Osc B frequency-modulating Osc A */
    PRE("Cross-Mod Drone", K(UNISON,1.f), K(DETUNE,0.55f), K(A_WAVE,0.f), K(A_VOL,0.65f), K(B_WAVE,0.f), K(B_VOL,0.5f), K(B_SEMI,6.f), K(B_FINE,14.f),
        K(PM_OSCB,0.22f), K(PM_DEST,1.f), K(CUTOFF,0.40f), K(RESO,0.30f), K(ENVAMT,0.10f),
        K(A_ATK,0.70f), K(A_SUS,1.f), K(A_REL,0.80f), K(KBD,0.6f)),
    /* 4-6 two octaves of stacked saws, nearly closed filter */
    PRE("Sub Rumble", K(UNISON,1.f), K(DETUNE,0.50f), K(A_WAVE,0.f), K(A_VOL,0.7f), K(A_SEMI,-12.f), K(B_WAVE,0.f), K(B_VOL,0.6f), K(B_SEMI,-24.f), K(B_FINE,7.f),
        K(LFO_DEST,2.f), K(LFO_WAVE,1.f), K(LFO_RATE,0.06f), K(LFO_AMT,0.30f),
        K(CUTOFF,0.26f), K(RESO,0.35f), K(ENVAMT,0.f), K(A_ATK,0.60f), K(A_SUS,1.f), K(A_REL,0.80f), K(KBD,0.3f)),
    /* 4-7 Osc B runs as an LFO (about 6 Hz, pulse) and modulates the filter: a rhythmic machine pulse */
    PRE("Machine Pulse", K(A_WAVE,0.f), K(A_VOL,0.75f), K(B_LFO,1.f), K(B_WAVE,0.85f), K(B_VOL,0.f), K(B_SEMI,4.f),
        K(PM_OSCB,0.18f), K(PM_DEST,4.f), K(CUTOFF,0.45f), K(RESO,0.45f), K(ENVAMT,0.f),
        K(A_ATK,0.02f), K(A_SUS,0.90f), K(A_REL,0.35f), K(KBD,0.8f)),
    /* 4-8 pink noise and a high-resonance filter jumping on a square LFO: interference */
    PRE("Dead Channel", K(NOISE,0.35f), K(NOISE_COL,0.90f), K(A_VOL,0.f),
        K(LFO_DEST,2.f), K(LFO_WAVE,2.f), K(LFO_RATE,0.55f), K(LFO_AMT,0.30f),
        K(CUTOFF,0.72f), K(RESO,0.60f), K(ENVAMT,0.10f), K(A_ATK,0.35f), K(A_SUS,0.90f), K(A_REL,0.60f), K(KBD,1.f)),
};
