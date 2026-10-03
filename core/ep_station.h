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
    EP_WAIT_LIST,     /* a list: arrows move, Enter (0dh) picks */
    EP_WAIT_TIME,     /* a key, or until a time (ffh: no key, the clock looked at) */
    EP_WAIT_TEXT      /* a text: '-', digits, capitals; backspace, Enter, Esc (ffh: no key, the
                         cursor blinks with the clock) */
};

/* 8bea up to its idle loop (8dac): on arrival the promotion, the Tribble offer and the
 * mission briefings, each maybe waiting for a key; then the commander's status */
int ep_status_screen(ep_game *g);

/* station_step while the title comes up (ep_title_key) */
#define EP_STEP_TITLE 0xf0

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

/* 5ac0: the charts (F4): short-range first, the galactic chart when pressed again; not in
 * witchspace */
int ep_chart_screen(ep_game *g);

/* 608f: the system nearest the cursor, its name and distance under the chart */
void ep_chart_find(ep_game *g);

/* 5d9f: the cursor back to the present system */
void ep_chart_home(ep_game *g);

/* 8880: DATA ON the selected system: distance, economy, government, tech level, population,
 * species, productivity, radius, the description and a picture */
void ep_data_screen(ep_game *g);

/* 6189: FIND: Which System ? (a name typed, the cursor onto it) */
int ep_chart_find_name(ep_game *g);

/* 0b20: a box over the screen with a title; 0b0a: the screen put back */
void ep_box_open(ep_game *g, uint16_t title);
void ep_box_close(ep_game *g);

/* the abandon (0a92) and exit (0ad5) questions; Y sets f.leave */
enum { EP_ASK_ABANDON = 1, EP_ASK_EXIT };
int ep_station_ask(ep_game *g, uint16_t title, int what);

/* 71c1: a new game from the saved commander (Jameson, or the one loaded), the flight and
 * the missions reset; the Tribble offer's price from the time of day (int 21h 2ch) */
void ep_new_game(ep_game *g, uint8_t hour, uint8_t minute, uint8_t second, uint8_t hundredths);

/* a004..a024: the game starts (after the title): the music stops, a new game, the status
 * screen at the station (EP_WAIT_* as ep_status_screen) */
int ep_start_game(ep_game *g, uint8_t hour, uint8_t minute, uint8_t second, uint8_t hundredths);

/* 07aa: SAVE COMMANDER: the name typed, the file written (asking before overwriting) */
int ep_save_screen(ep_game *g);

/* 08ab: LOAD COMMANDER: a list of the files; a good one takes the player to the station
 * (f.leave 3), a bad one to the title (f.leave 1) */
int ep_load_screen(ep_game *g);

/* where the screens idle */
enum {
    EP_IDLE_NONE = 0,
    EP_IDLE_STATUS,
    EP_IDLE_PLAIN,
    EP_IDLE_MARKET,
    EP_IDLE_EQUIP,
    EP_IDLE_LOCAL,
    EP_IDLE_GALAXY
};

/* one pass of the current screen's idle loop: the bar, the commands, the screen's own
 * work; returns the commands' EP_CMD_* (a new screen to show: EP_CMD_SCREEN) */
int ep_station_idle(ep_game *g);

/* the pass the pause menu interrupted, from where it stopped (EP_CMD_*) */
int ep_station_resume(ep_game *g);

/* 4a50: one timer tick: the clock, and a note's time up */
void ep_timer_tick(ep_game *g);

#endif
