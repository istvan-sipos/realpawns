#include <windows.h>
#include <stdint.h>
#include <string.h>
#include "savehook.h"
#include "patch.h"

/* FUN_004b7830 is entered with the save context in ESI and no stack
 * arguments; it returns its progress byte in AL.  Its first instruction,
 * MOVZX EAX, byte [ESI+0x64A], is 7 relocation-free bytes. */
#define VA_SAVE_WRITE    0x004B7830u
#define CTX_OFF_BUFFER   0x568   /* void *pvData                       */
#define CTX_OFF_SIZE     0x824   /* int   cubData                      */
#define CTX_OFF_SUBSTATE 0x64A   /* 3 = leaf I/O pending, 4 = success  */
#define CTX_OFF_FILENAME 0x9E6   /* char[] "DDDA.sav"                  */

static const uint8_t k_sig[] = { 0x0F, 0xB6, 0x86, 0x4A, 0x06, 0x00, 0x00 };

static savehook_fn g_cb;
void   *rp_save_tramp;
uint8_t rp_save_pre;
uint32_t rp_save_ret;

__attribute__((used))
static void __cdecl save_write_post(uint8_t *ctx)
{
    if (rp_save_pre != 3 || ctx[CTX_OFF_SUBSTATE] != 4) return;
    if (strcmp((const char*)ctx + CTX_OFF_FILENAME, "DDDA.sav") != 0) return;
    const void *buf = *(const void**)(ctx + CTX_OFF_BUFFER);
    int len = *(int*)(ctx + CTX_OFF_SIZE);
    if (!buf || len < 32) return;
    patch_logf("save: DDDA.sav written (%d bytes)", len);
    if (g_cb) g_cb(buf, len);
}

__attribute__((naked, used))
static void save_write_shim(void)
{
    __asm__ (
        "movzbl 0x64a(%esi), %eax      \n"
        "movb   %al, _rp_save_pre      \n"
        "call   *_rp_save_tramp        \n"
        "movzbl %al, %eax              \n"
        "movl   %eax, _rp_save_ret     \n"
        "pushl  %esi                   \n"
        "call   _save_write_post       \n"
        "addl   $4, %esp               \n"
        "movl   _rp_save_ret, %eax     \n"
        "ret                           \n"
    );
}

int savehook_install(savehook_fn on_written)
{
    g_cb = on_written;
    rp_save_tramp = patch_detour("save-write", VA_SAVE_WRITE, k_sig,
                                 sizeof k_sig, 7, (const void*)save_write_shim);
    return rp_save_tramp != NULL;
}
