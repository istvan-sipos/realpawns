#ifndef REALPAWNS_DISCIPLINE_H
#define REALPAWNS_DISCIPLINE_H

/* Let support pawns (party slots 2/3) earn Discipline and vocation rank
 * from kills.  Clones (rift.c) also gain XP like the main pawn and level
 * up, unless rp_level_up is 0; other support pawns never do.  Returns 1
 * if all patches installed. */
int discipline_install(void);

extern int rp_level_up;   /* ini [features] pawn_progression, default 1 */

#endif
