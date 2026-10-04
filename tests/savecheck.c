/* Saving a commander through the frontend's files (src/files.c) keeps the old one whenever the
 * new one cannot be put in place, and leaves no temporary file: a fresh save, a replacement, a
 * replacement that cannot happen (a folder where the file should be), and a folder that cannot
 * be written. Needs no game files.
 *   ep_savecheck [scratch folder] */
#include "files.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int failures;
static char dir[1024], p[1100], tmp[1110];

static void expect(int ok, const char *what)
{
    if (!ok) {
        printf("FAIL: %s\n", what);
        failures++;
    }
}

static int contents(const char *path, char *out, size_t n)
{
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    size_t k = fread(out, 1, n - 1, f);
    out[k] = 0;
    fclose(f);
    return (int)k;
}

static int exists(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0;
}

static int save(const char *text)
{
    return files_io.write(NULL, "JAMESON.CDR", (const uint8_t *)text, (int)strlen(text));
}

int main(int argc, char **argv)
{
    snprintf(dir, sizeof dir, "%s", argc > 1 ? argv[1] : "savecheck.tmpdir");
    mkdir(dir, 0755);
    files_init(dir, dir);
    char got[64];

    snprintf(p, sizeof p, "%s/JAMESON.CDR", dir);
    remove(p);
    expect(save("first") == 5, "a fresh save is written");
    expect(contents(p, got, sizeof got) == 5 && !strcmp(got, "first"), "a fresh save reads back");
    expect(save("second") == 6, "a replacement is written");
    expect(contents(p, got, sizeof got) == 6 && !strcmp(got, "second"), "a replacement reads back");
    snprintf(tmp, sizeof tmp, "%s/JAMESON.CDR.tmp", dir);
    expect(!exists(tmp), "no temporary file is left");

    /* the replacement cannot happen: a folder where the commander should go */
    snprintf(p, sizeof p, "%s/BLOCKED.CDR", dir);
    mkdir(p, 0755);
    expect(files_io.write(NULL, "BLOCKED.CDR", (const uint8_t *)"new", 3) == -1,
           "a failed replacement says so");
    expect(exists(p), "a failed replacement leaves what was there");
    snprintf(tmp, sizeof tmp, "%s/BLOCKED.CDR.tmp", dir);
    expect(!exists(tmp), "a failed replacement leaves no temporary file");
    rmdir(p);

    /* a folder that cannot be written (not as root, which may write anyway) */
    if (geteuid() != 0) {
        chmod(dir, 0555);
        snprintf(p, sizeof p, "%s/JAMESON.CDR", dir);
        expect(save("third") == -1, "a save into a read-only folder says so");
        expect(contents(p, got, sizeof got) == 6 && !strcmp(got, "second"), "the old commander is kept");
        chmod(dir, 0755);
    }

    snprintf(p, sizeof p, "%s/JAMESON.CDR", dir);
    remove(p);
    rmdir(dir);
    printf("%s\n", failures ? "saves: FAILED" : "saves: whole or not at all, the old one kept");
    return failures ? 1 : 0;
}
