/* "Windows Notification Toasts" source.
 *
 * Shows Windows-style pop-ups (XP / Vista-7 balloon, Windows 8, 10, 11) that
 * slide in from a corner of the source, stack, and slide out. Messages come
 * from the "Show test toast" button, from a text file the source watches
 * (each new line = one toast, e.g. a Stream Labels file), or from the optional
 * local web address (see wf-notify.h). */

#include <string.h>
#include <math.h>
#include <obs-module.h>
#include <util/platform.h>
#include <graphics/vec2.h>
#include <graphics/vec4.h>
#include "win-frame-text.h"
#include "wf-notify.h"

#define S_W "t_width"
#define S_H "t_height"
#define S_CORNER "t_corner"
#define S_STYLE "t_style"
#define S_SECONDS "t_seconds"
#define S_MAX "t_max"
#define S_SCALE "t_scale"
#define S_DARK "t_dark"
#define S_ACCENT "t_accent"
#define S_APP "t_app"
#define S_FILE "t_file"
#define S_INJECT "t_inject"

#define MAX_TOASTS 4

#define TOAST_PARAMS(X) \
	X(text_tex) X(quad_size) X(card) X(style) X(alpha) X(kind) X(dark) X(accent) X(text_pos) X(text_size) \
	X(icon) X(ui) X(tail)

enum {
#define X(n) TP_##n,
	TOAST_PARAMS(X)
#undef X
	TP_COUNT
};

struct toast {
	bool alive;
	uint64_t born_ns;
	struct wf_msg msg;
	int kind;
	gs_texture_t *tex;
	uint32_t tw, th;
	float cw, ch; /* card size */
	float y_cur;
	bool y_init;
	float icon_x, icon_y, icon_size;
	float text_x, text_y;
};

struct toast_src {
	obs_source_t *source;
	gs_effect_t *effect;
	gs_eparam_t *p[TP_COUNT];

	uint32_t width, height;
	int corner, style, max_toasts;
	float seconds, scale;
	bool dark;
	long long accent;
	char app[64];

	struct wf_filetail tail;
	uint64_t last_poll_ns;
	struct toast items[MAX_TOASTS];
	int test_index;
	bool http_acquired;
};

static const char *samples[5][3] = {
	{"follow", "New follower", "Oskar just followed the stream!"},
	{"sub", "New subscriber", "Alex subscribed for 3 months"},
	{"donation", "Donation received", "Sam donated $5.00: \"Great stream!\""},
	{"chat", "Chat", "Riley: this looks just like Windows XP"},
	{"info", "Starting soon", "The stream begins in a few minutes."},
};

static const char *ts_get_name(void *unused)
{
	UNUSED_PARAMETER(unused);
	return obs_module_text("Toast.Name");
}

static int kind_of(const char *t)
{
	if (!strcmp(t, "follow") || !strcmp(t, "follower"))
		return 1;
	if (!strcmp(t, "sub") || !strcmp(t, "subscribe") || !strcmp(t, "subscriber") || !strcmp(t, "member"))
		return 2;
	if (!strcmp(t, "donation") || !strcmp(t, "donate") || !strcmp(t, "tip") || !strcmp(t, "bits"))
		return 3;
	if (!strcmp(t, "chat") || !strcmp(t, "message"))
		return 4;
	return 0;
}

static void ts_update(void *data, obs_data_t *s)
{
	struct toast_src *t = data;
	t->width = (uint32_t)obs_data_get_int(s, S_W);
	t->height = (uint32_t)obs_data_get_int(s, S_H);
	if (t->width < 320)
		t->width = 320;
	if (t->height < 240)
		t->height = 240;
	t->corner = (int)obs_data_get_int(s, S_CORNER);
	t->style = (int)obs_data_get_int(s, S_STYLE);
	if (t->style < 0 || t->style > 4)
		t->style = 3;
	t->max_toasts = (int)obs_data_get_int(s, S_MAX);
	if (t->max_toasts < 1)
		t->max_toasts = 1;
	if (t->max_toasts > MAX_TOASTS)
		t->max_toasts = MAX_TOASTS;
	t->seconds = (float)obs_data_get_double(s, S_SECONDS);
	if (t->seconds < 2.0f)
		t->seconds = 2.0f;
	t->scale = (float)obs_data_get_double(s, S_SCALE);
	if (t->scale < 0.4f)
		t->scale = 1.0f;
	t->dark = obs_data_get_bool(s, S_DARK);
	t->accent = obs_data_get_int(s, S_ACCENT);
	strncpy(t->app, obs_data_get_string(s, S_APP), sizeof(t->app) - 1);
	t->app[sizeof(t->app) - 1] = 0;
	wf_filetail_set(&t->tail, obs_data_get_string(s, S_FILE));

	const char *inj = obs_data_get_string(s, S_INJECT);
	if (inj && *inj) { /* used by the test tools and the menu's "send test toast" */
		struct wf_msg m;
		if (wf_msg_parse_line(inj, &m))
			wf_notify_push(WF_TARGET_TOAST, &m);
		obs_data_set_string(s, S_INJECT, "");
	}
}

static void *ts_create(obs_data_t *settings, obs_source_t *source)
{
	struct toast_src *t = bzalloc(sizeof(*t));
	t->source = source;

	char *path = obs_module_file("effects/toast.effect");
	char *err = NULL;
	obs_enter_graphics();
	t->effect = gs_effect_create_from_file(path, &err);
	obs_leave_graphics();
	bfree(path);
	if (!t->effect) {
		blog(LOG_ERROR, "[win-frame-filter] toast effect failed: %s", err ? err : "unknown");
		bfree(err);
		bfree(t);
		return NULL;
	}
	bfree(err);
	int i = 0;
#define X(n) t->p[i++] = gs_effect_get_param_by_name(t->effect, #n);
	TOAST_PARAMS(X)
#undef X

	ts_update(t, settings);
	wf_notify_http_acquire();
	t->http_acquired = true;
	return t;
}

static void toast_free(struct toast *it)
{
	if (it->tex) {
		obs_enter_graphics();
		gs_texture_destroy(it->tex);
		obs_leave_graphics();
		it->tex = NULL;
	}
	it->alive = false;
}

static void ts_destroy(void *data)
{
	struct toast_src *t = data;
	for (int i = 0; i < MAX_TOASTS; i++)
		toast_free(&t->items[i]);
	if (t->http_acquired)
		wf_notify_http_release();
	obs_enter_graphics();
	if (t->effect)
		gs_effect_destroy(t->effect);
	obs_leave_graphics();
	bfree(t);
}

static void ts_defaults(obs_data_t *s)
{
	obs_data_set_default_int(s, S_W, 1920);
	obs_data_set_default_int(s, S_H, 1080);
	obs_data_set_default_int(s, S_CORNER, 0);
	obs_data_set_default_int(s, S_STYLE, 3);
	obs_data_set_default_double(s, S_SECONDS, 6.0);
	obs_data_set_default_int(s, S_MAX, 3);
	obs_data_set_default_double(s, S_SCALE, 1.0);
	obs_data_set_default_bool(s, S_DARK, true);
	obs_data_set_default_int(s, S_ACCENT, (int)0xFFD77800u);
	obs_data_set_default_string(s, S_APP, "Stream Alerts");
}

static bool test_clicked(obs_properties_t *props, obs_property_t *p, void *data)
{
	UNUSED_PARAMETER(props);
	UNUSED_PARAMETER(p);
	struct toast_src *t = data;
	const char **s = samples[t->test_index++ % 5];
	struct wf_msg m;
	memset(&m, 0, sizeof(m));
	strcpy(m.type, s[0]);
	strncpy(m.title, s[1], sizeof(m.title) - 1);
	strncpy(m.text, s[2], sizeof(m.text) - 1);
	wf_notify_push(WF_TARGET_TOAST, &m);
	return false;
}

static obs_properties_t *ts_properties(void *data)
{
	obs_properties_t *p = obs_properties_create();
	obs_properties_add_button2(p, "t_test", obs_module_text("Toast.Test"), test_clicked, data);
	obs_property_t *st = obs_properties_add_list(p, S_STYLE, obs_module_text("Toast.Style"), OBS_COMBO_TYPE_LIST,
						     OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(st, obs_module_text("Toast.S0"), 0);
	obs_property_list_add_int(st, obs_module_text("Toast.S1"), 1);
	obs_property_list_add_int(st, obs_module_text("Toast.S2"), 2);
	obs_property_list_add_int(st, obs_module_text("Toast.S3"), 3);
	obs_property_list_add_int(st, obs_module_text("Toast.S4"), 4);
	obs_property_t *co = obs_properties_add_list(p, S_CORNER, obs_module_text("Toast.Corner"), OBS_COMBO_TYPE_LIST,
						     OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(co, obs_module_text("Toast.C0"), 0);
	obs_property_list_add_int(co, obs_module_text("Toast.C1"), 1);
	obs_property_list_add_int(co, obs_module_text("Toast.C2"), 2);
	obs_property_list_add_int(co, obs_module_text("Toast.C3"), 3);
	obs_properties_add_float_slider(p, S_SECONDS, obs_module_text("Toast.Seconds"), 2.0, 20.0, 0.5);
	obs_properties_add_int_slider(p, S_MAX, obs_module_text("Toast.Max"), 1, MAX_TOASTS, 1);
	obs_properties_add_float_slider(p, S_SCALE, obs_module_text("Toast.Scale"), 0.5, 2.0, 0.05);
	obs_properties_add_bool(p, S_DARK, obs_module_text("Toast.Dark"));
	obs_properties_add_color(p, S_ACCENT, obs_module_text("Toast.Accent"));
	obs_properties_add_text(p, S_APP, obs_module_text("Toast.App"), OBS_TEXT_DEFAULT);
	obs_properties_add_path(p, S_FILE, obs_module_text("Toast.File"), OBS_PATH_FILE, "Text (*.txt *.log);;All (*.*)",
				NULL);
	obs_properties_add_int(p, S_W, obs_module_text("Toast.Width"), 320, 8192, 1);
	obs_properties_add_int(p, S_H, obs_module_text("Toast.Height"), 240, 8192, 1);
	return p;
}

/* ------------------------------------------------------------- toast life */

struct style_layout {
	float card_w, pad_x, pad_y, icon, gap;
	int px;
	const char *font;
	bool header;
	uint32_t title, body, head;
};

static struct style_layout layout_for(int style, bool dark)
{
	struct style_layout l;
	memset(&l, 0, sizeof(l));
	switch (style) {
	case 0:
		l = (struct style_layout){320, 10, 10, 16, 6, 12, "Tahoma", false, 0xFF000000u, 0xFF000000u, 0};
		break;
	case 1:
		l = (struct style_layout){340, 12, 12, 24, 8, 13, "Segoe UI", false, 0xFF33241Cu, 0xFF463830u, 0};
		break;
	case 2:
		l = (struct style_layout){360, 14, 12, 28, 12, 14, "Segoe UI", false, 0xFFFFFFFFu, 0xFFFFFFFFu, 0};
		break;
	case 3:
		l = (struct style_layout){364, 12, 10, 16, 6, 13, "Segoe UI", true,
					  dark ? 0xFFFFFFFFu : 0xFF000000u, dark ? 0xFFD0D0D0u : 0xFF3C3C3Cu,
					  dark ? 0xFFB0B0B0u : 0xFF6B6B6Bu};
		break;
	default:
		l = (struct style_layout){372, 14, 12, 18, 6, 13, "Segoe UI", true,
					  dark ? 0xFFFFFFFFu : 0xFF1A1A1Au, dark ? 0xFFCCCCCCu : 0xFF444444u,
					  dark ? 0xFFADADADu : 0xFF6E6E6Eu};
		break;
	}
	return l;
}

static void toast_start(struct toast_src *t, struct toast *it, const struct wf_msg *m)
{
	memset(it, 0, sizeof(*it));
	it->alive = true;
	it->born_ns = os_gettime_ns();
	it->msg = *m;
	it->kind = kind_of(m->type);

	const float ui = t->scale;
	struct style_layout l = layout_for(t->style, t->dark);
	const float card_w = floorf(l.card_w * ui);
	const float pad_x = floorf(l.pad_x * ui), pad_y = floorf(l.pad_y * ui);
	const float icon = floorf(l.icon * ui);
	float text_x;
	int header_indent = 0;
	if (l.header) {
		text_x = pad_x;
		header_indent = (int)(icon + floorf(6.0f * ui));
	} else {
		text_x = pad_x + icon + floorf(l.gap * ui);
	}
	int max_w = (int)(card_w - text_x - pad_x);
	it->tex = win_frame_render_text_wrapped(l.header ? (t->app[0] ? t->app : "Notification") : NULL, header_indent,
						m->title, m->text, l.font, (int)floorf(l.px * ui), max_w, 3, l.head, l.title,
						l.body, -1, &it->tw, &it->th);
	float text_h = it->th ? (float)it->th - 2.0f * WF_TEXT_PAD : 16.0f;
	float body_h = text_h + 2.0f * pad_y;
	if (!l.header && body_h < icon + 2.0f * pad_y)
		body_h = icon + 2.0f * pad_y;
	it->cw = card_w;
	it->ch = floorf(body_h);
	it->text_x = text_x;
	it->text_y = pad_y;
	it->icon_size = icon;
	it->icon_x = pad_x;
	it->icon_y = l.header ? pad_y + floorf(1.0f * ui) : pad_y;
}

static void ts_tick(void *data, float seconds)
{
	UNUSED_PARAMETER(seconds);
	struct toast_src *t = data;
	uint64_t now = os_gettime_ns();

	if (now - t->last_poll_ns > 400000000ULL) {
		t->last_poll_ns = now;
		wf_filetail_poll(&t->tail, WF_TARGET_TOAST);
	}

	const uint64_t life = (uint64_t)(t->seconds * 1e9);
	for (int i = 0; i < MAX_TOASTS; i++)
		if (t->items[i].alive && now - t->items[i].born_ns > life)
			toast_free(&t->items[i]);

	int active = 0;
	for (int i = 0; i < MAX_TOASTS; i++)
		if (t->items[i].alive)
			active++;
	while (active < t->max_toasts) {
		struct wf_msg m;
		if (!wf_notify_pop(WF_TARGET_TOAST, &m))
			break;
		for (int i = 0; i < MAX_TOASTS; i++) {
			if (!t->items[i].alive) {
				toast_start(t, &t->items[i], &m);
				active++;
				break;
			}
		}
	}
}

static float ease_out(float x)
{
	x = x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x);
	float u = 1.0f - x;
	return 1.0f - u * u * u;
}

static void set_v4(gs_eparam_t *p, float x, float y, float z, float w)
{
	struct vec4 v;
	vec4_set(&v, x, y, z, w);
	gs_effect_set_vec4(p, &v);
}

static void ts_render(void *data, gs_effect_t *unused)
{
	UNUSED_PARAMETER(unused);
	struct toast_src *t = data;
	const uint64_t now = os_gettime_ns();
	const float ui = t->scale;
	const float M = floorf(22.0f * ui);
	const float edge = floorf(24.0f * ui);
	const float gap = floorf(10.0f * ui);
	const float life = t->seconds;
	const bool right = (t->corner == 0 || t->corner == 2);
	const bool bottom = (t->corner == 0 || t->corner == 1);

	/* order slots newest-first from the corner */
	int order[MAX_TOASTS], n = 0;
	for (int pass = 0; pass < MAX_TOASTS; pass++) {
		int best = -1;
		for (int i = 0; i < MAX_TOASTS; i++) {
			if (!t->items[i].alive)
				continue;
			bool used = false;
			for (int k = 0; k < n; k++)
				if (order[k] == i)
					used = true;
			if (used)
				continue;
			if (best < 0 || t->items[i].born_ns > t->items[best].born_ns)
				best = i;
		}
		if (best >= 0)
			order[n++] = best;
	}

	const bool prev_srgb = gs_framebuffer_srgb_enabled();
	gs_enable_framebuffer_srgb(false);
	gs_blend_state_push();
	gs_blend_function_separate(GS_BLEND_SRCALPHA, GS_BLEND_INVSRCALPHA, GS_BLEND_ONE, GS_BLEND_INVSRCALPHA);

	float cursor = bottom ? (float)t->height - edge : edge;
	for (int k = 0; k < n; k++) {
		struct toast *it = &t->items[order[k]];
		float y_target = bottom ? cursor - it->ch : cursor;
		cursor += bottom ? -(it->ch + gap) : (it->ch + gap);
		if (!it->y_init) {
			it->y_cur = y_target;
			it->y_init = true;
		} else {
			it->y_cur += (y_target - it->y_cur) * 0.18f;
		}

		float age = (float)((double)(now - it->born_ns) / 1e9);
		float p_in = ease_out(age / 0.35f);
		float remain = life - age;
		float p_out = ease_out(remain / 0.35f);
		float slide = 1.0f - fminf(p_in, p_out);
		float travel = it->cw + edge + M;
		float x_card = right ? (float)t->width - edge - it->cw + slide * travel : edge - slide * travel;
		float alpha = fminf(p_in * 1.2f, p_out * 1.2f);
		if (alpha > 1.0f)
			alpha = 1.0f;

		float qw = it->cw + 2.0f * M, qh = it->ch + 2.0f * M;
		gs_matrix_push();
		gs_matrix_translate3f(x_card - M, it->y_cur - M, 0.0f);

		struct vec2 v2;
		if (it->tex)
			gs_effect_set_texture(t->p[TP_text_tex], it->tex);
		vec2_set(&v2, qw, qh);
		gs_effect_set_vec2(t->p[TP_quad_size], &v2);
		set_v4(t->p[TP_card], M, M, it->cw, it->ch);
		gs_effect_set_int(t->p[TP_style], t->style);
		gs_effect_set_float(t->p[TP_alpha], alpha);
		gs_effect_set_int(t->p[TP_kind], it->kind);
		gs_effect_set_int(t->p[TP_dark], t->dark ? 1 : 0);
		{
			uint32_t c = (uint32_t)t->accent;
			set_v4(t->p[TP_accent], (float)(c & 0xFF) / 255.0f, (float)((c >> 8) & 0xFF) / 255.0f,
			       (float)((c >> 16) & 0xFF) / 255.0f, 1.0f);
		}
		vec2_set(&v2, M + it->text_x - WF_TEXT_PAD, M + it->text_y - WF_TEXT_PAD);
		gs_effect_set_vec2(t->p[TP_text_pos], &v2);
		vec2_set(&v2, (float)(it->tw ? it->tw : 1), (float)(it->th ? it->th : 1));
		gs_effect_set_vec2(t->p[TP_text_size], &v2);
		{
			struct vec3 v3;
			vec3_set(&v3, M + it->icon_x, M + it->icon_y, it->icon_size);
			gs_effect_set_vec3(t->p[TP_icon], &v3);
		}
		gs_effect_set_float(t->p[TP_ui], ui);
		gs_effect_set_int(t->p[TP_tail], t->style <= 1 ? 1 : 0);

		while (gs_effect_loop(t->effect, "Draw"))
			gs_draw_sprite(NULL, 0, (uint32_t)qw, (uint32_t)qh);
		gs_matrix_pop();
	}

	gs_blend_state_pop();
	gs_enable_framebuffer_srgb(prev_srgb);
}

static uint32_t ts_width(void *data)
{
	return ((struct toast_src *)data)->width;
}

static uint32_t ts_height(void *data)
{
	return ((struct toast_src *)data)->height;
}

struct obs_source_info win_toast_source_info = {
	.id = "win_toast_source",
	.type = OBS_SOURCE_TYPE_INPUT,
	.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_CUSTOM_DRAW,
	.get_name = ts_get_name,
	.create = ts_create,
	.destroy = ts_destroy,
	.update = ts_update,
	.get_defaults = ts_defaults,
	.get_properties = ts_properties,
	.video_tick = ts_tick,
	.video_render = ts_render,
	.get_width = ts_width,
	.get_height = ts_height,
	.icon_type = OBS_ICON_TYPE_CUSTOM,
};
