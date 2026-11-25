#ifndef REALPAWNS_GAME_H
#define REALPAWNS_GAME_H

#include <stdint.h>
#include "patch.h"

/* GOG DDDA.exe addresses and layouts shared by several modules.
 * See realpawns.md for where each one comes from. */

#define VA_PBASE_PTR       0x018DB49Cu  /* *VA = pBase (NULL before a save loads) */

#define PARTY_PARAM_BASE   0xA6860u     /* slot s param = pBase + base + s*stride */
#define PARTY_PARAM_STRIDE 0x1660u
#define PARAM_OFF_ALIVE    0x004u       /* u8 */
#define PARAM_OFF_LEVEL    0x700u       /* u32 */
#define PARAM_OFF_EXP      0x2C4u       /* u32, progress within the level */
#define PARAM_OFF_KEY      0x15E4u      /* u32 mRomPawnUniqueID */
#define PBASE_OFF_HIRE_SLOT 0xA7EC0u    /* FUN_0043e0c0 idx i: param = pBase + this + i*stride */

#define REC_REGION_OFF     0x16E0u      /* 120 rift records in memory */
#define REC_REGION_COUNT   120
#define REC_SIZE           0x14E8u
#define REC_OFF_LEVEL      0x05Cu
#define REC_OFF_KEY        0x1478u

static inline uint8_t *game_pbase(void)
{
#ifdef RP_TEST_NO_GAME          /* offline test harness: no game memory */
    return NULL;
#endif
    return *(uint8_t**)patch_va(VA_PBASE_PTR);
}

/* Party slot 0..3 occupied?  (0 when no save is loaded.) */
static inline int game_slot_alive(int slot)
{
#ifdef RP_TEST_NO_GAME          /* offline test harness: no game memory */
    (void)slot;
    return 1;
#endif
    uint8_t *b = game_pbase();
    if (!b) return 0;
    return b[PARTY_PARAM_BASE + (uint32_t)slot * PARTY_PARAM_STRIDE + PARAM_OFF_ALIVE] != 0;
}

#endif
