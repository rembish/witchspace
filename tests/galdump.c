/* Print every system of every galaxy, one line each, for re/emu/galaxytest.py. */
#include "ep_galaxy.h"

#include <stdio.h>

int main(void)
{
    for (int g = 0; g < EP_GALAXIES; g++) {
        ep_seed s = ep_galaxy_seed(g);
        for (int n = 0; n < EP_SYSTEMS; n++) {
            ep_system d;
            ep_system_data(&s, &d);
            printf("%d %3d %04x%04x%04x %-8s x%3u y%3u gov%u eco%u tech%2u pop%3u prod%5u rad%5u "
                   "sp",
                   g, n, s.w[0], s.w[1], s.w[2], d.name, d.x, d.y, d.government, d.economy, d.tech,
                   d.population, d.productivity, d.radius);
            if (d.species[0] == 0xff)
                printf(" human");
            else
                printf(" %u %u %u %u", d.species[0], d.species[1], d.species[2], d.species[3]);
            printf(" desc %04x %04x\n", d.desc_seed[0], d.desc_seed[1]);
            for (int k = 0; k < 4; k++) ep_twist(&s);
        }
    }
    return 0;
}
