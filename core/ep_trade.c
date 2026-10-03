/* Elite Plus trading at a station, reconstructed from ELITE.EXE (see ep_trade.h). */
#include "ep_trade.h"

#include "ep_combat.h"
#include "ep_market.h"
#include "ep_tables.h"

static uint8_t *cargo(ep_game *g, int row)
{
    return g->cmdr.b + EP_CMDR_CARGO + 2 * row; /* [0] held, [1] on offer */
}

static uint8_t current(const ep_game *g, int field) { return g->cmdr.b[EP_CMDR_CURRENT + field]; }

uint16_t ep_goods_buy_price(const ep_game *g, int row)
{
    return ep_goods_price(row, current(g, EP_SYSREC_GOVERNMENT), current(g, EP_SYSREC_ECONOMY),
                          current(g, EP_SYSREC_TECH));
}

/* 8e23: pay if the cash (32 bits) covers it */
int ep_pay(ep_game *g, uint32_t amount)
{
    uint32_t cash = ep_commander_cash(&g->cmdr);
    if (cash < amount) return 0;
    ep_commander_set_cash(&g->cmdr, cash - amount);
    ep_cash_text(&g->cmdr);
    return 1;
}

static void earn(ep_game *g, uint32_t amount)
{
    ep_commander_set_cash(&g->cmdr, ep_commander_cash(&g->cmdr) + amount);
    ep_cash_text(&g->cmdr);
}

uint16_t ep_trade_buy(ep_game *g, int row)
{
    if (row < 0 || row >= EP_GOODS) return EP_TRADE_NOTHING;
    uint8_t *c = cargo(g, row);
    if (!c[1]) return EP_TRADE_NOTHING;
    uint8_t space = g->cmdr.b[EP_CMDR_EQUIPMENT + 1] == 1 ? 0x23 : 0x14; /* cargo bay ext. */
    if (row >= 13) {
        if (c[1] >= 0xfa) return 0xad3e; /* (the original tests what is on offer) */
    } else if (space <= g->cmdr.b[EP_CMDR_CARGO_USED]) {
        return 0xad2e; /* CARGO BAY FULL */
    }
    if (!ep_pay(g, ep_goods_buy_price(g, row))) return 0xad50; /* not enough cash */
    c[0]++;
    c[1]--;
    if (row < 13) g->cmdr.b[EP_CMDR_CARGO_USED]++;
    return EP_TRADE_OK;
}

uint16_t ep_trade_sell(ep_game *g, int row)
{
    if (row < 0 || row >= EP_GOODS) return EP_TRADE_NOTHING;
    uint8_t *c = cargo(g, row);
    if (!c[0]) return EP_TRADE_NOTHING;
    earn(g, ep_sell_price(ep_goods_buy_price(g, row)));
    c[0]--;
    if (++c[1] == 0) c[1]--;
    if (row < 13) g->cmdr.b[EP_CMDR_CARGO_USED]--;
    /* 98d4: illegal goods raise the legal status */
    uint8_t l = (uint8_t)(ep_goods_tech_adj[row][2] + g->cmdr.b[EP_CMDR_LEGAL]);
    if (l) g->cmdr.b[EP_CMDR_LEGAL] = l;
    return EP_TRADE_OK;
}

static uint16_t equipment_price(const ep_game *g, int row)
{
    uint16_t price[EP_EQUIPMENT], sell[EP_EQUIPMENT];
    ep_equipment_prices(current(g, EP_SYSREC_GOVERNMENT), current(g, EP_SYSREC_ECONOMY),
                        current(g, EP_SYSREC_TECH), g->cmdr.b + EP_CMDR_FUEL, price, sell);
    return price[row];
}

/* 8df7: laser rows: pulse 4, beam 5, mining 12, military 13 -> laser type 0..3, else -1 */
static int laser_type(int row) { return row == 4 ? 0 : row == 5 ? 1 : row == 12 ? 2 : row == 13 ? 3 : -1; }

uint8_t ep_free_mounts(const ep_game *g) { return (uint8_t)(~g->cmdr.b[EP_CMDR_LASERS] & 0x0f); }

void ep_fit_laser(ep_game *g, int type, int choice)
{
    /* 9524: the choice-th free mount (0 = the first) gets the laser */
    uint8_t mounts = g->cmdr.b[EP_CMDR_LASERS];
    for (int m = 0; m < 8; m++) {
        if (mounts >> m & 1) continue;
        if (choice-- > 0) continue;
        g->cmdr.b[EP_CMDR_LASERS] = (uint8_t)(mounts | 1u << m);
        uint8_t t = g->cmdr.b[EP_CMDR_LASER_TYPES];
        t = (uint8_t)((t & ~(3u << (2 * m))) | (unsigned)type << (2 * m));
        g->cmdr.b[EP_CMDR_LASER_TYPES] = t;
        return;
    }
}

uint16_t ep_equip_buy(ep_game *g, int row)
{
    uint8_t *fuel = g->cmdr.b + EP_CMDR_FUEL;
    if (row < 0 || row >= EP_EQUIPMENT) return EP_TRADE_NOTHING;
    if (row == 0) { /* fuel: a full tank, or what the cash buys */
        if (g->f.mission == 1) return 0x8dad;
        if (*fuel >= 0xfb) return 0xadaa;
        uint16_t per = equipment_price(g, 0);
        uint32_t p = (uint32_t)(uint16_t)((0xff - *fuel) * 7) * per;
        uint16_t cost = (uint16_t)(p >> 8);
        if (ep_pay(g, cost)) {
            *fuel = 0xff;
            return 0xadc3;
        }
        uint16_t lo = (uint16_t)ep_commander_cash(&g->cmdr);
        if (!lo) return 0xad50;
        uint32_t q = ((uint32_t)lo << 8) / per; /* 937a: the cash's low word buys fuel */
        uint8_t add = (uint8_t)((q & 0xffff) / 7);
        *fuel = (uint8_t)(*fuel + add);
        ep_pay(g, lo);
        return EP_TRADE_OK;
    }
    int lt = laser_type(row);
    if (fuel[row]) { /* 93a9: already have one */
        if (row == 1) {
            if (fuel[1] == 4) return 0xad64; /* four missiles at most */
        } else if (lt < 0) {
            return 0x8d5a; /* fitted already */
        }
    }
    if (lt >= 0) {
        if (g->cmdr.b[EP_CMDR_LASERS] == 0x0f) return 0x92e6; /* every mount has a laser */
        if (row == 12 && g->cmdr.b[EP_CMDR_EQUIPMENT + 5] != 1) return 0x8d7a;
    }
    if (!ep_pay(g, equipment_price(g, row))) return 0xad50;
    fuel[row]++;
    if (lt < 0) return EP_TRADE_OK;
    uint8_t free = ep_free_mounts(g);
    if ((free & (free - 1)) == 0) { /* 948d: one free mount: no question */
        ep_fit_laser(g, lt, 0);
        return EP_TRADE_OK;
    }
    return EP_TRADE_CHOOSE_MOUNT;
}
