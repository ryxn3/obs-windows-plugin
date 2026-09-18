/* "Windows Update Screen" source: a "Working on updates / Configuring Windows
 * Updates" screen in seven Windows eras, driven by a countdown so it doubles
 * as a "stream starting soon" scene. */

#include <string.h>
#include <stdio.h>
#include <math.h>
#include <time.h>
#include <obs-module.h>
#include <util/platform.h>
#include <graphics/vec2.h>
#include <graphics/vec3.h>
#include <graphics/vec4.h>
#include "win-frame-text.h"

#define S_W "u_width"
#define S_H "u_height"
#define S_VER "u_version"
#define S_MODE "u_mode"
#define S_DUR "u_duration"
#define S_TARGET "u_target"
#define S_PCT "u_percent"
#define S_REAL "u_realistic"
#define S_AUTO "u_autostart"
#define S_TITLE "u_title"
#define S_MSG "u_message"
#define S_FINISH "u_finish"
#define S_TIME "u_show_time"
#define S_SCALE "u_scale"
#define S_ACCENT "u_accent"

#define UPD_PARAMS(X) X(quad_size) X(ver) X(pct) X(time) X(u) X(dlg) X(bar) X(btn) X(spin) X(tint)

enum {
#define X(n) UP_##n,
	UPD_PARAMS(X)
#undef X
	UP_COUNT
};

#define MAX_ITEMS 8

struct item {
	char key[512];
	gs_texture_t *tex;
	uint32_t w, h;
	float x, y; /* placement (top-left of the text, without padding) */
	bool center;
	bool visible;
};

struct upd {
	obs_source_t *source;
	gs_effect_t *effect;
	gs_eparam_t *p[UP_COUNT];

	uint32_t width, height;
	int version, mode;
	float duration, percent_manual, scale;
	char target[16];
	bool realistic, autostart, show_time;
	long long accent;
	char title[160], message[240], finish[160];

	uint64_t start_ns;
	float pct;       /* 0..1 as displayed */
	float remaining; /* seconds, -1 when unknown */
	float t;         /* animation clock */
	struct item items[MAX_ITEMS];
};

static const char *us_get_name(void *unused)
{
	UNUSED_PARAMETER(unused);
	return obs_module_text("Update.Name");
}

/* per-version default texts */
static const char *def_title[7] = {"Windows 2000 Professional Setup",
				   "Windows Setup",
				   "Installing updates",
				   "Configuring Windows Updates",
				   "Working on updates",
				   "Working on updates",
				   "Working on updates"};
static const char *def_msg[7] = {
	"Please wait while Setup installs updates and copies files to your Windows installation folders. This may take several minutes to complete.",
	"Please wait while Setup updates your Windows configuration. Do not turn off your computer.",
	"Do not turn off your computer.",
	"Do not turn off your computer",
	"Don't turn off your PC. This will take a while.",
	"Don't turn off your PC. This will take a while.",
	"Don't turn off your PC. This will take a while."};

static void us_update(void *data, obs_data_t *s)
{
	struct upd *a = data;
	a->width = (uint32_t)obs_data_get_int(s, S_W);
	a->height = (uint32_t)obs_data_get_int(s, S_H);
	if (a->width < 320)
		a->width = 320;
	if (a->height < 240)
		a->height = 240;
	a->version = (int)obs_data_get_int(s, S_VER);
	if (a->version < 0 || a->version > 6)
		a->version = 5;
	a->mode = (int)obs_data_get_int(s, S_MODE);
	a->duration = (float)obs_data_get_double(s, S_DUR);
	if (a->duration < 5.0f)
		a->duration = 5.0f;
	a->percent_manual = (float)obs_data_get_double(s, S_PCT);
	a->realistic = obs_data_get_bool(s, S_REAL);
	a->autostart = obs_data_get_bool(s, S_AUTO);
	a->show_time = obs_data_get_bool(s, S_TIME);
	a->scale = (float)obs_data_get_double(s, S_SCALE);
	if (a->scale < 0.4f)
		a->scale = 1.0f;
	a->accent = obs_data_get_int(s, S_ACCENT);
	strncpy(a->target, obs_data_get_string(s, S_TARGET), sizeof(a->target) - 1);
	strncpy(a->title, obs_data_get_string(s, S_TITLE), sizeof(a->title) - 1);
	strncpy(a->message, obs_data_get_string(s, S_MSG), sizeof(a->message) - 1);
	strncpy(a->finish, obs_data_get_string(s, S_FINISH), sizeof(a->finish) - 1);
}

static void *us_create(obs_data_t *settings, obs_source_t *source)
{
	struct upd *a = bzalloc(sizeof(*a));
	a->source = source;
	char *path = obs_module_file("effects/update.effect");
	char *err = NULL;
	obs_enter_graphics();
	a->effect = gs_effect_create_from_file(path, &err);
	obs_leave_graphics();
	bfree(path);
	if (!a->effect) {
		blog(LOG_ERROR, "[win-frame-filter] update-screen effect failed: %s", err ? err : "unknown");
		bfree(err);
		bfree(a);
		return NULL;
	}
	bfree(err);
	int i = 0;
#define X(n) a->p[i++] = gs_effect_get_param_by_name(a->effect, #n);
	UPD_PARAMS(X)
#undef X
	us_update(a, settings);
	a->start_ns = os_gettime_ns();
	a->remaining = -1.0f;
	return a;
}

static void us_destroy(void *data)
{
	struct upd *a = data;
	obs_enter_graphics();
	for (int i = 0; i < MAX_ITEMS; i++)
		if (a->items[i].tex)
			gs_texture_destroy(a->items[i].tex);
	if (a->effect)
		gs_effect_destroy(a->effect);
	obs_leave_graphics();
	bfree(a);
}

static void us_activate(void *data)
{
	struct upd *a = data;
	if (a->autostart)
		a->start_ns = os_gettime_ns();
}

static void us_defaults(obs_data_t *s)
{
	obs_data_set_default_int(s, S_W, 1920);
	obs_data_set_default_int(s, S_H, 1080);
	obs_data_set_default_int(s, S_VER, 5);
	obs_data_set_default_int(s, S_MODE, 0);
	obs_data_set_default_double(s, S_DUR, 300.0);
	obs_data_set_default_string(s, S_TARGET, "20:00");
	obs_data_set_default_double(s, S_PCT, 42.0);
	obs_data_set_default_bool(s, S_REAL, true);
	obs_data_set_default_bool(s, S_AUTO, true);
	obs_data_set_default_string(s, S_FINISH, "The stream is starting now!");
	obs_data_set_default_bool(s, S_TIME, true);
	obs_data_set_default_double(s, S_SCALE, 1.0);
	obs_data_set_default_int(s, S_ACCENT, (int)0xFFD77800u);
}

static bool restart_clicked(obs_properties_t *props, obs_property_t *p, void *data)
{
	UNUSED_PARAMETER(props);
	UNUSED_PARAMETER(p);
	struct upd *a = data;
	a->start_ns = os_gettime_ns();
	return false;
}

static obs_properties_t *us_properties(void *data)
{
	obs_properties_t *p = obs_properties_create();
	obs_properties_add_button2(p, "u_restart", obs_module_text("Update.Restart"), restart_clicked, data);
	obs_property_t *v = obs_properties_add_list(p, S_VER, obs_module_text("Update.Version"), OBS_COMBO_TYPE_LIST,
						    OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(v, obs_module_text("Update.V0"), 0);
	obs_property_list_add_int(v, obs_module_text("Update.V1"), 1);
	obs_property_list_add_int(v, obs_module_text("Update.V2"), 2);
	obs_property_list_add_int(v, obs_module_text("Update.V3"), 3);
	obs_property_list_add_int(v, obs_module_text("Update.V4"), 4);
	obs_property_list_add_int(v, obs_module_text("Update.V5"), 5);
	obs_property_list_add_int(v, obs_module_text("Update.V6"), 6);
	obs_property_t *m = obs_properties_add_list(p, S_MODE, obs_module_text("Update.Mode"), OBS_COMBO_TYPE_LIST,
						    OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(m, obs_module_text("Update.M0"), 0);
	obs_property_list_add_int(m, obs_module_text("Update.M1"), 1);
	obs_property_list_add_int(m, obs_module_text("Update.M2"), 2);
	obs_properties_add_float(p, S_DUR, obs_module_text("Update.Duration"), 5.0, 86400.0, 5.0);
	obs_properties_add_text(p, S_TARGET, obs_module_text("Update.Target"), OBS_TEXT_DEFAULT);
	obs_properties_add_float_slider(p, S_PCT, obs_module_text("Update.Percent"), 0.0, 100.0, 1.0);
	obs_properties_add_bool(p, S_REAL, obs_module_text("Update.Realistic"));
	obs_properties_add_bool(p, S_AUTO, obs_module_text("Update.Auto"));
	obs_properties_add_bool(p, S_TIME, obs_module_text("Update.ShowTime"));
	obs_properties_add_text(p, S_TITLE, obs_module_text("Update.Title"), OBS_TEXT_DEFAULT);
	obs_properties_add_text(p, S_MSG, obs_module_text("Update.Message"), OBS_TEXT_DEFAULT);
	obs_properties_add_text(p, S_FINISH, obs_module_text("Update.Finish"), OBS_TEXT_DEFAULT);
	obs_properties_add_color(p, S_ACCENT, obs_module_text("Update.Accent"));
	obs_properties_add_float_slider(p, S_SCALE, obs_module_text("Update.Scale"), 0.5, 2.0, 0.05);
	obs_properties_add_int(p, S_W, obs_module_text("Update.Width"), 320, 8192, 1);
	obs_properties_add_int(p, S_H, obs_module_text("Update.Height"), 240, 8192, 1);
	return p;
}

/* progress that stalls like a real installer: (time fraction, percent) */
static float realistic_curve(float x)
{
	static const float pts[][2] = {{0.00f, 0.00f}, {0.10f, 0.02f}, {0.28f, 0.29f}, {0.42f, 0.31f}, {0.60f, 0.66f},
				       {0.78f, 0.69f}, {0.93f, 0.97f}, {1.00f, 1.00f}};
	if (x <= 0.0f)
		return 0.0f;
	for (int i = 1; i < 8; i++) {
		if (x <= pts[i][0]) {
			float k = (x - pts[i - 1][0]) / (pts[i][0] - pts[i - 1][0]);
			return pts[i - 1][1] + (pts[i][1] - pts[i - 1][1]) * k;
		}
	}
	return 1.0f;
}

static float seconds_until_clock(const char *hhmm)
{
	int h = -1, m = -1;
	if (sscanf(hhmm, "%d:%d", &h, &m) != 2 || h < 0 || h > 23 || m < 0 || m > 59)
		return -1.0f;
	time_t now = time(NULL);
	struct tm lt;
	localtime_s(&lt, &now);
	int target = h * 3600 + m * 60;
	int cur = lt.tm_hour * 3600 + lt.tm_min * 60 + lt.tm_sec;
	int diff = target - cur;
	if (diff < 0)
		diff += 24 * 3600;
	return (float)diff;
}

static void us_tick(void *data, float dt)
{
	struct upd *a = data;
	a->t += dt;
	float x = 0.0f;
	a->remaining = -1.0f;
	if (a->mode == 1) {
		x = a->percent_manual / 100.0f;
		a->pct = x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x);
		return;
	}
	if (a->mode == 2) {
		float rem = seconds_until_clock(a->target);
		if (rem < 0.0f) {
			x = 0.0f;
		} else {
			a->remaining = rem;
			x = 1.0f - rem / a->duration;
			if (x < 0.0f)
				x = 0.0f;
			if (rem > 12.0f * 3600.0f) /* just passed the time: show as finished */
				x = 1.0f, a->remaining = 0.0f;
		}
	} else {
		float el = (float)((double)(os_gettime_ns() - a->start_ns) / 1e9);
		a->remaining = a->duration - el;
		if (a->remaining < 0.0f)
			a->remaining = 0.0f;
		x = el / a->duration;
	}
	if (x > 1.0f)
		x = 1.0f;
	a->pct = a->realistic ? realistic_curve(x) : x;
	if (a->remaining <= 0.0f && a->mode != 1)
		a->pct = 1.0f;
}

/* -------------------------------------------------------------- text items */

static void item_set(struct item *it, int idx, const char *str, const char *font, int px, bool bold, uint32_t color,
		     int wrap_w, float x, float y, bool center)
{
	char key[512];
	snprintf(key, sizeof(key), "%s|%s|%d|%d|%08x|%d", str ? str : "", font, px, bold ? 1 : 0, color, wrap_w);
	UNUSED_PARAMETER(idx);
	it->x = x;
	it->y = y;
	it->center = center;
	it->visible = str && *str;
	if (!it->visible)
		return;
	if (strcmp(key, it->key) == 0 && it->tex)
		return;
	strncpy(it->key, key, sizeof(it->key) - 1);
	gs_texture_t *old = it->tex;
	if (wrap_w > 0) {
		it->tex = win_frame_render_text_wrapped(NULL, 0, NULL, str, font, px, wrap_w, 6, 0, 0, color, -1,
							&it->w, &it->h);
	} else {
		it->tex = win_frame_render_text_texture(str, font, px, bold, color, &it->w, &it->h);
	}
	if (old) {
		obs_enter_graphics();
		gs_texture_destroy(old);
		obs_leave_graphics();
	}
}

struct layout {
	float dlg[4], bar[4], btn[4], spin[4];
	float tint[3];
};

static void set_rect(float *r, float x, float y, float w, float h)
{
	r[0] = x;
	r[1] = y;
	r[2] = w;
	r[3] = h;
}

/* Builds/updates the text items and returns the geometry the shader needs. */
static void us_layout(struct upd *a, struct layout *L)
{
	memset(L, 0, sizeof(*L));
	const float W = (float)a->width, H = (float)a->height;
	const float u = H / 1080.0f * a->scale;
	const int pc = (int)floorf(a->pct * 100.0f + 0.0001f);
	const bool done = a->pct >= 0.9999f;
	const char *title = a->title[0] ? a->title : def_title[a->version];
	const char *msg = a->message[0] ? a->message : def_msg[a->version];

	char pct_line[192], time_line[96];
	if (a->remaining >= 0.0f && a->show_time) {
		int s = (int)ceilf(a->remaining);
		if (s >= 3600)
			snprintf(time_line, sizeof(time_line), "Starting in %d:%02d:%02d", s / 3600, (s / 60) % 60, s % 60);
		else
			snprintf(time_line, sizeof(time_line), "Starting in %d:%02d", s / 60, s % 60);
		if (done)
			time_line[0] = 0;
	} else {
		time_line[0] = 0;
	}

	struct item *it = a->items;
	for (int i = 0; i < MAX_ITEMS; i++)
		it[i].visible = false;
	const uint32_t WHITE = 0xFFFFFFFFu, BLACK = 0xFF000000u;

	switch (a->version) {
	case 0: { /* Windows 2000 text-mode setup */
		char rule[80];
		size_t n = strlen(title);
		if (n > 78)
			n = 78;
		memset(rule, '=', n);
		rule[n] = 0;
		const int px = (int)(24.0f * u);
		item_set(&it[0], 0, title, "Lucida Console", px, false, WHITE, 0, 60 * u, 44 * u, false);
		item_set(&it[1], 1, rule, "Lucida Console", px, false, WHITE, 0, 60 * u, 44 * u + px * 1.3f, false);
		item_set(&it[2], 2, msg, "Lucida Console", px, false, WHITE, (int)(W - 120 * u), 60 * u, 44 * u + px * 3.2f,
			 false);
		set_rect(L->dlg, W * 0.5f - 400 * u, H * 0.58f - 90 * u, 800 * u, 180 * u);
		set_rect(L->bar, L->dlg[0] + 28 * u, L->dlg[1] + 92 * u, L->dlg[2] - 56 * u, 40 * u);
		if (done)
			snprintf(pct_line, sizeof(pct_line), "%s", a->finish[0] ? a->finish : "Setup is complete.");
		else
			snprintf(pct_line, sizeof(pct_line), "Setup is copying files...  %d%%", pc);
		item_set(&it[3], 3, pct_line, "Lucida Console", (int)(22 * u), false, BLACK, 0, L->dlg[0] + 28 * u,
			 L->dlg[1] + 32 * u, false);
		item_set(&it[4], 4, time_line, "Lucida Console", (int)(20 * u), false, BLACK, 0, 20 * u, H - 30 * u,
			 false);
		break;
	}
	case 1: { /* Windows 98 setup dialog */
		set_rect(L->dlg, W * 0.5f - 340 * u, H * 0.5f - 160 * u, 680 * u, 320 * u);
		set_rect(L->bar, L->dlg[0] + 32 * u, L->dlg[1] + 190 * u, L->dlg[2] - 64 * u, 36 * u);
		set_rect(L->btn, L->dlg[0] + L->dlg[2] - 32 * u - 130 * u, L->dlg[1] + 262 * u, 130 * u, 36 * u);
		const int px = (int)(21 * u);
		item_set(&it[0], 0, title, "Tahoma", (int)(20 * u), true, WHITE, 0, L->dlg[0] + 14 * u, L->dlg[1] + 9 * u,
			 false);
		item_set(&it[1], 1, msg, "Tahoma", px, false, BLACK, (int)(L->dlg[2] - 64 * u), L->dlg[0] + 32 * u,
			 L->dlg[1] + 66 * u, false);
		if (done)
			snprintf(pct_line, sizeof(pct_line), "%s", a->finish[0] ? a->finish : "Done");
		else
			snprintf(pct_line, sizeof(pct_line), "%d%% complete", pc);
		item_set(&it[2], 2, pct_line, "Tahoma", px, false, BLACK, 0, L->dlg[0] + 32 * u, L->dlg[1] + 236 * u, false);
		item_set(&it[3], 3, "Cancel", "Tahoma", px, false, 0xFF808080u, 0, L->btn[0] + 36 * u, L->btn[1] + 6 * u,
			 false);
		item_set(&it[4], 4, time_line, "Tahoma", px, false, WHITE, 0, 20 * u, H - 40 * u, false);
		break;
	}
	case 2: { /* Windows XP */
		set_rect(L->bar, W * 0.5f - 300 * u, H * 0.56f, 600 * u, 34 * u);
		item_set(&it[0], 0, title, "Trebuchet MS", (int)(50 * u), true, WHITE, 0, W * 0.5f, H * 0.30f, true);
		item_set(&it[1], 1, msg, "Tahoma", (int)(24 * u), false, WHITE, 0, W * 0.5f, H * 0.30f + 74 * u, true);
		snprintf(pct_line, sizeof(pct_line), "%d%% complete", pc);
		item_set(&it[2], 2, done ? (a->finish[0] ? a->finish : "Done") : pct_line, "Tahoma", (int)(24 * u), false,
			 WHITE, 0, W * 0.5f, H * 0.56f + 52 * u, true);
		item_set(&it[3], 3, time_line, "Tahoma", (int)(22 * u), false, 0xFFE0E8FFu, 0, W * 0.5f, H * 0.90f, true);
		break;
	}
	case 3: { /* Vista / 7 */
		const int px = (int)(38 * u);
		float y = H * 0.36f;
		item_set(&it[0], 0, title, "Segoe UI", px, false, WHITE, 0, W * 0.5f, y, true);
		snprintf(pct_line, sizeof(pct_line), "%d%% complete", pc);
		item_set(&it[1], 1, done ? (a->finish[0] ? a->finish : "Done") : pct_line, "Segoe UI", px, false, WHITE,
			 0, W * 0.5f, y + 62 * u, true);
		item_set(&it[2], 2, msg, "Segoe UI", px, false, WHITE, 0, W * 0.5f, y + 124 * u, true);
		item_set(&it[3], 3, time_line, "Segoe UI", (int)(26 * u), false, 0xFFB0B0B0u, 0, W * 0.5f, H * 0.90f, true);
		break;
	}
	default: { /* Windows 8 / 10 / 11 */
		const float cy = (a->version == 6) ? H * 0.40f : H * 0.36f;
		const float R = (a->version == 6 ? 38.0f : 44.0f) * u;
		set_rect(L->spin, W * 0.5f, cy, R, (a->version == 4 ? 6.5f : 5.5f) * u);
		float ty = cy + R + 70 * u;
		const char *font = "Segoe UI Light";
		if (a->version == 6) {
			snprintf(pct_line, sizeof(pct_line), "%s  %d%%", title, pc);
			item_set(&it[0], 0, done ? title : pct_line, font, (int)(44 * u), false, WHITE, 0, W * 0.5f, ty, true);
			item_set(&it[1], 1, done ? (a->finish[0] ? a->finish : "Done") : msg, font, (int)(30 * u), false,
				 WHITE, 0, W * 0.5f, ty + 64 * u, true);
		} else {
			item_set(&it[0], 0, title, font, (int)(50 * u), false, WHITE, 0, W * 0.5f, ty, true);
			snprintf(pct_line, sizeof(pct_line), "%d%% complete", pc);
			item_set(&it[1], 1, done ? (a->finish[0] ? a->finish : "Done") : pct_line, font, (int)(36 * u), false,
				 WHITE, 0, W * 0.5f, ty + 78 * u, true);
			item_set(&it[2], 2, msg, font, (int)(30 * u), false, WHITE, 0, W * 0.5f, ty + 132 * u, true);
		}
		item_set(&it[3], 3, time_line, font, (int)(26 * u), false, 0xFFDCDCDCu, 0, W * 0.5f, H * 0.90f, true);
		if (a->version == 4) {
			L->tint[0] = 0.09f;
			L->tint[1] = 0.40f;
			L->tint[2] = 0.72f;
		} else if (a->version == 5) {
			uint32_t c = (uint32_t)a->accent;
			/* Windows 10: the accent colour, defaults to the familiar blue */
			L->tint[0] = (float)(c & 0xFF) / 255.0f;
			L->tint[1] = (float)((c >> 8) & 0xFF) / 255.0f;
			L->tint[2] = (float)((c >> 16) & 0xFF) / 255.0f;
		} else {
			L->tint[0] = 0.0f;
			L->tint[1] = 0.0f;
			L->tint[2] = 0.0f;
		}
		break;
	}
	}
}

static void set_v4(gs_eparam_t *p, const float *v)
{
	struct vec4 x;
	vec4_set(&x, v[0], v[1], v[2], v[3]);
	gs_effect_set_vec4(p, &x);
}

static void us_render(void *data, gs_effect_t *unused)
{
	UNUSED_PARAMETER(unused);
	struct upd *a = data;
	struct layout L;
	us_layout(a, &L);
	const float u = (float)a->height / 1080.0f * a->scale;

	const bool prev_srgb = gs_framebuffer_srgb_enabled();
	gs_enable_framebuffer_srgb(false);
	gs_blend_state_push();
	gs_blend_function_separate(GS_BLEND_SRCALPHA, GS_BLEND_INVSRCALPHA, GS_BLEND_ONE, GS_BLEND_INVSRCALPHA);

	struct vec2 v2;
	struct vec3 v3;
	vec2_set(&v2, (float)a->width, (float)a->height);
	gs_effect_set_vec2(a->p[UP_quad_size], &v2);
	gs_effect_set_int(a->p[UP_ver], a->version);
	gs_effect_set_float(a->p[UP_pct], a->pct);
	gs_effect_set_float(a->p[UP_time], a->t);
	gs_effect_set_float(a->p[UP_u], u);
	set_v4(a->p[UP_dlg], L.dlg);
	set_v4(a->p[UP_bar], L.bar);
	set_v4(a->p[UP_btn], L.btn);
	set_v4(a->p[UP_spin], L.spin);
	vec3_set(&v3, L.tint[0], L.tint[1], L.tint[2]);
	gs_effect_set_vec3(a->p[UP_tint], &v3);
	while (gs_effect_loop(a->effect, "Draw"))
		gs_draw_sprite(NULL, 0, a->width, a->height);

	gs_effect_t *def = obs_get_base_effect(OBS_EFFECT_DEFAULT);
	gs_eparam_t *img = gs_effect_get_param_by_name(def, "image");
	for (int i = 0; i < MAX_ITEMS; i++) {
		struct item *it = &a->items[i];
		if (!it->visible || !it->tex)
			continue;
		float x = it->x - WF_TEXT_PAD, y = it->y - WF_TEXT_PAD;
		if (it->center)
			x = it->x - ((float)it->w * 0.5f);
		gs_effect_set_texture(img, it->tex);
		gs_matrix_push();
		gs_matrix_translate3f(floorf(x), floorf(y), 0.0f);
		while (gs_effect_loop(def, "Draw"))
			gs_draw_sprite(it->tex, 0, it->w, it->h);
		gs_matrix_pop();
	}

	gs_blend_state_pop();
	gs_enable_framebuffer_srgb(prev_srgb);
}

static uint32_t us_width(void *data)
{
	return ((struct upd *)data)->width;
}

static uint32_t us_height(void *data)
{
	return ((struct upd *)data)->height;
}

struct obs_source_info win_update_source_info = {
	.id = "win_update_screen",
	.type = OBS_SOURCE_TYPE_INPUT,
	.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_CUSTOM_DRAW,
	.get_name = us_get_name,
	.create = us_create,
	.destroy = us_destroy,
	.update = us_update,
	.activate = us_activate,
	.get_defaults = us_defaults,
	.get_properties = us_properties,
	.video_tick = us_tick,
	.video_render = us_render,
	.get_width = us_width,
	.get_height = us_height,
	.icon_type = OBS_ICON_TYPE_CUSTOM,
};
