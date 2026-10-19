/*
 * Rev 2 vs Rev 4: measures how different the two voicings really are, and writes audition files.
 *   rev_compare [out_dir]        (default build/rev_compare; the folder must exist)
 * Everything runs offline on the engine; no third-party data. Output: a text report on stdout and
 *   test_rev2.wav, test_rev4.wav     the same two-chord phrase in each voicing (vintage 0 in both, so only the voicing differs)
 *   rev_compare.wav                  Rev 2, Rev 4, Rev 2, Rev 4 (2 s phrases)
 *   rev_compare_switch.wav           the same, but each phrase set up the way the Rev switch does it (Rev 2 also gets vintage 0.65)
 *   pad_pressure.wav                 two held notes with polyphonic pressure rising 0 -> 127 over 3 s: 4 s in Rev 2, then 4 s in Rev 4
 *   velocity_response.wav            note 48 at velocity 30, 64, 100, 127 (1 s each) through four velocity curves (A-D, see report_velocity_audit)
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/bank.h"

#define SR 44100.f
#define NFFT 32768

static tp5_engine E;
static float BL[128], BR[128];

/* ---- analysis ------------------------------------------------------------------------------------------------------ */
static void fft(float *re, float *im, int n)
{
    int i, j = 0, len;
    for (i = 1; i < n; i++) {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) { float t = re[i]; re[i] = re[j]; re[j] = t; t = im[i]; im[i] = im[j]; im[j] = t; }
    }
    for (len = 2; len <= n; len <<= 1) {
        const double ang = -6.283185307179586 / len;
        const float wr = (float)cos(ang), wi = (float)sin(ang);
        for (i = 0; i < n; i += len) {
            float cr = 1.f, ci = 0.f;
            for (j = 0; j < len / 2; j++) {
                const float ur = re[i + j], ui = im[i + j];
                const float vr = re[i + j + len / 2] * cr - im[i + j + len / 2] * ci;
                const float vi = re[i + j + len / 2] * ci + im[i + j + len / 2] * cr;
                re[i + j] = ur + vr; im[i + j] = ui + vi;
                re[i + j + len / 2] = ur - vr; im[i + j + len / 2] = ui - vi;
                { const float nr = cr * wr - ci * wi; ci = cr * wi + ci * wr; cr = nr; }
            }
        }
    }
}

typedef struct { double rms_db, low_db, high_db, centroid; } spec;   /* high_db: power above 500 Hz, in the same units as rms_db */

/* Hann-windowed spectrum of the last NFFT samples of x: total rms (dBFS), power below `low_hz` (dB, same reference), spectral centroid (Hz) */
static spec analyse(const float *x, int n, float low_hz)
{
    static float re[NFFT], im[NFFT];
    spec s;
    double tot = 0.0, low = 0.0, high = 0.0, cen = 0.0, ms = 0.0;
    int i;
    const float *p = x + (n - NFFT);
    for (i = 0; i < NFFT; i++) {
        const float w = 0.5f - 0.5f * cosf(6.2831853f * (float)i / (float)NFFT);
        re[i] = p[i] * w; im[i] = 0.f;
        ms += (double)p[i] * p[i];
    }
    fft(re, im, NFFT);
    for (i = 1; i < NFFT / 2; i++) {
        const double pw = (double)re[i] * re[i] + (double)im[i] * im[i], hz = (double)i * SR / NFFT;
        tot += pw; cen += pw * hz;
        if (hz < low_hz) low += pw;
        if (hz > 500.0) high += pw;
    }
    s.rms_db = 10.0 * log10(ms / NFFT + 1e-20);
    s.low_db = tot > 0.0 ? 10.0 * log10(low / tot * (ms / NFFT) + 1e-20) : -200.0;     /* bass power in the same units as the rms */
    s.high_db = tot > 0.0 ? 10.0 * log10(high / tot * (ms / NFFT) + 1e-20) : -200.0;
    s.centroid = tot > 0.0 ? cen / tot : 0.0;
    return s;
}

/* ---- setup ----------------------------------------------------------------------------------------------------------- */
static void defaults(int rev, float vintage)
{
    int i;
    tp5_init(&E, SR);
    for (i = 0; i < P_COUNT; i++) if (tp5_is_sound_param(i)) tp5_set_param(&E, i, tp5_params[i].def);
    tp5_set_param(&E, P_VOLUME, 1.f);
    tp5_set_param(&E, P_DRIFT, 0.f);
    E.p[P_REV] = (float)rev;                 /* chosen directly: no side effects, so only the voicing differs */
    E.p[P_VINTAGE] = vintage;
    E.sm[SM_REV] = (float)rev;
    E.sm[SM_VINTAGE] = vintage;
}

static void render(int frames, float *out)
{
    int done = 0;
    while (done < frames) {
        const int n = frames - done > 128 ? 128 : frames - done;
        tp5_render(&E, BL, BR, n);
        if (out) memcpy(out + done, BL, (size_t)n * sizeof(float));
        done += n;
    }
}

/* knob value for a -3 dB cutoff of `hz` with no key tracking and no envelope (src/tinypro5.c: 20 Hz * 2^(10 c - 1.2)) */
static float knob_for_hz(float hz) { return (log2f(hz / 20.f) + 1.2f) / 10.f; }

/* ---- 1. resonance roll-off -------------------------------------------------------------------------------------------- */
static spec bass_case(int rev, float vintage, int note, float reso, float cutoff_hz)
{
    static float buf[44100 * 3];
    defaults(rev, vintage);
    tp5_set_param(&E, P_A_VOL, 1.f); tp5_set_param(&E, P_A_WAVE, 0.f);
    tp5_set_param(&E, P_KBD, 0.f); tp5_set_param(&E, P_ENVAMT, 0.f);
    tp5_set_param(&E, P_CUTOFF, knob_for_hz(cutoff_hz)); tp5_set_param(&E, P_RESO, reso);
    tp5_set_param(&E, P_A_ATK, 0.f); tp5_set_param(&E, P_A_SUS, 1.f);
    tp5_note_on(&E, note, 100);
    render(44100 * 3, buf);
    return analyse(buf, 44100 * 3, 250.f);
}

static void report_rolloff(void)
{
    static const float resos[5] = { 0.f, 0.4f, 0.6f, 0.8f, 1.f };
    static const int notes[2] = { 36, 48 };
    int i, n;
    printf("== 1. resonance roll-off: saw, cutoff 1 kHz, no key tracking, no envelope\n");
    printf("   power below 250 Hz (dB re full scale, Hann FFT of the last %d samples), and the whole signal's rms\n", NFFT);
    printf("   note  reso   Rev2 <250Hz   Rev4 <250Hz   delta     Rev2 rms   Rev4 rms   delta   centroid Rev2 -> Rev4\n");
    for (n = 0; n < 2; n++)
        for (i = 0; i < 5; i++) {
            const spec a = bass_case(0, 0.f, notes[n], resos[i], 1000.f), b = bass_case(1, 0.f, notes[n], resos[i], 1000.f);
            printf("   %3d   %.1f   %9.2f     %9.2f   %+6.2f   %9.2f  %9.2f  %+6.2f   %6.0f -> %6.0f Hz\n",
                   notes[n], (double)resos[i], a.low_db, b.low_db, b.low_db - a.low_db, a.rms_db, b.rms_db, b.rms_db - a.rms_db, a.centroid, b.centroid);
        }
    {   /* what the Rev switch itself does: Rev 2 also gets vintage 0.65 */
        const spec a = bass_case(0, 0.65f, 36, 0.8f, 1000.f), b = bass_case(1, 0.f, 36, 0.8f, 1000.f);
        printf("   note 36, reso 0.8 with the switch's vintage (Rev2 0.65, Rev4 0): <250 Hz %+.2f dB\n", b.low_db - a.low_db);
    }
}

/* ---- 2. transient / envelope attack ----------------------------------------------------------------------------------- */
static void transient_case(int rev, float knob, int *amp_atk, int *filt_atk, int *t_peak, int *t90, float *slew, float *peak)
{
    static float y[44100 / 2];
    int n, i;
    float pk = 0.f, run = 0.f;
    defaults(rev, 0.f);
    tp5_set_param(&E, P_A_VOL, 1.f);
    tp5_set_param(&E, P_CUTOFF, 0.12f); tp5_set_param(&E, P_RESO, 0.3f); tp5_set_param(&E, P_ENVAMT, 1.f); tp5_set_param(&E, P_KBD, 0.f);
    tp5_set_param(&E, P_F_ATK, knob); tp5_set_param(&E, P_F_DEC, 0.45f); tp5_set_param(&E, P_F_SUS, 0.f);
    tp5_set_param(&E, P_A_ATK, knob); tp5_set_param(&E, P_A_SUS, 1.f);
    render(8192, NULL);                       /* settle the revision crossfade first */
    tp5_note_on(&E, 48, 127);
    *amp_atk = *filt_atk = -1;
    for (n = 0; n < 44100 / 2; n += 1) {
        tp5_render(&E, BL, BR, 1);
        y[n] = BL[0];
        if (*amp_atk < 0 && E.voice[0].a_stage != ENV_ATK) *amp_atk = n + 1;
        if (*filt_atk < 0 && E.voice[0].f_stage != ENV_ATK) *filt_atk = n + 1;
    }
    for (i = 0; i < 44100 / 2; i++) if (fabsf(y[i]) > pk) { pk = fabsf(y[i]); *t_peak = i; }
    *peak = pk;
    *slew = 0.f;
    for (i = 1; i < 44100 / 2; i++) { const float d = fabsf(y[i] - y[i - 1]); if (d > *slew) *slew = d; }
    *t90 = -1;
    for (i = 0; i < 44100 / 2; i++) { if (fabsf(y[i]) > run) run = fabsf(y[i]); if (run >= 0.9f * pk) { *t90 = i; break; } }
}

static void report_transient(void)
{
    static const float knobs[4] = { 0.f, 0.2f, 0.4f, 0.6f };
    int k;
    printf("\n== 2. attack: saw, closed filter opened by a 100%% filter envelope, note 48 vel 127; both envelope attack knobs as listed\n");
    printf("   knob   amp ramp (samples / ms)   filter ramp (samples)   output peak at (samples)   90%% of peak at   max slew |dy|   peak\n");
    for (k = 0; k < 4; k++) {
        int a2, f2, p2, n2, a4, f4, p4, n4;
        float s2, s4, k2, k4;
        transient_case(0, knobs[k], &a2, &f2, &p2, &n2, &s2, &k2);
        transient_case(1, knobs[k], &a4, &f4, &p4, &n4, &s4, &k4);
        printf("   %.1f    Rev2 %6d / %6.1f          Rev2 %6d             Rev2 %6d             Rev2 %6d        Rev2 %.4f    %.3f\n", (double)knobs[k], a2, a2 * 1000.0 / SR, f2, p2, n2, (double)s2, (double)k2);
        printf("        Rev4 %6d / %6.1f          Rev4 %6d             Rev4 %6d             Rev4 %6d        Rev4 %.4f    %.3f\n", a4, a4 * 1000.0 / SR, f4, p4, n4, (double)s4, (double)k4);
    }
}

/* ---- 3. velocity ------------------------------------------------------------------------------------------------------ */
static spec velocity_case(int mode, int vel, int preset)
{
    static float buf[44100 * 2];
    tp5_init(&E, SR);
    tp5_load_preset(&E, preset);
    tp5_set_param(&E, P_VOLUME, 1.f);
    if (mode == 1) tp5_set_param(&E, P_REV, 1.f);                         /* the Rev switch: velocity depths from TP5_VEL_*_REV4_DEF */
    if (mode == 2) { E.p[P_REV] = 1.f; E.sm[SM_REV] = 1.f; }               /* a Rev 4 program that lists no velocity depth */
    if (mode == 3) { E.p[P_REV] = 1.f; E.sm[SM_REV] = 1.f; E.p[P_VEL_FILT] = 1.f; E.p[P_VEL_AMP] = 1.f; }
    tp5_note_on(&E, 48, vel);
    render(44100 * 2, buf);
    return analyse(buf, 44100 * 2, 250.f);
}

static void report_velocity(void)
{
    static const char *name[4] = { "factory program as loaded (Rev 2)", "Rev switch used: Rev 4, depths 0.70 / 0.60", "Rev 4, depths 0 / 0 (nothing listed)", "Rev 4, depths 1 / 1" };
    int m;
    const int preset = 0;
    printf("\n== 3. velocity: factory program \"%s\" note 48, vel 32 vs vel 127\n", tp5_preset_name(preset));
    printf("   setup                                         level soft-hard (dB)   brightness soft-hard (centroid Hz)\n");
    for (m = 0; m < 4; m++) {
        const spec s = velocity_case(m, 32, preset), h = velocity_case(m, 127, preset);
        printf("   %-45s %+7.2f               %+8.0f\n", name[m], s.rms_db - h.rms_db, s.centroid - h.centroid);
    }
}

/* ---- 3b. external keyboard velocity curves ------------------------------------------------------------------------------ */
static const char *k_vcase_name[4] = {
    "A  Rev 2 (fixed 0.7 + 0.3 vel)", "B  Rev 4, earlier switch defaults (amp 0.30, filter 0.35)", "C  Rev 4, switch defaults now (amp 0.60, filter 0.70)", "D  Rev 4, maximum (amp 1.00, filter 1.00)" };

/* note 48, cutoff 0.3, filter envelope amount 0.8 (decay 0.5, sustain 0.2): the 0.74 s that start at note-on */
static void vcase_setup(int c)
{
    static const float amp[4] = { 0.f, 0.30f, 0.60f, 1.f }, filt[4] = { 0.f, 0.35f, 0.70f, 1.f };
    defaults(c > 0, 0.f);
    tp5_set_param(&E, P_A_VOL, 1.f);
    tp5_set_param(&E, P_CUTOFF, 0.3f); tp5_set_param(&E, P_RESO, 0.2f); tp5_set_param(&E, P_ENVAMT, 0.8f); tp5_set_param(&E, P_KBD, 0.5f);
    tp5_set_param(&E, P_F_ATK, 0.f); tp5_set_param(&E, P_F_DEC, 0.5f); tp5_set_param(&E, P_F_SUS, 0.2f);
    tp5_set_param(&E, P_A_ATK, 0.f); tp5_set_param(&E, P_A_SUS, 1.f);
    if (c > 0) { E.p[P_VEL_AMP] = amp[c]; E.p[P_VEL_FILT] = filt[c]; E.sm[SM_VEL_AMP] = amp[c]; E.sm[SM_VEL_FILT] = filt[c]; }
}

static void report_velocity_audit(void)
{
    static const int vels[4] = { 30, 64, 100, 127 };
    static float buf[NFFT];
    int c, v, i;
    printf("\n== 3b. external keyboard velocity: note 48, cutoff 0.3, filter envelope amount 0.8, first 0.74 s of the note\n");
    printf("   curve                                            vel   rms dBFS   >500 Hz dBFS   centroid Hz\n");
    for (c = 0; c < 4; c++) {
        spec s[4];
        for (v = 0; v < 4; v++) {
            vcase_setup(c);
            tp5_note_on(&E, 48, vels[v]);
            for (i = 0; i < NFFT; i += 128) { tp5_render(&E, BL, BR, 128); memcpy(buf + i, BL, 128 * sizeof(float)); }
            s[v] = analyse(buf, NFFT, 250.f);
            printf("   %-48s %3d   %8.2f   %12.2f   %9.0f\n", v == 0 ? k_vcase_name[c] : "", vels[v], s[v].rms_db, s[v].high_db, s[v].centroid);
        }
        printf("   %-48s 30 vs 127:  %+6.2f dB level,  %+6.1f dB above 500 Hz,  %+6.0f Hz centroid\n", "", s[0].rms_db - s[3].rms_db, s[0].high_db - s[3].high_db, s[0].centroid - s[3].centroid);
    }
}

/* ---- 4. the 32 factory programs, Rev 2 vs Rev 4 ------------------------------------------------------------------------ */
static void report_factory(void)
{
    static float a[44100 * 3], b[44100 * 3];
    int i, small = 0, n = TINYPRO5_PRESET_COUNT;
    double dmin = 1e9, dmax = -1e9, dsum = 0.0, cmin = 1e9, cmax = -1e9, csum = 0.0;
    static const int notes[3] = { 48, 55, 64 };
    printf("\n== 4. the 32 factory programs: Rev 2 as loaded vs the Rev switch set to Rev 4 (three-note chord, 3 s)\n");
    for (i = 0; i < n; i++) {
        int pass, k;
        spec sa, sb;
        double d, c;
        for (pass = 0; pass < 2; pass++) {
            tp5_init(&E, SR);
            tp5_load_preset(&E, i);
            tp5_set_param(&E, P_VOLUME, 1.f);
            if (pass) {          /* the switch, used before the notes: its vintage and velocity defaults apply, and the crossfade has settled */
                tp5_set_param(&E, P_REV, 1.f);
                E.sm[SM_REV] = 1.f; E.sm[SM_VINTAGE] = E.p[P_VINTAGE]; E.sm[SM_VEL_FILT] = E.p[P_VEL_FILT]; E.sm[SM_VEL_AMP] = E.p[P_VEL_AMP];
            }
            for (k = 0; k < 3; k++) tp5_note_on(&E, notes[k], 100);
            render(44100 * 3, pass ? b : a);
        }
        sa = analyse(a, 44100 * 3, 250.f); sb = analyse(b, 44100 * 3, 250.f);
        d = sb.rms_db - sa.rms_db; c = sb.centroid - sa.centroid;
        if (d < dmin) dmin = d;
        if (d > dmax) dmax = d;
        if (c < cmin) cmin = c;
        if (c > cmax) cmax = c;
        dsum += d; csum += c;
        if (fabs(d) < 1.0) small++;
        printf("   %2d %-22s level %+6.2f dB   <250 Hz %+6.2f dB   centroid %+7.0f Hz\n", i, tp5_preset_name(i), d, sb.low_db - sa.low_db, c);
    }
    printf("   level change: mean %+.2f dB, range %+.2f .. %+.2f; centroid change: mean %+.0f Hz, range %+.0f .. %+.0f; %d of %d within +-1 dB\n",
           dsum / n, dmin, dmax, csum / n, cmin, cmax, small, n);
}

/* ---- 5. aftertouch ------------------------------------------------------------------------------------------------------ */
static spec pressure_case(int rev, int kind, int amount)
{
    static float buf[44100 * 2];
    defaults(rev, 0.f);
    tp5_set_param(&E, P_A_VOL, 1.f); tp5_set_param(&E, P_CUTOFF, 0.3f); tp5_set_param(&E, P_RESO, 0.3f);
    tp5_set_param(&E, P_ENVAMT, 0.f); tp5_set_param(&E, P_A_ATK, 0.f); tp5_set_param(&E, P_A_SUS, 1.f);
    tp5_note_on(&E, 48, 100);
    if (kind == 0) { const uint8_t m[2] = { 0xD0, (uint8_t)amount }; tp5_midi(&E, m, 2); }          /* channel pressure */
    else { const uint8_t m[3] = { 0xA0, 48, (uint8_t)amount }; tp5_midi(&E, m, 3); }                /* polyphonic key pressure on the held note */
    render(44100 * 2, buf);
    return analyse(buf, 44100 * 2, 250.f);
}

static void report_pressure(void)
{
    static const int amounts[4] = { 0, 32, 64, 127 };
    int r, k, a;
    printf("\n== 5. aftertouch on a held note (saw, cutoff knob 0.3, reso 0.3): whole-signal level and the power above 500 Hz, vs pressure 0\n");
    for (r = 1; r >= 0; r--)
        for (k = 0; k < 2; k++) {
            const spec base = pressure_case(r, k, 0);
            printf("   Rev%d %-20s", r ? 4 : 2, k ? "polyphonic (0xA0)" : "channel (0xD0)");
            for (a = 1; a < 4; a++) {
                const spec s = pressure_case(r, k, amounts[a]);
                printf("   p%-3d level %+6.2f dB, >500 Hz %+6.1f dB", amounts[a], s.rms_db - base.rms_db, s.high_db - base.high_db);
            }
            printf("\n");
        }
}

/* ---- audition files ---------------------------------------------------------------------------------------------------- */
static void w32(FILE *f, uint32_t v) { fputc(v & 255, f); fputc((v >> 8) & 255, f); fputc((v >> 16) & 255, f); fputc((v >> 24) & 255, f); }
static void w16(FILE *f, uint16_t v) { fputc(v & 255, f); fputc((v >> 8) & 255, f); }

typedef struct { FILE *f; uint32_t frames; } wav;

static int wav_open(wav *w, const char *dir, const char *name)
{
    char path[512];
    snprintf(path, sizeof path, "%s/%s", dir, name);
    w->f = fopen(path, "wb");
    w->frames = 0;
    if (!w->f) { perror(path); return 0; }
    fwrite("RIFF", 1, 4, w->f); w32(w->f, 0); fwrite("WAVEfmt ", 1, 8, w->f);
    w32(w->f, 16); w16(w->f, 1); w16(w->f, 2); w32(w->f, 44100); w32(w->f, 44100u * 4u); w16(w->f, 4); w16(w->f, 16);
    fwrite("data", 1, 4, w->f); w32(w->f, 0);
    return 1;
}

static void wav_block(wav *w, int frames)
{
    static int16_t buf[128 * 2];
    tp5_render_i16(&E, buf, frames);
    fwrite(buf, 4, (size_t)frames, w->f);
    w->frames += (uint32_t)frames;
}

static void wav_close(wav *w, const char *dir, const char *name)
{
    fseek(w->f, 4, SEEK_SET); w32(w->f, 36u + w->frames * 4u);
    fseek(w->f, 40, SEEK_SET); w32(w->f, w->frames * 4u);
    fclose(w->f);
    printf("   %s/%s   %.1f s\n", dir, name, (double)w->frames / 44100.0);
}

/* the audition patch: low cutoff, high resonance, a filter envelope sweep, two saws, light poly-mod-free so only the voicing differs */
static void audition_patch(int rev, int as_switch)
{
    if (as_switch) { defaults(0, 0.f); tp5_set_param(&E, P_REV, (float)rev); E.sm[SM_REV] = (float)rev; E.sm[SM_VINTAGE] = E.p[P_VINTAGE]; E.sm[SM_VEL_FILT] = E.p[P_VEL_FILT]; E.sm[SM_VEL_AMP] = E.p[P_VEL_AMP]; }
    else defaults(rev, 0.f);
    tp5_set_param(&E, P_A_VOL, 1.f); tp5_set_param(&E, P_B_VOL, 0.8f); tp5_set_param(&E, P_B_WAVE, 0.f); tp5_set_param(&E, P_B_SEMI, 0.1f);
    tp5_set_param(&E, P_CUTOFF, 0.30f); tp5_set_param(&E, P_RESO, 0.75f); tp5_set_param(&E, P_ENVAMT, 0.55f); tp5_set_param(&E, P_KBD, 0.5f);
    tp5_set_param(&E, P_F_ATK, 0.12f); tp5_set_param(&E, P_F_DEC, 0.55f); tp5_set_param(&E, P_F_SUS, 0.25f); tp5_set_param(&E, P_F_REL, 0.4f);
    tp5_set_param(&E, P_A_ATK, 0.1f); tp5_set_param(&E, P_A_DEC, 0.6f); tp5_set_param(&E, P_A_SUS, 0.8f); tp5_set_param(&E, P_A_REL, 0.45f);
}

/* one 2 s phrase: a low fifth plus the octave pressed at t = 0 (velocity 100, then 70 for the top note), released at 1.2 s */
static void phrase(wav *w, int rev, int as_switch)
{
    static const int notes[3] = { 36, 43, 55 }, vels[3] = { 100, 100, 70 };
    int i, done = 0;
    audition_patch(rev, as_switch);
    for (i = 0; i < 3; i++) tp5_note_on(&E, notes[i], vels[i]);
    while (done < 2 * 44100) {
        if (done == (int)(1.2f * 44100.f) / 128 * 128) for (i = 0; i < 3; i++) tp5_note_off(&E, notes[i]);
        wav_block(w, 128);
        done += 128;
    }
}

static void write_files(const char *dir)
{
    wav w;
    int i;
    printf("\n== audition files\n");
    if (wav_open(&w, dir, "test_rev2.wav")) { phrase(&w, 0, 0); phrase(&w, 0, 0); wav_close(&w, dir, "test_rev2.wav"); }
    if (wav_open(&w, dir, "test_rev4.wav")) { phrase(&w, 1, 0); phrase(&w, 1, 0); wav_close(&w, dir, "test_rev4.wav"); }
    if (wav_open(&w, dir, "rev_compare.wav")) { for (i = 0; i < 4; i++) phrase(&w, i & 1, 0); wav_close(&w, dir, "rev_compare.wav"); }
    if (wav_open(&w, dir, "rev_compare_switch.wav")) { for (i = 0; i < 4; i++) phrase(&w, i & 1, 1); wav_close(&w, dir, "rev_compare_switch.wav"); }
    if (wav_open(&w, dir, "velocity_response.wav")) {
        static const int vels[4] = { 30, 64, 100, 127 };
        int c, v, done;
        for (c = 0; c < 4; c++)
            for (v = 0; v < 4; v++) {
                vcase_setup(c);
                tp5_note_on(&E, 48, vels[v]);
                for (done = 0; done < 44100; done += 128) {
                    if (done == (int)(0.6f * 44100.f) / 128 * 128) tp5_note_off(&E, 48);
                    wav_block(&w, 128);
                }
            }
        wav_close(&w, dir, "velocity_response.wav");
    }
    if (wav_open(&w, dir, "pad_pressure.wav")) {
        for (i = 0; i < 2; i++) {
            int done = 0, k;
            audition_patch(i, 1);
            tp5_set_param(&E, P_CUTOFF, 0.25f); tp5_set_param(&E, P_RESO, 0.4f); tp5_set_param(&E, P_ENVAMT, 0.f); tp5_set_param(&E, P_A_SUS, 1.f);
            tp5_note_on(&E, 48, 100); tp5_note_on(&E, 55, 100);
            while (done < 4 * 44100) {
                const int p = done < 3 * 44100 ? (int)(127.f * (float)done / (3.f * 44100.f)) : 127;
                static const int held[2] = { 48, 55 };
                for (k = 0; k < 2; k++) { const uint8_t m[3] = { 0xA0, (uint8_t)held[k], (uint8_t)p }; tp5_midi(&E, m, 3); }
                wav_block(&w, 128);
                done += 128;
            }
            tp5_all_notes_off(&E);
            wav_block(&w, 128);
        }
        wav_close(&w, dir, "pad_pressure.wav");
    }
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : "build/rev_compare";
    printf("TinyPro5 Rev 2 vs Rev 4 (not hardware-calibrated; measurements of this engine only)\n\n");
    report_rolloff();
    report_transient();
    report_velocity();
    report_velocity_audit();
    report_factory();
    report_pressure();
    write_files(dir);
    return 0;
}
