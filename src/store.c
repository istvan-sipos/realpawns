#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>
#include "store.h"
#include "game.h"
#include "patch.h"

struct entry {
    struct rp_clone c;
    FILETIME        mtime;
    DWORD           size;
    int             seen;     /* rescan mark */
};

struct pending { uint32_t key, origin; };

static char            g_dir[MAX_PATH];   /* with trailing backslash */
static struct entry   *g_entries;
static int             g_count, g_cap;
static struct pending  g_pending[16];
static int             g_npending;
static uint32_t        g_next = 1;        /* next clone number */
static CRITICAL_SECTION g_lock;
static int             g_lock_ready;

static const char k_header_fmt[] = "<!-- realpawns clone of %08X -->\n";

/* ---- files -------------------------------------------------------------- */

static int read_file(const char *path, char **buf, size_t *len)
{
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    char *b = NULL;
    long n = -1;
    if (fseek(f, 0, SEEK_END) == 0) n = ftell(f);
    if (n > 0 && n < (8L << 20) && fseek(f, 0, SEEK_SET) == 0 &&
        (b = malloc((size_t)n + 1)) && fread(b, 1, (size_t)n, f) == (size_t)n) {
        fclose(f);
        b[n] = 0;
        *buf = b;
        *len = (size_t)n;
        return 0;
    }
    free(b);
    fclose(f);
    return -1;
}

static int write_file(const char *path, const char *a, size_t alen,
                      const char *b, size_t blen)
{
    char tmp[MAX_PATH + 8];
    _snprintf(tmp, sizeof tmp, "%s.tmp", path);
    tmp[sizeof tmp - 1] = 0;
    FILE *f = fopen(tmp, "wb");
    if (!f) return -1;
    int ok = fwrite(a, 1, alen, f) == alen && fwrite(b, 1, blen, f) == blen;
    ok = (fclose(f) == 0) && ok;
    if (!ok || !MoveFileExA(tmp, path, MOVEFILE_REPLACE_EXISTING)) {
        DeleteFileA(tmp);
        return -1;
    }
    return 0;
}

static void clone_path(char *out, size_t n, uint32_t key)
{
    _snprintf(out, n, "%s%08X.xml", g_dir, key);
    out[n - 1] = 0;
}

static int hex8_name(const char *fn, uint32_t *key)
{
    /* exactly "XXXXXXXX.xml" */
    if (strlen(fn) != 12 || _stricmp(fn + 8, ".xml") != 0) return 0;
    uint32_t v = 0;
    for (int i = 0; i < 8; i++) {
        char c = fn[i];
        int d = (c >= '0' && c <= '9') ? c - '0' :
                (c >= 'a' && c <= 'f') ? c - 'a' + 10 :
                (c >= 'A' && c <= 'F') ? c - 'A' + 10 : -1;
        if (d < 0) return 0;
        v = (v << 4) | (uint32_t)d;
    }
    *key = v;
    return 1;
}

/* ---- counter ------------------------------------------------------------ */

static void counter_path(char *out, size_t n)
{
    _snprintf(out, n, "%snext_id.txt", g_dir);
    out[n - 1] = 0;
}

static void counter_read(void)
{
    char path[MAX_PATH + 16];
    counter_path(path, sizeof path);
    FILE *f = fopen(path, "r");
    if (!f) return;
    unsigned long v = 0;
    if (fscanf(f, "%lu", &v) == 1 && v > g_next) g_next = (uint32_t)v;
    fclose(f);
}

static int counter_write(void)
{
    char path[MAX_PATH + 16], txt[32];
    counter_path(path, sizeof path);
    int n = _snprintf(txt, sizeof txt, "%u\n", g_next);
    return write_file(path, txt, (size_t)n, "", 0);
}

/* ---- cache -------------------------------------------------------------- */

static struct entry *find(uint32_t key)
{
    for (int i = 0; i < g_count; i++)
        if (g_entries[i].c.key == key) return &g_entries[i];
    return NULL;
}

static struct entry *slot_for(uint32_t key)
{
    struct entry *e = find(key);
    if (e) return e;
    if (g_count == g_cap) {
        int cap = g_cap ? g_cap * 2 : 64;
        struct entry *n = realloc(g_entries, (size_t)cap * sizeof *n);
        if (!n) return NULL;
        g_entries = n;
        g_cap = cap;
    }
    e = &g_entries[g_count++];
    memset(e, 0, sizeof *e);
    e->c.key = key;
    return e;
}

static void bump_next(uint32_t key)
{
    uint32_t n = (key & RP_CLONE_MAX) + 1;
    if (n > g_next) g_next = n;
}

/* Parse a clone file.  0 on success, else a negative code. */
static int load_clone(const char *path, uint32_t key, struct rp_clone *out)
{
    char *buf;
    size_t len;
    if (read_file(path, &buf, &len) != 0) return -100;
    unsigned origin = 0;
    int rc = 0;
    if (sscanf(buf, "<!-- realpawns clone of %8x -->", &origin) != 1 ||
        !rp_key_vanilla(origin))
        rc = -103;
    if (rc == 0) rc = rp_parse_cmc(buf, len, &out->pawn);
    free(buf);
    if (rc == 0 && out->pawn.key != key) rc = -101;
    if (rc == 0 && (out->pawn.level < 1 || out->pawn.level > 200)) rc = -104;
    out->key = key;
    out->origin = origin;
    return rc;
}

static int scan(int verbose)
{
    for (int i = 0; i < g_count; i++) g_entries[i].seen = 0;

    char pat[MAX_PATH + 16];
    _snprintf(pat, sizeof pat, "%s*.xml", g_dir);
    pat[sizeof pat - 1] = 0;
    int added = 0, updated = 0, legacy = 0;
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pat, &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            uint32_t key;
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            if (!hex8_name(fd.cFileName, &key)) continue;
            if (!rp_key_clone(key)) {
                if (rp_key_vanilla(key) || rp_key_story(key)) legacy++;
                continue;
            }
            bump_next(key);
            struct entry *e = find(key);
            if (e && CompareFileTime(&e->mtime, &fd.ftLastWriteTime) == 0 &&
                e->size == fd.nFileSizeLow) {
                e->seen = 1;
                continue;
            }
            char path[MAX_PATH + 16];
            _snprintf(path, sizeof path, "%s%s", g_dir, fd.cFileName);
            path[sizeof path - 1] = 0;
            struct rp_clone c;
            int rc = load_clone(path, key, &c);
            if (rc != 0) {
                /* keep a previously good copy out of the lists too */
                if (e) e->seen = 0;
                patch_logf("store: skipping %s (error %d)", fd.cFileName, rc);
                continue;
            }
            int is_new = e == NULL;
            e = slot_for(key);
            if (!e) continue;
            e->c = c;
            e->mtime = fd.ftLastWriteTime;
            e->size = fd.nFileSizeLow;
            e->seen = 1;
            if (is_new) added++; else updated++;
            if (verbose >= 2 || (!is_new && verbose))
                patch_logf("store: %08X '%s' lv %u (clone of %08X) %s",
                           key, c.pawn.name, c.pawn.level, c.origin,
                           is_new ? "loaded" : "reloaded");
        } while (FindNextFileA(h, &fd));
        FindClose(h);
    }

    int removed = 0;
    for (int i = 0; i < g_count; ) {
        if (g_entries[i].seen) { i++; continue; }
        if (verbose)
            patch_logf("store: %08X '%s' gone from the folder",
                       g_entries[i].c.key, g_entries[i].c.pawn.name);
        g_entries[i] = g_entries[--g_count];
        removed++;
    }
    if (verbose >= 2 && legacy)
        patch_logf("store: ignoring %d file(s) from realpawns 1.2 or older "
                   "(keys 10.../2...); they can be deleted", legacy);
    return added + updated + removed;
}

/* ---- public ------------------------------------------------------------- */

void store_init(const char *dll_dir)
{
    if (!g_lock_ready) {
        InitializeCriticalSection(&g_lock);
        g_lock_ready = 1;
    }
    _snprintf(g_dir, sizeof g_dir, "%srealpawns\\", dll_dir);
    g_dir[sizeof g_dir - 1] = 0;
    if (!CreateDirectoryA(g_dir, NULL) && GetLastError() != ERROR_ALREADY_EXISTS) {
        patch_logf("store: cannot create %s (err %lu)", g_dir, GetLastError());
        g_dir[0] = 0;
        return;
    }
    counter_read();
    EnterCriticalSection(&g_lock);
    scan(2);
    LeaveCriticalSection(&g_lock);
    patch_logf("store: %d clone(s) in %s, next clone number %u",
               g_count, g_dir, g_next);
}

void store_rescan(void)
{
    if (!g_lock_ready || !g_dir[0]) return;
    EnterCriticalSection(&g_lock);
    scan(1);
    LeaveCriticalSection(&g_lock);
}

const struct rp_clone *store_get(uint32_t key)
{
    if (!g_lock_ready) return NULL;
    EnterCriticalSection(&g_lock);
    struct entry *e = find(key);
    LeaveCriticalSection(&g_lock);
    /* the cache only changes on the game thread, the same thread the rift
     * hooks run on */
    return e ? &e->c : NULL;
}

uint32_t store_origin(uint32_t key)
{
    const struct rp_clone *c = store_get(key);
    if (c) return c->origin;
    for (int i = 0; i < g_npending; i++)
        if (g_pending[i].key == key) return g_pending[i].origin;
    return 0;
}

int store_by_level(int level, uint32_t *keys, int max)
{
    int n = 0;
    if (!g_lock_ready) return 0;
    EnterCriticalSection(&g_lock);
    for (int i = 0; i < g_count && n < max; i++)
        if (g_entries[i].c.pawn.level == level) keys[n++] = g_entries[i].c.key;
    LeaveCriticalSection(&g_lock);
    /* newest first */
    for (int i = 1; i < n; i++)
        for (int j = i; j > 0 && keys[j] > keys[j - 1]; j--) {
            uint32_t t = keys[j]; keys[j] = keys[j - 1]; keys[j - 1] = t;
        }
    return n;
}

uint32_t store_new_clone(uint32_t origin)
{
    if (!g_lock_ready || !g_dir[0]) return 0;
    if (g_next > RP_CLONE_MAX) {
        patch_logf("clone: all %u clone numbers are used up", RP_CLONE_MAX);
        return 0;
    }
    uint32_t key = RP_CLONE_BASE | g_next;
    g_next++;
    if (counter_write() != 0) {
        g_next--;
        patch_logf("clone: cannot write %snext_id.txt", g_dir);
        return 0;
    }
    if (g_npending == (int)(sizeof g_pending / sizeof g_pending[0])) {
        memmove(g_pending, g_pending + 1, sizeof g_pending - sizeof g_pending[0]);
        g_npending--;
    }
    g_pending[g_npending].key = key;
    g_pending[g_npending].origin = origin;
    g_npending++;
    return key;
}

void store_forget(uint32_t key)
{
    for (int i = 0; i < g_npending; i++)
        if (g_pending[i].key == key) {
            g_pending[i] = g_pending[--g_npending];
            return;
        }
}

int store_clones_of(uint32_t origin, uint32_t *keys, int max)
{
    int n = 0;
    if (!g_lock_ready) return 0;
    EnterCriticalSection(&g_lock);
    for (int i = 0; i < g_count && n < max; i++)
        if (g_entries[i].c.origin == origin) keys[n++] = g_entries[i].c.key;
    LeaveCriticalSection(&g_lock);
    return n;
}

int store_count(void)
{
    return g_count;
}

/* Copy of `blk` with mRomPawnUniqueID set to `key`.  Caller frees. */
static char *rekey_block(const char *blk, size_t len, uint32_t key, size_t *out_len)
{
    static const char tag[] = "name=\"mRomPawnUniqueID\" value=\"";
    const char *end = blk + len, *v = NULL;
    for (const char *p = blk; p + sizeof tag - 1 <= end; p++)
        if (*p == 'n' && memcmp(p, tag, sizeof tag - 1) == 0) { v = p + sizeof tag - 1; break; }
    if (!v) return NULL;
    const char *q = memchr(v, '"', (size_t)(end - v));
    if (!q) return NULL;
    char num[16];
    int nl = _snprintf(num, sizeof num, "%u", key);
    size_t n = (size_t)(v - blk) + (size_t)nl + (size_t)(end - q);
    char *o = malloc(n);
    if (!o) return NULL;
    memcpy(o, blk, (size_t)(v - blk));
    memcpy(o + (v - blk), num, (size_t)nl);
    memcpy(o + (v - blk) + nl, q, (size_t)(end - q));
    *out_len = n;
    return o;
}

static void log_capture(int slot, const struct rp_clone *c, const char *what)
{
    static const char *const INCLN[RP_INCL] = {
        "Scather", "Medicant", "Mitigator", "Challenger", "Utilitarian",
        "Guardian", "Nexus", "Pioneer", "Acquisitor" };
    const struct rp_pawn *p = &c->pawn;
    char incl[160] = "";
    int n = 0;
    for (int k = 0; p->has_incl && k < RP_INCL; k++)
        if (p->incl_status[k] && n < (int)sizeof incl - 32)
            n += _snprintf(incl + n, sizeof incl - n, "%s%s %.0f",
                           n ? ", " : "", INCLN[k], p->incl_value[k]);
    incl[sizeof incl - 1] = 0;
    patch_logf("capture: slot %d: '%s' %08X (clone of %08X) lv %u exp %u/%u, "
               "voc %u rank %u, DP %d, inclination %s: %s", slot, p->name,
               c->key, c->origin, p->level, p->exp, p->next_exp, p->job,
               p->job < RP_JOBS ? p->job_level[p->job] : 0, p->job_point,
               !p->has_incl ? "missing" : n ? incl : "none", what);
}

static int capture_slot(int slot, const char *blk, size_t blen)
{
    struct rp_clone c;
    int rc = rp_parse_cmc(blk, blen, &c.pawn);
    if (rc != 0) {
        patch_logf("capture: slot %d: parse error %d, not saved", slot, rc);
        return 0;
    }
    uint32_t key = c.pawn.key;
    char *owned = NULL;
    const char *what = "saved";

    if (rp_key_vanilla(key)) {
        /* hired before clones existed: becomes a clone now.  The live party
         * slot takes the new key, so the next save carries it. */
        uint8_t *b = game_pbase();
        uint32_t *live = b ? (uint32_t*)(b + PARTY_PARAM_BASE +
                         (uint32_t)slot * PARTY_PARAM_STRIDE + PARAM_OFF_KEY) : NULL;
        if (!live || *live != key) {
            patch_logf("capture: slot %d: '%s' %08X: party slot doesn't match, "
                       "not converted", slot, c.pawn.name, key);
            return 0;
        }
        uint32_t nk = store_new_clone(key);
        if (!nk) return 0;
        size_t nlen;
        owned = rekey_block(blk, blen, nk, &nlen);
        if (!owned) {
            store_forget(nk);
            patch_logf("capture: slot %d: cannot rewrite the key", slot);
            return 0;
        }
        *live = nk;
        c.origin = key;
        c.key = c.pawn.key = nk;
        blk = owned;
        blen = nlen;
        what = "converted to a clone and saved";
    } else if (rp_key_clone(key)) {
        c.key = key;
        c.origin = store_origin(key);
        if (!c.origin) {
            patch_logf("capture: slot %d: '%s' %08X: origin unknown (file "
                       "deleted?), not saved", slot, c.pawn.name, key);
            return 0;
        }
    } else {
        patch_logf("capture: slot %d: '%s' %08X is not a rift pawn clone, "
                   "skipped", slot, c.pawn.name, key);
        return 0;
    }
    if (c.pawn.level < 1 || c.pawn.level > 200) {
        patch_logf("capture: slot %d: '%s' %08X: level %u, not saved",
                   slot, c.pawn.name, c.key, c.pawn.level);
        free(owned);
        return 0;
    }

    char path[MAX_PATH + 16], hdr[64];
    clone_path(path, sizeof path, c.key);
    int hl = _snprintf(hdr, sizeof hdr, k_header_fmt, c.origin);
    if (write_file(path, hdr, (size_t)hl, blk, blen) != 0) {
        patch_logf("capture: slot %d: cannot write %s", slot, path);
        free(owned);
        return 0;
    }
    free(owned);

    WIN32_FILE_ATTRIBUTE_DATA fa;
    int have_fa = GetFileAttributesExA(path, GetFileExInfoStandard, &fa);
    EnterCriticalSection(&g_lock);
    struct entry *e = slot_for(c.key);
    if (e) {
        e->c = c;
        if (have_fa) {
            e->mtime = fa.ftLastWriteTime;
            e->size = fa.nFileSizeLow;
        }
        e->seen = 1;
    }
    LeaveCriticalSection(&g_lock);
    store_forget(c.key);
    log_capture(slot, &c, what);
    return 1;
}

int store_capture_save(const void *sav, int len)
{
    if (!g_lock_ready || !g_dir[0]) return 0;
    const uint8_t *b = sav;
    if (!b || len < 32) return 0;
    uint32_t real_size, comp_size;
    memcpy(&real_size, b + 4, 4);
    memcpy(&comp_size, b + 8, 4);
    if (comp_size == 0 || comp_size > (uint32_t)(len - 32) ||
        real_size == 0 || real_size > (64u << 20)) {
        patch_logf("capture: unexpected save header (real=%u comp=%u len=%d)",
                   real_size, comp_size, len);
        return 0;
    }
    char *xml = malloc(real_size);
    if (!xml) return 0;
    uLongf out_len = real_size;
    if (uncompress((Bytef*)xml, &out_len, b + 32, comp_size) != Z_OK ||
        out_len != real_size) {
        patch_logf("capture: inflate failed");
        free(xml);
        return 0;
    }

    struct rp_span blk[2];
    rp_find_hired_blocks(xml, out_len, blk);
    int written = 0;
    for (int i = 0; i < 2; i++) {
        int slot = i + 2;
        if (blk[i].len && game_slot_alive(slot))
            written += capture_slot(slot, blk[i].p, blk[i].len);
    }
    free(xml);
    return written;
}
