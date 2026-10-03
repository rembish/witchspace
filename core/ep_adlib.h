/* Elite Plus on an AdLib card, reconstructed from ELITE.EXE's music driver (segment 2270).
 *
 * The title music is ADBLUE.MID, a standard MIDI file the driver plays itself: a timer
 * interrupt (0df8) at the song's rate steps through its events and turns them into writes to
 * the OPL2 chip (voices allocated per channel, instruments from the driver's tables). The
 * interrupt also keeps the game's own clock going (4a50 every 5555h of its divisor).
 *
 * The core keeps the original's machine as far as the music needs it: which timer interrupt
 * is installed and the timer's divisor. The frontend calls ep_pit_tick at 1193182 / divisor
 * Hz; the writes to the chip collect in g->opl[] for it to play.
 */
#ifndef EP_ADLIB_H
#define EP_ADLIB_H

#include "ep_game.h"

/* 003b: the driver's start-up: the song read (through g->io) */
void ep_adlib_init(ep_game *g);

/* 0000: the title music on (the song, its instruments, its timer) */
void ep_adlib_music(ep_game *g);

/* 0045: the music off (7dc: every voice off, the chip reset) */
void ep_adlib_stop(ep_game *g);

/* 1819: the game's own timer interrupt back (5555h) */
void ep_adlib_game_timer(ep_game *g);

/* 0df8: one tick of the music's timer interrupt */
void ep_adlib_tick(ep_game *g);

/* one tick of the timer: the interrupt installed (the game's, or the music's) */
void ep_pit_tick(ep_game *g);

#endif
