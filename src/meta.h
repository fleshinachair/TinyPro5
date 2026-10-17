/* Module metadata generated from the parameter table (single source of truth). */
#ifndef TINYPRO5_META_H
#define TINYPRO5_META_H

#include <stdint.h>

#define TP5_PAGES 4
#define TP5_KNOBS 8

/* Encoder layout: parameter id per knob, -1 = unused. */
extern const int8_t tp5_pages[TP5_PAGES][TP5_KNOBS];
extern const char  *tp5_page_names[TP5_PAGES];

/* JSON writers: return bytes written (excluding NUL) or -1 if buf is too small. */
int tp5_json_chain_params(char *buf, int cap);
int tp5_json_ui_hierarchy(char *buf, int cap);
int tp5_json_module(char *buf, int cap);
/* Runtime list for the host's "Select Bank" level: [{"index":N,"label":"Name (size)"}, ...] */
int tp5_json_bank_list(char *buf, int cap);

/* List entry `pos` in order `sort`. Factory bank: "group-program name", slot = (group-1)*8 + (program-1), so group 1 holds the basses and
 * sequences, 2 the keys and plucks, 3 the pads and swells, 4 the stingers, drones and effects. Custom banks: "slot name" (1-based).
 * Returns the length, or -1 if buf is too small / pos is out of range. */
int tp5_preset_label(int bank, int pos, int sort, char *buf, int cap);

#endif
