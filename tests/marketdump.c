/* Print commodity prices for every government, economy and tech level, then a run of
 * arrival quantities from the start-up generator state, equipment prices and the selling
 * price of every possible buying price, for re/emu/markettest.py. */
#include "ep_market.h"

#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv)
{
    int arrivals = argc > 1 ? atoi(argv[1]) : 200;
    for (int gov = 0; gov < 8; gov++)
        for (int eco = 0; eco < 8; eco++)
            for (int tech = 0; tech < 256; tech++) {
                printf("%d %d %d", gov, eco, tech);
                for (int k = 0; k < EP_GOODS; k++) {
                    uint16_t p = ep_goods_price(k, (uint8_t)gov, (uint8_t)eco, (uint8_t)tech);
                    printf(" %u/%u", p, ep_sell_price(p));
                }
                printf("\n");
            }
    ep_market_rng r = ep_market_rng_init();
    for (int a = 0; a < arrivals; a++) {
        printf("q");
        for (int k = 0; k < EP_GOODS; k++) printf(" %u", ep_goods_quantity(&r));
        printf("\n");
    }
    for (int gov = 0; gov < 8; gov++)
        for (int eco = 0; eco < 8; eco++)
            for (int tech = 0; tech < 256; tech++) {
                uint8_t owned[EP_EQUIPMENT];
                uint16_t price[EP_EQUIPMENT], sell[EP_EQUIPMENT];
                for (int k = 0; k < EP_EQUIPMENT; k++) owned[k] = (gov + eco + tech + k) % 3 == 0;
                int n = ep_equipment_prices((uint8_t)gov, (uint8_t)eco, (uint8_t)tech, owned, price, sell);
                printf("e %d %d %d %d", gov, eco, tech, n);
                for (int k = 0; k < n; k++) printf(" %u/%u", price[k], sell[k]);
                printf("\n");
            }
    for (unsigned v = 0; v < 0x10000; v += 16) {
        printf("s");
        for (unsigned k = v; k < v + 16; k++) printf(" %u", ep_sell_price((uint16_t)k));
        printf("\n");
    }
    return 0;
}
