/* rift: rift pawns that keep their progress, as clones.
 *
 * Hiring an official rift pawn (key 0x10LLIIII) makes a clone with its own
 * key 0x1200nnnn (store.c); the vanilla pawn is never touched.  A clone is
 * built like any rift pawn, from its origin's template at its own saved
 * level, and then gets its saved progress on top.
 *
 * Detours (GOG VAs):
 *   FUN_0047a260(builder, list, items)  rift list builder.  Before: resync
 *       the store; for "search by level" (builder+0x154C == 1) queue the
 *       clones saved at the queried level (builder+0x1528).  After: unqueue.
 *   FUN_00792510(mgr, out; EAX=level)   one search listing.  After: while
 *       the queue has clones, the listing becomes the next clone.
 *   FUN_00792610(mgr, out, key)         key dispatcher (favorites, history,
 *       pool refill).  After: a clone key is rebuilt from its file.
 *   FUN_007927c0(mgr, count)            random pool fill of pBase+0x16E0.
 *       After: re-walk the region.
 *   FUN_0043e0c0(pBase, idx, p, rec, flag)  party join (hire).  Before: a
 *       vanilla record gets a new clone key, so the party slot, the rift's
 *       "in party" marks and later favorites/history all carry it.
 * Every record handed out also gets the innate skills (innate.c). */

#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "rift.h"
#include "store.h"
#include "game.h"
#include "patch.h"
#include "innate.h"

/* ---- 0x14E8 record offsets ---------------------------------------------- */
#define R_NAME         0x008u   /* char[25]                   */
#define R_LEVEL        0x05Cu   /* u8                         */
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
#define R_STATS        0x1E0u   /* f32[10] mHp .. mBasicMgcDefend */
#define R_EXP          0x208u   /* u32                        */
#define R_NEXTEXP      0x20Cu   /* u32                        */
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

/* ---- template banks (FUN_00792510 / FUN_00792610) ------------------------ */
#define VA_BRACKETS    0x014EB954u  /* u8[33]: top level of each bank */
#define BANKS          33
#define MGR_OFF_READY  0x04u        /* u8, 1 = loaded */
#define MGR_OFF_BANKS  0x0Cu        /* bank*[33] */
#define BANK_OFF_COUNT 0x64u
#define BANK_OFF_LIST  0x70u
#define VA_SYNTH       0x00792C10u  /* stdcall(mgr, out, template, level) */

/* ---- list builder ---------------------------------------------------------- */
#define B_OFF_LEVEL    0x1528u
#define B_OFF_MODE     0x154Cu      /* 1 = search by level */
#define QUEUE_MAX      100

int rp_innate_enabled = 1;

typedef void (__stdcall *synth_fn)(void *mgr, void *out, void *tmpl, uint32_t level);

static uint8_t *g_mgr;              /* last rift manager seen */
static uint32_t g_queue[QUEUE_MAX];
static int      g_qn, g_qi, g_armed, g_qlevel;

static uint32_t rec_key(const uint8_t *rec)
{
    uint32_t k;
    memcpy(&k, rec + REC_OFF_KEY, 4);
    return k;
}

static void rec_name(const uint8_t *rec, char out[26])
{
    memcpy(out, rec + R_NAME, 25);
    out[25] = 0;
}

static int grant_innate(uint8_t *rec)
{
    uint32_t key = rec_key(rec);
    if (!rp_innate_enabled ||
        !(rp_key_vanilla(key) || rp_key_story(key) || rp_key_clone(key)))
        return 0;
    return innate_grant((uint32_t*)(rec + R_SKILLLV1),
                        (const uint32_t*)(rec + R_SKILLLV2), NULL) > 0;
}

/* The template a vanilla key was made from, or NULL. */
static void *origin_template(uint8_t *mgr, uint32_t origin)
{
    if (!mgr || mgr[MGR_OFF_READY] != 1) return NULL;
    uint32_t lv = (origin >> 16) & 0xFF, idx = origin & 0xFFFF;
    const uint8_t *top = patch_va(VA_BRACKETS);
    int b = 0;
    while (b < BANKS && lv > top[b]) b++;
    if (b == BANKS) b = 0;
    uint8_t *bank = *(uint8_t**)(mgr + MGR_OFF_BANKS + (uint32_t)b * 4);
    if (!bank || idx >= *(uint32_t*)(bank + BANK_OFF_COUNT)) return NULL;
    return (*(void***)(bank + BANK_OFF_LIST))[idx];
}

/* Saved progress onto a record already built from the clone's template. */
static void overlay(uint8_t *rec, const struct rp_clone *c)
{
    const struct rp_pawn *p = &c->pawn;
    rec[R_LEVEL] = p->level;
    memcpy(rec + R_STATS,   p->stats,     sizeof p->stats);
    memcpy(rec + R_EXP,     &p->exp,      4);
    memcpy(rec + R_NEXTEXP, &p->next_exp, 4);

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
    memcpy(rec + REC_OFF_KEY, &c->key, 4);
}

/* Build clone `c` into `rec`: its origin's template at its saved level,
 * then its progress.  `where` NULL = quiet. */
static int build_clone(uint8_t *mgr, uint8_t *rec, const struct rp_clone *c,
                       const char *where)
{
    void *tmpl = origin_template(mgr, c->origin);
    if (!tmpl) {
        patch_logf("%s: %08X '%s': no template for origin %08X, left as is",
                   where ? where : "rift", c->key, c->pawn.name, c->origin);
        return 0;
    }
    ((synth_fn)patch_va(VA_SYNTH))(mgr, rec, tmpl, c->pawn.level);
    overlay(rec, c);
    if (where) {
        const struct rp_pawn *p = &c->pawn;
        patch_logf("%s: clone %08X '%s' lv %u (of %08X): voc %u rank %u, DP %d",
                   where, c->key, p->name, p->level, c->origin, p->job,
                   p->job < RP_JOBS ? p->job_level[p->job] : 0, p->job_point);
    }
    return 1;
}

/* Log a vanilla record whose pawn has clones (test aid: the original must
 * still show up as itself). */
static void note_vanilla(const uint8_t *rec, const char *where)
{
    uint32_t key = rec_key(rec), clones[4];
    if (!rp_key_vanilla(key)) return;
    int n = store_clones_of(key, clones, 4);
    if (!n) return;
    char nm[26], list[48] = "";
    rec_name(rec, nm);
    for (int i = 0, o = 0; i < n && o < (int)sizeof list - 10; i++)
        o += _snprintf(list + o, sizeof list - o, "%s%08X", i ? ", " : "", clones[i]);
    patch_logf("%s: vanilla %08X '%s' lv %u (has clone%s %s)", where, key, nm,
               rec[R_LEVEL], n > 1 ? "s" : "", list);
}

int rift_overlay_pool(void)
{
    uint8_t *b = game_pbase();
    if (!b) return 0;
    int n = 0;
    for (int i = 0; i < REC_REGION_COUNT; i++) {
        uint8_t *rec = b + REC_REGION_OFF + (uint32_t)i * REC_SIZE;
        uint32_t key = rec_key(rec);
        if (rp_key_clone(key)) {
            /* already built from the right template: progress only */
            const struct rp_clone *c = store_get(key);
            if (c) { overlay(rec, c); n++; }
        }
        grant_innate(rec);
    }
    return n;
}

/* ---- hook bodies (cdecl, called from the shims) ------------------------- */

__attribute__((used))
static void __cdecl on_build_begin(uint8_t *builder)
{
    store_rescan();
    g_armed = 0;
    if (!builder || *(int32_t*)(builder + B_OFF_MODE) != 1) return;
    int lv = *(int32_t*)(builder + B_OFF_LEVEL);
    if (lv < 1) lv = 1;
    if (lv > 200) lv = 200;
    g_qlevel = lv;
    g_qn = store_by_level(lv, g_queue, QUEUE_MAX);
    g_qi = 0;
    g_armed = 1;
    patch_logf("search: level %d: %d clone(s) listed first", lv, g_qn);
}

__attribute__((used))
static void __cdecl on_build_end(void)
{
    g_armed = 0;
}

__attribute__((used))
static void __cdecl on_search(uint8_t *mgr, uint8_t *out, uint32_t ret)
{
    if (mgr) g_mgr = mgr;
    if (!(ret & 0xFF) || !out) return;
    while (g_armed && g_qi < g_qn) {
        const struct rp_clone *c = store_get(g_queue[g_qi++]);
        if (c && c->pawn.level == g_qlevel && build_clone(mgr, out, c, "search"))
            break;
    }
    note_vanilla(out, "search");
    grant_innate(out);
}

__attribute__((used))
static void __cdecl on_dispatch(uint8_t *mgr, uint8_t *out, uint32_t key,
                                uint32_t ret)
{
    static DWORD last_rescan;
    if (mgr) g_mgr = mgr;
    if (!(ret & 0xFF) || !out) return;
    if (rp_key_clone(key)) {
        const struct rp_clone *c = store_get(key);
        if (!c && GetTickCount() - last_rescan > 2000) {
            last_rescan = GetTickCount();
            store_rescan();
            c = store_get(key);
        }
        if (c) build_clone(mgr, out, c, "dispatch");
        else patch_logf("dispatch: clone %08X has no file; the game shows "
                        "it as unavailable", key);
    }
    note_vanilla(out, "dispatch");
    grant_innate(out);
}

__attribute__((used))
static void __cdecl on_pool(uint8_t *mgr)
{
    if (mgr) g_mgr = mgr;
    rift_overlay_pool();
}

static uint8_t *g_hire_rec;
static uint32_t g_hire_old, g_hire_new;

/* args: pBase, idx (0 main pawn, 1/2 = party slot 2/3), p, rec, flag */
__attribute__((used))
static void __cdecl on_hire_pre(const uint32_t *args)
{
    g_hire_rec = NULL;
    uint8_t *mgr = (uint8_t*)args[0];
    uint32_t idx = args[1];
    uint8_t *rec = (uint8_t*)args[3];
    if ((args[4] & 0xFF) || (idx != 1 && idx != 2) || !mgr || !rec) return;
    uint32_t key = rec_key(rec);
    if (!rp_key_vanilla(key)) return;
    /* the join is refused when the slot is taken */
    if (mgr[PBASE_OFF_HIRE_SLOT + idx * PARTY_PARAM_STRIDE + PARAM_OFF_ALIVE] == 1)
        return;
    uint32_t nk = store_new_clone(key);
    if (!nk) return;
    memcpy(rec + REC_OFF_KEY, &nk, 4);
    g_hire_rec = rec;
    g_hire_old = key;
    g_hire_new = nk;
}

__attribute__((used))
static void __cdecl on_hire_post(const uint32_t *args, uint32_t ret)
{
    uint8_t *rec = g_hire_rec;
    if (!rec) return;
    g_hire_rec = NULL;
    char nm[26];
    rec_name(rec, nm);
    if (ret & 0xFF) {
        patch_logf("hire: slot %u: '%s' %08X joins as clone %08X",
                   args[1] + 1, nm, g_hire_old, g_hire_new);
    } else {
        memcpy(rec + REC_OFF_KEY, &g_hire_old, 4);
        store_forget(g_hire_new);
        patch_logf("hire: slot %u: '%s' %08X not joined; clone number %08X "
                   "unused", args[1] + 1, nm, g_hire_old, g_hire_new);
    }
}

/* ---- shims ---------------------------------------------------------------
 * Each re-pushes the caller's stack arguments, CALLs the trampoline (which
 * pops them again: all are callee-cleanup), then runs the C body with the
 * original return value preserved in EAX.  EAX is also an input of
 * FUN_00792510 (level) and is passed through untouched. */

void *rp_tr_search, *rp_tr_dispatch, *rp_tr_pool, *rp_tr_build, *rp_tr_hire;

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
        "pushl  16(%esp)               \n"   /* arg: mgr */
        "call   _on_search             \n"
        "addl   $12, %esp              \n"
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
        "pushl  20(%esp)               \n"   /* arg: mgr */
        "call   _on_dispatch           \n"
        "addl   $16, %esp              \n"
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
        "pushl  %eax                   \n"   /* saved */
        "pushl  8(%esp)                \n"   /* arg: mgr */
        "call   _on_pool               \n"
        "addl   $4, %esp               \n"
        "popl   %eax                   \n"
        "ret    $8                     \n"
    );
}

__attribute__((naked, used))
static void build_shim(void)           /* stdcall(builder, list, items) */
{
    __asm__ (
        "pushl  4(%esp)                \n"   /* arg: builder */
        "call   _on_build_begin        \n"
        "addl   $4, %esp               \n"
        "pushl  12(%esp)               \n"
        "pushl  12(%esp)               \n"
        "pushl  12(%esp)               \n"
        "call   *_rp_tr_build          \n"
        "pushl  %eax                   \n"
        "call   _on_build_end          \n"
        "popl   %eax                   \n"
        "ret    $12                    \n"
    );
}

__attribute__((naked, used))
static void hire_shim(void)            /* stdcall(pBase, idx, p, rec, flag) */
{
    __asm__ (
        "leal   4(%esp), %eax          \n"
        "pushl  %eax                   \n"   /* arg: args */
        "call   _on_hire_pre           \n"
        "addl   $4, %esp               \n"
        "pushl  20(%esp)               \n"
        "pushl  20(%esp)               \n"
        "pushl  20(%esp)               \n"
        "pushl  20(%esp)               \n"
        "pushl  20(%esp)               \n"
        "call   *_rp_tr_hire           \n"
        "pushl  %eax                   \n"   /* saved */
        "pushl  %eax                   \n"   /* arg: ret */
        "leal   12(%esp), %ecx         \n"
        "pushl  %ecx                   \n"   /* arg: args */
        "call   _on_hire_post          \n"
        "addl   $8, %esp               \n"
        "popl   %eax                   \n"
        "ret    $20                    \n"
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
/* SUB ESP,0x20; PUSH EBX; MOV EBX,[ESP+0x30] | PUSH EBP; PUSH ESI; MOV ESI,[ESP+0x30] */
static const uint8_t k_build_sig[] =
    { 0x83, 0xEC, 0x20, 0x53, 0x8B, 0x5C, 0x24, 0x30, 0x55, 0x56, 0x8B, 0x74, 0x24, 0x30 };
/* PUSH EBX; MOV EBX,[ESP+8] | CMP byte [EBX+0x1C],0; PUSH EBP; MOV EBP,[ESP+0x10] */
static const uint8_t k_hire_sig[] =
    { 0x53, 0x8B, 0x5C, 0x24, 0x08, 0x80, 0x7B, 0x1C, 0x00, 0x55, 0x8B, 0x6C, 0x24, 0x10 };
/* synthesizer prologue: PUSH EBP; MOV EBP,ESP; AND ESP,-8; SUB ESP,0x3C; PUSH EBX; MOV EBX,[EBP+0xC] */
static const uint8_t k_synth_sig[] =
    { 0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF8, 0x83, 0xEC, 0x3C, 0x53, 0x8B, 0x5D, 0x0C };
static const uint8_t k_brackets[] = { 5, 9, 13, 17, 21, 25, 30, 35, 40 };

int rift_install(void)
{
    if (!patch_check("rift synthesizer (FUN_00792c10)", VA_SYNTH,
                     k_synth_sig, sizeof k_synth_sig) ||
        !patch_check("rift level brackets", VA_BRACKETS,
                     k_brackets, sizeof k_brackets))
        return 0;
    rp_tr_search = patch_detour("rift search (FUN_00792510)", 0x00792510u,
                                k_search_sig, sizeof k_search_sig, 6,
                                (const void*)search_shim);
    rp_tr_dispatch = patch_detour("rift dispatch (FUN_00792610)", 0x00792610u,
                                  k_dispatch_sig, sizeof k_dispatch_sig, 8,
                                  (const void*)dispatch_shim);
    rp_tr_pool = patch_detour("rift pool (FUN_007927c0)", 0x007927C0u,
                              k_pool_sig, sizeof k_pool_sig, 6,
                              (const void*)pool_shim);
    rp_tr_build = patch_detour("rift list builder (FUN_0047a260)", 0x0047A260u,
                               k_build_sig, sizeof k_build_sig, 8,
                               (const void*)build_shim);
    rp_tr_hire = patch_detour("party join (FUN_0043e0c0)", 0x0043E0C0u,
                              k_hire_sig, sizeof k_hire_sig, 5,
                              (const void*)hire_shim);
    return rp_tr_search && rp_tr_dispatch && rp_tr_pool && rp_tr_build &&
           rp_tr_hire;
}
