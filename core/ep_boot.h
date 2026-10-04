/* Elite Plus start-up, reconstructed from ELITE.EXE (entry 0000..00b7).
 *
 * ep_boot gives the state the title starts from (9e80), keeping the frontend's io, wait and
 * protection (set them first: with an AdLib the music is read through io): the data segment
 * as the executable loads it, the RNG seeded from the time of day (2Ch: hundredths ^ seconds
 * ^ minutes steps, 0 meaning 256), the video mode and sound device chosen, the song read
 * (31a: ADBLUE.MID, or a Roland's BLUTEST.MID), the copy protection's question picked (32b8,
 * one RNG step: a 3-byte record of ds:5070 gives page, paragraph, line, word and the hash of
 * the word), and the commander copied as the one to go back to (71b0).
 *
 * The question itself (1415) is opt-in: with g->protection off it is never asked and the
 * game runs as if it was answered right. ep_protection_ask puts it up (EP_WAIT_TEXT, through
 * ep_station_key); a wrong word sets f.protection_failed, and the title then quits (05ac).
 * This copy of the executable accepts any word (the comparison was replaced by no-ops); the
 * check here is the hash comparison those bytes stood for.
 */
#ifndef EP_BOOT_H
#define EP_BOOT_H

#include "ep_game.h"

#include <stdint.h>

/* video: ds:10bc (0 EGA, 1 VGA, 2 MCGA); sound: ds:4801 (0 Roland, 1 AdLib, 2 PC speaker); the
 * time of day seeds the RNG */
void ep_boot(ep_game *g, uint8_t video, uint8_t sound, uint8_t minute, uint8_t second, uint8_t hundredths);

/* 1415: the question (EP_WAIT_TEXT; the word through ep_station_key) */
int ep_protection_ask(ep_game *g);

/* 32b8: one RNG step, mixed down to a byte, counts records of ds:5070 (wrapping at the 0
 * after the last); the record's bits give f.prot_page .. prot_word and f.prot_hash */
void ep_protection_pick(ep_game *g);

/* the hash a typed word (NUL-terminated, capitals) is checked by: h = 2h + (c - 'A'), 9 bits */
uint16_t ep_protection_hash(const uint8_t *word);

#endif
