#ifndef REALPAWNS_PAWNXML_H
#define REALPAWNS_PAWNXML_H

#include <stddef.h>
#include <stdint.h>

/* pawnxml: read a pawn's progress out of save XML.
 *
 * The input is one <class type="cSAVE_DATA_CMC"> block, as found in the
 * inflated DDDA.sav (and as stored verbatim in realpawns/<key>.xml).
 * Only the fields realpawns persists are extracted.  The parse is
 * strict: every array must be present with its full element count, so a
 * malformed or truncated file is rejected instead of overlaying zeros. */

#define RP_JOBS        10
#define RP_WEAPONS     12     /* the record holds 12 weapon rows       */
#define RP_WEAPON_SUB  6
#define RP_SKILLLV     14
#define RP_ABILITY     6
#define RP_ABILITYINFO 4
#define RP_EQUIP       12
#define RP_STUDY       322
#define RP_STUDY_ENEMY 72
#define RP_STUDY_UNIQ  116
#define RP_INCL        9      /* inclinations, mInfo[ INFO_TYPE_* ]    */

struct rp_gear {
    int16_t  item_no;         /* -1 = empty */
    int16_t  num;
    uint32_t flag;            /* includes the upgrade bits */
};

struct rp_pawn {
    uint32_t key;             /* mRomPawnUniqueID */
    char     name[26];        /* for logging only */
    uint8_t  level;           /* for sanity checks only; never applied */

    uint8_t  job;
    uint8_t  job_level[RP_JOBS];
    uint32_t job_exp[RP_JOBS];
    uint32_t job_next[RP_JOBS];
    int32_t  job_point;       /* Discipline pool */

    int16_t  weapon_skill[RP_WEAPONS][RP_WEAPON_SUB];
    int8_t   ability[RP_ABILITY];
    uint32_t skill_lv1[RP_SKILLLV];
    uint32_t skill_lv2[RP_SKILLLV];
    uint32_t ability_info[RP_ABILITYINFO];

    struct rp_gear equip[RP_EQUIP];

    uint32_t study_flag[RP_STUDY];
    uint32_t local_study_flag[RP_STUDY];
    float    encount_frame[RP_STUDY_ENEMY];
    uint32_t kill_cnt[RP_STUDY_ENEMY];
    uint8_t  unique_cnt[RP_STUDY_UNIQ];

    /* optional: has_incl = 0 when the block lacks mInfo */
    uint8_t  has_incl;
    uint32_t incl_type[RP_INCL];
    float    incl_value[RP_INCL];
    uint32_t incl_status[RP_INCL];  /* 2 dominant, 1 active, 0 inactive */
};

/* Is `key` an official offline pawn key realpawns handles?
 * 0x10LLIIII (generated) or 0x2xxxxxxx (story).  pawnlib's 0x11xxxxxx
 * family and the 0xFFFFFFFF sentinel are excluded. */
int rp_key_supported(uint32_t key);

/* Parse one cSAVE_DATA_CMC block.  Returns 0 on success, or a negative
 * code naming the first missing piece (see pawnxml.c). */
int rp_parse_cmc(const char *xml, size_t len, struct rp_pawn *out);

/* A byte range inside the inflated save XML. */
struct rp_span { const char *p; size_t len; };

/* Locate the hired-pawn blocks mCmc[1] and mCmc[2] (party slots 2 and 3)
 * of the first <array name="mCmc" ...> in the inflated save.
 * out[0] = slot 2, out[1] = slot 3; len = 0 when not found.
 * Returns the number found. */
int rp_find_hired_blocks(const char *xml, size_t len, struct rp_span out[2]);

#endif
