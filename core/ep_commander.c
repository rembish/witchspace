/* Elite Plus commander block (see ep_commander.h). */
#include "ep_commander.h"

#include <string.h>

void ep_commander_default(ep_commander *c) { memcpy(c->b, ep_commander0, sizeof c->b); }

uint16_t ep_commander_checksum(const ep_commander *c)
{
    uint16_t ax = 0x454c;
    for (int i = 0; i < EP_CMDR_CHECKSUM; i++) {
        unsigned lo = (ax & 0xff) + c->b[i];
        ax = (uint16_t)((ax & 0xff00) + (lo >> 8 << 8) + (lo & 0xff));
        ax = (uint16_t)(ax << 1 | ax >> 15);
    }
    return ax;
}

void ep_commander_seal(ep_commander *c)
{
    uint16_t v = ep_commander_checksum(c);
    c->b[EP_CMDR_CHECKSUM] = (uint8_t)v;
    c->b[EP_CMDR_CHECKSUM + 1] = (uint8_t)(v >> 8);
}

int ep_commander_valid(const ep_commander *c)
{
    uint16_t stored = (uint16_t)(c->b[EP_CMDR_CHECKSUM] | c->b[EP_CMDR_CHECKSUM + 1] << 8);
    return stored == ep_commander_checksum(c);
}

uint32_t ep_commander_cash(const ep_commander *c)
{
    const uint8_t *p = c->b + EP_CMDR_CASH;
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

void ep_commander_set_cash(ep_commander *c, uint32_t tenths)
{
    for (int k = 0; k < 4; k++) c->b[EP_CMDR_CASH + k] = (uint8_t)(tenths >> (8 * k));
}
