#include <windows.h>
#include <stdint.h>
#include "innate.h"
#include "game.h"
#include "patch.h"

#define VA_SKILL_TABLE  0x014D4C30u  /* 0x20 bytes per skill id               */
#define SKILL_COUNT     0x1AE        /* FUN_0077e310's bound                  */
#define SK_OFF_TYPE     0x08         /* 1 = core skill, 2 = weapon skill      */

/* FUN_00672f60's starting-skill table: 12 x {u32 weapon key, s32 skill id,
 * u8 slot}, matched against the per-vocation key lists at 0x0185678C. */
#define VA_START_TABLE  0x01563548u
#define START_ROWS      12

/* live learned bits: param + 0x72C + {0x04, 0x3C, 0x74} = grades 1..3 */
#define PARAM_OFF_LEARN 0x72C

#define MAX_IDS 48
static uint16_t g_ids[MAX_IDS];
static int      g_n;

static void add_id(int id)
{
    if (id < 0 || id >= SKILL_COUNT || g_n >= MAX_IDS) return;
    for (int i = 0; i < g_n; i++)
        if (g_ids[i] == id) return;
    g_ids[g_n++] = (uint16_t)id;
}

int innate_init(void)
{
    g_n = 0;

    const uint8_t *t = patch_va(VA_SKILL_TABLE);
    const uint8_t *s = patch_va(VA_START_TABLE);
    int n_start = 0;
    /* sanity: GOG row 0 is {1 (sword), 40 (Blink Strike), 0} */
    if (*(const uint32_t*)s == 1 && *(const int32_t*)(s + 4) == 40) {
        for (int r = 0; r < START_ROWS; r++) {
            int32_t id = *(const int32_t*)(s + r * 12 + 4);
            const uint8_t *e = t + id * 0x20;
            if (id > 0 && id < SKILL_COUNT && *(const int32_t*)(e + SK_OFF_TYPE) == 2) {
                add_id(id);
                n_start++;
            }
        }
    }

    if (n_start < 10) {
        patch_logf("innate: starting-skill table looks wrong (%d found), off",
                   n_start);
        g_n = 0;
        return 0;
    }
    patch_logf("innate: %d vocation starting skills", n_start);
    return g_n;
}

static int known(const uint32_t *a, uint32_t id)
{
    return a && (a[id >> 5] & (1u << (id & 31)));
}

int innate_grant(uint32_t *lv1, const uint32_t *lv2, const uint32_t *lv3)
{
    int added = 0;
    for (int i = 0; i < g_n; i++) {
        uint32_t id = g_ids[i];
        if (known(lv1, id) || known(lv2, id) || known(lv3, id)) continue;
        lv1[id >> 5] |= 1u << (id & 31);
        added++;
    }
    return added;
}

int innate_grant_party(void)
{
    uint8_t *b = game_pbase();
    if (!b || !g_n) return 0;
    int added = 0;
    for (int s = 2; s <= 3; s++) {
        if (!game_slot_alive(s)) continue;
        uint8_t *l = b + PARTY_PARAM_BASE + (uint32_t)s * PARTY_PARAM_STRIDE + PARAM_OFF_LEARN;
        int n = innate_grant((uint32_t*)(l + 0x04), (const uint32_t*)(l + 0x3C),
                             (const uint32_t*)(l + 0x74));
        if (n) patch_logf("innate: party slot %d: %d free skill(s) granted", s, n);
        added += n;
    }
    return added;
}
