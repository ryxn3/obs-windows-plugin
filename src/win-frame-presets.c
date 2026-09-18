#include <string.h>
#include <stdio.h>
#include <obs-module.h>
#include <util/platform.h>
#include <util/dstr.h>
#include "win-frame-presets.h"
#include "win-frame-styles.h"

static char g_dir[512] = {0};

const char *win_frame_presets_dir(void)
{
	if (g_dir[0])
		return g_dir;

	char *cfg = obs_module_config_path("presets");
	if (cfg) {
		strncpy(g_dir, cfg, sizeof(g_dir) - 1);
		bfree(cfg);
	} else {
		strncpy(g_dir, "win-frame-filter-presets", sizeof(g_dir) - 1);
	}
	os_mkdirs(g_dir);
	return g_dir;
}

static void preset_path(const char *name, struct dstr *out)
{
	dstr_init_copy(out, win_frame_presets_dir());
	dstr_cat(out, "/");
	/* sanitize: only allow filesystem-safe characters */
	for (const char *c = name; *c; c++) {
		char ch = *c;
		bool ok = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') ||
			  ch == ' ' || ch == '-' || ch == '_';
		dstr_cat_ch(out, ok ? ch : '_');
	}
	dstr_cat(out, ".json");
}

void win_frame_presets_populate_list(obs_property_t *list)
{
	obs_property_list_clear(list);

	os_dir_t *dir = os_opendir(win_frame_presets_dir());
	if (!dir)
		return;

	struct os_dirent *ent;
	while ((ent = os_readdir(dir)) != NULL) {
		if (ent->directory)
			continue;
		size_t len = strlen(ent->d_name);
		if (len < 6 || strcmp(ent->d_name + len - 5, ".json") != 0)
			continue;
		char name[256];
		size_t copy_len = len - 5;
		if (copy_len >= sizeof(name))
			copy_len = sizeof(name) - 1;
		memcpy(name, ent->d_name, copy_len);
		name[copy_len] = 0;
		obs_property_list_add_string(list, name, name);
	}
	os_closedir(dir);
}

bool win_frame_presets_save(const char *name, obs_data_t *settings)
{
	if (!name || !*name || !settings)
		return false;
	struct dstr path;
	preset_path(name, &path);
	bool ok = obs_data_save_json_safe(settings, path.array, "tmp", "bak");
	dstr_free(&path);
	return ok;
}

obs_data_t *win_frame_presets_load(const char *name)
{
	if (!name || !*name)
		return NULL;
	struct dstr path;
	preset_path(name, &path);
	obs_data_t *data = obs_data_create_from_json_file_safe(path.array, "bak");
	dstr_free(&path);
	return data; /* NULL is a valid "missing/corrupt" result; caller must handle it */
}

bool win_frame_presets_delete(const char *name)
{
	struct dstr path;
	preset_path(name, &path);
	int rc = os_unlink(path.array);
	dstr_free(&path);
	return rc == 0;
}

bool win_frame_presets_duplicate(const char *src_name, const char *dst_name)
{
	obs_data_t *d = win_frame_presets_load(src_name);
	if (!d)
		return false;
	bool ok = win_frame_presets_save(dst_name, d);
	obs_data_release(d);
	return ok;
}

bool win_frame_presets_rename(const char *src_name, const char *dst_name)
{
	if (!win_frame_presets_duplicate(src_name, dst_name))
		return false;
	return win_frame_presets_delete(src_name);
}

bool win_frame_presets_export(const char *name, const char *dest_path)
{
	obs_data_t *d = win_frame_presets_load(name);
	if (!d)
		return false;
	bool ok = obs_data_save_json_safe(d, dest_path, "tmp", "bak");
	obs_data_release(d);
	return ok;
}

bool win_frame_presets_import(const char *src_path, const char *name)
{
	obs_data_t *d = obs_data_create_from_json_file(src_path);
	if (!d)
		return false; /* invalid/corrupt JSON: fail safe, never crash */
	bool ok = win_frame_presets_save(name, d);
	obs_data_release(d);
	return ok;
}

void win_frame_presets_seed_builtin(void)
{
	const char *dir = win_frame_presets_dir();
	for (int i = 0; i < STYLE_COUNT; i++) {
		if (i == STYLE_CUSTOM)
			continue;
		const struct win_frame_style_entry *e = &win_frame_style_table[i];

		struct dstr path;
		preset_path(e->display, &path);
		bool exists = os_file_exists(path.array);
		dstr_free(&path);
		if (exists)
			continue;

		obs_data_t *d = obs_data_create();
		obs_data_set_string(d, "style", e->id);
		win_frame_apply_style_defaults(d, e->id);
		win_frame_presets_save(e->display, d);
		obs_data_release(d);
	}
	(void)dir;
}

size_t win_frame_presets_enum(void (*cb)(const char *name, void *param), void *param)
{
	size_t count = 0;
	os_dir_t *dir = os_opendir(win_frame_presets_dir());
	if (!dir)
		return 0;
	struct os_dirent *ent;
	while ((ent = os_readdir(dir)) != NULL) {
		if (ent->directory)
			continue;
		size_t len = strlen(ent->d_name);
		if (len < 6 || strcmp(ent->d_name + len - 5, ".json") != 0)
			continue;
		char name[256];
		size_t n = len - 5 < sizeof(name) - 1 ? len - 5 : sizeof(name) - 1;
		memcpy(name, ent->d_name, n);
		name[n] = 0;
		cb(name, param);
		count++;
	}
	os_closedir(dir);
	return count;
}
