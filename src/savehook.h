#ifndef REALPAWNS_SAVEHOOK_H
#define REALPAWNS_SAVEHOOK_H

/* Detour on the save-write state machine (GOG FUN_004b7830, ported from
 * pawnlib-offline's pawnhook).  Calls `on_written(buf, len)` after each
 * successful write of DDDA.sav. */
typedef void (*savehook_fn)(const void *sav, int len);

int savehook_install(savehook_fn on_written);

#endif
