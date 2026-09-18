#include <string.h>
#include <stdio.h>
#include <obs-module.h>
#include <obs-frontend-api.h>
#include "wf-actions.h"
#include "wf-http.h"
#include "wf-prefs.h"
#include "win-frame-filter.h"
#include "win-frame-styles.h"
#include "win-frame-presets.h"

#ifndef WF_PLUGIN_VERSION
#define WF_PLUGIN_VERSION "0.0.0"
#endif

static void put_msg(char *msg, size_t n, const char *text)
{
	if (msg && n) {
		strncpy(msg, text, n - 1);
		msg[n - 1] = 0;
	}
}

/* ------------------------------------------------------------ enumeration */

struct enum_ctx {
	wf_framed_cb cb;
	void *param;
	size_t count;
};

static void filter_visit(obs_source_t *parent, obs_source_t *child, void *p)
{
	struct enum_ctx *c = p;
	const char *id = obs_source_get_id(child);
	if (id && strcmp(id, WF_FILTER_ID) == 0) {
		c->cb(parent, child, c->param);
		c->count++;
	}
}

static bool source_visit(void *p, obs_source_t *src)
{
	obs_source_enum_filters(src, filter_visit, p);
	return true;
}

size_t wf_enum_framed(wf_framed_cb cb, void *param)
{
	struct enum_ctx c = {cb, param, 0};
	obs_enum_sources(source_visit, &c);
	return c.count;
}

struct vs_ctx {
	wf_video_source_cb cb;
	void *param;
};

static bool video_source_visit(void *p, obs_source_t *src)
{
	struct vs_ctx *c = p;
	if (obs_source_get_type(src) == OBS_SOURCE_TYPE_INPUT && (obs_source_get_output_flags(src) & OBS_SOURCE_VIDEO))
		c->cb(src, c->param);
	return true;
}

void wf_enum_video_sources(wf_video_source_cb cb, void *param)
{
	struct vs_ctx c = {cb, param};
	obs_enum_sources(video_source_visit, &c);
}

/* ------------------------------------------------ scene walking */

struct scene_ctx {
	const char *style_id; /* apply-style walk when set                       */
	size_t count;
	char *msg; /* add-to-selected walk otherwise */
	size_t msg_sz;
};

static bool add_filter_internal(obs_source_t *src, char *msg, size_t msg_sz);

static void apply_visit(obs_source_t *parent, obs_source_t *child, void *p)
{
	(void)parent;
	const char *id = obs_source_get_id(child);
	if (!id || strcmp(id, WF_FILTER_ID) != 0)
		return;
	struct scene_ctx *c = p;
	obs_data_t *s = obs_source_get_settings(child);
	win_frame_apply_style_defaults(s, c->style_id);
	obs_data_set_string(s, S_STYLE, c->style_id);
	obs_source_update(child, s);
	obs_data_release(s);
	c->count++;
}

static bool scene_item_apply_visit(obs_scene_t *scene, obs_sceneitem_t *item, void *p)
{
	(void)scene;
	obs_source_enum_filters(obs_sceneitem_get_source(item), apply_visit, p);
	if (obs_sceneitem_is_group(item))
		obs_sceneitem_group_enum_items(item, scene_item_apply_visit, p);
	return true;
}

static bool scene_item_select_visit(obs_scene_t *scene, obs_sceneitem_t *item, void *p)
{
	(void)scene;
	struct scene_ctx *c = p;
	if (obs_sceneitem_selected(item) && add_filter_internal(obs_sceneitem_get_source(item), c->msg, c->msg_sz))
		c->count++;
	if (obs_sceneitem_is_group(item))
		obs_sceneitem_group_enum_items(item, scene_item_select_visit, p);
	return true;
}

static void walk_current_scene(bool (*visit)(obs_scene_t *, obs_sceneitem_t *, void *), struct scene_ctx *c)
{
	obs_source_t *scene_src = obs_frontend_get_current_scene();
	if (!scene_src)
		return;
	obs_scene_t *scene = obs_scene_from_source(scene_src);
	if (scene)
		obs_scene_enum_items(scene, visit, c);
	obs_source_release(scene_src);
}

size_t wf_scene_apply_style(const char *style_id)
{
	struct scene_ctx c = {0};
	c.style_id = style_id;
	walk_current_scene(scene_item_apply_visit, &c);
	return c.count;
}

/* -------------------------------------------------------------- add / copy */

struct has_ctx {
	bool found;
};

static void has_visit(obs_source_t *parent, obs_source_t *child, void *p)
{
	(void)parent;
	const char *id = obs_source_get_id(child);
	if (id && strcmp(id, WF_FILTER_ID) == 0)
		((struct has_ctx *)p)->found = true;
}

struct pick_ctx {
	obs_source_t *f;
};

static void pick_first(obs_source_t *parent, obs_source_t *child, void *p)
{
	(void)parent;
	struct pick_ctx *pk = p;
	const char *id = obs_source_get_id(child);
	if (!pk->f && id && strcmp(id, WF_FILTER_ID) == 0)
		pk->f = obs_source_get_ref(child);
}

/* Returns a strong reference (release with obs_source_release) or NULL. */
static obs_source_t *first_frame_filter(obs_source_t *src)
{
	struct pick_ctx pk = {NULL};
	obs_source_enum_filters(src, pick_first, &pk);
	return pk.f;
}

static bool add_filter_internal(obs_source_t *src, char *msg, size_t msg_sz)
{
	if (!src) {
		put_msg(msg, msg_sz, "No source selected.");
		return false;
	}
	if (!(obs_source_get_output_flags(src) & OBS_SOURCE_VIDEO)) {
		put_msg(msg, msg_sz, "That source has no video, so a frame can't be added.");
		return false;
	}
	struct has_ctx h = {false};
	obs_source_enum_filters(src, has_visit, &h);
	if (h.found) {
		put_msg(msg, msg_sz, "That source already has a Windows Camera Frame.");
		return false;
	}

	obs_data_t *s = obs_data_create();
	const char *style = wf_prefs_get()->default_style;
	win_frame_apply_style_defaults(s, style);
	obs_data_set_string(s, S_STYLE, style);
	obs_source_t *flt = obs_source_create(WF_FILTER_ID, "Windows Camera Frame", s, NULL);
	obs_data_release(s);
	if (!flt) {
		put_msg(msg, msg_sz, "The filter could not be created (is the plugin loaded correctly?).");
		return false;
	}
	obs_source_filter_add(src, flt);
	obs_source_release(flt);
	return true;
}

bool wf_add_filter_to_source(obs_source_t *src, char *msg, size_t msg_sz)
{
	return add_filter_internal(src, msg, msg_sz);
}

size_t wf_add_filter_to_selected(char *msg, size_t msg_sz)
{
	struct scene_ctx c = {0};
	c.msg = msg;
	c.msg_sz = msg_sz;
	if (msg && msg_sz)
		msg[0] = 0;
	walk_current_scene(scene_item_select_visit, &c);
	if (c.count == 0 && msg && msg_sz && !msg[0])
		put_msg(msg, msg_sz, "Select a source in the Sources list first.");
	return c.count;
}

bool wf_copy_style(const char *src_name, const char *dst_name, char *msg, size_t msg_sz)
{
	if (!src_name || !dst_name || !*src_name || !*dst_name) {
		put_msg(msg, msg_sz, "Choose both a source and a destination.");
		return false;
	}
	if (strcmp(src_name, dst_name) == 0) {
		put_msg(msg, msg_sz, "Source and destination are the same.");
		return false;
	}

	bool ok = false;
	obs_source_t *a = obs_get_source_by_name(src_name);
	obs_source_t *b = obs_get_source_by_name(dst_name);
	if (!a || !b) {
		put_msg(msg, msg_sz, "One of the sources no longer exists.");
		goto out;
	}
	obs_source_t *fa = first_frame_filter(a);
	if (!fa) {
		put_msg(msg, msg_sz, "The source has no Windows Camera Frame to copy from.");
		goto out;
	}
	obs_data_t *sa = obs_source_get_settings(fa);
	obs_data_t *copy = obs_data_create();
	obs_data_apply(copy, sa);
	obs_data_release(sa);
	obs_source_release(fa);

	obs_source_t *fb = first_frame_filter(b);
	if (fb) {
		obs_source_update(fb, copy);
		obs_source_release(fb);
		put_msg(msg, msg_sz, "Style copied.");
		ok = true;
	} else {
		obs_source_t *flt = obs_source_create(WF_FILTER_ID, "Windows Camera Frame", copy, NULL);
		if (flt) {
			obs_source_filter_add(b, flt);
			obs_source_release(flt);
			put_msg(msg, msg_sz, "Style copied (a new frame was added to the destination).");
			ok = true;
		} else {
			put_msg(msg, msg_sz, "The destination filter could not be created.");
		}
	}
	obs_data_release(copy);
out:
	if (a)
		obs_source_release(a);
	if (b)
		obs_source_release(b);
	return ok;
}

/* -------------------------------------------------------- preset download */

static bool ends_with(const char *s, const char *suffix)
{
	size_t a = strlen(s), b = strlen(suffix);
	return a >= b && strcmp(s + a - b, suffix) == 0;
}

/* Removes anything that points at local files, keeps a downloaded preset
 * from probing the user's disk. */
static void strip_paths(obs_data_t *d)
{
	char names[64][96];
	int n = 0;
	for (obs_data_item_t *it = obs_data_first(d); it && n < 64; obs_data_item_next(&it)) {
		const char *k = obs_data_item_get_name(it);
		if (k && ends_with(k, "_path")) {
			strncpy(names[n], k, sizeof(names[n]) - 1);
			names[n][sizeof(names[n]) - 1] = 0;
			n++;
		}
	}
	for (int i = 0; i < n; i++)
		obs_data_unset_user_value(d, names[i]);
}

static bool looks_like_preset(obs_data_t *d)
{
	return obs_data_has_user_value(d, S_STYLE) || obs_data_has_user_value(d, S_FRAME_THICKNESS) ||
	       obs_data_has_user_value(d, S_TITLEBAR_HEIGHT);
}

static void stem_from_url(const char *url, char *out, size_t n)
{
	const char *q = strchr(url, '?');
	size_t len = q ? (size_t)(q - url) : strlen(url);
	const char *slash = url;
	for (size_t i = 0; i < len; i++)
		if (url[i] == '/')
			slash = url + i + 1;
	size_t l = (size_t)(url + len - slash);
	if (l >= n)
		l = n - 1;
	memcpy(out, slash, l);
	out[l] = 0;
	size_t ol = strlen(out);
	if (ol > 5 && ends_with(out, ".json"))
		out[ol - 5] = 0;
	if (!out[0])
		strncpy(out, "Downloaded preset", n - 1);
}

static bool save_downloaded(const char *name, obs_data_t *d)
{
	if (!looks_like_preset(d))
		return false;
	strip_paths(d);
	return win_frame_presets_save(name, d);
}

static bool fetch_json(const char *url, obs_data_t **out, char *err, size_t err_sz)
{
	char *body = NULL;
	size_t len = 0;
	*out = NULL;
	if (!wf_http_get(url, 1024 * 1024, &body, &len, err, err_sz))
		return false;
	obs_data_t *d = obs_data_create_from_json(body);
	bfree(body);
	if (!d) {
		put_msg(err, err_sz, "The download is not valid JSON.");
		return false;
	}
	*out = d;
	return true;
}

int wf_download_presets(const char *url, char *msg, size_t msg_sz)
{
	char err[256] = {0};
	obs_data_t *root = NULL;
	if (!url || !*url) {
		put_msg(msg, msg_sz, "Enter a URL first.");
		return -1;
	}
	if (!fetch_json(url, &root, err, sizeof(err))) {
		put_msg(msg, msg_sz, err);
		return -1;
	}

	int saved = 0, failed = 0;
	obs_data_array_t *list = obs_data_get_array(root, "presets");
	if (list) {
		size_t n = obs_data_array_count(list);
		if (n > 50)
			n = 50;
		for (size_t i = 0; i < n; i++) {
			obs_data_t *item = obs_data_array_item(list, i);
			const char *name = obs_data_get_string(item, "name");
			const char *purl = obs_data_get_string(item, "url");
			obs_data_t *preset = NULL;
			if (name && *name && purl && *purl && fetch_json(purl, &preset, err, sizeof(err))) {
				if (save_downloaded(name, preset))
					saved++;
				else
					failed++;
				obs_data_release(preset);
			} else {
				failed++;
			}
			obs_data_release(item);
		}
		obs_data_array_release(list);
	} else {
		char name[128];
		const char *n = obs_data_get_string(root, "name");
		if (n && *n)
			strncpy(name, n, sizeof(name) - 1), name[sizeof(name) - 1] = 0;
		else
			stem_from_url(url, name, sizeof(name));
		if (save_downloaded(name, root))
			saved++;
		else
			failed++;
	}
	obs_data_release(root);

	char m[192];
	if (saved == 0) {
		snprintf(m, sizeof(m), "Nothing was imported (%d item%s did not look like a preset).", failed,
			 failed == 1 ? "" : "s");
		put_msg(msg, msg_sz, m);
		return -1;
	}
	snprintf(m, sizeof(m), "Imported %d preset%s%s.", saved, saved == 1 ? "" : "s",
		 failed ? " (some items were skipped)" : "");
	put_msg(msg, msg_sz, m);
	return saved;
}

/* ------------------------------------------------------------ update check */

static bool parse_ver(const char *s, int v[3])
{
	v[0] = v[1] = v[2] = 0;
	if (!s)
		return false;
	if (*s == 'v' || *s == 'V')
		s++;
	return sscanf(s, "%d.%d.%d", &v[0], &v[1], &v[2]) >= 2;
}

int wf_check_update(char *msg, size_t msg_sz, char *url_out, size_t url_sz)
{
	if (url_out && url_sz)
		url_out[0] = 0;
	const char *url = wf_prefs_get()->update_url;
	if (!url[0]) {
		put_msg(msg, msg_sz,
			"No update URL is set. Open Settings and enter the address of your update JSON "
			"(for example a GitHub raw file containing {\"version\":\"1.2.0\",\"url\":\"...\"}).");
		return -1;
	}
	char err[256] = {0};
	obs_data_t *d = NULL;
	if (!fetch_json(url, &d, err, sizeof(err))) {
		put_msg(msg, msg_sz, err);
		return -1;
	}
	const char *latest = obs_data_get_string(d, "version");
	int a[3], b[3];
	if (!parse_ver(latest, a) || !parse_ver(WF_PLUGIN_VERSION, b)) {
		put_msg(msg, msg_sz, "The update file has no readable \"version\".");
		obs_data_release(d);
		return -1;
	}
	int newer = 0;
	for (int i = 0; i < 3; i++) {
		if (a[i] != b[i]) {
			newer = a[i] > b[i];
			break;
		}
	}
	const char *dl = obs_data_get_string(d, "url");
	if (url_out && url_sz && dl && strncmp(dl, "https://", 8) == 0)
		strncpy(url_out, dl, url_sz - 1), url_out[url_sz - 1] = 0;

	char m[192];
	if (newer)
		snprintf(m, sizeof(m), "Version %s is available (you have %s).", latest, WF_PLUGIN_VERSION);
	else
		snprintf(m, sizeof(m), "You are up to date (version %s).", WF_PLUGIN_VERSION);
	put_msg(msg, msg_sz, m);
	obs_data_release(d);
	return newer ? 1 : 0;
}
