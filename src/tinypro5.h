/*
 * TinyPro5 - Prophet-5 style virtual analog polysynth engine (Rev 2 voicing, or Rev 4 voicing with the rev switch).
 *
 * Pure C11, no dynamic allocation anywhere, no globals mutated by the audio path.
 * The engine is host-agnostic; src/plugin.c adapts it to the Schwung v2 plugin ABI.
 */
#ifndef TINYPRO5_H
#define TINYPRO5_H

#include <stddef.h>
#include <stdint.h>

#define TINYPRO5_VOICES          5
#define TINYPRO5_PRESET_COUNT    32    /* the factory bank (bank 0, src/presets_factory.c): 4 groups x 8 programs */
#define TINYPRO5_PRESET_MAX      1024  /* most presets in any one bank, factory or custom (src/bank.h) */

/* Master bus: the limiter ceiling is -6.1 dBFS; the headroom scale is applied
 * before the limiter and calibrated (see tests/test_prophet5.c) so that the
 * limiter does not engage in the full-polyphony / max-resonance case. */
#define TINYPRO5_CEILING_DBFS    (-6.1f)
#define TINYPRO5_CEILING_LIN     0.4950f /* 20*log10(0.4950) = -6.11 dBFS */
#define TINYPRO5_OUTPUT_HEADROOM 0.2600f
/* Makeup gain, applied AFTER the calibrated level (diag_peak_pre is measured before it, so the headroom contract above is unchanged)
 * and BEFORE the look-ahead limiter, which keeps the output under the ceiling. +6 dB: with it every factory program's single note and
 * three-note chord stays under the ceiling at the default volume; only dense, loud chords make the limiter work. */
#define TINYPRO5_OUTPUT_MAKEUP   2.0f
/* On top of that, each factory program carries its own gain (src/presets_factory.c, 1..8): programs whose sound is naturally quiet (a closed
 * filter, a single mono note, a short pluck) are lifted toward the loudness of the open polyphonic ones, so the bank sits within a few dB
 * (tests/test_audit.c). The look-ahead limiter still holds the ceiling. */
#define TP5_LIMITER_LOOKAHEAD    128   /* maximum look-ahead in samples (the engine uses 1.5 ms of it) */

/* Parameter ids. Everything before P_VOICES except P_VOLUME / P_MODWHEEL is a
 * "sound" parameter that presets own. */
typedef enum {
    P_CUTOFF, P_RESO, P_ENVAMT,
    P_F_ATK, P_F_DEC, P_F_SUS, P_F_REL,
    P_A_ATK, P_A_DEC, P_A_SUS, P_A_REL,
    P_KBD, P_DRIFT, P_VOLUME,
    P_A_WAVE, P_B_WAVE, P_A_SEMI, P_B_SEMI, P_B_FINE, P_SYNC, P_A_PW, P_B_PW,
    P_A_VOL, P_B_VOL, P_NOISE, P_NOISE_COL, P_GLIDE, P_B_LFO, P_B_KBD,
    P_PM_FENV, P_PM_OSCB, P_PM_DEST,
    P_LFO_RATE, P_LFO_WAVE, P_LFO_DEST, P_LFO_AMT, P_MODWHEEL,
    P_UNISON,               /* voice mode: 0 Poly, 1 Unison, 2 Mono, 3 Legato (key voice_mode, alias unison) */
    P_DETUNE,               /* unison spread */
    P_REV,                  /* hardware revision voicing: REV_2 (SSM 2040 style filter, analog envelopes: the original engine) or REV_4 (CEM 3320 style filter, fast envelopes, velocity + aftertouch) */
    P_VINTAGE,              /* 0..1 macro: extra voice-to-voice component drift, envelope variance and oscillator micro-detune (0 adds nothing) */
    P_VEL_FILT,             /* REV_4: how much note velocity scales the filter envelope (internal: not on a front-panel page, programs carry it) */
    P_VEL_AMP,              /* REV_4: how much note velocity scales the amp envelope (internal, as above) */
    P_VOICES,               /* voice count 1..5: polyphony in Poly, stack size in Unison; a performance setting, not owned by presets */
    P_BANK,                 /* active bank: 0 = the 32 factory programs, 1.. = custom banks found in banks/ (src/bank.h) */
    P_SORT,                 /* preset list order: 0 = bank order (1-1 ...), 1 = alphabetical */
    P_PRESET,               /* position in the active bank's list, in the order P_SORT selects (bank order: the slot itself) */
    P_LOAD, P_SAVE,
    P_REVERT,               /* drop the saved edit of the current program and reload the bank's original */
    P_COUNT
} tp5_param_id;

typedef struct {
    const char *key;   /* set_param/get_param key                     */
    const char *label; /* OLED label, strictly < 8 characters         */
    float min, max, def;
    uint8_t is_int;
} tp5_param_info;

extern const tp5_param_info tp5_params[P_COUNT];

/* Values of P_REV. A program that does not list rev is REV_2 with vintage 0 and no velocity modulation: the engine as it was before the toggle. */
enum { REV_2 = 0, REV_4 = 1 };
/* Choosing a revision with tp5_set_param(P_REV) also sets that revision's default vintage (loading a program or restoring a state does not). */
#define TP5_VINTAGE_REV2_DEF  0.65f
#define TP5_VINTAGE_REV4_DEF  0.00f
#define TP5_VEL_FILT_REV4_DEF 0.70f
#define TP5_VEL_AMP_REV4_DEF  0.60f

int  tp5_param_id_from_key(const char *key); /* -1 when unknown */
int  tp5_is_sound_param(int id);

/* ---- presets ---- */
typedef struct { uint8_t id; float v; } tp5_kv;
typedef struct { const char *name; const tp5_kv *kv; int n; float gain; } tp5_preset_def;   /* gain: this program's own output level (>= 1), see TINYPRO5_OUTPUT_MAKEUP */
extern const tp5_preset_def tp5_presets[TINYPRO5_PRESET_COUNT];

/* ---- SSM 2040 style 4-pole low-pass (exposed for unit tests) ---- */
typedef struct { float s[4]; } tp5_ladder;
void  tp5_ladder_reset(tp5_ladder *f);
/* g = tan(pi*fc/fs), k = resonance feedback (self-oscillation at ~4). */
float tp5_ladder_tick(tp5_ladder *f, float x, float g, float k);
/* The same with the REV_4 voicing blended in by rev = 0..1. rev 0 is tp5_ladder_tick bit for bit; rev 1 gives back part of the passband that the
 * resonance feedback takes away (less bass drop) and moves the saturation knee up (cleaner self-oscillation, less pitch pull). */
float tp5_ladder_tick_rev(tp5_ladder *f, float x, float g, float k, float rev);

/* ---- engine ---- */
enum { ENV_IDLE = 0, ENV_ATK, ENV_DEC, ENV_SUS, ENV_REL };

typedef struct {
    float   ph_a, ph_b;
    float   pitch, pitch_tgt;   /* MIDI note units (glide applies) */
    int     note;
    uint8_t gate, sustained, used, kick;   /* kick: seed a self-oscillating filter on the first sample of a note */
    uint32_t age;
    float   vel;
    float   f_env, a_env;
    uint8_t f_stage, a_stage;
    tp5_ladder filt;
    uint32_t rng;
    float   pink[3];
    float   dr_x[3][3];         /* drift one-pole states [channel][band] */
    float   dr[3];              /* drift outputs (unit variance)         */
    uint32_t dr_count;
    float   uni_cents;          /* unison detune offset                  */
    float   gain_l, gain_r;
    float   press, press_sm;    /* polyphonic key pressure on this voice's note, 0..1, and its per-sample smoothed value (acts in REV_4 only) */
} tp5_voice;

typedef struct { float atk, dec, sus, rel; } tp5_env_coefs;

enum { SM_CUTOFF, SM_RESO, SM_ENVAMT, SM_A_WAVE, SM_B_WAVE, SM_B_SEMI, SM_A_PW, SM_B_PW,
       SM_A_VOL, SM_B_VOL, SM_NOISE, SM_PM_FENV, SM_PM_OSCB, SM_LFO_AMT, SM_VOLUME, SM_A_SEMI,
       SM_REV, SM_VINTAGE, SM_VEL_FILT, SM_VEL_AMP, SM_AFTERTOUCH, SM_COUNT };

typedef struct {
    float sr;
    float p[P_COUNT];           /* current (target) parameter values   */
    float sm[SM_COUNT];         /* per-sample smoothed values          */
    tp5_voice voice[TINYPRO5_VOICES];
    tp5_env_coefs fcoef, acoef;
    uint32_t age;
    float   bend;               /* semitones                            */
    float   aftertouch;         /* channel pressure 0..1 (acts in REV_4 only) */
    uint8_t pedal;
    uint8_t note_stack[16]; int stack_n; /* unison note priority (last)  */
    float   lfo_ph, lfo_out;
    float   dc_x[2], dc_y[2];
    /* output limiter: look-ahead delay lines, required-gain history and its smoothed envelope */
    float   lim_dl[2][TP5_LIMITER_LOOKAHEAD], lim_req[TP5_LIMITER_LOOKAHEAD], lim_env;
    float   lim_avg_sum;        /* running sum of the last lim_n envelope values */
    float   lim_avg[TP5_LIMITER_LOOKAHEAD];
    int     lim_pos, lim_n, lim_hot;
    float   lim_rel;            /* release coefficient */
    float   prog_gain, gain_sm; /* the loaded program's output gain (bank table) and its smoothed value */
    int     base_voices;        /* the voice count when the program was loaded: Reload puts it back (the count itself is a performance setting, not part of a program) */
    float   sm_coef;
    float   dr_c[3], dr_g[3];   /* drift band coefficients / normalisers */
    /* user preset overlay (Save) */

    /* diagnostics (read by tests) */
    float    diag_peak_pre;     /* max |sample| at the calibrated level, before makeup + limiter */
    float    diag_peak_post;
    uint32_t diag_limiter_hits;
    uint32_t diag_nan_resets;
    uint32_t diag_blocks;
} tp5_engine;

void  tp5_init(tp5_engine *e, float sample_rate);
void  tp5_set_param(tp5_engine *e, int id, float v);
float tp5_get_param(const tp5_engine *e, int id);
void  tp5_load_preset(tp5_engine *e, int idx);
void  tp5_save_preset(tp5_engine *e, int idx);
/* What Reload / Save / Revert can do right now (the host shows "n/a" on a control with nothing to do):
 *   dirty  = the sound parameters differ from what loading the selected program would give (unsaved tweaks, or another program is selected);
 *   reload = save = dirty;  revert = the selected program has a saved edit. */
int   tp5_program_dirty(const tp5_engine *e);
int   tp5_can_reload(const tp5_engine *e);
int   tp5_can_save(const tp5_engine *e);
int   tp5_can_revert(const tp5_engine *e);
const char *tp5_preset_name(int idx);

/* Preset list orders and banks: see bank.h (tp5_bank_slot / tp5_bank_pos). A slot indexes the bank's table, a position the list. */

void  tp5_note_on(tp5_engine *e, int note, int vel);
void  tp5_note_off(tp5_engine *e, int note);
void  tp5_all_notes_off(tp5_engine *e);
void  tp5_cc(tp5_engine *e, int cc, int val);
void  tp5_pitch_bend(tp5_engine *e, int value14); /* 0..16383, 8192 = centre */
void  tp5_aftertouch(tp5_engine *e, int value);   /* channel pressure 0..127: REV_4 sends it to LFO depth and cutoff, REV_2 ignores it */
void  tp5_poly_pressure(tp5_engine *e, int note, int value);   /* polyphonic key pressure (MIDI 0xA0, what the Move pads send): the same, for the voices playing `note` */
void  tp5_midi(tp5_engine *e, const uint8_t *msg, int len);

int   tp5_active_voices(const tp5_engine *e);

/* Render `frames` stereo float samples (already limited to the -6.1 dBFS ceiling). */
void  tp5_render(tp5_engine *e, float *l, float *r, int frames);
/* Same, interleaved int16 as Schwung expects. frames may exceed 128. */
void  tp5_render_i16(tp5_engine *e, int16_t *out_lr, int frames);

/* Serialise / restore the whole patch (+ saved user slots). Returns length or -1. */
int   tp5_state_save(const tp5_engine *e, char *buf, int buf_len);
int   tp5_state_load(tp5_engine *e, const char *s);

#endif
