/* Emits module.json from the parameter table so metadata can never drift from the DSP. */
#include <stdio.h>

#include "../src/meta.h"

static char g_buf[8192];

int main(int argc, char **argv)
{
    FILE *f = stdout;
    int n = tp5_json_module(g_buf, (int)sizeof g_buf);
    if (n < 0) { fprintf(stderr, "module.json exceeds the 8 KB loader limit\n"); return 1; }
    if (argc > 1) {
        f = fopen(argv[1], "wb");
        if (!f) { perror(argv[1]); return 1; }
    }
    fwrite(g_buf, 1, (size_t)n, f);
    if (f != stdout) fclose(f);
    return 0;
}
