#ifndef REALPAWNS_STORE_H
#define REALPAWNS_STORE_H

#include <stdint.h>
#include "pawnxml.h"

/* store: one file per clone, <dll_dir>realpawns\<KEY>.xml, KEY = 1200nnnn.
 *
 * A clone is made when an official rift pawn (0x10LLIIII) is hired: it
 * gets its own key, so the vanilla pawn stays as it is.  The file is the
 * clone's cSAVE_DATA_CMC block from the last save that had it hired,
 * preceded by a header line naming its origin (the vanilla key it was
 * cloned from):
 *
 *     <!-- realpawns clone of 10B302C4 -->
 *
 * The files are cached in memory; store_rescan() resyncs the cache with
 * the folder (new, changed and deleted files) and runs before every rift
 * list is built.  realpawns\next_id.txt holds the next clone number;
 * numbers are never reused. */

struct rp_clone {
    uint32_t       key;       /* 0x1200nnnn */
    uint32_t       origin;    /* 0x10LLIIII */
    struct rp_pawn pawn;
};

/* Create the folder if needed, read the counter and load every clone. */
void store_init(const char *dll_dir);

/* Resync the cache with the folder. */
void store_rescan(void);

/* Cached clone `key`, or NULL.  Valid until the next rescan or capture. */
const struct rp_clone *store_get(uint32_t key);

/* Origin of clone `key`: from its file, or from a hire not saved yet.
 * 0 if unknown. */
uint32_t store_origin(uint32_t key);

/* Keys of the clones saved at `level`, newest first.  Returns the count. */
int store_by_level(int level, uint32_t *keys, int max);

/* Allocate a clone key for a hire of `origin` and remember the origin
 * until the first capture.  Returns 0 when the numbers are used up or the
 * counter can't be written. */
uint32_t store_new_clone(uint32_t origin);

/* Drop the remembered origin of a hire that didn't happen. */
void store_forget(uint32_t key);

/* Keys of the clones made from vanilla key `origin`.  Returns the count. */
int store_clones_of(uint32_t origin, uint32_t *keys, int max);

int store_count(void);

/* Save-write hook entry: `sav` is the raw DDDA.sav buffer (32-byte header
 * + zlib).  Writes the file of each hired clone; a hired vanilla pawn
 * (hired before clones existed) becomes a clone here.  Returns the number
 * of files written. */
int store_capture_save(const void *sav, int len);

#endif
