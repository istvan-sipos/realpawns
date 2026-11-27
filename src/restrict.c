#include <windows.h>
#include <stdint.h>
#include "patch.h"
#include "restrict.h"

/* --- game layout (GOG DDDA.exe, read off Ghidra) --------------------------
 *
 * uGUIEquipment, `this`:
 *   +0x26E0  party member being edited; its +0x7F0 points at a record
 *            whose +0x08 is the party slot index (0 Arisen, 1 main pawn,
 *            2/3 support pawns)
 *   +0x2734  item the action menu is open on
 *
 * Item (bag entry):
 *   +0x04  item definition; def +0x58 bit 19 = restricted for support pawns
 *   +0x10  int16 id
 *   +0x12  int16 quantity
 *   +0x20  uint32 flags; 0x600 = upgraded past what support pawns accept
 */
#define GUI_OFF_MEMBER       0x26E0
#define MEMBER_OFF_REC       0x07F0
#define REC_OFF_INDEX        0x08
#define GUI_OFF_ACTION_ITEM  0x2734

#define ITEM_OFF_DEF         0x04
#define DEF_OFF_FLAGS        0x58
#define DEF_RESTRICTED       (1u << 19)
#define ITEM_OFF_ID          0x10
#define ITEM_OFF_QTY         0x12
#define ITEM_OFF_FLAGS       0x20
#define ITEM_UPGRADED        0x600u

static int rd32(const void *p, uint32_t *out)
{
    if (!p || IsBadReadPtr(p, 4)) return 0;
    *out = *(const uint32_t*)p;
    return 1;
}

/* ============================================================================
 * equip: FUN_00645410 ([Equip] enabled?)
 *
 * Every path that survives the earlier checks reaches this block:
 *
 *   006455c5  80 7C 24 1C 00      cmp  byte [esp+1c], 0
 *   006455ca  74 5A               jz   00645626           ; return 0
 *   006455cc  8B 85 34 27 00 00   mov  eax, [ebp+2734]    ; <- patched
 *   006455d2  8B 48 04            mov  ecx, [eax+4]
 *   006455d5  8B 51 58            mov  edx, [ecx+58]
 *   006455d8  C1 EA 13            shr  edx, 13
 *   006455db  F6 C2 01            test dl, 1
 *   006455de  75 29               jnz  00645609           ; restricted type
 *   ...                           upgraded item? party index 2/3? -> 0
 *
 * equip_shim logs, then continues at 006454a6 (`mov al, 1` + epilogue),
 * which the block itself jumps to for unrestricted items, so the stack is
 * already in the shape it expects.  At the patch site EBP = uGUIEquipment*.
 * ============================================================================ */

#define EQUIP_SIG_VA         0x006455C5u
#define EQUIP_PATCH_OFF      7
static const uint8_t k_equip_sig[] = {
    0x80, 0x7C, 0x24, 0x1C, 0x00, 0x74, 0x5A,
    0x8B, 0x85, 0x34, 0x27, 0x00, 0x00,
    0x8B, 0x48, 0x04, 0x8B, 0x51, 0x58, 0xC1, 0xEA, 0x13, 0xF6, 0xC2, 0x01,
    0x75, 0x29,
};

#define EQUIP_RESUME_VA      0x006454A6u
static const uint8_t k_equip_resume_sig[] = {   /* mov al,1; pop x4; ... */
    0xB0, 0x01, 0x5F, 0x5E, 0x5D, 0x5B, 0x83, 0xC4, 0x08, 0xC2, 0x04, 0x00,
};

void *g_restrict_equip_resume = NULL;

/* The button state is polled while the menu is open, so only log when the
 * game's rule would have disabled [Equip], and only once per item/pawn. */
static uint32_t g_last_item = 0;
static int      g_last_idx  = -1;

__attribute__((used))
static void __cdecl on_equip_check(const uint8_t *gui)
{
    uint32_t item = 0, member = 0, rec = 0, idx = 0, def = 0, dflags = 0;
    if (!rd32(gui + GUI_OFF_ACTION_ITEM, &item) || !item) return;
    if (!rd32(gui + GUI_OFF_MEMBER, &member) ||
        !rd32((const uint8_t*)member + MEMBER_OFF_REC, &rec) ||
        !rd32((const uint8_t*)rec + REC_OFF_INDEX, &idx))
        return;
    if (idx != 2 && idx != 3) return;

    const uint8_t *it = (const uint8_t*)item;
    if (IsBadReadPtr(it, ITEM_OFF_FLAGS + 4)) return;
    rd32(it + ITEM_OFF_DEF, &def);
    rd32((const uint8_t*)def + DEF_OFF_FLAGS, &dflags);
    uint32_t iflags = *(const uint32_t*)(it + ITEM_OFF_FLAGS);

    int restricted_type = (dflags & DEF_RESTRICTED) != 0;
    int upgraded        = (iflags & ITEM_UPGRADED) != 0;
    if (!restricted_type && !upgraded) return;   /* game allows it anyway */

    if (item == g_last_item && (int)idx == g_last_idx) return;
    g_last_item = item;
    g_last_idx  = (int)idx;

    patch_logf("equip check: party index %u, item %p id=%d qty=%d "
               "flags=0x%08X def+58=0x%08X (%s%s) -> allowed",
               idx, (void*)it,
               *(const int16_t*)(it + ITEM_OFF_ID),
               *(const int16_t*)(it + ITEM_OFF_QTY),
               iflags, dflags,
               restricted_type ? "restricted type" : "",
               upgraded ? (restricted_type ? ", upgraded" : "upgraded") : "");
}

__attribute__((naked, used))
static void equip_shim(void)
{
    __asm__ (
        "pushal                              \n"
        "pushl  %ebp                         \n"
        "call   _on_equip_check              \n"
        "addl   $4, %esp                     \n"
        "popal                               \n"
        "jmp    *_g_restrict_equip_resume    \n"
    );
}

/* ============================================================================
 * give: FUN_00460ea0(item, from, to) -> count that can be given (0 = none)
 *
 *   00461172  85 DB                   test ebx, ebx           ; to == Arisen?
 *   00461174  74 3E                   jz   004611b4           ; return count
 *   00461176  8B 4C 24 1C             mov  ecx, [esp+1c]      ; recipient
 *   0046117a  83 B9 F4 07 00 00 01    cmp  dword [ecx+7f4], 1 ; main pawn?
 *   00461181  74 31                   jz   004611b4           ; <- JMP
 *   00461183  ...                     restricted/upgraded -> return 0
 *
 * Turning the JZ into a JMP returns the count for every recipient.
 * ============================================================================ */

#define GIVE_SIG_VA          0x00461172u
#define GIVE_PATCH_OFF       15
static const uint8_t k_give_sig[] = {
    0x85, 0xDB, 0x74, 0x3E, 0x8B, 0x4C, 0x24, 0x1C,
    0x83, 0xB9, 0xF4, 0x07, 0x00, 0x00, 0x01,
    0x74, 0x31,
    0x8B, 0x55, 0x04, 0x8B, 0x4A, 0x58, 0xC1, 0xE9, 0x13, 0xF6, 0xC1, 0x01,
    0x75, 0x21,
};
static const uint8_t k_give_patch[] = { 0xEB };

/* ============================================================================
 * withdraw: FUN_0071f580(storage_gui, party_index) -> count party member
 *           party_index can withdraw from storage (0 = name greyed out)
 *
 *   0071f66e  8B 54 01 58             mov  edx, [ecx+eax+58]  ; def flags
 *   0071f672  C1 EA 13 / F6 C2 01     shr edx,13 / test dl,1
 *   0071f678  75 14                   jnz  0071f68e           ; restricted
 *   0071f67a  ...                     upgraded (& 0x600)?  else -> 0071f695
 *   0071f68e  83 7C 24 20 02          cmp  dword [esp+20], 2  ; party index
 *   0071f693  73 6A                   jnc  0071f6ff           ; <- NOP NOP
 *   0071f695  8D 77 08                lea  esi, [edi+8]       ; normal path
 *
 * The storage menu's own copy of the support-pawn rule (it doesn't go
 * through FUN_00460ea0).  NOPing the JNC lets restricted items fall through
 * to the normal weight / stack-size checks.
 * ============================================================================ */

#define WITHDRAW_SIG_VA      0x0071F66Eu
#define WITHDRAW_PATCH_OFF   37
static const uint8_t k_withdraw_sig[] = {
    0x8B, 0x54, 0x01, 0x58, 0xC1, 0xEA, 0x13, 0xF6, 0xC2, 0x01, 0x75, 0x14,
    0x0F, 0xBF, 0x4F, 0x10, 0xC1, 0xE1, 0x07, 0x03, 0xC8, 0x74, 0x10,
    0xF7, 0x47, 0x20, 0x00, 0x06, 0x00, 0x00, 0x74, 0x07,
    0x83, 0x7C, 0x24, 0x20, 0x02,
    0x73, 0x6A,
    0x8D, 0x77, 0x08,
};
static const uint8_t k_withdraw_patch[] = { 0x90, 0x90 };

/* ============================================================================
 * upgrade pages: FUN_006ea420, uGUIShopUpgrade page list (one page per
 * present party member, then storage)
 *
 * The same screen serves the smiths (mode 0) and Barroch (mode 1,
 * gui+0x608).  In mode 1 the support pawns' pages are left out:
 *
 *   006ea4b7  8B 75 08                mov  esi, [ebp+8]       ; gui
 *   006ea4ba  83 BE 08 06 00 00 01    cmp  dword [esi+608], 1 ; Barroch?
 *   006ea4c1  75 17                   jnz  006ea4da           ; <- JMP
 *   006ea4c3  83 FB 02                cmp  ebx, 2             ; slot 2/3:
 *   006ea4c6  0F 84 23 01 00 00       jz   006ea5ef           ;   skip page
 *   006ea4cc  83 FB 03                cmp  ebx, 3
 *   006ea4cf  0F 84 1A 01 00 00       jz   006ea5ef
 *
 * Turning the JNZ into a JMP adds their pages in both modes.  The rest of
 * the screen works by page / slot index, as it does for the smiths.
 * ============================================================================ */

#define PAGES_SIG_VA         0x006EA4B7u
#define PAGES_PATCH_OFF      10
static const uint8_t k_pages_sig[] = {
    0x8B, 0x75, 0x08,
    0x83, 0xBE, 0x08, 0x06, 0x00, 0x00, 0x01,
    0x75, 0x17,
    0x83, 0xFB, 0x02, 0x0F, 0x84, 0x23, 0x01, 0x00, 0x00,
    0x83, 0xFB, 0x03, 0x0F, 0x84, 0x1A, 0x01, 0x00, 0x00,
};
static const uint8_t k_pages_patch[] = { 0xEB };

/* ============================================================================
 * Install
 * ============================================================================ */

int restrict_install(const struct restrict_opts *opts)
{
    int ok = 1;

    if (opts->equip) {
        if (patch_check("gear/equip resume", EQUIP_RESUME_VA,
                        k_equip_resume_sig, sizeof(k_equip_resume_sig))) {
            g_restrict_equip_resume = patch_va(EQUIP_RESUME_VA);
            ok &= patch_jmp6("gear/equip", EQUIP_SIG_VA,
                             k_equip_sig, sizeof(k_equip_sig),
                             EQUIP_PATCH_OFF, (const void*)&equip_shim);
        } else {
            ok = 0;
        }
    } else {
        patch_logf("gear/equip: off");
    }

    if (opts->give) {
        ok &= patch_bytes("gear/give", GIVE_SIG_VA,
                          k_give_sig, sizeof(k_give_sig), GIVE_PATCH_OFF,
                          k_give_patch, sizeof(k_give_patch));
        ok &= patch_bytes("gear/withdraw", WITHDRAW_SIG_VA,
                          k_withdraw_sig, sizeof(k_withdraw_sig),
                          WITHDRAW_PATCH_OFF,
                          k_withdraw_patch, sizeof(k_withdraw_patch));
    } else {
        patch_logf("gear/give: off (give + withdraw)");
    }

    if (opts->upgrade_pages) {
        ok &= patch_bytes("gear/upgrade pages", PAGES_SIG_VA,
                          k_pages_sig, sizeof(k_pages_sig), PAGES_PATCH_OFF,
                          k_pages_patch, sizeof(k_pages_patch));
    } else {
        patch_logf("gear/upgrade pages: off");
    }
    return ok;
}
