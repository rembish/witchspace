/* Elite Plus trading at a station, reconstructed from ELITE.EXE: buying and selling goods
 * (96de, 9781), buying equipment (932f) and fitting lasers (9492, 9524), without the screens.
 *
 * The screens are ep_station.c's: MARKET PRICES goes through ep_trade_buy and ep_trade_sell
 * (and every payment through ep_pay), while EQUIP SHIP has its own ep_equipment_buy, which
 * asks for the mount on screen. ep_equip_buy, ep_free_mounts and ep_fit_laser are the same
 * rules with the mount passed in; nothing in the game calls them now. */
#ifndef EP_TRADE_H
#define EP_TRADE_H

#include "ep_game.h"

/* Results: done, nothing to do, or ask which free mount gets the laser; any other value is
 * the message the original shows (the data address of its text). */
enum { EP_TRADE_OK = 0, EP_TRADE_NOTHING = 1, EP_TRADE_CHOOSE_MOUNT = 2 };

/* Buying price of a commodity here (the docked system: ds:831f) */
uint16_t ep_goods_buy_price(const ep_game *g, int row);

/* 8e23: pay if the cash covers it (and redo the cash text); 0 if not */
int ep_pay(ep_game *g, uint32_t amount);

/* One unit of commodity `row` (0..16; -1, no row under the cursor, does nothing): bought
 * (96de: room in the hold, cash) or sold (9781: illegal goods raise the legal status, ds:92a1's
 * third byte). EP_TRADE_OK, EP_TRADE_NOTHING or a message's address. */
uint16_t ep_trade_buy(ep_game *g, int row);

/* Whether a commodity takes room in the hold (counted in its tonnes, ds:839c): rows 0..12 in
 * the original; with EP_FIX_HOLD Alien Items (16) too, as scooping them counts them */
int ep_goods_in_tonnes(const ep_game *g, int row);
/* EP_FIX_HOLD: the tonnes counted again from what is held (a loaded commander's, which the
 * original's bug may have left too high); not while mission 1's refugees fill the hold */
void ep_hold_recount(ep_game *g);
uint16_t ep_trade_sell(ep_game *g, int row);

/* Equipment row (0 fuel .. 13 military laser, see ep_equipment): bought if allowed and paid
 * for; a laser with more than one free mount returns EP_TRADE_CHOOSE_MOUNT */
uint16_t ep_equip_buy(ep_game *g, int row);

/* Laser mounts without a laser, a bit each (front, rear, left, right) */
uint8_t ep_free_mounts(const ep_game *g);

/* Fit a laser of type 0..3 to the choice-th free mount (after EP_TRADE_CHOOSE_MOUNT) */
void ep_fit_laser(ep_game *g, int type, int choice);

#endif
