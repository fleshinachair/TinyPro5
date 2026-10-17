/* Bank registry: static storage, no IO, no heap. See bank.h. Bank 0 is the compiled-in factory table (src/presets_factory.c); custom banks live in the pool below. */
#include <string.h>

#include "bank.h"

#define KV_POOL 131072

typedef struct { char name[TP5_BANK_NAME_LEN]; uint32_t kv_off; uint8_t n; } cpreset;

static cpreset  g_pre[TP5_BANK_POOL_PRESETS];             /* custom banks only; bank 0 is read straight from tp5_presets */
static tp5_kv   g_kv[KV_POOL];
static uint32_t g_kv_used;
static int      g_pre_used;

static struct { char name[TP5_BANK_NAME_LEN]; int first, n; int sort_off; int sorted; } g_bank[TP5_MAX_BANKS];
static int g_nbanks = 1;                                   /* bank 0 = factory */
static uint16_t g_sorted[TINYPRO5_PRESET_COUNT + TP5_BANK_POOL_PRESETS];   /* rank -> slot, per bank at sort_off */
static uint16_t g_rank[TINYPRO5_PRESET_COUNT + TP5_BANK_POOL_PRESETS];     /* slot -> rank */
static int g_sort_used;

static void init_factory(void)
{
    if (g_bank[0].name[0]) return;
    strcpy(g_bank[0].name, "Factory");
    g_bank[0].first = 0;
    g_bank[0].n = TINYPRO5_PRESET_COUNT;
    g_bank[0].sort_off = 0;
    g_sort_used = TINYPRO5_PRESET_COUNT;
}

void tp5_banks_reset(void)
{
    int b;
    init_factory();
    for (b = 1; b < TP5_MAX_BANKS; b++) memset(&g_bank[b], 0, sizeof g_bank[b]);
    g_nbanks = 1;
    g_pre_used = 0;
    g_kv_used = 0;
    g_sort_used = TINYPRO5_PRESET_COUNT;
}

int tp5_bank_count(void) { init_factory(); return g_nbanks; }

const char *tp5_bank_name(int bank)
{
    init_factory();
    return (bank >= 0 && bank < g_nbanks) ? g_bank[bank].name : "";
}

int tp5_bank_size(int bank)
{
    init_factory();
    return (bank >= 0 && bank < g_nbanks) ? g_bank[bank].n : 0;
}

const char *tp5_bank_preset_name(int bank, int slot)
{
    init_factory();
    if (bank < 0 || bank >= g_nbanks || slot < 0 || slot >= g_bank[bank].n) return "";
    return bank == 0 ? tp5_presets[slot].name : g_pre[g_bank[bank].first + slot].name;
}

float tp5_bank_gain(int bank, int slot)
{
    init_factory();
    if (bank != 0 || slot < 0 || slot >= g_bank[0].n) return 1.f;
    return tp5_presets[slot].gain >= 1.f ? tp5_presets[slot].gain : 1.f;
}

int tp5_bank_preset_kv(int bank, int slot, const tp5_kv **kv)
{
    init_factory();
    if (bank < 0 || bank >= g_nbanks || slot < 0 || slot >= g_bank[bank].n) { *kv = NULL; return 0; }
    if (bank == 0) { *kv = tp5_presets[slot].kv; return tp5_presets[slot].n; }
    {
        const cpreset *p = &g_pre[g_bank[bank].first + slot];
        *kv = g_kv + p->kv_off;
        return p->n;
    }
}

int tp5_bank_begin(const char *name)
{
    size_t i;
    init_factory();
    if (g_nbanks >= TP5_MAX_BANKS) return -1;
    memset(&g_bank[g_nbanks], 0, sizeof g_bank[g_nbanks]);
    for (i = 0; name && name[i] && i < TP5_BANK_NAME_LEN - 1; i++) g_bank[g_nbanks].name[i] = name[i];
    if (!g_bank[g_nbanks].name[0]) strcpy(g_bank[g_nbanks].name, "Bank");
    g_bank[g_nbanks].first = g_pre_used;
    return g_nbanks++;
}

int tp5_bank_add(int bank, const char *name, const tp5_kv *kv, int n)
{
    cpreset *p;
    size_t i;
    if (bank != g_nbanks - 1 || bank < 1) return -1;                       /* only the newest bank grows */
    if (g_bank[bank].n >= TP5_BANK_MAX_PRESETS || g_pre_used >= TP5_BANK_POOL_PRESETS) return -1;
    if (n < 0) n = 0;
    if (n > TP5_BANK_KV_PER_PRESET) n = TP5_BANK_KV_PER_PRESET;
    if (g_kv_used + (uint32_t)n > KV_POOL) return -1;
    p = &g_pre[g_pre_used];
    memset(p, 0, sizeof *p);
    for (i = 0; name && name[i] && i < TP5_BANK_NAME_LEN - 1; i++) p->name[i] = name[i];
    if (!p->name[0]) strcpy(p->name, "?");
    p->kv_off = g_kv_used;
    p->n = (uint8_t)n;
    if (n) memcpy(g_kv + g_kv_used, kv, (size_t)n * sizeof *kv);
    g_kv_used += (uint32_t)n;
    g_pre_used++;
    g_bank[bank].n++;
    return 0;
}

static int name_cmp(int bank, int a, int b)
{
    const char *x = tp5_bank_preset_name(bank, a), *y = tp5_bank_preset_name(bank, b);
    for (; *x && *y; x++, y++) {
        int cx = (unsigned char)*x, cy = (unsigned char)*y;
        if (cx >= 'A' && cx <= 'Z') cx += 'a' - 'A';
        if (cy >= 'A' && cy <= 'Z') cy += 'a' - 'A';
        if (cx != cy) return cx - cy;
    }
    if (*x != *y) return (unsigned char)*x - (unsigned char)*y;
    return a - b;                                   /* equal names keep bank order */
}

static void build_sort(int bank)
{
    uint16_t *s, *r;
    int i, j, n;
    if (g_bank[bank].sorted) return;
    n = g_bank[bank].n;
    if (bank > 0) g_bank[bank].sort_off = g_sort_used;
    if (g_bank[bank].sort_off + n > (int)(sizeof g_sorted / sizeof *g_sorted)) return;
    s = g_sorted + g_bank[bank].sort_off;
    r = g_rank + g_bank[bank].sort_off;
    for (i = 0; i < n; i++) {                       /* insertion sort: runs once per bank, never in the audio path */
        for (j = i; j > 0 && name_cmp(bank, i, s[j - 1]) < 0; j--) s[j] = s[j - 1];
        s[j] = (uint16_t)i;
    }
    for (i = 0; i < n; i++) r[s[i]] = (uint16_t)i;
    if (bank > 0) g_sort_used += n;
    g_bank[bank].sorted = 1;
}

int tp5_bank_end(int bank)
{
    init_factory();
    if (bank < 1 || bank != g_nbanks - 1) return 0;
    if (g_bank[bank].n == 0) {                      /* nothing usable in it: forget the bank */
        memset(&g_bank[bank], 0, sizeof g_bank[bank]);
        g_nbanks--;
        return 0;
    }
    build_sort(bank);
    return g_bank[bank].n;
}

int tp5_bank_slot(int bank, int pos, int sort)
{
    int n;
    init_factory();
    if (bank < 0 || bank >= g_nbanks) bank = 0;
    n = g_bank[bank].n;
    if (n < 1) return 0;
    if (pos < 0) pos = 0;
    if (pos >= n) pos = n - 1;
    if (!sort) return pos;
    build_sort(bank);
    return g_bank[bank].sorted ? g_sorted[g_bank[bank].sort_off + pos] : pos;
}

int tp5_bank_pos(int bank, int slot, int sort)
{
    int n;
    init_factory();
    if (bank < 0 || bank >= g_nbanks) bank = 0;
    n = g_bank[bank].n;
    if (n < 1) return 0;
    if (slot < 0) slot = 0;
    if (slot >= n) slot = n - 1;
    if (!sort) return slot;
    build_sort(bank);
    return g_bank[bank].sorted ? g_rank[g_bank[bank].sort_off + slot] : slot;
}
