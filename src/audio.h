/* The sound: the PC speaker, as the core's sequencer sets it (a square wave at 1193182 /
 * divisor Hz while the speaker is on), and the AdLib's chip (an OPL2, emulated by Nuked
 * OPL3) given the writes the core's music driver makes. */
#ifndef AUDIO_H
#define AUDIO_H

#include <stdint.h>

void audio_init(void);
void audio_speaker(uint16_t divisor, int on);

/* writes to the chip (register, value) made `ago` seconds before now: they play as far apart
 * as they were made, a little later than now */
void audio_opl(const uint8_t (*writes)[2], int n, double ago);

void audio_quit(void);

#endif
