/*
 * TinyPro5 DSP engine. See tinypro5.h.
 *
 * Signal flow per voice (Prophet-5 Rev 2 topology):
 *
 *   Osc B (saw/tri/pulse, LFO mode, kbd toggle) --+--> mixer
 *   Osc A (saw/pulse, hard sync to B) ------------+--> mixer --> SSM 2040 4-pole LPF --> VCA
 *   Noise (white <-> pink) -----------------------+
 *
 *   Poly-Mod: [Filter Env, Osc B] -> [Osc A freq, Osc A PW, Filter cutoff]
 *   Global LFO -> [pitch A/B, PWM A/B, cutoff]
 *
 * Realtime rules: no malloc/free, no I/O, no locks, no unbounded loops in tp5_render*.
 */
#include "tinypro5.h"
#include "bank.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PI_F      3.14159265358979323846f
#define TWO_PI_F  6.28318530717958647692f

/* ------------------------------------------------------------------------- */
/* parameter table                                                            */
/* ------------------------------------------------------------------------- */

const tp5_param_info tp5_params[P_COUNT] = {
    [P_CUTOFF]    = { "cutoff",      "Cutoff",  0.f,   1.f,   0.60f, 0 },
    [P_RESO]      = { "resonance",   "Reso",    0.f,   1.f,   0.15f, 0 },
    [P_ENVAMT]    = { "env_amt",     "EnvAmt", -1.f,   1.f,   0.30f, 0 },
    [P_F_ATK]     = { "f_attack",    "FAtk",    0.f,   1.f,   0.05f, 0 },
    [P_F_DEC]     = { "f_decay",     "FDecay",  0.f,   1.f,   0.45f, 0 },
    [P_F_SUS]     = { "f_sustain",   "FSus",    0.f,   1.f,   0.30f, 0 },
    [P_F_REL]     = { "f_release",   "FRel",    0.f,   1.f,   0.30f, 0 },
    [P_A_ATK]     = { "attack",      "AAtk",  0.f,   1.f,   0.04f, 0 },
    [P_A_DEC]     = { "decay",       "ADecay",   0.f,   1.f,   0.50f, 0 },
    [P_A_SUS]     = { "sustain",     "ASus", 0.f,   1.f,   0.80f, 0 },
    [P_A_REL]     = { "release",     "ARel", 0.f,   1.f,   0.30f, 0 },
    [P_KBD]       = { "kbd_track",   "KbdTrk",  0.f,   1.f,   1.00f, 0 },
    [P_DRIFT]     = { "drift",       "Drift",   0.f,   1.f,   0.30f, 0 },
    [P_VOLUME]    = { "master_vol",  "Volume",  0.f,   1.f,   0.80f, 0 },
    [P_A_WAVE]    = { "osca_wave",   "AWave",   0.f,   1.f,   0.00f, 0 },
    [P_B_WAVE]    = { "oscb_wave",   "BWave",   0.f,   1.f,   0.00f, 0 },
    [P_A_SEMI]    = { "osca_pitch",  "APitch", -12.f,  36.f,  0.00f, 0 },
    [P_B_SEMI]    = { "oscb_pitch",  "BPitch", -48.f,  60.f,  0.00f, 0 },
    [P_B_FINE]    = { "oscb_fine",   "BFine",  -50.f,  50.f,  0.00f, 0 },
    [P_SYNC]      = { "osc_sync",    "Sync",    0.f,   1.f,   0.00f, 1 },
    [P_A_PW]      = { "osca_pw",     "APW",     0.f,   1.f,   0.50f, 0 },
    [P_B_PW]      = { "oscb_pw",     "BPW",     0.f,   1.f,   0.50f, 0 },
    [P_A_VOL]     = { "osca_vol",    "AVol",    0.f,   1.f,   0.80f, 0 },
    [P_B_VOL]     = { "oscb_vol",    "BVol",    0.f,   1.f,   0.00f, 0 },
    [P_NOISE]     = { "noise",       "Noise",   0.f,   1.f,   0.00f, 0 },
    [P_NOISE_COL] = { "noise_color", "NColor",  0.f,   1.f,   0.50f, 0 },
    [P_GLIDE]     = { "glide",       "Glide",   0.f,   1.f,   0.00f, 0 },
    [P_B_LFO]     = { "oscb_lfo",    "BLFO",    0.f,   1.f,   0.00f, 1 },
    [P_B_KBD]     = { "oscb_kbd",    "BKbd",    0.f,   1.f,   1.00f, 1 },
    [P_PM_FENV]   = { "pm_filenv",   "PM-Fenv", 0.f,   1.f,   0.00f, 0 },
    [P_PM_OSCB]   = { "pm_oscb",     "PM-OscB", 0.f,   1.f,   0.00f, 0 },
    [P_PM_DEST]   = { "pm_dest",     "PM-Dest", 0.f,   7.f,   1.00f, 1 },
    [P_LFO_RATE]  = { "lfo_rate",    "LFORate", 0.f,   1.f,   0.40f, 0 },
    [P_LFO_WAVE]  = { "lfo_wave",    "LFOWave", 0.f,   2.f,   1.00f, 1 },
    [P_LFO_DEST]  = { "lfo_dest",    "LFODest", 0.f,   7.f,   7.00f, 1 },
    [P_LFO_AMT]   = { "lfo_amount",  "LFOAmt",  0.f,   1.f,   0.00f, 0 },
    [P_MODWHEEL]  = { "mod_wheel",   "ModWhl",  0.f,   1.f,   0.00f, 0 },
    [P_UNISON]    = { "voice_mode",  "Mode",    0.f,   3.f,   0.00f, 1 },
    [P_DETUNE]    = { "detune",      "Detune",  0.f,   1.f,   0.30f, 0 },
    [P_VOICES]    = { "voice_count", "Voices",  1.f,   5.f,   5.00f, 1 },
    [P_BANK]      = { "bank",        "Bank",    0.f,   (float)(TP5_MAX_BANKS - 1), 0.f, 1 },
    [P_SORT]      = { "preset_sort", "Order",   0.f,   1.f,   0.00f, 1 },
    [P_PRESET]    = { "preset",      "Preset",  0.f,   (float)(TINYPRO5_PRESET_MAX - 1), 0.f, 1 },
    [P_LOAD]      = { "load_preset", "Reload",    0.f,   1.f,   0.00f, 1 },
    [P_SAVE]      = { "save_preset", "Save",    0.f,   1.f,   0.00f, 1 },
    [P_REVERT]    = { "revert_preset", "Revert",  0.f,   1.f,   0.00f, 1 },
};

int tp5_param_id_from_key(const char *key)
{
    int i;
    if (!key) return -1;
    for (i = 0; i < P_COUNT; i++)
        if (strcmp(tp5_params[i].key, key) == 0) return i;
    if (strcmp(key, "unison") == 0) return P_UNISON;   /* the old on/off key: 1 still means Unison */
    return -1;
}

int tp5_is_sound_param(int id)
{
    return id >= 0 && id < P_VOICES && id != P_VOLUME && id != P_MODWHEEL;
}

const char *tp5_preset_name(int idx)           /* a slot of the factory bank (bank 0) */
{
    return tp5_bank_preset_name(0, idx);
}

/* ------------------------------------------------------------------------- */
/* small helpers                                                              */
/* ------------------------------------------------------------------------- */

static inline float clampf(float x, float lo, float hi) { return x < lo ? lo : (x > hi ? hi : x); }

/* Rational tanh approximation, exact +-1 at |x| >= 3, monotonic, C0. */
static inline float fast_tanh(float x)
{
    float x2;
    if (x >= 3.f) return 1.f;
    if (x <= -3.f) return -1.f;
    x2 = x * x;
    return x * (27.f + x2) / (27.f + 9.f * x2);
}

/* ---- response laws ---------------
 * Panel knobs are 0..1 like the instrument's 0-10 dials.
 * Cutoff: the -3 dB point at knob c is a pure exponential, 1.2 octaves below this engine's old 20 Hz * 2^(10 c).
 * Keyboard tracking: 0 / 0.25 / 0.5 / 0.75 / 1 give 0 / 0.20 / 0.61 / 1.01 / 1.38 octaves of cutoff per octave of pitch
 * about a pivot at MIDI 36. Resonance: the feedback gain k (self-oscillation at k = 4.0) reaches 4 at knob 0.6, not at 1.0.
 */
#define TP5_CUTOFF_OFFSET_OCT 1.2f
#define TP5_KBD_PIVOT 36.f

static float kbd_oct_per_oct(float p)
{
    static const float pts[5] = { 0.f, 0.20f, 0.61f, 1.01f, 1.38f };
    const float x = clampf(p, 0.f, 1.f) * 4.f;
    const int i = x >= 4.f ? 3 : (int)x;
    return pts[i] + (pts[i + 1] - pts[i]) * (x - (float)i);
}

static float reso_to_k(float r)
{
    static const float k[11] = { 0.f, 0.33f, 0.86f, 1.52f, 2.27f, 3.100f, 4.200f, 5.000f, 6.000f, 7.000f, 8.000f };
    const float x = clampf(r, 0.f, 1.f) * 10.f;
    const int i = x >= 10.f ? 9 : (int)x;
    return k[i] + (k[i + 1] - k[i]) * (x - (float)i);
}

static inline float rnd_bipolar(uint32_t *s)
{
    uint32_t x = *s;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    *s = x;
    return (float)(int32_t)x * (1.0f / 2147483648.0f);
}

static inline float blep2(float t, float dt)
{
    if (t < dt) { t /= dt; return t + t - t * t - 1.f; }
    if (t > 1.f - dt) { t = (t - 1.f) / dt; return t * t + t + t + 1.f; }
    return 0.f;
}

static inline float wave_saw(float ph, float inc) { return 2.f * ph - 1.f - blep2(ph, inc); }
static inline float wave_tri(float ph) { return 4.f * fabsf(ph - 0.5f) - 1.f; }
static inline float wave_pulse(float ph, float inc, float duty)
{
    float t2 = ph - duty;
    float v = ph < duty ? 1.f : -1.f;
    if (t2 < 0.f) t2 += 1.f;
    return v + blep2(ph, inc) - blep2(t2, inc);
}

/* 2^x via a degree-7 polynomial on the fraction plus exponent-bit assembly.
 * Relative error < 2e-6 (0.004 cent) -- far below audible pitch error. */
static inline float fast_exp2(float x)
{
    float f, p, scale;
    int32_t i;
    uint32_t bits;
    x = clampf(x, -100.f, 100.f);
    i = (int32_t)x;
    if ((float)i > x) i--;               /* floor */
    f = x - (float)i;
    p = 1.f + f * (0.69314718f + f * (0.24022651f + f * (0.05550411f + f * (0.00961813f
        + f * (0.00133336f + f * (0.00015404f + f * 0.0000152527f))))));
    bits = (uint32_t)(i + 127) << 23;
    memcpy(&scale, &bits, sizeof scale);
    return p * scale;
}

/* tan(a) for a in (0, 1.26] (fc <= 0.4*fs) via a [7/6] Pade; error < 0.3 %. */
static inline float fast_tan(float a)
{
    const float a2 = a * a;
    return a * (135135.f + a2 * (-17325.f + a2 * (378.f - a2)))
             / (135135.f + a2 * (-62370.f + a2 * (3150.f - 28.f * a2)));
}

static inline float midi_to_hz(float semis) { return 440.f * fast_exp2((semis - 69.f) * (1.f / 12.f)); }

/* Envelope laws; knob 0..1 = the panel dial.
 *   attack : a LINEAR ramp (10 % -> 90 % takes 0.11 of the 10-90 % time ... amplitude is linear in time); full-scale ramp
 *            time 0.133 s * 10^(4.69 (k - 0.5))
 *   decay  : exponential toward a target 0.02 below the sustain level, time constant 0.0715 s * 10^(4.62 (k - 0.5))
 *   release: exponential toward a target 0.03 below zero,              time constant 0.094 s * 10^(4.63 (k - 0.5))
 * The undershoot targets make the tail speed up near the end (-20 dB -> -40 dB takes 1.6x as long, not 2x). */
/* Poly-Mod depths at dial 1.0 for the filter-envelope source: linear in the dial, +33.85
 * semitones on Osc A, +14.7 octaves of cutoff, 3.2 PW-knob units. The Osc B source: 0.3 * dial^1.25 of that scale. */
#define TP5_PM_PITCH_ST 33.85f
#define TP5_PM_FILTER_OCT 14.7f
#define TP5_PM_PW 3.2f
#define TP5_PMB_RATIO 0.3f
/* What Osc B feeds into Poly-Mod is the SUM of its enabled waveforms, not the unit-amplitude blend the mixer path uses. Saw and triangle weigh 1 each, the pulse 2.
 * The B-wave dial is a morph; these are the weights at its detents (saw+tri .25, tri .5, all three .6, tri+pulse .75,
 * saw+pulse .85, pulse 1) and they are interpolated between them. Order: saw, triangle, pulse. */
static const float k_pmb_knot[7]    = { 0.f, 0.25f, 0.5f, 0.6f, 0.75f, 0.85f, 1.f };
static const float k_pmb_weight[7][3] = { { 1, 0, 0 }, { 1, 1, 0 }, { 0, 1, 0 }, { 1, 1, 2 }, { 0, 1, 2 }, { 1, 0, 2 }, { 0, 0, 2 } };
static inline void pmb_weights(float w, float *ws, float *wt, float *wp)
{
    int i = 0;
    float f;
    while (i < 5 && w > k_pmb_knot[i + 1]) i++;
    f = clampf((w - k_pmb_knot[i]) / (k_pmb_knot[i + 1] - k_pmb_knot[i]), 0.f, 1.f);
    *ws = k_pmb_weight[i][0] + f * (k_pmb_weight[i + 1][0] - k_pmb_weight[i][0]);
    *wt = k_pmb_weight[i][1] + f * (k_pmb_weight[i + 1][1] - k_pmb_weight[i][1]);
    *wp = k_pmb_weight[i][2] + f * (k_pmb_weight[i + 1][2] - k_pmb_weight[i][2]);
}
/* The filter envelope decays/releases 1.45x slower than the amp-envelope law for the same knob. */
/* Saturation knee of the ladder in signal units. 1 up to k = 3.2 (ordinary resonance sounds as before), rising to 3 by k = 4.2:
 * a self-oscillating filter then swings further before it clips (about 12-16 dB above a typical patch). */
#define TP5_LADDER_HEADROOM_MAX 1.45f
#define TP5_SELFOSC_K 3.6f          /* feedback gain above which the filter is seeded (self-oscillation starts at k = 4) */
#define TP5_SELFOSC_NOISE 3e-4f     /* about -70 dBFS of white noise at the filter input */
#define TP5_SELFOSC_KICK 0.04f      /* one-sample click at note-on */
#define TP5_FENV_TSCALE 1.45f
#define TP5_DECAY_UNDERSHOOT 0.02f
#define TP5_RELEASE_UNDERSHOOT 0.03f
static inline float atk_ramp_time(float v) { return 0.133f * powf(10.f, 4.69f * (v - 0.5f)); }
static inline float dec_tau(float v) { return 0.0715f * powf(10.f, 4.62f * (v - 0.5f)); }
static inline float rel_tau(float v) { return 0.094f * powf(10.f, 4.63f * (v - 0.5f)); }

/* ------------------------------------------------------------------------- */
/* SSM 2040 style ladder                                                      */
/* ------------------------------------------------------------------------- */

void tp5_ladder_reset(tp5_ladder *f) { memset(f, 0, sizeof *f); }

/*
 * Four zero-delay-feedback (TPT) one-pole low-pass stages with the global
 * feedback path solved instantaneously (Zavalishin). Soft saturation is applied
 * at the loop input and between stages, which bounds every state and gives the
 * rounded, "creamy" self-oscillation of the early SSM chips rather than a hard
 * clip. Everything downstream of the input saturator is a cascade of stable
 * linear stages driven by a bounded signal, so the filter cannot blow up.
 */
float tp5_ladder_tick(tp5_ladder *f, float x, float g, float k)
{
    const float G = g / (1.f + g);
    const float G2 = G * G, G4 = G2 * G2, oneG = 1.f - G;
    const float S = oneG * (G2 * G * f->s[0] + G2 * f->s[1] + G * f->s[2] + f->s[3]);
    /* No input compensation: like the CEM3320 voicing, the passband loses 1/(1+k) as resonance rises. */
    const float hr = 1.f + (TP5_LADDER_HEADROOM_MAX - 1.f) * clampf(k - 3.2f, 0.f, 1.f), ihr = 1.f / hr;
    float y = hr * fast_tanh((x - k * S) / (1.f + k * G4) * ihr);
    int i;
    for (i = 0; i < 4; i++) {
        const float v = G * (y - f->s[i]);
        const float out = v + f->s[i];
        f->s[i] = out + v;
        if (fabsf(f->s[i]) < 1e-20f) f->s[i] = 0.f; /* flush denormals */
        y = (i < 3) ? hr * fast_tanh(out * ihr) : out;
    }
    return y;
}

/* ------------------------------------------------------------------------- */
/* envelopes                                                                  */
/* ------------------------------------------------------------------------- */

static inline float env_tick(float *env, uint8_t *stage, const tp5_env_coefs *c)
{
    float x = *env;
    switch (*stage) {
    case ENV_ATK: /* linear ramp */
        x += c->atk;
        if (x >= 1.f) { x = 1.f; *stage = ENV_DEC; }
        break;
    case ENV_DEC: /* exponential toward a target just below the sustain level */
        x += (c->sus - TP5_DECAY_UNDERSHOOT - x) * c->dec;
        if (x <= c->sus) { x = c->sus; *stage = ENV_SUS; }
        break;
    case ENV_SUS:
        x = c->sus;
        break;
    case ENV_REL:
        x += (-TP5_RELEASE_UNDERSHOOT - x) * c->rel;
        if (x <= 1e-4f) { x = 0.f; *stage = ENV_IDLE; }
        break;
    default:
        x = 0.f;
        break;
    }
    *env = x;
    return x;
}

static void env_coefs(tp5_env_coefs *c, float sr, float a, float d, float s, float r, float tscale, int amp)
{
    c->atk = 1.f / (atk_ramp_time(a) * sr);
    c->dec = 1.f - expf(-1.f / (dec_tau(d) * tscale * sr));
    c->rel = 1.f - expf(-1.f / (rel_tau(r) * tscale * sr));
    /* amp sustain: dead zone below knob 0.061, then linear; the filter envelope sustain is s^1.1 */
    c->sus = amp ? clampf((s - 0.061f) * (1.f / 0.939f), 0.f, 1.f) : powf(s, 1.1f);
}

/* ------------------------------------------------------------------------- */
/* init / params                                                              */
/* ------------------------------------------------------------------------- */

static const float k_voice_pan[TINYPRO5_VOICES]     = { -0.30f, 0.30f, -0.15f, 0.15f, 0.00f };
/* static "component tolerance" offsets in cents, scaled by Drift */
static const float k_voice_tol[TINYPRO5_VOICES]     = { 0.0f, -1.7f, 2.1f, -0.9f, 1.3f };
#define DRIFT_UPDATE_MASK 15u /* drift is re-sampled every 16 samples */

static void smooth_targets(const float *p, float *t)
{
    t[SM_CUTOFF] = p[P_CUTOFF];
    t[SM_RESO]   = p[P_RESO];
    t[SM_ENVAMT] = p[P_ENVAMT];
    t[SM_A_WAVE] = p[P_A_WAVE];
    t[SM_B_WAVE] = p[P_B_WAVE];
    t[SM_B_SEMI] = p[P_B_SEMI] + p[P_B_FINE] * 0.01f;
    t[SM_A_PW]   = p[P_A_PW];
    t[SM_B_PW]   = p[P_B_PW];
    t[SM_A_VOL]  = p[P_A_VOL];
    t[SM_B_VOL]  = p[P_B_VOL];
    t[SM_NOISE]  = p[P_NOISE];
    t[SM_PM_FENV] = p[P_PM_FENV];
    t[SM_PM_OSCB] = TP5_PMB_RATIO * p[P_PM_OSCB] * sqrtf(sqrtf(p[P_PM_OSCB]));   /* ~dial^1.25 */
    t[SM_LFO_AMT] = clampf(p[P_LFO_AMT] + p[P_MODWHEEL], 0.f, 1.f);
    t[SM_VOLUME] = p[P_VOLUME];
    t[SM_A_SEMI] = p[P_A_SEMI];
}

void tp5_init(tp5_engine *e, float sr)
{
    static const float band_hz[3] = { 0.05f, 0.35f, 1.8f };
    float tgt[SM_COUNT];
    int i, b;

    memset(e, 0, sizeof *e);
    e->sr = sr;
    for (i = 0; i < P_COUNT; i++) e->p[i] = tp5_params[i].def;
    e->sm_coef = 1.f - expf(-1.f / (0.006f * sr));
    e->lim_n = (int)(0.0015f * sr + 0.5f);                       /* 1.5 ms look-ahead */
    if (e->lim_n < 8) e->lim_n = 8;
    if (e->lim_n > TP5_LIMITER_LOOKAHEAD) e->lim_n = TP5_LIMITER_LOOKAHEAD;
    e->lim_rel = 1.f - expf(-1.f / (0.080f * sr));               /* 80 ms release */
    e->lim_env = 1.f;
    for (i = 0; i < TP5_LIMITER_LOOKAHEAD; i++) { e->lim_req[i] = 1.f; e->lim_avg[i] = 1.f; }
    e->lim_avg_sum = (float)e->lim_n;

    for (b = 0; b < 3; b++) {
        const float dt = (float)(DRIFT_UPDATE_MASK + 1u) / sr;
        const float c = 1.f - expf(-TWO_PI_F * band_hz[b] * dt);
        e->dr_c[b] = c;
        /* one-pole filtered uniform noise has std = 0.577*sqrt(c/(2-c)); normalise to 1 */
        e->dr_g[b] = 1.f / (0.57735f * sqrtf(c / (2.f - c)));
    }
    for (i = 0; i < TINYPRO5_VOICES; i++) {
        tp5_voice *v = &e->voice[i];
        v->rng = 0x9E3779B9u * (uint32_t)(i + 1) + 0x7F4A7C15u;
        v->ph_a = 0.5f * (rnd_bipolar(&v->rng) + 1.f);
        v->ph_b = 0.5f * (rnd_bipolar(&v->rng) + 1.f);
        v->gain_l = 1.f - 0.5f * k_voice_pan[i];
        v->gain_r = 1.f + 0.5f * k_voice_pan[i];
        v->dr_count = (uint32_t)i * 3u; /* stagger drift updates across voices */
        tp5_ladder_reset(&v->filt);
    }
    smooth_targets(e->p, tgt);
    memcpy(e->sm, tgt, sizeof tgt);
    tp5_load_preset(e, 0);
    e->gain_sm = e->prog_gain;
    env_coefs(&e->fcoef, sr, e->p[P_F_ATK], e->p[P_F_DEC], e->p[P_F_SUS], e->p[P_F_REL], TP5_FENV_TSCALE, 0);
    env_coefs(&e->acoef, sr, e->p[P_A_ATK], e->p[P_A_DEC], e->p[P_A_SUS], e->p[P_A_REL], 1.f, 1);
}

/* The sound parameters that loading program `idx` of `bank` produces: the bank's own data, then the user's saved edit on top. Only the
 * sound parameters of `out` (P_COUNT floats) are written. */
static void program_sound(int bank, int idx, float *out)
{
    int i;
    const tp5_kv *kvs = NULL;
    int nkv;
    float ov[P_COUNT];
    for (i = 0; i < P_COUNT; i++)
        if (tp5_is_sound_param(i)) out[i] = tp5_params[i].def;
    nkv = tp5_bank_preset_kv(bank, idx, &kvs);
    for (i = 0; i < nkv; i++) {
        const tp5_kv *kv = &kvs[i];
        if (kv->id < P_COUNT && tp5_is_sound_param(kv->id))
            out[kv->id] = clampf(kv->v, tp5_params[kv->id].min, tp5_params[kv->id].max);
    }
    if (tp5_overlay_get(bank, idx, ov))
        for (i = 0; i < P_COUNT; i++)
            if (tp5_is_sound_param(i)) out[i] = clampf(ov[i], tp5_params[i].min, tp5_params[i].max);
}

static int selected_bank(const tp5_engine *e)
{
    const int bank = (int)e->p[P_BANK];
    return (bank < 0 || bank >= tp5_bank_count()) ? 0 : bank;
}

static int selected_slot(const tp5_engine *e)
{
    return tp5_bank_slot(selected_bank(e), (int)e->p[P_PRESET], (int)e->p[P_SORT]);
}

static int sound_dirty(const tp5_engine *e)
{
    float ref[P_COUNT];
    int i;
    program_sound(selected_bank(e), selected_slot(e), ref);
    for (i = 0; i < P_COUNT; i++)
        if (tp5_is_sound_param(i) && fabsf(e->p[i] - ref[i]) > 2e-3f) return 1;   /* a state blob keeps five significant digits */
    return 0;
}

int tp5_program_dirty(const tp5_engine *e)
{
    return sound_dirty(e) || (int)e->p[P_VOICES] != e->base_voices;
}

int tp5_can_reload(const tp5_engine *e) { return tp5_program_dirty(e); }
int tp5_can_save(const tp5_engine *e)   { return tp5_program_dirty(e); }

int tp5_can_revert(const tp5_engine *e)
{
    float ov[P_COUNT];
    return tp5_overlay_get(selected_bank(e), selected_slot(e), ov);
}

void tp5_load_preset(tp5_engine *e, int idx)
{
    int i, bank = (int)e->p[P_BANK];
    float prog[P_COUNT];
    if (bank < 0 || bank >= tp5_bank_count()) bank = 0;
    if (idx < 0) idx = 0;
    if (idx >= tp5_bank_size(bank)) idx = tp5_bank_size(bank) - 1;
    if (idx < 0) idx = 0;
    program_sound(bank, idx, prog);
    for (i = 0; i < P_COUNT; i++)
        if (tp5_is_sound_param(i)) e->p[i] = prog[i];
    e->prog_gain = tp5_bank_gain(bank, idx);
    e->base_voices = (int)e->p[P_VOICES];
    e->p[P_BANK] = (float)bank;
    e->p[P_PRESET] = (float)tp5_bank_pos(bank, idx, (int)e->p[P_SORT]);   /* the list position of this slot in the current order */
}

void tp5_save_preset(tp5_engine *e, int idx)
{
    const int bank = (int)e->p[P_BANK];
    if (idx < 0 || idx >= tp5_bank_size(bank)) return;
    if (sound_dirty(e)) tp5_overlay_put(bank, idx, e->p);   /* any bank: the edit lives in the overlay, never in the bank file */
    e->base_voices = (int)e->p[P_VOICES];           /* the voice count is not stored, but Save accepts it as the new "as loaded" */
}

static void voice_release(tp5_voice *v);

void tp5_set_param(tp5_engine *e, int id, float v)
{
    const tp5_param_info *pi;
    if (id < 0 || id >= P_COUNT || !(v == v)) return; /* reject unknown ids and NaN */
    pi = &tp5_params[id];
    v = clampf(v, pi->min, pi->max);
    if (pi->is_int) v = floorf(v + 0.5f);

    switch (id) {
    case P_LOAD:
        e->p[P_LOAD] = 0.f;
        if (v > 0.5f) tp5_set_param(e, P_VOICES, (float)e->base_voices);
        if (v > 0.5f) tp5_load_preset(e, tp5_bank_slot((int)e->p[P_BANK], (int)e->p[P_PRESET], (int)e->p[P_SORT]));
        return;
    case P_REVERT:
        e->p[P_REVERT] = 0.f;
        if (v > 0.5f) {
            const int slot = tp5_bank_slot((int)e->p[P_BANK], (int)e->p[P_PRESET], (int)e->p[P_SORT]);
            tp5_overlay_remove((int)e->p[P_BANK], slot);
            tp5_load_preset(e, slot);
        }
        return;
    case P_SAVE:
        e->p[P_SAVE] = 0.f;
        if (v > 0.5f) tp5_save_preset(e, tp5_bank_slot((int)e->p[P_BANK], (int)e->p[P_PRESET], (int)e->p[P_SORT]));
        return;
    case P_BANK: {                          /* a bank switch loads the first program of the new bank */
        const int nb = tp5_bank_count();
        const int target = v >= (float)nb ? nb - 1 : (int)v;
        if (target == (int)e->p[P_BANK]) return;
        e->p[P_BANK] = (float)target;
        tp5_load_preset(e, tp5_bank_slot(target, 0, (int)e->p[P_SORT]));
        return;
    }
    case P_PRESET: {                        /* positions beyond the active bank's list clamp to its last entry */
        const int size = tp5_bank_size((int)e->p[P_BANK]);
        e->p[P_PRESET] = v >= (float)size ? (float)(size > 0 ? size - 1 : 0) : v;
        return;
    }
    case P_SORT: {                          /* switching the order keeps the selected program selected */
        const int bank = (int)e->p[P_BANK];
        const int slot = tp5_bank_slot(bank, (int)e->p[P_PRESET], (int)e->p[P_SORT]);
        e->p[P_SORT] = v;
        e->p[P_PRESET] = (float)tp5_bank_pos(bank, slot, (int)v);
        return;
    }
    case P_UNISON:
        if (v != e->p[P_UNISON]) { e->p[id] = v; tp5_all_notes_off(e); }
        return;
    case P_VOICES: {
        int i;
        e->p[id] = v;
        for (i = (int)v; i < TINYPRO5_VOICES; i++)           /* voices that are no longer in play fade out */
            if (e->voice[i].a_stage != ENV_IDLE && (e->voice[i].gate || e->voice[i].sustained)) voice_release(&e->voice[i]);
        return;
    }
    default:
        e->p[id] = v;
        return;
    }
}

float tp5_get_param(const tp5_engine *e, int id)
{
    return (id >= 0 && id < P_COUNT) ? e->p[id] : 0.f;
}

/* ------------------------------------------------------------------------- */
/* voice allocation                                                           */
/* ------------------------------------------------------------------------- */

int tp5_active_voices(const tp5_engine *e)
{
    int i, n = 0;
    for (i = 0; i < TINYPRO5_VOICES; i++)
        if (e->voice[i].a_stage != ENV_IDLE) n++;
    return n;
}

/*
 * Steal order: idle voice -> quietest releasing voice -> oldest gated voice
 * (ties broken toward the lowest note). Deterministic and bounded (5 voices).
 */
static int voice_count(const tp5_engine *e)
{
    const int n = (int)e->p[P_VOICES];
    return n < 1 ? 1 : (n > TINYPRO5_VOICES ? TINYPRO5_VOICES : n);
}

/* Voices in play: the voice count in Poly and Unison, a single voice in Mono and Legato. */
static int voices_in_play(const tp5_engine *e)
{
    return (int)e->p[P_UNISON] >= 2 ? 1 : voice_count(e);
}

static int voice_alloc(const tp5_engine *e)
{
    const int nv = voices_in_play(e);
    int i, best = -1;
    for (i = 0; i < nv; i++)
        if (e->voice[i].a_stage == ENV_IDLE) return i;
    for (i = 0; i < nv; i++) {
        const tp5_voice *v = &e->voice[i];
        if (!v->gate && !v->sustained &&
            (best < 0 || v->a_env < e->voice[best].a_env)) best = i;
    }
    if (best >= 0) return best;
    for (i = 0; i < nv; i++) {
        const tp5_voice *v = &e->voice[i];
        if (best < 0 || v->age < e->voice[best].age ||
            (v->age == e->voice[best].age && v->note < e->voice[best].note)) best = i;
    }
    return best;
}

static float glide_coef_from(float sr, float g)
{
    return (g < 0.01f) ? 1.f : 1.f - expf(-1.f / (0.01f * powf(200.f, g) * sr));
}

static void voice_trigger(tp5_engine *e, tp5_voice *v, int note, int vel, float uni_cents)
{
    const float gc = glide_coef_from(e->sr, e->p[P_GLIDE]);
    v->note = note;
    v->pitch_tgt = (float)note;
    if (gc >= 1.f || !v->used) v->pitch = (float)note;
    v->used = 1;
    v->gate = 1;
    v->sustained = 0;
    v->age = ++e->age;
    v->vel = (float)vel * (1.f / 127.f);
    v->uni_cents = uni_cents;
    v->a_stage = ENV_ATK;
    v->f_stage = ENV_ATK;
    v->kick = 1;
}

/* New pitch on a sounding voice without touching its envelopes (glide applies as usual) */
static void voice_legato(tp5_engine *e, tp5_voice *v, int note)
{
    const float gc = glide_coef_from(e->sr, e->p[P_GLIDE]);
    v->note = note;
    v->pitch_tgt = (float)note;
    if (gc >= 1.f) v->pitch = (float)note;
    v->age = ++e->age;
}

static void voice_release(tp5_voice *v)
{
    v->gate = 0;
    v->sustained = 0;
    if (v->a_stage != ENV_IDLE) v->a_stage = ENV_REL;
    if (v->f_stage != ENV_IDLE) v->f_stage = ENV_REL;
}

/* Unison spread: the stack is centred, +-30 cents at full Detune for five voices (15 cents per step). */
static float unison_cents(const tp5_engine *e, int i, int n)
{
    return ((float)i - 0.5f * (float)(n - 1)) * 15.f * e->p[P_DETUNE];
}

void tp5_note_on(tp5_engine *e, int note, int vel)
{
    int i;
    if (note < 0 || note > 127) return;
    if (vel <= 0) { tp5_note_off(e, note); return; }
    if (vel > 127) vel = 127;

    if ((int)e->p[P_UNISON] >= 1) {
        /* Unison / Mono / Legato: last-note priority over a stack of held notes */
        const int mode = (int)e->p[P_UNISON];
        const int was_held = e->stack_n > 0;
        int j, n = 0;
        uint8_t tmp[16];
        for (j = 0; j < e->stack_n; j++)
            if (e->note_stack[j] != note) tmp[n++] = e->note_stack[j];
        if (n >= 16) { memmove(tmp, tmp + 1, 15); n = 15; }
        tmp[n++] = (uint8_t)note;
        memcpy(e->note_stack, tmp, (size_t)n);
        e->stack_n = n;
        if (mode == 1) {
            const int nv = voice_count(e);
            for (i = 0; i < nv; i++)
                voice_trigger(e, &e->voice[i], note, vel, unison_cents(e, i, nv));
        } else if (mode == 3 && was_held && e->voice[0].gate) {
            voice_legato(e, &e->voice[0], note);          /* Legato: overlapping notes only change pitch */
        } else {
            voice_trigger(e, &e->voice[0], note, vel, 0.f);   /* Mono (and the first Legato note): retrigger */
        }
        return;
    }

    for (i = 0; i < voice_count(e); i++) { /* retrigger on the same voice */
        tp5_voice *v = &e->voice[i];
        if (v->a_stage != ENV_IDLE && (v->gate || v->sustained) && v->note == note) {
            voice_trigger(e, v, note, vel, 0.f);
            return;
        }
    }
    voice_trigger(e, &e->voice[voice_alloc(e)], note, vel, 0.f);
}

void tp5_note_off(tp5_engine *e, int note)
{
    int i;
    if ((int)e->p[P_UNISON] >= 1) {
        const int mode = (int)e->p[P_UNISON];
        int j, n = 0;
        uint8_t tmp[16];
        for (j = 0; j < e->stack_n; j++)
            if (e->note_stack[j] != note) tmp[n++] = e->note_stack[j];
        if (n == e->stack_n) return; /* not held */
        memcpy(e->note_stack, tmp, (size_t)n);
        e->stack_n = n;
        if (n > 0) { /* back to the previous held note: Mono retriggers it, Unison and Legato glide to it */
            if (mode == 2) {
                voice_trigger(e, &e->voice[0], e->note_stack[n - 1], (int)(e->voice[0].vel * 127.f + 0.5f), 0.f);
            } else {
                for (i = 0; i < TINYPRO5_VOICES; i++) {
                    e->voice[i].note = e->note_stack[n - 1];
                    e->voice[i].pitch_tgt = (float)e->note_stack[n - 1];
                }
            }
        } else {
            for (i = 0; i < TINYPRO5_VOICES; i++) {
                if (e->pedal) e->voice[i].sustained = 1, e->voice[i].gate = 0;
                else voice_release(&e->voice[i]);
            }
        }
        return;
    }
    for (i = 0; i < TINYPRO5_VOICES; i++) {
        tp5_voice *v = &e->voice[i];
        if (v->gate && v->note == note) {
            if (e->pedal) { v->sustained = 1; v->gate = 0; }
            else voice_release(v);
        }
    }
}

void tp5_all_notes_off(tp5_engine *e)
{
    int i;
    e->stack_n = 0;
    for (i = 0; i < TINYPRO5_VOICES; i++) voice_release(&e->voice[i]);
}

void tp5_pitch_bend(tp5_engine *e, int value14)
{
    e->bend = ((float)(value14 - 8192) / 8192.f) * 2.f;
}

void tp5_cc(tp5_engine *e, int cc, int val)
{
    int i;
    switch (cc) {
    case 1:  e->p[P_MODWHEEL] = (float)val * (1.f / 127.f); break;
    case 64:
        if (val >= 64) e->pedal = 1;
        else {
            e->pedal = 0;
            for (i = 0; i < TINYPRO5_VOICES; i++)
                if (e->voice[i].sustained) voice_release(&e->voice[i]);
        }
        break;
    case 120: case 123: tp5_all_notes_off(e); break;
    default: break;
    }
}

void tp5_midi(tp5_engine *e, const uint8_t *msg, int len)
{
    if (len < 1) return;
    switch (msg[0] & 0xF0) {
    case 0x80: if (len >= 3) tp5_note_off(e, msg[1] & 0x7F); break;
    case 0x90: if (len >= 3) tp5_note_on(e, msg[1] & 0x7F, msg[2] & 0x7F); break;
    case 0xB0: if (len >= 3) tp5_cc(e, msg[1] & 0x7F, msg[2] & 0x7F); break;
    case 0xE0: if (len >= 3) tp5_pitch_bend(e, (msg[1] & 0x7F) | ((msg[2] & 0x7F) << 7)); break;
    default: break;
    }
}

/* ------------------------------------------------------------------------- */
/* voice rendering                                                            */
/* ------------------------------------------------------------------------- */

typedef struct {
    float lfo;          /* smoothed LFO sample, -1..1                    */
    float lfo_pitch;    /* semitones                                     */
    float lfo_pwm;      /* pulse-width units                             */
    float lfo_filt;     /* octaves                                       */
    float noise_color;
    float uni_gain;
    int   pm_dest;
    int   b_lfo, b_kbd, sync;
    float kbd, drift;
    float glide_coef;
    int   lfo_dest;     /* bit mask: 1 pitch, 2 pwm, 4 filter            */
} blockctx;

static void drift_update(tp5_engine *e, tp5_voice *v)
{
    int ch, b;
    for (ch = 0; ch < 3; ch++) {
        float sum = 0.f;
        for (b = 0; b < 3; b++) {
            v->dr_x[ch][b] += e->dr_c[b] * (rnd_bipolar(&v->rng) - v->dr_x[ch][b]);
            sum += v->dr_x[ch][b] * e->dr_g[b];
        }
        v->dr[ch] = clampf(sum * 0.57735f, -3.f, 3.f); /* /sqrt(3) -> unit variance */
    }
}

static inline float pb_after(float d) { return d + d - d * d - 1.f; }

static inline void voice_tick(tp5_engine *e, tp5_voice *v, int vi, const blockctx *c, const float *sm,
                              const tp5_env_coefs *acoef, const tp5_env_coefs *fcoef,
                              float *out_l, float *out_r)
{
    const float sr = e->sr;
    float amp, fenv, drA, drB, drF, semisA, semisB, incA, incB, bout, aout;
    float pm_src, pm_pitch, pwA, pwB, wB, noise, mix, oct, fc, g, k, y;
    int wrapB = 0;
    float dfrac = 0.f;

    amp = env_tick(&v->a_env, &v->a_stage, acoef);
    if (v->a_stage == ENV_IDLE) {
        v->f_env = 0.f; v->f_stage = ENV_IDLE;
        tp5_ladder_reset(&v->filt);
        return;
    }
    fenv = env_tick(&v->f_env, &v->f_stage, fcoef);

    v->pitch += (v->pitch_tgt - v->pitch) * c->glide_coef;

    if ((v->dr_count++ & DRIFT_UPDATE_MASK) == 0u) drift_update(e, v);
    /* drift: random walk (~1/f) plus static per-voice component tolerance */
    drA = c->drift * (6.f * v->dr[0] + k_voice_tol[vi]) * 0.01f;
    drB = c->drift * (6.f * v->dr[1] - k_voice_tol[vi]) * 0.01f;
    drF = c->drift * 0.12f * v->dr[2];

    /* ---- Osc B ---- */
    if (c->b_lfo) {
        const float fb = 4.f * fast_exp2(sm[SM_B_SEMI] * (1.f / 6.f)); /* 1 .. 16 Hz */
        incB = fb / sr;
    } else {
        semisB = (c->b_kbd ? v->pitch + e->bend : 60.f) + sm[SM_B_SEMI]
               + v->uni_cents * 0.01f + drB + c->lfo_pitch;
        incB = midi_to_hz(semisB) / sr;
    }
    incB = clampf(incB, 1e-6f, 0.45f);
    v->ph_b += incB;
    if (v->ph_b >= 1.f) { v->ph_b -= 1.f; wrapB = 1; dfrac = v->ph_b / incB; }

    pwB = 0.05f + 0.9f * clampf(sm[SM_B_PW] + c->lfo_pwm, 0.f, 1.f);
    wB = sm[SM_B_WAVE];
    if (wB < 0.5f) {
        const float wt = 2.f * wB;
        bout = (1.f - wt) * wave_saw(v->ph_b, incB) + wt * wave_tri(v->ph_b);
    } else {
        const float wp = 2.f * wB - 1.f;
        bout = (1.f - wp) * wave_tri(v->ph_b) + wp * wave_pulse(v->ph_b, incB, pwB);
    }

    /* ---- Poly-Mod ---- */
    {
        const float aF = sm[SM_PM_FENV];                      /* linear in the dial */
        const float aB = sm[SM_PM_OSCB];                       /* law applied to the target in tp5 smoothing setup */
        float bpm = 0.f;
        if (aB > 0.f) {
            float ws, wt, wp;
            pmb_weights(wB, &ws, &wt, &wp);
            if (ws > 0.f) bpm += ws * wave_saw(v->ph_b, incB);
            if (wt > 0.f) bpm += wt * wave_tri(v->ph_b);
            if (wp > 0.f) bpm += wp * wave_pulse(v->ph_b, incB, pwB);
        }
        pm_src = aF * fenv + aB * bpm;
    }
    pm_pitch = (c->pm_dest & 1) ? pm_src * TP5_PM_PITCH_ST : 0.f;

    /* ---- Osc A ---- */
    semisA = v->pitch + sm[SM_A_SEMI] + e->bend + v->uni_cents * 0.01f + drA + c->lfo_pitch + pm_pitch;
    incA = clampf(midi_to_hz(semisA) / sr, 1e-6f, 0.45f);
    v->ph_a += incA;
    if (v->ph_a >= 1.f) v->ph_a -= 1.f;
    pwA = 0.05f + 0.9f * clampf(sm[SM_A_PW] + c->lfo_pwm + ((c->pm_dest & 2) ? pm_src * TP5_PM_PW : 0.f), 0.f, 1.f);
    {
        float saw, pul;
        if (c->sync && wrapB) {
            /* hard sync: A restarts when B wraps; patch the discontinuity with a
             * sub-sample-accurate correction (value just before the reset vs after). */
            float p = v->ph_a - incA * dfrac;
            float before, after, jump;
            if (p < 0.f) p += 1.f;
            v->ph_a = incA * dfrac;
            saw = 2.f * v->ph_a - 1.f + (-p) * pb_after(dfrac);
            before = p < pwA ? 1.f : -1.f;
            after  = v->ph_a < pwA ? 1.f : -1.f;
            jump = after - before;
            pul = after + 0.5f * jump * pb_after(dfrac);
        } else {
            saw = wave_saw(v->ph_a, incA);
            pul = wave_pulse(v->ph_a, incA, pwA);
        }
        aout = (1.f - sm[SM_A_WAVE]) * saw + sm[SM_A_WAVE] * pul;
    }

    /* ---- noise (white <-> pink) ---- */
    {
        const float white = rnd_bipolar(&v->rng);
        float pink;
        v->pink[0] = 0.99765f * v->pink[0] + white * 0.0990460f;
        v->pink[1] = 0.96300f * v->pink[1] + white * 0.2965164f;
        v->pink[2] = 0.57000f * v->pink[2] + white * 1.0526913f;
        pink = (v->pink[0] + v->pink[1] + v->pink[2] + white * 0.1848f) * 0.11f;
        noise = (1.f - c->noise_color) * white * 0.55f + c->noise_color * pink;
        if (fabsf(v->pink[0]) < 1e-20f) v->pink[0] = 0.f;
        if (fabsf(v->pink[1]) < 1e-20f) v->pink[1] = 0.f;
        if (fabsf(v->pink[2]) < 1e-20f) v->pink[2] = 0.f;
    }

    mix = 0.5f * (sm[SM_A_VOL] * aout + sm[SM_B_VOL] * bout + sm[SM_NOISE] * noise);

    /* ---- filter ---- */
    oct = sm[SM_CUTOFF] * 10.f - TP5_CUTOFF_OFFSET_OCT
        + sm[SM_ENVAMT] * fenv * 8.2f
        + c->kbd * (v->pitch - TP5_KBD_PIVOT) * (1.f / 12.f)
        + drF + c->lfo_filt
        + ((c->pm_dest & 4) ? pm_src * TP5_PM_FILTER_OCT : 0.f);
    fc = clampf(20.f * fast_exp2(oct), 10.f, 0.4f * sr);
    g = fast_tan(PI_F * fc / sr);
    k = reso_to_k(sm[SM_RESO]);
    /* An analog filter at full resonance sings from its own noise and the key-down click; an ideal one fed with silence never
     * starts. The "bell" and "alien" programs have an EMPTY mixer and rely on exactly that (oscillator B only modulates the
     * cutoff), so above the self-oscillation point the filter gets a click on the first sample of a note and a very small noise
     * floor, enough to start the oscillation within a few cycles. */
    if (k > TP5_SELFOSC_K) {
        mix += TP5_SELFOSC_NOISE * rnd_bipolar(&v->rng);
        if (v->kick) mix += TP5_SELFOSC_KICK;
    }
    v->kick = 0;
    y = tp5_ladder_tick(&v->filt, mix, g, k);

    if (!(y == y) || fabsf(y) > 4.f) { /* safety net; tests assert it never fires */
        tp5_ladder_reset(&v->filt);
        e->diag_nan_resets++;
        return;
    }
    y *= amp * (0.7f + 0.3f * v->vel) * c->uni_gain;
    *out_l += y * v->gain_l;
    *out_r += y * v->gain_r;
}

/* ------------------------------------------------------------------------- */
/* render                                                                     */
/* ------------------------------------------------------------------------- */

#if defined(__aarch64__)
static inline uint64_t fpcr_get(void) { uint64_t v; __asm__ volatile("mrs %0, fpcr" : "=r"(v)); return v; }
static inline void fpcr_set(uint64_t v) { __asm__ volatile("msr fpcr, %0" : : "r"(v)); }
#endif

static void make_ctx(const tp5_engine *e, const float *p, blockctx *c)
{
    static const uint8_t lfo_mask[8] = { 1, 2, 4, 5, 6, 3, 7, 0 }; /* 1=pitch 2=pwm 4=filter */
    memset(c, 0, sizeof *c);
    c->noise_color = p[P_NOISE_COL];
    c->uni_gain = ((int)p[P_UNISON] == 1 && p[P_VOICES] > 1.5f) ? 1.342f / sqrtf(p[P_VOICES]) : 1.f;   /* 0.6 for five stacked voices */
    c->pm_dest = (int)p[P_PM_DEST];
    c->b_lfo = p[P_B_LFO] > 0.5f;
    c->b_kbd = p[P_B_KBD] > 0.5f;
    c->sync = p[P_SYNC] > 0.5f;
    c->kbd = kbd_oct_per_oct(p[P_KBD]);
    c->drift = p[P_DRIFT];
    c->glide_coef = glide_coef_from(e->sr, p[P_GLIDE]);
    c->lfo_dest = lfo_mask[(int)p[P_LFO_DEST] & 7];
}

void tp5_render(tp5_engine *e, float *l, float *r, int frames)
{
    const float sr = e->sr;
    const float ceil_lin = TINYPRO5_CEILING_LIN;
    blockctx c;
    float tgt[SM_COUNT];
    float lfo_inc, lfo_coef;
    int n, i;
#if defined(__aarch64__)
    const uint64_t fpcr_saved = fpcr_get();
    fpcr_set(fpcr_saved | (1ull << 24)); /* flush-to-zero */
#endif

    /* block-rate setup */
    env_coefs(&e->fcoef, sr, e->p[P_F_ATK], e->p[P_F_DEC], e->p[P_F_SUS], e->p[P_F_REL], TP5_FENV_TSCALE, 0);
    env_coefs(&e->acoef, sr, e->p[P_A_ATK], e->p[P_A_DEC], e->p[P_A_SUS], e->p[P_A_REL], 1.f, 1);
    smooth_targets(e->p, tgt);
    make_ctx(e, e->p, &c);
    lfo_inc = 0.1f * powf(300.f, e->p[P_LFO_RATE]) / sr;
    lfo_coef = 1.f - expf(-1.f / (0.002f * sr));

    for (n = 0; n < frames; n++) {
        float lfo_raw, amt, mixl = 0.f, mixr = 0.f, ol, or_, vol, hx, ch_out[2];

        /* parameter smoothing: one pole per control -> no zipper noise */
        for (i = 0; i < SM_COUNT; i++) {
            const float d = tgt[i] - e->sm[i];
            e->sm[i] = (fabsf(d) < 1e-7f) ? tgt[i] : e->sm[i] + d * e->sm_coef; /* snap: never dwell in denormals */
        }

        /* global LFO */
        e->lfo_ph += lfo_inc;
        if (e->lfo_ph >= 1.f) e->lfo_ph -= 1.f;
        switch ((int)e->p[P_LFO_WAVE]) {
        case 0:  lfo_raw = 2.f * e->lfo_ph - 1.f; break;
        case 1:  lfo_raw = wave_tri(e->lfo_ph); break;
        default: lfo_raw = e->lfo_ph < 0.5f ? 1.f : -1.f; break;
        }
        e->lfo_out += (lfo_raw - e->lfo_out) * lfo_coef;
        amt = e->sm[SM_LFO_AMT];
        c.lfo = e->lfo_out;
        c.lfo_pitch = (c.lfo_dest & 1) ? c.lfo * amt * amt * 12.f : 0.f;
        c.lfo_pwm   = (c.lfo_dest & 2) ? c.lfo * amt * 0.4f : 0.f;
        c.lfo_filt  = (c.lfo_dest & 4) ? c.lfo * amt * 3.f : 0.f;

        for (i = 0; i < TINYPRO5_VOICES; i++)
            voice_tick(e, &e->voice[i], i, &c, e->sm, &e->acoef, &e->fcoef, &mixl, &mixr);

        /* master: volume^2 taper, calibrated headroom, DC block, soft ceiling */
        vol = e->sm[SM_VOLUME];
        vol = vol * vol * TINYPRO5_OUTPUT_HEADROOM;
        hx = mixl * vol;
        ol = hx - e->dc_x[0] + 0.9995f * e->dc_y[0]; e->dc_x[0] = hx; e->dc_y[0] = ol;
        hx = mixr * vol;
        or_ = hx - e->dc_x[1] + 0.9995f * e->dc_y[1]; e->dc_x[1] = hx; e->dc_y[1] = or_;
        if (fabsf(e->dc_y[0]) < 1e-20f) e->dc_y[0] = 0.f;
        if (fabsf(e->dc_y[1]) < 1e-20f) e->dc_y[1] = 0.f;

        {   /* makeup, then a look-ahead limiter: the gain is pulled down over 1.5 ms before a peak reaches the output, and recovers over
             * 80 ms, so the ceiling holds without waveshaping. Below the ceiling the gain is exactly 1 (a pure 1.5 ms delay). */
            const float pre = fmaxf(fabsf(ol), fabsf(or_));
            const int N = e->lim_n;
            float req, gmin, env, dl_l, dl_r, g, m;
            int j, q, pos = e->lim_pos;
            if (pre > e->diag_peak_pre) e->diag_peak_pre = pre;
            e->gain_sm += (e->prog_gain - e->gain_sm) * e->sm_coef;      /* the program's own level, eased in when a program loads */
            ol *= TINYPRO5_OUTPUT_MAKEUP * e->gain_sm; or_ *= TINYPRO5_OUTPUT_MAKEUP * e->gain_sm;
            m = fmaxf(fabsf(ol), fabsf(or_));
            req = m > ceil_lin ? ceil_lin / m : 1.f;                 /* gain this sample needs */
            dl_l = e->lim_dl[0][pos]; dl_r = e->lim_dl[1][pos];      /* the sample from N samples ago leaves the delay line */
            e->lim_dl[0][pos] = ol; e->lim_dl[1][pos] = or_;
            if (req < 1.f) e->lim_hot = N + 1;
            gmin = req;                                              /* minimum over the departing sample's requirement (still at pos) and the next N */
            if (e->lim_hot > 0) {                                    /* (cheap when idle) */
                for (j = 0; j < N; j++) if (e->lim_req[j] < gmin) gmin = e->lim_req[j];
                e->lim_hot--;
            }
            e->lim_req[pos] = req;
            env = gmin < e->lim_env ? gmin : e->lim_env + (gmin - e->lim_env) * e->lim_rel;     /* instant attack, slow release */
            e->lim_env = env;
            e->lim_avg_sum += env - e->lim_avg[pos];                 /* moving average over N: the attack becomes a 1.5 ms ramp */
            e->lim_avg[pos] = env;
            g = e->lim_avg_sum / (float)N;
            if (g > 1.f) g = 1.f;
            if (g < 1.f) e->diag_limiter_hits++;
            e->lim_pos = pos + 1 >= N ? 0 : pos + 1;
            ch_out[0] = dl_l * g; ch_out[1] = dl_r * g;
            for (q = 0; q < 2; q++)                                  /* backstop against float rounding: the ceiling is absolute */
                if (ch_out[q] > ceil_lin) ch_out[q] = ceil_lin; else if (ch_out[q] < -ceil_lin) ch_out[q] = -ceil_lin;
            ol = ch_out[0]; or_ = ch_out[1];
            if (fmaxf(fabsf(ol), fabsf(or_)) > e->diag_peak_post)
                e->diag_peak_post = fmaxf(fabsf(ol), fabsf(or_));
        }
        l[n] = ol;
        r[n] = or_;
    }
    e->diag_blocks++;
#if defined(__aarch64__)
    fpcr_set(fpcr_saved);
#endif
}

void tp5_render_i16(tp5_engine *e, int16_t *out, int frames)
{
    float bl[128], br[128];
    int done = 0;
    while (done < frames) {
        const int n = (frames - done) > 128 ? 128 : (frames - done);
        int i;
        tp5_render(e, bl, br, n);
        for (i = 0; i < n; i++) {
            long a = lrintf(bl[i] * 32768.f), b = lrintf(br[i] * 32768.f);
            if (a > 32767) a = 32767;
            if (a < -32768) a = -32768;
            if (b > 32767) b = 32767;
            if (b < -32768) b = -32768;
            out[2 * (done + i)] = (int16_t)a;
            out[2 * (done + i) + 1] = (int16_t)b;
        }
        done += n;
    }
}

/* ------------------------------------------------------------------------- */
/* state (patch + user slots), text form: "TP5:1;key=val;...;uN=v,v,...;"     */
/* ------------------------------------------------------------------------- */

int tp5_state_save(const tp5_engine *e, char *buf, int cap)
{
    int len, i;
    if (!buf || cap < 16) return -1;
    len = snprintf(buf, (size_t)cap, "TP5:1;");
    if (len < 0 || len >= cap) return -1;
    for (i = 0; i < P_COUNT; i++) {
        int w;
        if (i == P_LOAD || i == P_SAVE || i == P_REVERT || i == P_MODWHEEL) continue;
        w = snprintf(buf + len, (size_t)(cap - len), "%s=%.5g;", tp5_params[i].key, (double)e->p[i]);
        if (w < 0 || w >= cap - len) { buf[0] = 0; return -1; }
        len += w;
    }
    return len;
}

int tp5_state_load(tp5_engine *e, const char *s)
{
    if (!s || strncmp(s, "TP5:1;", 6) != 0) return -1;
    s += 6;
    while (*s) {
        const char *eq = strchr(s, '=');
        const char *end = strchr(s, ';');
        char key[32];
        size_t kl;
        if (!eq || !end || eq > end) break;
        kl = (size_t)(eq - s);
        if (kl < sizeof key) {
            memcpy(key, s, kl);
            key[kl] = 0;
            if (key[0] == 'u' && key[1] >= '0' && key[1] <= '9') {
                const int slot = atoi(key + 1);
                if (slot >= 0 && slot < TINYPRO5_PRESET_COUNT) {
                    /* states written before the overlay existed kept factory edits here: move them into the overlay (bank 0) */
                    const char *q = eq + 1;
                    float ov[P_COUNT];
                    int i;
                    for (i = 0; i < P_COUNT; i++) ov[i] = tp5_params[i].def;
                    for (i = 0; i < P_COUNT && q < end; i++) {
                        char *nx;
                        if (!tp5_is_sound_param(i)) continue;
                        ov[i] = clampf(strtof(q, &nx), tp5_params[i].min, tp5_params[i].max);
                        q = (*nx == ',') ? nx + 1 : nx;
                    }
                    if (slot < tp5_bank_size(0)) tp5_overlay_put(0, slot, ov);
                }
            } else {
                const int id = tp5_param_id_from_key(key);
                if (id >= 0) {
                    float v = strtof(eq + 1, NULL);
                    v = clampf(v, tp5_params[id].min, tp5_params[id].max);
                    if (tp5_params[id].is_int) v = floorf(v + 0.5f);
                    e->p[id] = v;
                }
            }
        }
        s = end + 1;
    }
    e->prog_gain = tp5_bank_gain(selected_bank(e), selected_slot(e));
    {   /* a session saved against a longer list (the old 200-program factory bank) must not point past this one */
        const int size = tp5_bank_size(selected_bank(e));
        if (size > 0 && e->p[P_PRESET] > (float)(size - 1)) e->p[P_PRESET] = (float)(size - 1);
    }
    e->base_voices = (int)e->p[P_VOICES];            /* a restored session starts "as loaded" */
    return 0;
}
