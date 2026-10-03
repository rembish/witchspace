/* Read commander blocks (hex, one per line) and print their checksums, for
 * re/emu/cmdrtest.py; "default" prints the default block in hex. */
#include "ep_commander.h"

#include <stdio.h>
#include <string.h>

int main(int argc, char **argv)
{
    ep_commander c;
    if (argc > 1 && !strcmp(argv[1], "default")) {
        ep_commander_default(&c);
        for (int k = 0; k < EP_COMMANDER_SIZE; k++) printf("%02x", c.b[k]);
        printf(" %d\n", ep_commander_valid(&c));
        return 0;
    }
    for (;;) {
        for (int k = 0; k < EP_COMMANDER_SIZE; k++) {
            unsigned b;
            if (scanf("%2x", &b) != 1) return 0;
            c.b[k] = (uint8_t)b;
        }
        printf("%u\n", ep_commander_checksum(&c));
    }
}
