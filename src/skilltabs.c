/* skilltabs: character tabs for support pawns on the inn skill screen.
 *
 * uGUISkillLearn's init FUN_006f8fd0 builds its character tab list as
 * values {0, 1} with count 1 (Arisen) or 2 (+ main pawn, if
 * FUN_0077da70 says the main pawn is present), hands it to the tab
 * builder FUN_004e7fc0(gui, count, values), and hides tab widgets
 * [count..3]; the layout has 4.  The tab callback FUN_006ffc70 copies the
 * selected value into gui+0x270 (party slot) and every lookup after that
 * is generic, as is the close handler FUN_00512460 (loops slots 0..3).
 *
 * We replace 0x006F906F..0x006F9095 (the list setup) with a shim that
 * fills values[] with every present slot, Arisen first, and sets EDI =
 * count.  values[] is the 4-int local at [ESP+0x10]; the uGUISkill screen
 * FUN_006f2730 builds the same 4-slot list. */

#include <windows.h>
#include <stdint.h>
#include "skilltabs.h"
#include "game.h"
#include "patch.h"
#include "innate.h"

#define VA_SITE        0x006F906Fu
#define VA_RESUME      0x006F9095u
#define VA_ACTORS_PTR  0x018DB4CCu   /* *VA + 0x99C: Arisen actor; +0x9A0 + (n-1)*4: party n */
#define PARAM_OFF_ARISEN 0xA76D0u    /* slot 0 param (not on the slot formula) */
#define PARAM_OFF_GONE   0x354u      /* u8, nonzero = not usable */
#define PARAM_OFF_SLOT   0x008u

void *rp_st_resume;

/* Same test as FUN_0077da70. */
static int present(const uint8_t *param)
{
    if (!param[PARAM_OFF_ALIVE] || param[PARAM_OFF_GONE]) return 0;
    uint32_t idx = *(const uint32_t*)(param + PARAM_OFF_SLOT);
    const uint8_t *act = *(const uint8_t**)patch_va(VA_ACTORS_PTR);
    if (!act) return 0;
    if (idx == 0) return *(const uint32_t*)(act + 0x99C) != 0;
    if (idx - 1 > 2) return 0;
    return *(const uint32_t*)(act + 0x9A0 + (idx - 1) * 4) != 0;
}

__attribute__((used))
static int __cdecl build_tabs(int32_t values[4])
{
    /* hired pawns synthesized before realpawns (or with it off) lack their
     * innate skills; fix them up before the screen reads the bits */
    innate_grant_party();

    int n = 0;
    values[n++] = 0;                         /* the Arisen, always */
    uint8_t *b = game_pbase();
    if (b) {
        for (int s = 1; s <= 3; s++) {
            const uint8_t *p = b + PARTY_PARAM_BASE + (uint32_t)s * PARTY_PARAM_STRIDE;
            if (present(p)) values[n++] = s;
        }
    }
    values[1] = n > 1 ? values[1] : 1;       /* original always wrote 1 here */
    return n;
}

__attribute__((naked, used))
static void tabs_shim(void)
{
    __asm__ (
        "pushal                        \n"
        "leal   0x30(%esp), %eax       \n"   /* &values = frame ESP+0x10 */
        "pushl  %eax                   \n"
        "call   _build_tabs            \n"
        "addl   $4, %esp               \n"
        "movl   %eax, (%esp)           \n"   /* -> EDI after popal */
        "popal                         \n"
        "jmp    *_rp_st_resume         \n"
    );
}

/* MOV EAX,[0x018DB49C]; ADD EAX,0xA7EC0; MOV [ESP+0x10],0; MOV [ESP+0x14],EDI;
 * JZ +0E; CALL 0x0077DA70; CMP AL,1; JNZ +5; MOV EDI,2; LEA EDX,[ESP+0x10] */
static const uint8_t k_sig[] = {
    0xA1, 0x9C, 0xB4, 0x8D, 0x01, 0x05, 0xC0, 0x7E, 0x0A, 0x00,
    0xC7, 0x44, 0x24, 0x10, 0x00, 0x00, 0x00, 0x00, 0x89, 0x7C,
    0x24, 0x14, 0x74, 0x0E, 0xE8, 0xE4, 0x49, 0x08, 0x00, 0x3C,
    0x01, 0x75, 0x05, 0xBF, 0x02, 0x00, 0x00, 0x00, 0x8D, 0x54,
    0x24, 0x10 };

int skilltabs_install(void)
{
    rp_st_resume = patch_va(VA_RESUME);
    return patch_jmp6("skill tabs (FUN_006f8fd0)", VA_SITE, k_sig, sizeof k_sig,
                      0, (const void*)tabs_shim);
}
