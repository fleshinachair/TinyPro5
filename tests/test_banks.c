/*
 * Custom banks: inflate / zip / .h2p / .syx parsing, the Repro-5 and Prophet-5 mappings, the bank registry, bank switching
 * in the engine, and folder scanning. Only SYNTHETIC patches are used (tests/bank_fixture.h, made by tools/make_bank_fixture.py,
 * and made-up .syx programs built in this file); no third-party patch data is read. Run from the repo root.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <direct.h>
#define MKDIR(p) _mkdir(p)
#else
#include <sys/stat.h>
#define MKDIR(p) mkdir((p), 0755)
#endif

#include "../src/bank.h"
#include "../src/meta.h"
#include "bank_fixture.h"

static int g_checks, g_fail;
#define CHECK(cond, ...) do { g_checks++; if (!(cond)) { g_fail++; printf("    FAIL line %d: ", __LINE__); \
    printf(__VA_ARGS__); printf("\n"); } } while (0)

/* deterministic made-up native patches (they exercise pack / unpack / mapping; they are not any instrument's programs) */
static void fill_raw(int i, uint8_t *r)
{
    int k;
    for (k = 0; k < 55; k++) r[k] = (uint8_t)((i * 37 + k * 11 + 5) % 128);
}

static tp5_engine E;
static float BL[128], BR[128];
static uint8_t g_scratch[64 * 1024];

/* ---- helpers ---------------------------------------------------------------------------------------------------- */
typedef struct { int n; char names[16][64]; size_t lens[16]; char text[16][8192]; } collect;

static int collect_cb(const char *name, const uint8_t *d, size_t len, void *u)
{
    collect *c = (collect *)u;
    if (c->n < 16 && len < sizeof c->text[0]) {
        strncpy(c->names[c->n], name, 63);
        memcpy(c->text[c->n], d, len);
        c->text[c->n][len] = 0;
        c->lens[c->n] = len;
        c->n++;
    }
    return 0;
}

static float eff(const tp5_engine *e, int id) { return tp5_get_param(e, id); }

/* load slot `slot` of `bank` into a fresh engine */
static void load(int bank, int slot)
{
    tp5_init(&E, 44100.f);
    tp5_set_param(&E, P_BANK, (float)bank);
    tp5_load_preset(&E, slot);
}

static int render_ok(void)
{
    int n, k, finite = 1;
    float pk = 0.f;
    tp5_set_param(&E, P_VOLUME, 1.f);
    tp5_note_on(&E, 48, 110); tp5_note_on(&E, 55, 110); tp5_note_on(&E, 60, 110);
    for (n = 0; n < 44100 / 128; n++) {
        tp5_render(&E, BL, BR, 128);
        for (k = 0; k < 128; k++) {
            if (!isfinite(BL[k]) || !isfinite(BR[k])) finite = 0;
            if (fabsf(BL[k]) > pk) pk = fabsf(BL[k]);
        }
    }
    return finite && pk > 0.0005f && pk < 1.f;
}

static void put_text(const char *path, const void *d, size_t n)
{
    FILE *f = fopen(path, "wb");
    if (f) { fwrite(d, 1, n, f); fclose(f); }
}

/* a program dump frame: F0 01 32 02 group program <152 packed bytes> F7 */
static size_t make_frame(uint8_t *out, const uint8_t *raw133, int g, int p)
{
    size_t k, j, o = 0;
    out[o++] = 0xF0; out[o++] = 0x01; out[o++] = 0x32; out[o++] = 0x02; out[o++] = (uint8_t)g; out[o++] = (uint8_t)p;
    for (k = 0; k < 19; k++) {
        uint8_t msb = 0;
        for (j = 0; j < 7; j++) if (k * 7 + j < 133 && (raw133[k * 7 + j] & 0x80)) msb |= (uint8_t)(1u << j);
        out[o++] = msb;
        for (j = 0; j < 7; j++) out[o++] = (uint8_t)((k * 7 + j < 133 ? raw133[k * 7 + j] : 0) & 0x7F);
    }
    out[o++] = 0xF7;
    return o;
}

/* ---- tests ---------------------------------------------------------------------------------------------------------- */
static void test_inflate_zip(void)
{
    collect c;
    int r, i;
    uint8_t bad[sizeof k_fixture_zip];
    size_t got = 0;
    uint8_t out[64];
    /* hand-made streams: a stored block and a fixed-Huffman block */
    static const uint8_t stored[] = { 0x01, 0x05, 0x00, 0xFA, 0xFF, 'h', 'e', 'l', 'l', 'o' };
    static const uint8_t fixed[] = { 0xCB, 0x48, 0xCD, 0xC9, 0xC9, 0x07, 0x00 };       /* "hello" */
    printf("[zip / inflate]\n");
    CHECK(tp5_inflate(stored, sizeof stored, out, sizeof out, &got) == 0 && got == 5 && memcmp(out, "hello", 5) == 0, "stored block");
    CHECK(tp5_inflate(fixed, sizeof fixed, out, sizeof out, &got) == 0 && got == 5 && memcmp(out, "hello", 5) == 0, "fixed block");
    CHECK(tp5_inflate(fixed, sizeof fixed, out, 3, &got) != 0, "output overflow must fail cleanly");
    CHECK(tp5_inflate(fixed, 3, out, sizeof out, &got) != 0, "truncated input must fail cleanly");
    CHECK(tp5_inflate((const uint8_t *)"\xFF\xFF\xFF", 3, out, sizeof out, &got) != 0, "garbage must fail cleanly");

    memset(&c, 0, sizeof c);
    r = tp5_zip_walk(k_fixture_zip, sizeof k_fixture_zip, ".h2p", g_scratch, sizeof g_scratch, collect_cb, &c);
    CHECK(r == 6 && c.n == 6, "fixture has 6 .h2p entries (deflated and stored), got %d / %d", r, c.n);
    CHECK(strcmp(c.names[0], "Synthetic/01 Leads/Zed Lead.h2p") == 0, "first entry '%s'", c.names[0]);
    CHECK(strstr(c.text[0], "#AM=Repro-5") != NULL && strstr(c.text[0], "Cutoff=70.00") != NULL, "inflated text is intact");
    CHECK(strstr(c.text[c.n - 1], "Cutoff=90.00") != NULL, "stored entry is intact");
    memset(&c, 0, sizeof c);
    CHECK(tp5_zip_walk(k_fixture_zip, sizeof k_fixture_zip, ".TXT", g_scratch, sizeof g_scratch, collect_cb, &c) == 1, "suffix match is case-insensitive");
    CHECK(tp5_zip_walk(k_fixture_zip, sizeof k_fixture_zip, ".h2p", g_scratch, 20, collect_cb, &c) == 0, "entries larger than the scratch buffer are skipped");

    CHECK(tp5_zip_walk(k_fixture_zip, 10, ".h2p", g_scratch, sizeof g_scratch, collect_cb, &c) == -1, "tiny file is not a zip");
    CHECK(tp5_zip_walk(k_fixture_zip, sizeof k_fixture_zip - 30, ".h2p", g_scratch, sizeof g_scratch, collect_cb, &c) < 0, "truncated archive is rejected");
    for (i = 0; i < 400; i++) {                                    /* random corruption: never crashes, never overruns */
        size_t at = (size_t)((i * 2654435761u) % sizeof bad);
        memcpy(bad, k_fixture_zip, sizeof bad);
        bad[at] ^= (uint8_t)(1u << (i & 7));
        bad[(at * 7 + 13) % sizeof bad] ^= 0x5A;
        memset(&c, 0, sizeof c);
        r = tp5_zip_walk(bad, sizeof bad, ".h2p", g_scratch, sizeof g_scratch, collect_cb, &c);
        if (r < -3 || r > 16) CHECK(0, "corrupt zip returned %d", r);
    }
    CHECK(1, "400 corrupted archives survived");
}

static void test_h2p(void)
{
    collect c;
    tp5_h2p h;
    tp5_kv kv[TP5_BANK_KV_PER_PRESET];
    int n, i;
    printf("[.h2p parser and Repro-5 mapping]\n");
    memset(&c, 0, sizeof c);
    tp5_zip_walk(k_fixture_zip, sizeof k_fixture_zip, ".h2p", g_scratch, sizeof g_scratch, collect_cb, &c);

    CHECK(tp5_h2p_parse(c.text[0], c.lens[0], &h) == 1, "Repro-5 patch must parse");
    CHECK(h.has[H_FLT_CUT] && fabsf(h.v[H_FLT_CUT] - 70.f) < 1e-4f, "cutoff 70 read as %.3f", (double)h.v[H_FLT_CUT]);
    CHECK(h.has[H_A_OCT] && h.v[H_A_OCT] == 1.f, "keys after the binary tail must be ignored (Oct=%.0f)", (double)h.v[H_A_OCT]);
    CHECK(!h.has[H_FENV_ATK] || h.v[H_FENV_ATK] == 0.f, "attack 0 is read");
    CHECK(tp5_h2p_parse(c.text[3], c.lens[3], &h) == 0, "another instrument (#AM=Zebra2) must be rejected");
    CHECK(tp5_h2p_parse(c.text[4], c.lens[4], &h) == 0, "a Repro-5 file with no recognised keys is not a patch");
    CHECK(tp5_h2p_parse("", 0, &h) == 0 && tp5_h2p_parse(NULL, 0, &h) == 0, "empty input");
    CHECK(tp5_h2p_parse("\x01\x02\xFF binary \x00 junk", 17, &h) == 0, "binary junk");
    {   /* CRLF line ends, no header, odd numbers, a very long line */
        static const char t[] = "#cm=Filter\r\nCutoff=1e1\r\nReso=abc\r\nKeyAmt=-5\r\nEnvAmt=  42.5  \r\nVMod=\r\n";
        char big[20000];
        CHECK(tp5_h2p_parse(t, sizeof t - 1, &h) == 1 && fabsf(h.v[H_FLT_CUT] - 10.f) < 1e-4f && !h.has[H_FLT_RES] &&
              fabsf(h.v[H_FLT_KEY] + 5.f) < 1e-4f && fabsf(h.v[H_FLT_ENV] - 42.5f) < 1e-4f && !h.has[H_FLT_VMOD],
              "number parsing: cutoff %.2f key %.2f env %.2f", (double)h.v[H_FLT_CUT], (double)h.v[H_FLT_KEY], (double)h.v[H_FLT_ENV]);
        memset(big, 'x', sizeof big);
        memcpy(big, "#cm=Filter\nCutoff=", 18);
        CHECK(tp5_h2p_parse(big, sizeof big, &h) == 0, "an endless line must not crash");
    }
    /* defaults for missing keys: an almost empty patch leaves everything else at the engine default */
    {
        static const char t[] = "#AM=Repro-5\n#cm=Filter\nCutoff=33\n";
        tp5_h2p_parse(t, sizeof t - 1, &h);
        n = tp5_h2p_to_kv(&h, kv, TP5_BANK_KV_PER_PRESET);
        CHECK(n == 1 && kv[0].id == P_CUTOFF && fabsf(kv[0].v - 0.33f) < 1e-4f, "one key in, one parameter out (n=%d)", n);
        CHECK(tp5_h2p_to_kv(&h, kv, 0) == 0, "zero capacity");
    }

    /* full mapping through the engine */
    CHECK(tp5_bank_begin("Synthetic") == 1, "first custom bank is 1");
    for (i = 0; i < c.n; i++) {
        if (!tp5_h2p_parse(c.text[i], c.lens[i], &h)) continue;
        n = tp5_h2p_to_kv(&h, kv, TP5_BANK_KV_PER_PRESET);
        {
            char nm[64];
            const char *b = strrchr(c.names[i], '/');
            strcpy(nm, b ? b + 1 : c.names[i]);
            nm[strlen(nm) - 4] = 0;
            tp5_bank_add(1, nm, kv, n);
        }
    }
    CHECK(tp5_bank_end(1) == 4, "fixture yields 4 usable presets (the Zebra2 and the empty file are dropped), got %d", tp5_bank_size(1));
    CHECK(strcmp(tp5_bank_preset_name(1, 0), "Zed Lead") == 0 && strcmp(tp5_bank_preset_name(1, 3), "Stored") == 0, "names come from file names");

    load(1, 0);   /* Zed Lead */
    CHECK(fabsf(eff(&E, P_CUTOFF) - 0.70f) < 1e-4f && fabsf(eff(&E, P_RESO) - 0.60f) < 1e-4f, "cutoff/reso");
    CHECK(fabsf(eff(&E, P_ENVAMT) - 0.40f) < 1e-4f && fabsf(eff(&E, P_KBD) - 1.f) < 1e-4f, "env amount / key tracking");
    CHECK(fabsf(eff(&E, P_F_SUS) - 0.30f) < 1e-4f && fabsf(eff(&E, P_A_DEC) - 0.60f) < 1e-4f && fabsf(eff(&E, P_A_SUS) - 0.80f) < 1e-4f, "envelopes");
    CHECK(fabsf(eff(&E, P_A_SEMI) + 1.994f) < 1e-3f && fabsf(eff(&E, P_B_SEMI) - 10.006f) < 1e-3f, "Repro-5 pitch law: A %.3f B %.3f", (double)eff(&E, P_A_SEMI), (double)eff(&E, P_B_SEMI));
    CHECK(eff(&E, P_SYNC) == 1.f && fabsf(eff(&E, P_A_VOL) - 0.4f) < 1e-4f && fabsf(eff(&E, P_B_VOL) - 1.f) < 1e-4f && eff(&E, P_B_WAVE) == 0.f, "sync / mixer / saw B");
    CHECK(render_ok(), "Zed Lead renders");
    load(1, 1);   /* alpha Pad */
    CHECK(eff(&E, P_PM_DEST) == 5.f && fabsf(eff(&E, P_PM_FENV) - 0.10f) < 1e-4f && eff(&E, P_A_WAVE) == 1.f && fabsf(eff(&E, P_ENVAMT) + 0.40f) < 1e-4f, "poly-mod destinations, pulse A, negative env polarity");
    CHECK(render_ok(), "alpha Pad renders");
    load(1, 2);   /* Mid Bass: octave 0, semitone 6, fixed-pitch B, glide, no tracking */
    CHECK(fabsf(eff(&E, P_A_SEMI) + 7.994f) < 1e-3f && eff(&E, P_B_KBD) == 0.f && fabsf(eff(&E, P_B_SEMI) - 12.04f) < 1e-3f && fabsf(eff(&E, P_GLIDE) - 0.4f) < 1e-4f && eff(&E, P_KBD) == 0.f, "octave/semitone, fixed B, glide");
    CHECK(render_ok(), "Mid Bass renders");
    load(1, 3);
    CHECK(fabsf(eff(&E, P_CUTOFF) - 0.9f) < 1e-4f && fabsf(eff(&E, P_RESO) - 0.25f) < 1e-4f, "stored entry: cutoff 0.9, reso 0.25");
}

static void test_syx(void)
{
    uint8_t raw[133], frame[200], file[200 * 5];
    char name[24];
    size_t pos, fl, total = 0;
    int i, bad = 0;
    printf("[.syx decoder and Prophet-5 mapping]\n");
    for (i = 0; i < 200; i++) {
        tp5_kv kv[TP5_BANK_KV_PER_PRESET];
        int n;
        memset(raw, 0, sizeof raw);
        fill_raw(i, raw);
        memcpy(raw + 65, "TEST PATCH NAME     ", 20);
        fl = make_frame(frame, raw, i / 40, i % 40);
        pos = 0;
        {
            uint8_t back[133];
            if (!tp5_syx_next(frame, fl, &pos, back, name) || memcmp(back, raw, 133) != 0 || pos != fl) bad++;
        }
        n = tp5_p5_to_kv(raw, kv, TP5_BANK_KV_PER_PRESET);
        CHECK(n > 10 && n <= TP5_BANK_KV_PER_PRESET, "program %d maps to %d parameters", i, n);
        if (i < 5) { memcpy(file + total, frame, fl); total += fl; }
    }
    CHECK(bad == 0, "%d frames did not round-trip through pack / unpack", bad);
    CHECK(strcmp(name, "Test Patch Name") == 0, "ALL CAPS names become Title Case ('%s')", name);

    /* a file with junk between frames, a damaged frame and a foreign SysEx message */
    {
        uint8_t mix[1200];
        size_t o = 0, p = 0;
        int count = 0;
        mix[o++] = 0x00; mix[o++] = 0x12;
        memcpy(mix + o, file, 159); o += 159;
        mix[o++] = 0xF0; mix[o++] = 0x7E; mix[o++] = 0x7F; mix[o++] = 0x06; mix[o++] = 0x01; mix[o++] = 0xF7;     /* identity request */
        memcpy(mix + o, file + 159, 159); mix[o + 20] = 0xF0; o += 159;                                           /* status byte inside: damaged */
        memcpy(mix + o, file + 318, 159); o += 159;
        mix[o++] = 0xF0; mix[o++] = 0x01;                                                                          /* unterminated */
        while (tp5_syx_next(mix, o, &p, raw, name)) count++;
        CHECK(count == 2, "junk, foreign and damaged frames are skipped: found %d programs", count);
        p = 0;
        CHECK(tp5_syx_next(mix, 5, &p, raw, name) == 0, "short input");
    }
}

static void test_registry_engine(void)
{
    tp5_kv a[2] = { { P_CUTOFF, 0.5f }, { P_RESO, 0.4f } };
    int i, b;
    char buf[64];
    printf("[bank registry and engine]\n");
    tp5_banks_reset();
    CHECK(tp5_bank_count() == 1 && strcmp(tp5_bank_name(0), "Factory") == 0 && tp5_bank_size(0) == TINYPRO5_PRESET_COUNT, "factory bank is bank 0");
    CHECK(tp5_bank_end(tp5_bank_begin("Empty")) == 0 && tp5_bank_count() == 1, "an empty bank is dropped");
    b = tp5_bank_begin("Names");
    CHECK(b == 1, "bank index %d", b);
    {
        static const char *nm[] = { "zebra", "Apple", "apple", "Mango", "8 bit", "Banana" };
        for (i = 0; i < 6; i++) CHECK(tp5_bank_add(b, nm[i], a, 2) == 0, "add %d", i);
    }
    CHECK(tp5_bank_add(0, "x", a, 2) == -1 && tp5_bank_add(7, "x", a, 2) == -1, "only the newest custom bank accepts presets");
    CHECK(tp5_bank_end(b) == 6, "bank closed with 6 presets");
    {
        const char *want[] = { "8 bit", "Apple", "apple", "Banana", "Mango", "zebra" };   /* case-insensitive, ties keep bank order */
        for (i = 0; i < 6; i++) CHECK(strcmp(tp5_bank_preset_name(b, tp5_bank_slot(b, i, 1)), want[i]) == 0, "A-Z position %d is '%s'", i, tp5_bank_preset_name(b, tp5_bank_slot(b, i, 1)));
        for (i = 0; i < 6; i++) CHECK(tp5_bank_pos(b, tp5_bank_slot(b, i, 1), 1) == i, "inverse at %d", i);
    }
    CHECK(tp5_bank_slot(9, 3, 0) == 3 && tp5_bank_size(9) == 0, "unknown bank falls back to the factory table");
    CHECK(tp5_bank_slot(b, 99, 0) == 5 && tp5_bank_slot(b, -4, 0) == 0, "positions are clamped");

    /* engine: switching banks loads the first program, list size follows, labels use the right form, factory edits are separate */
    tp5_init(&E, 44100.f);
    tp5_set_param(&E, P_PRESET, 7.f);
    tp5_set_param(&E, P_BANK, 1.f);
    CHECK((int)eff(&E, P_BANK) == 1 && (int)eff(&E, P_PRESET) == 0 && fabsf(eff(&E, P_CUTOFF) - 0.5f) < 1e-5f && fabsf(eff(&E, P_RESO) - 0.4f) < 1e-5f, "bank switch loads program 0 of the new bank");
    tp5_set_param(&E, P_PRESET, 500.f);
    CHECK((int)eff(&E, P_PRESET) == 5, "position beyond the bank clamps to its last entry (%d)", (int)eff(&E, P_PRESET));
    tp5_set_param(&E, P_BANK, 9.f);
    CHECK((int)eff(&E, P_BANK) == 1, "bank index beyond the last clamps (%d)", (int)eff(&E, P_BANK));
    tp5_set_param(&E, P_SORT, 1.f);
    tp5_set_param(&E, P_BANK, 0.f);
    CHECK((int)eff(&E, P_BANK) == 0 && (int)eff(&E, P_PRESET) == 0, "back to the factory bank: first entry of the current (A-Z) list");
    tp5_set_param(&E, P_SORT, 0.f);
    tp5_set_param(&E, P_BANK, 1.f);
    tp5_set_param(&E, P_CUTOFF, 0.123f);
    tp5_set_param(&E, P_SAVE, 1.f);
    tp5_set_param(&E, P_LOAD, 1.f);
    CHECK(fabsf(eff(&E, P_CUTOFF) - 0.123f) < 1e-5f, "a saved edit in a custom bank is kept in the overlay (got %.3f)", (double)eff(&E, P_CUTOFF));
    tp5_set_param(&E, P_REVERT, 1.f);
    CHECK(fabsf(eff(&E, P_CUTOFF) - 0.5f) < 1e-5f, "Revert brings the bank's own program back (got %.3f)", (double)eff(&E, P_CUTOFF));
    CHECK(tp5_preset_label(1, 0, 0, buf, sizeof buf) > 0 && strcmp(buf, "1 zebra") == 0, "custom label '%s'", buf);
    CHECK(tp5_preset_label(1, 0, 1, buf, sizeof buf) > 0 && strcmp(buf, "5 8 bit") == 0, "custom A-Z label keeps the slot number '%s'", buf);
    CHECK(tp5_preset_label(0, 0, 1, buf, sizeof buf) > 0 && buf[1] == '-' && buf[3] == ' ', "factory A-Z label keeps the group-program address '%s'", buf);
    CHECK(tp5_preset_label(0, 0, 0, buf, sizeof buf) > 0 && strncmp(buf, "1-1 ", 4) == 0, "factory label '%s'", buf);
    CHECK(tp5_preset_label(1, 6, 0, buf, sizeof buf) == -1, "past the end of the bank");

    /* state round trip keeps bank + order + position */
    tp5_set_param(&E, P_SORT, 1.f);
    tp5_set_param(&E, P_PRESET, 3.f);
    {
        char st[8192];
        tp5_engine F;
        CHECK(tp5_state_save(&E, st, sizeof st) > 0, "state saves");
        tp5_init(&F, 44100.f);
        CHECK(tp5_state_load(&F, st) == 0 && (int)eff(&F, P_BANK) == 1 && (int)eff(&F, P_SORT) == 1 && (int)eff(&F, P_PRESET) == 3, "state restores bank %d sort %d position %d", (int)eff(&F, P_BANK), (int)eff(&F, P_SORT), (int)eff(&F, P_PRESET));
    }

    /* a saved state that names a bank which no longer exists must fall back to the factory bank */
    tp5_banks_reset();
    tp5_init(&E, 44100.f);
    CHECK(tp5_state_load(&E, "TP5:1;bank=3;preset_sort=1;preset=150;") == 0, "state load");
    tp5_load_preset(&E, 5);
    CHECK((int)eff(&E, P_BANK) == 0 || tp5_bank_count() > (int)eff(&E, P_BANK), "stale bank index repaired (%d)", (int)eff(&E, P_BANK));
}

static void test_scan(void)
{
    static uint8_t syx[159 * 3];
    uint8_t raw[133];
    collect *c = (collect *)calloc(1, sizeof *c);
    char path[256];
    size_t o = 0;
    int i, n;
    printf("[folder scan]\n");
    MKDIR("build/banks_test"); MKDIR("build/banks_test/Folder"); MKDIR("build/banks_test/Folder/Sub"); MKDIR("build/banks_test/EmptyFolder");
    put_text("build/banks_test/Synth.zip", k_fixture_zip, sizeof k_fixture_zip);
    put_text("build/banks_test/empty.zip", k_fixture_zip, 40);                                 /* damaged archive */
    put_text("build/banks_test/junk.txt", "not a bank", 10);
    for (i = 0; i < 3; i++) {
        memset(raw, 0, sizeof raw);
        fill_raw(160 + i, raw);
        memcpy(raw + 65, "BRASS               ", 20);
        o += make_frame(syx + o, raw, 5, i);
    }
    put_text("build/banks_test/Prophet.syx", syx, o);
    tp5_zip_walk(k_fixture_zip, sizeof k_fixture_zip, ".h2p", g_scratch, sizeof g_scratch, collect_cb, c);
    put_text("build/banks_test/Folder/Sub/X Patch.h2p", c->text[0], c->lens[0]);
    put_text("build/banks_test/loose1.h2p", c->text[1], c->lens[1]);
    put_text("build/banks_test/loose2.h2p", c->text[2], c->lens[2]);
    put_text("build/banks_test/loose3.h2p", c->text[3], c->lens[3]);                           /* Zebra2: skipped */
    free(c);

    n = tp5_banks_scan("build/banks_test");
    printf("    %s\n", tp5_banks_report());
    CHECK(n == 4 && tp5_bank_count() == 5, "four custom banks expected, got %d", n);
    CHECK(strcmp(tp5_bank_name(1), "Folder") == 0 && tp5_bank_size(1) == 1 && strcmp(tp5_bank_preset_name(1, 0), "X Patch") == 0, "folder bank '%s' x%d", tp5_bank_name(1), tp5_bank_size(1));
    CHECK(strcmp(tp5_bank_name(2), "Prophet") == 0 && tp5_bank_size(2) == 3 && strcmp(tp5_bank_preset_name(2, 0), "Brass") == 0, "syx bank '%s' x%d '%s'", tp5_bank_name(2), tp5_bank_size(2), tp5_bank_preset_name(2, 0));
    CHECK(strcmp(tp5_bank_name(3), "Synth") == 0 && tp5_bank_size(3) == 4, "zip bank '%s' x%d", tp5_bank_name(3), tp5_bank_size(3));
    CHECK(strcmp(tp5_bank_name(4), "Loose") == 0 && tp5_bank_size(4) == 2, "flat .h2p files form 'Loose' (%d presets)", tp5_bank_size(4));
    for (i = 1; i < tp5_bank_count(); i++) {
        int s;
        for (s = 0; s < tp5_bank_size(i); s++) {
            load(i, s);
            CHECK(render_ok(), "bank %d program %d (%s) renders", i, s, tp5_bank_preset_name(i, s));
        }
    }
    /* bank 0 is always the compiled-in factory table: no file in the banks folder replaces or renames it, and custom banks follow it */
    {
        CHECK(tp5_bank_size(0) == TINYPRO5_PRESET_COUNT && strcmp(tp5_bank_name(0), "Factory") == 0 &&
              strcmp(tp5_bank_preset_name(0, 0), "Night Drive Bass") == 0 && strcmp(tp5_bank_preset_name(0, 31), "Dead Channel") == 0,
              "bank 0 is the %d-program factory table ('%s' ... '%s')", tp5_bank_size(0), tp5_bank_preset_name(0, 0), tp5_bank_preset_name(0, 31));
        CHECK(strcmp(tp5_bank_name(1), "Folder") == 0, "custom banks stay numbered after bank 0 ('%s')", tp5_bank_name(1));
        load(0, 17);
        CHECK(render_ok(), "a factory program renders");
        /* a file that merely carries the old default bank's name is an ordinary custom bank now (here: not a SysEx dump, so it adds none) */
        put_text("build/banks_test/prophet5-v1.03.syx", "this is not a sysex file", 24);
        n = tp5_banks_scan("build/banks_test");
        CHECK(n == 4 && tp5_bank_size(0) == TINYPRO5_PRESET_COUNT && tp5_bank_count() == 5, "a stray .syx does not touch bank 0 (%d custom banks)", n);
        remove("build/banks_test/prophet5-v1.03.syx");
    }
    CHECK(tp5_banks_scan("build/no_such_folder") == 0 && tp5_bank_count() == 1, "a missing folder means no custom banks");
    CHECK(tp5_banks_scan(NULL) == 0, "NULL folder");
    n = tp5_banks_scan("build/banks_test");
    CHECK(n == 4, "scanning twice gives the same result (%d)", n);
    (void)path;
}

/* ---- Save on every bank: edits live in the overlay files, the bank files are never touched ------------------------------- */
static unsigned long fnv_file(const char *path)
{
    unsigned long h = 2166136261ul;
    FILE *f = fopen(path, "rb");
    int c;
    if (!f) return 0;
    while ((c = fgetc(f)) != EOF) h = (h ^ (unsigned long)c) * 16777619ul;
    fclose(f);
    return h;
}

static void test_overlay(void)
{
    static uint8_t syx[159 * 3];
    uint8_t raw[133];
    collect *c = (collect *)calloc(1, sizeof *c);
    static const char *files[] = { "build/banks_ov/Synth.zip", "build/banks_ov/Prophet.syx", "build/banks_ov/loose1.h2p",
                                   "build/banks_ov/Folder/x.h2p" };
    static const char *sidecars[] = { "Factory", "Folder", "Prophet", "Synth", "Loose" };
    unsigned long before[4], after[4];
    size_t o = 0;
    int i, b, n;
    printf("[Save on every bank (overlay)]\n");
    MKDIR("build/banks_ov"); MKDIR("build/banks_ov/Folder"); MKDIR("build/user_ov");
    remove("build/banks_ov/prophet5-v1.03.syx");      /* left behind by test runs from before the factory bank was compiled in */
    remove("build/user_ov/prophet5-v1.03.tp5u");
    for (i = 0; i < 5; i++) { char p[128]; sprintf(p, "build/user_ov/%s.tp5u", sidecars[i]); remove(p); }
    for (i = 0; i < 3; i++) {
        memset(raw, 0, sizeof raw);
        fill_raw(100 + i, raw);
        memcpy(raw + 65, i == 0 ? "OVERLAY ONE         " : i == 1 ? "OVERLAY TWO         " : "OVERLAY THREE       ", 20);
        o += make_frame(syx + o, raw, 5, i);
    }
    put_text("build/banks_ov/Prophet.syx", syx, o);
    put_text("build/banks_ov/Synth.zip", k_fixture_zip, sizeof k_fixture_zip);
    tp5_zip_walk(k_fixture_zip, sizeof k_fixture_zip, ".h2p", g_scratch, sizeof g_scratch, collect_cb, c);
    put_text("build/banks_ov/loose1.h2p", c->text[1], c->lens[1]);
    put_text("build/banks_ov/Folder/x.h2p", c->text[0], c->lens[0]);
    free(c);
    for (i = 0; i < 4; i++) before[i] = fnv_file(files[i]);

    tp5_overlay_clear();
    tp5_overlay_set_dir("build/user_ov");
    n = tp5_banks_scan("build/banks_ov");
    CHECK(n == 4 && tp5_bank_count() == 5, "fixture banks: %d custom", n);
    CHECK(tp5_overlay_count() == 0, "no overrides yet");

    /* edit one program in every bank (factory, folder .h2p, .syx, .zip, loose .h2p), save, and write the overlay files */
    for (b = 0; b < tp5_bank_count(); b++) {
        const int slot = tp5_bank_size(b) > 1 ? 1 : 0;
        float orig;
        load(b, slot);
        orig = eff(&E, P_CUTOFF);
        tp5_set_param(&E, P_CUTOFF, orig > 0.5f ? 0.123f : 0.876f);
        tp5_set_param(&E, P_A_ATK, 0.777f);
        tp5_set_param(&E, P_SAVE, 1.f);
        CHECK(tp5_overlay_dirty() >= 1, "bank %d: Save marks the overlay dirty", b);
        CHECK(tp5_overlay_flush() == 1 && tp5_overlay_dirty() == 0, "bank %d: the overlay file is written", b);
    }
    CHECK(tp5_overlay_count() == 5, "five saved programs (%d)", tp5_overlay_count());
    {
        FILE *g = fopen("build/user_ov/Prophet.tp5u", "rb");
        char head[200] = "";
        if (g) { size_t k = fread(head, 1, sizeof head - 1, g); head[k] = 0; fclose(g); }
        CHECK(strncmp(head, "TP5U 1\nbank Prophet\nu|1|Overlay Two|", 36) == 0, "sidecar is readable text keyed by bank and program name: '%.40s'", head);
    }

    /* "restart": forget the memory, scan again (this reads the overlay files), load the programs */
    tp5_overlay_clear();
    CHECK(tp5_overlay_count() == 0, "memory cleared");
    n = tp5_banks_scan("build/banks_ov");
    CHECK(n == 4 && tp5_overlay_count() == 5, "the overlay files were read back (%d entries)", tp5_overlay_count());
    for (b = 0; b < tp5_bank_count(); b++) {
        const int slot = tp5_bank_size(b) > 1 ? 1 : 0;
        float o2;
        load(b, slot);
        o2 = eff(&E, P_CUTOFF);
        CHECK((fabsf(o2 - 0.123f) < 1e-4f || fabsf(o2 - 0.876f) < 1e-4f) && fabsf(eff(&E, P_A_ATK) - 0.777f) < 1e-4f,
              "bank %d (%s) slot %d: the saved edit is back after a restart (cutoff %.3f, attack %.3f)", b, tp5_bank_name(b), slot, (double)o2, (double)eff(&E, P_A_ATK));
        CHECK(render_ok(), "bank %d: the edited program renders", b);
        if (tp5_bank_size(b) > 2 || (b > 0 && tp5_bank_size(b) > 1 && slot == 1)) {            /* a neighbour is untouched */
            load(b, 0);
            CHECK(fabsf(eff(&E, P_A_ATK) - 0.777f) > 1e-4f, "bank %d: program 0 was not affected by the edit of program 1", b);
        }
    }

    /* the user's own files are exactly as they were */
    for (i = 0; i < 4; i++) after[i] = fnv_file(files[i]);
    for (i = 0; i < 4; i++) CHECK(before[i] == after[i] && before[i] != 0, "%s was not modified", files[i]);

    /* Revert drops the override, in memory and on disk */
    load(3, 1);
    CHECK(fabsf(eff(&E, P_A_ATK) - 0.777f) < 1e-4f, "(edit present before revert)");
    tp5_set_param(&E, P_REVERT, 1.f);
    CHECK(fabsf(eff(&E, P_A_ATK) - 0.777f) > 1e-4f, "Revert restores the zip's own program");
    CHECK(tp5_overlay_flush() == 1, "revert rewrites the bank's overlay file");
    tp5_overlay_clear();
    tp5_banks_scan("build/banks_ov");
    CHECK(tp5_overlay_count() == 4, "after a restart only the other four overrides remain (%d)", tp5_overlay_count());
    load(3, 1);
    CHECK(fabsf(eff(&E, P_A_ATK) - 0.777f) > 1e-4f, "the reverted program stays original after a restart");

    /* a bank file that changed under an override (the program has another name now) must not receive the stale edit */
    memset(raw, 0, sizeof raw);
    fill_raw(100 + 1, raw);
    memcpy(raw + 65, "SOMETHING ELSE      ", 20);
    o = 0;
    for (i = 0; i < 3; i++) {
        uint8_t r2[133];
        memset(r2, 0, sizeof r2);
        fill_raw(100 + i, r2);
        memcpy(r2 + 65, i == 1 ? "SOMETHING ELSE      " : "OVERLAY ONE         ", 20);
        o += make_frame(syx + o, r2, 5, i);
    }
    put_text("build/banks_ov/Prophet.syx", syx, o);
    tp5_overlay_clear();
    tp5_banks_scan("build/banks_ov");
    load(2, 1);
    CHECK(fabsf(eff(&E, P_A_ATK) - 0.777f) > 1e-4f, "a renamed program ignores the old override (%s)", tp5_bank_preset_name(2, 1));

    /* damaged / foreign / hostile overlay files are ignored without harm */
    {
        char bad[400];
        const char *junk = "this is not an overlay file\x01\x02\n";
        put_text("build/user_ov/Garbage.tp5u", junk, strlen(junk));
        sprintf(bad, "TP5U 1\nbank Synth\nu|9999|x|cutoff=0.5;\nu|1|%s|cutoff=nan;bogus=1;resonance=7;\nu|abc\n", tp5_bank_preset_name(3, 1));
        put_text("build/user_ov/Partial.tp5u", bad, strlen(bad));
    }
    tp5_overlay_clear();
    n = tp5_banks_scan("build/banks_ov");
    CHECK(n == 4, "scan survives damaged overlay files (%d banks)", n);
    load(3, 1);
    CHECK(eff(&E, P_RESO) <= 1.f && eff(&E, P_RESO) >= 0.f && eff(&E, P_CUTOFF) == eff(&E, P_CUTOFF), "values from a damaged file are clamped and never NaN");
    remove("build/user_ov/Garbage.tp5u");
    remove("build/user_ov/Partial.tp5u");
}

int main(void)
{
    printf("custom bank tests\n");
    test_inflate_zip();
    test_h2p();
    test_syx();
    test_registry_engine();
    test_scan();
    test_overlay();
    printf("    %d checks, %d failed\n    RESULT: %s\n", g_checks, g_fail, g_fail ? "FAIL" : "PASS");
    return g_fail ? 1 : 0;
}
