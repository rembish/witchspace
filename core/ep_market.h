/* Elite Plus commodity market, reconstructed from ELITE.EXE.
 *
 * Unlike the original Elite, prices do not fluctuate: market_prices (97d8) derives each one
 * from the docked system's economy, government and tech level by a chain of 8.8 fixed-point
 * multiplies. Only the quantities on offer are random, drawn once per arrival from a
 * generator of their own (ds:92e0) that is never reseeded.
 */
#ifndef EP_MARKET_H
#define EP_MARKET_H

#include "ep_tables.h"

#include <stdint.h>

typedef struct {
    uint16_t w[3]; /* ds:92e0, 92e2, 92e4 */
} ep_market_rng;

/* Buying price of commodity k in tenths of a credit (97d8). */
uint16_t ep_goods_price(int k, uint8_t government, uint8_t economy, uint8_t tech);

/* What the market pays for something bought at `price` (8e6b). */
uint16_t ep_sell_price(uint16_t price);

/* Generator state at start-up. */
ep_market_rng ep_market_rng_init(void);

/* Quantity of one commodity on offer, drawn on arrival (8f5a). */
uint8_t ep_goods_quantity(ep_market_rng *r);

/* Equipment offered at a station (9161): the first records whose minimum tech level is at
 * most tech + 1, up to EP_EQUIPMENT. Fills prices (tenths of a credit) and what the station
 * pays back for items with a nonzero count in owned[] (fuel is never bought back); returns
 * the number of items listed. */
int ep_equipment_prices(uint8_t government, uint8_t economy, uint8_t tech, const uint8_t owned[EP_EQUIPMENT],
                        uint16_t price[EP_EQUIPMENT], uint16_t sell[EP_EQUIPMENT]);

#endif
