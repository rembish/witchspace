/* Elite Plus system descriptions ("goat soup"), reconstructed from ELITE.EXE.
 *
 * describe_system (632b) expands a template with the text printer at 6396. Bytes 1..6 are
 * control codes (system name, name + "ian", random name, backspace, capitals on/off), bytes
 * from 0x80 pick one of five alternatives with the description generator seeded from the
 * system (ds:8351/8353), everything else is copied, dropping doubled spaces.
 */
#ifndef EP_DESC_H
#define EP_DESC_H

#include "ep_galaxy.h"

#define EP_DESC_MAX 256 /* the original's buffer at ds:5a3e */

/* 632b on the game's own state: the description into out (the buffer at ds:5a3e, which the
 * caller has zeroed), using and advancing the description seeds, the name buffer (ds:8338,
 * cut at its length; random names pass through it), the global seed and ds:5a2b, 5a34 */
typedef struct {
    uint8_t *out;   /* EP_DESC_MAX bytes */
    uint8_t before; /* the byte before the buffer (ds:5a3d) */
    uint16_t *r0, *r1;
    uint8_t *name; /* EP_NAMEBUF bytes */
    ep_seed *seed;
    uint8_t *caps;
    uint8_t *save; /* 8 bytes */
} ep_desc_io;
void ep_describe(ep_desc_io *io);

/* Description of the system with seed s, as the Data on System screen shows it. */
void ep_system_description(const ep_seed *s, char out[EP_DESC_MAX + 1]);

#endif
