/* Elite Plus trading at a station, reconstructed from ELITE.EXE: buying and selling goods
 * (96de, 9781), buying equipment (932f) and fitting lasers (9492, 9524). The screens are the
 * frontend's; these are the actions behind them. */
#ifndef EP_TRADE_H
#define EP_TRADE_H

#include "ep_game.h"

/* Results: done, nothing to do, or ask which free mount gets the laser; any other value is
 * the message the original shows (the data address of its text). */
enum { EP_TRADE_OK = 0, EP_TRADE_NOTHING = 1, EP_TRADE_CHOOSE_MOUNT = 2 };

/* Buying price of a commodity here (the docked system: ds:831f) */
uint16_t ep_goods_buy_price(const ep_game *g, int row);

/* One unit of a commodity */
/* 8e23: pay if the cash covers it (and redo the cash text); 0 if not */
int ep_pay(ep_game *g, uint32_t amount);

uint16_t ep_trade_buy(ep_game *g, int row);
uint16_t ep_trade_sell(ep_game *g, int row);

/* Equipment row (0 fuel .. 13 military laser, see ep_equipment) */
uint16_t ep_equip_buy(ep_game *g, int row);

/* Laser mounts without a laser, a bit each (front, rear, left, right) */
uint8_t ep_free_mounts(const ep_game *g);

/* Fit a laser of type 0..3 to the choice-th free mount (after EP_TRADE_CHOOSE_MOUNT) */
void ep_fit_laser(ep_game *g, int type, int choice);

#endif
