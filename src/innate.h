#ifndef REALPAWNS_INNATE_H
#define REALPAWNS_INNATE_H

#include <stdint.h>

/* innate: the skills a character gets for free: each vocation's starting
 * skills, e.g. Blink Strike (40) for swords, from the table FUN_00672f60
 * learns from on a vocation change.  (Type-1 core skills such as Dire
 * Onslaught are bought with DP and are NOT granted.)
 *
 * Rift pawns are synthesized without them, so e.g. Burst Strike can't be
 * learned (the learn list only offers upgrades of known skills).  We
 * grant all of them regardless of vocation, like a character who has
 * tried every vocation, so a later vocation change needs no extra work.
 * A skill already known at any grade is left alone. */

/* Read the starting-skill ids from the game's table.  Returns the number of skill
 * ids found (0 = tables didn't look as expected; feature off). */
int innate_init(void);

/* Set the grade-1 bit of every free skill not already known in lv1/lv2/lv3
 * (each u32[14]; lv3 may be NULL).  Returns the number of skills added. */
int innate_grant(uint32_t *lv1, const uint32_t *lv2, const uint32_t *lv3);

/* Grant to the live params of occupied party slots 2/3.  Returns the
 * number of skills added. */
int innate_grant_party(void);

#endif
