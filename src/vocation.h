#ifndef REALPAWNS_VOCATION_H
#define REALPAWNS_VOCATION_H

/* vocation: support pawns change vocation at the game's vocation screen.
 * When the screen opens, the first present support pawn (slot 2, then 3)
 * carrying the marker item takes the main pawn's row. */

extern int rp_vc_marker;    /* item id, default 1529 (Neutralizing Elixir) */
extern int rp_vc_consume;   /* remove one marker per vocation change       */

/* Returns 1 when every patch is in place. */
int vocation_install(void);

#endif
