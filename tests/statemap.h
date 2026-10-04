/* Test-only bridge between the core's state and the original's data segment (ds:0000..ffff):
 * load a snapshot into ep_game, store ep_game back over it, and mark which bytes the core
 * models. */
#ifndef STATEMAP_H
#define STATEMAP_H

#include "ep_game.h"

#define DS_SIZE 0x10000

void state_load(ep_game *g, const uint8_t ds[DS_SIZE]);  /* ep_ds_load */
void state_store(const ep_game *g, uint8_t ds[DS_SIZE]); /* ep_ds_store */
void state_mask(uint8_t mask[DS_SIZE]);                  /* ep_ds_mask */

#endif
