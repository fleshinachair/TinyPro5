/*
 * Custom bank discovery: the only file IO in the module, run once from create_instance (never from the audio thread).
 *
 *   <banks_dir>/Name.zip        every .h2p / .syx inside -> one bank called "Name"
 *   <banks_dir>/Name.syx        one bank called "Name"   (Prophet-5 program dumps)
 *   <banks_dir>/ANY.h2p         flat files -> one bank called "Loose"
 *   <banks_dir>/Name/           an extracted bank: every .h2p / .syx below it (3 levels deep) -> one bank called "Name"
 *
 * Banks are listed in name order, presets in directory order (zip: archive order). Static buffers only, no heap.
 */
#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#define MKDIR(p) _mkdir(p)
#else
#define MKDIR(p) mkdir((p), 0755)
#endif

#include "bank.h"

#ifndef S_ISDIR
#define S_ISDIR(m) (((m) & S_IFMT) == S_IFDIR)
#endif
#ifndef S_ISREG
#define S_ISREG(m) (((m) & S_IFMT) == S_IFREG)
#endif

#define MAX_NAMES   1600
#define NAME_LEN    200
#define FILE_CAP    (16u << 20)
#define SCRATCH_CAP (256u << 10)

static uint8_t g_file[FILE_CAP];
static uint8_t g_scratch[SCRATCH_CAP];
static char    g_names[MAX_NAMES][NAME_LEN];
static char    g_report[200] = "banks: not scanned";
static int     g_added, g_skipped, g_files;

const char *tp5_banks_report(void) { return g_report; }

/* ---- user overlay files: <user dir>/<bank>.tp5u, one small text file per bank (format in overlay.c) ---------------------- */
static int list_dir_fwd(const char *dir, char (*out)[200], int cap);
static char g_user_dir[400];
static int  g_user_dir_explicit;
static char g_ovbuf[1 << 20];

void tp5_overlay_set_dir(const char *dir)
{
    g_user_dir[0] = 0;
    g_user_dir_explicit = 0;
    if (dir && strlen(dir) < sizeof g_user_dir) { strcpy(g_user_dir, dir); g_user_dir_explicit = 1; }
}

static int ends_ci(const char *s, const char *suf);

int tp5_overlay_load_dir(void)
{
    static char names[64][NAME_LEN];
    int n, i, total = 0;
    if (!g_user_dir[0]) return 0;
    n = list_dir_fwd(g_user_dir, names, 64);
    for (i = 0; i < n; i++) {
        char path[2 * NAME_LEN + 8];
        FILE *f;
        size_t len;
        if (!ends_ci(names[i], ".tp5u") || strlen(g_user_dir) + strlen(names[i]) + 2 >= sizeof path) continue;
        strcpy(path, g_user_dir);
        strcat(path, "/");
        strcat(path, names[i]);
        f = fopen(path, "rb");
        if (!f) continue;
        len = fread(g_ovbuf, 1, sizeof g_ovbuf - 1, f);
        fclose(f);
        g_ovbuf[len] = 0;
        total += tp5_overlay_parse(g_ovbuf, len);
    }
    return total;
}

int tp5_overlay_flush(void)
{
    char bank[TP5_BANK_NAME_LEN];
    int written = 0;
    if (!g_user_dir[0]) return 0;
    MKDIR(g_user_dir);
    while (tp5_overlay_dirty_bank(0, bank, sizeof bank)) {
        char path[2 * NAME_LEN + 16], tmp[2 * NAME_LEN + 24], fname[TP5_BANK_NAME_LEN + 8];
        size_t i, o = 0;
        int len;
        FILE *f;
        for (i = 0; bank[i] && o < TP5_BANK_NAME_LEN; i++) {      /* a file name from the bank name */
            const char c = bank[i];
            fname[o++] = ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.') ? c : '_';
        }
        fname[o] = 0;
        if (!fname[0]) strcpy(fname, "bank");
        if (strlen(g_user_dir) + strlen(fname) + 12 >= sizeof path) { tp5_overlay_clean_bank(bank); continue; }
        sprintf(path, "%s/%s.tp5u", g_user_dir, fname);
        sprintf(tmp, "%s.tmp", path);
        len = tp5_overlay_serialize(bank, g_ovbuf, (int)sizeof g_ovbuf);
        f = len > 0 ? fopen(tmp, "wb") : NULL;
        if (f) {
            const int ok = fwrite(g_ovbuf, 1, (size_t)len, f) == (size_t)len;
            fclose(f);
            if (ok) {
#ifdef _WIN32
                remove(path);                                       /* rename() will not replace a file on Windows */
#endif
                if (rename(tmp, path) == 0) written++;
            }
        }
        tp5_overlay_clean_bank(bank);                               /* do not retry forever on a read-only disk */
    }
    return written;
}

static int ends_ci(const char *s, const char *suf)
{
    const size_t a = strlen(s), b = strlen(suf);
    size_t i;
    if (b > a) return 0;
    for (i = 0; i < b; i++) {
        char x = s[a - b + i], y = suf[i];
        if (x >= 'A' && x <= 'Z') x = (char)(x + 32);
        if (y >= 'A' && y <= 'Z') y = (char)(y + 32);
        if (x != y) return 0;
    }
    return 1;
}

static void base_no_ext(const char *path, char *out, size_t cap)
{
    const char *b = path, *p;
    size_t n;
    for (p = path; *p; p++) if (*p == '/' || *p == '\\') b = p + 1;
    n = strlen(b);
    if (n > 4 && b[n - 4] == '.') n -= 4;
    else if (n > 5 && b[n - 5] == '.') n -= 5;
    if (n >= cap) n = cap - 1;
    memcpy(out, b, n);
    out[n] = 0;
}

/* ---- adding presets ------------------------------------------------------------------------------------------ */
static void add_h2p(int bank, const char *path, const uint8_t *data, size_t len)
{
    tp5_h2p h;
    tp5_kv kv[TP5_BANK_KV_PER_PRESET];
    char name[TP5_BANK_NAME_LEN];
    int n;
    if (!tp5_h2p_parse((const char *)data, len, &h)) { g_skipped++; return; }      /* another instrument, or not a patch */
    n = tp5_h2p_to_kv(&h, kv, TP5_BANK_KV_PER_PRESET);
    base_no_ext(path, name, sizeof name);
    if (tp5_bank_add(bank, name, kv, n) == 0) g_added++; else g_skipped++;
}

static void add_syx(int bank, const uint8_t *data, size_t len)
{
    size_t pos = 0;
    uint8_t raw[TP5_SYX_RAW_MAX];
    int raw_len = 0;
    char name[24];
    tp5_kv kv[TP5_BANK_KV_PER_PRESET];
    while (tp5_syx_next_ex(data, len, &pos, raw, &raw_len, name)) {
        const int n = tp5_p5_to_kv_ex(raw, raw_len, kv, TP5_BANK_KV_PER_PRESET);
        if (tp5_bank_add(bank, name, kv, n) == 0) g_added++; else g_skipped++;
    }
}

typedef struct { int bank; } zctx;

static int zip_h2p_cb(const char *name, const uint8_t *data, size_t len, void *user)
{
    add_h2p(((zctx *)user)->bank, name, data, len);
    return 0;
}

static int zip_syx_cb(const char *name, const uint8_t *data, size_t len, void *user)
{
    (void)name;
    add_syx(((zctx *)user)->bank, data, len);
    return 0;
}

static size_t read_file(const char *path, size_t cap)
{
    FILE *f = fopen(path, "rb");
    size_t n;
    if (!f) return 0;
    n = fread(g_file, 1, cap, f);
    fclose(f);
    return n;
}

static void load_one(int bank, const char *path)
{
    size_t n;
    g_files++;
    if (ends_ci(path, ".zip")) {
        zctx z;
        z.bank = bank;
        n = read_file(path, FILE_CAP);
        if (n == 0) { g_skipped++; return; }
        if (tp5_zip_walk(g_file, n, ".h2p", g_scratch, SCRATCH_CAP, zip_h2p_cb, &z) < 0 ||
            tp5_zip_walk(g_file, n, ".syx", g_scratch, SCRATCH_CAP, zip_syx_cb, &z) < 0)
            g_skipped++;
    } else if (ends_ci(path, ".syx")) {
        n = read_file(path, 1u << 20);
        if (n) add_syx(bank, g_file, n); else g_skipped++;
    } else if (ends_ci(path, ".h2p")) {
        n = read_file(path, SCRATCH_CAP);
        if (n) add_h2p(bank, path, g_file, n); else g_skipped++;
    }
}

/* ---- directory walking ----------------------------------------------------------------------------------------- */
static int is_dir(const char *p) { struct stat st; return stat(p, &st) == 0 && S_ISDIR(st.st_mode); }
static int is_file(const char *p) { struct stat st; return stat(p, &st) == 0 && S_ISREG(st.st_mode); }

/* names of the entries of `dir` (not . / ..), sorted; returns the count */
static int list_dir(const char *dir, char (*out)[NAME_LEN], int cap)
{
    DIR *d = opendir(dir);
    struct dirent *e;
    int n = 0, i, j;
    if (!d) return 0;
    while ((e = readdir(d)) != NULL && n < cap) {
        if (e->d_name[0] == '.' && (e->d_name[1] == 0 || (e->d_name[1] == '.' && e->d_name[2] == 0))) continue;
        if (strlen(e->d_name) >= NAME_LEN) continue;
        strcpy(out[n++], e->d_name);
    }
    closedir(d);
    for (i = 1; i < n; i++) {                       /* insertion sort, byte order */
        char tmp[NAME_LEN];
        strcpy(tmp, out[i]);
        for (j = i; j > 0 && strcmp(tmp, out[j - 1]) < 0; j--) strcpy(out[j], out[j - 1]);
        strcpy(out[j], tmp);
    }
    return n;
}

static int list_dir_fwd(const char *dir, char (*out)[200], int cap) { return list_dir(dir, out, cap); }

/* every bank file below `dir`, in path order, into one bank (depth <= 3). The name list lives on the stack of the caller's
 * recursion level: it is a small fixed array per level, so the total stays bounded. */
static void walk_folder(int bank, const char *dir, int depth)
{
    static char lvl[3][400][NAME_LEN];             /* per depth, so recursion does not clobber the parent's list */
    int n, i;
    if (depth > 2) return;
    n = list_dir(dir, lvl[depth], 400);
    for (i = 0; i < n; i++) {
        char path[2 * NAME_LEN + 8];
        if (strlen(dir) + strlen(lvl[depth][i]) + 2 >= sizeof path) continue;
        strcpy(path, dir);
        strcat(path, "/");
        strcat(path, lvl[depth][i]);
        if (is_dir(path)) walk_folder(bank, path, depth + 1);
        else if (is_file(path)) load_one(bank, path);
    }
}

int tp5_banks_scan(const char *banks_dir)
{
    int n, i;
    g_added = g_skipped = g_files = 0;
    tp5_banks_reset();                                       /* bank 0 is the compiled-in factory table; custom banks are numbered after it */
    if (!g_user_dir_explicit && banks_dir) {                      /* <banks folder>/../user, or ./user when there is no parent */
        const char *last = NULL, *p;
        size_t pre;
        for (p = banks_dir; *p; p++) if (*p == '/' || *p == '\\') last = p;
        pre = last ? (size_t)(last - banks_dir) : 0;
        if (pre + 8 < sizeof g_user_dir) {
            memcpy(g_user_dir, banks_dir, pre);
            strcpy(g_user_dir + pre, last ? "/user" : "user");
        }
    }
    tp5_overlay_clear();
    tp5_overlay_load_dir();
    if (!banks_dir || !is_dir(banks_dir)) {
        strcpy(g_report, "banks: no banks folder");
        return 0;
    }
    n = list_dir(banks_dir, g_names, MAX_NAMES);
    for (i = 0; i < n; i++) {                       /* pass 1: folders, zips and .syx files, one bank each */
        char path[2 * NAME_LEN + 8], nm[TP5_BANK_NAME_LEN];
        int b;
        if (strlen(banks_dir) + strlen(g_names[i]) + 2 >= sizeof path) continue;
        strcpy(path, banks_dir);
        strcat(path, "/");
        strcat(path, g_names[i]);
        if (is_dir(path)) {
            b = tp5_bank_begin(g_names[i]);
            if (b < 0) break;
            walk_folder(b, path, 0);
            tp5_bank_end(b);
        } else if (ends_ci(path, ".zip") || ends_ci(path, ".syx")) {
            base_no_ext(path, nm, sizeof nm);
            b = tp5_bank_begin(nm);
            if (b < 0) break;
            load_one(b, path);
            tp5_bank_end(b);
        }
    }
    {                                               /* pass 2: flat .h2p files share the last bank, "Loose" */
        int b = -1;
        for (i = 0; i < n; i++) {
            char path[2 * NAME_LEN + 8];
            if (!ends_ci(g_names[i], ".h2p") || strlen(banks_dir) + strlen(g_names[i]) + 2 >= sizeof path) continue;
            if (b < 0) { b = tp5_bank_begin("Loose"); if (b < 0) break; }
            strcpy(path, banks_dir);
            strcat(path, "/");
            strcat(path, g_names[i]);
            if (is_file(path)) load_one(b, path);
        }
        if (b >= 0) tp5_bank_end(b);
    }
    {
        const int found = tp5_bank_count() - 1;
        int w = snprintf(g_report, sizeof g_report, "banks: %d custom bank(s), %d preset(s) from %d file(s), %d skipped",
                         found, g_added, g_files, g_skipped);
        if (w < 0) g_report[0] = 0;
        return found;
    }
}
