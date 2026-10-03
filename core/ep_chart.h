/* Elite Plus galaxy charts: picking a system with the cursor, distances, reconstructed from
 * ELITE.EXE. The chart cursor and centre live in the commander block (ds:8316..831e). */
#ifndef EP_CHART_H
#define EP_CHART_H

#include "ep_game.h"

/* 5e4d: the cursor in galaxy coordinates (x low byte, y high byte; the zoomed chart is 7/2
 * times larger around the centre) */
uint16_t ep_cursor_position(const ep_game *g);

/* 5fe1: the system nearest to the cursor (on the zoomed chart: within its window) becomes
 * the selected index, g->seed is set to it and the cursor snaps to it (5e95) */
void ep_find_nearest(ep_game *g);

/* 6047: distance from the chart centre to g->seed's system, in tenths of a light year (x4
 * of the integer root), into the selected record */
void ep_system_distance(ep_game *g);

/* 5ee8: find the nearest system, its distance (also as text, ds:5562), and its data and
 * name into the selected record (ds:8338); g->seed ends four twists past the system */
void ep_select_system(ep_game *g);

#endif
