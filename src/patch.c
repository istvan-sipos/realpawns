#include <windows.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include "patch.h"

#define VA_IMAGEBASE 0x00400000u

static patch_logfn g_log = NULL;

void patch_set_log(patch_logfn log) { g_log = log; }

void patch_logf(const char *fmt, ...)
{
    if (!g_log) return;
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    _vsnprintf(buf, sizeof(buf) - 1, fmt, ap);
    va_end(ap);
    buf[sizeof(buf) - 1] = 0;
    g_log(buf);
}

uint8_t *patch_va(uint32_t va)
{
    return (uint8_t*)GetModuleHandleA(NULL) + (va - VA_IMAGEBASE);
}

static void log_bytes(const char *label, const uint8_t *p, size_t n)
{
    char hex[3 * 32 + 1];
    size_t k = 0;
    if (n > 32) n = 32;
    if (IsBadReadPtr(p, n)) {
        patch_logf("  %s @ %p: unreadable", label, (void*)p);
        return;
    }
    for (size_t i = 0; i < n; i++)
        k += (size_t)_snprintf(hex + k, sizeof(hex) - k, "%02X ", p[i]);
    hex[k ? k - 1 : 0] = 0;
    patch_logf("  %s: %s", label, hex);
}

int patch_check(const char *label, uint32_t va, const uint8_t *sig, size_t n)
{
    const uint8_t *p = patch_va(va);
    if (!IsBadReadPtr(p, n) && memcmp(p, sig, n) == 0) return 1;
    patch_logf("%s: signature mismatch at 0x%08X, not patched "
               "(not the GOG build?)", label, va);
    log_bytes("expected", sig, n);
    log_bytes("actual  ", p, n);
    return 0;
}

static int write_code(const char *label, uint8_t *p,
                      const uint8_t *bytes, size_t n)
{
    DWORD oldp = 0;
    if (!VirtualProtect(p, n, PAGE_EXECUTE_READWRITE, &oldp)) {
        patch_logf("%s: VirtualProtect failed (err %lu)", label,
                   GetLastError());
        return 0;
    }
    memcpy(p, bytes, n);
    VirtualProtect(p, n, oldp, &oldp);
    FlushInstructionCache(GetCurrentProcess(), p, n);
    return 1;
}

int patch_jmp6(const char *label, uint32_t sig_va, const uint8_t *sig,
               size_t sig_n, size_t off, const void *shim)
{
    if (!patch_check(label, sig_va, sig, sig_n)) return 0;

    uint8_t *p = patch_va(sig_va) + off;
    uint8_t code[6];
    code[0] = 0xE9;
    *(int32_t*)(code + 1) = (int32_t)((const uint8_t*)shim - (p + 5));
    code[5] = 0x90;
    if (!write_code(label, p, code, sizeof(code))) return 0;

    patch_logf("%s: patched at %p (shim=%p)", label, (void*)p, shim);
    return 1;
}

int patch_bytes(const char *label, uint32_t sig_va, const uint8_t *sig,
                size_t sig_n, size_t off, const uint8_t *bytes, size_t n)
{
    if (!patch_check(label, sig_va, sig, sig_n)) return 0;

    uint8_t *p = patch_va(sig_va) + off;
    if (!write_code(label, p, bytes, n)) return 0;

    patch_logf("%s: patched %u byte(s) at %p", label, (unsigned)n, (void*)p);
    return 1;
}

void *patch_detour(const char *label, uint32_t va, const uint8_t *sig,
                   size_t sig_n, size_t stolen, const void *shim)
{
    if (stolen < 5 || stolen > 16 || stolen > sig_n) {
        patch_logf("%s: bad steal size %u", label, (unsigned)stolen);
        return NULL;
    }
    if (!patch_check(label, va, sig, sig_n)) return NULL;

    uint8_t *fn = patch_va(va);
    uint8_t *t = (uint8_t*)VirtualAlloc(NULL, 32, MEM_COMMIT | MEM_RESERVE,
                                        PAGE_EXECUTE_READWRITE);
    if (!t) {
        patch_logf("%s: trampoline alloc failed", label);
        return NULL;
    }
    memcpy(t, fn, stolen);
    t[stolen] = 0xE9;
    *(int32_t*)(t + stolen + 1) = (int32_t)((fn + stolen) - (t + stolen + 5));
    FlushInstructionCache(GetCurrentProcess(), t, 32);

    uint8_t code[16];
    code[0] = 0xE9;
    *(int32_t*)(code + 1) = (int32_t)((const uint8_t*)shim - (fn + 5));
    for (size_t i = 5; i < stolen; i++) code[i] = 0x90;
    if (!write_code(label, fn, code, stolen)) {
        VirtualFree(t, 0, MEM_RELEASE);
        return NULL;
    }

    patch_logf("%s: detour at %p (tramp=%p shim=%p)", label, (void*)fn,
               (void*)t, shim);
    return t;
}
