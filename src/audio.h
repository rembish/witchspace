/* The PC speaker, as the core's sequencer sets it: a square wave at 1193182 / divisor Hz while
 * the speaker is on. */
#ifndef AUDIO_H
#define AUDIO_H

#include <stdint.h>

void audio_init(void);
void audio_speaker(uint16_t divisor, int on);
void audio_quit(void);

#endif
