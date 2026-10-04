/* The sound: the PC speaker, as the core's sequencer sets it (a square wave at 1193182 /
 * divisor Hz while the speaker is on), and the AdLib's chip (an OPL2, emulated by Nuked
 * OPL3) given the writes the core's music driver makes. */
#ifndef AUDIO_H
#define AUDIO_H

#include <stddef.h>
#include <stdint.h>

/* the chip reset and the SDL audio device opened (no sound, silently, if there is none) */
void audio_init(void);
/* the speaker as the core left it (g->speaker, g->speaker_on); a divisor of 0 is silent */
void audio_speaker(uint16_t divisor, int on);

/* writes to the chip (register, value) made `ago` seconds before now: they play as far apart
 * as they were made, a little later than now */
void audio_opl(const uint8_t (*writes)[2], int n, double ago);

/* the title's theme, an MP3 file's bytes (malloc'd; kept, and freed by audio_quit or the next
 * theme): 1 if it decodes, else 0 and the bytes are the caller's */
int audio_theme_load(uint8_t *data, size_t len);
/* the theme playing (from its start each time it is turned on, again at its end) or not */
void audio_theme(int play);

/* the device closed */
void audio_quit(void);

#endif
