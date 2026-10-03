/* Start the core as the original starts, for re/emu/boottest.py.
 *   bootdump VIDEO SOUND MINUTE SECOND HUNDREDTHS WORD OUT
 * writes the data segment as the core models it (the protection's question answered with
 * WORD) */
#include "ep_boot.h"
#include "ep_dsmap.h"
#include "ep_station.h"

#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv)
{
    if (argc != 8) return 2;
    static ep_game g;
    ep_boot(&g, (uint8_t)atoi(argv[1]), (uint8_t)atoi(argv[2]), (uint8_t)atoi(argv[3]),
            (uint8_t)atoi(argv[4]), (uint8_t)atoi(argv[5]));
    int w = ep_protection_ask(&g);
    for (const char *c = argv[6]; w == EP_WAIT_TEXT && *c; c++) w = ep_station_key(&g, (uint8_t)*c);
    if (w == EP_WAIT_TEXT) ep_station_key(&g, 0x0d);
    if (g.f.protection_failed) printf("protection: wrong\n");
    static uint8_t ds[EP_DS_SIZE];
    ep_ds_store(&g, ds);
    FILE *f = fopen(argv[7], "wb");
    if (!f || fwrite(ds, 1, sizeof ds, f) != sizeof ds) return 2;
    fclose(f);
    return 0;
}
