#include <windows.h>
#include <stdint.h>
#include "patch.h"
#include "unequip.h"

/* --- game layout (GOG DDDA.exe, read off Ghidra) --------------------------
 *
 * uGUIEquipment (the party equipment pause menu), `this`:
 *   +0x26E0  ptr to the party member being edited; that object's +0x08 is
 *            the party slot index (0 Arisen, 1 main pawn, 2/3 support pawns)
 *   +0x2734  item the current equip/unequip action acts on
 *   +0x2738  item*[12]: items removed during this menu session, by
 *            equipment slot.  Filled for every party member; only acted on
 *            for support pawns, when the menu closes.
 *
 * Item (sItemManager bag entry):
 *   +0x10  int16  item id
 *   +0x12  int16  quantity
 *   +0x20  uint32 flags
 */
#define GUI_OFF_MEMBER       0x26E0
#define GUI_OFF_ACTION_ITEM  0x2734
#define GUI_OFF_REMOVED      0x2738
#define GUI_REMOVED_N        12

#define ITEM_OFF_ID          0x10
#define ITEM_OFF_QTY         0x12
#define ITEM_OFF_FLAGS       0x20

static int read_i32(const void *p, int32_t *out)
{
    if (!p || IsBadReadPtr(p, 4)) return 0;
    *out = *(const int32_t*)p;
    return 1;
}

static void log_item(const char *prefix, int slot, const uint8_t *item)
{
    if (!item || IsBadReadPtr(item, ITEM_OFF_FLAGS + 4)) {
        patch_logf("%s slot=%d item=%p (unreadable)", prefix, slot, (void*)item);
        return;
    }
    patch_logf("%s slot=%d item=%p id=%d qty=%d flags=0x%08X", prefix, slot,
          (void*)item,
          *(const int16_t*)(item + ITEM_OFF_ID),
          *(const int16_t*)(item + ITEM_OFF_QTY),
          *(const uint32_t*)(item + ITEM_OFF_FLAGS));
}

/* ============================================================================
 * send_away: FUN_00647980 state 2 (equipment menu closing)
 *
 *   00647b1c  8B 79 08            mov  edi, [ecx+8]       ; party index
 *   00647b1f  89 7C 24 1C         mov  [esp+1c], edi
 *   00647b23  85 FF               test edi, edi
 *   00647b25  0F 84 09 03 00 00   jz   00647e34           ; <- patched
 *   00647b2b  83 FF 01            cmp  edi, 1
 *   00647b2e  0F 84 00 03 00 00   jz   00647e34
 *   00647b34  ...                 support pawn: delete removed items
 *
 * The JZ becomes a JMP to send_away_shim, which logs and then always
 * continues at 00647e34, the path the Arisen and main pawn take.
 * 00647e34 is `xor esi, esi`, so the flags we clobber don't matter.
 * At the patch site EBP = uGUIEquipment*, EDI = party index.
 * ============================================================================ */

#define SEND_AWAY_SIG_VA     0x00647B1Cu
#define SEND_AWAY_PATCH_OFF  9
#define SEND_AWAY_RESUME_VA  0x00647E34u
static const uint8_t k_send_away_sig[] = {
    0x8B, 0x79, 0x08, 0x89, 0x7C, 0x24, 0x1C, 0x85, 0xFF,
    0x0F, 0x84, 0x09, 0x03, 0x00, 0x00,
    0x83, 0xFF, 0x01, 0x0F, 0x84, 0x00, 0x03, 0x00, 0x00,
};

void *g_unequip_send_away_resume = NULL;

__attribute__((used))
static void __cdecl on_send_away(const uint8_t *gui, int idx)
{
    int n = 0;
    for (int i = 0; i < GUI_REMOVED_N; i++) {
        int32_t p = 0;
        if (read_i32(gui + GUI_OFF_REMOVED + i * 4, &p) && p) n++;
    }
    if (idx < 2 && n == 0) return;   /* nothing interesting to report */

    patch_logf("menu close: party index %d, %d removed item(s)%s",
          idx, n, idx >= 2 ? " -> kept (send-away skipped)" : "");
    for (int i = 0; i < GUI_REMOVED_N; i++) {
        int32_t p = 0;
        if (read_i32(gui + GUI_OFF_REMOVED + i * 4, &p) && p)
            log_item("  removed", i, (const uint8_t*)(uintptr_t)p);
    }
}

__attribute__((naked, used))
static void send_away_shim(void)
{
    __asm__ (
        "pushal                              \n"
        "pushl  %edi                         \n"
        "pushl  %ebp                         \n"
        "call   _on_send_away                \n"
        "addl   $8, %esp                     \n"
        "popal                               \n"
        "jmp    *_g_unequip_send_away_resume \n"
    );
}

/* ============================================================================
 * prompt: FUN_00646e10 state 0 (equip/unequip action)
 *
 *   00646ffc  F7 DE               neg  esi
 *   00646ffe  1B F6               sbb  esi, esi
 *   00647000  83 C6 1E            add  esi, 1e            ; message 0x1e/0x1f
 *   00647003  85 DB               test ebx, ebx           ; party index
 *   00647005  0F 84 81 00 00 00   jz   0064708c           ; <- patched
 *   0064700b  BD 01 00 00 00      mov  ebp, 1
 *   00647010  3B DD               cmp  ebx, ebp
 *   00647012  74 78               jz   0064708c
 *   00647014  ...                 support pawn: open yes/no dialog
 *
 * The JZ becomes a JMP to prompt_shim, which logs and then always continues
 * at 0064708c, where the Arisen and main pawn go straight into the action.
 * 0064708c starts with a CMP, so the flags we clobber don't matter, and that
 * path doesn't use the EBP set at 0064700b.
 * At the patch site EDI = uGUIEquipment*, EBX = party index.
 * ============================================================================ */

#define PROMPT_SIG_VA        0x00646FFCu
#define PROMPT_PATCH_OFF     9
#define PROMPT_RESUME_VA     0x0064708Cu
static const uint8_t k_prompt_sig[] = {
    0xF7, 0xDE, 0x1B, 0xF6, 0x83, 0xC6, 0x1E, 0x85, 0xDB,
    0x0F, 0x84, 0x81, 0x00, 0x00, 0x00,
    0xBD, 0x01, 0x00, 0x00, 0x00, 0x3B, 0xDD, 0x74, 0x78,
};

void *g_unequip_prompt_resume = NULL;

__attribute__((used))
static void __cdecl on_prompt(const uint8_t *gui, int idx)
{
    if (idx < 2) return;
    int32_t p = 0;
    read_i32(gui + GUI_OFF_ACTION_ITEM, &p);
    patch_logf("equip action on party index %d: send-away prompt skipped", idx);
    if (p) log_item("  action", -1, (const uint8_t*)(uintptr_t)p);
}

__attribute__((naked, used))
static void prompt_shim(void)
{
    __asm__ (
        "pushal                              \n"
        "pushl  %ebx                         \n"
        "pushl  %edi                         \n"
        "call   _on_prompt                   \n"
        "addl   $8, %esp                     \n"
        "popal                               \n"
        "jmp    *_g_unequip_prompt_resume    \n"
    );
}

/* ============================================================================
 * Install
 * ============================================================================ */

int unequip_install(const struct unequip_opts *opts)
{
    int ok = 1;

    if (opts->keep_items) {
        g_unequip_send_away_resume = patch_va(SEND_AWAY_RESUME_VA);
        ok &= patch_jmp6("gear/send_away", SEND_AWAY_SIG_VA,
                         k_send_away_sig, sizeof(k_send_away_sig),
                         SEND_AWAY_PATCH_OFF, (const void*)&send_away_shim);
    } else {
        patch_logf("gear/send_away: disabled in realpawns.ini");
    }

    if (opts->skip_prompt) {
        g_unequip_prompt_resume = patch_va(PROMPT_RESUME_VA);
        ok &= patch_jmp6("gear/prompt", PROMPT_SIG_VA,
                         k_prompt_sig, sizeof(k_prompt_sig),
                         PROMPT_PATCH_OFF, (const void*)&prompt_shim);
    } else {
        patch_logf("gear/prompt: disabled in realpawns.ini");
    }
    return ok;
}
