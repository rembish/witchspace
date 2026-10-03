/* Elite Plus commodity market, reconstructed from ELITE.EXE (see ep_market.h). */
#include "ep_market.h"

#include <string.h>

/* `mul` then keep the middle word of the product, as the original does with al=ah, ah=dl */
static uint16_t fmul(uint16_t a, uint16_t b) { return (uint16_t)(((uint32_t)a * b) >> 8); }

uint16_t ep_goods_price(int k, uint8_t government, uint8_t economy, uint8_t tech)
{
    int8_t t = (int8_t)(tech < 10 ? tech : 9);
    uint16_t v = 0x100;
    v = fmul(v, ep_goods_eco_factor[k][economy & 7]);
    v = fmul(v, ep_goods_gov_factor[k][government & 7]);
    v = fmul(v, 0x100);
    v = fmul(v, ep_goods_base_price[k]);
    uint16_t adj = (uint16_t)(0x100 + ep_goods_tech_adj[k][0] + ep_goods_tech_adj[k][1] * t);
    return fmul(v, adj);
}

uint16_t ep_sell_price(uint16_t price)
{
    if (!price) return 0;
    uint16_t cut = (uint16_t)(price >> 5);
    while (cut >= 100) cut >>= 1;
    return (uint16_t)(price - (cut + 1));
}

ep_market_rng ep_market_rng_init(void)
{
    ep_market_rng r;
    memcpy(r.w, ep_market_rng0, sizeof r.w);
    return r;
}

/* 9880: the galaxy twist step; returns w0 + w1 from before the step */
static uint16_t market_random(ep_market_rng *r)
{
    uint16_t ret = (uint16_t)(r->w[0] + r->w[1]);
    r->w[0] = r->w[1];
    r->w[1] = r->w[2];
    r->w[2] = (uint16_t)(r->w[2] + ret);
    return ret;
}

uint8_t ep_goods_quantity(ep_market_rng *r)
{
    uint16_t v = market_random(r);
    uint8_t lo = (uint8_t)(v & 0x1f), hi = (uint8_t)((v >> 8) & 3);
    if (lo < 7) return 0;
    return (uint8_t)((lo - 7) ^ hi);
}
