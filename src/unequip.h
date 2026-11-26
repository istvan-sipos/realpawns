#ifndef REALPAWNS_UNEQUIP_H
#define REALPAWNS_UNEQUIP_H

/* unequip: keep items removed from support pawns (from pawnitems 1.0.0).
 *
 * Patches two branches in uGUIEquipment (the party equipment pause menu)
 * that only run for party slots 2/3 (support pawns):
 *
 *   send_away  FUN_00647980 state 2 (menu close): skip the loop that deletes
 *              every item in the session's "removed" list from the pawn's
 *              bag and logs it as returned to the pawn's owner.
 *   prompt     FUN_00646e10 state 0 (equip/unequip action): skip the
 *              "Removing this equipment will send it away" confirmation,
 *              so the action goes the same way it does for the main pawn.
 *
 * Addresses are for the GOG build.  Each site is checked byte for byte
 * before it is patched, and nothing is patched if a check fails. */

#include "patch.h"

struct unequip_opts {
    int keep_items;    /* install the send_away patch */
    int skip_prompt;   /* install the prompt patch    */
};

/* Returns 1 if every requested patch was installed, 0 otherwise. */
int unequip_install(const struct unequip_opts *opts);

#endif
