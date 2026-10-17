/*
 * Schwung / Move native plugin (API v2) wrapper around the TinyPro5 engine.
 *
 * Instances live in a static pool: nothing here allocates. render_block() only
 * calls tp5_render_i16(), which is allocation/IO/lock free.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bank.h"
#include "meta.h"
#include "schwung_api.h"
#include "tinypro5.h"

#define MAX_INSTANCES 4

typedef struct { tp5_engine e; int used; } instance_t;

static instance_t g_pool[MAX_INSTANCES];

static int action_available(const tp5_engine *e, int id)
{
    return id == P_LOAD ? tp5_can_reload(e) : id == P_SAVE ? tp5_can_save(e) : tp5_can_revert(e);
}
static float g_sample_rate = (float)MOVE_SAMPLE_RATE;

static int g_banks_scanned;

static void *create_instance(const char *module_dir, const char *json_defaults)
{
    int i;
    (void)json_defaults;
    if (!g_banks_scanned && module_dir) {      /* custom banks: .zip, .syx and .h2p files in <module>/banks/, scanned once, never while rendering */
        char dir[512];
        int w = snprintf(dir, sizeof dir, "%s/user", module_dir);
        if (w > 0 && w < (int)sizeof dir) tp5_overlay_set_dir(dir);      /* saved edits: <module>/user/<bank>.tp5u */
        w = snprintf(dir, sizeof dir, "%s/banks", module_dir);
        if (w > 0 && w < (int)sizeof dir) tp5_banks_scan(dir);
        g_banks_scanned = 1;
    }
    for (i = 0; i < MAX_INSTANCES; i++) {
        if (!g_pool[i].used) {
            g_pool[i].used = 1;
            tp5_init(&g_pool[i].e, g_sample_rate);
            return &g_pool[i];
        }
    }
    return NULL;
}

static void destroy_instance(void *instance)
{
    instance_t *in = (instance_t *)instance;
    if (in) in->used = 0;
}

static void on_midi(void *instance, const uint8_t *msg, int len, int source)
{
    instance_t *in = (instance_t *)instance;
    (void)source;
    if (in && msg) tp5_midi(&in->e, msg, len);
}

static void set_param(void *instance, const char *key, const char *val)
{
    instance_t *in = (instance_t *)instance;
    size_t kl;
    int id;
    if (!in || !key || !val) return;

    kl = strlen(key);
    if (strcmp(key, "state") == 0 || (kl > 6 && strcmp(key + kl - 6, ":state") == 0)) {
        tp5_state_load(&in->e, val);
        if (tp5_overlay_dirty()) tp5_overlay_flush();                  /* edits migrated from an older state blob */
        return;
    }
    if (strcmp(key, "all_notes_off") == 0) { tp5_all_notes_off(&in->e); return; }
    id = tp5_param_id_from_key(key);
    if (id < 0) return;
    tp5_set_param(&in->e, id, strtof(val, NULL));
    if ((id == P_SAVE || id == P_REVERT) && tp5_overlay_dirty()) tp5_overlay_flush();   /* host thread, never the audio thread */
    /* The host's preset browser only writes `preset` while scrolling, so selecting a program loads it. */
    if (id == P_PRESET) tp5_load_preset(&in->e, tp5_bank_slot((int)in->e.p[P_BANK], (int)in->e.p[P_PRESET], (int)in->e.p[P_SORT]));
}

static int put(char *buf, int cap, const char *s)
{
    size_t n = strlen(s);
    if ((int)n >= cap) return -1;
    memcpy(buf, s, n + 1);
    return (int)n;
}

static int get_param(void *instance, const char *key, char *buf, int cap)
{
    instance_t *in = (instance_t *)instance;
    int id, n;
    if (!in || !key || !buf || cap < 1) return -1;

    if (strcmp(key, "chain_params") == 0) return tp5_json_chain_params(buf, cap);
    if (strcmp(key, "ui_hierarchy") == 0) return tp5_json_ui_hierarchy(buf, cap);
    if (strcmp(key, "bank_list") == 0) return tp5_json_bank_list(buf, cap);
    if (strcmp(key, "state") == 0) return tp5_state_save(&in->e, buf, cap);
    if (strcmp(key, "name") == 0) return put(buf, cap, "TinyPro5");
    if (strcmp(key, "preset_name") == 0 || strcmp(key, "patch_name") == 0)
        return tp5_preset_label((int)in->e.p[P_BANK], (int)in->e.p[P_PRESET], (int)in->e.p[P_SORT], buf, cap);
    if (strncmp(key, "preset_name:", 12) == 0)                 /* any entry's label in the active bank and order, for list UIs */
        return tp5_preset_label((int)in->e.p[P_BANK], atoi(key + 12), (int)in->e.p[P_SORT], buf, cap);
    if (strcmp(key, "preset_count") == 0) {                    /* size of the active bank */
        n = snprintf(buf, (size_t)cap, "%d", tp5_bank_size((int)in->e.p[P_BANK]));
        return (n < 0 || n >= cap) ? -1 : n;
    }
    if (strcmp(key, "bank_count") == 0) {
        n = snprintf(buf, (size_t)cap, "%d", tp5_bank_count());
        return (n < 0 || n >= cap) ? -1 : n;
    }
    if (strcmp(key, "bank_name") == 0) return put(buf, cap, tp5_bank_name((int)in->e.p[P_BANK]));
    if (strncmp(key, "bank_name:", 10) == 0) return put(buf, cap, tp5_bank_name(atoi(key + 10)));
    if (strncmp(key, "bank_size:", 10) == 0) {
        n = snprintf(buf, (size_t)cap, "%d", tp5_bank_size(atoi(key + 10)));
        return (n < 0 || n >= cap) ? -1 : n;
    }
    if (strcmp(key, "bank_report") == 0) return put(buf, cap, tp5_banks_report());
    if (strcmp(key, "can_reload") == 0) return put(buf, cap, tp5_can_reload(&in->e) ? "1" : "0");      /* read by canvas.js: is there anything to do? */
    if (strcmp(key, "can_save") == 0)   return put(buf, cap, tp5_can_save(&in->e) ? "1" : "0");
    if (strcmp(key, "can_revert") == 0) return put(buf, cap, tp5_can_revert(&in->e) ? "1" : "0");
    if (strcmp(key, "active_voices") == 0) {
        n = snprintf(buf, (size_t)cap, "%d", tp5_active_voices(&in->e));
        return (n < 0 || n >= cap) ? -1 : n;
    }
    id = tp5_param_id_from_key(key);
    if (id < 0) return -1;
    if (id == P_LOAD || id == P_SAVE || id == P_REVERT) {        /* the host draws a canvas button as a box holding this text: a caption, "NONE" when there is nothing to do */
        const char *cap_on = id == P_LOAD ? "RELD" : id == P_SAVE ? "SAVE" : "REVT";
        return put(buf, cap, action_available(&in->e, id) ? cap_on : "NONE");
    }
    if (tp5_params[id].is_int) n = snprintf(buf, (size_t)cap, "%d", (int)in->e.p[id]);
    else n = snprintf(buf, (size_t)cap, "%.4f", (double)in->e.p[id]);
    return (n < 0 || n >= cap) ? -1 : n;
}

static int get_error(void *instance, char *buf, int cap)
{
    (void)instance;
    if (buf && cap > 0) buf[0] = 0;
    return 0;
}

static void render_block(void *instance, int16_t *out, int frames)
{
    instance_t *in = (instance_t *)instance;
    if (!in) { memset(out, 0, (size_t)frames * 2u * sizeof(int16_t)); return; }
    tp5_render_i16(&in->e, out, frames);
}

static plugin_api_v2_t g_api = {
    MOVE_PLUGIN_API_VERSION_2,
    create_instance, destroy_instance, on_midi, set_param, get_param, get_error, render_block
};

#if defined(__GNUC__)
__attribute__((visibility("default")))
#endif
plugin_api_v2_t *move_plugin_init_v2(const host_api_v1_t *host)
{
    if (host && host->sample_rate > 0) g_sample_rate = (float)host->sample_rate;
    return &g_api;
}
