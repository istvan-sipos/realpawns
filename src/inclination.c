/* inclination: use inclination elixirs from any pawn's inventory.
 *
 * Vanilla: the item menu disables Use for every item in a pawn's
 * inventory, and the elixir effect always lands on the main pawn.  The
 * Use path itself (FUN_0066b230 -> FUN_00b747f0) is already generic over
 * the bag owner: it runs the item effect on the owner's actor and removes
 * the item from the owner's bag.
 *
 *   M  0x00460CB0  FUN_00460cb0 (can-use check, EAX = item, arg = bag
 *                  owner): for elixirs in a pawn's bag, answer from our
 *                  own check on that pawn instead of "not your bag" (5).
 *   E  0x00B7504F  FUN_00b74e40 (item effect on an actor, EBX = actor,
 *                  EBP = item): the elixir block is replaced.  Elixirs
 *                  from a support pawn's bag act on that pawn; everything
 *                  else keeps the vanilla main-pawn behaviour.
 *
 * Every inclination mutator returns early unless the pawn is the main
 * pawn (param+0x7F4 == 1) and the controller is enabled (ctrl+8 == 0).
 * For a support pawn both are set for the duration of the one call into
 * the game's own elixir routine FUN_0057ef30 and then restored, so support
 * pawns still never drift on their own.
 *
 * See realpawns.md, "Inclination elixirs on any pawn". */

#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include "inclination.h"
#include "game.h"
#include "patch.h"

#define VA_ACTORS_PTR  0x018DB4CCu
#define VA_CAN_MAIN    0x00441A00u   /* stdcall(itemNo) -> AL           */
#define VA_APPLY       0x0057EF30u   /* EAX = ctrl; push itemNo; ret 4  */
#define VA_EFFECT      0x00B74E40u   /* actor vtable +0x2DC             */

#define ITEM_FIRST     1520          /* Scather's Elixir                 */
#define ITEM_LAST      1529          /* Neutralizing Elixir              */

#define A_LIVE         0x1D0Cu       /* u32, > 0 while active            */
#define A_HP           0x1D18u       /* -> -> +8 f32 HP                  */
#define A_AI           0x2E64u       /* AI object X; ctrl = X + 0x88     */
#define A_PARAM        0x3DECu       /* party param                      */
#define X_PAWN         0x2ACu        /* u8: X belongs to a pawn          */
#define C_OFF          0x88u
#define C_DISABLED     0x08u         /* u8                               */
#define C_ENTRY0       0x10u         /* -> param mInfo[0]                */
#define P_MAINPAWN     0x7F4u        /* u32: 1 = the player's own pawn   */
#define P_GONE         0x354u
#define P_INFO         0x14BCu       /* {u32 type, f32 value, u32 status}[9] */
#define PB_PROGRESS    0x30u         /* everything gated below 9000      */

void *rp_in_fn_can_main, *rp_in_fn_apply;
void *rp_in_res_m, *rp_in_res_e;

static const char *const k_names[9] = {
    "Scather", "Medicant", "Mitigator", "Challenger", "Utilitarian",
    "Guardian", "Nexus", "Pioneer", "Acquisitor" };

static const char *item_name(int no)
{
    return no == ITEM_LAST ? "Neutralizing" : k_names[no - ITEM_FIRST];
}

/* ---- game calls ------------------------------------------------------- */

__attribute__((naked, used))
static int __cdecl call_can_main(int item_no)
{
    __asm__ (
        "pushl  4(%esp)                \n"
        "call   *_rp_in_fn_can_main    \n"   /* ret 4 */
        "movzbl %al, %eax              \n"
        "ret                           \n"
    );
}

__attribute__((naked, used))
static void __cdecl call_apply(uint8_t *ctrl, int item_no)
{
    __asm__ (
        "pushl  %ebx                   \n"
        "pushl  %esi                   \n"
        "pushl  %edi                   \n"
        "pushl  %ebp                   \n"
        "movl   20(%esp), %eax         \n"   /* ctrl */
        "pushl  24(%esp)               \n"   /* item_no */
        "call   *_rp_in_fn_apply       \n"   /* ret 4 */
        "popl   %ebp                   \n"
        "popl   %edi                   \n"
        "popl   %esi                   \n"
        "popl   %ebx                   \n"
        "ret                           \n"
    );
}

/* ---- pawn state ------------------------------------------------------- */

static uint8_t *party_actor(int slot)
{
    uint8_t *act = *(uint8_t**)patch_va(VA_ACTORS_PTR);
    if (!act || slot < 1 || slot > 3) return NULL;
    return *(uint8_t**)(act + 0x9A0 + (uint32_t)(slot - 1) * 4);
}

static uint8_t *slot_param(int slot)
{
    uint8_t *b = game_pbase();
    return b ? b + PARTY_PARAM_BASE + (uint32_t)slot * PARTY_PARAM_STRIDE : NULL;
}

/* Why a support pawn can't take an elixir right now, or NULL if it can.
 * The same conditions FUN_00441a00 + FUN_0057f160 test for the main pawn,
 * minus the main-pawn-only flags (param+0x7F4, ctrl+8). */
static const char *support_refusal(int slot, uint8_t **ctrl_out, uint8_t **param_out)
{
    uint8_t *b = game_pbase(), *p = slot_param(slot), *a = party_actor(slot);
    if (!b || !p) return "no save loaded";
    if (!p[PARAM_OFF_ALIVE] || p[P_GONE]) return "not in the party";
    if (!a) return "no actor";
    if (*(uint32_t*)(a + A_LIVE) == 0) return "actor not active";
    uint8_t **hp = *(uint8_t***)(a + A_HP);
    if (!hp || !*hp || !(*(float*)(*hp + 8) > 0.0f)) return "down";
    uint8_t *x = *(uint8_t**)(a + A_AI);
    if (!x) return "no AI object";
    uint8_t *c = x + C_OFF;
    if (*(uint8_t**)(c + 4) != x || *(uint8_t**)(x + 4) != a) return "AI not bound";
    if (!x[X_PAWN]) return "AI is not a pawn's";
    if (*(uint8_t**)(a + A_PARAM) != p) return "actor param is not the party param";
    if (*(uint8_t**)(c + C_ENTRY0) != p + P_INFO) return "inclinations not bound";
    if (*(int32_t*)(b + PB_PROGRESS) >= 9000) return "story progress";
    void **vt = *(void***)a;
    if (!vt || vt[0x2DC / 4] != (void*)patch_va(VA_EFFECT)) return "unknown actor class";
    if (ctrl_out) *ctrl_out = c;
    if (param_out) *param_out = p;
    return NULL;
}

/* Held items (lantern & co) take another path in FUN_00b747f0. */
static int held_item(const uint8_t *item)
{
    const uint8_t *def = *(uint8_t* const*)(item + 4);
    return !def || ((*(const uint32_t*)(def + 0x58) >> 13) & 1);
}

/* ---- M: can-use check ------------------------------------------------- */

/* Last answer logged per bag owner, so the menu redraw doesn't spam. */
static int g_last_item[4], g_last_rc[4] = { -1, -1, -1, -1 };

__attribute__((used))
static int __cdecl inc_usable(uint8_t *item, int owner)
{
    int no = *(int16_t*)(item + 0x10), rc;
    const char *why = NULL;
    if (held_item(item)) {
        rc = 1;
        why = "held-item path";
    } else if (!*(uint8_t**)patch_va(VA_ACTORS_PTR) ||
               !*(void**)(*(uint8_t**)patch_va(VA_ACTORS_PTR) + 0x99C)) {
        rc = 5;
        why = "no Arisen";
    } else if ((*(uint32_t*)(item + 0x20) >> 8) & 1) {
        rc = 2;
        why = "item busy";
    } else if (owner == 1) {
        rc = call_can_main(no) ? 0 : 4;
        if (rc) why = "main pawn can't take it now";
    } else {
        why = support_refusal(owner, NULL, NULL);
        rc = why ? 4 : 0;
    }
    if (g_last_item[owner] != no || g_last_rc[owner] != rc) {
        const uint8_t *def = *(uint8_t* const*)(item + 4);
        patch_logf("inclination: slot %d: %s Elixir %s%s%s (flags %08X/%08X)",
                   owner, item_name(no), rc ? "not usable" : "usable",
                   why ? ": " : "", why ? why : "",
                   def ? *(const uint32_t*)(def + 0x38) : 0,
                   def ? *(const uint32_t*)(def + 0x58) : 0);
        g_last_item[owner] = no;
        g_last_rc[owner] = rc;
    }
    return rc;
}

/* Entry of FUN_00460cb0, replacing CMP dword [ESP+4],0 (5 bytes). */
__attribute__((naked, used)) static void m_shim(void)
{
    __asm__ (
        "movl   4(%esp), %ecx          \n"   /* bag owner */
        "decl   %ecx                   \n"
        "cmpl   $2, %ecx               \n"   /* owner 1..3 */
        "ja     1f                     \n"
        "movswl 0x10(%eax), %edx       \n"
        "subl   $1520, %edx            \n"
        "cmpl   $9, %edx               \n"
        "ja     1f                     \n"
        "incl   %ecx                   \n"
        "pushl  %ecx                   \n"
        "pushl  %eax                   \n"
        "call   _inc_usable            \n"
        "addl   $8, %esp               \n"
        "ret    $4                     \n"
        "1:                            \n"
        "cmpl   $0, 4(%esp)            \n"   /* the replaced instruction */
        "jmp    *_rp_in_res_m          \n"
    );
}

/* ---- E: elixir effect ------------------------------------------------- */

static void log_change(int slot, int no, const float before[9],
                       const uint32_t sbefore[9], const uint8_t *param)
{
    char buf[512];
    int n = 0;
    for (int i = 0; i < 9 && n < (int)sizeof buf - 64; i++) {
        float v = *(const float*)(param + P_INFO + i * 12 + 4);
        uint32_t s = *(const uint32_t*)(param + P_INFO + i * 12 + 8);
        if (v != before[i] || s != sbefore[i])
            n += _snprintf(buf + n, sizeof buf - n, " %s %.0f->%.0f (%u->%u)",
                           k_names[i], before[i], v, sbefore[i], s);
    }
    buf[n] = 0;
    patch_logf("inclination: slot %d: %s Elixir:%s", slot, item_name(no),
               n ? buf : " no change");
}

static void apply_support(int slot, int no)
{
    uint8_t *ctrl, *p;
    const char *why = support_refusal(slot, &ctrl, &p);
    if (why) {
        patch_logf("inclination: slot %d: %s Elixir NOT applied: %s",
                   slot, item_name(no), why);
        return;
    }
    float before[9];
    uint32_t sbefore[9];
    for (int i = 0; i < 9; i++) {
        before[i]  = *(float*)(p + P_INFO + i * 12 + 4);
        sbefore[i] = *(uint32_t*)(p + P_INFO + i * 12 + 8);
    }
    uint32_t main_flag = *(uint32_t*)(p + P_MAINPAWN);
    uint8_t disabled = ctrl[C_DISABLED];
    *(uint32_t*)(p + P_MAINPAWN) = 1;
    ctrl[C_DISABLED] = 0;
    call_apply(ctrl, no);
    *(uint32_t*)(p + P_MAINPAWN) = main_flag;
    ctrl[C_DISABLED] = disabled;
    log_change(slot, no, before, sbefore, p);
}

__attribute__((used))
static void __cdecl inc_effect(uint8_t *actor, uint8_t *item)
{
    int no = *(int16_t*)(item + 0x10);
    if (no < ITEM_FIRST || no > ITEM_LAST) return;  /* vanilla: no-op too */
    int owner = *(int8_t*)(item + 0x14);
    if (owner == 2 || owner == 3) {
        /* from a support pawn's bag: only that pawn, once */
        if (actor == party_actor(owner)) apply_support(owner, no);
        return;
    }
    /* from the main pawn's bag (not possible in vanilla): once, on it */
    if (owner == 1 && actor != party_actor(1)) return;
    /* vanilla: the main pawn */
    if (!call_can_main(no)) return;
    uint8_t *main = party_actor(1);
    if (!main) return;
    uint8_t *p = slot_param(1);
    float before[9];
    uint32_t sbefore[9];
    for (int i = 0; p && i < 9; i++) {
        before[i]  = *(float*)(p + P_INFO + i * 12 + 4);
        sbefore[i] = *(uint32_t*)(p + P_INFO + i * 12 + 8);
    }
    call_apply(*(uint8_t**)(main + A_AI) + C_OFF, no);
    if (p) log_change(1, no, before, sbefore, p);
}

/* Replaces 0x00B7504F..0x00B75086 (MOVSX EDX,[EBP+0x10] ... CALL
 * FUN_0057ef30) and resumes at 0x00B75086. */
__attribute__((naked, used)) static void e_shim(void)
{
    __asm__ (
        "pushal                        \n"
        "pushl  %ebp                   \n"   /* item */
        "pushl  %ebx                   \n"   /* actor */
        "call   _inc_effect            \n"
        "addl   $8, %esp               \n"
        "popal                         \n"
        "jmp    *_rp_in_res_e          \n"
    );
}

/* ---- install ---------------------------------------------------------- */

int inclination_install(void)
{
    rp_in_fn_can_main = patch_va(VA_CAN_MAIN);
    rp_in_fn_apply    = patch_va(VA_APPLY);
    rp_in_res_m       = patch_va(0x00460CB5u);
    rp_in_res_e       = patch_va(0x00B75086u);

    /* CMP [ESP+4],0x5F9; JA */
    static const uint8_t k_can_main[] = { 0x81, 0x7C, 0x24, 0x04, 0xF9, 0x05, 0x00, 0x00, 0x77, 0x49 };
    /* PUSH ESI; MOV ESI,EAX; CMP byte [ESI+8],0; JNZ */
    static const uint8_t k_apply[] = { 0x56, 0x8B, 0xF0, 0x80, 0x7E, 0x08, 0x00, 0x0F, 0x85 };
    /* SUB ESP,0x14; PUSH EBX; PUSH EBP; MOV EBP,[ESP+0x20]; MOV EBX,ECX */
    static const uint8_t k_effect[] = { 0x83, 0xEC, 0x14, 0x53, 0x55, 0x8B, 0x6C, 0x24, 0x20, 0x8B, 0xD9 };
    /* CMP [ESP+4],0; PUSH ESI; MOV ESI,EAX; JZ +9; MOV EAX,5; POP ESI; RET 4 */
    static const uint8_t k_m[] = {
        0x83, 0x7C, 0x24, 0x04, 0x00, 0x56, 0x8B, 0xF0, 0x74, 0x09,
        0xB8, 0x05, 0x00, 0x00, 0x00, 0x5E, 0xC2, 0x04, 0x00 };
    /* MOVSX EDX,[EBP+0x10]; CMP EDX,0x5F0; JC; PUSH EDX; CALL FUN_00441a00;
     * TEST AL,AL; JZ; MOV ECX,[0x018DB4CC]; MOV EAX,[ECX+0x9A0]; TEST; JZ;
     * MOV EAX,[EAX+0x2E64]; PUSH EDX; ADD EAX,0x88; CALL FUN_0057ef30;
     * MOVSX ECX,[EBP+0x10] */
    static const uint8_t k_e[] = {
        0x0F, 0xBF, 0x55, 0x10, 0x81, 0xFA, 0xF0, 0x05, 0x00, 0x00, 0x72, 0x2B,
        0x52, 0xE8, 0x9F, 0xC9, 0x8C, 0xFF, 0x84, 0xC0, 0x74, 0x21,
        0x8B, 0x0D, 0xCC, 0xB4, 0x8D, 0x01, 0x8B, 0x81, 0xA0, 0x09, 0x00, 0x00,
        0x85, 0xC0, 0x74, 0x11, 0x8B, 0x80, 0x64, 0x2E, 0x00, 0x00, 0x52,
        0x05, 0x88, 0x00, 0x00, 0x00, 0xE8, 0xAA, 0x9E, 0xA0, 0xFF,
        0x0F, 0xBF, 0x4D, 0x10 };

    if (!patch_check("inclination: can-use main pawn (0x00441A00)", VA_CAN_MAIN,
                     k_can_main, sizeof k_can_main) ||
        !patch_check("inclination: apply (0x0057EF30)", VA_APPLY,
                     k_apply, sizeof k_apply) ||
        !patch_check("inclination: item effect (0x00B74E40)", VA_EFFECT,
                     k_effect, sizeof k_effect) ||
        !patch_check("inclination M (0x00460CB0)", 0x00460CB0u, k_m, sizeof k_m) ||
        !patch_check("inclination E (0x00B7504F)", 0x00B7504Fu, k_e, sizeof k_e))
        return 0;

    /* E first: on its own it only changes what an elixir from a support
     * bag does, and vanilla never uses one from there. */
    uint8_t e[10] = { 0xE9 }, m[5] = { 0xE9 };
    *(int32_t*)(e + 1) = (int32_t)((uint8_t*)e_shim - (patch_va(0x00B7504Fu) + 5));
    for (int i = 5; i < 10; i++) e[i] = 0x90;
    *(int32_t*)(m + 1) = (int32_t)((uint8_t*)m_shim - (patch_va(0x00460CB0u) + 5));
    return patch_bytes("inclination E (0x00B7504F)", 0x00B7504Fu, k_e, sizeof k_e,
                       0, e, sizeof e) &&
           patch_bytes("inclination M (0x00460CB0)", 0x00460CB0u, k_m, sizeof k_m,
                       0, m, sizeof m);
}
