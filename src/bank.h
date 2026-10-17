/*
 * TinyPro5 preset banks: the built-in 32 factory programs (bank 0, src/presets_factory.c) plus any custom banks found in <module>/banks/.
 *
 *   h2p.c      pure code: u-he .h2p text parser, Prophet-5 .syx decoder, parameter mappings, zip reader + inflate
 *   bank.c     pure code: the bank registry (static storage) and the per-bank bank / alphabetical orders
 *   overlay.c  pure code: the user overlay (saved edits of any bank, kept outside the bank files)
 *   bankscan.c the ONLY file IO: finds .zip, .syx and .h2p files and folders in <module>/banks/ at module creation
 *
 * Nothing here runs in the audio path. All storage is static (no heap).
 */
#ifndef TINYPRO5_BANK_H
#define TINYPRO5_BANK_H

#include <stddef.h>
#include <stdint.h>

#include "tinypro5.h"

#define TP5_MAX_BANKS         16
#define TP5_BANK_MAX_PRESETS  1024     /* per bank (the preset parameter goes 0..1023) */
#define TP5_BANK_NAME_LEN     28
#define TP5_BANK_POOL_PRESETS 6144     /* all custom banks together */
#define TP5_BANK_KV_PER_PRESET 40

/* ---- u-he .h2p ------------------------------------------------------------------------------------------------ */
enum {
    H_FENV_ATK, H_FENV_DEC, H_FENV_SUS, H_FENV_REL,
    H_AENV_ATK, H_AENV_DEC, H_AENV_SUS, H_AENV_REL,
    H_A_OCT, H_A_FREQ, H_A_FINE, H_A_SAW, H_A_RECT, H_A_PW, H_A_SYNC, H_A_VFM, H_A_VPWM,
    H_B_OCT, H_B_FREQ, H_B_FINE, H_B_SAW, H_B_TRI, H_B_RECT, H_B_PW, H_B_LOW, H_B_KPITCH,
    H_VMOD_B, H_VMOD_ENV,
    H_MIX_A, H_MIX_B, H_MIX_NOISE,
    H_FLT_CUT, H_FLT_RES, H_FLT_KEY, H_FLT_ENV, H_FLT_VMOD, H_FLT_POL,
    H_GLIDE,
    H_COUNT
};

typedef struct {
    float   v[H_COUNT];
    uint8_t has[H_COUNT];
    int     n_keys;      /* recognised keys seen */
} tp5_h2p;

/* Parse the text part of a .h2p (everything up to the binary tail). Returns 1 for a usable Repro-5 patch (header absent or
 * `#AM=Repro-5`, at least one recognised key), 0 otherwise (another instrument, empty, binary garbage). Never reads past len. */
int tp5_h2p_parse(const char *text, size_t len, tp5_h2p *out);

/* Repro-5 dial values (0-100 displays) -> TinyPro5 parameters. Keys missing from the patch emit nothing, so the engine
 * default applies. Returns the number of tp5_kv written (never more than cap). */
int tp5_h2p_to_kv(const tp5_h2p *h, tp5_kv *out, int cap);

/* ---- Prophet-5 .syx program dumps: decoded only from the user's own bank files, none ships with the module --- */
/* Next program frame (F0 01 <31|32> 02|03 ...) at or after *pos: unpacks the 7-in-8 bytes to `raw` (133 bytes, zero padded) and
 * the 20-character name. Returns 1 for a program, 0 at the end of data; damaged / foreign frames are skipped. */
int tp5_syx_next(const uint8_t *data, size_t len, size_t *pos, uint8_t raw[133], char name[24]);
/* Dial positions -> engine parameters (Repro-5-calibrated laws). Returns the tp5_kv count. */
int tp5_p5_to_kv(const uint8_t *raw, tp5_kv *out, int cap);

/* ---- zip ------------------------------------------------------------------------------------------------------ */
/* Raw DEFLATE (RFC 1951). Returns 0 on success; outlen receives the byte count. Fails cleanly on corrupt or oversized data. */
int tp5_inflate(const uint8_t *in, size_t inlen, uint8_t *out, size_t outcap, size_t *outlen);

/* Calls cb(name, data, len, user) for every stored/deflated file entry whose name ends in `suffix` (case-insensitive) and whose
 * unpacked size fits `scratch`. A non-zero return from cb stops the walk. Returns the number of entries delivered, or <0 on a
 * malformed archive (-1 no end record, -2 bad directory, -3 unsupported: encrypted / zip64). */
typedef int (*tp5_zip_cb)(const char *name, const uint8_t *data, size_t len, void *user);
int tp5_zip_walk(const uint8_t *zip, size_t zlen, const char *suffix, uint8_t *scratch, size_t scratch_cap,
                 tp5_zip_cb cb, void *user);

/* ---- registry (bank 0 = the factory programs) --------------------------------------------------------------------- */
void        tp5_banks_reset(void);                      /* forget all custom banks */
int         tp5_bank_begin(const char *name);           /* new empty custom bank, or -1 when full */
int         tp5_bank_add(int bank, const char *name, const tp5_kv *kv, int n);   /* 0 on success, -1 when full */
int         tp5_bank_end(int bank);                     /* close the newest bank: builds its A-Z order; an empty bank is dropped. Returns its size */
int         tp5_bank_count(void);
const char *tp5_bank_name(int bank);
int         tp5_bank_size(int bank);
const char *tp5_bank_preset_name(int bank, int slot);
float       tp5_bank_gain(int bank, int slot);                                   /* per-program output gain (>= 1): levels the factory bank, 1 elsewhere */
int         tp5_bank_preset_kv(int bank, int slot, const tp5_kv **kv);           /* count; for bank 0 the compiled-in factory table */
/* list position <-> slot, in bank order (sort 0) or case-insensitive A-Z (sort 1) */
int         tp5_bank_slot(int bank, int pos, int sort);
int         tp5_bank_pos(int bank, int slot, int sort);

/* ---- user overlay: Save works on every bank without touching the bank files (src/overlay.c) ------------------------------ */
/* An edit saved over program `slot` of `bank` is kept under the bank's name and applied on top of the freshly parsed program every
 * time it loads, as long as the program still has the name it had when saved. One text file per bank in <module>/user/ makes it
 * survive restarts (src/bankscan.c); the .zip / .h2p / .syx files themselves are never written. */
void tp5_overlay_clear(void);                                    /* forget every override (memory only) */
int  tp5_overlay_get(int bank, int slot, float *vals);           /* P_COUNT floats; 1 when an override exists and still matches */
int  tp5_overlay_put(int bank, int slot, const float *vals);     /* P_COUNT floats; 0 ok, -1 when the store is full */
int  tp5_overlay_remove(int bank, int slot);                     /* 1 when something was removed */
int  tp5_overlay_count(void);
int  tp5_overlay_dirty(void);                                    /* banks with changes not yet written */
int  tp5_overlay_dirty_bank(int i, char *out, size_t cap);       /* name of the i-th dirty bank */
void tp5_overlay_clean_bank(const char *bank);
int  tp5_overlay_serialize(const char *bank, char *buf, int cap);
int  tp5_overlay_parse(const char *text, size_t len);            /* adds the entries of one file; returns how many */
/* file IO (bankscan.c) */
void tp5_overlay_set_dir(const char *dir);                       /* where the per-bank files live (default: <banks folder>/../user) */
int  tp5_overlay_load_dir(void);                                 /* reads every *.tp5u there into the store; returns entries read */
int  tp5_overlay_flush(void);                                    /* writes every dirty bank; returns files written */

/* ---- scanning (file IO, run once from create_instance) ------------------------------------------------------------- */
int         tp5_banks_scan(const char *banks_dir);      /* returns the number of custom banks found */
const char *tp5_banks_report(void);                     /* one-line summary of the last scan */

#endif
