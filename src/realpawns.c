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
static int   g_discipline = 1;
static int   g_skill_tabs = 1;
static int   g_innate = 1;
static int   g_vocation = 1;
static int   g_inclination = 1;
static struct unequip_opts  g_unequip  = { 1, 1 };
static struct restrict_opts g_restrict = { 1, 1, 1 };

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
    g_persist    = GetPrivateProfileIntA("features", "persist", 1, ini) != 0;
    g_discipline = GetPrivateProfileIntA("features", "discipline", 1, ini) != 0;
    rp_level_up  = GetPrivateProfileIntA("features", "leveling", 1, ini) != 0;
    g_skill_tabs = GetPrivateProfileIntA("features", "skill_tabs", 1, ini) != 0;
    g_innate     = GetPrivateProfileIntA("features", "innate_skills", 1, ini) != 0;
    g_vocation   = GetPrivateProfileIntA("features", "vocation_change", 1, ini) != 0;
    g_inclination = GetPrivateProfileIntA("features", "inclination_elixirs", 1, ini) != 0;
    rp_vc_marker = GetPrivateProfileIntA("vocation", "marker_item", 1529, ini);
    rp_vc_consume = GetPrivateProfileIntA("vocation", "consume_marker", 0, ini) != 0;
    g_unequip.keep_items  = GetPrivateProfileIntA("gear", "keep_items", 1, ini) != 0;
    g_unequip.skip_prompt = GetPrivateProfileIntA("gear", "skip_prompt", 1, ini) != 0;
    g_restrict.equip      = GetPrivateProfileIntA("gear", "allow_equip", 1, ini) != 0;
    g_restrict.give       = GetPrivateProfileIntA("gear", "allow_give", 1, ini) != 0;
    g_restrict.upgrade_pages = GetPrivateProfileIntA("gear", "upgrade_pages", 1, ini) != 0;
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
    if (g_innate) innate_grant_party();
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

        rp_innate_enabled = g_innate && innate_init() > 0;
        if (!g_innate) patch_logf("innate: disabled in realpawns.ini");

        if (g_persist) {
            store_init(g_dir);
            int ok_r = rift_install();
            int ok_s = savehook_install(on_save_written);
            patch_logf("persist: %s", ok_r && ok_s ? "ready" :
                       "one or more hooks NOT installed (see above)");
        } else {
            patch_logf("persist: disabled in realpawns.ini");
        }

        /* only clones level up, through the Discipline patches */
        if (!g_persist || !g_discipline) rp_level_up = 0;
        if (g_discipline)
            patch_logf("discipline: %s, leveling %s",
                       discipline_install() ? "ready" : "NOT installed (see above)",
                       rp_level_up ? "on" : "off");
        else
            patch_logf("discipline: disabled in realpawns.ini");

        if (g_skill_tabs)
            patch_logf("skill tabs: %s", skilltabs_install() ? "ready" :
                       "NOT installed (see above)");
        else
            patch_logf("skill tabs: disabled in realpawns.ini");

        if (g_vocation)
            patch_logf("vocation change: %s (marker item %d, %s)",
                       vocation_install() ? "ready" : "NOT installed (see above)",
                       rp_vc_marker, rp_vc_consume ? "used up" : "kept");
        else
            patch_logf("vocation change: disabled in realpawns.ini");

        if (g_inclination)
            patch_logf("inclination elixirs: %s", inclination_install() ? "ready" :
                       "NOT installed (see above)");
        else
            patch_logf("inclination elixirs: disabled in realpawns.ini");

        if (g_unequip.keep_items || g_unequip.skip_prompt ||
            g_restrict.equip || g_restrict.give || g_restrict.upgrade_pages) {
            int ok_u = unequip_install(&g_unequip);
            int ok_g = restrict_install(&g_restrict);
            patch_logf("gear: %s", ok_u && ok_g ? "ready" :
                       "one or more patches NOT installed (see above)");
        } else {
            patch_logf("gear: disabled in realpawns.ini");
        }
    } else if (reason == DLL_PROCESS_DETACH) {
        patch_logf("realpawns.dll unloaded");
        if (g_log) fclose(g_log);
        g_log = NULL;
    }
    return TRUE;
}
