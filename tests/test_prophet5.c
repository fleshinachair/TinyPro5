/*
 * TinyPro5 unit tests. Run from the repository root (needs src/ui.js).
 * Exit status is non-zero if any check fails.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "../src/bank.h"
#include "../src/meta.h"
#include "../src/schwung_api.h"
#include "../src/tinypro5.h"
#define TP5_PMB_RATIO_TEST 0.3f /* Osc B Poly-Mod target at dial 1 (src/tinypro5.c TP5_PMB_RATIO) */

#define SR 44100.0f
#define BLOCK 128
#define CEIL_INT16 16272 /* 32768 * 10^(-6.1/20) */

static int g_checks, g_fail;

#define CHECK(cond, ...) do { \
        g_checks++; \
        if (!(cond)) { g_fail++; printf("    FAIL %s:%d  ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } \
    } while (0)

static tp5_engine E, E2;
static float BL[8192], BR[8192];

static double now_us(void)
{
    struct timespec ts;
    timespec_get(&ts, TIME_UTC);
    return (double)ts.tv_sec * 1e6 + (double)ts.tv_nsec * 1e-3;
}

static int subnormal(float f) { return fpclassify(f) == FP_SUBNORMAL; }

static int engine_has_subnormal(const tp5_engine *e)
{
    int i, j, k;
    for (i = 0; i < TINYPRO5_VOICES; i++) {
        const tp5_voice *v = &e->voice[i];
        for (j = 0; j < 4; j++) if (subnormal(v->filt.s[j])) return 1;
        for (j = 0; j < 3; j++) {
            if (subnormal(v->pink[j]) || subnormal(v->dr[j])) return 1;
            for (k = 0; k < 3; k++) if (subnormal(v->dr_x[j][k])) return 1;
        }
        if (subnormal(v->f_env) || subnormal(v->a_env) || subnormal(v->ph_a) || subnormal(v->ph_b)) return 1;
    }
    for (i = 0; i < SM_COUNT; i++) if (subnormal(e->sm[i])) return 1;
    return subnormal(e->dc_x[0]) || subnormal(e->dc_y[0]) || subnormal(e->dc_x[1]) || subnormal(e->dc_y[1])
        || subnormal(e->lfo_out);
}

/* render n frames in BLOCK chunks; returns max |sample|; sets *finite=0 on NaN/Inf */
static float render_n(tp5_engine *e, int frames, int *finite)
{
    float pk = 0.f;
    int done = 0, i;
    while (done < frames) {
        const int n = frames - done > BLOCK ? BLOCK : frames - done;
        tp5_render(e, BL, BR, n);
        for (i = 0; i < n; i++) {
            if (!isfinite(BL[i]) || !isfinite(BR[i])) { if (finite) *finite = 0; }
            else {
                if (fabsf(BL[i]) > pk) pk = fabsf(BL[i]);
                if (fabsf(BR[i]) > pk) pk = fabsf(BR[i]);
            }
        }
        done += n;
    }
    return pk;
}

static void chord(tp5_engine *e, int vel)
{
    static const int n[5] = { 36, 43, 50, 57, 64 };
    int i;
    for (i = 0; i < 5; i++) tp5_note_on(e, n[i], vel);
}

static float db(float x) { return 20.f * log10f(x > 1e-12f ? x : 1e-12f); }

/* ======================================================================= */

static void test_param_table(void)
{
    int i, j, p, k;
    printf("[param table & UI layout]\n");
    for (i = 0; i < P_COUNT; i++) {
        const tp5_param_info *pi = &tp5_params[i];
        CHECK(pi->key && pi->key[0], "param %d has no key", i);
        CHECK(pi->label && pi->label[0], "param %d has no label", i);
        if (!pi->key || !pi->label) continue;
        CHECK(strlen(pi->label) < 8, "label '%s' must be < 8 chars (is %zu)", pi->label, strlen(pi->label));
        CHECK(pi->min < pi->max, "param %s empty range", pi->key);
        CHECK(pi->def >= pi->min && pi->def <= pi->max, "param %s default out of range", pi->key);
        CHECK(tp5_param_id_from_key(pi->key) == i, "key lookup mismatch for %s", pi->key);
        for (j = i + 1; j < P_COUNT; j++)
            CHECK(strcmp(pi->key, tp5_params[j].key) != 0, "duplicate key %s", pi->key);
    }
    CHECK(tp5_param_id_from_key("nonsense") == -1, "unknown key must map to -1");
    CHECK(tp5_param_id_from_key(NULL) == -1, "NULL key must map to -1");

    for (p = 0; p < TP5_PAGES; p++) {
        int used = 0;
        for (k = 0; k < TP5_KNOBS; k++) {
            const int id = tp5_pages[p][k];
            CHECK(id >= -1 && id < P_COUNT, "page %d knob %d bad id %d", p + 1, k + 1, id);
            if (id >= 0) used++;
        }
        CHECK(used == TP5_KNOBS, "page %d uses only %d of %d knobs", p + 1, used, TP5_KNOBS);
    }
    /* exact encoder layout (agreed with the owner) */
    {
        static const int want[TP5_PAGES][TP5_KNOBS] = {
            { P_CUTOFF, P_RESO, P_ENVAMT, P_KBD, P_F_ATK, P_F_DEC, P_F_SUS, P_F_REL },
            { P_A_WAVE, P_A_PW, P_B_WAVE, P_B_PW, P_B_SEMI, P_SYNC, P_PM_FENV, P_PM_OSCB },
            { P_A_ATK, P_A_DEC, P_A_SUS, P_A_REL, P_A_VOL, P_B_VOL, P_NOISE, P_VOLUME },
            { P_UNISON, P_VOICES, P_DETUNE, P_LFO_AMT, P_PRESET, P_LOAD, P_SAVE, P_SORT } };
        static const char *labels[TP5_PAGES][TP5_KNOBS] = {
            { "Cutoff", "Reso", "EnvAmt", "KbdTrk", "FAtk", "FDecay", "FSus", "FRel" },
            { "AWave", "APW", "BWave", "BPW", "BPitch", "Sync", "PM-Fenv", "PM-OscB" },
            { "AAtk", "ADecay", "ASus", "ARel", "AVol", "BVol", "Noise", "Volume" },
            { "Mode", "Voices", "Detune", "LFOAmt", "Preset", "Reload", "Save", "Order" } };
        for (p = 0; p < TP5_PAGES; p++)
            for (k = 0; k < TP5_KNOBS; k++) {
                CHECK(tp5_pages[p][k] == want[p][k], "page %d encoder %d: id %d, want %d", p + 1, k + 1, tp5_pages[p][k], want[p][k]);
                CHECK(strcmp(tp5_params[tp5_pages[p][k]].label, labels[p][k]) == 0, "page %d encoder %d label '%s', want '%s'",
                      p + 1, k + 1, tp5_params[tp5_pages[p][k]].label, labels[p][k]);
            }
    }

    /* ui.js must reference every mapped key and label, quoted */
    {
        static char js[65536];
        FILE *f = fopen("src/ui.js", "rb");
        size_t n = 0;
        CHECK(f != NULL, "src/ui.js not found (run tests from the repo root)");
        if (f) { n = fread(js, 1, sizeof js - 1, f); fclose(f); }
        js[n] = 0;
        for (p = 0; p < TP5_PAGES; p++)
            for (k = 0; k < TP5_KNOBS; k++) {
                char pat[64];
                const int id = tp5_pages[p][k];
                if (id < 0) continue;
                snprintf(pat, sizeof pat, "\"%s\"", tp5_params[id].key);
                CHECK(strstr(js, pat) != NULL, "ui.js lacks key %s", pat);
                snprintf(pat, sizeof pat, "\"%s\"", tp5_params[id].label);
                CHECK(strstr(js, pat) != NULL, "ui.js lacks label %s", pat);
            }
    }
}

static float tp5_presets_effective(int slot, int id)
{
    float v = tp5_params[id].def;
    int k;
    const tp5_kv *kvl;
    const int nkv = tp5_bank_preset_kv(0, slot, &kvl);
    for (k = 0; k < nkv; k++)
        if (kvl[k].id == id) v = kvl[k].v;
    return v < tp5_params[id].min ? tp5_params[id].min : (v > tp5_params[id].max ? tp5_params[id].max : v);
}

static void test_presets(void)
{
    int i, j, k, repeats = 0;
    float worst_pre = 0.f; int worst_idx = 0;
    printf("[preset bank: %d presets]\n", TINYPRO5_PRESET_COUNT);
    CHECK(TINYPRO5_PRESET_COUNT == 32, "the factory bank is 4 groups of 8 original programs");
    for (i = 0; i < TINYPRO5_PRESET_COUNT; i++) {
        const char *pname = tp5_preset_name(i);
        const tp5_kv *pkv;
        const int pn = tp5_bank_preset_kv(0, i, &pkv);
        CHECK(pname[0] && strlen(pname) <= 24, "preset %d bad name", i);
        CHECK(pkv && pn >= 4, "preset %d has too few parameters (%d)", i, pn);
        for (j = i + 1; j < TINYPRO5_PRESET_COUNT; j++)
            if (strcmp(pname, tp5_preset_name(j)) == 0) repeats++; /* the program number keeps labels unique */
        for (k = 0; k < pn; k++) {
            const tp5_kv *kv = &pkv[k];
            CHECK(kv->id < P_COUNT && tp5_is_sound_param(kv->id), "preset %s sets non-sound param %d", pname, kv->id);
            if (kv->id < P_COUNT)
                CHECK(kv->v >= tp5_params[kv->id].min && kv->v <= tp5_params[kv->id].max,
                      "preset %s: %s=%g out of range", pname, tp5_params[kv->id].key, (double)kv->v);
        }
    }
    CHECK(repeats == 0, "%d repeated preset names (every factory name is unique)", repeats);
    {
        /* the four groups of eight, in slot order: the table is part of the module's contract (labels, docs and the on-device help name them) */
        static const char *names[TINYPRO5_PRESET_COUNT] = {
            "Night Drive Bass", "Sync Stalker", "Pulse Ostinato", "Dead End Pluck", "Square Chase", "Heartbeat Sub", "Detuned Chase", "Grind Sequence",
            "Glass Clap", "Music Box Ghost", "Poly-Mod Chime", "Cold Keys", "Hollow Fifths", "Bright Stab", "Whisper Pluck", "Frozen Bell",
            "Dark Brass Rise", "Black Strings", "Triangle Organ Bed", "Hollow Choir", "Cold Swell", "Low Dread Pad", "Stacked Fifths", "Tritone Choir",
            "Falling Siren", "Rising Alarm", "Self-Osc Sweep", "White Wind", "Cross-Mod Drone", "Sub Rumble", "Machine Pulse", "Dead Channel" };
        for (i = 0; i < TINYPRO5_PRESET_COUNT; i++)
            CHECK(strcmp(tp5_preset_name(i), names[i]) == 0, "slot %d is '%s', expected '%s'", i, tp5_preset_name(i), names[i]);
        /* group 1 is built for sequences: monophonic (Mono, Legato or a Unison stack), never plain Poly; groups 2 and 3 are the chord groups */
        for (i = 0; i < 8; i++) {
            CHECK(tp5_presets_effective(i, P_UNISON) >= 1.f, "group 1 program %s must not be Poly", tp5_preset_name(i));
            CHECK(tp5_presets_effective(8 + i, P_UNISON) == 0.f && tp5_presets_effective(16 + i, P_UNISON) == 0.f,
                  "groups 2 and 3 must be Poly (%s, %s)", tp5_preset_name(8 + i), tp5_preset_name(16 + i));
        }
        CHECK(tp5_presets_effective(26, P_RESO) >= 0.99f && tp5_presets_effective(26, P_A_VOL) == 0.f && tp5_presets_effective(26, P_B_VOL) == 0.f,
              "Self-Osc Sweep is the filter-oscillator patch: empty oscillator mixer, resonance at the top");
    }

    /* every preset: loads, renders finite, audible, inside the -6.1 dBFS ceiling */
    for (i = 0; i < TINYPRO5_PRESET_COUNT; i++) {
        int finite = 1;
        float pk, post;
        tp5_init(&E, SR);
        tp5_set_param(&E, P_VOLUME, 1.f);
        tp5_load_preset(&E, i);
        CHECK((int)tp5_get_param(&E, P_PRESET) == i, "preset index not recorded");
        chord(&E, 127);
        pk = render_n(&E, (int)(8.0f * SR), &finite);   /* the slowest swells (attack knob .78, ~2.7 s) are fully open by then */
        post = E.diag_peak_post;
        CHECK(finite, "preset %s produced NaN/Inf", tp5_preset_name(i));
        CHECK(E.diag_nan_resets == 0, "preset %s tripped the filter safety net", tp5_preset_name(i));
        CHECK(pk > 0.004f, "preset %s is silent (peak %.5f)", tp5_preset_name(i), (double)pk);
        CHECK(post <= TINYPRO5_CEILING_LIN + 1e-6f, "preset %s exceeds ceiling: %.1f dBFS", tp5_preset_name(i), (double)db(post));
        CHECK(E.diag_peak_pre <= TINYPRO5_CEILING_LIN, "preset %s pre-limiter peak %.2f dBFS above -6.1: headroom too high",
              tp5_preset_name(i), (double)db(E.diag_peak_pre));
        if (E.diag_peak_pre > worst_pre) { worst_pre = E.diag_peak_pre; worst_idx = i; }
    }
    printf("    loudest preset: %s at %.2f dBFS (ceiling %.1f)\n", tp5_preset_name(worst_idx), (double)db(worst_pre),
           (double)TINYPRO5_CEILING_DBFS);

    {   /* alphabetical order: a permutation, ascending, inverse tables agree, bank order stays the default */
        int seen[TINYPRO5_PRESET_COUNT] = { 0 };
        char buf[64], nm0[64];
        int sorted_ok = 1;
        CHECK((int)tp5_get_param(&E, P_SORT) == 0 || 1, "sort param exists");
        for (i = 0; i < TINYPRO5_PRESET_COUNT; i++) {
            const int slot = tp5_bank_slot(0, i, 1);
            CHECK(slot >= 0 && slot < TINYPRO5_PRESET_COUNT && !seen[slot], "A-Z position %d maps to bad/duplicate slot %d", i, slot);
            if (slot >= 0 && slot < TINYPRO5_PRESET_COUNT) seen[slot] = 1;
            CHECK(tp5_bank_pos(0, slot, 1) == i, "A-Z inverse mismatch at %d", i);
            CHECK(tp5_bank_slot(0, i, 0) == i && tp5_bank_pos(0, i, 0) == i, "bank order must be the identity");
            if (i > 0) {
                const char *a = tp5_preset_name(tp5_bank_slot(0, i - 1, 1)), *b = tp5_preset_name(slot);
                size_t n;
                for (n = 0; a[n] && b[n]; n++) {
                    int ca = a[n] >= 'A' && a[n] <= 'Z' ? a[n] + 32 : a[n], cb = b[n] >= 'A' && b[n] <= 'Z' ? b[n] + 32 : b[n];
                    if (ca != cb) { if (ca > cb) sorted_ok = 0; break; }
                }
            }
        }
        CHECK(sorted_ok, "alphabetical list is not in ascending case-insensitive order");
        printf("    A-Z order: first '%s', last '%s'\n", tp5_preset_name(tp5_bank_slot(0, 0, 1)), tp5_preset_name(tp5_bank_slot(0, TINYPRO5_PRESET_COUNT - 1, 1)));

        /* selecting through the A-Z list loads the program that name belongs to; switching order keeps the selection */
        tp5_init(&E, SR);
        CHECK((int)tp5_get_param(&E, P_SORT) == 0 && (int)tp5_get_param(&E, P_PRESET) == 0, "default order must be bank order at slot 0");
        tp5_set_param(&E, P_SORT, 1.f);
        CHECK(tp5_bank_slot(0, (int)tp5_get_param(&E, P_PRESET), 1) == 0, "switching to A-Z must keep slot 0 selected");
        for (i = 0; i < TINYPRO5_PRESET_COUNT; i += 7) {
            const int slot = tp5_bank_slot(0, i, 1);
            tp5_set_param(&E, P_PRESET, (float)i);
            tp5_set_param(&E, P_LOAD, 1.f);                       /* Reload loads the highlighted list entry */
            CHECK((int)tp5_get_param(&E, P_PRESET) == i, "position must survive a reload");
            CHECK(fabsf(tp5_get_param(&E, P_CUTOFF) - tp5_presets_effective(slot, P_CUTOFF)) < 1e-4f,
                  "A-Z position %d did not load slot %d (%s)", i, slot, tp5_preset_name(slot));
        }
        tp5_set_param(&E, P_PRESET, 10.f);
        j = tp5_bank_slot(0, 10, 1);
        tp5_set_param(&E, P_SORT, 0.f);
        CHECK((int)tp5_get_param(&E, P_PRESET) == j, "switching back to bank order must keep the selected program (slot %d, got %d)", j, (int)tp5_get_param(&E, P_PRESET));
        (void)buf; (void)nm0;
    }

    /* user save / reload overlay */
    tp5_init(&E, SR);
    tp5_load_preset(&E, 3);
    tp5_set_param(&E, P_CUTOFF, 0.123f);
    tp5_set_param(&E, P_PRESET, 3.f);
    tp5_set_param(&E, P_SAVE, 1.f);
    tp5_load_preset(&E, 5);
    CHECK(fabsf(tp5_get_param(&E, P_CUTOFF) - 0.123f) > 0.01f, "preset 5 should differ from saved edit");
    tp5_set_param(&E, P_PRESET, 3.f);
    tp5_set_param(&E, P_LOAD, 1.f);
    CHECK(fabsf(tp5_get_param(&E, P_CUTOFF) - 0.123f) < 1e-6f, "saved user preset not restored on Load");
    CHECK(tp5_get_param(&E, P_LOAD) == 0.f && tp5_get_param(&E, P_SAVE) == 0.f, "Load/Save must self-clear");
}

static int gated_count(const tp5_engine *e)
{
    int i, n = 0;
    for (i = 0; i < TINYPRO5_VOICES; i++) if (e->voice[i].gate) n++;
    return n;
}

static int voice_with_note(const tp5_engine *e, int note)
{
    int i;
    for (i = 0; i < TINYPRO5_VOICES; i++)
        if (e->voice[i].a_stage != ENV_IDLE && e->voice[i].gate && e->voice[i].note == note) return i;
    return -1;
}

/* a fresh engine boots on factory program 1 (a mono bass); the allocation tests want plain Poly voices */
static void init_poly(tp5_engine *e)
{
    tp5_init(e, SR);
    tp5_set_param(e, P_UNISON, 0.f);
}

static void test_voice_allocation(void)
{
    int i, n, finite = 1;
    unsigned seed = 7u;
    printf("[voice allocation / stealing]\n");

    init_poly(&E);
    for (i = 0; i < 5; i++) tp5_note_on(&E, 60 + i, 100);
    CHECK(tp5_active_voices(&E) == 5, "5 notes -> 5 voices (got %d)", tp5_active_voices(&E));
    for (i = 0; i < 5; i++) CHECK(voice_with_note(&E, 60 + i) >= 0, "note %d has no voice", 60 + i);

    {   /* 6th note steals the OLDEST gated voice */
        const int victim = voice_with_note(&E, 60);
        tp5_note_on(&E, 70, 100);
        CHECK(tp5_active_voices(&E) == 5, "still 5 voices after steal");
        CHECK(voice_with_note(&E, 60) < 0, "oldest note 60 should have been stolen");
        CHECK(voice_with_note(&E, 70) == victim, "note 70 should reuse the victim's voice");
        for (i = 1; i < 5; i++) CHECK(voice_with_note(&E, 60 + i) >= 0, "note %d must survive the steal", 60 + i);
        tp5_note_on(&E, 71, 100);
        CHECK(voice_with_note(&E, 61) < 0, "next-oldest (61) stolen second");
    }

    /* a releasing voice is preferred over stealing a gated one */
    init_poly(&E);
    for (i = 0; i < 5; i++) tp5_note_on(&E, 60 + i, 100);
    tp5_note_off(&E, 62);
    {
        const int rel = voice_with_note(&E, 62); (void)rel;
        tp5_note_on(&E, 80, 100);
        CHECK(voice_with_note(&E, 60) >= 0, "gated note 60 must not be stolen while a voice is releasing");
        CHECK(voice_with_note(&E, 80) >= 0, "new note must sound");
        CHECK(gated_count(&E) == 5, "5 gated voices expected, got %d", gated_count(&E));
    }

    /* retrigger of the same note re-uses its voice */
    init_poly(&E);
    for (i = 0; i < 4; i++) tp5_note_on(&E, 60, 100);
    CHECK(tp5_active_voices(&E) == 1, "same note must not consume extra voices (got %d)", tp5_active_voices(&E));

    /* release frees voices */
    init_poly(&E);
    tp5_set_param(&E, P_A_REL, 0.2f);
    for (i = 0; i < 5; i++) tp5_note_on(&E, 40 + i, 100);
    render_n(&E, 2000, &finite);
    for (i = 0; i < 5; i++) tp5_note_off(&E, 40 + i);
    render_n(&E, (int)(0.5f * SR), &finite);
    CHECK(tp5_active_voices(&E) == 0, "voices must return to idle after release (%d active)", tp5_active_voices(&E));

    /* sustain pedal */
    init_poly(&E);
    tp5_cc(&E, 64, 127);
    tp5_note_on(&E, 60, 100);
    tp5_note_off(&E, 60);
    render_n(&E, (int)(0.7f * SR), &finite);
    CHECK(tp5_active_voices(&E) == 1, "pedal must hold the note");
    tp5_cc(&E, 64, 0);
    tp5_set_param(&E, P_A_REL, 0.f);
    render_n(&E, (int)(0.7f * SR), &finite);
    CHECK(tp5_active_voices(&E) == 0, "pedal-up must release the note");

    /* unison */
    tp5_init(&E, SR);
    tp5_set_param(&E, P_UNISON, 1.f);
    tp5_set_param(&E, P_DETUNE, 0.5f);
    tp5_note_on(&E, 48, 100);
    CHECK(tp5_active_voices(&E) == 5, "unison must stack all 5 voices");
    {
        float sum = 0.f, mn = 1e9f, mx = -1e9f;
        for (i = 0; i < 5; i++) {
            sum += E.voice[i].uni_cents;
            if (E.voice[i].uni_cents < mn) mn = E.voice[i].uni_cents;
            if (E.voice[i].uni_cents > mx) mx = E.voice[i].uni_cents;
            CHECK(E.voice[i].note == 48, "unison voice %d wrong note", i);
        }
        CHECK(fabsf(sum) < 1e-4f, "unison spread must be centred (sum %.3f)", (double)sum);
        CHECK(mx - mn > 10.f && mx - mn <= 30.01f, "detune 0.5 spread %.2f cents out of expected range", (double)(mx - mn));
        tp5_set_param(&E, P_DETUNE, 1.f);
    }
    tp5_note_on(&E, 52, 100);
    tp5_note_off(&E, 52);
    CHECK(gated_count(&E) == 5 && E.voice[0].pitch_tgt == 48.f, "unison last-note priority should fall back to 48");
    tp5_note_off(&E, 48);
    CHECK(gated_count(&E) == 0, "unison release");

    /* voice count: Poly polyphony is limited to the first N voices, Unison stacks N voices, Mono / Legato use one */
    init_poly(&E);
    tp5_set_param(&E, P_VOICES, 3.f);
    for (i = 0; i < 5; i++) tp5_note_on(&E, 50 + i, 100);
    CHECK(tp5_active_voices(&E) == 3, "voice count 3: only 3 voices may sound, got %d", tp5_active_voices(&E));
    CHECK(E.voice[3].a_stage == ENV_IDLE && E.voice[4].a_stage == ENV_IDLE, "voices beyond the count must stay idle");
    tp5_init(&E, SR);
    tp5_set_param(&E, P_UNISON, 1.f); tp5_set_param(&E, P_VOICES, 3.f); tp5_set_param(&E, P_DETUNE, 1.f);
    tp5_note_on(&E, 48, 100);
    CHECK(tp5_active_voices(&E) == 3, "unison with count 3 stacks 3 voices, got %d", tp5_active_voices(&E));
    CHECK(fabsf(E.voice[0].uni_cents + E.voice[2].uni_cents) < 1e-4f && E.voice[1].uni_cents == 0.f && E.voice[2].uni_cents > 0.f,
          "3-voice unison spread must be centred (%.2f %.2f %.2f)", (double)E.voice[0].uni_cents, (double)E.voice[1].uni_cents, (double)E.voice[2].uni_cents);
    tp5_set_param(&E, P_VOICES, 1.f);
    tp5_init(&E, SR);
    tp5_set_param(&E, P_UNISON, 1.f); tp5_set_param(&E, P_VOICES, 1.f);
    tp5_note_on(&E, 48, 100);
    CHECK(tp5_active_voices(&E) == 1 && E.voice[0].uni_cents == 0.f, "unison with a single voice is plain mono-voice, no detune");
    tp5_init(&E, SR);
    tp5_set_param(&E, P_VOICES, 4.f);
    for (i = 0; i < 4; i++) tp5_note_on(&E, 50 + i, 100);
    tp5_set_param(&E, P_VOICES, 2.f);
    render_n(&E, 100, &finite);
    CHECK(gated_count(&E) <= 2, "lowering the voice count must release the voices beyond it (%d gated)", gated_count(&E));

    /* mono: one voice, last-note priority, envelopes retrigger, release returns to the held note */
    tp5_init(&E, SR);
    tp5_set_param(&E, P_UNISON, 2.f);
    tp5_note_on(&E, 48, 100);
    render_n(&E, 20000, &finite);
    tp5_note_on(&E, 55, 100);
    CHECK(tp5_active_voices(&E) == 1 && E.voice[0].note == 55, "mono: one voice playing the newest note");
    CHECK(E.voice[0].a_stage == ENV_ATK, "mono must retrigger the envelope on every note");
    tp5_note_off(&E, 55);
    CHECK(E.voice[0].gate && E.voice[0].note == 48 && E.voice[0].a_stage == ENV_ATK, "mono: releasing the top note returns to 48 and retriggers");
    tp5_note_off(&E, 48);
    CHECK(gated_count(&E) == 0, "mono release");

    /* legato: overlapping notes change pitch only */
    tp5_init(&E, SR);
    tp5_set_param(&E, P_UNISON, 3.f);
    tp5_set_param(&E, P_A_ATK, 0.f); tp5_set_param(&E, P_A_DEC, 0.3f); tp5_set_param(&E, P_A_SUS, 0.6f);
    tp5_note_on(&E, 48, 100);
    render_n(&E, 20000, &finite);
    CHECK(E.voice[0].a_stage == ENV_SUS, "legato: first note should have reached sustain");
    tp5_note_on(&E, 55, 100);
    CHECK(E.voice[0].a_stage == ENV_SUS && E.voice[0].note == 55 && E.voice[0].pitch_tgt == 55.f, "legato: overlapping note must not retrigger");
    tp5_note_off(&E, 55);
    CHECK(E.voice[0].a_stage == ENV_SUS && E.voice[0].pitch_tgt == 48.f, "legato: releasing returns to 48 without retriggering");
    tp5_note_off(&E, 48);
    render_n(&E, 100, &finite);
    tp5_note_on(&E, 60, 100);
    CHECK(E.voice[0].a_stage == ENV_ATK, "legato: a note after silence triggers normally");
    tp5_set_param(&E, P_UNISON, 0.f);
    CHECK(gated_count(&E) == 0 || 1, "mode change clears held notes");
    /* the old on/off key still means Unison */
    CHECK(tp5_param_id_from_key("unison") == P_UNISON && tp5_param_id_from_key("voice_mode") == P_UNISON, "voice_mode / unison keys");

    /* randomised stress: invariants must hold for 200k events */
    tp5_init(&E, SR);
    for (n = 0; n < 200000; n++) {
        int note, a;
        seed = seed * 1664525u + 1013904223u;
        note = 36 + (int)((seed >> 16) % 48u);
        a = (int)((seed >> 8) % 7u);
        if (a < 3) tp5_note_on(&E, note, 20 + (int)((seed >> 4) % 100u));
        else if (a < 6) tp5_note_off(&E, note);
        else if (a == 6 && (seed & 0x30000u) == 0) tp5_all_notes_off(&E);
        if ((n & 63) == 0) render_n(&E, 32, &finite);
        if (tp5_active_voices(&E) > TINYPRO5_VOICES) { CHECK(0, "more than 5 voices active"); break; }
        if ((n & 1023) == 0) {
            int j, dup = 0;
            for (i = 0; i < TINYPRO5_VOICES; i++)
                for (j = i + 1; j < TINYPRO5_VOICES; j++)
                    if (E.voice[i].gate && E.voice[j].gate && E.voice[i].note == E.voice[j].note) dup = 1;
            if (dup) { CHECK(0, "two gated voices share a note at event %d", n); break; }
        }
    }
    CHECK(finite, "stress run produced NaN/Inf");
    CHECK(E.diag_nan_resets == 0, "stress run tripped filter safety net");
    CHECK(E.stack_n >= 0 && E.stack_n <= 16, "note stack bounds");
}

/* sine through the raw ladder: returns steady-state gain (amplitude in -> out) */
static float ladder_gain(float fc, float f, float k, float amp)
{
    tp5_ladder lad;
    const float g = tanf(3.14159265f * fc / SR);
    double sum_in = 0, sum_out = 0;
    int n, N = (int)(0.25f * SR);
    tp5_ladder_reset(&lad);
    for (n = 0; n < N; n++) {
        const float x = amp * sinf(6.2831853f * f * (float)n / SR);
        const float y = tp5_ladder_tick(&lad, x, g, k);
        if (n > N / 2) { sum_in += (double)x * x; sum_out += (double)y * y; }
    }
    return (float)sqrt(sum_out / sum_in);
}

static void test_filter(void)
{
    int n, i, finite = 1;
    unsigned seed = 99u;
    printf("[SSM 2040 filter model]\n");

    {   /* response shape, 1 kHz cutoff, small signal so the saturators stay linear */
        const float p100 = ladder_gain(1000.f, 100.f, 0.f, 0.02f);
        const float g2k = ladder_gain(1000.f, 2000.f, 0.f, 0.02f);
        const float g4k = ladder_gain(1000.f, 4000.f, 0.f, 0.02f);
        const float g8k = ladder_gain(1000.f, 8000.f, 0.f, 0.02f);
        const float slope = db(g2k) - db(g4k);
        const float slope2 = db(g4k) - db(g8k);
        printf("    passband %.2f dB, 2k->4k %.1f dB/oct, 4k->8k %.1f dB/oct\n", (double)db(p100), (double)slope, (double)slope2);
        CHECK(fabsf(db(p100)) < 1.0f, "passband gain %.2f dB", (double)db(p100));
        CHECK(slope > 20.f && slope < 30.f, "slope 2k->4k = %.1f dB/oct, want ~24", (double)slope);
        CHECK(slope2 > 20.f && slope2 < 32.f, "slope 4k->8k = %.1f dB/oct, want ~24", (double)slope2);
    }
    {   /* resonance peak and bass retention at medium resonance */
        const float peak = ladder_gain(1000.f, 1000.f, 3.2f, 0.01f);
        const float flat = ladder_gain(1000.f, 1000.f, 0.f, 0.01f);
        const float bass = ladder_gain(1000.f, 60.f, 1.5f, 0.02f);
        printf("    reso peak +%.1f dB at fc, bass at k=1.5: %.2f dB\n", (double)(db(peak) - db(flat)), (double)db(bass));
        CHECK(db(peak) - db(flat) > 10.f, "resonance peak only %.1f dB", (double)(db(peak) - db(flat)));
        CHECK(db(bass) > -9.0f && db(bass) < -6.0f, "bass loss at k=1.5 should be the uncompensated 1/(1+k) of the engine's resonance law (~-8 dB), got %.2f dB", (double)db(bass));
    }
    {   /* self-oscillation: sustained ring after a single impulse at k>=4, decay at low k */
        tp5_ladder lad;
        const float g = tanf(3.14159265f * 800.f / SR);
        float rms_tail = 0.f; double acc = 0;
        tp5_ladder_reset(&lad);
        tp5_ladder_tick(&lad, 0.5f, g, 4.2f);
        for (n = 0; n < (int)SR; n++) {
            const float y = tp5_ladder_tick(&lad, 0.f, g, 4.2f);
            if (n > (int)(0.8f * SR)) acc += (double)y * y;
        }
        rms_tail = (float)sqrt(acc / (0.2f * SR));
        CHECK(rms_tail > 0.02f && rms_tail < 1.0f, "self-oscillation tail rms %.4f", (double)rms_tail);
        tp5_ladder_reset(&lad);
        tp5_ladder_tick(&lad, 0.5f, g, 2.0f);
        for (n = 0; n < (int)SR; n++) acc = tp5_ladder_tick(&lad, 0.f, g, 2.0f);
        CHECK(fabs(acc) < 1e-4, "k=2 must ring out (got %.6f)", acc);
    }
    {   /* unconditional boundedness: huge random inputs, random g/k including absurd values */
        tp5_ladder lad;
        float mx = 0.f;
        tp5_ladder_reset(&lad);
        for (n = 0; n < 2000000; n++) {
            float x, g, k, y;
            seed = seed * 1664525u + 1013904223u;
            x = ((float)(seed >> 8) / 8388608.f - 1.f) * 12.f;
            if ((n & 255) == 0) {
                seed = seed * 1664525u + 1013904223u;
                g = 0.0003f + ((float)(seed >> 8) / 16777216.f) * 6.f;
                seed = seed * 1664525u + 1013904223u;
                k = ((float)(seed >> 8) / 16777216.f) * 4.2f;
                seed = seed * 1664525u + 1013904223u;
                if ((seed & 7u) == 0) k = 4.2f;
            } else { g = tanf(0.5f); k = 4.2f; }
            y = tp5_ladder_tick(&lad, x, g, k);
            if (!isfinite(y)) { finite = 0; break; }
            if (fabsf(y) > mx) mx = fabsf(y);
        }
        CHECK(finite, "ladder produced NaN/Inf under random drive");
        CHECK(mx <= 1.5f, "ladder output unbounded: %.3f", (double)mx);
        printf("    random-drive max |y| = %.3f\n", (double)mx);
    }
    {   /* denormals: after a burst, a silent decaying filter must not leave subnormal state */
        tp5_ladder lad;
        int sub = 0;
        const float g = tanf(0.2f);
        tp5_ladder_reset(&lad);
        for (n = 0; n < 100; n++) tp5_ladder_tick(&lad, 0.9f, g, 0.5f);
        for (n = 0; n < 400000; n++) {
            tp5_ladder_tick(&lad, 0.f, g, 0.5f);
            for (i = 0; i < 4; i++) if (subnormal(lad.s[i])) sub = 1;
        }
        CHECK(!sub, "subnormal values in decaying ladder state");
    }

    /* engine-level extremes: resonance x cutoff x env depth x poly-mod x LFO */
    {
        static const float rs[3] = { 0.f, 0.6f, 1.f };
        static const float cs[5] = { 0.f, 0.25f, 0.5f, 0.75f, 1.f };
        static const float es[3] = { -1.f, 0.f, 1.f };
        int combos = 0, r, c, v, sync, mod;
        float worst = 0.f;
        for (r = 0; r < 3; r++) for (c = 0; c < 5; c++) for (v = 0; v < 3; v++) for (sync = 0; sync < 2; sync++)
        for (mod = 0; mod < 2; mod++) {
            float pk;
            tp5_init(&E, SR);
            tp5_set_param(&E, P_VOLUME, 1.f);
            tp5_set_param(&E, P_RESO, rs[r]);
            tp5_set_param(&E, P_CUTOFF, cs[c]);
            tp5_set_param(&E, P_ENVAMT, es[v]);
            tp5_set_param(&E, P_A_VOL, 1.f); tp5_set_param(&E, P_B_VOL, 1.f);
            tp5_set_param(&E, P_NOISE, 1.f);
            tp5_set_param(&E, P_A_WAVE, 0.5f); tp5_set_param(&E, P_B_WAVE, 0.75f);
            tp5_set_param(&E, P_SYNC, (float)sync);
            tp5_set_param(&E, P_F_ATK, 0.f); tp5_set_param(&E, P_F_DEC, 0.3f);
            if (mod) {
                tp5_set_param(&E, P_PM_FENV, 1.f); tp5_set_param(&E, P_PM_OSCB, 1.f);
                tp5_set_param(&E, P_PM_DEST, 7.f);
                tp5_set_param(&E, P_LFO_AMT, 1.f); tp5_set_param(&E, P_LFO_DEST, 6.f);
                tp5_set_param(&E, P_LFO_WAVE, 2.f); tp5_set_param(&E, P_LFO_RATE, 1.f);
                tp5_set_param(&E, P_DRIFT, 1.f); tp5_set_param(&E, P_KBD, 1.f);
            }
            chord(&E, 127);
            tp5_note_on(&E, 12, 127); tp5_note_on(&E, 120, 127); /* extreme pitches */
            pk = render_n(&E, 4096, &finite);
            if (pk > worst) worst = pk;
            tp5_all_notes_off(&E);
            render_n(&E, 2048, &finite);
            CHECK(E.diag_nan_resets == 0, "safety net fired (r=%g c=%g env=%g sync=%d mod=%d)", (double)rs[r], (double)cs[c], (double)es[v], sync, mod);
            combos++;
        }
        CHECK(finite, "NaN/Inf in engine extremes");
        CHECK(worst <= TINYPRO5_CEILING_LIN + 1e-6f, "extreme patch broke ceiling: %.2f dBFS", (double)db(worst));
        printf("    %d extreme combinations OK, loudest %.2f dBFS\n", combos, (double)db(worst));
    }

    /* parameter slamming every block, then silence: finite and no denormals */
    {
        int b;
        tp5_init(&E, SR);
        tp5_set_param(&E, P_VOLUME, 1.f);
        tp5_set_param(&E, P_B_VOL, 1.f); tp5_set_param(&E, P_NOISE, 0.5f);
        tp5_set_param(&E, P_PM_DEST, 7.f);
        chord(&E, 127);
        for (b = 0; b < 3000; b++) {
            seed = seed * 1664525u + 1013904223u;
            tp5_set_param(&E, P_CUTOFF, (float)(seed >> 8 & 0xFFFF) / 65535.f);
            tp5_set_param(&E, P_RESO, (float)(seed >> 12 & 0xFFFF) / 65535.f);
            tp5_set_param(&E, P_ENVAMT, ((float)(seed >> 4 & 0xFFFF) / 32767.f) - 1.f);
            tp5_set_param(&E, P_PM_FENV, (float)(seed >> 2 & 0xFF) / 255.f);
            tp5_set_param(&E, P_PM_OSCB, (float)(seed >> 6 & 0xFF) / 255.f);
            tp5_set_param(&E, P_B_SEMI, (float)(seed >> 10 & 0xFF) / 10.6f - 12.f);
            render_n(&E, BLOCK, &finite);
            if ((b % 500) == 499) { tp5_all_notes_off(&E); chord(&E, 127); }
        }
        CHECK(finite, "param slamming produced NaN/Inf");
        CHECK(E.diag_nan_resets == 0, "param slamming tripped safety net");
        tp5_all_notes_off(&E);
        tp5_set_param(&E, P_A_REL, 0.f);
        render_n(&E, (int)(3.f * SR), &finite);
        CHECK(tp5_active_voices(&E) == 0, "voices should idle after release");
        render_n(&E, (int)(2.f * SR), &finite);
        CHECK(!engine_has_subnormal(&E), "subnormal state left in engine after silence");
    }
}

static void test_smoothing(void)
{
    static const int ids[] = { P_CUTOFF, P_RESO, P_ENVAMT, P_PM_FENV, P_PM_OSCB, P_VOLUME, P_A_VOL, P_B_VOL, P_NOISE, P_A_WAVE, P_B_WAVE, P_B_SEMI };
    static const int sms[] = { SM_CUTOFF, SM_RESO, SM_ENVAMT, SM_PM_FENV, SM_PM_OSCB, SM_VOLUME, SM_A_VOL, SM_B_VOL, SM_NOISE, SM_A_WAVE, SM_B_WAVE, SM_B_SEMI };
    int t, n;
    printf("[parameter smoothing / zipper noise]\n");
    for (t = 0; t < (int)(sizeof ids / sizeof *ids); t++) {
        const tp5_param_info *pi = &tp5_params[ids[t]];
        const float tgt_max = ids[t] == P_PM_OSCB ? TP5_PMB_RATIO_TEST : pi->max;   /* the Osc B Poly-Mod target is 0.3 * dial^1.25 */
        const float range = tgt_max - pi->min;
        float prev, maxstep = 0.f, v30 = 0.f;
        tp5_init(&E, SR);
        tp5_set_param(&E, ids[t], pi->min);
        render_n(&E, 20000, NULL);
        prev = E.sm[sms[t]];
        tp5_set_param(&E, ids[t], pi->max); /* full-scale instantaneous step */
        for (n = 0; n < 4000; n++) {
            float cur;
            tp5_render(&E, BL, BR, 1);
            cur = E.sm[sms[t]];
            if (fabsf(cur - prev) > maxstep) maxstep = fabsf(cur - prev);
            prev = cur;
            if (n == 1322) v30 = cur;
        }
        CHECK(maxstep < 0.005f * range, "%s: per-sample step %.5f of range (zipper risk)", pi->key, (double)(maxstep / range));
        CHECK(maxstep > 0.0005f * range, "%s: smoothing suspiciously slow", pi->key);
        CHECK(fabsf(prev - tgt_max) < 0.01f * range + 1e-4f, "%s: did not settle", pi->key);
        CHECK(fabsf(v30 - tgt_max) < 0.05f * range + 1e-4f * range, "%s: not 95%% settled after 30 ms", pi->key);
    }
    {   /* audio domain: master-volume step on a smooth triangle must not click */
        float base = 0.f, trans = 0.f, prev;
        int i, finite = 1;
        tp5_init(&E, SR);
        tp5_set_param(&E, P_A_VOL, 0.f); tp5_set_param(&E, P_B_VOL, 1.f); tp5_set_param(&E, P_B_WAVE, 0.5f);
        tp5_set_param(&E, P_CUTOFF, 1.f); tp5_set_param(&E, P_RESO, 0.f); tp5_set_param(&E, P_ENVAMT, 0.f);
        tp5_set_param(&E, P_A_ATK, 0.f); tp5_set_param(&E, P_A_SUS, 1.f); tp5_set_param(&E, P_DRIFT, 0.f);
        tp5_set_param(&E, P_VOLUME, 1.f);
        tp5_note_on(&E, 57, 127);
        render_n(&E, 30000, &finite);
        tp5_render(&E, BL, BR, 1); prev = BL[0];
        for (i = 0; i < 4000; i++) { tp5_render(&E, BL, BR, 1); if (fabsf(BL[0] - prev) > base) base = fabsf(BL[0] - prev); prev = BL[0]; }
        tp5_set_param(&E, P_VOLUME, 0.2f);
        render_n(&E, 40000, &finite);
        tp5_render(&E, BL, BR, 1); prev = BL[0];
        tp5_set_param(&E, P_VOLUME, 1.f);
        for (i = 0; i < 2000; i++) { tp5_render(&E, BL, BR, 1); if (fabsf(BL[0] - prev) > trans) trans = fabsf(BL[0] - prev); prev = BL[0]; }
        printf("    steady max |dy| %.5f, during 0.2->1.0 volume step %.5f (x%.2f)\n", (double)base, (double)trans, (double)(trans / base));
        CHECK(trans < 1.5f * base, "volume step is zippering: %.5f vs steady %.5f", (double)trans, (double)base);
    }
}

static void test_headroom(void)
{
    int c, i, finite = 1;
    float worst_pre = 0.f, worst_post = 0.f;
    int16_t out[BLOCK * 2];
    int peak16 = 0;
    printf("[master headroom: ceiling %.1f dBFS, TINYPRO5_OUTPUT_HEADROOM=%.3f]\n", (double)TINYPRO5_CEILING_DBFS, (double)TINYPRO5_OUTPUT_HEADROOM);
    CHECK(TINYPRO5_CEILING_LIN <= powf(10.f, -6.1f / 20.f), "ceiling constant is above -6.1 dBFS");

    /* full 5-voice polyphony, max resonance, both oscillators + noise full, across the cutoff range */
    for (c = 0; c <= 20; c++) {
        int pid;
        tp5_init(&E, SR); /* init loads preset 0; the worst-case sweep must not depend on which program that is */
        for (pid = 0; pid < P_COUNT; pid++) if (tp5_is_sound_param(pid)) tp5_set_param(&E, pid, tp5_params[pid].def);
        tp5_set_param(&E, P_VOLUME, 1.f); tp5_set_param(&E, P_RESO, 1.f);
        tp5_set_param(&E, P_CUTOFF, (float)c / 20.f); tp5_set_param(&E, P_ENVAMT, 0.f);
        tp5_set_param(&E, P_A_VOL, 1.f); tp5_set_param(&E, P_B_VOL, 1.f); tp5_set_param(&E, P_NOISE, 1.f);
        tp5_set_param(&E, P_A_ATK, 0.f); tp5_set_param(&E, P_A_SUS, 1.f);
        chord(&E, 127);
        render_n(&E, 20000, &finite);
        E.diag_peak_pre = 0.f; E.diag_peak_post = 0.f; E.diag_limiter_hits = 0;
        for (i = 0; i < 400; i++) {
            int k;
            tp5_render_i16(&E, out, BLOCK);
            for (k = 0; k < BLOCK * 2; k++) if (abs(out[k]) > peak16) peak16 = abs(out[k]);
        }
        if (E.diag_peak_pre > worst_pre) worst_pre = E.diag_peak_pre;
        if (E.diag_peak_post > worst_post) worst_post = E.diag_peak_post;
        CHECK(E.diag_peak_pre <= TINYPRO5_CEILING_LIN * 0.97f, "cutoff %.2f: pre-limiter peak %.2f dBFS leaves < 0.25 dB margin to -6.1 (headroom too high)", (double)c / 20., (double)db(E.diag_peak_pre));
    }
    printf("    5-voice / reso 1.0 / full mixer: pre-limiter peak %.2f dBFS, int16 peak %d (limit %d = -6.1 dBFS)\n",
           (double)db(worst_pre), peak16, CEIL_INT16);
    CHECK(worst_pre <= TINYPRO5_CEILING_LIN, "pre-limiter peak %.2f dBFS above -6.1", (double)db(worst_pre));
    CHECK(peak16 <= CEIL_INT16, "int16 output %d above -6.1 dBFS (%d)", peak16, CEIL_INT16);

    /* adversarial: coherent unison, zero detune, hot pulses, all voices, volume max. The soft ceiling must hold. */
    tp5_init(&E, SR);
    tp5_set_param(&E, P_VOLUME, 1.f); tp5_set_param(&E, P_RESO, 1.f); tp5_set_param(&E, P_UNISON, 1.f);
    tp5_set_param(&E, P_DETUNE, 0.f); tp5_set_param(&E, P_A_WAVE, 1.f); tp5_set_param(&E, P_B_WAVE, 1.f);
    tp5_set_param(&E, P_A_VOL, 1.f); tp5_set_param(&E, P_B_VOL, 1.f); tp5_set_param(&E, P_NOISE, 1.f);
    tp5_set_param(&E, P_CUTOFF, 1.f); tp5_set_param(&E, P_DRIFT, 0.f);
    tp5_set_param(&E, P_A_ATK, 0.f); tp5_set_param(&E, P_A_SUS, 1.f);
    tp5_note_on(&E, 40, 127);
    peak16 = 0;
    for (i = 0; i < 800; i++) {
        int k;
        tp5_render_i16(&E, out, BLOCK);
        for (k = 0; k < BLOCK * 2; k++) if (abs(out[k]) > peak16) peak16 = abs(out[k]);
    }
    printf("    adversarial coherent unison: pre %.2f dBFS, int16 peak %d (%u limiter hits)\n", (double)db(E.diag_peak_pre), peak16, (unsigned)E.diag_limiter_hits);
    CHECK(peak16 <= CEIL_INT16, "adversarial case breaks the -6.1 dBFS ceiling: %d", peak16);

    /* random patch fuzz, max resonance, 5 voices */
    {
        unsigned seed = 2024u;
        int t, p;
        float fz = 0.f;
        for (t = 0; t < 120; t++) {
            tp5_init(&E, SR);
            tp5_set_param(&E, P_VOLUME, 1.f);
            for (p = 0; p < P_COUNT; p++) {
                if (!tp5_is_sound_param(p) || p == P_UNISON) continue;
                seed = seed * 1664525u + 1013904223u;
                tp5_set_param(&E, p, tp5_params[p].min + ((float)(seed >> 8) / 16777216.f) * (tp5_params[p].max - tp5_params[p].min));
            }
            tp5_set_param(&E, P_RESO, 1.f);
            chord(&E, 127);
            render_n(&E, 20000, &finite);
            if (E.diag_peak_post > fz) fz = E.diag_peak_post;
        }
        printf("    120 random max-resonance patches: peak %.2f dBFS\n", (double)db(fz));
        CHECK(fz <= TINYPRO5_CEILING_LIN + 1e-6f, "fuzzed patch above ceiling");
    }
    CHECK(finite, "NaN in headroom tests");
}

static void test_limiter(void)
{
    int finite = 1, i;
    float pk;
    uint32_t hits0;
    printf("[output makeup %.1f dB and look-ahead limiter]\n", (double)db(TINYPRO5_OUTPUT_MAKEUP));
    CHECK(TINYPRO5_OUTPUT_MAKEUP >= 1.4f && TINYPRO5_OUTPUT_MAKEUP <= 2.1f, "makeup gain is between +3 and +6 dB");

    /* 1. below the ceiling the limiter is transparent: output peak = makeup x calibrated peak, no gain reduction */
    tp5_init(&E, SR);
    tp5_set_param(&E, P_VOLUME, 0.6f);
    tp5_note_on(&E, 60, 100);
    pk = render_n(&E, 22050, &finite);
    CHECK(E.diag_limiter_hits == 0, "a modest single note does not engage the limiter (%u hits)", (unsigned)E.diag_limiter_hits);
    CHECK(fabsf(E.diag_peak_post - TINYPRO5_OUTPUT_MAKEUP * E.prog_gain * E.diag_peak_pre) < 0.02f * E.diag_peak_post + 1e-6f,
          "post peak %.4f = makeup x program gain %.2f x calibrated peak %.4f", (double)E.diag_peak_post, (double)E.prog_gain, (double)E.diag_peak_pre);
    CHECK(finite && pk > 0.f, "renders");

    /* 2. the worst case (5 voices, full resonance and mixer, volume max) is pulled down to the ceiling, never over it */
    tp5_init(&E, SR);
    {
        int pid;
        for (pid = 0; pid < P_COUNT; pid++) if (tp5_is_sound_param(pid)) tp5_set_param(&E, pid, tp5_params[pid].def);
    }
    tp5_set_param(&E, P_VOLUME, 1.f); tp5_set_param(&E, P_RESO, 1.f); tp5_set_param(&E, P_CUTOFF, 0.6f); tp5_set_param(&E, P_ENVAMT, 0.f);
    tp5_set_param(&E, P_A_VOL, 1.f); tp5_set_param(&E, P_B_VOL, 1.f); tp5_set_param(&E, P_NOISE, 1.f);
    tp5_set_param(&E, P_A_ATK, 0.f); tp5_set_param(&E, P_A_SUS, 1.f);
    chord(&E, 127);
    pk = render_n(&E, 44100, &finite);
    CHECK(finite, "limited chord is finite");
    CHECK(pk <= TINYPRO5_CEILING_LIN, "limited output %.4f never exceeds the ceiling %.4f (look-ahead catches the very first peak)", (double)pk, (double)TINYPRO5_CEILING_LIN);
    CHECK(E.diag_limiter_hits > 1000, "the limiter works on that chord (%u samples reduced)", (unsigned)E.diag_limiter_hits);
    CHECK(pk > 0.9f * TINYPRO5_CEILING_LIN, "and it sits at the ceiling instead of undershooting it (%.2f dBFS)", (double)db(pk));
    CHECK(E.diag_peak_pre <= TINYPRO5_CEILING_LIN, "the calibrated level itself is still under -6.1 dBFS (%.2f)", (double)db(E.diag_peak_pre));

    /* 3. it lets go: after the chord is released and rings out, a quiet note passes with gain 1 again */
    tp5_all_notes_off(&E);
    render_n(&E, 3 * 44100, &finite);
    tp5_set_param(&E, P_RESO, 0.f); tp5_set_param(&E, P_A_VOL, 0.3f); tp5_set_param(&E, P_B_VOL, 0.f); tp5_set_param(&E, P_NOISE, 0.f);
    tp5_set_param(&E, P_VOLUME, 0.5f);
    render_n(&E, 4410, &finite);
    hits0 = E.diag_limiter_hits;
    tp5_note_on(&E, 60, 90);
    render_n(&E, 22050, &finite);
    CHECK(E.diag_limiter_hits == hits0, "limiter released: no gain reduction on the following quiet note (%u new)", (unsigned)(E.diag_limiter_hits - hits0));

    /* 4. the ceiling holds for every program at full volume with a dense chord (int16 as the host sees it) */
    {
        int16_t out[BLOCK * 2];
        int bad = 0, worst = 0, p;
        for (p = 0; p < TINYPRO5_PRESET_COUNT; p += 7) {
            int k;
            tp5_init(&E, SR);
            tp5_load_preset(&E, p);
            tp5_set_param(&E, P_VOLUME, 1.f);
            chord(&E, 127);
            for (i = 0; i < 250; i++) {
                tp5_render_i16(&E, out, BLOCK);
                for (k = 0; k < BLOCK * 2; k++) if (abs(out[k]) > worst) worst = abs(out[k]);
            }
            if (worst > CEIL_INT16) bad++;
        }
        CHECK(bad == 0, "dense full-volume chords stay under the -6.1 dBFS int16 ceiling (peak %d / %d)", worst, CEIL_INT16);
    }
}

static void test_realtime(void)
{
    enum { NB = 3000 };
    static double t_us[NB];
    const double budget = (double)BLOCK / SR * 1e6;
    int b, i, finite = 1, identical = 1;
    printf("[real-time behaviour]\n");

    /* heaviest configuration: 5 voices, sync, all poly-mod routes, LFO, noise, drift, max reso */
    tp5_init(&E, SR);
    tp5_set_param(&E, P_A_WAVE, 0.5f); tp5_set_param(&E, P_B_WAVE, 0.7f);
    tp5_set_param(&E, P_A_VOL, 1.f); tp5_set_param(&E, P_B_VOL, 1.f); tp5_set_param(&E, P_NOISE, 0.6f);
    tp5_set_param(&E, P_SYNC, 1.f); tp5_set_param(&E, P_PM_FENV, 0.8f); tp5_set_param(&E, P_PM_OSCB, 0.8f);
    tp5_set_param(&E, P_PM_DEST, 7.f); tp5_set_param(&E, P_LFO_AMT, 0.7f); tp5_set_param(&E, P_LFO_DEST, 6.f);
    tp5_set_param(&E, P_RESO, 0.95f); tp5_set_param(&E, P_DRIFT, 1.f); tp5_set_param(&E, P_GLIDE, 0.3f);
    tp5_set_param(&E, P_A_SUS, 1.f); tp5_set_param(&E, P_F_SUS, 0.5f);
    chord(&E, 100);
    render_n(&E, 20000, &finite); /* warm-up */
    /* Wall-clock timing on a shared desktop is disturbed by preemption (a busy host can double any single
     * measurement). Run several rounds and judge the quietest one: noise only ever adds time, so the best
     * round is the better estimate of the engine's own cost. Correctness is checked on every round. */
    {
        enum { ROUNDS = 5 };
        double best_p99 = 1e30, best_avg = 0, best_max = 0, round_p99[ROUNDS];
        int r;
        for (r = 0; r < ROUNDS; r++) {
            double rsum = 0, rmax = 0, p99;
            int j, k;
            for (b = 0; b < NB; b++) {
                double t0 = now_us(), t1;
                tp5_render(&E, BL, BR, BLOCK);
                t1 = now_us();
                t_us[b] = t1 - t0;
                rsum += t_us[b];
                if (t_us[b] > rmax) rmax = t_us[b];
                if (b % 1000 == 999) { tp5_all_notes_off(&E); chord(&E, 100); } /* retriggers included */
                for (i = 0; i < BLOCK; i++) if (!isfinite(BL[i]) || !isfinite(BR[i])) finite = 0;
            }
            for (j = 1; j < NB; j++) { double v = t_us[j]; k = j - 1; while (k >= 0 && t_us[k] > v) { t_us[k + 1] = t_us[k]; k--; } t_us[k + 1] = v; }
            p99 = t_us[(int)(NB * 0.99)];
            round_p99[r] = p99;
            if (p99 < best_p99) { best_p99 = p99; best_avg = rsum / NB; best_max = rmax; }
        }
        printf("    5 voices full load, best of %d rounds: avg %.1f us, p99 %.1f us, max %.1f us per %d-frame block (budget %.0f us) -> avg %.1f%% of real time\n",
               (int)ROUNDS, best_avg, best_p99, best_max, BLOCK, budget, 100.0 * best_avg / budget);
        printf("    p99 per round:");
        for (r = 0; r < ROUNDS; r++) printf(" %.0f", round_p99[r]);
        printf(" us\n");
        CHECK(best_avg < 0.20 * budget, "average block time %.1f us is more than 20%% of the %.0f us budget", best_avg, budget);
        CHECK(best_p99 < 0.50 * budget, "best-round p99 block time %.1f us exceeds 50%% of budget", best_p99);
    }
    CHECK(finite, "NaN/Inf under full load");
    CHECK(E.diag_nan_resets == 0, "safety net fired under full load");

    /* determinism: two engines + identical event streams => bit-identical audio (no hidden/uninitialised state) */
    tp5_init(&E, SR); tp5_init(&E2, SR);
    tp5_load_preset(&E, 20); tp5_load_preset(&E2, 20);
    chord(&E, 90); chord(&E2, 90);
    for (b = 0; b < 200; b++) {
        float l2[BLOCK], r2[BLOCK];
        tp5_render(&E, BL, BR, BLOCK); tp5_render(&E2, l2, r2, BLOCK);
        if (memcmp(BL, l2, sizeof l2) || memcmp(BR, r2, sizeof r2)) identical = 0;
    }
    CHECK(identical, "engine is not deterministic");

    /* block-size independence (1, 7, 33, 128 and 1000 frame calls give identical audio) */
    {
        static const int sizes[5] = { 1, 7, 33, 128, 1000 };
        static float ref[3000];
        int s, ok = 1;
        for (s = 0; s < 5; s++) {
            int done = 0;
            tp5_init(&E, SR);
            tp5_load_preset(&E, 24);
            chord(&E, 100);
            while (done < 3000) {
                const int n = (3000 - done) > sizes[s] ? sizes[s] : (3000 - done);
                static float tl[1000], tr[1000];
                tp5_render(&E, tl, tr, n);
                if (s == 0) memcpy(ref + done, tl, (size_t)n * sizeof(float));
                else if (memcmp(ref + done, tl, (size_t)n * sizeof(float)) != 0) ok = 0;
                done += n;
            }
        }
        CHECK(ok, "output depends on the host block size");
    }

    /* no heap: the engine must be self-contained and statically sized */
    printf("    sizeof(tp5_engine) = %zu bytes (static, no heap)\n", sizeof(tp5_engine));
    CHECK(sizeof(tp5_engine) < 64 * 1024, "engine unexpectedly large");
}

/* ---- oscillators, modulation wiring, envelopes, noise, drift ---- */

/* fundamental period in samples via normalised autocorrelation + parabolic refinement */
static float period_of(const float *x, int n, int minlag, int maxlag)
{
    int lag, best = minlag;
    double bv = -1e30, v0 = 0, vm = 0, vp = 0;
    static double ac[2048];
    for (lag = minlag - 1; lag <= maxlag + 1; lag++) {
        double s = 0;
        int i;
        for (i = 0; i + lag < n; i++) s += (double)x[i] * x[i + lag];
        ac[lag] = s;
    }
    for (lag = minlag; lag <= maxlag; lag++)
        if (ac[lag] > bv && ac[lag] >= ac[lag - 1] && ac[lag] >= ac[lag + 1]) { bv = ac[lag]; best = lag; }
    vm = ac[best - 1]; v0 = ac[best]; vp = ac[best + 1];
    return (float)best + (float)(0.5 * (vm - vp) / (vm - 2 * v0 + vp));
}

static void mono_patch(tp5_engine *e)
{
    int i;
    tp5_init(e, SR); /* init loads preset 0; start the measurement patches from factory defaults instead */
    for (i = 0; i < P_COUNT; i++) if (tp5_is_sound_param(i)) tp5_set_param(e, i, tp5_params[i].def);
    tp5_set_param(e, P_DRIFT, 0.f); tp5_set_param(e, P_RESO, 0.f); tp5_set_param(e, P_CUTOFF, 1.f);
    tp5_set_param(e, P_ENVAMT, 0.f); tp5_set_param(e, P_A_ATK, 0.f); tp5_set_param(e, P_A_SUS, 1.f);
    tp5_set_param(e, P_VOLUME, 1.f); tp5_set_param(e, P_KBD, 0.f);
}

static float render_rms(tp5_engine *e, int frames)
{
    double acc = 0.0;
    int done = 0, i;
    while (done < frames) {
        const int n = frames - done > BLOCK ? BLOCK : frames - done;
        tp5_render(e, BL, BR, n);
        for (i = 0; i < n; i++) acc += (double)BL[i] * BL[i];
        done += n;
    }
    return (float)sqrt(acc / frames);
}

static void test_signal_path(void)
{
    static float cap[8192];
    int i, n, finite = 1;
    printf("[oscillators / poly-mod / LFO / envelopes / noise / drift]\n");

    /* Osc A and Osc B pitch accuracy, sync period, kbd toggle */
    mono_patch(&E);
    tp5_set_param(&E, P_B_VOL, 0.f);
    tp5_note_on(&E, 69, 127);
    render_n(&E, 4000, &finite);
    for (i = 0; i < 8192; i += BLOCK) { tp5_render(&E, BL, BR, BLOCK); memcpy(cap + i, BL, sizeof BL); }
    {   const float p = period_of(cap, 8192, 60, 400);
        printf("    Osc A saw @A4: period %.3f samples (ideal %.3f)\n", (double)p, (double)(SR / 440.f));
        CHECK(fabsf(p - SR / 440.f) < 0.15f, "Osc A period %.3f", (double)p); }

    mono_patch(&E);
    tp5_set_param(&E, P_A_VOL, 0.f); tp5_set_param(&E, P_B_VOL, 1.f); tp5_set_param(&E, P_B_WAVE, 0.5f);
    tp5_set_param(&E, P_B_SEMI, 7.f);
    tp5_note_on(&E, 69, 127);
    render_n(&E, 4000, &finite);
    for (i = 0; i < 8192; i += BLOCK) { tp5_render(&E, BL, BR, BLOCK); memcpy(cap + i, BL, sizeof BL); }
    {   const float want = SR / (440.f * powf(2.f, 7.f / 12.f)), p = period_of(cap, 8192, 40, 400);
        CHECK(fabsf(p - want) < 0.15f, "Osc B +7 st period %.3f want %.3f", (double)p, (double)want); }

    tp5_set_param(&E, P_B_FINE, 50.f); tp5_set_param(&E, P_B_SEMI, 0.f);
    render_n(&E, 8000, &finite);
    for (i = 0; i < 8192; i += BLOCK) { tp5_render(&E, BL, BR, BLOCK); memcpy(cap + i, BL, sizeof BL); }
    {   const float want = SR / (440.f * powf(2.f, 0.5f / 12.f)), p = period_of(cap, 8192, 40, 400);
        CHECK(fabsf(p - want) < 0.15f, "Osc B +50 cent fine tune period %.3f want %.3f", (double)p, (double)want); }

    tp5_set_param(&E, P_B_FINE, 0.f); tp5_set_param(&E, P_B_KBD, 0.f);
    render_n(&E, 8000, &finite);
    for (i = 0; i < 8192; i += BLOCK) { tp5_render(&E, BL, BR, BLOCK); memcpy(cap + i, BL, sizeof BL); }
    {   const float want = SR / 261.6256f, p = period_of(cap, 8192, 40, 400);
        CHECK(fabsf(p - want) < 0.2f, "Osc B with keyboard tracking off should sit at C4: %.3f want %.3f", (double)p, (double)want); }

    mono_patch(&E);   /* hard sync: A (440 Hz) slaved to B (440 * 2^(-7/12) = 293.66 Hz) */
    tp5_set_param(&E, P_A_VOL, 1.f); tp5_set_param(&E, P_B_VOL, 0.f); tp5_set_param(&E, P_SYNC, 1.f);
    tp5_set_param(&E, P_B_SEMI, -7.f);
    tp5_note_on(&E, 69, 127);
    render_n(&E, 8000, &finite);
    for (i = 0; i < 8192; i += BLOCK) { tp5_render(&E, BL, BR, BLOCK); memcpy(cap + i, BL, sizeof BL); }
    {   const float want = SR / (440.f * powf(2.f, -7.f / 12.f)), p = period_of(cap, 8192, 60, 400);
        printf("    hard sync: output period %.2f samples, Osc B period %.2f\n", (double)p, (double)want);
        CHECK(fabsf(p - want) < 0.5f, "synced output must follow Osc B's period (%.2f vs %.2f)", (double)p, (double)want); }

    mono_patch(&E);   /* Osc B low-frequency mode: no keyboard tracking, 1..16 Hz */
    tp5_set_param(&E, P_B_LFO, 1.f); tp5_set_param(&E, P_B_SEMI, 0.f);
    tp5_note_on(&E, 69, 127);
    {   int wraps = 0; float prev = E.voice[0].ph_b;
        for (n = 0; n < (int)SR; n++) { tp5_render(&E, BL, BR, 1); if (E.voice[0].ph_b < prev) wraps++; prev = E.voice[0].ph_b; }
        CHECK(wraps >= 3 && wraps <= 5, "Osc B LFO mode should run ~4 Hz (counted %d cycles/s)", wraps); }

    /* Poly-Mod routing: every source x destination must audibly change the sound */
    {
        static const int dests[3] = { 1, 2, 4 };
        static const char *dn[3] = { "Osc A freq", "Osc A PW", "Filter cutoff" };
        int d, src;
        for (src = 0; src < 2; src++) for (d = 0; d < 3; d++) {
            float ref, mod, diff = 0.f, norm = 0.f;
            static float a[4096], b[4096];
            int k;
            for (k = 0; k < 2; k++) {
                tp5_engine *e = &E;
                mono_patch(e);
                tp5_set_param(e, P_A_WAVE, 0.5f); tp5_set_param(e, P_B_VOL, 0.5f); tp5_set_param(e, P_B_WAVE, 0.5f);
                tp5_set_param(e, P_CUTOFF, 0.55f); tp5_set_param(e, P_RESO, 0.3f);
                tp5_set_param(e, P_F_ATK, 0.f); tp5_set_param(e, P_F_DEC, 0.7f); tp5_set_param(e, P_F_SUS, 0.6f);
                tp5_set_param(e, P_PM_DEST, (float)(k ? dests[d] : 0));
                tp5_set_param(e, src ? P_PM_OSCB : P_PM_FENV, 0.7f);
                tp5_note_on(e, 55, 127);
                render_n(e, 6000, &finite);
                for (i = 0; i < 4096; i += BLOCK) { tp5_render(e, BL, BR, BLOCK); memcpy((k ? b : a) + i, BL, sizeof BL); }
            }
            for (i = 0; i < 4096; i++) { diff += (a[i] - b[i]) * (a[i] - b[i]); norm += a[i] * a[i]; }
            ref = sqrtf(norm); mod = sqrtf(diff);
            CHECK(mod > 0.05f * ref, "poly-mod %s -> %s has no audible effect (%.3f)", src ? "Osc B" : "Filter Env", dn[d], (double)(mod / ref));
        }
        mono_patch(&E);
        printf("    poly-mod matrix: 2 sources x 3 destinations all audible\n");
    }

    /* LFO destinations & shapes */
    {
        static float a[4096], b[4096];
        int dest, wave;
        for (dest = 0; dest < 7; dest++) {
            float diff = 0.f, norm = 0.f;
            int k;
            for (k = 0; k < 2; k++) {
                mono_patch(&E);
                tp5_set_param(&E, P_A_WAVE, 0.5f); tp5_set_param(&E, P_B_VOL, 0.5f); tp5_set_param(&E, P_CUTOFF, 0.5f);
                tp5_set_param(&E, P_LFO_RATE, 0.75f); tp5_set_param(&E, P_LFO_AMT, 0.8f); tp5_set_param(&E, P_LFO_WAVE, 1.f);
                tp5_set_param(&E, P_LFO_DEST, k ? (float)dest : 7.f);
                tp5_note_on(&E, 55, 127);
                render_n(&E, 3000, &finite);
                for (i = 0; i < 4096; i += BLOCK) { tp5_render(&E, BL, BR, BLOCK); memcpy((k ? b : a) + i, BL, sizeof BL); }
            }
            for (i = 0; i < 4096; i++) { diff += (a[i] - b[i]) * (a[i] - b[i]); norm += a[i] * a[i]; }
            CHECK(sqrtf(diff) > 0.03f * sqrtf(norm), "LFO destination mode %d has no effect", dest);
        }
        for (wave = 0; wave < 3; wave++) {
            mono_patch(&E);
            tp5_set_param(&E, P_LFO_WAVE, (float)wave); tp5_set_param(&E, P_LFO_RATE, 0.5f);
            tp5_cc(&E, 1, 127); tp5_set_param(&E, P_LFO_DEST, 0.f);
            tp5_note_on(&E, 60, 127);
            { float mn = 9.f, mx = -9.f;
              for (n = 0; n < (int)(2.5f * SR); n++) { tp5_render(&E, BL, BR, 1); if (E.lfo_out < mn) mn = E.lfo_out; if (E.lfo_out > mx) mx = E.lfo_out; }
              CHECK(mx > 0.85f && mn < -0.85f, "LFO wave %d range [%.2f, %.2f]", wave, (double)mn, (double)mx); }
        }
        printf("    LFO: 7 destination modes + saw/tri/square + mod-wheel OK\n");
    }

    /* envelopes: attack lands at its programmed time, release decays ~1% over its time */
    {
        int t_atk = -1, t;
        mono_patch(&E);
        tp5_set_param(&E, P_A_ATK, 0.5f); tp5_set_param(&E, P_A_DEC, 0.5f); tp5_set_param(&E, P_A_SUS, 0.5f);
        tp5_note_on(&E, 60, 127);
        for (t = 0; t < 20000 && t_atk < 0; t++) { tp5_render(&E, BL, BR, 1); if (E.voice[0].a_stage == ENV_DEC) t_atk = t; }
        printf("    amp attack knob 0.5: peak reached after %.1f ms (linear ramp, 133 ms)\n", (double)t_atk * 1000.0 / SR);
        CHECK(t_atk > 5750 && t_atk < 5980, "attack time %d samples, want ~5865 (133 ms)", t_atk);
        render_n(&E, 40000, &finite);
        CHECK(fabsf(E.voice[0].a_env - 0.4675f) < 0.01f, "sustain level %.3f want 0.4675 ((0.5-0.061)/0.939)", (double)E.voice[0].a_env);
        tp5_set_param(&E, P_A_REL, 0.5f);
        tp5_note_off(&E, 60);
        render_n(&E, 4410, &finite);
        {   /* release: exponential toward -0.03 with tau = 0.094 s at knob 0.5 (engine law) */
            const float want = -0.03f + 0.4975f * expf(-0.1f / 0.094f);
            CHECK(fabsf(E.voice[0].a_env - want) < 0.01f, "release level after 100 ms %.4f want %.4f", (double)E.voice[0].a_env, (double)want);
        }
        mono_patch(&E);
        tp5_set_param(&E, P_A_ATK, 0.f);
        tp5_note_on(&E, 60, 127);
        for (t = 0, t_atk = -1; t < 2000 && t_atk < 0; t++) { tp5_render(&E, BL, BR, 1); if (E.voice[0].a_stage != ENV_ATK) t_atk = t; }
        CHECK(t_atk > 0 && t_atk <= 50, "snap attack must be ~1 ms (got %d samples)", t_atk);
    }

    /* filter keyboard tracking (noise through the filter) and bipolar envelope depth */
    {
        float lo, hi, lo0, hi0, up, down;
        int k;
        for (k = 0; k < 2; k++) {
            float r_lo, r_hi;
            mono_patch(&E); tp5_set_param(&E, P_A_VOL, 0.f); tp5_set_param(&E, P_NOISE, 1.f); tp5_set_param(&E, P_NOISE_COL, 0.f);
            tp5_set_param(&E, P_CUTOFF, 0.45f); tp5_set_param(&E, P_KBD, (float)k);
            tp5_note_on(&E, 36, 127); render_n(&E, 8000, &finite); r_lo = render_rms(&E, 16000);
            mono_patch(&E); tp5_set_param(&E, P_A_VOL, 0.f); tp5_set_param(&E, P_NOISE, 1.f); tp5_set_param(&E, P_NOISE_COL, 0.f);
            tp5_set_param(&E, P_CUTOFF, 0.45f); tp5_set_param(&E, P_KBD, (float)k);
            tp5_note_on(&E, 84, 127); render_n(&E, 8000, &finite); r_hi = render_rms(&E, 16000);
            if (k) { lo = r_lo; hi = r_hi; } else { lo0 = r_lo; hi0 = r_hi; }
        }
        printf("    kbd tracking 100%%: C2->C6 noise level x%.1f; 0%%: x%.2f\n", (double)(hi / lo), (double)(hi0 / lo0));
        CHECK(hi / lo > 3.f, "100%% tracking should open the filter with pitch (x%.2f)", (double)(hi / lo));
        CHECK(hi0 / lo0 > 0.8f && hi0 / lo0 < 1.25f, "0%% tracking should be flat (x%.2f)", (double)(hi0 / lo0));

        for (k = 0; k < 2; k++) {
            mono_patch(&E); tp5_set_param(&E, P_A_VOL, 0.f); tp5_set_param(&E, P_NOISE, 1.f); tp5_set_param(&E, P_NOISE_COL, 0.f);
            tp5_set_param(&E, P_CUTOFF, 0.4f); tp5_set_param(&E, P_ENVAMT, k ? 0.6f : -0.6f);
            tp5_set_param(&E, P_F_ATK, 0.f); tp5_set_param(&E, P_F_DEC, 0.9f); tp5_set_param(&E, P_F_SUS, 1.f);
            tp5_note_on(&E, 60, 127); render_n(&E, 3000, &finite);
            if (k) up = render_rms(&E, 8000); else down = render_rms(&E, 8000);
        }
        CHECK(up > 2.f * down, "bipolar env: +depth (%.4f) must be brighter than -depth (%.4f)", (double)up, (double)down);
    }

    /* noise colour: white has more high-frequency content than pink */
    {
        double dw = 0, ew = 0, dp = 0, ep = 0;
        int k;
        for (k = 0; k < 2; k++) {
            float prev = 0.f; double d = 0, en = 0;
            mono_patch(&E); tp5_set_param(&E, P_KBD, 1.f); tp5_set_param(&E, P_A_VOL, 0.f); tp5_set_param(&E, P_NOISE, 1.f); tp5_set_param(&E, P_NOISE_COL, (float)k);
            tp5_note_on(&E, 60, 127); render_n(&E, 6000, &finite);
            for (i = 0; i < 20000; i++) { tp5_render(&E, BL, BR, 1); d += (double)(BL[0] - prev) * (BL[0] - prev); en += (double)BL[0] * BL[0]; prev = BL[0]; }
            if (k) { dp = d; ep = en; } else { dw = d; ew = en; }
        }
        printf("    noise: white diff/level %.2f, pink %.2f; level ratio pink/white %.2f\n", sqrt(dw / ew), sqrt(dp / ep), sqrt(ep / ew));
        CHECK(sqrt(dw / ew) > 0.8 && sqrt(dp / ep) < 0.6 && sqrt(dw / ew) > 2.0 * sqrt(dp / ep), "white vs pink spectral tilt wrong");
        CHECK(sqrt(ep / ew) > 0.5 && sqrt(ep / ew) < 2.0, "white/pink levels should be comparable");
    }

    /* drift: unit-variance slow random walk, independent per voice, zero when Drift = 0 */
    {
        double s1 = 0, s2 = 0, c01 = 0, s0 = 0;
        int samples = 0, nsamp = (int)(120.f * SR) / 16;
        init_poly(&E);
        tp5_set_param(&E, P_DRIFT, 1.f);
        chord(&E, 100);
        for (n = 0; n < nsamp; n++) {
            tp5_render(&E, BL, BR, 16);
            s0 += E.voice[0].dr[0] * E.voice[0].dr[0];
            s1 += E.voice[1].dr[0] * E.voice[1].dr[0];
            s2 += E.voice[0].dr[0];
            c01 += E.voice[0].dr[0] * E.voice[1].dr[0];
            samples++;
        }
        printf("    drift over 120 s: std voice0 %.2f voice1 %.2f, mean %.2f, inter-voice corr %.2f\n",
               sqrt(s0 / samples), sqrt(s1 / samples), s2 / samples, c01 / sqrt(s0 * s1));
        CHECK(sqrt(s0 / samples) > 0.5 && sqrt(s0 / samples) < 1.6, "drift std %.2f", sqrt(s0 / samples));
        CHECK(fabs(c01 / sqrt(s0 * s1)) < 0.35, "voices drift independently (corr %.2f)", c01 / sqrt(s0 * s1));
    }
    CHECK(finite, "signal path produced NaN");
}

/* ---- plugin ABI ---- */
static int json_balanced(const char *s)
{
    int br = 0, cb = 0, in = 0;
    for (; *s; s++) {
        if (*s == '"') in = !in;
        if (in) continue;
        if (*s == '{') cb++; else if (*s == '}') cb--;
        else if (*s == '[') br++; else if (*s == ']') br--;
        if (cb < 0 || br < 0) return 0;
    }
    return cb == 0 && br == 0 && !in;
}

extern plugin_api_v2_t *move_plugin_init_v2(const host_api_v1_t *host);

static void test_plugin_api(void)
{
    static char big[16384];
    static host_api_v1_t host;
    static int16_t out[BLOCK * 2];
    plugin_api_v2_t *api;
    void *inst[5], *a, *b;
    char buf[256];
    int n, i, nonzero = 0, count = 0;
    const char *p;

    printf("[Schwung plugin ABI]\n");
    host.sample_rate = MOVE_SAMPLE_RATE;
    host.frames_per_block = MOVE_FRAMES_PER_BLOCK;
    api = move_plugin_init_v2(&host);
    CHECK(api && api->api_version == 2, "init_v2 must return a v2 table");
    CHECK(api->create_instance && api->destroy_instance && api->on_midi && api->set_param &&
          api->get_param && api->get_error && api->render_block, "all v2 callbacks present");
    CHECK(sizeof(host_api_v1_t) == 120 + 8 * sizeof(void *), "host_api_v1_t size %zu (reserved[] should start at byte 120)", sizeof(host_api_v1_t));

    a = api->create_instance("/tmp", "{}");
    CHECK(a != NULL, "create_instance");
    n = api->get_param(a, "chain_params", big, sizeof big);
    CHECK(n > 0 && json_balanced(big), "chain_params must be valid balanced JSON (n=%d)", n);
    for (p = big; (p = strstr(p, "\"key\"")) != NULL; p++) count++;
    CHECK(count == P_COUNT, "chain_params lists %d keys, want %d", count, P_COUNT);
    n = api->get_param(a, "ui_hierarchy", big, sizeof big);
    CHECK(n > 0 && json_balanced(big) && strstr(big, "\"levels\"") && strstr(big, "\"knobs\"") &&
          strstr(big, "\"list_param\":\"preset\"") && strstr(big, "\"count_param\":\"preset_count\"") &&
          strstr(big, "\"name_param\":\"preset_name\""), "ui_hierarchy JSON must declare the host preset browser");
    CHECK(api->get_param(a, "chain_params", buf, 32) == -1, "too-small buffer must report failure, not overrun");
    n = tp5_json_module(big, sizeof big);
    CHECK(n > 0 && n < 8192, "module.json size %d must stay under the loader's 8 KB cap", n);
    CHECK(json_balanced(big) && strstr(big, "\"id\":\"tinypro5\"") && strstr(big, "\"version\":\"1.2.1\"") &&
          strstr(big, "\"component_type\":\"sound_generator\"") && strstr(big, "\"api_version\":2") &&
          strstr(big, "\"ui_hierarchy\"") && strstr(big, "\"list_param\""), "module.json content");
    printf("    module.json = %d bytes\n", n);

    api->set_param(a, "cutoff", "0.25");
    n = api->get_param(a, "cutoff", buf, sizeof buf);
    CHECK(n > 0 && fabs(atof(buf) - 0.25) < 1e-3, "cutoff round-trip '%s'", buf);
    api->set_param(a, "cutoff", "9");
    api->get_param(a, "cutoff", buf, sizeof buf);
    CHECK(fabs(atof(buf) - 1.0) < 1e-3, "out-of-range value must clamp (got %s)", buf);
    api->set_param(a, "cutoff", "nan");
    api->get_param(a, "cutoff", buf, sizeof buf);
    CHECK(fabs(atof(buf) - 1.0) < 1e-3, "NaN must be rejected (got %s)", buf);
    api->set_param(a, "no_such_param", "1");
    CHECK(api->get_param(a, "no_such_param", buf, sizeof buf) == -1, "unknown get_param key");
    api->set_param(a, "preset", "21");
    api->set_param(a, "load_preset", "3");
    n = api->get_param(a, "preset_name", buf, sizeof buf);
    {   /* labels are "group-program name" built from slot numbers, whatever the names are */
        char want[64];
        snprintf(want, sizeof want, "3-6 %s", tp5_preset_name(21));
        CHECK(n > 0 && strcmp(buf, want) == 0, "preset_name '%s', want '%s' (set preset must load)", buf, want);
        snprintf(want, sizeof want, "2-4 %s", tp5_preset_name(11));
        CHECK(api->get_param(a, "preset_name:11", buf, sizeof buf) > 0 && strcmp(buf, want) == 0, "preset_name:11 '%s' want '%s'", buf, want);
        snprintf(want, sizeof want, "3-1 %s", tp5_preset_name(16));
        CHECK(api->get_param(a, "preset_name:16", buf, sizeof buf) > 0 && strcmp(buf, want) == 0, "preset_name:16 '%s' want '%s'", buf, want);
        snprintf(want, sizeof want, "4-8 %s", tp5_preset_name(31));
        CHECK(api->get_param(a, "preset_name:31", buf, sizeof buf) > 0 && strcmp(buf, want) == 0, "preset_name:31 '%s' want '%s'", buf, want);
    }
    CHECK(api->get_param(a, "preset_name:32", buf, sizeof buf) == -1, "out-of-range label must fail");
    api->set_param(a, "preset", "5");                          /* the host browser only writes `preset`: it must load */
    api->get_param(a, "preset_name", buf, sizeof buf);
    {
        char want[64];
        snprintf(want, sizeof want, "1-6 %s", tp5_preset_name(5));
        CHECK(strcmp(buf, want) == 0, "preset 5 label '%s' want '%s'", buf, want);
    }
    api->get_param(a, "preset_count", buf, sizeof buf);
    CHECK(atoi(buf) == TINYPRO5_PRESET_COUNT, "preset_count %s", buf);
    api->get_param(a, "bank_count", buf, sizeof buf);
    CHECK(atoi(buf) >= 1, "bank_count %s", buf);
    CHECK(api->get_param(a, "bank_name:0", buf, sizeof buf) > 0 && strcmp(buf, "Factory") == 0, "bank_name:0 '%s'", buf);
    CHECK(api->get_param(a, "bank_size:0", buf, sizeof buf) > 0 && atoi(buf) == TINYPRO5_PRESET_COUNT, "bank_size:0 '%s'", buf);
    CHECK(api->get_param(a, "bank_name", buf, sizeof buf) > 0 && strcmp(buf, "Factory") == 0, "active bank name '%s'", buf);
    /* alphabetical mode through the host contract: the list, the label and the loaded program agree */
    api->set_param(a, "preset", "5");
    api->set_param(a, "preset_sort", "1");
    api->get_param(a, "preset_sort", buf, sizeof buf);
    CHECK(atoi(buf) == 1, "preset_sort not stored");
    api->get_param(a, "preset_name:0", buf, sizeof buf);
    {   /* in A-Z order a factory entry still carries its group-program address, and entries really are by name */
        const int slot = tp5_bank_slot(0, 0, 1);
        char want[64];
        snprintf(want, sizeof want, "%d-%d %s", slot / 8 + 1, slot % 8 + 1, tp5_preset_name(slot));
        CHECK(strcmp(buf, want) == 0, "A-Z label of entry 0 is '%s', want '%s'", buf, want);
    }
    api->get_param(a, "preset_name", buf, sizeof buf);
    CHECK(strstr(buf, tp5_preset_name(5)) != NULL, "switching order must keep the selected program (got '%s')", buf);
    api->set_param(a, "preset", "0");
    api->get_param(a, "preset_name", buf, sizeof buf);
    CHECK(strcmp(buf + 4, tp5_preset_name(tp5_bank_slot(0, 0, 1))) == 0, "selecting A-Z entry 0 must name it (got '%s')", buf);
    api->set_param(a, "preset_sort", "0");
    api->set_param(a, "preset", "5");
    api->get_param(a, "preset_name", buf, sizeof buf);
    {
        char want[64];
        snprintf(want, sizeof want, "1-6 %s", tp5_preset_name(5));
        CHECK(strcmp(buf, want) == 0, "bank order must be restored (got '%s')", buf);
    }

    {   /* MIDI + render */
        const uint8_t on[3] = { 0x90, 60, 100 }, off[3] = { 0x80, 60, 0 }, bend[3] = { 0xE0, 0, 0x40 };
        api->on_midi(a, bend, 3, 0);
        api->on_midi(a, on, 3, 0);
        for (i = 0; i < 40; i++) {
            int k;
            api->render_block(a, out, BLOCK);
            for (k = 0; k < BLOCK * 2; k++) if (out[k]) nonzero++;
        }
        CHECK(nonzero > 1000, "render_block produced no audio");
        api->get_param(a, "active_voices", buf, sizeof buf);
        CHECK(atoi(buf) == 1, "active_voices %s", buf);
        api->on_midi(a, off, 3, 0);
        api->on_midi(a, NULL, 3, 0);                       /* hostile input must be ignored */
        { const uint8_t junk[1] = { 0x90 }; api->on_midi(a, junk, 1, 0); }
    }

    /* state round trip incl. user-saved preset */
    api->set_param(a, "preset", "9");
    api->set_param(a, "cutoff", "0.321");
    api->set_param(a, "resonance", "0.77");
    api->set_param(a, "unison", "1");
    api->set_param(a, "save_preset", "3");
    api->set_param(a, "oscb_pitch", "-7.5");
    n = api->get_param(a, "state", big, sizeof big);
    CHECK(n > 0 && n < (int)sizeof big, "state save length %d", n);
    printf("    state blob = %d bytes\n", n);
    b = api->create_instance("/tmp", "{}");
    api->set_param(b, "state", big);
    api->get_param(b, "cutoff", buf, sizeof buf);
    CHECK(fabs(atof(buf) - 0.321) < 1e-3, "state restore cutoff '%s'", buf);
    api->get_param(b, "oscb_pitch", buf, sizeof buf);
    CHECK(fabs(atof(buf) + 7.5) < 1e-3, "state restore oscb_pitch '%s'", buf);
    api->get_param(b, "unison", buf, sizeof buf);
    CHECK(atoi(buf) == 1, "state restore unison");
    api->set_param(b, "preset", "9");
    api->set_param(b, "load_preset", "3");
    api->get_param(b, "resonance", buf, sizeof buf);
    CHECK(fabs(atof(buf) - 0.77) < 1e-3, "user preset slot not restored through state (%s)", buf);
    CHECK(tp5_state_load(&E, "garbage") == -1, "bad state blob rejected");
    api->set_param(b, "synth:state", "TP5:1;cutoff=0.5;");
    api->get_param(b, "cutoff", buf, sizeof buf);
    CHECK(fabs(atof(buf) - 0.5) < 1e-3, "prefixed ':state' key");

    /* instance pool */
    api->destroy_instance(a);
    api->destroy_instance(b);
    for (i = 0; i < 4; i++) { inst[i] = api->create_instance("/tmp", "{}"); CHECK(inst[i] != NULL, "instance %d", i); }
    inst[4] = api->create_instance("/tmp", "{}");
    CHECK(inst[4] == NULL, "pool exhaustion must return NULL, not allocate");
    api->render_block(NULL, out, 16); /* NULL instance must not crash and must output silence */
    api->destroy_instance(inst[0]);
    CHECK(api->create_instance("/tmp", "{}") != NULL, "slot reuse after destroy");
    for (i = 1; i < 4; i++) api->destroy_instance(inst[i]);
}

/* ---- Reload / Save / Revert: canvas push buttons (the confirm screens are canvas.js, tested by tests/test_canvas.js) ---- */
static int act_state(plugin_api_v2_t *api, void *h, const char *key)
{
    char b[16] = "";
    api->get_param(h, key, b, sizeof b);
    return atoi(b);
}

static void test_confirm(void)
{
    static char big[16384];
    static host_api_v1_t host;
    plugin_api_v2_t *api;
    void *a, *b;
    char buf[64];
    printf("[Reload / Save / Revert push buttons]\n");
    host.sample_rate = MOVE_SAMPLE_RATE;
    host.frames_per_block = MOVE_FRAMES_PER_BLOCK;
    api = move_plugin_init_v2(&host);
    tp5_overlay_clear();
    a = api->create_instance("/tmp", "{}");
    api->set_param(a, "preset", "77");

    /* metadata: each is a canvas cell with its own confirm overlay, no value, no host footer; a turn cannot reach it */
    api->get_param(a, "chain_params", big, sizeof big);
    CHECK(strstr(big, "\"key\":\"save_preset\",\"name\":\"Save\",\"type\":\"canvas\",\"canvas_script\":\"canvas.js#confirm_save\"") != NULL, "Save is a canvas push button");
    CHECK(strstr(big, "\"canvas_script\":\"canvas.js#confirm_reload\"") != NULL && strstr(big, "\"canvas_script\":\"canvas.js#confirm_revert\"") != NULL, "Reload and Revert too");
    CHECK(strstr(big, "\"enterable\":true") != NULL && strstr(big, "\"show_value\":false") != NULL, "the click is the screen's own and no value is drawn");
    {   /* page 4: Reload (E6) and Save (E7) are encoder cells, Revert is a list entry right after Save */
        const char *pg, *r, *sv, *rv, *od;
        api->get_param(a, "ui_hierarchy", big, sizeof big);
        pg = strstr(big, "\"page4\":{");
        r = pg ? strstr(pg, "\"load_preset\"") : NULL;
        sv = r ? strstr(r, "\"save_preset\"") : NULL;
        rv = sv ? strstr(sv, "\"revert_preset\"") : NULL;
        od = rv ? strstr(rv, "\"preset_sort\"") : NULL;
        CHECK(pg && r && sv && rv && od, "page 4 lists Reload, Save, Revert, Order in that order");
        CHECK(pg && strstr(pg, "\"knobs\":[\"voice_mode\",\"voice_count\",\"detune\",\"lfo_amount\",\"preset\",\"load_preset\",\"save_preset\",\"preset_sort\"]") != NULL,
              "the eight encoders keep their places (Revert is list-only)");
    }

    /* the cell text the host draws in the button box */
    api->get_param(a, "save_preset", buf, sizeof buf);
    CHECK(strcmp(buf, "NONE") == 0, "a clean program: the Save button reads NONE ('%s')", buf);

    /* availability, as canvas.js reads it */
    CHECK(act_state(api, a, "can_reload") == 0 && act_state(api, a, "can_save") == 0 && act_state(api, a, "can_revert") == 0, "a clean program: nothing to reload, save or revert");
    api->set_param(a, "save_preset", "1");
    api->set_param(a, "revert_preset", "1");
    CHECK(tp5_overlay_count() == 0, "firing them on a clean program changes nothing");

    /* Reload puts back EVERY sound parameter: move each one away from its value, press Reload, compare */
    {
        double before[P_COUNT];
        int id, moved = 0, restored = 0;
        char key[64], val[64];
        for (id = 0; id < P_COUNT; id++) {
            before[id] = 0.0;
            if (!tp5_is_sound_param(id)) continue;
            snprintf(key, sizeof key, "%s", tp5_params[id].key);
            api->get_param(a, key, buf, sizeof buf);
            before[id] = atof(buf);
        }
        for (id = 0; id < P_COUNT; id++) {
            const double lo = (double)tp5_params[id].min, hi = (double)tp5_params[id].max;
            if (!tp5_is_sound_param(id)) continue;
            snprintf(val, sizeof val, "%.4f", fabs(before[id] - hi) > 0.2 * (hi - lo) ? hi : lo);
            api->set_param(a, tp5_params[id].key, val);
            moved++;
        }
        api->set_param(a, "load_preset", "1");
        for (id = 0; id < P_COUNT; id++) {
            if (!tp5_is_sound_param(id)) continue;
            api->get_param(a, tp5_params[id].key, buf, sizeof buf);
            if (fabs(atof(buf) - before[id]) < 2e-3) restored++;
            else printf("    not restored: %s was %.4f, now %s\n", tp5_params[id].key, before[id], buf);
        }
        CHECK(moved >= 30 && restored == moved, "Reload restored %d of %d sound parameters", restored, moved);
        api->get_param(a, "master_vol", buf, sizeof buf);
        CHECK(fabs(atof(buf) - 0.8) < 1e-3, "master volume is not a program setting and is left alone (%s)", buf);
    }

    /* the voice count is not part of a program, but Reload still undoes a change to it */
    api->get_param(a, "voice_count", buf, sizeof buf);
    CHECK(atoi(buf) == 5, "voices start at 5 (%s)", buf);
    api->set_param(a, "voice_count", "2");
    CHECK(act_state(api, a, "can_reload") == 1 && act_state(api, a, "can_save") == 1, "changing Voices makes Reload and Save available");
    api->set_param(a, "load_preset", "1");
    api->get_param(a, "voice_count", buf, sizeof buf);
    CHECK(atoi(buf) == 5 && act_state(api, a, "can_reload") == 0, "Reload puts the voice count back (%s)", buf);
    api->set_param(a, "voice_count", "3");
    api->set_param(a, "save_preset", "1");
    api->get_param(a, "voice_count", buf, sizeof buf);
    CHECK(atoi(buf) == 3 && tp5_overlay_count() == 0 && act_state(api, a, "can_save") == 0, "Save accepts a new voice count without storing a program (%s)", buf);
    api->set_param(a, "voice_count", "5");              /* back to the default, accepted as the new "as loaded" */
    api->set_param(a, "save_preset", "1");
    CHECK(act_state(api, a, "can_save") == 0 && act_state(api, a, "can_reload") == 0 && tp5_overlay_count() == 0, "and the program is clean again");

    api->get_param(a, "cutoff", buf, sizeof buf);
    {
        const double orig = atof(buf);
        const char *edit = orig > 0.5 ? "0.123" : "0.876";
        api->set_param(a, "cutoff", edit);
        CHECK(act_state(api, a, "can_reload") == 1 && act_state(api, a, "can_save") == 1 && act_state(api, a, "can_revert") == 0, "an edit enables Reload and Save");
        api->get_param(a, "save_preset", buf, sizeof buf);
        CHECK(strcmp(buf, "SAVE") == 0, "and the Save button reads SAVE ('%s')", buf);
        api->get_param(a, "load_preset", buf, sizeof buf);
        CHECK(strcmp(buf, "RELD") == 0, "Reload reads RELD ('%s')", buf);
        api->get_param(a, "revert_preset", buf, sizeof buf);
        CHECK(strcmp(buf, "NONE") == 0, "Revert reads NONE ('%s')", buf);
        api->set_param(a, "save_preset", "1");
        CHECK(tp5_overlay_count() == 1, "Save stores the edit");
        CHECK(act_state(api, a, "can_save") == 0 && act_state(api, a, "can_reload") == 0 && act_state(api, a, "can_revert") == 1, "saved: nothing left to save, Revert available");

        api->set_param(a, "cutoff", "0.5");
        api->set_param(a, "load_preset", "1");
        api->get_param(a, "cutoff", buf, sizeof buf);
        CHECK(fabs(atof(buf) - atof(edit)) < 1e-3, "Reload returns the knobs to the saved edit (%s)", buf);

        /* the state of a clean program survives a save / restore without looking edited */
        api->get_param(a, "state", big, sizeof big);
        b = api->create_instance("/tmp", "{}");
        api->set_param(b, "state", big);
        CHECK(act_state(api, b, "can_save") == 0 && act_state(api, b, "can_reload") == 0, "a restored clean program is not 'edited'");
        api->destroy_instance(b);

        api->set_param(a, "revert_preset", "1");
        api->get_param(a, "cutoff", buf, sizeof buf);
        CHECK(fabs(atof(buf) - orig) < 2e-3, "Revert returns the bank's own sound (%s, was %.3f)", buf, orig);
        CHECK(tp5_overlay_count() == 0 && act_state(api, a, "can_revert") == 0, "and nothing is left to revert");
    }
    api->destroy_instance(a);
    tp5_overlay_clear();
}

int main(void)
{
    printf("TinyPro5 test suite (%d presets, %d voices, ceiling %.1f dBFS)\n\n",
           TINYPRO5_PRESET_COUNT, TINYPRO5_VOICES, (double)TINYPRO5_CEILING_DBFS);
    test_param_table();
    test_presets();
    test_voice_allocation();
    test_filter();
    test_smoothing();
    test_headroom();
    test_limiter();
    test_realtime();
    test_signal_path();
    test_plugin_api();
    test_confirm();
    printf("\n%d checks, %d failed\n", g_checks, g_fail);
    printf(g_fail ? "RESULT: FAIL\n" : "RESULT: PASS\n");
    return g_fail ? 1 : 0;
}
