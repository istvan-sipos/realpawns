#ifndef REALPAWNS_RESTRICT_H
#define REALPAWNS_RESTRICT_H

/* restrict: let support pawns take any gear (from pawnitems 1.0.0).
 *
 * The game refuses support pawns (party slots 2/3) items whose type
 * carries a "restricted" flag (item def +0x58, bit 19), and upgraded items
 * (item flags & 0x600, probably 3 stars and dragonforged).  Two checks:
 *
 *   equip  FUN_00645410: the enable check for the [Equip] button in the
 *          equipment menu.  Its final block returns 0 for a restricted
 *          item on a support pawn.  We jump past that block to `return 1`.
 *   give   FUN_00460ea0(item, from, to): how many of `item` can be given
 *          to party member `to` (0 = not allowed).  Used by [Give] and the
 *          item menus.  Restricted items count only for the Arisen or the
 *          main pawn; the patch makes every recipient count.
 *          FUN_0071f580 is the storage menu's own copy of that rule (how
 *          many of a stored item a party member can withdraw; 0 greys the
 *          name out).  Both are patched together under `give`.
 *
 * Addresses are for the GOG build, signature-checked before patching. */

#include "patch.h"

struct restrict_opts {
    int equip;   /* install the equip patch */
    int give;    /* install the give and withdraw patches */
};

/* Returns 1 if every requested patch was installed, 0 otherwise. */
int restrict_install(const struct restrict_opts *opts);

#endif
