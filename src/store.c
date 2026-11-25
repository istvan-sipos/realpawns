#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>
#include "store.h"
#include "game.h"
#include "patch.h"

struct entry {
    uint32_t       key;
    struct rp_pawn pawn;
};

static char           g_dir[MAX_PATH];   /* with trailing backslash */
static struct entry  *g_entries;
static int            g_count, g_cap;
static CRITICAL_SECTION g_lock;
static int            g_lock_ready;

static struct entry *find(uint32_t key)
{
    for (int i = 0; i < g_count; i++)
        if (g_entries[i].key == key) return &g_entries[i];
    return NULL;
}

static void put(const struct rp_pawn *p)
{
    struct entry *e = find(p->key);
    if (!e) {
        if (g_count == g_cap) {
            int cap = g_cap ? g_cap * 2 : 64;
            struct entry *n = realloc(g_entries, (size_t)cap * sizeof *n);
            if (!n) return;
            g_entries = n;
            g_cap = cap;
        }
        e = &g_entries[g_count++];
    }
    e->key = p->key;
    e->pawn = *p;
}

static int read_file(const char *path, char **buf, size_t *len)
{
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    char *b = NULL;
    long n = -1;
    if (fseek(f, 0, SEEK_END) == 0) n = ftell(f);
    if (n > 0 && n < (8L << 20) && fseek(f, 0, SEEK_SET) == 0 &&
        (b = malloc((size_t)n)) && fread(b, 1, (size_t)n, f) == (size_t)n) {
        fclose(f);
        *buf = b;
        *len = (size_t)n;
        return 0;
    }
    free(b);
    fclose(f);
    return -1;
}

static int write_file(const char *path, const char *data, size_t len)
{
    char tmp[MAX_PATH + 8];
    _snprintf(tmp, sizeof tmp, "%s.tmp", path);
    tmp[sizeof tmp - 1] = 0;
    FILE *f = fopen(tmp, "wb");
    if (!f) return -1;
    int ok = fwrite(data, 1, len, f) == len;
    ok = (fclose(f) == 0) && ok;
    if (!ok || !MoveFileExA(tmp, path, MOVEFILE_REPLACE_EXISTING)) {
        DeleteFileA(tmp);
        return -1;
    }
    return 0;
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
        return;
    }

    char pat[MAX_PATH + 8];
    _snprintf(pat, sizeof pat, "%s*.xml", g_dir);
    pat[sizeof pat - 1] = 0;
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pat, &fd);
    if (h == INVALID_HANDLE_VALUE) {
        patch_logf("store: %s is empty", g_dir);
        return;
    }
    int bad = 0;
    do {
        uint32_t key;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        if (!hex8_name(fd.cFileName, &key)) continue;
        char path[MAX_PATH + 16];
        _snprintf(path, sizeof path, "%s%s", g_dir, fd.cFileName);
        path[sizeof path - 1] = 0;
        char *buf;
        size_t len;
        struct rp_pawn p;
        int rc = -100;
        if (read_file(path, &buf, &len) == 0) {
            rc = rp_parse_cmc(buf, len, &p);
            free(buf);
        }
        if (rc == 0 && p.key != key) rc = -101;
        if (rc == 0 && !rp_key_supported(key)) rc = -102;
        if (rc != 0) {
            patch_logf("store: skipping %s (error %d)", fd.cFileName, rc);
            bad++;
            continue;
        }
        put(&p);
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    patch_logf("store: loaded %d pawn(s) from %s%s", g_count, g_dir,
               bad ? " (some skipped, see above)" : "");
}

const struct rp_pawn *store_get(uint32_t key)
{
    if (!g_lock_ready) return NULL;
    EnterCriticalSection(&g_lock);
    struct entry *e = find(key);
    LeaveCriticalSection(&g_lock);
    /* entries are only ever overwritten in place by store_capture_save on
     * the game thread, the same thread the rift hooks run on */
    return e ? &e->pawn : NULL;
}

int store_count(void)
{
    return g_count;
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
        if (!blk[i].len || !game_slot_alive(slot)) continue;
        struct rp_pawn p;
        int rc = rp_parse_cmc(blk[i].p, blk[i].len, &p);
        if (rc != 0) {
            patch_logf("capture: slot %d: parse error %d, not saved", slot, rc);
            continue;
        }
        if (!rp_key_supported(p.key)) {
            patch_logf("capture: slot %d: '%s' key %08X is not an official "
                       "offline pawn, skipped", slot, p.name, p.key);
            continue;
        }
        char path[MAX_PATH + 16];
        _snprintf(path, sizeof path, "%s%08X.xml", g_dir, p.key);
        path[sizeof path - 1] = 0;
        if (write_file(path, blk[i].p, blk[i].len) != 0) {
            patch_logf("capture: slot %d: cannot write %s", slot, path);
            continue;
        }
        EnterCriticalSection(&g_lock);
        put(&p);
        LeaveCriticalSection(&g_lock);
        written++;
        patch_logf("capture: slot %d: '%s' %08X (lv %u, voc %u rank %u, "
                   "DP %d) saved", slot, p.name, p.key, p.level, p.job,
                   p.job < RP_JOBS ? p.job_level[p.job] : 0, p.job_point);
    }
    free(xml);
    return written;
}
