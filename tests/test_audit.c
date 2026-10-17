/*
 * Factory-bank audit: renders all 32 programs of the compiled-in bank (src/presets_factory.c) and checks that none is silent or
 * exceptionally quiet, that the bank sits within a few dB of one loudness, that nothing passes the output ceiling, and that the
 * programs that depend on special engine behaviour (self-oscillating filter, Osc B as a modulator or LFO, hard sync) really are
 * wired that way. Run from the repo root (scripts/test.sh does).
 *
 *   1. Audio: middle C, velocity 100, held 6 s then released for 3 s. No program is silent (peak < -80 dBFS) or exceptionally
 *      quiet (peak < -38 dBFS).
 *   2. Levelling: a three-note chord at the default volume. Every program is within LEVEL_MAX_DB of the median loudest-100-ms
 *      RMS, most within LEVEL_BAND_DB, and none passes the -6.1 dBFS ceiling.
 *   3. Structure: a program with an empty oscillator mixer is acceptable only as a filter-oscillator patch (resonance at the top);
 *      a program that uses Osc B only as a modulator or LFO really sends it somewhere; hard-sync programs have Osc B in the
 *      keyboard-tracked audio range.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/bank.h"
#include "../src/tinypro5.h"

static int g_checks, g_fail;
#define CHECK(cond, ...) do { g_checks++; if (!(cond)) { g_fail++; printf("    FAIL line %d: ", __LINE__); \
    printf(__VA_ARGS__); printf("\n"); } } while (0)

#define N TINYPRO5_PRESET_COUNT
#define SR 44100.f
#define LEVEL_BAND_DB 4.0
#define LEVEL_MAX_DB  7.0
#define LEVEL_NEAR_MIN 26
#define LOUD_WIN      1323        /* 30 ms: short enough that a plucked program is judged by its attack, not diluted by its decay */
static double  g_peak_db[N], g_rms_db[N];
static tp5_engine E;
static float BL[128], BR[128];

static float eff(int slot, int id)       /* effective parameter of a factory program: engine default, then its list */
{
    const tp5_kv *kv;
    const int n = tp5_bank_preset_kv(0, slot, &kv);
    float v = tp5_params[id].def;
    int i;
    for (i = 0; i < n; i++) if (kv[i].id == id) v = kv[i].v;
    return v < tp5_params[id].min ? tp5_params[id].min : (v > tp5_params[id].max ? tp5_params[id].max : v);
}

static int cmp_d(const void *a, const void *b) { const double x = *(const double *)a, y = *(const double *)b; return x < y ? -1 : x > y; }

static void render_program(int slot)
{
    int i, n, k, win = 0;
    double peak = 0.0, best_win = 0.0, acc = 0.0;
    tp5_init(&E, SR);
    tp5_set_param(&E, P_VOLUME, 1.f);
    tp5_load_preset(&E, slot);
    /* let the parameter smoothers settle before the key goes down: the player picks a program, then plays. Without this, the old
     * program's mixer levels gliding to zero would leak into a filter that is meant to be driven by nothing. */
    for (i = 0; i < (int)(0.3f * SR / 128.f); i++) tp5_render(&E, BL, BR, 128);
    tp5_note_on(&E, 60, 100);                                  /* middle C, standard velocity */
    for (i = 0; i < 2; i++) {
        const int blocks = (int)((i == 0 ? 6.0f : 3.0f) * SR / 128.f);
        if (i == 1) tp5_note_off(&E, 60);
        for (n = 0; n < blocks; n++) {
            tp5_render(&E, BL, BR, 128);
            for (k = 0; k < 128; k++) {
                const double v = 0.5 * ((double)BL[k] + (double)BR[k]);
                if (!isfinite(v)) { peak = 1e9; continue; }
                if (fabs(v) > peak) peak = fabs(v);
                acc += v * v;
                if (++win == 4410) {                            /* loudest 100 ms RMS */
                    if (sqrt(acc / win) > best_win) best_win = sqrt(acc / win);
                    acc = 0.0; win = 0;
                }
            }
        }
    }
    g_peak_db[slot] = 20.0 * log10(peak + 1e-12);
    g_rms_db[slot] = 20.0 * log10(best_win + 1e-12);
}

/* A three-note velocity-100 chord at the default volume: the level a player hears. Fills the peak and the loudest-100ms rms. */
static void chord_level(int slot, double *peak_db, double *rms_db)
{
    static const int notes[3] = { 48, 52, 55 };
    int i, n, k, win = 0;
    double peak = 0.0, best = 0.0, acc = 0.0;
    tp5_init(&E, SR);
    tp5_load_preset(&E, slot);
    for (i = 0; i < (int)(0.3f * SR / 128.f); i++) tp5_render(&E, BL, BR, 128);
    for (i = 0; i < 3; i++) tp5_note_on(&E, notes[i], 100);
    for (n = 0; n < (int)(5.0f * SR / 128.f); n++) {
        tp5_render(&E, BL, BR, 128);
        for (k = 0; k < 128; k++) {
            const double l = (double)BL[k], r = (double)BR[k];
            if (fabs(l) > peak) peak = fabs(l);
            if (fabs(r) > peak) peak = fabs(r);
            acc += 0.5 * (l * l + r * r);
            if (++win == LOUD_WIN) { if (acc / win > best) best = acc / win; acc = 0.0; win = 0; }
        }
    }
    *peak_db = 20.0 * log10(peak + 1e-12);
    *rms_db = 10.0 * log10(best + 1e-15);
}

static void describe(int s)
{
    printf("      %d-%d %-20s peak %6.1f dBFS  loudest-100ms rms %6.1f dBFS\n", s / 8 + 1, s % 8 + 1, tp5_preset_name(s), g_peak_db[s], g_rms_db[s]);
    printf("        mixer  : A %.2f  B %.2f  noise %.2f | filter cutoff %.2f reso %.2f env %.2f kbd %.2f\n", (double)eff(s, P_A_VOL),
           (double)eff(s, P_B_VOL), (double)eff(s, P_NOISE), (double)eff(s, P_CUTOFF), (double)eff(s, P_RESO), (double)eff(s, P_ENVAMT), (double)eff(s, P_KBD));
    printf("        poly-mod: dest %d  env %.2f  oscB %.2f   sync %d  osc B as LFO %d\n", (int)eff(s, P_PM_DEST), (double)eff(s, P_PM_FENV), (double)eff(s, P_PM_OSCB),
           (int)eff(s, P_SYNC), (int)eff(s, P_B_LFO));
}

int main(void)
{
    int s, mixer_off = 0;
    double sorted[N], median;
    printf("factory bank audit\n");
    CHECK(tp5_bank_size(0) == N && N == 32, "the factory bank must hold 32 programs (found %d)", tp5_bank_size(0));

    /* 1. audio */
    printf("  rendering %d programs (middle C, velocity 100, 6 s held + 3 s release)...\n", N);
    for (s = 0; s < N; s++) { render_program(s); sorted[s] = g_peak_db[s]; }
    qsort(sorted, N, sizeof *sorted, cmp_d);
    median = 0.5 * (sorted[N / 2 - 1] + sorted[N / 2]);
    printf("  peak (dBFS): quietest %.1f, median %.1f, loudest %.1f\n", sorted[0], median, sorted[N - 1]);
    for (s = 0; s < N; s++) {
        CHECK(g_peak_db[s] > -80.0, "%s is SILENT (peak %.1f dBFS)", tp5_preset_name(s), g_peak_db[s]);
        CHECK(g_peak_db[s] > -38.0, "%s is exceptionally quiet (peak %.1f dBFS)", tp5_preset_name(s), g_peak_db[s]);
        CHECK(g_peak_db[s] <= 20.0 * log10((double)TINYPRO5_CEILING_LIN) + 0.01, "%s passes the ceiling (%.2f dBFS)", tp5_preset_name(s), g_peak_db[s]);
    }

    /* 2. levelling: a three-note chord at the default volume */
    {
        double pk[N], rm[N], srt[N], med, worst_pk = -200.0, worst_dev = 0.0;
        int near = 0;
        for (s = 0; s < N; s++) {
            chord_level(s, &pk[s], &rm[s]);
            srt[s] = rm[s];
            if (pk[s] > worst_pk) worst_pk = pk[s];
        }
        qsort(srt, N, sizeof *srt, cmp_d);
        med = 0.5 * (srt[N / 2 - 1] + srt[N / 2]);
        for (s = 0; s < N; s++) {
            if (fabs(rm[s] - med) <= LEVEL_BAND_DB) near++;
            if (fabs(rm[s] - med) > worst_dev) worst_dev = fabs(rm[s] - med);
        }
        printf("  three-note chord at default volume: loudest-100ms rms quietest %.1f, median %.1f, loudest %.1f dBFS; peak <= %.1f dBFS; %d/%d within %.0f dB of the median, worst %.1f dB\n",
               srt[0], med, srt[N - 1], worst_pk, near, N, LEVEL_BAND_DB, worst_dev);
        CHECK(worst_pk <= 20.0 * log10((double)TINYPRO5_CEILING_LIN) + 0.01, "a chord passes the -6.1 dBFS ceiling (%.2f dBFS)", worst_pk);
        CHECK(near >= LEVEL_NEAR_MIN, "only %d of %d programs are within %.0f dB of the median chord loudness", near, N, LEVEL_BAND_DB);
        for (s = 0; s < N; s++)
            CHECK(fabs(rm[s] - med) <= LEVEL_MAX_DB, "%s is %.1f dB from the median chord loudness (limit %.0f)", tp5_preset_name(s), rm[s] - med, LEVEL_MAX_DB);
        for (s = 0; s < N; s++)
            if (fabs(rm[s] - med) > LEVEL_BAND_DB || getenv("TP5_AUDIT_VERBOSE"))
                printf("    %s %d-%d %-20s chord rms %6.1f dBFS (%+.1f dB), peak %6.1f dBFS\n", fabs(rm[s] - med) > LEVEL_BAND_DB ? "outside band:" : "             ", s / 8 + 1, s % 8 + 1,
                       tp5_preset_name(s), rm[s], rm[s] - med, pk[s]);
    }

    /* 3. structure */
    for (s = 0; s < N; s++) {
        const int mixer_is_off = eff(s, P_A_VOL) == 0.f && eff(s, P_B_VOL) == 0.f;
        const int b_modulates = eff(s, P_PM_OSCB) > 0.f && ((int)eff(s, P_PM_DEST) != 0);
        if (mixer_is_off && eff(s, P_NOISE) == 0.f) {
            mixer_off++;
            CHECK(eff(s, P_RESO) >= 0.9f, "%s: empty mixer without a self-oscillating filter", tp5_preset_name(s));
        }
        if (eff(s, P_SYNC) > 0.5f)
            CHECK(eff(s, P_B_KBD) > 0.5f && eff(s, P_B_LFO) < 0.5f, "%s: hard sync needs Osc B in the keyboard-tracked audio range", tp5_preset_name(s));
        if (eff(s, P_B_LFO) > 0.5f)
            CHECK(b_modulates, "%s: Osc B runs as an LFO but modulates nothing", tp5_preset_name(s));
    }
    for (s = 0; s < N; s++) {
        const float g = tp5_bank_gain(0, s);
        CHECK(g >= 1.f && g <= 8.001f, "%s: gain %.3f outside 1..8", tp5_preset_name(s), (double)g);
    }
    CHECK(tp5_bank_gain(1, 0) == 1.f && tp5_bank_gain(0, -1) == 1.f && tp5_bank_gain(0, 999) == 1.f, "gain is 1 outside the factory bank");
    puts("  quietest programs by single-note peak (information):");
    {
        int shown = 0, pick;
        char used[N];
        memset(used, 0, sizeof used);
        while (shown < 4) {
            int best = -1;
            for (pick = 0; pick < N; pick++) if (!used[pick] && (best < 0 || g_peak_db[pick] < g_peak_db[best])) best = pick;
            used[best] = 1;
            describe(best);
            shown++;
        }
    }
    CHECK(mixer_off <= 1, "%d programs have an empty mixer (expected only the self-oscillating sweep)", mixer_off);
    printf("    %d checks, %d failed\n    RESULT: %s\n", g_checks, g_fail, g_fail ? "FAIL" : "PASS");
    return g_fail ? 1 : 0;
}
