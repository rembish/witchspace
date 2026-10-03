/* Test bridge over the core's data-segment map (see statemap.h, core/ep_dsmap.h). */
#include "statemap.h"

#include "ep_dsmap.h"

void state_load(ep_game *g, const uint8_t ds[DS_SIZE]) { ep_ds_load(g, ds); }

void state_store(const ep_game *g, uint8_t ds[DS_SIZE]) { ep_ds_store(g, ds); }

void state_mask(uint8_t mask[DS_SIZE]) { ep_ds_mask(mask); }
