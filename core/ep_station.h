/* Elite Plus at the station (and the screens brought up in flight), reconstructed from
 * ELITE.EXE.
 *
 * A screen is drawn once (text and sprite primitives), then idles: the function-key bar and
 * the commands run until one switches screens. The original waits for keys inside some
 * screens (a promotion, the Tribble offer, mission briefings); here those are resumable:
 * the entry returns EP_WAIT_* and ep_station_key carries on with the key pressed.
 */
#ifndef EP_STATION_H
#define EP_STATION_H

#include "ep_game.h"

enum {
    EP_WAIT_NONE = 0, /* the screen is up, idling */
    EP_WAIT_KEY,      /* any key goes on */
    EP_WAIT_YN,       /* Y/y or N/n (other keys are ignored) */
    EP_WAIT_LIST      /* a list: arrows move, Enter (0dh) picks */
};

/* 8bea up to its idle loop (8dac): on arrival the promotion, the Tribble offer and the
 * mission briefings, each maybe waiting for a key; then the commander's status */
int ep_status_screen(ep_game *g);

/* a key for the screen that waits (EP_WAIT_*); returns what it waits for next */
int ep_station_key(ep_game *g, uint8_t key);

/* 8c2d..8da4: the commander's status (current and target systems, fuel, cash, condition,
 * legal status, rating, Tribbles or mission cargo, the ship and its equipment) */
void ep_status_view(ep_game *g);

/* 9d06: the ship and its equipment as sprites */
void ep_status_picture(ep_game *g);

/* 8dbc: the rating's text for a kill count */
uint16_t ep_rating_text(const ep_game *g, uint16_t kills);

/* the list on screen (0bea): colours (text low byte, back high), rows (bit 7: centred),
 * the cursor, the texts at ds:items, position and width; all rows drawn */
void ep_list_open(ep_game *g, uint16_t colours, uint16_t selected, uint8_t rows, uint8_t cursor,
                  uint16_t items, int16_t x, int16_t y, int16_t w);

/* 0c24: mode 1 draws every row, 2 the cursor's; then the arrow keys (48h up, 50h down)
 * the commands saw move the cursor; returns the key unless it moved the cursor (ffh) */
uint8_t ep_list_poll(ep_game *g, int mode);

/* 97d8: each commodity's buying and selling price here (ds:8d0a) */
void ep_market_prices(ep_game *g);

/* 8ea2: the market's rows (prices, quantities on offer, what is in the hold); the first
 * time after arriving the quantities are drawn */
void ep_market_rows(ep_game *g);

/* 9048: MARKET PRICES; at the station a list to buy from and sell to */
void ep_market_screen(ep_game *g);

/* 96de, 9781: buy or sell one of the commodity under the cursor */
void ep_market_buy(ep_game *g);
void ep_market_sell(ep_game *g);

/* 9161: the equipment rows (price, and what the station pays back for what is fitted) */
void ep_equipment_rows(ep_game *g);

/* 924a: EQUIP SHIP */
void ep_equipment_screen(ep_game *g);

/* 932f, 9563: buy or sell the item under the cursor; a laser with several mounts to choose
 * asks for one (EP_WAIT_LIST: the arrows and Enter, through ep_station_key) */
int ep_equipment_buy(ep_game *g);
int ep_equipment_sell(ep_game *g);

/* where the screens idle */
enum { EP_IDLE_NONE = 0, EP_IDLE_STATUS, EP_IDLE_PLAIN, EP_IDLE_MARKET, EP_IDLE_EQUIP };

/* one pass of the current screen's idle loop: the bar, the commands, the screen's own
 * work; returns the commands' EP_CMD_* (a new screen to show: EP_CMD_SCREEN) */
int ep_station_idle(ep_game *g);

/* 4a50: one timer tick: the clock, and a note's time up */
void ep_timer_tick(ep_game *g);

#endif
