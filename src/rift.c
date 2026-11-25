/* rift: overlay stored progress onto offline-pawn records as the game
 * builds them, so the rift list, the pawn preview, skill filters and the
 * hired pawn all see the same data.
 *
 * Three post-call detours (GOG VAs):
 *   FUN_00792510(mgr, out; EAX=level)  search listing: synthesizes one
 *       pawn for the queried level and stamps out+0x1478 last.
 *   FUN_00792610(mgr, out, key)        key dispatcher: favorites,
 *       borrow/history lists, story pawns, pool refill from key lists.
 *   FUN_007927c0(mgr, count)           random pool fill of the in-memory
 *       record region pBase+0x16E0; we re-walk the whole region after it.
 * Each wrapper runs the original first (so the record is a valid,
 * game-built one) and then overwrites only the persisted fields. */

#include <windows.h>
#include <stdint.h>
#include <string.h>
#include "rift.h"
#include "store.h"
#include "game.h"
#include "patch.h"
#include "innate.h"

/* ---- 0x14E8 record offsets ---------------------------------------------- */
#define R_JOB          0x05Du   /* u8                         */
#define R_JOBLEVEL     0x05Eu   /* u8[10]                     */
#define R_JOBEXP       0x068u   /* u32[10]                    */
#define R_JOBNEXT      0x090u   /* u32[10]                    */
#define R_JOBPOINT     0x0B8u   /* s32                        */
#define R_WEAPONSKILL  0x0C8u   /* s16[12][6]                 */
#define R_ABILITY      0x158u   /* s8[6]                      */
#define R_SKILLLV1     0x160u   /* u32[14]                    */
#define R_SKILLLV2     0x198u   /* u32[14]                    */
#define R_ABILITYINFO  0x1D0u   /* u32[4]                     */
#define R_EQUIP        0x4E6u   /* cITEM_DATA[12], stride 32  */
#define R_EQ_STRIDE    32u
#define R_EQ_ITEMNO    2u       /* s16 */
#define R_EQ_NUM       4u       /* s16 */
#define R_EQ_FLAG      18u      /* u32 */
#define R_STUDYFLAG    0x684u   /* u32[322]                   */
#define R_ENCOUNT      0xB8Cu   /* f32[72]  mStudyData.EncountFrame */
#define R_KILLCNT      0xCACu   /* u32[72]  mStudyData.KillCnt      */
#define R_UNIQUECNT    0xDCCu   /* u8[116]  mStudyData.UniqueCnt    */
#define R_LOCALSTUDY   0xE40u   /* u32[322]                   */
#define R_INFO         0x1348u  /* {u32 type, f32 value, u32 status}[9] */
#define R_INFO_STRIDE  12u

int rp_innate_enabled = 1;

/* Innate skills for every official record, stored or not.  Done after the
 * stored overlay, so it also repairs older store files.  Quiet: a search
 * touches up to 100 records. */
static int grant_innate(uint8_t *rec)
{
    uint32_t key;
    memcpy(&key, rec + REC_OFF_KEY, 4);
    if (!rp_innate_enabled || !rp_key_supported(key)) return 0;
    return innate_grant((uint32_t*)(rec + R_SKILLLV1),
                        (const uint32_t*)(rec + R_SKILLLV2), NULL) > 0;
}

static int overlay(uint8_t *rec, const char *where)
{
    uint32_t key;
    memcpy(&key, rec + REC_OFF_KEY, 4);
    if (!rp_key_supported(key)) return 0;
    const struct rp_pawn *p = store_get(key);
    if (!p) return 0;

    uint8_t lv = rec[REC_OFF_LEVEL];
    if (lv != p->level) {
        patch_logf("%s: %08X '%s': record level %u != stored level %u, "
                   "left as is", where, key, p->name, lv, p->level);
        return 0;
    }

    rec[R_JOB] = p->job;
    memcpy(rec + R_JOBLEVEL,    p->job_level,    sizeof p->job_level);
    memcpy(rec + R_JOBEXP,      p->job_exp,      sizeof p->job_exp);
    memcpy(rec + R_JOBNEXT,     p->job_next,     sizeof p->job_next);
    memcpy(rec + R_JOBPOINT,    &p->job_point,   4);
    memcpy(rec + R_WEAPONSKILL, p->weapon_skill, sizeof p->weapon_skill);
    memcpy(rec + R_ABILITY,     p->ability,      sizeof p->ability);
    memcpy(rec + R_SKILLLV1,    p->skill_lv1,    sizeof p->skill_lv1);
    memcpy(rec + R_SKILLLV2,    p->skill_lv2,    sizeof p->skill_lv2);
    memcpy(rec + R_ABILITYINFO, p->ability_info, sizeof p->ability_info);

    /* Only the three gameplay members of each in-memory item; the rest of
     * the 32-byte struct holds a live pointer (see pawnlib's fieldmap). */
    for (int i = 0; i < RP_EQUIP; i++) {
        uint8_t *e = rec + R_EQUIP + (uint32_t)i * R_EQ_STRIDE;
        memcpy(e + R_EQ_ITEMNO, &p->equip[i].item_no, 2);
        memcpy(e + R_EQ_NUM,    &p->equip[i].num,     2);
        memcpy(e + R_EQ_FLAG,   &p->equip[i].flag,    4);
    }

    memcpy(rec + R_STUDYFLAG,  p->study_flag,       sizeof p->study_flag);
    memcpy(rec + R_ENCOUNT,    p->encount_frame,    sizeof p->encount_frame);
    memcpy(rec + R_KILLCNT,    p->kill_cnt,         sizeof p->kill_cnt);
    memcpy(rec + R_UNIQUECNT,  p->unique_cnt,       sizeof p->unique_cnt);
    memcpy(rec + R_LOCALSTUDY, p->local_study_flag, sizeof p->local_study_flag);

    if (p->has_incl)
        for (int i = 0; i < RP_INCL; i++) {
            uint8_t *e = rec + R_INFO + (uint32_t)i * R_INFO_STRIDE;
            memcpy(e,     &p->incl_type[i],   4);
            memcpy(e + 4, &p->incl_value[i],  4);
            memcpy(e + 8, &p->incl_status[i], 4);
        }

    patch_logf("%s: %08X '%s' (lv %u) restored: voc %u rank %u, DP %d",
               where, key, p->name, lv, p->job,
               p->job < RP_JOBS ? p->job_level[p->job] : 0, p->job_point);
    return 1;
}

static int apply(uint8_t *rec, const char *where)
{
    int n = overlay(rec, where);
    grant_innate(rec);
    return n;
}

int rift_overlay_pool(void)
{
    uint8_t *b = game_pbase();
    if (!b || !store_count()) return 0;
    int n = 0;
    for (int i = 0; i < REC_REGION_COUNT; i++)
        n += apply(b + REC_REGION_OFF + (uint32_t)i * REC_SIZE, "pool");
    return n;
}

/* ---- hook bodies (cdecl, called from the shims) ------------------------- */

__attribute__((used))
static void __cdecl on_search(uint8_t *out, uint32_t ret)
{
    if ((ret & 0xFF) && out) apply(out, "search");
}

__attribute__((used))
static void __cdecl on_dispatch(uint8_t *out, uint32_t key, uint32_t ret)
{
    if (!(ret & 0xFF) || !out) return;
    uint32_t stamped;
    memcpy(&stamped, out + REC_OFF_KEY, 4);
    if (stamped == key) apply(out, "dispatch");
}

__attribute__((used))
static void __cdecl on_pool(void)
{
    rift_overlay_pool();
}

/* ---- shims ---------------------------------------------------------------
 * Each re-pushes the caller's stack arguments, CALLs the trampoline (which
 * pops them again: all three are callee-cleanup), then runs the C body
 * with the original return value preserved in EAX.  EAX is also an input
 * of FUN_00792510 (level) and is passed through untouched. */

void *rp_tr_search, *rp_tr_dispatch, *rp_tr_pool;

__attribute__((naked, used))
static void search_shim(void)          /* stdcall(mgr, out), EAX = level */
{
    __asm__ (
        "pushl  8(%esp)                \n"   /* out */
        "pushl  8(%esp)                \n"   /* mgr */
        "call   *_rp_tr_search         \n"
        "pushl  %eax                   \n"   /* saved */
        "pushl  %eax                   \n"   /* arg: ret */
        "pushl  16(%esp)               \n"   /* arg: out */
        "call   _on_search             \n"
        "addl   $8, %esp               \n"
        "popl   %eax                   \n"
        "ret    $8                     \n"
    );
}

__attribute__((naked, used))
static void dispatch_shim(void)        /* stdcall(mgr, out, key) */
{
    __asm__ (
        "pushl  12(%esp)               \n"   /* key */
        "pushl  12(%esp)               \n"   /* out */
        "pushl  12(%esp)               \n"   /* mgr */
        "call   *_rp_tr_dispatch       \n"
        "pushl  %eax                   \n"   /* saved */
        "pushl  %eax                   \n"   /* arg: ret */
        "pushl  20(%esp)               \n"   /* arg: key */
        "pushl  20(%esp)               \n"   /* arg: out */
        "call   _on_dispatch           \n"
        "addl   $12, %esp              \n"
        "popl   %eax                   \n"
        "ret    $12                    \n"
    );
}

__attribute__((naked, used))
static void pool_shim(void)            /* stdcall(mgr, count) */
{
    __asm__ (
        "pushl  8(%esp)                \n"   /* count */
        "pushl  8(%esp)                \n"   /* mgr */
        "call   *_rp_tr_pool           \n"
        "pushl  %eax                   \n"
        "call   _on_pool               \n"
        "popl   %eax                   \n"
        "ret    $8                     \n"
    );
}

/* ---- install --------------------------------------------------------------- */

/* PUSH EBX; PUSH EBP; MOV EBP,[ESP+0xC] | CMP byte [EBP+4],1; PUSH ESI; PUSH EDI; MOV EDI,EAX */
static const uint8_t k_search_sig[] =
    { 0x53, 0x55, 0x8B, 0x6C, 0x24, 0x0C, 0x80, 0x7D, 0x04, 0x01, 0x56, 0x57, 0x8B, 0xF8 };
/* SUB ESP,0x24; PUSH EBX; MOV EBX,[ESP+0x30] | PUSH EBP; MOV EBP,[ESP+0x30] */
static const uint8_t k_dispatch_sig[] =
    { 0x83, 0xEC, 0x24, 0x53, 0x8B, 0x5C, 0x24, 0x30, 0x55, 0x8B, 0x6C, 0x24, 0x30 };
/* PUSH EBP; MOV EBP,ESP; AND ESP,-8 | MOV EAX,0x1514 */
static const uint8_t k_pool_sig[] =
    { 0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF8, 0xB8, 0x14, 0x15, 0x00, 0x00 };

int rift_install(void)
{
    rp_tr_search = patch_detour("rift search (FUN_00792510)", 0x00792510u,
                                k_search_sig, sizeof k_search_sig, 6,
                                (const void*)search_shim);
    rp_tr_dispatch = patch_detour("rift dispatch (FUN_00792610)", 0x00792610u,
                                  k_dispatch_sig, sizeof k_dispatch_sig, 8,
                                  (const void*)dispatch_shim);
    rp_tr_pool = patch_detour("rift pool (FUN_007927c0)", 0x007927C0u,
                              k_pool_sig, sizeof k_pool_sig, 6,
                              (const void*)pool_shim);
    return rp_tr_search && rp_tr_dispatch && rp_tr_pool;
}
