/* discipline: Discipline (DP + vocation rank progress) for support pawns.
 *
 * FUN_00442f20 hands kill XP to party slots 0..3.  Slots 0/1 call the
 * award routine FUN_00780380 (EDI = param, stack = xp), which adds the XP
 * to param+0x2C4 and the derived Discipline to mJobExp[voc] (+0x2F4) and
 * the mJobPoint pool (+0x344).  Slots 2/3 only bank 10% of the XP for the
 * owner at param+0x34C.  Three patches (GOG VAs):
 *
 *   A 0x004431FA  slot 2/3 case: also call FUN_00780380 with the same XP,
 *                 then carry on with the original 10% banking.
 *   B 0x007803B5  JNZ that returns unless param+0x7F4 == 1 (main pawn):
 *                 NOPed.  The only caller is FUN_00442f20.
 *   C 0x0078040B  ADD [EDI+0x2C4],ECX (the XP add): skipped when the
 *                 param's party slot (+0x008) is 2 or 3, unless the pawn is
 *                 a realpawns clone (key +0x15E4 = 0x1200nnnn) and leveling
 *                 is on.  Other support pawns never level up: their level
 *                 would be lost on the next load.
 *
 * Clones get the XP the main pawn would get (slot 1 case of FUN_00442f20):
 *   xp + (ArisenTotal - PawnTotal) * rate * 0.01, at least 0,
 * where Total = FUN_00b597b0(level) + exp and rate comes from the object
 * at pBase+0xB8754 (vtable +0x3C).  Pawns behind the Arisen catch up;
 * pawns far ahead get little or nothing.  The Discipline they earn is
 * derived from that same amount, as for the main pawn.  Level-ups then
 * happen in the game's per-frame FUN_00b59380 like for any party member.
 *
 * Rank-ups from mJobExp are handled every frame by FUN_00b59890 for any
 * party member, so nothing else is needed. */

#include <windows.h>
#include <stdint.h>
#include "discipline.h"
#include "game.h"
#include "patch.h"
#include "pawnxml.h"

int rp_level_up = 1;      /* ini [features] leveling */

void *rp_dp_award;        /* FUN_00780380                     */
void *rp_dp_tenth;        /* &float 0.1 at 0x014CC42C         */
void *rp_dp_resume_a;     /* 0x00443200                       */
void *rp_dp_resume_c;     /* 0x00780411                       */

#define VA_LEVEL_TOTAL  0x00B597B0u  /* cdecl u32 (level): XP to reach it */
#define PBASE_OFF_RATE  0xB8754u     /* object; vtable +0x3C: catch-up rate */

typedef uint32_t (__cdecl *level_total_fn)(uint32_t level);
typedef float (__attribute__((thiscall)) *rate_fn)(void *self, int arg);

/* XP to award support pawn `param` for a kill worth `xp`. */
__attribute__((used))
static uint32_t __cdecl support_xp(uint8_t *param, uint32_t xp, float arisen_total)
{
    uint32_t key = *(uint32_t*)(param + PARAM_OFF_KEY);
    if (!rp_level_up || !rp_key_clone(key)) return xp;
    uint32_t lv = *(uint32_t*)(param + PARAM_OFF_LEVEL);
    if (lv >= 200) return xp;
    uint8_t *b = game_pbase();
    float rate = 0.0f;
    void *obj = b ? *(void**)(b + PBASE_OFF_RATE) : NULL;
    if (obj) {
        rate_fn fn = (rate_fn)(*(void***)obj)[0x3C / 4];
        rate = fn(obj, 0);
    }
    float pawn_total = (float)((level_total_fn)patch_va(VA_LEVEL_TOTAL))(lv) +
                       (float)*(uint32_t*)(param + PARAM_OFF_EXP);
    float v = (arisen_total - pawn_total) * rate * 0.01f + (float)xp;
    if (v < 0.0f) v = 0.0f;
    if (v > 4.0e9f) v = 4.0e9f;
    return (uint32_t)(long long)v;          /* the game truncates too */
}

/* At 0x004431FA: ESI = param, EBP = xp (int), ST0 = xp (float), and the
 * Arisen's total XP (float) at [ESP+0x18] (computed in the slot 0 case).
 * Replaces FMUL dword [0x014CC42C]. */
__attribute__((naked, used))
static void support_award_shim(void)
{
    __asm__ (
        "subl   $8, %esp               \n"
        "fstpl  (%esp)                 \n"   /* park ST0 */
        "pushl  0x20(%esp)             \n"   /* Arisen total (float) */
        "pushl  %ebp                   \n"   /* xp */
        "pushl  %esi                   \n"   /* param */
        "call   _support_xp            \n"
        "addl   $12, %esp              \n"
        "movl   %esi, %edi             \n"   /* EDI = param (free here) */
        "pushl  %eax                   \n"   /* xp to award */
        "call   *_rp_dp_award          \n"   /* ret 4 */
        "fldl   (%esp)                 \n"
        "addl   $8, %esp               \n"
        "movl   _rp_dp_tenth, %eax     \n"   /* EAX is rewritten next */
        "fmuls  (%eax)                 \n"
        "jmp    *_rp_dp_resume_a       \n"
    );
}

/* At 0x0078040B: EDI = param, ECX = xp to add. */
__attribute__((naked, used))
static void exp_add_shim(void)
{
    __asm__ (
        "cmpl   $2, 8(%edi)            \n"
        "jb     2f                     \n"
        "cmpl   $0, _rp_level_up       \n"
        "je     1f                     \n"
        "pushl  %eax                   \n"
        "movl   0x15e4(%edi), %eax     \n"   /* mRomPawnUniqueID */
        "andl   $0xffff0000, %eax      \n"
        "cmpl   $0x12000000, %eax      \n"   /* clone? */
        "popl   %eax                   \n"
        "jne    1f                     \n"
        "2:                            \n"
        "addl   %ecx, 0x2c4(%edi)      \n"
        "1:                            \n"
        "jmp    *_rp_dp_resume_c       \n"
    );
}

/* FMUL dword [0x014CC42C] | FNSTCW [ESP+0x10] */
static const uint8_t k_sig_a[] =
    { 0xD8, 0x0D, 0x2C, 0xC4, 0x4C, 0x01, 0xD9, 0x7C, 0x24, 0x10 };
/* CMP dword [EDI+0x7F4],1 | JNZ 0x00780590 */
static const uint8_t k_sig_b[] =
    { 0x83, 0xBF, 0xF4, 0x07, 0x00, 0x00, 0x01, 0x0F, 0x85, 0xD5, 0x01, 0x00, 0x00 };
static const uint8_t k_nop6[] = { 0x90, 0x90, 0x90, 0x90, 0x90, 0x90 };
/* MOV ECX,[ESP+8] | ADD [EDI+0x2C4],ECX | FLDCW [ESP+0x14] */
static const uint8_t k_sig_c[] =
    { 0x8B, 0x4C, 0x24, 0x08, 0x01, 0x8F, 0xC4, 0x02, 0x00, 0x00, 0xD9, 0x6C, 0x24, 0x14 };

int discipline_install(void)
{
    rp_dp_award    = patch_va(0x00780380u);
    rp_dp_tenth    = patch_va(0x014CC42Cu);
    rp_dp_resume_a = patch_va(0x00443200u);
    rp_dp_resume_c = patch_va(0x00780411u);

    /* all three signatures first, so it's all or nothing */
    if (!patch_check("discipline A (0x004431FA)", 0x004431FAu, k_sig_a, sizeof k_sig_a) ||
        !patch_check("discipline B (0x007803AE)", 0x007803AEu, k_sig_b, sizeof k_sig_b) ||
        !patch_check("discipline C (0x00780407)", 0x00780407u, k_sig_c, sizeof k_sig_c))
        return 0;

    /* C before A: the XP skip must be live before slots 2/3 reach the award */
    int ok = patch_jmp6("discipline C: XP for slots 2/3 only if clone", 0x00780407u,
                        k_sig_c, sizeof k_sig_c, 4, (const void*)exp_add_shim);
    ok = ok && patch_bytes("discipline B: award gate", 0x007803AEu,
                           k_sig_b, sizeof k_sig_b, 7, k_nop6, sizeof k_nop6);
    ok = ok && patch_jmp6("discipline A: award slots 2/3", 0x004431FAu,
                          k_sig_a, sizeof k_sig_a, 0, (const void*)support_award_shim);
    return ok;
}
