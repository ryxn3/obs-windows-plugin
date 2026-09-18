#include <string.h>
#include <obs-module.h>
#include <util/platform.h>
#include "wf-prefs.h"

#ifndef WF_DEFAULT_UPDATE_URL
#define WF_DEFAULT_UPDATE_URL ""
#endif

static struct wf_prefs g_prefs;
static bool g_loaded = false;

static void copy_str(char *dst, size_t n, const char *src)
{
	if (!src)
		src = "";
	strncpy(dst, src, n - 1);
	dst[n - 1] = 0;
}

static void set_defaults(void)
{
	memset(&g_prefs, 0, sizeof(g_prefs));
	copy_str(g_prefs.default_style, sizeof(g_prefs.default_style), "winxp_luna");
	copy_str(g_prefs.update_url, sizeof(g_prefs.update_url), WF_DEFAULT_UPDATE_URL);
}

static char *prefs_path(void)
{
	char *p = obs_module_config_path("settings.json");
	if (p) {
		char dir[512];
		copy_str(dir, sizeof(dir), p);
		char *slash = strrchr(dir, '/');
		char *bslash = strrchr(dir, '\\');
		if (bslash && (!slash || bslash > slash))
			slash = bslash;
		if (slash) {
			*slash = 0;
			os_mkdirs(dir);
		}
	}
	return p;
}

void wf_prefs_load(void)
{
	set_defaults();
	g_loaded = true;

	char *path = prefs_path();
	if (!path)
		return;
	obs_data_t *d = obs_data_create_from_json_file_safe(path, "bak");
	bfree(path);
	if (!d)
		return; /* missing or corrupt: keep defaults */

	const char *v = obs_data_get_string(d, "default_style");
	if (v && *v)
		copy_str(g_prefs.default_style, sizeof(g_prefs.default_style), v);
	v = obs_data_get_string(d, "update_url");
	if (v && *v) /* an empty saved value keeps the built-in default */
		copy_str(g_prefs.update_url, sizeof(g_prefs.update_url), v);
	copy_str(g_prefs.download_url, sizeof(g_prefs.download_url), obs_data_get_string(d, "download_url"));
	g_prefs.welcome_shown = obs_data_get_bool(d, "welcome_shown");
	obs_data_release(d);
}

void wf_prefs_save(void)
{
	char *path = prefs_path();
	if (!path)
		return;
	obs_data_t *d = obs_data_create();
	obs_data_set_string(d, "default_style", g_prefs.default_style);
	obs_data_set_string(d, "update_url", g_prefs.update_url);
	obs_data_set_string(d, "download_url", g_prefs.download_url);
	obs_data_set_bool(d, "welcome_shown", g_prefs.welcome_shown);
	obs_data_save_json_safe(d, path, "tmp", "bak");
	obs_data_release(d);
	bfree(path);
}

struct wf_prefs *wf_prefs_get(void)
{
	if (!g_loaded)
		wf_prefs_load();
	return &g_prefs;
}
