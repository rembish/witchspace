/* The test tools' copy of the original's tables, loaded before main: ELITE.EXE and ELITE.GRF
 * from $EP_ORIGINAL (default: the source tree's original/). Without them a tool exits 77
 * (skipped). Linked into every test tool, so their mains find the tables loaded. */
#include "ep_tables.h"

#include <stdio.h>
#include <stdlib.h>

#ifndef EP_SOURCE_ORIGINAL
#define EP_SOURCE_ORIGINAL "original"
#endif

static uint8_t *slurp(const char *dir, const char *name, size_t *len)
{
    char p[1100];
    snprintf(p, sizeof p, "%s/%s", dir, name);
    FILE *f = fopen(p, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *d = n > 0 ? malloc((size_t)n) : NULL;
    if (!d || fread(d, 1, (size_t)n, f) != (size_t)n) {
        free(d);
        d = NULL;
    }
    fclose(f);
    *len = (size_t)n;
    return d;
}

__attribute__((constructor)) static void load(void)
{
    const char *dir = getenv("EP_ORIGINAL") ? getenv("EP_ORIGINAL") : EP_SOURCE_ORIGINAL;
    size_t n;
    uint8_t *exe = slurp(dir, "ELITE.EXE", &n);
    if (!exe) {
        fprintf(stderr, "no %s/ELITE.EXE (set EP_ORIGINAL): skipped\n", dir);
        exit(77);
    }
    int r = ep_data_load(exe, n);
    free(exe);
    if (r) {
        fprintf(stderr, "%s/ELITE.EXE: %s\n", dir, ep_data_error(r));
        exit(77);
    }
    uint8_t *grf = slurp(dir, "ELITE.GRF", &n);
    if (grf) ep_data_grf(grf, n);
    free(grf);
}
