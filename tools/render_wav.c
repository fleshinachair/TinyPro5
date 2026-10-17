/*
 * Offline renderer: writes a 16-bit stereo WAV so the engine can be auditioned off-device.
 *   render_wav <out.wav> <preset 0..199> [seconds]
 * Plays a five-note chord on the chosen preset and releases it after two thirds of the length.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/tinypro5.h"

static tp5_engine g_e;
static int16_t g_buf[128 * 2];

static void w32(FILE *f, uint32_t v) { fputc(v & 255, f); fputc((v >> 8) & 255, f); fputc((v >> 16) & 255, f); fputc((v >> 24) & 255, f); }
static void w16(FILE *f, uint16_t v) { fputc(v & 255, f); fputc((v >> 8) & 255, f); }

int main(int argc, char **argv)
{
    static const int chord[5] = { 48, 55, 60, 63, 67 };
    const int sr = 44100;
    int total, done = 0, i;
    float secs = argc > 3 ? (float)atof(argv[3]) : 12.f;
    FILE *f;

    if (argc < 3) { fprintf(stderr, "usage: %s out.wav <preset> [seconds]\n", argv[0]); return 2; }
    tp5_init(&g_e, (float)sr);
    tp5_load_preset(&g_e, atoi(argv[2]));
    for (i = 0; i < 5; i++) tp5_note_on(&g_e, chord[i], 110);
    total = (int)(secs * (float)sr);
    f = fopen(argv[1], "wb");
    if (!f) { perror(argv[1]); return 1; }
    fwrite("RIFF", 1, 4, f); w32(f, 36u + (uint32_t)total * 4u); fwrite("WAVEfmt ", 1, 8, f);
    w32(f, 16); w16(f, 1); w16(f, 2); w32(f, (uint32_t)sr); w32(f, (uint32_t)sr * 4u); w16(f, 4); w16(f, 16);
    fwrite("data", 1, 4, f); w32(f, (uint32_t)total * 4u);
    while (done < total) {
        const int n = (total - done) > 128 ? 128 : (total - done);
        if (done > total * 2 / 3 && done - n <= total * 2 / 3) tp5_all_notes_off(&g_e);
        tp5_render_i16(&g_e, g_buf, n);
        fwrite(g_buf, 4, (size_t)n, f);
        done += n;
    }
    fclose(f);
    return 0;
}
