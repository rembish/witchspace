/* Elite Plus commander: the block at ds:82db that is also the save file (226 bytes, with a
 * checksum in the last word), reconstructed from ELITE.EXE.
 *
 * Kept as the original's bytes so save files stay compatible both ways; fields are reached
 * through the offsets below, named as they are identified (offsets are from ds:82db; the
 * ds address is 82db + offset).
 */
#ifndef EP_COMMANDER_H
#define EP_COMMANDER_H

#include "ep_galaxy.h"
#include "ep_tables.h"

#include <stdint.h>

enum {
    EP_CMDR_MAGIC = 0x00,        /* "ELITE Commander File" 1a */
    EP_CMDR_TITLE = 0x15,        /* ds:82f0 "COMMANDER " */
    EP_CMDR_CASH_TEXT = 0x20,    /* ds:82fb cash as shown, "100.0 Credits" */
    EP_CMDR_SEED = 0x34,         /* ds:830f galaxy seed, 3 words */
    EP_CMDR_GALAXY = 0x3a,       /* ds:8315 */
    EP_CMDR_CHART_CENTRE = 0x3b, /* ds:8316/8317 */
    EP_CMDR_CURSOR = 0x3d,       /* ds:8318/8319 */
    EP_CMDR_ZOOM = 0x43,         /* ds:831e: zoomed chart */
    EP_CMDR_CURRENT = 0x44,      /* ds:831f: current system record (25 bytes) */
    EP_CMDR_SELECTED = 0x5d,     /* ds:8338: selected system record (25 bytes) */
    EP_CMDR_FUEL = 0x7b,         /* ds:8356 */
    EP_CMDR_EQUIPMENT = 0x7c,    /* ds:8357..: counts per equipment record 1..13 */
    EP_CMDR_LASERS = 0x8a,       /* ds:8365: laser mounts fitted, a bit per view */
    EP_CMDR_LASER_TYPES = 0x8b,  /* ds:8366: two bits of laser type per view */
    EP_CMDR_CASH = 0x8c,         /* ds:8367: 32 bits, tenths of a credit */
    EP_CMDR_LEGAL = 0x90,        /* ds:836b: 0 clean, higher worse */
    EP_CMDR_KILLS = 0x91,        /* ds:836c: word */
    EP_CMDR_NAME = 0x95,         /* ds:8370 */
    EP_CMDR_CARGO = 0x9e,        /* ds:8379: 17 x (held, on offer) */
    EP_CMDR_MARKET_DRAWN = 0xc2, /* ds:839d */
    EP_CMDR_CHECKSUM = 0xe0,     /* ds:83bb */
};

/* Record layout inside EP_CMDR_CURRENT / EP_CMDR_SELECTED (copies of ds:8338..8350) */
enum {
    EP_SYSREC_NAME = 0x00,
    EP_SYSREC_INDEX = 0x0a,
    EP_SYSREC_DIST = 0x0b,
    EP_SYSREC_GOVERNMENT = 0x0d,
    EP_SYSREC_ECONOMY = 0x0e,
    EP_SYSREC_TECH = 0x0f,
};

typedef struct {
    uint8_t b[EP_COMMANDER_SIZE];
} ep_commander;

/* The commander the game starts with (the block's contents in the EXE). */
void ep_commander_default(ep_commander *c);

/* 77c5: checksum over everything before the checksum word. */
uint16_t ep_commander_checksum(const ep_commander *c);

/* Store the checksum (as saving does). */
void ep_commander_seal(ep_commander *c);

/* Loading accepts a block whose stored checksum matches (09fe). */
int ep_commander_valid(const ep_commander *c);

static inline uint8_t ep_commander_b(const ep_commander *c, int off) { return c->b[off]; }
static inline void ep_commander_set_b(ep_commander *c, int off, uint8_t v) { c->b[off] = v; }

uint32_t ep_commander_cash(const ep_commander *c);
void ep_commander_set_cash(ep_commander *c, uint32_t tenths);

#endif
