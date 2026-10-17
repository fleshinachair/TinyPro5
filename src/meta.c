#include "meta.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "bank.h"
#include "tinypro5.h"

const int8_t tp5_pages[TP5_PAGES][TP5_KNOBS] = {
    /* 1: Filter */
    { P_CUTOFF, P_RESO, P_ENVAMT, P_KBD, P_F_ATK, P_F_DEC, P_F_SUS, P_F_REL },
    /* 2: Oscillators (+ the two Poly-Mod amounts) */
    { P_A_WAVE, P_A_PW, P_B_WAVE, P_B_PW, P_B_SEMI, P_SYNC, P_PM_FENV, P_PM_OSCB },
    /* 3: Amp envelope + mixer */
    { P_A_ATK, P_A_DEC, P_A_SUS, P_A_REL, P_A_VOL, P_B_VOL, P_NOISE, P_VOLUME },
    /* 4: Voice + system (E5 = the preset list, E8 = list order; Reload / Save are push buttons that ask first, Revert is a list row after Save; the bank is chosen on the host's Select Bank page; PM-Dest, LFORate, LFOWave and Glide are on the More pages) */
    { P_UNISON, P_VOICES, P_DETUNE, P_LFO_AMT, P_PRESET, P_LOAD, P_SAVE, P_SORT },
};

const char *tp5_page_names[TP5_PAGES] = { "Filter", "Osc", "Amp+Mix", "Voice+Sys" };

/* Parameters that are not on an encoder page: still reachable from the host's page menu (levels "more1" / "more2"). */
#define TP5_EXTRA_PAGES 2
static const int8_t k_extra_pages[TP5_EXTRA_PAGES][TP5_KNOBS] = {
    { P_PM_DEST, P_LFO_RATE, P_LFO_WAVE, P_LFO_DEST, P_GLIDE, P_DRIFT, P_A_SEMI, P_B_FINE },
    { P_NOISE_COL, P_B_LFO, P_B_KBD, P_MODWHEEL, -1, -1, -1, -1 },
};
static const char *k_extra_names[TP5_EXTRA_PAGES] = { "More 1", "More 2" };

/* bounded appender: tracks overflow instead of writing past the end */
typedef struct { char *buf; int cap, len, bad, inline_meta; } jw;

static void jput(jw *w, const char *fmt, ...)
{
    va_list ap;
    int n;
    if (w->bad) return;
    va_start(ap, fmt);
    n = vsnprintf(w->buf + w->len, (size_t)(w->cap - w->len), fmt, ap);
    va_end(ap);
    if (n < 0 || n >= w->cap - w->len) { w->bad = 1; return; }
    w->len += n;
}

static int jfinish(jw *w) { return w->bad ? -1 : w->len; }

/* ---- parameter metadata the host UI renders from ---------------------------------------------------------------------
 * Stepped parameters are declared "enum" with their option names (the host shows the words, and reads/writes the index),
 * so Mode shows Poly / Unison / Mono / Legato instead of 0-3. Anything else is int or float with its range. */
#define TP5_MAX_OPTS 16

typedef struct { const char *opts[TP5_MAX_OPTS]; int n; } enum_def;

static char g_bank_opt[TP5_MAX_BANKS][TP5_BANK_NAME_LEN + 8];     /* bank names, escaped for JSON, rebuilt per call */

static void json_str(char *out, size_t cap, const char *in)        /* printable ASCII only, quotes and backslashes escaped */
{
    size_t o = 0;
    for (; *in && o + 3 < cap; in++) {
        const unsigned char c = (unsigned char)*in;
        if (c == '"' || c == '\\') { out[o++] = '\\'; out[o++] = (char)c; }
        else if (c >= 32 && c < 127) out[o++] = (char)c;
        else out[o++] = '?';
    }
    out[o] = 0;
}

static int enum_for(int id, enum_def *e)
{
    int i;
    memset(e, 0, sizeof *e);
    switch (id) {
    case P_UNISON: { static const char *o[] = { "Poly", "Unison", "Mono", "Legato" }; for (i = 0; i < 4; i++) e->opts[i] = o[i]; e->n = 4; return 1; }
    case P_PM_DEST: { static const char *o[] = { "Off", "FreqA", "PWA", "FreqA+PWA", "Filter", "FreqA+Flt", "PWA+Flt", "All" }; for (i = 0; i < 8; i++) e->opts[i] = o[i]; e->n = 8; return 1; }
    case P_LFO_WAVE: { static const char *o[] = { "Saw", "Triangle", "Square" }; for (i = 0; i < 3; i++) e->opts[i] = o[i]; e->n = 3; return 1; }
    case P_LFO_DEST: { static const char *o[] = { "Pitch", "PWM", "Filter", "Pitch+Flt", "PWM+Flt", "Pitch+PWM", "All", "Off" }; for (i = 0; i < 8; i++) e->opts[i] = o[i]; e->n = 8; return 1; }
    case P_SYNC: case P_B_LFO: case P_B_KBD: { static const char *o[] = { "Off", "On" }; e->opts[0] = o[0]; e->opts[1] = o[1]; e->n = 2; return 1; }
    case P_SORT: { static const char *o[] = { "Bank order", "A-Z" }; e->opts[0] = o[0]; e->opts[1] = o[1]; e->n = 2; return 1; }
    case P_BANK: {
        const int n = tp5_bank_count();
        for (i = 0; i < n && i < TP5_MAX_OPTS; i++) { json_str(g_bank_opt[i], sizeof g_bank_opt[i], tp5_bank_name(i)); e->opts[i] = g_bank_opt[i]; }
        e->n = i;
        return 1;
    }
    default: return 0;
    }
}

/* key, type, range, default and options of one parameter, without the surrounding braces or the name/label field */
static void put_meta(jw *w, int id)
{
    const tp5_param_info *p = &tp5_params[id];
    enum_def e;
    if (id == P_LOAD || id == P_SAVE || id == P_REVERT) {
        /* A push button that opens the module's own "are you sure?" screen (canvas.js): a click opens it, a knob turn does nothing,
         * Yes (jog + click) acts, No or Back cancels. See canvas.js. */
        jput(w, "\"type\":\"canvas\",\"canvas_script\":\"canvas.js#confirm_%s\",\"show_value\":false,\"show_footer\":false,\"enterable\":true",
             id == P_LOAD ? "reload" : id == P_SAVE ? "save" : "revert");
    } else if (enum_for(id, &e)) {
        int i;
        jput(w, "\"type\":\"enum\",\"options\":[");
        for (i = 0; i < e.n; i++) jput(w, "%s\"%s\"", i ? "," : "", e.opts[i]);
        jput(w, "]");
        jput(w, ",\"default\":%d", (int)p->def);
    } else if (id == P_PRESET) {                    /* sized at run time by preset_count */
        jput(w, "\"type\":\"int\",\"min\":0,\"max\":%d,\"default\":0", TINYPRO5_PRESET_MAX - 1);
    } else if (p->is_int) {
        jput(w, "\"type\":\"int\",\"min\":%d,\"max\":%d,\"default\":%d", (int)p->min, (int)p->max, (int)p->def);
    } else {
        jput(w, "\"type\":\"float\",\"min\":%g,\"max\":%g,\"default\":%g,\"step\":0.01", (double)p->min, (double)p->max, (double)p->def);
    }
}

static void put_chain_params(jw *w)
{
    int i;
    jput(w, "[");
    for (i = 0; i < P_COUNT; i++) {
        if (i) jput(w, ",");
        jput(w, "{\"key\":\"%s\",\"name\":\"%s\",", tp5_params[i].key, tp5_params[i].label);
        put_meta(w, i);
        jput(w, "}");
    }
    jput(w, "]");
}

int tp5_json_chain_params(char *buf, int cap)
{
    jw w = { buf, cap, 0, 0, 0 };
    if (cap > 0) buf[0] = 0;
    put_chain_params(&w);
    return jfinish(&w);
}

/* the runtime list behind the "Select Bank" level: [{"index":0,"label":"Factory (32)"}, ...] */
int tp5_json_bank_list(char *buf, int cap)
{
    jw w = { buf, cap, 0, 0, 0 };
    int i;
    if (cap > 0) buf[0] = 0;
    jput(&w, "[");
    for (i = 0; i < tp5_bank_count(); i++) {
        char nm[2 * TP5_BANK_NAME_LEN];
        json_str(nm, sizeof nm, tp5_bank_name(i));
        jput(&w, "%s{\"index\":%d,\"label\":\"%s (%d)\"}", i ? "," : "", i, nm, tp5_bank_size(i));
    }
    jput(&w, "]");
    return jfinish(&w);
}

static void put_knobs(jw *w, int page)
{
    int k;
    jput(w, "\"knobs\":[");
    for (k = 0; k < TP5_KNOBS; k++) {
        if (k) jput(w, ",");
        if (tp5_pages[page][k] >= 0) jput(w, "\"%s\"", tp5_params[tp5_pages[page][k]].key);
        else jput(w, "\"\"");
    }
    jput(w, "]");
}


int tp5_preset_label(int bank, int pos, int sort, char *buf, int cap)
{
    int n;
    if (pos < 0 || pos >= tp5_bank_size(bank) || !buf || cap < 1) return -1;
    {
        const int idx = tp5_bank_slot(bank, pos, sort);
        const char *nm = tp5_bank_preset_name(bank, idx);
        /* The label is the same in both orders (the order only changes which entries come first): a factory program keeps
         * its group-program address, so a list sorted by name still shows where each program lives. */
        if (bank == 0) {
            n = snprintf(buf, (size_t)cap, "%d-%d %s", idx / 8 + 1, idx % 8 + 1, nm);
        } else {         /* custom banks have no group/program address: the slot number (1-based) stands in */
            n = snprintf(buf, (size_t)cap, "%d %s", idx + 1, nm);
        }
    }
    return (n < 0 || n >= cap) ? -1 : n;
}

/* Declares one knob page entry: key + label + the same metadata chain_params carries (the host merges both). */
static void put_page_param(jw *w, int id, const char *label)
{
    jput(w, "{\"key\":\"%s\",\"label\":\"%s\"", tp5_params[id].key, label);
    if (w->inline_meta) {            /* module.json stays small (the standalone loader caps it at 8 KB): chain_params has it */
        jput(w, ",");
        put_meta(w, id);
    }
    jput(w, "}");
}

/* The root level is the host's preset browser: scrolling writes `preset` (which loads the program) and reads
 * `preset_name` ("group-bank-program name"), with `preset_count` sizing the list. The "Banks" level holds the list order
 * and a "Select Bank" list (runtime items from `bank_list`, the choice written to `bank`, then back to the preset browser). */
static void put_ui_hierarchy(jw *w)
{
    int pg, k;
    jput(w, "{\"levels\":{\"root\":{\"name\":\"TinyPro5\",\"label\":\"TinyPro5\",\"list_param\":\"preset\","
            "\"count_param\":\"preset_count\",\"name_param\":\"preset_name\",\"params\":[");
    for (pg = 0; pg < TP5_PAGES; pg++)
        jput(w, "%s{\"level\":\"page%d\",\"label\":\"%s\"}", pg ? "," : "", pg + 1, tp5_page_names[pg]);
    for (pg = 0; pg < TP5_EXTRA_PAGES; pg++)
        jput(w, ",{\"level\":\"more%d\",\"label\":\"%s\"}", pg + 1, k_extra_names[pg]);
    jput(w, ",{\"level\":\"bank_list\",\"label\":\"Select Bank\"}],");
    jput(w, "\"knobs\":[]}");                      /* the encoder pages are their own levels (Filter, Osc, ...) */
    for (pg = 0; pg < TP5_PAGES; pg++) {
        jput(w, ",\"page%d\":{\"name\":\"%s\",\"params\":[", pg + 1, tp5_page_names[pg]);
        for (k = 0; k < TP5_KNOBS; k++) {
            if (tp5_pages[pg][k] < 0) continue;
            if (k) jput(w, ",");
            put_page_param(w, tp5_pages[pg][k], tp5_params[tp5_pages[pg][k]].label);
            if (tp5_pages[pg][k] == P_SAVE) {        /* the 8 encoders are full: Revert is a 9th list entry, right after Save */
                jput(w, ",");
                put_page_param(w, P_REVERT, tp5_params[P_REVERT].label);
            }
        }
        jput(w, "],");
        put_knobs(w, pg);
        jput(w, "}");
    }
    for (pg = 0; pg < TP5_EXTRA_PAGES; pg++) {
        int first = 1;
        jput(w, ",\"more%d\":{\"name\":\"%s\",\"params\":[", pg + 1, k_extra_names[pg]);
        for (k = 0; k < TP5_KNOBS; k++) {
            if (k_extra_pages[pg][k] < 0) continue;
            if (!first) jput(w, ",");
            first = 0;
            put_page_param(w, k_extra_pages[pg][k], tp5_params[k_extra_pages[pg][k]].label);
        }
        jput(w, "],\"knobs\":[");
        for (k = 0, first = 1; k < TP5_KNOBS; k++) {
            if (k_extra_pages[pg][k] < 0) continue;
            jput(w, "%s\"%s\"", first ? "" : ",", tp5_params[k_extra_pages[pg][k]].key);
            first = 0;
        }
        jput(w, "]}");
    }
    jput(w, ",\"bank_list\":{\"name\":\"Select Bank\",\"label\":\"Select Bank\",\"items_param\":\"bank_list\","
            "\"select_param\":\"bank\",\"navigate_to\":\"root\"}");
    jput(w, "}}");
}

int tp5_json_ui_hierarchy(char *buf, int cap)
{
    jw w = { buf, cap, 0, 0, 1 };
    if (cap > 0) buf[0] = 0;
    put_ui_hierarchy(&w);
    return jfinish(&w);
}

int tp5_json_module(char *buf, int cap)
{
    jw w = { buf, cap, 0, 0, 0 };
    if (cap > 0) buf[0] = 0;
    jput(&w, "{\n  \"id\": \"tinypro5\",\n  \"name\": \"TinyPro5\",\n  \"abbrev\": \"TP5\",\n"
             "  \"version\": \"1.0.0\",\n  \"api_version\": 2,\n  \"category\": \"instrument\",\n"
             "  \"author\": \"TinyPro5\",\n"
             "  \"description\": \"Prophet-5 Rev 2 style 5-voice virtual analog: SSM 2040 filter, Poly-Mod, "
             "drift and 32 original factory programs\",\n"
             "  \"dsp\": \"dsp.so\",\n  \"ui\": \"ui.js\",\n"
             "  \"capabilities\": {\n    \"audio_out\": true,\n    \"midi_in\": true,\n    \"chainable\": true,\n"
             "    \"component_type\": \"sound_generator\",\n    \"ui_hierarchy\": ");
    put_ui_hierarchy(&w);
    jput(&w, ",\n    \"chain_params\": ");
    put_chain_params(&w);
    jput(&w, "\n  }\n}\n");
    return jfinish(&w);
}
