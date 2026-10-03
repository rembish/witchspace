/* Elite Plus flight frame, reconstructed from ELITE.EXE (see ep_frame.h). */
#include "ep_frame.h"

#include "ep_combat.h"
#include "ep_dust.h"
#include "ep_flight.h"
#include "ep_ships.h"
#include "ep_world.h"

void ep_frame_before_ai(ep_game *g)
{
    if (++g->f.flash == 6) g->f.flash = 0; /* 3921 */
    ep_missile_lock(g);
    ep_dashboard_tick(g);
    ep_dust_frame(g);
    int drawn[EP_OBJECTS];
    ep_world_update(g, drawn);
    ep_enemy_fire(g);
    ep_laser_hits(g);
    ep_fuel_leak(g);
    ep_message_tick(g);
    if (ep_view_laser(g) >= 0) g->f.reg_dl = 0x10; /* 4f34: the crosshair sprite (3411) */
}

void ep_frame_from_ai(ep_game *g)
{
    ep_ai_frame(g);
    ep_controls(g);
    ep_collisions(g);
    ep_tribbles_tick(g);
}

void ep_flight_frame(ep_game *g)
{
    ep_frame_before_ai(g);
    ep_frame_from_ai(g);
}
