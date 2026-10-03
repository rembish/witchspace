/* Read planet slots (one per line: player angles 0..2, extra angle, then 64 bytes in hex) and
 * print the slot after planet_to_camera and the apparent sizes 100 and 50, for
 * re/emu/planettest.py. */
#include "ep_objects.h"

#include <stdio.h>

int main(void)
{
    static ep_space s;
    unsigned p0, p1, p2, x;
    while (scanf("%u %u %u %u", &p0, &p1, &p2, &x) == 4) {
        s.player_angle[0] = (uint16_t)p0;
        s.player_angle[1] = (uint16_t)p1;
        s.player_angle[2] = (uint16_t)p2;
        s.extra_angle = (uint16_t)x;
        for (int k = 0; k < 3; k++) s.rot[k] = ep_rot_from_angle(s.player_angle[k]);
        ep_object *o = &s.obj[0];
        for (int k = 0; k < 64; k++) {
            unsigned b;
            if (scanf("%2x", &b) != 1) return 1;
            o->b[k] = (uint8_t)b;
        }
        ep_planet_to_camera(&s, o);
        printf("%u %u ", ep_apparent_size(o, 100), ep_apparent_size(o, 50));
        for (int k = 0; k < 64; k++) printf("%02x", o->b[k]);
        printf("\n");
    }
    return 0;
}
