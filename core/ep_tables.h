/* The original's tables, read from your copy of ELITE.EXE (and ELITE.GRF) by ep_data.c:
 * nothing of the game's data is in this program. Load them with ep_data_load before
 * anything else (and ep_data_grf for the pictures' widths). */
#ifndef EP_TABLES_H
#define EP_TABLES_H

#include <stddef.h>
#include <stdint.h>

/* ELITE.EXE (EXEPACK-compressed, as released, or unpacked): 0 when loaded; otherwise one of */
enum {
    EP_DATA_OK = 0,
    EP_DATA_NOT_EXE,     /* not a DOS executable, or cut short */
    EP_DATA_BAD_PACKING, /* EXEPACK data that does not unpack */
    EP_DATA_VERSION      /* another program, or another version of Elite Plus */
};
int ep_data_load(const uint8_t *exe, size_t len);
const char *ep_data_error(int code);

/* ELITE.GRF: the MCGA pictures' widths (what the blit leaves in DL); 0 when read */
int ep_data_grf(const uint8_t *grf, size_t len);

/* ds:5509: seed words of the galaxies (galaxy 8 is reached only by a lucky galactic jump) */
extern uint16_t ep_galaxy_seeds[9][3];
/* ds:5585: name digrams; index 0 is two spaces (no letters) */
extern char ep_digrams[65];

#define EP_DESC_TOKENS 39

extern const char *ep_desc_template;                  /* ds:5a35: system description template */
extern const char *ep_desc_ian;                       /* ds:63ff: appended by text code 2 */
extern const char *ep_desc_tokens[EP_DESC_TOKENS][5]; /* ds:5b4a: tokens 80h.., five each */

#define EP_GOODS 17

extern const char *ep_goods_names[EP_GOODS];      /* ds:8f2d: padded to 13, then the unit */
extern uint16_t ep_goods_eco_factor[EP_GOODS][8]; /* ds:905f: price factor by economy (8.8) */
extern uint16_t ep_goods_gov_factor[EP_GOODS][8]; /* ds:916f: by government (8.8) */
extern uint16_t ep_goods_base_price[EP_GOODS];    /* ds:927f: tenths of a credit */
extern int8_t ep_goods_tech_adj[EP_GOODS][3];     /* ds:92a1: factor 256 + a + b * tech; c: illegal */
extern uint16_t ep_market_rng0[3];                /* ds:92e0: the market generator at start-up */

#define EP_EQUIPMENT 14

typedef struct {
    uint8_t min_tech;
    const char *name;
    int8_t gov_factor, eco_factor;
    uint16_t base_price;
} ep_equipment_record;

extern ep_equipment_record ep_equipment[EP_EQUIPMENT]; /* ds:8bef */

/* ss:65bc: ship models by type, offsets into ep_models (ffff: none). A model: vertex count,
 * vertices (3 x i16), then face groups {01, vertex offset (x10), normal (3 x i16)} each
 * followed by primitives {00 triangle, 02 quad, 04 line: vertex offsets, colour}, ending in 03 */
#define EP_MODELS_SIZE 9644
extern uint16_t ep_model_offset[32];
extern uint8_t ep_models[EP_MODELS_SIZE];
extern int16_t ep_sin1024[1024]; /* ds:2cc0: rotation matrices, 1024 steps a turn, Q15 */
extern int16_t ep_sin2048[2048]; /* ds:6410: the player's rotation slots, 2048 a turn, Q15 */

#define EP_TITLE_SHIPS 24

extern uint8_t ep_title_ships[EP_TITLE_SHIPS]; /* ds:b263: the title's ships in turn */
extern uint16_t ep_title_min_dist[32];         /* ds:b1bc: their closest distance by type */
extern const char *ep_ship_names[30];          /* ds:b27c: ship names by type */

extern uint8_t ep_ring_start[30];    /* ds:85be: hyperspace rings at the start: delay, radius, colour */
extern uint8_t ep_glyph_width[0x5b]; /* ds:0d48: the width of each glyph 20h..7ah */
#define EP_SPRITES 139
extern uint8_t ep_sprite_width[EP_SPRITES]; /* ELITE.GRF: the low byte of each MCGA picture's width */
/* the data segment as loaded (EP_DS_INITIAL bytes of data, zeros after) */
#define EP_DS_INITIAL 0xbe34
extern uint8_t ep_ds_initial[0x10000];
/* segment 2270, the music driver, as loaded (the AdLib's effects keep their data in it) */
#define EP_DRV_SIZE 0x3010
extern uint8_t ep_drv_initial[EP_DRV_SIZE];
/* a byte of the data segment's static text regions as loaded (0 elsewhere) */
uint8_t ep_ds_static(uint16_t addr);
extern uint8_t ep_key_rows[6][12]; /* ds:031f: the function-key bar of each screen: command ids */
extern uint8_t ep_icon_sprite[37]; /* ds:0374: icon sprite of each command id */
extern uint8_t ep_bar_colour[3];   /* ds:02fb: bar colour, space view (0) or another screen */
#define EP_SHIP_TEXT_TYPES 0x40    /* the type names start here (ds:80e9) */
#define EP_SHIP_TEXT_SIZE  321
extern uint8_t ep_ship_text[EP_SHIP_TEXT_SIZE]; /* ds:80a9: role names, then type names; NUL-separated */

extern uint8_t ep_mcga_colour[256]; /* ds:1cf3: MCGA pixel value of each game colour */
extern uint8_t ep_dac[768];         /* ds:1144: VGA/MCGA DAC, 6 bits a component */

extern uint16_t ep_tan256[256];      /* ds:7410: tangents for the arctangent (6e8a), Q15 */
extern uint16_t ep_hit_size[32];     /* ds:b0e5: laser hit box by type, bytes swapped as abd1 uses it */
extern uint16_t ep_crash_radius[32]; /* ds:7614: collision radius by type */
/* ds:8739: the spawn table {type, speed, turn, bounty, missiles, canisters, debris, energy, +3f, +1c} */
extern uint8_t ep_spawn[31][10];
extern uint8_t ep_spawn_limit[8][4];   /* ds:886f: most ships of a kind by government */
extern uint16_t ep_spawn_chance[8][4]; /* ds:8899: chance (of 65536) of a new ship a frame */

#define EP_COMMANDER_SIZE 226

extern uint8_t ep_commander0[EP_COMMANDER_SIZE]; /* ds:82db: the commander at start-up */

#endif
