/* pawnxml: strict extraction of a pawn's persisted fields from save XML.
 * Substring search on bounded spans, no DOM; the save format is fixed. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pawnxml.h"

typedef struct { const char *p, *end; } xspan;

static const char *xfind(xspan in, const char *needle)
{
    size_t n = strlen(needle);
    if (n == 0 || (size_t)(in.end - in.p) < n) return NULL;
    const char *last = in.end - n;
    for (const char *p = in.p; p <= last; p++) {
        p = memchr(p, needle[0], (size_t)(last - p) + 1);
        if (!p) return NULL;
        if (memcmp(p, needle, n) == 0) return p;
    }
    return NULL;
}

/* `open` points at a "<class ...>" tag.  Returns a pointer just past the
 * matching "</class>", or NULL.  *body receives the inner span. */
static const char *class_end(const char *open, const char *end, xspan *body)
{
    const char *gt = memchr(open, '>', (size_t)(end - open));
    if (!gt) return NULL;
    const char *q = gt + 1;
    int depth = 1;
    while (q < end) {
        const char *lt = memchr(q, '<', (size_t)(end - q));
        if (!lt) return NULL;
        size_t rem = (size_t)(end - lt);
        if (rem >= 8 && memcmp(lt, "</class>", 8) == 0) {
            if (--depth == 0) {
                if (body) { body->p = gt + 1; body->end = lt; }
                return lt + 8;
            }
            q = lt + 8;
        } else if (rem >= 7 && memcmp(lt, "<class ", 7) == 0) {
            /* a self-closing <class .../> would unbalance this; the save
             * never emits one */
            depth++;
            q = lt + 7;
        } else {
            q = lt + 1;
        }
    }
    return NULL;
}

static int scope_class(xspan in, const char *name, const char *type, xspan *out)
{
    char tag[128];
    int n = snprintf(tag, sizeof tag, "<class name=\"%s\" type=\"%s\">", name, type);
    if (n <= 0 || n >= (int)sizeof tag) return -1;
    const char *s = xfind(in, tag);
    if (!s) return -1;
    return class_end(s, in.end, out) ? 0 : -1;
}

/* ---- numbers (locale-independent) -------------------------------------- */

static int parse_long(const char *s, const char *e, long long *out)
{
    int neg = 0;
    long long v = 0;
    if (s < e && (*s == '-' || *s == '+')) neg = (*s++ == '-');
    if (s >= e) return -1;
    for (; s < e; s++) {
        if (*s < '0' || *s > '9') return -1;
        v = v * 10 + (*s - '0');
        if (v > 0xFFFFFFFFLL) return -1;
    }
    *out = neg ? -v : v;
    return 0;
}

static int parse_float(const char *s, const char *e, float *out)
{
    int neg = 0, digits = 0;
    double v = 0.0, scale = 1.0;
    if (s < e && (*s == '-' || *s == '+')) neg = (*s++ == '-');
    for (; s < e && *s >= '0' && *s <= '9'; s++, digits++) v = v * 10 + (*s - '0');
    if (s < e && *s == '.') {
        for (s++; s < e && *s >= '0' && *s <= '9'; s++, digits++) {
            scale /= 10;
            v += (*s - '0') * scale;
        }
    }
    if (s < e && (*s == 'e' || *s == 'E')) {
        long long x;
        if (parse_long(s + 1, e, &x)) return -1;
        double m = 1.0;
        for (long long i = 0; i < (x < 0 ? -x : x) && i < 64; i++) m *= 10;
        v = x < 0 ? v / m : v * m;
        s = e;
    }
    if (!digits || s != e) return -1;
    *out = (float)(neg ? -v : v);
    return 0;
}

/* ---- scalars and arrays ------------------------------------------------ */

static int get_scalar(xspan in, const char *name, long long *out)
{
    char nd[96];
    int n = snprintf(nd, sizeof nd, "name=\"%s\" value=\"", name);
    if (n <= 0 || n >= (int)sizeof nd) return -1;
    const char *h = xfind(in, nd);
    if (!h) return -1;
    const char *v = h + n;
    const char *q = memchr(v, '"', (size_t)(in.end - v));
    if (!q) return -1;
    return parse_long(v, q, out);
}

static int get_scalar_f(xspan in, const char *name, float *out)
{
    char nd[96];
    int n = snprintf(nd, sizeof nd, "name=\"%s\" value=\"", name);
    if (n <= 0 || n >= (int)sizeof nd) return -1;
    const char *h = xfind(in, nd);
    if (!h) return -1;
    const char *v = h + n;
    const char *q = memchr(v, '"', (size_t)(in.end - v));
    if (!q) return -1;
    return parse_float(v, q, out);
}

enum etype { E_U8, E_S8, E_S16, E_U32, E_S32, E_F32 };

static const char *etype_name(enum etype t)
{
    switch (t) {
    case E_U8:  return "u8";
    case E_S8:  return "s8";
    case E_S16: return "s16";
    case E_U32: return "u32";
    case E_S32: return "s32";
    default:    return "f32";
    }
}

/* Read <array name="NAME" type="T" count="COUNT"> with exactly COUNT
 * <T value="..."/> children.  Returns 0 on success. */
static int get_array(xspan in, const char *name, enum etype t, int count, void *dst)
{
    const char *tn = etype_name(t);
    char op[128];
    int n = snprintf(op, sizeof op, "<array name=\"%s\" type=\"%s\" count=\"%d\">",
                     name, tn, count);
    if (n <= 0 || n >= (int)sizeof op) return -1;
    const char *s = xfind(in, op);
    if (!s) return -1;
    xspan body = { s + n, in.end };
    const char *close = xfind(body, "</array>");
    if (!close) return -1;
    body.end = close;

    char tag[16];
    int tl = snprintf(tag, sizeof tag, "<%s value=\"", tn);
    const char *p = body.p;
    for (int i = 0; i < count; i++) {
        xspan rem = { p, body.end };
        const char *h = xfind(rem, tag);
        if (!h) return -1;
        const char *v = h + tl;
        const char *q = memchr(v, '"', (size_t)(body.end - v));
        if (!q) return -1;
        long long x = 0;
        float f = 0;
        if (t == E_F32 ? parse_float(v, q, &f) : parse_long(v, q, &x)) return -1;
        switch (t) {
        case E_U8:  ((uint8_t*)dst)[i]  = (uint8_t)x;  break;
        case E_S8:  ((int8_t*)dst)[i]   = (int8_t)x;   break;
        case E_S16: ((int16_t*)dst)[i]  = (int16_t)x;  break;
        case E_U32: ((uint32_t*)dst)[i] = (uint32_t)x; break;
        case E_S32: ((int32_t*)dst)[i]  = (int32_t)x;  break;
        case E_F32: ((float*)dst)[i]    = f;           break;
        }
        p = q + 1;
    }
    return 0;
}

static int get_equip(xspan prm, struct rp_gear out[RP_EQUIP])
{
    static const char op[] = "<array name=\"mEquipItem\" type=\"class\" count=\"12\">";
    const char *s = xfind(prm, op);
    if (!s) return -1;
    const char *p = s + sizeof op - 1;
    for (int i = 0; i < RP_EQUIP; i++) {
        xspan rem = { p, prm.end };
        const char *c = xfind(rem, "<class type=\"sItemManager::cITEM_PARAM_DATA\">");
        if (!c) return -1;
        xspan g;
        const char *after = class_end(c, prm.end, &g);
        if (!after) return -1;
        long long no, num, flag;
        if (get_scalar(g, "data.mItemNo", &no) ||
            get_scalar(g, "data.mNum", &num) ||
            get_scalar(g, "data.mFlag", &flag))
            return -1;
        out[i].item_no = (int16_t)no;
        out[i].num     = (int16_t)num;
        out[i].flag    = (uint32_t)flag;
        p = after;
    }
    return 0;
}

/* ---- public ------------------------------------------------------------- */

int rp_parse_cmc(const char *xml, size_t len, struct rp_pawn *out)
{
    memset(out, 0, sizeof *out);
    xspan all = { xml, xml + len };
    const char *root = xfind(all, "<class type=\"cSAVE_DATA_CMC\">");
    if (!root) return -1;
    xspan cmc;
    if (!class_end(root, all.end, &cmc)) return -2;

    long long v;
    if (get_scalar(cmc, "mRomPawnUniqueID", &v)) return -3;
    out->key = (uint32_t)v;

    xspan edit, prm;
    if (scope_class(cmc, "mEdit", "cSAVE_DATA_EDIT", &edit) == 0) {
        uint8_t nm[25];
        if (get_array(edit, "(u8*)mNameStr", E_U8, 25, nm) == 0) {
            memcpy(out->name, nm, 25);
            out->name[25] = 0;
        }
    }
    if (scope_class(cmc, "mParam", "cSAVE_DATA_PARAM", &prm)) return -4;

    if (get_scalar(prm, "mLevel", &v)) return -5;
    out->level = (uint8_t)v;
    if (get_scalar(prm, "mExp", &v)) return -22;
    out->exp = (uint32_t)v;
    if (get_scalar(prm, "mNextExp", &v)) return -23;
    out->next_exp = (uint32_t)v;
    static const char *const STAT[RP_STATS] = {
        "mHp", "mHpMax", "mHpMaxWhite", "mStamina", "mStaminaBase",
        "mStaminaLv", "mBasicAttack", "mBasicDefend", "mBasicMgcAttack",
        "mBasicMgcDefend" };
    for (int i = 0; i < RP_STATS; i++)
        if (get_scalar_f(prm, STAT[i], &out->stats[i])) return -24;
    if (get_scalar(prm, "mJob", &v)) return -6;
    out->job = (uint8_t)v;

    int32_t jp[RP_JOBS];
    if (get_array(prm, "mJobLevel",   E_U8,  RP_JOBS, out->job_level)) return -7;
    if (get_array(prm, "mJobExp",     E_U32, RP_JOBS, out->job_exp))   return -8;
    if (get_array(prm, "mJobNextExp", E_U32, RP_JOBS, out->job_next))  return -9;
    if (get_array(prm, "mJobPoint",   E_S32, RP_JOBS, jp))             return -10;
    out->job_point = jp[0];      /* scalar exposed as 10 equal elements */

    static const char *const WPN[RP_WEAPONS] = {
        "SWORD", "MACE", "GSWORD", "DAGGER", "WAND", "WAND_DX", "HAMMER",
        "SHIELD", "SHIELD_L", "BOW", "BOW_L", "BOW_MG" };
    for (int w = 0; w < RP_WEAPONS; w++) {
        char nm[48];
        snprintf(nm, sizeof nm, "mWeaponSkill[nWeapon::%s]", WPN[w]);
        if (get_array(prm, nm, E_S16, RP_WEAPON_SUB, out->weapon_skill[w])) return -11;
    }
    if (get_array(prm, "mSkillLv1",    E_U32, RP_SKILLLV,     out->skill_lv1))    return -12;
    if (get_array(prm, "mSkillLv2",    E_U32, RP_SKILLLV,     out->skill_lv2))    return -13;
    if (get_array(prm, "mAbility",     E_S8,  RP_ABILITY,     out->ability))      return -14;
    if (get_array(prm, "mAbilityInfo", E_U32, RP_ABILITYINFO, out->ability_info)) return -15;
    if (get_equip(prm, out->equip)) return -16;

    if (get_array(cmc, "mStudyFlag",             E_U32, RP_STUDY,       out->study_flag))       return -17;
    if (get_array(cmc, "mLocalStudyFlag",        E_U32, RP_STUDY,       out->local_study_flag)) return -18;
    if (get_array(cmc, "mStudyData.EncountFrame", E_F32, RP_STUDY_ENEMY, out->encount_frame))   return -19;
    if (get_array(cmc, "mStudyData.KillCnt",     E_U32, RP_STUDY_ENEMY, out->kill_cnt))         return -20;
    if (get_array(cmc, "mStudyData.UniqueCnt",   E_U8,  RP_STUDY_UNIQ,  out->unique_cnt))       return -21;

    /* inclinations: optional, all nine or none */
    static const char *const INCL[RP_INCL] = {
        "BELLIGERENT", "PRUDENT", "POOR_AIM", "STRATEGY", "TACTICS",
        "PROTECTION", "SAME_SUPPORT", "CURIOSITY", "GATHER" };
    int ok = 1;
    for (int i = 0; ok && i < RP_INCL; i++) {
        char nm[64];
        long long t, st;
        snprintf(nm, sizeof nm, "mInfo[ INFO_TYPE_%s ].mInfoType", INCL[i]);
        ok = get_scalar(cmc, nm, &t) == 0;
        snprintf(nm, sizeof nm, "mInfo[ INFO_TYPE_%s ].mValue", INCL[i]);
        ok = ok && get_scalar_f(cmc, nm, &out->incl_value[i]) == 0;
        snprintf(nm, sizeof nm, "mInfo[ INFO_TYPE_%s ].mStatus", INCL[i]);
        ok = ok && get_scalar(cmc, nm, &st) == 0;
        if (ok) {
            out->incl_type[i]   = (uint32_t)t;
            out->incl_status[i] = (uint32_t)st;
        }
    }
    out->has_incl = (uint8_t)ok;
    return 0;
}

int rp_find_hired_blocks(const char *xml, size_t len, struct rp_span out[2])
{
    static const char anchor[] = "<array name=\"mCmc\" type=\"class\" count=\"3\">";
    static const char open[]   = "<class type=\"cSAVE_DATA_CMC\">";
    memset(out, 0, 2 * sizeof *out);
    xspan all = { xml, xml + len };
    const char *a = xfind(all, anchor);
    if (!a) return 0;

    const char *p = a + sizeof anchor - 1;
    int found = 0;
    for (int i = 0; i < 3; i++) {
        /* the three blocks follow each other directly */
        while (p < all.end && (*p == '\n' || *p == '\r' || *p == ' ' || *p == '\t')) p++;
        if ((size_t)(all.end - p) < sizeof open - 1 ||
            memcmp(p, open, sizeof open - 1) != 0)
            break;
        const char *e = class_end(p, all.end, NULL);
        if (!e) break;
        if (i > 0) {
            out[i - 1].p = p;
            out[i - 1].len = (size_t)(e - p);
            found++;
        }
        p = e;
    }
    return found;
}
