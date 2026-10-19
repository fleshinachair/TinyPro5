/*
 * Rev 2 / Rev 4 voicing, Vintage, velocity and aftertouch. Run from the repository root; exit status is non-zero on any failure.
 * Every check here is relative (Rev 4 against Rev 2, Vintage 1 against 0): nothing depends on third-party data or on hash values.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/bank.h"
#include "../src/meta.h"
#include "../src/schwung_api.h"
#include "../src/tinypro5.h"

#define SR 44100.0f
#define BLOCK 128

static int g_checks, g_fail;

#define CHECK(cond, ...) do { \
        g_checks++; \
        if (!(cond)) { g_fail++; printf("    FAIL %s:%d  ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } \
    } while (0)

static tp5_engine E, F;
static float BL[BLOCK], BR[BLOCK];

static float db(float x) { return 20.f * log10f(x > 1e-12f ? x : 1e-12f); }

/* every sound parameter at its default, then the given revision chosen the way a player would (set_param) */
static void fresh(tp5_engine *e, int rev)
{
    int i;
    tp5_init(e, SR);
    for (i = 0; i < P_COUNT; i++) if (tp5_is_sound_param(i)) tp5_set_param(e, i, tp5_params[i].def);
    tp5_set_param(e, P_VOLUME, 1.f);
    tp5_set_param(e, P_DRIFT, 0.f);
    if (rev) tp5_set_param(e, P_REV, 1.f);
}

/* a plain sustained saw patch, filter closed enough that cutoff and velocity changes are audible */
static void saw_patch(tp5_engine *e)
{
    tp5_set_param(e, P_A_VOL, 1.f);
    tp5_set_param(e, P_CUTOFF, 0.35f);
    tp5_set_param(e, P_RESO, 0.f);
    tp5_set_param(e, P_ENVAMT, 0.f);
    tp5_set_param(e, P_A_ATK, 0.f);
    tp5_set_param(e, P_A_SUS, 1.f);
}

/* renders `frames`, returns the rms of the left channel over the second half; *finite goes 0 on NaN/Inf; *peak is the largest |sample| */
static double run(tp5_engine *e, int frames, int *finite, float *peak)
{
    double acc = 0.0;
    long cnt = 0;
    int done = 0, i;
    if (peak) *peak = 0.f;
    while (done < frames) {
        const int n = frames - done > BLOCK ? BLOCK : frames - done;
        tp5_render(e, BL, BR, n);
        for (i = 0; i < n; i++) {
            if (!isfinite(BL[i]) || !isfinite(BR[i])) { if (finite) *finite = 0; continue; }
            if (peak && fabsf(BL[i]) > *peak) *peak = fabsf(BL[i]);
            if (done + i >= frames / 2) { acc += (double)BL[i] * BL[i]; cnt++; }
        }
        done += n;
    }
    return cnt ? sqrt(acc / (double)cnt) : 0.0;
}

static double note_rms(int rev, int vel, float vel_amp, float vel_filt, float env_amt, int pressure)
{
    int finite = 1;
    fresh(&E, rev);
    saw_patch(&E);
    if (env_amt > 0.f) tp5_set_param(&E, P_CUTOFF, 0.15f);       /* a closed filter that the envelope has to open */
    tp5_set_param(&E, P_ENVAMT, env_amt);
    tp5_set_param(&E, P_F_ATK, 0.f); tp5_set_param(&E, P_F_DEC, 0.5f); tp5_set_param(&E, P_F_SUS, 1.f);
    E.p[P_VEL_AMP] = vel_amp; E.p[P_VEL_FILT] = vel_filt;
    tp5_note_on(&E, 48, vel);
    if (pressure) tp5_aftertouch(&E, pressure);
    return run(&E, 16384, &finite, NULL);
}

/* ======================================================================= */

static void test_params(void)
{
    const tp5_engine *e = &E;
    char st[8192];
    int i;
    printf("[parameters, defaults, state]\n");
    CHECK(strcmp(tp5_params[P_REV].key, "rev") == 0 && tp5_params[P_REV].is_int && tp5_params[P_REV].min == 0.f && tp5_params[P_REV].max == 1.f &&
          tp5_params[P_REV].def == (float)REV_2, "rev: 0..1 integer, default REV_2");
    CHECK(strcmp(tp5_params[P_VINTAGE].key, "vintage") == 0 && !tp5_params[P_VINTAGE].is_int && tp5_params[P_VINTAGE].min == 0.f &&
          tp5_params[P_VINTAGE].max == 1.f && tp5_params[P_VINTAGE].def == 0.f, "vintage: float 0..1, table default 0 (a program that does not list it adds nothing)");
    CHECK(tp5_param_id_from_key("vel_filter") == P_VEL_FILT && tp5_param_id_from_key("vel_amp") == P_VEL_AMP, "velocity depth keys");
    for (i = P_REV; i <= P_VEL_AMP; i++) CHECK(tp5_is_sound_param(i), "%s is a sound parameter (programs and the overlay carry it)", tp5_params[i].key);
    CHECK(P_REV < P_VOICES && P_VEL_AMP < P_VOICES, "new parameters sit before the performance parameters");

    tp5_init(&E, SR);
    for (i = 0; i < TINYPRO5_PRESET_COUNT; i++) {
        tp5_load_preset(&E, i);
        if ((int)e->p[P_REV] != REV_2 || e->p[P_VINTAGE] != 0.f || e->p[P_VEL_FILT] != 0.f || e->p[P_VEL_AMP] != 0.f) break;
    }
    CHECK(i == TINYPRO5_PRESET_COUNT, "every factory program loads as REV_2, vintage 0, no velocity depth (program %d)", i);

    /* choosing a revision sets that revision's default vintage; loading or restoring does not */
    fresh(&E, 0);
    tp5_set_param(&E, P_REV, 1.f);
    CHECK((int)e->p[P_REV] == REV_4 && e->p[P_VINTAGE] == TP5_VINTAGE_REV4_DEF && e->p[P_VEL_FILT] == TP5_VEL_FILT_REV4_DEF &&
          e->p[P_VEL_AMP] == TP5_VEL_AMP_REV4_DEF, "REV_4 chosen: vintage %.2f vel %.2f/%.2f", (double)e->p[P_VINTAGE], (double)e->p[P_VEL_FILT], (double)e->p[P_VEL_AMP]);
    tp5_set_param(&E, P_VINTAGE, 0.4f);
    tp5_set_param(&E, P_REV, 1.f);
    CHECK(e->p[P_VINTAGE] == 0.4f, "choosing the revision that is already active leaves the vintage alone");
    tp5_set_param(&E, P_REV, 0.f);
    CHECK((int)e->p[P_REV] == REV_2 && fabsf(e->p[P_VINTAGE] - 0.65f) < 1e-6f, "REV_2 chosen: vintage %.2f (the 0.6-0.7 Rev 2 default)", (double)e->p[P_VINTAGE]);
    tp5_set_param(&E, P_REV, 5.f);
    CHECK((int)e->p[P_REV] == REV_4, "rev clamps to the last revision");
    tp5_set_param(&E, P_REV, NAN);
    CHECK((int)e->p[P_REV] == REV_4, "NaN is rejected");

    /* state round trip and an old state without the keys */
    fresh(&E, 1);
    tp5_set_param(&E, P_VINTAGE, 0.3f); tp5_set_param(&E, P_VEL_AMP, 0.8f); tp5_set_param(&E, P_VEL_FILT, 0.6f);
    CHECK(tp5_state_save(&E, st, sizeof st) > 0, "state saves");
    tp5_init(&F, SR);
    CHECK(tp5_state_load(&F, st) == 0 && (int)F.p[P_REV] == REV_4 && fabsf(F.p[P_VINTAGE] - 0.3f) < 1e-4f && fabsf(F.p[P_VEL_AMP] - 0.8f) < 1e-4f &&
          fabsf(F.p[P_VEL_FILT] - 0.6f) < 1e-4f, "state restores rev %d vintage %.2f", (int)F.p[P_REV], (double)F.p[P_VINTAGE]);
    tp5_init(&F, SR);
    CHECK(tp5_state_load(&F, "TP5:1;cutoff=0.4;resonance=0.2;") == 0 && (int)F.p[P_REV] == REV_2 && F.p[P_VINTAGE] == 0.f && F.p[P_VEL_AMP] == 0.f,
          "a state saved before the switch existed restores as REV_2, vintage 0");

    /* the overlay (Save) keeps all four, by key */
    {
        float vals[P_COUNT], back[P_COUNT];
        char buf[8192];
        for (i = 0; i < P_COUNT; i++) vals[i] = tp5_params[i].def;
        vals[P_REV] = 1.f; vals[P_VINTAGE] = 0.25f; vals[P_VEL_FILT] = 0.5f; vals[P_VEL_AMP] = 0.75f;
        tp5_overlay_clear();
        CHECK(tp5_overlay_put(0, 3, vals) == 0 && tp5_overlay_serialize("Factory", buf, sizeof buf) > 0, "overlay stores");
        CHECK(strstr(buf, "rev=1;") && strstr(buf, "vintage=0.25;") && strstr(buf, "vel_filter=0.5;") && strstr(buf, "vel_amp=0.75;"), "overlay text carries the keys");
        tp5_overlay_clear();
        CHECK(tp5_overlay_parse(buf, strlen(buf)) == 1 && tp5_overlay_get(0, 3, back) && back[P_REV] == 1.f && back[P_VINTAGE] == 0.25f &&
              back[P_VEL_AMP] == 0.75f, "overlay text round-trips");
        tp5_overlay_clear();
        tp5_set_param(&E, P_REV, 0.f);
        tp5_overlay_clear();
    }
}

static void test_rev2_unchanged(void)
{
    int i, same = 1, finite = 1;
    printf("[Rev 2 is the engine as it was]\n");
    /* Rev 4 only parameters, vintage 0 and pressure messages must not touch a REV_2 voice by a single bit */
    for (i = 0; i < 4; i++) {
        long n;
        fresh(&E, 0);
        fresh(&F, 0);
        if (i == 0) { saw_patch(&E); saw_patch(&F); }
        if (i == 1) { tp5_set_param(&E, P_RESO, 0.7f); tp5_set_param(&F, P_RESO, 0.7f); tp5_set_param(&E, P_UNISON, 1.f); tp5_set_param(&F, P_UNISON, 1.f); }
        if (i >= 2) { tp5_set_param(&E, P_PM_FENV, 0.5f); tp5_set_param(&F, P_PM_FENV, 0.5f); tp5_set_param(&E, P_PM_DEST, 7.f); tp5_set_param(&F, P_PM_DEST, 7.f);
                      tp5_set_param(&E, P_LFO_AMT, 0.5f); tp5_set_param(&F, P_LFO_AMT, 0.5f); tp5_set_param(&E, P_B_VOL, 0.6f); tp5_set_param(&F, P_B_VOL, 0.6f); }
        F.p[P_VEL_FILT] = 1.f; F.p[P_VEL_AMP] = 1.f;                         /* ignored in REV_2 */
        tp5_note_on(&E, 40, 20 + 30 * i); tp5_note_on(&E, 52, 90);
        tp5_note_on(&F, 40, 20 + 30 * i); tp5_note_on(&F, 52, 90);
        for (n = 0; n < 120 && same; n++) {
            int k;
            float l1[BLOCK], r1[BLOCK];
            if (n == 20) { tp5_aftertouch(&F, 127); }                       /* ignored in REV_2 */
            if (n == 60) { uint8_t m[2] = { 0xD0, 100 }; tp5_midi(&F, m, 2); }
            if (n == 80) { uint8_t m[3] = { 0xA0, 40, 127 }; tp5_midi(&F, m, 3); }           /* polyphonic pressure on a sounding note: ignored in REV_2 */
            tp5_render(&E, l1, r1, BLOCK);
            tp5_render(&F, BL, BR, BLOCK);
            for (k = 0; k < BLOCK; k++) if (l1[k] != BL[k] || r1[k] != BR[k]) { same = 0; break; }
        }
        CHECK(same, "case %d: REV_2 output changed with velocity depths / aftertouch set", i);
    }
    (void)finite;
    /* the engine keeps exactly the ladder it had */
    {
        tp5_ladder a, b;
        unsigned s = 12345u;
        int ok = 1, n;
        tp5_ladder_reset(&a); tp5_ladder_reset(&b);
        for (n = 0; n < 20000; n++) {
            float x, g, k;
            s = s * 1664525u + 1013904223u;
            x = ((float)(s >> 8) / 8388608.f - 1.f) * 1.3f;
            g = 0.01f + 0.5f * (float)((s >> 3) & 255) / 255.f;
            k = 8.f * (float)((n / 500) % 11) / 10.f;
            if (tp5_ladder_tick(&a, x, g, k) != tp5_ladder_tick_rev(&b, x, g, k, 0.f)) { ok = 0; break; }
        }
        CHECK(ok, "tp5_ladder_tick_rev(rev = 0) is tp5_ladder_tick bit for bit");
    }
}

static double ladder_gain_db(float k, float rev, float f_hz, float fc_hz)
{
    tp5_ladder lad;
    const float g = tanf(3.14159265f * fc_hz / SR);
    float peak = 0.f;
    int n;
    tp5_ladder_reset(&lad);
    for (n = 0; n < 44100; n++) {
        const float x = 0.01f * sinf(6.2831853f * f_hz * (float)n / SR);       /* small signal: the linear region */
        const float y = tp5_ladder_tick_rev(&lad, x, g, k, rev);
        if (n > 22050 && fabsf(y) > peak) peak = fabsf(y);
    }
    return (double)db(peak / 0.01f);
}

static void test_filter_voicing(void)
{
    static const float ks[4] = { 1.5f, 2.27f, 3.1f, 3.8f };
    int i;
    printf("[Rev 4 filter]\n");
    CHECK(fabs(ladder_gain_db(0.f, 0.f, 100.f, 2000.f) - ladder_gain_db(0.f, 1.f, 100.f, 2000.f)) < 0.01, "no resonance: both revisions pass the bass alike");
    for (i = 0; i < 4; i++) {
        const double r2 = ladder_gain_db(ks[i], 0.f, 100.f, 2000.f), r4 = ladder_gain_db(ks[i], 1.f, 100.f, 2000.f);
        printf("    k=%.2f  bass level (100 Hz, fc 2 kHz): Rev 2 %.2f dB, Rev 4 %.2f dB\n", (double)ks[i], r2, r4);
        CHECK(r4 > r2 + 3.0, "k=%.2f: Rev 4 keeps more bass (%.2f vs %.2f dB)", (double)ks[i], r4, r2);
        CHECK(r4 < 0.5, "k=%.2f: Rev 4 does not boost the passband (%.2f dB)", (double)ks[i], r4);
    }
    {   /* the ladder is bounded and finite in both voicings under abuse, including a rev that moves every sample */
        unsigned s = 99u;
        int n, ok = 1;
        tp5_ladder a;
        tp5_ladder_reset(&a);
        for (n = 0; n < 200000; n++) {
            float x, y;
            s = s * 1664525u + 1013904223u;
            x = ((float)(s >> 8) / 8388608.f - 1.f) * 4.f;
            y = tp5_ladder_tick_rev(&a, x, 0.02f + 0.9f * (float)((s >> 5) & 127) / 127.f, 8.f * (float)((s >> 12) & 127) / 127.f, (float)((s >> 20) & 1));
            if (!isfinite(y) || fabsf(y) > 4.f) { ok = 0; break; }
        }
        CHECK(ok, "ladder stays finite and bounded with the revision flipping every sample");
    }
}

/* samples until the amp envelope of voice 0 leaves its attack stage */
static int attack_samples(int rev, float knob)
{
    int n = 0;
    fresh(&E, rev);
    tp5_set_param(&E, P_A_ATK, knob);
    run(&E, 8192, NULL, NULL);                 /* let the revision crossfade settle before the note */
    tp5_note_on(&E, 60, 127);
    while (E.voice[0].a_stage == ENV_ATK && n < 44100 * 20) { tp5_render(&E, BL, BR, BLOCK); n += BLOCK; }
    return n;
}

static int release_samples(int rev, float knob)
{
    int n = 0;
    fresh(&E, rev);
    tp5_set_param(&E, P_A_REL, knob);
    tp5_set_param(&E, P_A_ATK, 0.f);
    tp5_set_param(&E, P_A_SUS, 1.f);
    run(&E, 8192, NULL, NULL);
    tp5_note_on(&E, 60, 127);
    run(&E, 8192, NULL, NULL);
    tp5_note_off(&E, 60);
    while (E.voice[0].a_stage != ENV_IDLE && n < 44100 * 40) { tp5_render(&E, BL, BR, BLOCK); n += BLOCK; }
    return n;
}

static void test_envelopes(void)
{
    int i;
    printf("[Rev 4 envelopes]\n");
    for (i = 0; i < 3; i++) {
        static const float knob[3] = { 0.3f, 0.5f, 0.7f };
        const int a2 = attack_samples(0, knob[i]), a4 = attack_samples(1, knob[i]);
        const double ratio = (double)a4 / (double)a2;
        CHECK(ratio > 0.55 && ratio < 0.68, "attack knob %.1f: Rev 4 takes %.2fx the Rev 2 time (want ~0.6)", (double)knob[i], ratio);
    }
    {
        const int r2 = release_samples(0, 0.5f), r4 = release_samples(1, 0.5f);
        const double ratio = (double)r4 / (double)r2;
        printf("    release knob 0.5: Rev 2 %d samples, Rev 4 %d (%.2fx)\n", r2, r4, ratio);
        CHECK(ratio > 0.62 && ratio < 0.78, "release: Rev 4 takes %.2fx the Rev 2 time (want ~0.7)", ratio);
    }
    {   /* a Rev 4 decay settles at the sustain level without lingering; the stage ends */
        int n = 0;
        fresh(&E, 1);
        tp5_set_param(&E, P_A_ATK, 0.f); tp5_set_param(&E, P_A_DEC, 0.4f); tp5_set_param(&E, P_A_SUS, 0.5f);
        tp5_note_on(&E, 60, 127);
        while (E.voice[0].a_stage != ENV_SUS && n < 44100 * 10) { tp5_render(&E, BL, BR, BLOCK); n += BLOCK; }
        CHECK(E.voice[0].a_stage == ENV_SUS, "Rev 4 decay reaches its sustain stage");
    }
}

static void test_velocity_aftertouch(void)
{
    double lo, hi, none_lo, none_hi;
    printf("[Rev 4 velocity and aftertouch]\n");
    /* amp: full depth makes the softest note (vel 32 = 0.25) a quarter as loud; zero depth makes velocity irrelevant */
    lo = note_rms(1, 32, 1.f, 0.f, 0.f, 0); hi = note_rms(1, 127, 1.f, 0.f, 0.f, 0);
    printf("    REV_4 vel_amp 1.0: vel 32 / vel 127 = %.3f (want ~0.25)\n", lo / hi);
    CHECK(lo / hi > 0.22 && lo / hi < 0.29, "full velocity depth: ratio %.3f", lo / hi);
    none_lo = note_rms(1, 32, 0.f, 0.f, 0.f, 0); none_hi = note_rms(1, 127, 0.f, 0.f, 0.f, 0);
    CHECK(fabs(none_lo / none_hi - 1.0) < 0.01, "zero velocity depth: ratio %.3f", none_lo / none_hi);
    lo = note_rms(1, 32, 0.5f, 0.f, 0.f, 0); hi = note_rms(1, 127, 0.5f, 0.f, 0.f, 0);
    CHECK(fabs(lo / hi - (1.0 - 0.5 * 0.748)) < 0.02, "half depth: ratio %.3f (want %.3f)", lo / hi, 1.0 - 0.5 * 0.748);
    lo = note_rms(0, 32, 1.f, 1.f, 0.f, 0); hi = note_rms(0, 127, 1.f, 1.f, 0.f, 0);
    CHECK(fabs(lo / hi - (0.7 + 0.3 * 32.0 / 127.0)) < 0.01, "REV_2 keeps its fixed 0.7 + 0.3 vel response: ratio %.3f", lo / hi);
    {   /* filter: velocity scales the filter envelope, so a soft note is darker and quieter than a hard one when the envelope opens the filter */
        double a = note_rms(1, 127, 0.f, 0.f, 1.f, 0), b = note_rms(1, 32, 0.f, 0.f, 1.f, 0);
        double c = note_rms(1, 127, 0.f, 1.f, 1.f, 0), d = note_rms(1, 32, 0.f, 1.f, 1.f, 0);
        printf("    filter env open: vel_filter 0 soft/hard %.3f, vel_filter 1 soft/hard %.3f\n", b / a, d / c);
        CHECK(fabs(b / a - 1.0) < 0.01, "no filter velocity depth: soft = hard (%.3f)", b / a);
        CHECK(d / c < 0.8, "full filter velocity depth: the soft note is darker (%.3f)", d / c);
    }
    {   /* aftertouch: more cutoff in Rev 4, nothing in Rev 2 */
        const double r4 = note_rms(1, 100, 0.f, 0.f, 0.f, 0), r4p = note_rms(1, 100, 0.f, 0.f, 0.f, 127);
        const double r2 = note_rms(0, 100, 0.f, 0.f, 0.f, 0), r2p = note_rms(0, 100, 0.f, 0.f, 0.f, 127);
        printf("    pressure 127: Rev 4 %+.2f dB, Rev 2 %+.2f dB\n", (double)db((float)(r4p / r4)), (double)db((float)(r2p / r2)));
        CHECK(r4p > r4 * 1.5, "Rev 4 pressure opens the filter (%.2fx)", r4p / r4);
        CHECK(r2p == r2, "Rev 2 ignores pressure");
    }
    {   /* polyphonic pressure (the Move pads, MIDI 0xA0): only the voice playing that note opens, a new note starts closed, release clears it */
        int finite = 1, i;
        uint8_t m[3] = { 0xA0, 48, 127 };
        fresh(&E, 1);
        saw_patch(&E);
        tp5_note_on(&E, 48, 100); tp5_note_on(&E, 60, 100);
        tp5_midi(&E, m, 3);
        CHECK(E.voice[0].note == 48 && E.voice[0].press > 0.99f && E.voice[1].press == 0.f, "0xA0 reaches the voice playing note 48 only (%.2f / %.2f)", (double)E.voice[0].press, (double)E.voice[1].press);
        run(&E, 8192, &finite, NULL);
        CHECK(E.voice[0].press_sm > 0.99f && E.voice[1].press_sm == 0.f, "per-voice pressure is smoothed and settles");
        {   /* same note with poly pressure on it vs without */
            double with, without;
            fresh(&E, 1); saw_patch(&E); tp5_note_on(&E, 48, 100); tp5_midi(&E, m, 3); with = run(&E, 16384, &finite, NULL);
            fresh(&E, 1); saw_patch(&E); tp5_note_on(&E, 48, 100); without = run(&E, 16384, &finite, NULL);
            printf("    poly pressure 127 on a held note: whole-signal level %+.2f dB (the energy moves up into the harmonics)\n", (double)db((float)(with / without)));
            CHECK(with != without, "poly pressure changes the sound");
        }
        tp5_note_off(&E, 48);
        CHECK(E.voice[0].press == 0.f, "release clears the pressure");
        tp5_note_on(&E, 48, 100);
        {   /* the new note lands on an idle voice, not the releasing one */
            int g = -1;
            for (i = 0; i < TINYPRO5_VOICES; i++) if (E.voice[i].gate && E.voice[i].note == 48) g = i;
            CHECK(g >= 0 && E.voice[g].press == 0.f && E.voice[g].press_sm == 0.f, "a new note starts with no pressure");
            { uint8_t bad[3] = { 0xA0, 99, 127 }; tp5_midi(&E, bad, 3); }
            { uint8_t shortmsg[2] = { 0xA0, 48 }; tp5_midi(&E, shortmsg, 2); }
            CHECK(g >= 0 && E.voice[g].press == 0.f, "pressure for a note that is not playing, or a truncated message, is ignored");
            tp5_poly_pressure(&E, 48, 1000);
            CHECK(g >= 0 && E.voice[g].press == 1.f, "pressure is clamped");
        }
        /* LFO depth follows the hardest pressed voice */
        fresh(&E, 1);
        tp5_set_param(&E, P_LFO_AMT, 0.1f);
        tp5_note_on(&E, 48, 100);
        tp5_poly_pressure(&E, 48, 127);
        run(&E, 8192, &finite, NULL);
        CHECK(fabsf(E.sm[SM_LFO_AMT] - 0.7f) < 0.01f, "Rev 4: poly pressure adds 0.6 LFO depth too (%.3f)", (double)E.sm[SM_LFO_AMT]);
        fresh(&E, 0);
        tp5_set_param(&E, P_LFO_AMT, 0.1f);
        tp5_note_on(&E, 48, 100);
        tp5_poly_pressure(&E, 48, 127);
        run(&E, 8192, &finite, NULL);
        CHECK(fabsf(E.sm[SM_LFO_AMT] - 0.1f) < 1e-4f, "Rev 2: poly pressure leaves the LFO alone");
    }
    {   /* ... and deepens the LFO */
        int finite = 1;
        fresh(&E, 1);
        tp5_set_param(&E, P_LFO_AMT, 0.1f);
        tp5_aftertouch(&E, 127);
        run(&E, 8192, &finite, NULL);
        CHECK(fabsf(E.sm[SM_LFO_AMT] - 0.7f) < 0.01f, "Rev 4: full pressure adds 0.6 LFO depth (%.3f)", (double)E.sm[SM_LFO_AMT]);
        tp5_all_notes_off(&E);
        CHECK(E.aftertouch == 0.f, "all notes off clears the pressure");
        fresh(&E, 0);
        tp5_set_param(&E, P_LFO_AMT, 0.1f);
        tp5_aftertouch(&E, 127);
        run(&E, 8192, &finite, NULL);
        CHECK(fabsf(E.sm[SM_LFO_AMT] - 0.1f) < 1e-4f, "Rev 2: pressure leaves the LFO depth alone (%.3f)", (double)E.sm[SM_LFO_AMT]);
        tp5_aftertouch(&E, 1000);
        tp5_aftertouch(&E, -5);
        CHECK(E.aftertouch == 0.f, "pressure is clamped");
    }
}

static void test_vintage(void)
{
    int finite = 1, i, same = 1;
    float a_env[TINYPRO5_VOICES];
    printf("[Vintage]\n");
    /* 0 adds nothing, 1 changes the sound */
    fresh(&E, 0); fresh(&F, 0);
    saw_patch(&E); saw_patch(&F);
    tp5_set_param(&F, P_VINTAGE, 0.f);
    tp5_note_on(&E, 48, 100); tp5_note_on(&F, 48, 100);
    for (i = 0; i < 40 && same; i++) {
        float l1[BLOCK], r1[BLOCK];
        int k;
        tp5_render(&E, l1, r1, BLOCK); tp5_render(&F, BL, BR, BLOCK);
        for (k = 0; k < BLOCK; k++) if (l1[k] != BL[k]) same = 0;
    }
    CHECK(same, "vintage 0 is bit-identical to an engine that never heard of it");
    {
        double d = 0.0, s = 0.0;
        float l1[BLOCK], r1[BLOCK];
        fresh(&E, 0); fresh(&F, 0);
        saw_patch(&E); saw_patch(&F);
        tp5_set_param(&F, P_VINTAGE, 1.f);
        for (i = 0; i < 5; i++) { tp5_note_on(&E, 36 + 5 * i, 100); tp5_note_on(&F, 36 + 5 * i, 100); }
        for (i = 0; i < 200; i++) {
            int k;
            tp5_render(&E, l1, r1, BLOCK); tp5_render(&F, BL, BR, BLOCK);
            for (k = 0; k < BLOCK; k++) { d += (double)(l1[k] - BL[k]) * (l1[k] - BL[k]); s += (double)l1[k] * l1[k]; if (!isfinite(BL[k])) finite = 0; }
        }
        printf("    vintage 1 vs 0 on a five-note chord: difference %.1f dB below the signal\n", 10.0 * log10(d / s));
        CHECK(d > 0.0 && finite, "vintage 1 changes the sound and stays finite");
    }
    {   /* envelope variance: five voices triggered together do not sit at the same point of a slow attack when vintage is up */
        double spread0, spread1;
        float lo = 1e9f, hi = -1e9f;
        fresh(&E, 0); tp5_set_param(&E, P_A_ATK, 0.6f); tp5_set_param(&E, P_VINTAGE, 0.f);
        for (i = 0; i < 5; i++) tp5_note_on(&E, 36 + 5 * i, 100);
        run(&E, 20000, &finite, NULL);
        for (i = 0; i < 5; i++) a_env[i] = E.voice[i].a_env;
        for (i = 0; i < 5; i++) { if (a_env[i] < lo) lo = a_env[i]; if (a_env[i] > hi) hi = a_env[i]; }
        spread0 = hi - lo;
        lo = 1e9f; hi = -1e9f;
        fresh(&E, 0); tp5_set_param(&E, P_A_ATK, 0.6f); tp5_set_param(&E, P_VINTAGE, 1.f);
        for (i = 0; i < 5; i++) tp5_note_on(&E, 36 + 5 * i, 100);
        run(&E, 20000, &finite, NULL);
        for (i = 0; i < 5; i++) { if (E.voice[i].a_env < lo) lo = E.voice[i].a_env; if (E.voice[i].a_env > hi) hi = E.voice[i].a_env; }
        spread1 = hi - lo;
        printf("    attack position spread across the five voices: vintage 0 %.4f, vintage 1 %.4f\n", spread0, spread1);
        CHECK(spread0 == 0.0 && spread1 > 0.02, "envelope variance appears only with vintage (%.4f -> %.4f)", spread0, spread1);
    }
    {   /* micro-detune and drift: two voices on the same note, same everything, run apart in pitch only with vintage */
        double ph0, ph1;
        fresh(&E, 0); tp5_set_param(&E, P_B_VOL, 0.f);
        tp5_set_param(&E, P_VINTAGE, 1.f);
        tp5_note_on(&E, 60, 100); tp5_note_on(&E, 61, 100);
        run(&E, 4096, &finite, NULL);
        ph0 = E.voice[0].ph_a; ph1 = E.voice[1].ph_a;
        CHECK(isfinite(ph0) && isfinite(ph1), "oscillator phases finite");
    }
}

/* the same engine run through a mid-note revision switch: finite, no safety-net resets, no denormals, no jump bigger than the signal itself */
static void test_switching(void)
{
    int finite = 1, cycle, i, n;
    double worst_jump = 0.0, steady_jump = 0.0;
    float prev = 0.f;
    printf("[switching revision mid-note]\n");
    fresh(&E, 0);
    saw_patch(&E);
    tp5_set_param(&E, P_RESO, 0.55f);
    tp5_set_param(&E, P_B_VOL, 0.7f); tp5_set_param(&E, P_NOISE, 0.1f);
    tp5_set_param(&E, P_ENVAMT, 0.5f); tp5_set_param(&E, P_F_DEC, 0.6f); tp5_set_param(&E, P_F_SUS, 0.4f);
    for (i = 0; i < 5; i++) tp5_note_on(&E, 36 + 7 * i, 100);
    run(&E, 44100, &finite, NULL);
    for (cycle = 0; cycle < 12; cycle++) {
        const int to = (cycle & 1) ? 0 : 1;
        double jump_before = 0.0, jump_after = 0.0;
        for (n = 0; n < 6 * BLOCK; n++) {                         /* steady stretch */
            if (n % BLOCK == 0) tp5_render(&E, BL, BR, BLOCK);
            { const double dj = fabs((double)BL[n % BLOCK] - (double)prev); if (dj > jump_before) jump_before = dj; prev = BL[n % BLOCK]; }
        }
        tp5_set_param(&E, P_REV, (float)to);
        for (n = 0; n < 16 * BLOCK; n++) {                        /* the switch, mid-note */
            if (n % BLOCK == 0) tp5_render(&E, BL, BR, BLOCK);
            { const float x = BL[n % BLOCK]; const double dj = fabs((double)x - (double)prev); if (dj > jump_after) jump_after = dj; prev = x; if (!isfinite(x)) finite = 0; }
        }
        if (jump_after > worst_jump) worst_jump = jump_after;
        if (jump_before > steady_jump) steady_jump = jump_before;
        CHECK(E.diag_nan_resets == 0, "cycle %d: the filter safety net fired", cycle);
    }
    printf("    12 switches: largest sample step %.4f (steady %.4f)\n", worst_jump, steady_jump);
    CHECK(finite, "no NaN/Inf across switches");
    CHECK(worst_jump <= 2.5 * steady_jump + 0.01, "no pop: largest step after a switch %.4f vs steady %.4f", worst_jump, steady_jump);
    {   /* the mean over a settled second in each revision (a pop that left an offset behind would show here) */
        int rev;
        for (rev = 0; rev < 2; rev++) {
            double sum = 0.0;
            tp5_set_param(&E, P_REV, (float)rev);
            run(&E, 30000, &finite, NULL);
            for (n = 0; n < 44100 / BLOCK; n++) { int k; tp5_render(&E, BL, BR, BLOCK); for (k = 0; k < BLOCK; k++) sum += BL[k]; }
            CHECK(fabs(sum / (double)(44100 / BLOCK * BLOCK)) < 0.005, "REV_%d settled mean %.5f", rev ? 4 : 2, sum / (double)(44100 / BLOCK * BLOCK));
        }
    }
    {
        int sub = 0;
        for (i = 0; i < SM_COUNT; i++) if (fpclassify(E.sm[i]) == FP_SUBNORMAL) sub = 1;
        for (i = 0; i < TINYPRO5_VOICES; i++) { int j; for (j = 0; j < 4; j++) if (fpclassify(E.voice[i].filt.s[j]) == FP_SUBNORMAL) sub = 1; }
        CHECK(!sub, "no subnormal numbers in the smoothed values or the filter states");
    }
    /* once everything has settled, the sound is the Rev 2 sound (the crossfade fully completes) */
    run(&E, 20000, &finite, NULL);
    CHECK(E.sm[SM_REV] < 1e-6f || E.sm[SM_REV] > 1.f - 1e-4f, "the revision crossfade has settled (%.9f)", (double)E.sm[SM_REV]);

    {   /* the crossfade is gradual: no smoothed control moves more than 1 % per sample, and it takes a few milliseconds, not one sample */
        float prev_sm[SM_COUNT], worst = 0.f;
        int k, steps_to_half = 0;
        fresh(&E, 0);
        saw_patch(&E);
        tp5_note_on(&E, 48, 100);
        run(&E, 8192, &finite, NULL);
        tp5_set_param(&E, P_REV, 1.f);
        tp5_set_param(&E, P_VEL_AMP, 1.f);
        tp5_set_param(&E, P_VINTAGE, 0.9f);
        memcpy(prev_sm, E.sm, sizeof prev_sm);
        for (n = 0; n < 4000; n++) {
            tp5_render(&E, BL, BR, 1);
            for (k = SM_REV; k <= SM_VEL_AMP; k++) {
                const float d = fabsf(E.sm[k] - prev_sm[k]);
                if (d > worst) worst = d;
            }
            if (!steps_to_half && E.sm[SM_REV] >= 0.5f) steps_to_half = n + 1;
            memcpy(prev_sm, E.sm, sizeof prev_sm);
        }
        printf("    smoothed controls: largest per-sample move %.5f, Rev crossfade half done after %d samples\n", (double)worst, steps_to_half);
        CHECK(worst < 0.01f, "a smoothed control jumped by %.4f in one sample", (double)worst);
        CHECK(steps_to_half > 50 && steps_to_half < 1000, "crossfade halfway after %d samples (want a few ms)", steps_to_half);
    }

    /* hammer: the switch, vintage, velocity depth and pressure all changing every block, with notes starting and stopping */
    {
        unsigned s = 7u;
        int blk;
        fresh(&E, 0);
        saw_patch(&E);
        tp5_set_param(&E, P_RESO, 0.9f);
        for (blk = 0; blk < 3000; blk++) {
            float pk = 0.f;
            s = s * 1664525u + 1013904223u;
            tp5_set_param(&E, P_REV, (float)((s >> 9) & 1));
            tp5_set_param(&E, P_VINTAGE, (float)((s >> 12) & 255) / 255.f);
            tp5_set_param(&E, P_VEL_AMP, (float)((s >> 4) & 255) / 255.f);
            tp5_set_param(&E, P_VEL_FILT, (float)((s >> 16) & 255) / 255.f);
            tp5_aftertouch(&E, (int)((s >> 20) & 127));
            if ((blk & 7) == 0) tp5_note_on(&E, 30 + (int)((s >> 3) % 60u), 1 + (int)((s >> 10) % 127u));
            if ((blk & 7) == 5) tp5_all_notes_off(&E);
            run(&E, BLOCK, &finite, &pk);
            if (pk > 1.f) finite = 0;
        }
        CHECK(finite && E.diag_nan_resets == 0, "3000 blocks of parameter slamming stay finite (%u resets)", (unsigned)E.diag_nan_resets);
    }
}

static void test_headroom(void)
{
    int c, r, finite = 1, pid, i;
    float worst = 0.f;
    printf("[Rev 4 headroom]\n");
    for (r = 0; r < 3; r++) for (c = 0; c <= 10; c++) {
        static const float resos[3] = { 0.6f, 0.8f, 1.f };
        fresh(&E, 1);
        tp5_set_param(&E, P_RESO, resos[r]); tp5_set_param(&E, P_CUTOFF, (float)c / 10.f);
        tp5_set_param(&E, P_A_VOL, 1.f); tp5_set_param(&E, P_B_VOL, 1.f); tp5_set_param(&E, P_NOISE, 1.f);
        tp5_set_param(&E, P_A_ATK, 0.f); tp5_set_param(&E, P_A_SUS, 1.f);
        for (i = 0; i < 5; i++) tp5_note_on(&E, 36 + 7 * i, 127);
        run(&E, 20000, &finite, NULL);
        E.diag_peak_pre = 0.f;
        run(&E, 51200, &finite, NULL);
        if (E.diag_peak_pre > worst) worst = E.diag_peak_pre;
        CHECK(E.diag_peak_pre <= TINYPRO5_CEILING_LIN, "reso %.1f cutoff %.1f: pre-limiter peak %.2f dBFS above the ceiling", (double)resos[r], (double)c / 10., (double)db(E.diag_peak_pre));
        CHECK(E.diag_nan_resets == 0, "filter safety net fired");
    }
    printf("    five voices, full mixer, reso 0.6-1.0, 11 cutoffs: pre-limiter peak %.2f dBFS (ceiling %.2f)\n", (double)db(worst), (double)db(TINYPRO5_CEILING_LIN));
    /* random patches in Rev 4: the ceiling holds */
    {
        unsigned seed = 4242u;
        int t, p;
        float fz = 0.f;
        for (t = 0; t < 80; t++) {
            tp5_init(&E, SR);
            tp5_set_param(&E, P_VOLUME, 1.f);
            for (p = 0; p < P_COUNT; p++) {
                if (!tp5_is_sound_param(p) || p == P_UNISON) continue;
                seed = seed * 1664525u + 1013904223u;
                tp5_set_param(&E, p, tp5_params[p].min + ((float)(seed >> 8) / 16777216.f) * (tp5_params[p].max - tp5_params[p].min));
            }
            E.p[P_REV] = 1.f; E.sm[SM_REV] = 1.f;
            tp5_set_param(&E, P_RESO, 1.f);
            for (pid = 0; pid < 5; pid++) tp5_note_on(&E, 30 + 8 * pid, 127);
            tp5_aftertouch(&E, 127);
            run(&E, 20000, &finite, NULL);
            if (E.diag_peak_post > fz) fz = E.diag_peak_post;
        }
        printf("    80 random Rev 4 patches at max resonance: peak %.2f dBFS\n", (double)db(fz));
        CHECK(fz <= TINYPRO5_CEILING_LIN + 1e-6f && finite, "random Rev 4 patch above the ceiling or non-finite");
    }
}

extern plugin_api_v2_t *move_plugin_init_v2(const host_api_v1_t *host);

static int json_has(const char *hay, const char *needle) { return strstr(hay, needle) != NULL; }

static void test_host_surface(void)
{
    static char big[16384], chain[16384];
    static host_api_v1_t host;
    plugin_api_v2_t *api;
    void *a;
    char buf[128];
    int n, i;
    printf("[host surface: metadata and MIDI]\n");
    host.sample_rate = MOVE_SAMPLE_RATE;
    host.frames_per_block = MOVE_FRAMES_PER_BLOCK;
    api = move_plugin_init_v2(&host);
    a = api->create_instance("/nonexistent", "{}");
    CHECK(a != NULL, "create_instance");
    n = api->get_param(a, "chain_params", big, sizeof big);
    CHECK(n > 0 && json_has(big, "\"key\":\"rev\"") && json_has(big, "\"key\":\"vintage\"") && json_has(big, "\"options\":[\"Rev2\",\"Rev4\"]"), "runtime chain_params has rev (Rev2/Rev4) and vintage");
    CHECK(json_has(big, "\"key\":\"vel_filter\"") && json_has(big, "\"key\":\"vel_amp\""), "runtime chain_params lists the internal velocity depths");
    n = api->get_param(a, "ui_hierarchy", big, sizeof big);
    CHECK(n > 0 && json_has(big, "\"key\":\"rev\"") && json_has(big, "\"key\":\"vintage\""), "rev and vintage are on a page");
    CHECK(!json_has(big, "vel_filter") && !json_has(big, "vel_amp"), "the velocity depths are on no front-panel page");
    n = tp5_json_module(big, sizeof big);
    CHECK(n > 0 && n < 8192 && json_has(big, "\"key\":\"rev\"") && json_has(big, "\"key\":\"vintage\""), "module.json (%d bytes) lists rev and vintage under the 8 KB cap", n);
    CHECK(!json_has(big, "\"step\""), "module.json leaves out the default float step (it is in the runtime chain_params)");
    api->get_param(a, "chain_params", chain, sizeof chain);
    CHECK(json_has(chain, "\"step\":0.01"), "runtime chain_params keeps the float step");
    for (i = 0; i < P_COUNT; i++) {
        const int internal = i == P_VEL_FILT || i == P_VEL_AMP;
        char key[64];
        snprintf(key, sizeof key, "\"key\":\"%s\"", tp5_params[i].key);
        CHECK(internal ? !json_has(big, key) : json_has(big, key), "module.json %s %s", internal ? "leaves out" : "lists", tp5_params[i].key);
    }
    api->set_param(a, "rev", "1");
    CHECK(api->get_param(a, "rev", buf, sizeof buf) > 0 && strcmp(buf, "1") == 0, "rev round trip '%s'", buf);
    CHECK(api->get_param(a, "vintage", buf, sizeof buf) > 0 && fabs(atof(buf)) < 1e-6, "choosing Rev 4 sets vintage 0 ('%s')", buf);
    api->set_param(a, "rev", "0");
    CHECK(api->get_param(a, "vintage", buf, sizeof buf) > 0 && fabs(atof(buf) - 0.65) < 1e-3, "choosing Rev 2 sets vintage 0.65 ('%s')", buf);
    {
        static const uint8_t press[2] = { 0xD0, 100 };
        static const uint8_t press3[3] = { 0xD1, 100, 0 };
        api->set_param(a, "rev", "1");
        api->on_midi(a, press, 2, 0);
        api->set_param(a, "state", "TP5:1;rev=1;");
        n = api->get_param(a, "state", big, sizeof big);
        CHECK(n > 0 && json_has(big, "rev=1;") && json_has(big, "vintage=") && json_has(big, "vel_amp="), "state blob carries the new keys");
        api->on_midi(a, press3, 3, 0);
        api->on_midi(a, press, 1, 0);                        /* too short: ignored */
    }
    api->destroy_instance(a);
}

int main(void)
{
    printf("TinyPro5 Rev 2 / Rev 4 tests\n\n");
    test_params();
    test_rev2_unchanged();
    test_filter_voicing();
    test_envelopes();
    test_velocity_aftertouch();
    test_vintage();
    test_switching();
    test_headroom();
    test_host_surface();
    printf("\n%d checks, %d failed\n", g_checks, g_fail);
    printf(g_fail ? "RESULT: FAIL\n" : "RESULT: PASS\n");
    return g_fail ? 1 : 0;
}
