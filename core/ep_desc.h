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

/* Description of the system with seed s, as the Data on System screen shows it. */
void ep_system_description(const ep_seed *s, char out[EP_DESC_MAX + 1]);

#endif
