/* Elite Plus game state and the original's data segment (ds:0000..ffff).
 *
 * The core keeps the original's state in ep_game; this map says where each part lived. The
 * tests use it to load a snapshot of the running original and compare; the core uses it to
 * read text the way the original did, by address: a message id is the address of its text,
 * and texts mix static strings (ep_ds_static, from the executable) with live buffers (names,
 * cash, countdown digits) that are game state.
 */
#ifndef EP_DSMAP_H
#define EP_DSMAP_H

#include "ep_game.h"

#define EP_DS_SIZE 0x10000

void ep_ds_load(ep_game *g, const uint8_t ds[EP_DS_SIZE]);
void ep_ds_store(const ep_game *g, uint8_t ds[EP_DS_SIZE]);
void ep_ds_mask(uint8_t mask[EP_DS_SIZE]);

/* the byte at ds:addr as the original would read it: game state where mapped, else the
 * executable's static data (0 outside the text regions) */
uint8_t ep_ds_byte(const ep_game *g, uint16_t addr);

/* the NUL-terminated bytes at ds:addr (codes included) into out; returns the length */
int ep_ds_string(const ep_game *g, uint16_t addr, uint8_t *out, int max);

#endif
