/* Read seeds ("w0 w1 w2" in hex, one system per line) and print each system's description,
 * for re/emu/desctest.py. */
#include "ep_desc.h"

#include <stdio.h>

int main(void)
{
    unsigned a, b, c;
    while (scanf("%x %x %x", &a, &b, &c) == 3) {
        ep_seed s = { { (uint16_t)a, (uint16_t)b, (uint16_t)c } };
        char text[EP_DESC_MAX + 1];
        ep_system_description(&s, text);
        printf("%s\n", text);
    }
    return 0;
}
