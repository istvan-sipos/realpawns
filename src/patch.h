#ifndef REALPAWNS_PATCH_H
#define REALPAWNS_PATCH_H

#include <stddef.h>
#include <stdint.h>

/* patch — signature-checked code patches on DDDA.exe.
 *
 * All VAs are static addresses at the preferred image base 0x00400000
 * (GOG build).  Every patch first compares a signature block around the
 * site byte for byte and refuses to write anything on a mismatch, logging
 * the expected and actual bytes. */

typedef void (*patch_logfn)(const char *msg);

void patch_set_log(patch_logfn log);
void patch_logf(const char *fmt, ...);

/* Runtime address of a static VA. */
uint8_t *patch_va(uint32_t va);

/* Does memory at `va` match `sig`?  Logs both on a mismatch. */
int patch_check(const char *label, uint32_t va, const uint8_t *sig, size_t n);

/* Check `sig` at `sig_va`, then overwrite the 6-byte instruction at
 * sig_va+off with `JMP shim` + NOP. */
int patch_jmp6(const char *label, uint32_t sig_va, const uint8_t *sig,
               size_t sig_n, size_t off, const void *shim);

/* Check `sig` at `sig_va`, then write `n` bytes at sig_va+off. */
int patch_bytes(const char *label, uint32_t sig_va, const uint8_t *sig,
                size_t sig_n, size_t off, const uint8_t *bytes, size_t n);

/* Check `sig` at `va`, copy the first `stolen` bytes (which must be
 * relocation-free; the signature guarantees what they are) into a fresh
 * executable trampoline followed by `JMP va+stolen`, then overwrite the
 * site with `JMP shim` (+ NOPs).  Returns the trampoline, or NULL. */
void *patch_detour(const char *label, uint32_t va, const uint8_t *sig,
                   size_t sig_n, size_t stolen, const void *shim);

#endif
