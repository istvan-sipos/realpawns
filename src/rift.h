#ifndef REALPAWNS_RIFT_H
#define REALPAWNS_RIFT_H

/* Install the three rift record detours.  Returns 1 if all installed. */
int rift_install(void);

/* Re-apply stored progress to every matching record of the in-memory rift
 * pool (pBase+0x16E0).  Returns the number of records changed. */
int rift_overlay_pool(void);

/* 0 disables the innate-skill grant on rift records (ini). */
extern int rp_innate_enabled;

#endif
