/* Elite Plus sound, reconstructed from ELITE.EXE: what the game asks for (events for the
 * frontend) and the PC speaker's sequencer, which runs in the timer interrupt.
 *
 * Every sound goes through 4c98 (EP_EV_SOUND, its number). With the speaker (ds:4801 = 2) a
 * number picks one of twelve sequences (through ds:45c0, or directly with bit 7 set); each
 * timer tick (4a50) the sequencer steps it: a sequence is notes (a pattern, a pitch, a length)
 * and rests; a pattern bends the pitch tick by tick, waits, loops, or turns noise on. What it
 * sets the speaker to is g->speaker (the PIT divisor, 1193182 / Hz) and g->speaker_on; the
 * frontend sounds that. The AdLib and Roland drivers (segment 2270) are the frontend's: it
 * gets the same events.
 *
 * ds:45ea, the sequencer's flags: 1 stopped (a sequence ended, or being set up), 2 the next
 * note is due, 4 the speaker to be turned on, 8 noise, 10h the note holds until its pattern
 * ends it.
 */
#ifndef EP_SOUND_H
#define EP_SOUND_H

#include "ep_game.h"

/* 4c98 */
void ep_sound(ep_game *g, uint8_t id);

/* 4deb, 4df5, 4dff: a sound the laser's must not cut short (ds:4fe0) */
void ep_sound_marked(ep_game *g, uint8_t id);

/* 4dc9: the laser (fired & 3), not over a marked sound still playing on the speaker */
void ep_sound_laser(ep_game *g, uint8_t fired);

/* 4da4: under fire (not over another sound on the speaker, ds:45eb < 4f4b) */
void ep_sound_under_fire(ep_game *g);

/* 4e1a: the ground below (EP_EV_SURFACE_SOUND, size): sequence 9 at a pitch on the speaker,
 * else one of four sounds by size */
void ep_surface_sound(ep_game *g, uint16_t size);

/* 4e5a: the launch (no sound on the speaker); the original then waits until it has played:
 * EP_EV_WAIT ticks */
void ep_launch_sound(ep_game *g);

/* 4d21, 4d55: the title music on, off (EP_EV_MUSIC 2, 1); 4d6c: sound turned off (1) or on (0)
 * from the options (EP_EV_MUSIC that) */
void ep_music_start(ep_game *g);
void ep_music_stop(ep_game *g);
void ep_music_switch(ep_game *g, uint8_t off);

/* 4d8e: the title music again once it has ended (the speaker) */
void ep_music_again(ep_game *g);

/* 0215: the keyboard interrupt, for one byte from port 60h (a PC set-1 scancode, 80h set
 * when released, E0h prefixes): the key table (ds:020d: 0 down, 80h up) and, for a press,
 * its code (ds:0cad) as the last key (ds:0d2f) */
void ep_key_event(ep_game *g, uint8_t scancode);

/* 4a50: a timer tick (1193182 / 5555h Hz): the clock (not while paused), the countdown at
 * ds:45e4, the speaker's sequencer */
void ep_timer_tick(ep_game *g);

#endif
