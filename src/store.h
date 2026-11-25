#ifndef REALPAWNS_STORE_H
#define REALPAWNS_STORE_H

#include <stdint.h>
#include "pawnxml.h"

/* store: the per-pawn progress files, <dll_dir>realpawns\<KEY>.xml.
 *
 * Each file is the pawn's cSAVE_DATA_CMC block copied verbatim from the
 * last save that had the pawn hired.  All files are parsed at startup
 * and kept in memory; lookups never touch the disk. */

/* Create the folder if needed and load every <8 hex>.xml in it. */
void store_init(const char *dll_dir);

/* Parsed progress for `key`, or NULL. */
const struct rp_pawn *store_get(uint32_t key);

int store_count(void);

/* Save-write hook entry: `sav` is the raw DDDA.sav buffer (32-byte header
 * + zlib).  Captures each occupied hired slot that carries an official
 * key.  Returns the number of pawns written. */
int store_capture_save(const void *sav, int len);

#endif
