/* "Windows Alt+Tab" scene transition.
 *
 * Draws the Alt+Tab switcher of a chosen Windows version over the outgoing
 * scene, slides the highlight to the incoming scene, then reveals it. The
 * total time and the pause on the new window are settings; OBS shows the
 * duration as fixed to our value in its Scene Transitions dock. */

#include <string.h>
#include <math.h>
#include <obs-module.h>
#include <graphics/vec2.h>
#include <graphics/vec4.h>
#include "win-frame-text.h"

#define S_VERSION "at_version"
#define S_COUNT "at_count"
#define S_HOLD "at_hold"
#define S_DURATION "at_duration_ms"
#define S_SCALE "at_scale"
#define S_DARK "at_dark"
#define S_ACCENT "at_accent"
#define S_TITLES "at_titles"

#define WF_ALTTAB_PARAMS(X) \
	X(tex_a) X(tex_b) X(atlas) X(canvas_size) X(ov_alpha) X(bg_mix) X(dim) X(panel_scale) X(version) X(n) \
	X(sel_pos) X(dark) X(accent) X(tile0) X(tile_step) X(header_h) X(panel_rect) X(atlas_w) X(atlas_lh) \
	X(atlas_lines) X(show_titles) X(ui) X(lw0) X(lw1) X(title_pos)

enum {
#define X(n) AP_##n,
	WF_ALTTAB_PARAMS(X)
#undef X
	AP_COUNT
};

struct alttab {
	obs_source_t *source;
	gs_effect_t *effect;
	gs_eparam_t *p[AP_COUNT];

	int version, count, hold_pct, duration_ms;
	float scale;
	bool dark, titles;
	long long accent;

	/* cached title atlas */
	gs_texture_t *atlas;
	uint32_t atlas_w, atlas_lh;
	float widths[8];
	char cached_a[128], cached_b[128];
	int cached_version, cached_count, cached_px;
	bool cached_dark;

	float fade_out; /* for the audio crossfade */
};

static const char *decoys[5][6] = {
	{"My Computer", "Notepad", "Minesweeper", "Paint", "Solitaire", "Calculator"},
	{"Computer", "Windows Explorer", "Notepad", "Paint", "Calculator", "Windows Media Player"},
	{"This PC", "Internet Explorer", "Notepad", "Store", "Photos", "Calculator"},
	{"This PC", "Microsoft Edge", "Notepad", "Settings", "Photos", "Calculator"},
	{"File Explorer", "Microsoft Edge", "Notepad", "Settings", "Photos", "Terminal"},
};

static const char *at_get_name(void *unused)
{
	UNUSED_PARAMETER(unused);
	return obs_module_text("AltTab.Name");
}

static void at_update(void *data, obs_data_t *s)
{
	struct alttab *a = data;
	a->version = (int)obs_data_get_int(s, S_VERSION);
	if (a->version < 0 || a->version > 4)
		a->version = 3;
	a->count = (int)obs_data_get_int(s, S_COUNT);
	if (a->count < 2)
		a->count = 2;
	if (a->count > 8)
		a->count = 8;
	a->hold_pct = (int)obs_data_get_int(s, S_HOLD);
	a->duration_ms = (int)obs_data_get_int(s, S_DURATION);
	if (a->duration_ms < 200)
		a->duration_ms = 200;
	a->scale = (float)obs_data_get_double(s, S_SCALE);
	if (a->scale < 0.3f)
		a->scale = 1.0f;
	a->dark = obs_data_get_bool(s, S_DARK);
	a->titles = obs_data_get_bool(s, S_TITLES);
	a->accent = obs_data_get_int(s, S_ACCENT);
	a->fade_out = 0.10f;

	/* the "time" setting: lock OBS's duration control to our value */
	obs_transition_enable_fixed(a->source, true, (uint32_t)a->duration_ms);
}

static void *at_create(obs_data_t *settings, obs_source_t *source)
{
	struct alttab *a = bzalloc(sizeof(*a));
	a->source = source;

	char *path = obs_module_file("effects/alttab.effect");
	char *err = NULL;
	obs_enter_graphics();
	a->effect = gs_effect_create_from_file(path, &err);
	obs_leave_graphics();
	if (!a->effect) {
		blog(LOG_ERROR, "[win-frame-filter] alt+tab effect failed: %s", err ? err : "unknown error");
		bfree(err);
		bfree(path);
		bfree(a);
		return NULL;
	}
	bfree(err);
	bfree(path);

	int i = 0;
#define X(n) a->p[i++] = gs_effect_get_param_by_name(a->effect, #n);
	WF_ALTTAB_PARAMS(X)
#undef X

	a->cached_version = -1;
	at_update(a, settings);
	return a;
}

static void at_destroy(void *data)
{
	struct alttab *a = data;
	obs_enter_graphics();
	if (a->atlas)
		gs_texture_destroy(a->atlas);
	if (a->effect)
		gs_effect_destroy(a->effect);
	obs_leave_graphics();
	bfree(a);
}

static void at_defaults(obs_data_t *s)
{
	obs_data_set_default_int(s, S_VERSION, 3);
	obs_data_set_default_int(s, S_COUNT, 4);
	obs_data_set_default_int(s, S_HOLD, 20);
	obs_data_set_default_int(s, S_DURATION, 1600);
	obs_data_set_default_double(s, S_SCALE, 1.0);
	obs_data_set_default_bool(s, S_DARK, true);
	obs_data_set_default_int(s, S_ACCENT, (int)0xFFD77800u); /* ABGR for #0078D7 */
	obs_data_set_default_bool(s, S_TITLES, true);
}

static obs_properties_t *at_properties(void *data)
{
	UNUSED_PARAMETER(data);
	obs_properties_t *p = obs_properties_create();
	obs_property_t *v = obs_properties_add_list(p, S_VERSION, obs_module_text("AltTab.Version"), OBS_COMBO_TYPE_LIST,
						    OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(v, obs_module_text("AltTab.V0"), 0);
	obs_property_list_add_int(v, obs_module_text("AltTab.V1"), 1);
	obs_property_list_add_int(v, obs_module_text("AltTab.V2"), 2);
	obs_property_list_add_int(v, obs_module_text("AltTab.V3"), 3);
	obs_property_list_add_int(v, obs_module_text("AltTab.V4"), 4);
	obs_properties_add_int_slider(p, S_DURATION, obs_module_text("AltTab.Duration"), 300, 10000, 50);
	obs_properties_add_int_slider(p, S_HOLD, obs_module_text("AltTab.Hold"), 5, 40, 1);
	obs_properties_add_int_slider(p, S_COUNT, obs_module_text("AltTab.Count"), 2, 8, 1);
	obs_properties_add_float_slider(p, S_SCALE, obs_module_text("AltTab.Scale"), 0.5, 2.0, 0.05);
	obs_properties_add_bool(p, S_DARK, obs_module_text("AltTab.Dark"));
	obs_properties_add_color(p, S_ACCENT, obs_module_text("AltTab.Accent"));
	obs_properties_add_bool(p, S_TITLES, obs_module_text("AltTab.Titles"));
	return p;
}

/* ---------------------------------------------------------------- render */

static float smooth(float e0, float e1, float x)
{
	float t = (x - e0) / (e1 - e0);
	t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
	return t * t * (3.0f - 2.0f * t);
}

static void copy_name(char *dst, size_t n, obs_source_t *src)
{
	const char *nm = src ? obs_source_get_name(src) : "";
	strncpy(dst, nm ? nm : "", n - 1);
	dst[n - 1] = 0;
}

static void rebuild_atlas(struct alttab *a, int px)
{
	char name_a[128], name_b[128];
	obs_source_t *sa = obs_transition_get_source(a->source, OBS_TRANSITION_SOURCE_A);
	obs_source_t *sb = obs_transition_get_source(a->source, OBS_TRANSITION_SOURCE_B);
	copy_name(name_a, sizeof(name_a), sa);
	copy_name(name_b, sizeof(name_b), sb);
	if (sa)
		obs_source_release(sa);
	if (sb)
		obs_source_release(sb);

	bool same = a->atlas && strcmp(name_a, a->cached_a) == 0 && strcmp(name_b, a->cached_b) == 0 &&
		    a->cached_version == a->version && a->cached_count == a->count && a->cached_px == px &&
		    a->cached_dark == a->dark;
	if (same)
		return;

	const char *lines[8];
	int n = a->count;
	lines[0] = name_a[0] ? name_a : "Current scene";
	lines[n - 1] = name_b[0] ? name_b : "Next scene";
	for (int i = 1; i < n - 1; i++)
		lines[i] = decoys[a->version][(i - 1) % 6];

	if (a->atlas) {
		obs_enter_graphics();
		gs_texture_destroy(a->atlas);
		obs_leave_graphics();
		a->atlas = NULL;
	}
	const bool light_text = (a->version == 1 || a->version == 2) || a->dark;
	const uint32_t color = (a->version == 0) ? 0xFF000000u : (light_text ? 0xFFFFFFFFu : 0xFF141414u);
	const char *font = (a->version == 0) ? "Tahoma" : "Segoe UI";
	memset(a->widths, 0, sizeof(a->widths));
	a->atlas = win_frame_render_text_atlas(lines, n, font, px, false, color, &a->atlas_w, &a->atlas_lh, a->widths);

	strncpy(a->cached_a, name_a, sizeof(a->cached_a) - 1);
	strncpy(a->cached_b, name_b, sizeof(a->cached_b) - 1);
	a->cached_version = a->version;
	a->cached_count = a->count;
	a->cached_px = px;
	a->cached_dark = a->dark;
}

static void set_v4(gs_eparam_t *p, float x, float y, float z, float w)
{
	struct vec4 v;
	vec4_set(&v, x, y, z, w);
	gs_effect_set_vec4(p, &v);
}

static void at_callback(void *data, gs_texture_t *ta, gs_texture_t *tb, float t, uint32_t cx, uint32_t cy)
{
	struct alttab *a = data;
	if (!ta && !tb)
		return;
	if (!ta)
		ta = tb;
	if (!tb)
		tb = ta;

	const int n = a->count;
	const int ver = a->version;
	const float sc = a->scale * fmaxf((float)cy / 1080.0f, 0.5f);

	int px = (int)floorf((ver == 0 ? 11.0f : (ver >= 3 ? 13.0f : 17.0f)) * sc + 0.5f);
	if (px < 9)
		px = 9;
	rebuild_atlas(a, px);

	/* ---- timeline */
	const float fade_in = 0.12f, fade_out = a->fade_out;
	float hold = (float)a->hold_pct / 100.0f;
	float t0 = fade_in;
	float t1 = 1.0f - fade_out - hold;
	if (t1 < t0 + 0.15f)
		t1 = t0 + 0.15f;
	float ps = (t - t0) / (t1 - t0);
	ps = ps < 0.0f ? 0.0f : (ps > 1.0f ? 1.0f : ps);
	const float steps = (float)(n - 1);
	float s = ps * steps;
	float ip = floorf(fminf(s, steps - 0.0001f));
	float sel = ip + smooth(0.0f, 0.5f, s - ip);
	if (ps >= 1.0f)
		sel = steps;

	const float ov = smooth(0.0f, fade_in, t) * (1.0f - smooth(1.0f - fade_out * 0.7f, 1.0f, t));
	const float bgm = smooth(1.0f - fade_out, 1.0f - fade_out * 0.35f, t);
	static const float dims[5] = {0.0f, 0.20f, 0.45f, 0.50f, 0.40f};
	const float pscale = 0.94f + 0.06f * smooth(0.0f, fade_in, t);

	/* ---- layout (canvas pixels) */
	float px0 = 0, py0 = 0, pw = 0, ph = 0;
	float tx = 0, ty = 0, tw = 0, th = 0, step = 0, hdr = 0, title_y = 0;
	float lh = (float)a->atlas_lh;
	float maxw = 0;
	for (int i = 0; i < n; i++)
		if (a->widths[i] > maxw)
			maxw = a->widths[i];

	if (ver == 0) {
		float cell = floorf(48.0f * sc), pad = floorf(14.0f * sc);
		float text_h = lh + floorf(8.0f * sc);
		pw = fmaxf(cell * n + pad * 2.0f, maxw + pad * 3.0f);
		ph = pad + cell + text_h + floorf(pad * 0.6f);
		px0 = floorf(((float)cx - pw) * 0.5f);
		py0 = floorf(((float)cy - ph) * 0.5f);
		tw = th = cell;
		step = cell;
		tx = floorf(px0 + (pw - cell * n) * 0.5f);
		ty = py0 + pad;
		title_y = ty + cell + floorf(6.0f * sc);
	} else {
		float tile_w = (float)cx * 0.16f * a->scale;
		float gap = floorf(tile_w * 0.11f);
		float total = n * tile_w + (n - 1) * gap;
		if (total > (float)cx * 0.90f) {
			tile_w = ((float)cx * 0.90f) / (n + (n - 1) * 0.11f);
			gap = floorf(tile_w * 0.11f);
			total = n * tile_w + (n - 1) * gap;
		}
		tile_w = floorf(tile_w);
		tw = tile_w;
		th = floorf(tile_w * (float)cy / (float)cx);
		step = tw + gap;
		total = n * tw + (n - 1) * gap;
		hdr = (ver >= 3) ? floorf(fmaxf(lh + 6.0f * sc, tw * 0.10f)) : 0.0f;
		float pad = floorf(gap * 1.2f + 6.0f * sc);
		ph = hdr + th + pad * 2.0f;
		pw = (ver == 2) ? (float)cx : total + pad * 2.0f;
		px0 = (ver == 2) ? 0.0f : floorf(((float)cx - pw) * 0.5f);
		py0 = floorf(((float)cy - ph) * 0.5f);
		tx = floorf(((float)cx - total) * 0.5f);
		ty = py0 + pad + hdr;
		title_y = py0 - lh - floorf(10.0f * sc);
	}

	/* ---- bind + draw */
	const bool prev = gs_framebuffer_srgb_enabled();
	gs_enable_framebuffer_srgb(true);

	gs_effect_set_texture_srgb(a->p[AP_tex_a], ta);
	gs_effect_set_texture_srgb(a->p[AP_tex_b], tb);
	if (a->atlas)
		gs_effect_set_texture(a->p[AP_atlas], a->atlas);

	struct vec2 v2;
	vec2_set(&v2, (float)cx, (float)cy);
	gs_effect_set_vec2(a->p[AP_canvas_size], &v2);
	gs_effect_set_float(a->p[AP_ov_alpha], ov);
	gs_effect_set_float(a->p[AP_bg_mix], bgm);
	gs_effect_set_float(a->p[AP_dim], dims[ver]);
	gs_effect_set_float(a->p[AP_panel_scale], pscale);
	gs_effect_set_int(a->p[AP_version], ver);
	gs_effect_set_int(a->p[AP_n], n);
	gs_effect_set_float(a->p[AP_sel_pos], sel);
	gs_effect_set_int(a->p[AP_dark], a->dark ? 1 : 0);
	{
		uint32_t c = (uint32_t)a->accent;
		set_v4(a->p[AP_accent], (float)(c & 0xFF) / 255.0f, (float)((c >> 8) & 0xFF) / 255.0f,
		       (float)((c >> 16) & 0xFF) / 255.0f, 1.0f);
	}
	set_v4(a->p[AP_tile0], tx, ty, tw, th);
	gs_effect_set_float(a->p[AP_tile_step], step);
	gs_effect_set_float(a->p[AP_header_h], hdr);
	set_v4(a->p[AP_panel_rect], px0, py0, pw, ph);
	gs_effect_set_float(a->p[AP_atlas_w], (float)(a->atlas_w ? a->atlas_w : 1));
	gs_effect_set_float(a->p[AP_atlas_lh], (float)(a->atlas_lh ? a->atlas_lh : 1));
	gs_effect_set_float(a->p[AP_atlas_lines], (float)n);
	gs_effect_set_int(a->p[AP_show_titles], (a->titles && a->atlas) ? 1 : 0);
	gs_effect_set_float(a->p[AP_ui], sc);
	set_v4(a->p[AP_lw0], a->widths[0], a->widths[1], a->widths[2], a->widths[3]);
	set_v4(a->p[AP_lw1], a->widths[4], a->widths[5], a->widths[6], a->widths[7]);
	vec2_set(&v2, 0.0f, title_y);
	gs_effect_set_vec2(a->p[AP_title_pos], &v2);

	while (gs_effect_loop(a->effect, "Draw"))
		gs_draw_sprite(NULL, 0, cx, cy);

	gs_enable_framebuffer_srgb(prev);
}

static void at_video_render(void *data, gs_effect_t *effect)
{
	struct alttab *a = data;
	UNUSED_PARAMETER(effect);
	obs_transition_video_render(a->source, at_callback);
}

static float mix_b(void *data, float t)
{
	struct alttab *a = data;
	return smooth(1.0f - a->fade_out, 1.0f - a->fade_out * 0.35f, t);
}

static float mix_a(void *data, float t)
{
	return 1.0f - mix_b(data, t);
}

static bool at_audio_render(void *data, uint64_t *ts_out, struct obs_source_audio_mix *audio, uint32_t mixers,
			    size_t channels, size_t sample_rate)
{
	struct alttab *a = data;
	return obs_transition_audio_render(a->source, ts_out, audio, mixers, channels, sample_rate, mix_a, mix_b);
}

static enum gs_color_space at_color_space(void *data, size_t count, const enum gs_color_space *preferred)
{
	UNUSED_PARAMETER(count);
	UNUSED_PARAMETER(preferred);
	struct alttab *a = data;
	return obs_transition_video_get_color_space(a->source);
}

struct obs_source_info win_alttab_transition_info = {
	.id = "win_alttab_transition",
	.type = OBS_SOURCE_TYPE_TRANSITION,
	.get_name = at_get_name,
	.create = at_create,
	.destroy = at_destroy,
	.update = at_update,
	.get_defaults = at_defaults,
	.get_properties = at_properties,
	.video_render = at_video_render,
	.audio_render = at_audio_render,
	.video_get_color_space = at_color_space,
};
