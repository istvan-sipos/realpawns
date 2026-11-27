#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "patch.h"
#include "savehook.h"
#include "store.h"
#include "rift.h"
#include "discipline.h"
#include "skilltabs.h"
#include "innate.h"
#include "vocation.h"
#include "inclination.h"
#include "unequip.h"
#include "restrict.h"

#ifndef REALPAWNS_VERSION
#define REALPAWNS_VERSION "dev"
#endif

static char  g_dir[MAX_PATH];        /* DLL folder, trailing backslash */
static char  g_log_mode[32] = "truncate";
static FILE *g_log;
static int   g_persist = 1;
static int   g_progression = 1;   /* Discipline, leveling, skill tabs, vocation */
static int   g_inclination = 1;
static int   g_pawn_gear = 1;     /* unequip, equip, upgrade pages             */
static struct unequip_opts  g_unequip;
static struct restrict_opts g_restrict;

static void write_log(const char *msg)
{
    if (!g_log) return;
    SYSTEMTIME t;
    GetLocalTime(&t);
    fprintf(g_log, "%02u:%02u:%02u %s\n", t.wHour, t.wMinute, t.wSecond, msg);
    fflush(g_log);
}

static void init_paths(void)
{
    HMODULE self = NULL;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                       GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       (LPCSTR)&init_paths, &self);
    GetModuleFileNameA(self, g_dir, sizeof g_dir);
    char *bs = strrchr(g_dir, '\\');
    if (bs) bs[1] = 0; else g_dir[0] = 0;
}

static void read_config(void)
{
    char ini[MAX_PATH + 16];
    _snprintf(ini, sizeof ini, "%srealpawns.ini", g_dir);
    ini[sizeof ini - 1] = 0;
    GetPrivateProfileStringA("main", "logging", "truncate", g_log_mode,
                             sizeof g_log_mode, ini);
    g_persist     = GetPrivateProfileIntA("features", "persist", 1, ini) != 0;
    g_progression = GetPrivateProfileIntA("features", "pawn_progression", 1, ini) != 0;
    g_inclination = GetPrivateProfileIntA("features", "inclination_elixirs", 1, ini) != 0;
    g_pawn_gear   = GetPrivateProfileIntA("features", "pawn_gear", 1, ini) != 0;
    rp_vc_marker  = GetPrivateProfileIntA("vocation", "marker_item", 1529, ini);
    rp_vc_consume = GetPrivateProfileIntA("vocation", "consume_marker", 0, ini) != 0;

    rp_level_up = g_progression;
    g_unequip.keep_items     = g_pawn_gear;
    g_unequip.skip_prompt    = g_pawn_gear;
    g_restrict.equip         = g_pawn_gear;
    g_restrict.upgrade_pages = g_pawn_gear;
    /* the vocation marker and the elixirs are restricted items: support
     * pawns can only be handed them with the give patch */
    g_restrict.give = g_pawn_gear || g_progression || g_inclination;
}

static void open_log(void)
{
    if (strcmp(g_log_mode, "disabled") == 0) return;
    char path[MAX_PATH + 16];
    _snprintf(path, sizeof path, "%srealpawns.log", g_dir);
    path[sizeof path - 1] = 0;
    g_log = fopen(path, strcmp(g_log_mode, "append") == 0 ? "a" : "w");
}

static void on_save_written(const void *sav, int len)
{
    /* the save just written lacks them; the next one will have them */
    innate_grant_party();
    if (!g_persist) return;
    int n = store_capture_save(sav, len);
    if (n > 0) {
        /* the rift pool may already hold an older copy of these pawns */
        int k = rift_overlay_pool();
        if (k) patch_logf("save: refreshed %d pool record(s)", k);
    }
}

BOOL APIENTRY DllMain(HMODULE mod, DWORD reason, LPVOID reserved)
{
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(mod);
        init_paths();
        read_config();
        open_log();
        patch_set_log(write_log);
        patch_logf("realpawns.dll " REALPAWNS_VERSION " loaded");

        rp_innate_enabled = innate_init() > 0;

        /* clones are always made; persist only decides whether the save
         * hook writes their files (read back either way) */
        store_init(g_dir);
        int ok_r = rift_install();
        int ok_s = savehook_install(on_save_written);
        patch_logf("clones: %s", ok_r && ok_s ? "ready" :
                   "one or more hooks NOT installed (see above)");
        patch_logf("persist: %s", g_persist ? "on" :
                   "off in realpawns.ini (clone files are read, not written)");

        if (g_progression) {
            /* Discipline, and XP for clones; skill tabs; vocation change */
            int ok_d = discipline_install();
            int ok_t = skilltabs_install();
            int ok_v = vocation_install();
            patch_logf("pawn progression: %s (marker item %d, %s)",
                       ok_d && ok_t && ok_v ? "ready" :
                       "one or more patches NOT installed (see above)",
                       rp_vc_marker, rp_vc_consume ? "used up" : "kept");
        } else {
            patch_logf("pawn progression: disabled in realpawns.ini");
        }

        if (g_inclination)
            patch_logf("inclination elixirs: %s", inclination_install() ? "ready" :
                       "NOT installed (see above)");
        else
            patch_logf("inclination elixirs: disabled in realpawns.ini");

        if (g_pawn_gear) {
            int ok_u = unequip_install(&g_unequip);
            int ok_g = restrict_install(&g_restrict);
            patch_logf("pawn gear: %s", ok_u && ok_g ? "ready" :
                       "one or more patches NOT installed (see above)");
        } else if (g_restrict.give) {
            /* pawn progression / inclination elixirs still need it */
            patch_logf("pawn gear: disabled in realpawns.ini, giving restricted "
                       "items %s", restrict_install(&g_restrict) ? "on" :
                       "NOT installed (see above)");
        } else {
            patch_logf("pawn gear: disabled in realpawns.ini");
        }
    } else if (reason == DLL_PROCESS_DETACH) {
        patch_logf("realpawns.dll unloaded");
        if (g_log) fclose(g_log);
        g_log = NULL;
    }
    return TRUE;
}
