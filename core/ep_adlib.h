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

/* 1819: the game's own timer interrupt back (5555h), the effects started again (1930) */
void ep_adlib_game_timer(ep_game *g);

/* 0df8: one tick of the music's timer interrupt */
void ep_adlib_tick(ep_game *g);

/* one tick of the timer: the interrupt installed (the game's, the music's, or the effects') */
void ep_pit_tick(ep_game *g);

/* d74 (and the effects' 2315): a write to the chip, into g->opl */
void ep_opl(ep_game *g, uint8_t reg, uint8_t val);

/* The effects in flight (1842..262e): a small interpreter of the driver's effect programs
 * (bank cs:0ea0), ten voices of the chip, its state in the driver's segment (g->adlib.fx). */

/* 1930: the effects' start: the chip's waveforms and rhythm off, every voice silent */
void ep_adfx_init(ep_game *g);

/* 17c6: the effects' timer interrupt (16c1, at 555h) in place of the game's */
void ep_adfx_install(ep_game *g);

/* 185a: effect `id` queued (16 places), started on the effects' next step */
void ep_adfx_queue(ep_game *g, uint8_t id);

/* 16c1: one tick of the effects' timer interrupt (the game's clock every 16th) */
void ep_adfx_tick(ep_game *g);

#endif
