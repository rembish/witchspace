/* Commander files (see files.h). */
#include "files.h"

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif
#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char base[1024] = ".", data[1024] = "original";

void files_init(const char *dir, const char *data_dir)
{
    snprintf(base, sizeof base, "%s", dir);
    snprintf(data, sizeof data, "%s", data_dir);
}

static void path(char *out, size_t n, const char *name) { snprintf(out, n, "%s/%s", base, name); }

static int exists(void *ctx, const char *name)
{
    (void)ctx;
    char p[1100];
    path(p, sizeof p, name);
    FILE *f = fopen(p, "rb"); /* 3d00: it opens for reading */
    if (!f) return 0;
    fclose(f);
    return 1;
}

int files_find(const char *dir, const char *name, char *out, size_t n)
{
    snprintf(out, n, "%s/%s", dir, name);
    FILE *f = fopen(out, "rb");
    if (f) {
        fclose(f);
        return 1;
    }
    DIR *d = opendir(dir);
    if (!d) return 0;
    int found = 0;
    struct dirent *e;
    while (!found && (e = readdir(d))) {
        const char *a = e->d_name, *b = name;
        while (*a && *b && toupper((unsigned char)*a) == toupper((unsigned char)*b)) a++, b++;
        if (!*a && !*b) {
            snprintf(out, n, "%s/%s", dir, e->d_name);
            found = 1;
        }
    }
    closedir(d);
    return found;
}

static int read_file(void *ctx, const char *name, uint8_t *buf, int max)
{
    (void)ctx;
    char p[1100];
    size_t len = strlen(name);
    if (len > 4 && !strcmp(name + len - 4, ".MID")) { /* the music: with the game's files */
        if (!files_find(data, name, p, sizeof p)) return -1;
    } else {
        path(p, sizeof p, name);
    }
    FILE *f = fopen(p, "rb");
    if (!f) return -1;
    int n = (int)fread(buf, 1, (size_t)max, f);
    fclose(f);
    return n;
}

static int write_file(void *ctx, const char *name, const uint8_t *bytes, int len)
{
    (void)ctx;
    char p[1100];
    path(p, sizeof p, name);
    FILE *f = fopen(p, "wb");
    if (!f) return -1;
    int n = (int)fwrite(bytes, 1, (size_t)len, f);
    fclose(f);
#ifdef __EMSCRIPTEN__
    emscripten_run_script(
        "FS.syncfs(false, function(err) {})"); /* the commander kept in the browser's storage */
#endif
    return n;
}

/* *.CDR, as DOS matches it: names in capitals, 8.3 */
static int list(void *ctx, char names[][13], int max)
{
    (void)ctx;
    DIR *d = opendir(base);
    if (!d) return 0;
    int n = 0;
    struct dirent *e;
    while (n < max && (e = readdir(d))) {
        size_t len = strlen(e->d_name);
        if (len < 5 || len > 12) continue;
        char up[13];
        for (size_t k = 0; k <= len; k++) up[k] = (char)toupper((unsigned char)e->d_name[k]);
        if (strcmp(up + len - 4, ".CDR") || len - 4 > 8) continue;
        if (strcmp(up, e->d_name)) continue; /* the game writes capitals; others it would not find */
        memcpy(names[n++], up, len + 1);
    }
    closedir(d);
    return n;
}

const ep_io files_io = { NULL, exists, read_file, write_file, list };

uint8_t *files_slurp(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
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
