/* Broken game files must be rejected cleanly: run under AddressSanitizer and UBSan in CI, with
 * no game files needed. Hand-made cases (empty, cut short, an EXEPACK stub at CS 0, offsets
 * past the end) and seeded random ones go to the EXE loader (ep_data_load), the core's GRF
 * reader (ep_data_grf) and the frontend's GRF decoder (grf_parse). With a copy of the game in
 * $EP_ORIGINAL the real files are cut and mutated too, and must still load whole.
 *   ep_badinput */
#include "ep_tables.h"
#include "grf.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;

static void expect(int ok, const char *what)
{
    if (!ok) {
        printf("FAIL: %s\n", what);
        failures++;
    }
}

static uint32_t seed = 12345;
static uint32_t rnd(void)
{
    seed = seed * 1103515245u + 12345u;
    return seed >> 8;
}

static void put16(uint8_t *p, unsigned v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

/* an MZ header: the whole file is image after a 32-byte header, entered at cs:10h */
static size_t mz(uint8_t *b, size_t len, unsigned cs)
{
    memset(b, 0, 32);
    b[0] = 'M';
    b[1] = 'Z';
    put16(b + 2, (unsigned)(len % 512));
    put16(b + 4, (unsigned)((len + 511) / 512));
    put16(b + 8, 2); /* header paragraphs */
    put16(b + 0x14, 0x10);
    put16(b + 0x16, cs);
    return len;
}

/* the loader given an exact-size heap copy, so any read outside the input is caught */
static int load(const uint8_t *b, size_t n)
{
    uint8_t *c = malloc(n ? n : 1);
    if (!c) return EP_DATA_OK;
    memcpy(c, b, n);
    int r = ep_data_load(c, n);
    free(c);
    return r;
}

static int grf_both(const uint8_t *b, size_t n)
{
    uint8_t *c = malloc(n ? n : 1);
    if (!c) return 1;
    memcpy(c, b, n);
    int r = ep_data_grf(c, n) != 0 && !grf_parse(c, n);
    free(c);
    return r;
}

static void exe_cases(void)
{
    static uint8_t b[4096];
    expect(load(b, 0) != EP_DATA_OK, "empty EXE");
    expect(ep_data_load((const uint8_t *)"M", 1) != EP_DATA_OK, "1-byte EXE");
    /* an EXEPACK stub at CS 0: no packed data before it (the review's case) */
    size_t n = mz(b, 32 + 64, 0);
    memcpy(b + 32 + 14, "RB", 2);
    put16(b + 32 + 12, 153360 / 16); /* the size it unpacks to: the real one */
    expect(load(b, n) != EP_DATA_OK, "EXEPACK stub at cs 0");
    b[29] = 0x00, b[30] = 0x01, b[31] = 0xb3; /* a copy command just before the image */
    expect(load(b, n) != EP_DATA_OK, "EXEPACK stub at cs 0, a command before it");
    /* a stub past the end */
    n = mz(b, 32 + 64, 0x400);
    expect(load(b, n) != EP_DATA_OK, "EXEPACK stub past the end");
    /* a stub whose commands ask for more than there is */
    n = mz(b, 32 + 256, 8);
    uint8_t *img = b + 32;
    memset(img, 0xb2, 128);
    memcpy(img + 128 + 14, "RB", 2);
    put16(img + 128 + 12, 153360 / 16);
    expect(load(b, n) != EP_DATA_OK, "EXEPACK copy past the start");
    img[127] = 0xb1; /* a fill of a huge count */
    img[126] = 0xff;
    img[125] = 0xff;
    expect(load(b, n) != EP_DATA_OK, "EXEPACK fill past the start");
    for (int k = 0; k < 20000; k++) { /* random headers, stubs and commands */
        size_t len = 32 + rnd() % 2048;
        mz(b, len, rnd() % 160);
        for (size_t i = 32; i < len; i++) b[i] = (uint8_t)rnd();
        if (rnd() & 1) {
            size_t at = 32 + (size_t)(b[0x16] | b[0x17] << 8) * 16;
            if (at + 16 <= len) {
                memcpy(b + at + 14, "RB", 2);
                if (rnd() & 1) put16(b + at + 12, 153360 / 16);
            }
        }
        if (rnd() % 4 == 0) b[2 + rnd() % 6] = (uint8_t)rnd(); /* sizes that lie */
        expect(load(b, len) != EP_DATA_OK, "random EXE accepted");
    }
}

static void grf_cases(void)
{
    static uint8_t b[4096];
    expect(grf_both(b, 0), "empty GRF");
    expect(grf_both(b, 1), "1-byte GRF");
    memset(b, 0, 16);
    b[10] = 0xff, b[11] = 0xff, b[14] = 1; /* the pictures past the end */
    expect(grf_both(b, 16), "GRF offset past the end");
    memset(b, 0, 32);
    b[10] = 16, b[14] = 3; /* three pictures, the first 10 x 10, then nothing */
    b[16] = 10, b[17] = 0, b[18] = 10;
    expect(!grf_parse(b, 19), "GRF cut short");
    for (int k = 0; k < 20000; k++) {
        size_t len = rnd() % 1024;
        for (size_t i = 0; i < len; i++) b[i] = (uint8_t)rnd();
        if (len >= 16 && (rnd() & 1)) b[10] = 16, b[11] = b[12] = b[13] = 0;
        grf_both(b, len); /* either way, no reading outside */
    }
}

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
    if (d && fread(d, 1, (size_t)n, f) != (size_t)n) {
        free(d);
        d = NULL;
    }
    fclose(f);
    *len = d ? (size_t)n : 0;
    return d;
}

/* the real files, if there: they load; cut or mutated, they are rejected or load safely */
static void real_cases(const char *dir)
{
    size_t n;
    uint8_t *exe = slurp(dir, "ELITE.EXE", &n);
    if (exe) {
        expect(ep_data_load(exe, n) == EP_DATA_OK, "the real ELITE.EXE loads");
        uint8_t *m = malloc(n);
        for (int k = 0; k < 300 && m; k++) {
            memcpy(m, exe, n);
            size_t cut = k < 100 ? rnd() % n : n;
            for (int j = 0; k >= 100 && j < 8; j++) m[rnd() % n] = (uint8_t)rnd();
            ep_data_load(m, cut);
        }
        free(m);
        expect(ep_data_load(exe, n) == EP_DATA_OK, "the real ELITE.EXE loads again");
        free(exe);
    }
    uint8_t *grf = slurp(dir, "ELITE.GRF", &n);
    if (grf) {
        expect(ep_data_grf(grf, n) == 0, "the real ELITE.GRF's widths read");
        expect(grf_parse(grf, n), "the real ELITE.GRF decodes");
        for (int k = 0; k < 100; k++) grf_parse(grf, rnd() % n);
        free(grf);
    }
    printf("real files from %s: %s\n", dir, exe || grf ? "checked" : "none");
}

int main(void)
{
    exe_cases();
    grf_cases();
    const char *dir = getenv("EP_ORIGINAL");
    if (dir) real_cases(dir);
    printf("%s\n", failures ? "broken inputs: FAILED" : "broken inputs: all rejected cleanly");
    return failures ? 1 : 0;
}
