/* vocation: vocation change for support pawns, chosen by a marker item.
 *
 * The vocation screen (uGUIJobChange, vtable 0x01563728) has exactly two
 * rows: row 0 = the Arisen, row 1 = party slot 1 (the main pawn).  Every
 * place in the screen's code that turns row 1 into a party slot is
 * patched to read `rp_vc_row1` instead.  It is 1 (vanilla) unless, when
 * the screen opens, a present support pawn (slot 2, then 3) carries the
 * marker item; then row 1 is that pawn.  Nothing is swapped, so nothing
 * needs restoring.
 *
 *   init  vtable[1] 0x00670130 wrapped: choose the pawn before, fix the
 *         row-1 unlock costs after (per pawn: see fix_costs).
 *   A     0x006701F8 / 0x0067020B / 0x00670527 / 0x006707A6  init: row 1
 *         present test, current job, alive/actor test, name.
 *   B     0x0067220C  FUN_006721b0: row DP for the cost display.
 *   C     IMUL r,0x1660 sites 0x006734EC 0x006736F1 0x00673D62 0x006740D6
 *         0x00674430 (skill list, vocation list, list build, can-select).
 *   D     0x0067179B  unlock step: param whose DP pays the unlock cost.
 *   E     0x006717E4  unlock step: skip the shared (main pawn) unlock flag
 *         for a redirected row.
 *   F     0x006713AF  close: wait for the redirected slot's refresh flag.
 *   G     0x00674360  apply: replaced (vocation, gear, skills, marker).
 *
 * See realpawns.md, "Vocation change via a marker item". */

#include <windows.h>
#include <stdint.h>
#include "vocation.h"
#include "game.h"
#include "patch.h"

#define VA_ITEMMGR_PTR   0x018DB4B4u   /* *VA = sItemManager            */
#define VA_ITEM_LOCKFLAG 0x018B1B90u   /* byte: always take item lock   */
#define VA_ACTORS_PTR    0x018DB4CCu
#define VA_VTBL_INIT     0x0156373Cu   /* uGUIJobChange vtable[1]       */
#define VA_INIT          0x00670130u
#define VA_FLAGS         0x014F9024u   /* s16[2][10] unlock flag ids    */
#define VA_COSTS         0x014F904Cu   /* s16[10] unlock DP cost        */

#define PARAM_OFF_ARISEN 0xA76D0u
#define PBASE_OFF_REFRESH 0xAC3C0u     /* u32[4] per-slot model refresh */
#define P_GONE   0x354u
#define P_SLOT   0x008u
#define P_JOB    0x010u
#define P_JOBLV  0x2CCu                /* u32[10] */
#define P_JOBNXT 0x31Cu                /* u32[10] */

#define G_GAMESTART 0x298u             /* u8: character-creation mode   */
#define G_JOB       0x29Cu             /* s32[2] chosen job per row     */
#define G_CHANGED   0x2A4u             /* u8[2]                          */
#define G_COST      0x270u             /* s16[2][10]                     */

#define BAG_STRIDE  0x11E8u
#define BAG_OFF     0x178u

int  rp_vc_marker = 1529;            /* Neutralizing Elixir */
int  rp_vc_consume;

/* read by the asm shims */
uint32_t rp_vc_row1 = 1;              /* party slot shown in row 1 */
uint32_t rp_vc_off  = PARTY_PARAM_BASE + PARTY_PARAM_STRIDE;  /* its param offset */

void *rp_vc_fn_init, *rp_vc_fn_442570, *rp_vc_fn_401280;
void *rp_vc_fn_671d00, *rp_vc_fn_77dd00, *rp_vc_fn_77dfb0;
void *rp_vc_fn_45f500, *rp_vc_fn_458230;
void *rp_vc_res_a1, *rp_vc_res_a2, *rp_vc_res_a3ok, *rp_vc_res_a3no, *rp_vc_res_a4;
void *rp_vc_res_b, *rp_vc_res_f;
void *rp_vc_res_c1, *rp_vc_res_c2, *rp_vc_res_c3, *rp_vc_res_c4, *rp_vc_res_c5;

static void set_row1(uint32_t slot)
{
    rp_vc_row1 = slot;
    rp_vc_off  = PARTY_PARAM_BASE + slot * PARTY_PARAM_STRIDE;
}

static uint8_t *slot_param(uint8_t *b, uint32_t slot)
{
    return slot == 0 ? b + PARAM_OFF_ARISEN
                     : b + PARTY_PARAM_BASE + slot * PARTY_PARAM_STRIDE;
}

static const uint8_t *actor_of(const uint8_t *param)
{
    const uint8_t *act = *(const uint8_t**)patch_va(VA_ACTORS_PTR);
    if (!act) return NULL;
    uint32_t idx = *(const uint32_t*)(param + P_SLOT);
    if (idx == 0) return *(const uint8_t**)(act + 0x99C);
    if (idx - 1 > 2) return NULL;
    return *(const uint8_t**)(act + 0x9A0 + (idx - 1) * 4);
}

/* Same test as FUN_0077da70. */
static int present(const uint8_t *param)
{
    return param[PARAM_OFF_ALIVE] && !param[P_GONE] && actor_of(param);
}

/* ---- item manager ---------------------------------------------------- */

/* FUN_0045f500: thiscall(ECX = itemMgr, EAX = owner; itemNo, kind) */
__attribute__((naked, used))
static void *__cdecl call_find_item(void *mgr, int owner, int item_no)
{
    __asm__ (
        "movl   4(%esp), %ecx          \n"
        "movl   8(%esp), %eax          \n"
        "pushl  $0                     \n"   /* kind: plain item */
        "pushl  16(%esp)               \n"   /* item_no (12 + 4) */
        "call   *_rp_vc_fn_45f500      \n"   /* ret 8 */
        "ret                           \n"
    );
}

/* FUN_00458230: EAX = item, ESI = bag container; push count */
__attribute__((naked, used))
static void __cdecl call_remove_item(void *item, void *bag, int count)
{
    __asm__ (
        "pushl  %esi                   \n"
        "movl   8(%esp), %eax          \n"
        "movl   12(%esp), %esi         \n"
        "pushl  16(%esp)               \n"
        "call   *_rp_vc_fn_458230      \n"   /* ret 4 */
        "popl   %esi                   \n"
        "ret                           \n"
    );
}

static uint8_t *item_mgr(void)
{
    return *(uint8_t**)patch_va(VA_ITEMMGR_PTR);
}

static int need_lock(const uint8_t *mgr)
{
    return mgr[0x1C] || *patch_va(VA_ITEM_LOCKFLAG);
}

/* How many markers does `slot` carry? */
static int marker_count(int slot)
{
    uint8_t *mgr = item_mgr();
    if (!mgr || rp_vc_marker <= 0) return 0;
    const uint8_t *it = call_find_item(mgr, slot, rp_vc_marker);
    return it ? *(const int16_t*)(it + 0x12) : 0;
}

static int remove_marker(int slot)
{
    uint8_t *mgr = item_mgr();
    if (!mgr) return 0;
    int lock = need_lock(mgr);
    if (lock) EnterCriticalSection((LPCRITICAL_SECTION)(mgr + 4));
    uint8_t *it = call_find_item(mgr, slot, rp_vc_marker);  /* relocks: recursive */
    if (it) {
        *(uint32_t*)(it + 0x20) &= ~0x80u;     /* as FUN_004615f0 does */
        call_remove_item(it, mgr + (uint32_t)slot * BAG_STRIDE + BAG_OFF, 1);
    }
    if (lock) LeaveCriticalSection((LPCRITICAL_SECTION)(mgr + 4));
    return it != NULL;
}

/* ---- init wrapper ---------------------------------------------------- */

__attribute__((used))
static void __cdecl voc_select(uint8_t *gui)
{
    set_row1(1);
    uint8_t *b = game_pbase();
    if (!b || gui[G_GAMESTART]) return;
    for (uint32_t s = 2; s <= 3; s++) {
        const uint8_t *p = slot_param(b, s);
        int pres = present(p), n = marker_count((int)s);
        patch_logf("vocation: slot %u: alive %u, gone %u, actor %s, "
                   "item %d x%d%s", s, p[PARAM_OFF_ALIVE], p[P_GONE],
                   actor_of(p) ? "yes" : "no", rp_vc_marker, n,
                   pres && n > 0 && rp_vc_row1 == 1 ? " -> chosen" : "");
        if (pres && n > 0 && rp_vc_row1 == 1) set_row1(s);
    }
}

/* User rule: a vocation is locked for a pawn when its rank is 1 and its
 * next-rank threshold is 0.  The game never writes that for rift pawns
 * (they start with every vocation at rank 1 and a nonzero threshold), so
 * in practice they pay nothing; the marker item is the cost. */
static int locked(const uint8_t *param, int j)
{
    return *(const uint32_t*)(param + P_JOBLV + j * 4) == 1 &&
           *(const uint32_t*)(param + P_JOBNXT + j * 4) == 0;
}

__attribute__((used))
static void __cdecl voc_after_init(uint8_t *gui)
{
    if (rp_vc_row1 == 1) return;
    uint8_t *b = game_pbase();
    if (!b) return;
    const uint8_t *p = slot_param(b, rp_vc_row1);
    const int16_t *flags = (const int16_t*)patch_va(VA_FLAGS) + 10;
    const int16_t *costs = (const int16_t*)patch_va(VA_COSTS);
    int16_t *row = (int16_t*)(gui + G_COST) + 10;
    for (int j = 0; j < 10; j++)
        row[j] = (flags[j] >= 0 && locked(p, j)) ? costs[j] : 0;
}

__attribute__((naked, used))
static void init_shim(void)
{
    __asm__ (
        "pushl  %ecx                   \n"   /* gui (kept for after) */
        "pushl  %ecx                   \n"
        "call   _voc_select            \n"
        "addl   $4, %esp               \n"
        "movl   (%esp), %ecx           \n"
        "call   *_rp_vc_fn_init        \n"   /* fastcall, plain ret */
        "call   _voc_after_init        \n"   /* arg still at (%esp) */
        "addl   $4, %esp               \n"
        "ret                           \n"
    );
}

/* ---- A: init, hardcoded slot 1 --------------------------------------- */

__attribute__((naked, used)) static void a1_shim(void)
{   /* LEA EAX,[ESI+0xA7EC0] */
    __asm__ ("movl _rp_vc_off, %eax\n addl %esi, %eax\n jmp *_rp_vc_res_a1\n");
}
__attribute__((naked, used)) static void a2_shim(void)
{   /* MOV EAX,[ESI+0xA7ED0] */
    __asm__ ("movl _rp_vc_off, %eax\n movl 0x10(%esi,%eax), %eax\n jmp *_rp_vc_res_a2\n");
}
__attribute__((naked, used)) static void a3_shim(void)
{   /* EAX = pBase: alive, gone, then EAX = slot index; else skip */
    __asm__ (
        "pushl  %edx                   \n"
        "movl   _rp_vc_off, %edx       \n"
        "addl   %eax, %edx             \n"
        "cmpb   $0, 4(%edx)            \n"
        "je     1f                     \n"
        "cmpb   $0, 0x354(%edx)        \n"
        "jne    1f                     \n"
        "movl   8(%edx), %eax          \n"
        "popl   %edx                   \n"
        "jmp    *_rp_vc_res_a3ok       \n"
        "1:                            \n"
        "popl   %edx                   \n"
        "jmp    *_rp_vc_res_a3no       \n"
    );
}
__attribute__((naked, used)) static void a4_shim(void)
{   /* LEA EDX,[ESI+0xA7EC0] */
    __asm__ ("movl _rp_vc_off, %edx\n addl %esi, %edx\n jmp *_rp_vc_res_a4\n");
}

/* ---- B: cost display -------------------------------------------------- */

__attribute__((naked, used)) static void b_shim(void)
{   /* ADD EAX,0xA7EC0 */
    __asm__ ("addl _rp_vc_off, %eax\n jmp *_rp_vc_res_b\n");
}

/* ---- C: IMUL r,r,0x1660 with r = row ---------------------------------- */

#define IMUL_SHIM(name, reg, res)                                          \
    __attribute__((naked, used)) static void name(void)                    \
    {                                                                      \
        __asm__ (                                                          \
            "cmpl   $1, %" reg "           \n"                             \
            "jne    1f                     \n"                             \
            "movl   _rp_vc_row1, %" reg "  \n"                             \
            "1:                            \n"                             \
            "imull  $0x1660, %" reg ", %" reg "\n"                         \
            "jmp    *_" res "              \n");                           \
    }
IMUL_SHIM(c1_shim, "eax", "rp_vc_res_c1")
IMUL_SHIM(c2_shim, "eax", "rp_vc_res_c2")
IMUL_SHIM(c3_shim, "eax", "rp_vc_res_c3")
IMUL_SHIM(c4_shim, "eax", "rp_vc_res_c4")
IMUL_SHIM(c5_shim, "edx", "rp_vc_res_c5")

/* ---- D/E: unlock step (ESI = row) ------------------------------------ */

__attribute__((naked, used)) static void d_shim(void)
{   /* replaces CALL FUN_00442570 (EAX = row, ECX = pBase) */
    __asm__ (
        "cmpl   $1, %eax               \n"
        "jne    1f                     \n"
        "movl   _rp_vc_row1, %eax      \n"
        "1:                            \n"
        "jmp    *_rp_vc_fn_442570      \n"
    );
}
__attribute__((naked, used)) static void e_shim(void)
{   /* replaces CALL FUN_00401280 (set unlock flag EDI) */
    __asm__ (
        "cmpl   $1, %esi               \n"
        "jne    1f                     \n"
        "cmpl   $1, _rp_vc_row1        \n"
        "je     1f                     \n"
        "ret                           \n"   /* redirected: not shared */
        "1:                            \n"
        "jmp    *_rp_vc_fn_401280      \n"
    );
}

/* ---- F: close waits for the refresh flag (ESI = row, ECX = pBase) ----- */

__attribute__((naked, used)) static void f_shim(void)
{
    __asm__ (
        "pushl  %esi                   \n"
        "cmpl   $1, %esi               \n"
        "jne    1f                     \n"
        "movl   _rp_vc_row1, %esi      \n"
        "1:                            \n"
        "cmpl   $0, 0xac3c0(%ecx,%esi,4)\n"
        "popl   %esi                   \n"   /* flags survive */
        "jmp    *_rp_vc_res_f          \n"
    );
}

/* ---- G: apply --------------------------------------------------------- */

/* One row of the original FUN_00674360 loop, with its register setup:
 * EBX = gui, ESI = slot, EDI = param. */
__attribute__((naked, used))
static void __cdecl apply_row(uint8_t *gui, uint32_t slot, uint8_t *param, int job)
{
    __asm__ (
        "pushl  %ebx                   \n"
        "pushl  %esi                   \n"
        "pushl  %edi                   \n"
        "movl   16(%esp), %ebx         \n"
        "movl   20(%esp), %esi         \n"
        "movl   24(%esp), %edi         \n"
        "movl   28(%esp), %eax         \n"
        "pushl  %eax                   \n"   /* job */
        "movl   %eax, 0x10(%edi)       \n"
        "pushl  %ebx                   \n"   /* gui */
        "movl   %esi, %eax             \n"
        "call   *_rp_vc_fn_671d00      \n"   /* ret 8 */
        "movl   %edi, %ecx             \n"
        "call   *_rp_vc_fn_77dd00      \n"
        "call   *_rp_vc_fn_77dfb0      \n"   /* EDI = param */
        "popl   %edi                   \n"
        "popl   %esi                   \n"
        "popl   %ebx                   \n"
        "ret                           \n"
    );
}

__attribute__((used))
static void __cdecl voc_apply(uint8_t *gui)
{
    for (uint32_t row = 0; row < 2; row++) {
        uint8_t *b = game_pbase();
        if (!b) return;
        int job = *(int32_t*)(gui + G_JOB + row * 4);
        if (job <= 0 || !gui[G_CHANGED + row]) continue;
        uint32_t slot = row == 1 ? rp_vc_row1 : 0;
        uint8_t *p = slot_param(b, slot);
        int old = *(int32_t*)(p + P_JOB);
        apply_row(gui, slot, p, job);
        b = game_pbase();
        *(uint32_t*)(b + PBASE_OFF_REFRESH + slot * 4) = 1;
        if (slot >= 2 && old != job) {
            int gone = rp_vc_consume && remove_marker((int)slot);
            patch_logf("vocation: slot %u: vocation %d -> %d%s", slot, old,
                       job, rp_vc_consume ? (gone ? ", marker used up"
                                                  : ", marker NOT found")
                                          : "");
        }
    }
}

__attribute__((naked, used)) static void g_shim(void)
{
    __asm__ (
        "pushl  4(%esp)                \n"
        "call   _voc_apply             \n"
        "addl   $4, %esp               \n"
        "ret    $4                     \n"
    );
}

/* ---- install ---------------------------------------------------------- */

struct site {
    const char    *label;
    uint32_t       va;        /* signature start */
    const uint8_t *sig;
    size_t         sig_n;
    size_t         off;       /* patched instruction */
    size_t         len;       /* its length (>= 5) */
    uint8_t        op;        /* 0xE9 jmp, 0xE8 call */
    const void    *shim;
};

#define S(...) (const uint8_t[]){ __VA_ARGS__ }, sizeof((const uint8_t[]){ __VA_ARGS__ })

/* IMUL r,r,0x1660 + LEA x,[r+...+0xA6860] */
static const struct site k_sites[] = {
    { "vocation A1 (0x006701F8)", 0x006701F8u,
      S(0x8D,0x86,0xC0,0x7E,0x0A,0x00, 0x85,0xC0, 0x74,0x1D, 0xE8,0x69,0xD8,0x10,0x00),
      0, 6, 0xE9, (const void*)a1_shim },
    { "vocation A2 (0x0067020B)", 0x0067020Bu,
      S(0x8B,0x86,0xD0,0x7E,0x0A,0x00, 0x89,0x83,0xA0,0x02,0x00,0x00),
      0, 6, 0xE9, (const void*)a2_shim },
    { "vocation A3 (0x00670527)", 0x00670522u,
      S(0xA1,0x9C,0xB4,0x8D,0x01, 0x80,0xB8,0xC4,0x7E,0x0A,0x00,0x00, 0x74,0x3F,
        0x80,0xB8,0x14,0x82,0x0A,0x00,0x00, 0x75,0x36, 0x8B,0x80,0xC8,0x7E,0x0A,0x00,
        0x3B,0xC7),
      5, 7, 0xE9, (const void*)a3_shim },
    { "vocation A4 (0x006707A6)", 0x006707A6u,
      S(0x8D,0x96,0xC0,0x7E,0x0A,0x00, 0x85,0xD2, 0x75,0x07),
      0, 6, 0xE9, (const void*)a4_shim },
    { "vocation B (0x0067220C)", 0x006721FCu,
      S(0xA1,0x9C,0xB4,0x8D,0x01, 0x85,0xC9, 0x75,0x07, 0x05,0xD0,0x76,0x0A,0x00,
        0xEB,0x05, 0x05,0xC0,0x7E,0x0A,0x00, 0x8B,0x88,0x44,0x03,0x00,0x00),
      16, 5, 0xE9, (const void*)b_shim },
    { "vocation C1 (0x006734EC)", 0x006734ECu,
      S(0x69,0xC0,0x60,0x16,0x00,0x00, 0x8D,0x84,0x08,0x60,0x68,0x0A,0x00),
      0, 6, 0xE9, (const void*)c1_shim },
    { "vocation C2 (0x006736F1)", 0x006736F1u,
      S(0x69,0xC0,0x60,0x16,0x00,0x00, 0x8D,0x94,0x08,0x60,0x68,0x0A,0x00),
      0, 6, 0xE9, (const void*)c2_shim },
    { "vocation C3 (0x00673D62)", 0x00673D62u,
      S(0x69,0xC0,0x60,0x16,0x00,0x00, 0x8D,0x84,0x30,0x60,0x68,0x0A,0x00),
      0, 6, 0xE9, (const void*)c3_shim },
    { "vocation C4 (0x006740D6)", 0x006740D6u,
      S(0x69,0xC0,0x60,0x16,0x00,0x00, 0x8D,0x94,0x08,0x60,0x68,0x0A,0x00),
      0, 6, 0xE9, (const void*)c4_shim },
    { "vocation C5 (0x00674430)", 0x00674430u,
      S(0x69,0xD2,0x60,0x16,0x00,0x00, 0x8D,0xB4,0x32,0x60,0x68,0x0A,0x00),
      0, 6, 0xE9, (const void*)c5_shim },
    { "vocation D (0x0067179B)", 0x00671791u,
      S(0x8B,0xF8, 0x8B,0xC6, 0x8B,0xCA, 0x89,0x7C,0x24,0x0C, 0xE8,0xD0,0x0D,0xDD,0xFF),
      10, 5, 0xE8, (const void*)d_shim },
    { "vocation E (0x006717E4)", 0x006717D0u,
      S(0x0F,0xB7,0x04,0x45,0x24,0x90,0x4F,0x01, 0x33,0xC9, 0x66,0x3B,0xC8,
        0x7F,0x14, 0x0F,0xBF,0xF8, 0x8B,0xC2, 0xE8,0x97,0xFA,0xD8,0xFF),
      20, 5, 0xE8, (const void*)e_shim },
    { "vocation F (0x006713AF)", 0x006713AFu,
      S(0x83,0xBC,0xB1,0xC0,0xC3,0x0A,0x00,0x00, 0x0F,0x85,0xE4,0x07,0x00,0x00),
      0, 8, 0xE9, (const void*)f_shim },
    { "vocation G (0x00674360)", 0x00674360u,
      S(0x51, 0x8B,0x0D,0x9C,0xB4,0x8D,0x01, 0x53, 0x8B,0x5C,0x24,0x0C, 0x56,0x57,0x33,0xF6),
      0, 7, 0xE9, (const void*)g_shim },
};

static int install_site(const struct site *s)
{
    uint8_t code[16];
    uint8_t *at = patch_va(s->va) + s->off;
    code[0] = s->op;
    *(int32_t*)(code + 1) = (int32_t)((const uint8_t*)s->shim - (at + 5));
    for (size_t i = 5; i < s->len; i++) code[i] = 0x90;
    return patch_bytes(s->label, s->va, s->sig, s->sig_n, s->off, code, s->len);
}

int vocation_install(void)
{
    rp_vc_fn_init   = patch_va(VA_INIT);
    rp_vc_fn_442570 = patch_va(0x00442570u);
    rp_vc_fn_401280 = patch_va(0x00401280u);
    rp_vc_fn_671d00 = patch_va(0x00671D00u);
    rp_vc_fn_77dd00 = patch_va(0x0077DD00u);
    rp_vc_fn_77dfb0 = patch_va(0x0077DFB0u);
    rp_vc_fn_45f500 = patch_va(0x0045F500u);
    rp_vc_fn_458230 = patch_va(0x00458230u);
    rp_vc_res_a1   = patch_va(0x006701FEu);
    rp_vc_res_a2   = patch_va(0x00670211u);
    rp_vc_res_a3ok = patch_va(0x0067053Fu);
    rp_vc_res_a3no = patch_va(0x0067056Fu);
    rp_vc_res_a4   = patch_va(0x006707ACu);
    rp_vc_res_b    = patch_va(0x00672211u);
    rp_vc_res_c1   = patch_va(0x006734F2u);
    rp_vc_res_c2   = patch_va(0x006736F7u);
    rp_vc_res_c3   = patch_va(0x00673D68u);
    rp_vc_res_c4   = patch_va(0x006740DCu);
    rp_vc_res_c5   = patch_va(0x00674436u);
    rp_vc_res_f    = patch_va(0x006713B7u);

    /* the other functions the shims call, unchanged since analysis */
    static const uint8_t k_find[]   = { 0x56, 0x8B, 0xF0, 0x57, 0x8B, 0xF9, 0x83, 0xFE, 0x04 };
    static const uint8_t k_442570[] = { 0x83, 0xF8, 0x04, 0x75, 0x03, 0x33, 0xC0, 0xC3 };
    uint32_t vt_rt = (uint32_t)(uintptr_t)patch_va(VA_INIT);  /* vtable holds &init */

    int n = (int)(sizeof k_sites / sizeof k_sites[0]);
    int ok = patch_check("vocation: item find (0x0045F500)", 0x0045F500u, k_find, sizeof k_find) &&
             patch_check("vocation: party param (0x00442570)", 0x00442570u, k_442570, sizeof k_442570) &&
             patch_check("vocation: init vtable (0x0156373C)", VA_VTBL_INIT,
                         (const uint8_t*)&vt_rt, 4);
    for (int i = 0; ok && i < n; i++)
        ok = patch_check(k_sites[i].label, k_sites[i].va, k_sites[i].sig, k_sites[i].sig_n);
    if (!ok) return 0;

    /* code sites first (all no-ops while row1 = 1), the init wrapper last */
    for (int i = 0; i < n; i++)
        if (!install_site(&k_sites[i])) return 0;
    uint32_t shim = (uint32_t)(uintptr_t)init_shim;
    return patch_bytes("vocation: init wrapper", VA_VTBL_INIT,
                       (const uint8_t*)&vt_rt, 4, 0, (const uint8_t*)&shim, 4);
}
