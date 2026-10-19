/*
 * Flat C ABI over the TinyPro5 engine for Python ctypes (build/libtinypro5_test.so).
 * One engine instance, no heap. Used only by tools/verify_presets.py.
 */
#include "../src/bank.h"
#include "../src/tinypro5.h"

#if defined(_WIN32)
#define TP5H __declspec(dllexport)
#else
#define TP5H __attribute__((visibility("default")))
#endif

static tp5_engine g_e;

TP5H void tp5h_init(float sr) { tp5_init(&g_e, sr); }
TP5H int tp5h_param_count(void) { return P_COUNT; }
TP5H int tp5h_param_id(const char *key) { return tp5_param_id_from_key(key); }
TP5H const char *tp5h_param_key(int id) { return (id >= 0 && id < P_COUNT) ? tp5_params[id].key : ""; }
TP5H float tp5h_param_min(int id) { return tp5_params[id].min; }
TP5H float tp5h_param_max(int id) { return tp5_params[id].max; }
TP5H float tp5h_param_def(int id) { return tp5_params[id].def; }
TP5H int tp5h_is_sound_param(int id) { return tp5_is_sound_param(id); }
TP5H float tp5h_get(int id) { return tp5_get_param(&g_e, id); }
TP5H void tp5h_set(int id, float v) { tp5_set_param(&g_e, id, v); }

TP5H int tp5h_preset_count(void) { return TINYPRO5_PRESET_COUNT; }
TP5H const char *tp5h_preset_name(int i) { return tp5_preset_name(i); }
TP5H int tp5h_preset_kv_count(int i) { const tp5_kv *kv; return tp5_bank_preset_kv(0, i, &kv); }
TP5H int tp5h_preset_kv(int i, int k, int *id, float *v)
{
    const tp5_kv *kv;
    const int n = tp5_bank_preset_kv(0, i, &kv);
    if (i < 0 || i >= TINYPRO5_PRESET_COUNT || k < 0 || k >= n) return 0;
    *id = kv[k].id;
    *v = kv[k].v;
    return 1;
}
TP5H void tp5h_load_preset(int i) { tp5_load_preset(&g_e, i); }

/* banks: scan a folder of .zip / .syx / .h2p files (the same code the module runs at start-up), then load any program of any bank */
TP5H int tp5h_scan(const char *dir) { return tp5_banks_scan(dir); }
TP5H const char *tp5h_scan_report(void) { return tp5_banks_report(); }
TP5H int tp5h_bank_count(void) { return tp5_bank_count(); }
TP5H int tp5h_bank_size(int b) { return tp5_bank_size(b); }
TP5H const char *tp5h_bank_name(int b) { return tp5_bank_name(b); }
TP5H const char *tp5h_bank_program_name(int b, int slot) { return tp5_bank_preset_name(b, slot); }
TP5H void tp5h_load_bank_program(int b, int slot)
{
    tp5_set_param(&g_e, P_BANK, (float)b);
    tp5_load_preset(&g_e, slot);
}

TP5H void tp5h_aftertouch(int value) { tp5_aftertouch(&g_e, value); }
TP5H void tp5h_note_on(int note, int vel) { tp5_note_on(&g_e, note, vel); }
TP5H void tp5h_note_off(int note) { tp5_note_off(&g_e, note); }
TP5H void tp5h_all_notes_off(void) { tp5_all_notes_off(&g_e); }
TP5H void tp5h_render(float *l, float *r, int frames) { tp5_render(&g_e, l, r, frames); }

TP5H float tp5h_peak_pre(void) { return g_e.diag_peak_pre; }
TP5H float tp5h_peak_post(void) { return g_e.diag_peak_post; }
TP5H unsigned tp5h_nan_resets(void) { return g_e.diag_nan_resets; }
TP5H float tp5h_ceiling(void) { return TINYPRO5_CEILING_LIN; }
TP5H float tp5h_makeup(void) { return TINYPRO5_OUTPUT_MAKEUP; }
TP5H void tp5h_reset_diag(void) { g_e.diag_peak_pre = 0.f; g_e.diag_peak_post = 0.f; g_e.diag_nan_resets = 0; }
