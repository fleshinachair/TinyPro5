/*
 * User overlay: edits saved over programs of ANY bank, kept apart from the bank files themselves. Pure code, static storage.
 *
 * An entry is (bank name, slot) -> the full set of sound parameters of the edited program, plus the program's name at the time it
 * was saved. When a program is loaded the overlay is applied on top of the freshly parsed bank program, but only if the name still
 * matches, so a bank file that was replaced or reordered can never receive a stale override. The bank files (.zip, .h2p, .syx)
 * are never written to. Persistence (one small text file per bank in <module>/user/) is src/bankscan.c's job.
 *
 * Text format, one file per bank:
 *   TP5U 1
 *   bank <bank name>
 *   u|<slot>|<program name>|<key>=<value>;<key>=<value>;...
 * Keys are parameter keys, so the files survive parameter-table changes; unknown keys are ignored, missing ones take defaults.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bank.h"

#define OV_MAX 1024
#define NAMELEN TP5_BANK_NAME_LEN

typedef struct {
    char bank[NAMELEN], name[NAMELEN];
    uint16_t slot;
    uint8_t used;
    float v[P_COUNT];
} ov_entry;

static ov_entry g_ov[OV_MAX];
static char g_dirty[TP5_MAX_BANKS][NAMELEN];
static int g_ndirty;

/* printable, no field separators or line breaks: what is stored and what is compared */
static void clean(char *dst, const char *src)
{
    size_t i = 0;
    for (; src && *src && i < NAMELEN - 1; src++) {
        const unsigned char c = (unsigned char)*src;
        dst[i++] = (c < 32 || c == '|' || c == 127) ? ' ' : (char)c;
    }
    dst[i] = 0;
}

static void mark_dirty(const char *bank)
{
    int i;
    for (i = 0; i < g_ndirty; i++) if (strcmp(g_dirty[i], bank) == 0) return;
    if (g_ndirty < TP5_MAX_BANKS) strcpy(g_dirty[g_ndirty++], bank);
}

static ov_entry *find(const char *bank, int slot)
{
    int i;
    for (i = 0; i < OV_MAX; i++)
        if (g_ov[i].used && g_ov[i].slot == slot && strcmp(g_ov[i].bank, bank) == 0) return &g_ov[i];
    return NULL;
}

void tp5_overlay_clear(void)
{
    memset(g_ov, 0, sizeof g_ov);
    g_ndirty = 0;
}

int tp5_overlay_get(int bank, int slot, float *vals)
{
    char b[NAMELEN], n[NAMELEN];
    const ov_entry *e;
    clean(b, tp5_bank_name(bank));
    e = find(b, slot);
    if (!e) return 0;
    clean(n, tp5_bank_preset_name(bank, slot));
    if (strcmp(e->name, n) != 0) return 0;          /* the bank changed under the override: leave the program alone */
    memcpy(vals, e->v, sizeof e->v);
    return 1;
}

int tp5_overlay_put(int bank, int slot, const float *vals)
{
    char b[NAMELEN];
    ov_entry *e;
    int i;
    clean(b, tp5_bank_name(bank));
    if (!b[0] || slot < 0 || slot >= TP5_BANK_MAX_PRESETS) return -1;
    e = find(b, slot);
    if (!e) {
        for (i = 0; i < OV_MAX && g_ov[i].used; i++) {}
        if (i == OV_MAX) return -1;
        e = &g_ov[i];
        memset(e, 0, sizeof *e);
        strcpy(e->bank, b);
        e->slot = (uint16_t)slot;
        e->used = 1;
    }
    clean(e->name, tp5_bank_preset_name(bank, slot));
    memcpy(e->v, vals, sizeof e->v);
    mark_dirty(b);
    return 0;
}

int tp5_overlay_remove(int bank, int slot)
{
    char b[NAMELEN];
    ov_entry *e;
    clean(b, tp5_bank_name(bank));
    e = find(b, slot);
    if (!e) return 0;
    e->used = 0;
    mark_dirty(b);
    return 1;
}

int tp5_overlay_count(void)
{
    int i, n = 0;
    for (i = 0; i < OV_MAX; i++) n += g_ov[i].used;
    return n;
}

int tp5_overlay_dirty_bank(int i, char *out, size_t cap)
{
    if (i < 0 || i >= g_ndirty || cap < NAMELEN) return 0;
    strcpy(out, g_dirty[i]);
    return 1;
}

void tp5_overlay_clean_bank(const char *bank)
{
    int i;
    for (i = 0; i < g_ndirty; i++)
        if (strcmp(g_dirty[i], bank) == 0) { memmove(g_dirty[i], g_dirty[i + 1], (size_t)(g_ndirty - i - 1) * NAMELEN); g_ndirty--; return; }
}

int tp5_overlay_dirty(void) { return g_ndirty; }

int tp5_overlay_serialize(const char *bank, char *buf, int cap)
{
    int len, i, w;
    len = snprintf(buf, (size_t)cap, "TP5U 1\nbank %s\n", bank);
    if (len < 0 || len >= cap) return -1;
    for (i = 0; i < OV_MAX; i++) {
        int k;
        if (!g_ov[i].used || strcmp(g_ov[i].bank, bank) != 0) continue;
        w = snprintf(buf + len, (size_t)(cap - len), "u|%d|%s|", (int)g_ov[i].slot, g_ov[i].name);
        if (w < 0 || w >= cap - len) return -1;
        len += w;
        for (k = 0; k < P_COUNT; k++) {
            if (!tp5_is_sound_param(k)) continue;
            w = snprintf(buf + len, (size_t)(cap - len), "%s=%.7g;", tp5_params[k].key, (double)g_ov[i].v[k]);
            if (w < 0 || w >= cap - len) return -1;
            len += w;
        }
        if (len + 2 >= cap) return -1;
        buf[len++] = '\n';
        buf[len] = 0;
    }
    return len;
}

int tp5_overlay_parse(const char *text, size_t len)
{
    char bank[NAMELEN] = "";
    size_t i = 0;
    int added = 0, versioned = 0;
    while (i < len) {
        const char *ls = text + i, *le;
        size_t n = 0;
        while (i + n < len && text[i + n] != '\n') n++;
        le = ls + n;
        i += n + 1;
        if (n > 0 && le[-1] == '\r') le--;
        if (le - ls >= 6 && memcmp(ls, "TP5U 1", 6) == 0) { versioned = 1; continue; }
        if (!versioned) return added;                   /* not one of our files */
        if (le - ls > 5 && memcmp(ls, "bank ", 5) == 0) {
            size_t m = (size_t)(le - ls - 5);
            if (m >= NAMELEN) m = NAMELEN - 1;
            memcpy(bank, ls + 5, m);
            bank[m] = 0;
            continue;
        }
        if (le - ls > 4 && ls[0] == 'u' && ls[1] == '|' && bank[0]) {
            const char *p = ls + 2, *q;
            char name[NAMELEN];
            long slot;
            ov_entry *e;
            int k, free_i;
            char *endp;
            slot = strtol(p, &endp, 10);
            if (endp == p || *endp != '|' || slot < 0 || slot >= TP5_BANK_MAX_PRESETS) continue;
            p = endp + 1;
            q = p;
            while (q < le && *q != '|') q++;
            if (q >= le) continue;
            {
                size_t m = (size_t)(q - p);
                if (m >= NAMELEN) m = NAMELEN - 1;
                memcpy(name, p, m);
                name[m] = 0;
            }
            p = q + 1;
            e = find(bank, (int)slot);
            if (!e) {
                for (free_i = 0; free_i < OV_MAX && g_ov[free_i].used; free_i++) {}
                if (free_i == OV_MAX) return added;
                e = &g_ov[free_i];
            }
            memset(e, 0, sizeof *e);
            strcpy(e->bank, bank);
            strcpy(e->name, name);
            e->slot = (uint16_t)slot;
            e->used = 1;
            for (k = 0; k < P_COUNT; k++) e->v[k] = tp5_params[k].def;
            while (p < le) {                            /* key=value; pairs */
                const char *eq = p, *sc;
                char key[32];
                while (eq < le && *eq != '=') eq++;
                sc = eq;
                while (sc < le && *sc != ';') sc++;
                if (eq < le && (size_t)(eq - p) < sizeof key) {
                    int id;
                    float v;
                    memcpy(key, p, (size_t)(eq - p));
                    key[eq - p] = 0;
                    id = tp5_param_id_from_key(key);
                    v = (float)strtod(eq + 1, NULL);
                    if (id >= 0 && tp5_is_sound_param(id) && v == v) {
                        if (v < tp5_params[id].min) v = tp5_params[id].min;
                        if (v > tp5_params[id].max) v = tp5_params[id].max;
                        e->v[id] = v;
                    }
                }
                p = sc < le ? sc + 1 : le;
            }
            added++;
        }
    }
    return added;
}
