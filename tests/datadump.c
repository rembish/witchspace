/* Every table read from the original, as text, for re/tools/gen_tables.py --check. */
#include "ep_tables.h"

#include <stdio.h>
#include <string.h>

static void bytes(const char *name, const uint8_t *b, size_t n)
{
    printf("%s:", name);
    for (size_t k = 0; k < n; k++) printf(" %02x", b[k]);
    printf("\n");
}

static void words(const char *name, const uint16_t *w, size_t n)
{
    printf("%s:", name);
    for (size_t k = 0; k < n; k++) printf(" %04x", w[k]);
    printf("\n");
}

static void str(const char *name, const char *s) { bytes(name, (const uint8_t *)s, strlen(s)); }

int main(void)
{
    words("galaxy_seeds", &ep_galaxy_seeds[0][0], 27);
    str("digrams", ep_digrams);
    str("desc_template", ep_desc_template);
    str("desc_ian", ep_desc_ian);
    for (int t = 0; t < EP_DESC_TOKENS; t++)
        for (int k = 0; k < 5; k++) str("desc_token", ep_desc_tokens[t][k]);
    for (int k = 0; k < EP_GOODS; k++) {
        str("goods_name", ep_goods_names[k]);
        words("goods_eco", ep_goods_eco_factor[k], 8);
        words("goods_gov", ep_goods_gov_factor[k], 8);
        bytes("goods_tech", (const uint8_t *)ep_goods_tech_adj[k], 3);
    }
    words("goods_base", ep_goods_base_price, EP_GOODS);
    words("market_rng0", ep_market_rng0, 3);
    for (int k = 0; k < EP_EQUIPMENT; k++) {
        const ep_equipment_record *e = &ep_equipment[k];
        printf("equipment: %d %d %d %d ", e->min_tech, e->gov_factor, e->eco_factor, e->base_price);
        str("name", e->name);
    }
    words("model_offset", ep_model_offset, 32);
    bytes("models", ep_models, sizeof ep_models);
    words("sin1024", (const uint16_t *)ep_sin1024, 1024);
    words("sin2048", (const uint16_t *)ep_sin2048, 2048);
    bytes("title_ships", ep_title_ships, EP_TITLE_SHIPS);
    words("title_min_dist", ep_title_min_dist, 32);
    for (int k = 0; k < 30; k++) str("ship_name", ep_ship_names[k]);
    bytes("ring_start", ep_ring_start, 30);
    bytes("glyph_width", ep_glyph_width, 0x5b);
    bytes("sprite_width", ep_sprite_width, EP_SPRITES);
    bytes("ds_initial", ep_ds_initial, EP_DS_INITIAL);
    bytes("drv_initial", ep_drv_initial, EP_DRV_SIZE);
    printf("ds_static:");
    for (unsigned a = 0; a < 0x10000; a++) printf(" %02x", ep_ds_static((uint16_t)a));
    printf("\n");
    bytes("key_rows", &ep_key_rows[0][0], 72);
    bytes("icon_sprite", ep_icon_sprite, 37);
    bytes("bar_colour", ep_bar_colour, 3);
    bytes("ship_text", ep_ship_text, sizeof ep_ship_text);
    bytes("mcga_colour", ep_mcga_colour, 256);
    bytes("dac", ep_dac, 768);
    words("tan256", ep_tan256, 256);
    words("hit_size", ep_hit_size, 32);
    words("crash_radius", ep_crash_radius, 32);
    bytes("spawn", &ep_spawn[0][0], sizeof ep_spawn);
    bytes("spawn_limit", &ep_spawn_limit[0][0], sizeof ep_spawn_limit);
    words("spawn_chance", &ep_spawn_chance[0][0], 32);
    bytes("commander0", ep_commander0, EP_COMMANDER_SIZE);
    return 0;
}
