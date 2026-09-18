/* "Paperclip Assistant" source: an original wire-paperclip character with
 * googly eyes that idles, blinks, looks around and pops up a speech balloon
 * with typewriter text. Messages come from a list of tips on a timer, from
 * the "Say something" button, from a watched text file, or from the optional
 * local web address (see wf-notify.h). */

#include <string.h>
#include <math.h>
#include <obs-module.h>
#include <util/platform.h>
#include <graphics/vec2.h>
#include <graphics/vec3.h>
#include <graphics/vec4.h>
#include "win-frame-text.h"
#include "wf-notify.h"

#define S_W "a_width"
#define S_H "a_height"
#define S_THEME "a_theme"
#define S_SCALE "a_scale"
#define S_INTERVAL "a_interval"
#define S_TITLE "a_title"
#define S_TIPS "a_tips"
#define S_FILE "a_file"
#define S_INJECT "a_inject"

#define ASSIST_PARAMS(X) \
	X(text_tex) X(quad_size) X(char_box) X(pose) X(eyes) X(bubble) X(apex) X(text_pos) X(text_size) X(balpha) \
	X(theme) X(ui)

enum {
#define X(n) AP_##n,
	ASSIST_PARAMS(X)
#undef X
	AP_COUNT
};

#define TYPE_CPS 42.0f /* typewriter speed, characters per second */

struct assist {
	obs_source_t *source;
	gs_effect_t *effect;
	gs_eparam_t *p[AP_COUNT];

	uint32_t width, height;
	int theme;
	float scale, interval;
	char title[96];
	char *tips;

	struct wf_filetail tail;
	uint64_t last_poll_ns;
	bool http_acquired;

	/* animation */
	float t;
	float next_blink, blink_t;
	float next_look, look_x, look_y, look_tx, look_ty;
	float hop, mood;
	uint32_t rng;
	int tip_index;
	float tip_timer;

	/* current balloon */
	bool active;
	float msg_t, msg_hold, msg_type_time;
	struct wf_msg msg;
	int body_len, shown;
	gs_texture_t *tex;
	uint32_t tw, th;
	float bw, bh; /* balloon size (px) */
};

static const char *default_tips =
	"It looks like you're streaming. Would you like help with that?\n"
	"Tip: sip some water. Your viewers want you hydrated.\n"
	"It looks like you've been sitting for a while. Stand up and stretch!\n"
	"Did you remember to unmute your microphone?\n"
	"Tip: say hi to new people in chat.\n"
	"It looks like you're about to say \"um\". Would you like me to stop you?\n"
	"Don't forget to follow, subscribe and ring the tiny bell!";

static const char *as_get_name(void *unused)
{
	UNUSED_PARAMETER(unused);
	return obs_module_text("Assistant.Name");
}

static float frand(struct assist *a)
{
	a->rng = a->rng * 1664525u + 1013904223u;
	return (float)((a->rng >> 8) & 0xFFFF) / 65535.0f;
}

static void as_update(void *data, obs_data_t *s)
{
	struct assist *a = data;
	a->width = (uint32_t)obs_data_get_int(s, S_W);
	a->height = (uint32_t)obs_data_get_int(s, S_H);
	if (a->width < 320)
		a->width = 320;
	if (a->height < 320)
		a->height = 320;
	a->theme = (int)obs_data_get_int(s, S_THEME);
	if (a->theme < 0 || a->theme > 1)
		a->theme = 0;
	a->scale = (float)obs_data_get_double(s, S_SCALE);
	if (a->scale < 0.4f)
		a->scale = 1.0f;
	a->interval = (float)obs_data_get_double(s, S_INTERVAL);
	strncpy(a->title, obs_data_get_string(s, S_TITLE), sizeof(a->title) - 1);
	a->title[sizeof(a->title) - 1] = 0;
	bfree(a->tips);
	const char *tips = obs_data_get_string(s, S_TIPS);
	a->tips = bstrdup(tips && *tips ? tips : default_tips);
	wf_filetail_set(&a->tail, obs_data_get_string(s, S_FILE));

	const char *inj = obs_data_get_string(s, S_INJECT);
	if (inj && *inj) {
		struct wf_msg m;
		if (wf_msg_parse_line(inj, &m))
			wf_notify_push(WF_TARGET_ASSISTANT, &m);
		obs_data_set_string(s, S_INJECT, "");
	}
}

static void *as_create(obs_data_t *settings, obs_source_t *source)
{
	struct assist *a = bzalloc(sizeof(*a));
	a->source = source;
	a->rng = (uint32_t)(os_gettime_ns() & 0xFFFFFFFFu) | 1u;

	char *path = obs_module_file("effects/assistant.effect");
	char *err = NULL;
	obs_enter_graphics();
	a->effect = gs_effect_create_from_file(path, &err);
	obs_leave_graphics();
	bfree(path);
	if (!a->effect) {
		blog(LOG_ERROR, "[win-frame-filter] assistant effect failed: %s", err ? err : "unknown");
		bfree(err);
		bfree(a);
		return NULL;
	}
	bfree(err);
	int i = 0;
#define X(n) a->p[i++] = gs_effect_get_param_by_name(a->effect, #n);
	ASSIST_PARAMS(X)
#undef X

	a->next_blink = 1.5f + frand(a) * 2.5f;
	a->next_look = 1.0f;
	as_update(a, settings);
	wf_notify_http_acquire();
	a->http_acquired = true;
	return a;
}

static void free_tex(struct assist *a)
{
	if (a->tex) {
		obs_enter_graphics();
		gs_texture_destroy(a->tex);
		obs_leave_graphics();
		a->tex = NULL;
	}
}

static void as_destroy(void *data)
{
	struct assist *a = data;
	free_tex(a);
	if (a->http_acquired)
		wf_notify_http_release();
	obs_enter_graphics();
	if (a->effect)
		gs_effect_destroy(a->effect);
	obs_leave_graphics();
	bfree(a->tips);
	bfree(a);
}

static void as_defaults(obs_data_t *s)
{
	obs_data_set_default_int(s, S_W, 640);
	obs_data_set_default_int(s, S_H, 480);
	obs_data_set_default_int(s, S_THEME, 0);
	obs_data_set_default_double(s, S_SCALE, 1.0);
	obs_data_set_default_double(s, S_INTERVAL, 45.0);
	obs_data_set_default_string(s, S_TITLE, "Paperclip");
	obs_data_set_default_string(s, S_TIPS, default_tips);
}

static const char *nth_tip(struct assist *a, char *out, size_t n);

static bool say_clicked(obs_properties_t *props, obs_property_t *p, void *data)
{
	UNUSED_PARAMETER(props);
	UNUSED_PARAMETER(p);
	struct assist *a = data;
	struct wf_msg m;
	memset(&m, 0, sizeof(m));
	strcpy(m.type, "tip");
	nth_tip(a, m.text, sizeof(m.text));
	if (!m.text[0])
		strcpy(m.text, "It looks like you're streaming. Would you like help with that?");
	wf_notify_push(WF_TARGET_ASSISTANT, &m);
	return false;
}

static obs_properties_t *as_properties(void *data)
{
	obs_properties_t *p = obs_properties_create();
	obs_properties_add_button2(p, "a_say", obs_module_text("Assistant.Say"), say_clicked, data);
	obs_property_t *th = obs_properties_add_list(p, S_THEME, obs_module_text("Assistant.Theme"),
						     OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(th, obs_module_text("Assistant.T0"), 0);
	obs_property_list_add_int(th, obs_module_text("Assistant.T1"), 1);
	obs_properties_add_float_slider(p, S_INTERVAL, obs_module_text("Assistant.Interval"), 0.0, 300.0, 5.0);
	obs_properties_add_text(p, S_TITLE, obs_module_text("Assistant.Title"), OBS_TEXT_DEFAULT);
	obs_properties_add_text(p, S_TIPS, obs_module_text("Assistant.Tips"), OBS_TEXT_MULTILINE);
	obs_properties_add_path(p, S_FILE, obs_module_text("Assistant.File"), OBS_PATH_FILE,
				"Text (*.txt *.log);;All (*.*)", NULL);
	obs_properties_add_float_slider(p, S_SCALE, obs_module_text("Assistant.Scale"), 0.5, 2.0, 0.05);
	obs_properties_add_int(p, S_W, obs_module_text("Assistant.Width"), 320, 8192, 1);
	obs_properties_add_int(p, S_H, obs_module_text("Assistant.Height"), 320, 8192, 1);
	return p;
}

/* ------------------------------------------------------------- balloon */

static void render_balloon_text(struct assist *a, int reveal)
{
	const float ui = a->scale;
	const bool classic = (a->theme == 0);
	const int px = (int)floorf((classic ? 13.0f : 14.0f) * ui);
	const int max_w = (int)(a->bw - 2.0f * floorf(14.0f * ui));
	gs_texture_t *old = a->tex;
	uint32_t w = 0, h = 0;
	a->tex = win_frame_render_text_wrapped(NULL, 0, a->msg.title, a->msg.text, classic ? "Tahoma" : "Segoe UI", px,
					       max_w, 6, 0, classic ? 0xFF000000u : 0xFFFFFFFFu,
					       classic ? 0xFF000000u : 0xFFD8D8D8u, reveal, &w, &h);
	if (old) {
		obs_enter_graphics();
		gs_texture_destroy(old);
		obs_leave_graphics();
	}
	a->tw = w;
	a->th = h;
	a->bh = (float)h - 2.0f * WF_TEXT_PAD + 2.0f * floorf(11.0f * ui);
}

static void start_message(struct assist *a, const struct wf_msg *m)
{
	a->msg = *m;
	if (!a->msg.title[0])
		strncpy(a->msg.title, a->title, sizeof(a->msg.title) - 1);
	a->body_len = (int)strlen(a->msg.text);
	a->shown = 0;
	a->active = true;
	a->msg_t = 0.0f;
	a->msg_type_time = (float)a->body_len / TYPE_CPS;
	a->msg_hold = 3.5f + 0.045f * (float)a->body_len;
	a->hop = 1.0f;
	a->mood = 1.0f;
	a->bw = floorf(300.0f * a->scale);
	if (a->bw > (float)a->width - 16.0f * a->scale)
		a->bw = (float)a->width - 16.0f * a->scale;
	render_balloon_text(a, 0);
}

static const char *nth_tip(struct assist *a, char *out, size_t n)
{
	int total = 0;
	for (const char *c = a->tips; *c;) {
		const char *e = strchr(c, '\n');
		size_t len = e ? (size_t)(e - c) : strlen(c);
		if (len)
			total++;
		c += len + (e ? 1 : 0);
	}
	out[0] = 0;
	if (!total)
		return out;
	int want = a->tip_index++ % total, k = 0;
	for (const char *c = a->tips; *c;) {
		const char *e = strchr(c, '\n');
		size_t len = e ? (size_t)(e - c) : strlen(c);
		if (len && k++ == want) {
			if (len > n - 1)
				len = n - 1;
			memcpy(out, c, len);
			out[len] = 0;
			if (len && out[len - 1] == '\r')
				out[len - 1] = 0;
			break;
		}
		c += len + (e ? 1 : 0);
	}
	return out;
}

static void as_tick(void *data, float dt)
{
	struct assist *a = data;
	if (dt > 0.25f)
		dt = 0.25f;
	a->t += dt;

	uint64_t now = os_gettime_ns();
	if (now - a->last_poll_ns > 400000000ULL) {
		a->last_poll_ns = now;
		wf_filetail_poll(&a->tail, WF_TARGET_ASSISTANT);
	}

	/* --- message life cycle */
	if (a->active) {
		a->msg_t += dt;
		int want = 0;
		float type_start = 0.25f;
		if (a->msg_t > type_start)
			want = (int)((a->msg_t - type_start) * TYPE_CPS);
		if (want > a->body_len)
			want = a->body_len;
		if (want != a->shown) {
			a->shown = want;
			render_balloon_text(a, want >= a->body_len ? -1 : want);
		}
		float total = type_start + a->msg_type_time + a->msg_hold + 0.3f;
		if (a->msg_t > total) {
			a->active = false;
			free_tex(a);
			a->tip_timer = 0.0f;
		}
	} else {
		struct wf_msg m;
		if (wf_notify_pop(WF_TARGET_ASSISTANT, &m)) {
			start_message(a, &m);
		} else if (a->interval >= 5.0f) {
			a->tip_timer += dt;
			if (a->tip_timer >= a->interval) {
				a->tip_timer = 0.0f;
				memset(&m, 0, sizeof(m));
				strcpy(m.type, "tip");
				nth_tip(a, m.text, sizeof(m.text));
				if (m.text[0])
					start_message(a, &m);
			}
		}
	}

	/* --- idle animation */
	if (a->blink_t > 0.0f) {
		a->blink_t -= dt;
		if (a->blink_t < 0.0f)
			a->blink_t = 0.0f;
	} else {
		a->next_blink -= dt;
		if (a->next_blink <= 0.0f) {
			a->blink_t = 0.16f;
			a->next_blink = 2.0f + frand(a) * 3.5f;
			if (frand(a) < 0.2f)
				a->next_blink = 0.25f; /* occasional double blink */
		}
	}
	a->next_look -= dt;
	if (a->next_look <= 0.0f) {
		a->next_look = 1.2f + frand(a) * 2.5f;
		a->look_tx = (frand(a) - 0.5f) * 2.0f;
		a->look_ty = (frand(a) - 0.5f) * 1.4f;
	}
	float tx = a->look_tx, ty = a->look_ty;
	if (a->active) { /* read along with the balloon */
		tx = -0.6f;
		ty = -0.8f;
	}
	float k = 1.0f - expf(-dt * 8.0f);
	a->look_x += (tx - a->look_x) * k;
	a->look_y += (ty - a->look_y) * k;
	a->hop -= dt * 2.2f;
	if (a->hop < 0.0f)
		a->hop = 0.0f;
	a->mood -= dt * 0.7f;
	if (a->mood < 0.0f)
		a->mood = 0.0f;
}

static void set_v4(gs_eparam_t *p, float x, float y, float z, float w)
{
	struct vec4 v;
	vec4_set(&v, x, y, z, w);
	gs_effect_set_vec4(p, &v);
}

static void as_render(void *data, gs_effect_t *unused)
{
	UNUSED_PARAMETER(unused);
	struct assist *a = data;
	const float ui = a->scale;
	const float W = (float)a->width, H = (float)a->height;
	const float S = 200.0f * ui;

	/* character pose */
	float hop = sinf(fminf(a->hop, 1.0f) * 3.14159265f); /* 0..1..0 as hop goes 1 -> 0 */
	float hop_h = hop * 26.0f * ui * (a->hop > 0.0f ? 1.0f : 0.0f);
	float cx = W - 14.0f * ui - 0.19f * S;
	float cy = H - 10.0f * ui - 0.405f * S - hop_h;
	float wiggle = sinf(a->t * 1.3f) * 0.018f + sinf(a->t * 0.7f) * 0.012f;
	if (a->hop > 0.0f)
		wiggle += sinf(a->hop * 14.0f) * 0.06f * a->hop;
	float sq_y = 1.0f + 0.012f * sinf(a->t * 2.1f) + 0.06f * hop;
	float sq_x = 1.0f - 0.03f * hop;
	float blink = a->blink_t > 0.0f ? sinf((1.0f - a->blink_t / 0.16f) * 3.14159265f) : 0.0f;

	/* balloon geometry: sits above the character, right-aligned */
	float alpha = 0.0f, slide = 0.0f;
	float apex_x = cx + 0.03f * S, apex_y = cy - 0.43f * S - 4.0f * ui;
	float bx = 0, by = 0;
	if (a->active && a->tex) {
		float total_out = 0.3f;
		float end = 0.25f + a->msg_type_time + a->msg_hold;
		float fin = fminf(a->msg_t / 0.25f, 1.0f);
		float fout = a->msg_t > end ? fmaxf(1.0f - (a->msg_t - end) / total_out, 0.0f) : 1.0f;
		alpha = fminf(fin, fout);
		slide = (1.0f - alpha) * 10.0f * ui;
		bx = W - 8.0f * ui - a->bw;
		by = apex_y - 20.0f * ui - a->bh + slide;
	}

	const bool prev_srgb = gs_framebuffer_srgb_enabled();
	gs_enable_framebuffer_srgb(false);
	gs_blend_state_push();
	gs_blend_function_separate(GS_BLEND_SRCALPHA, GS_BLEND_INVSRCALPHA, GS_BLEND_ONE, GS_BLEND_INVSRCALPHA);

	struct vec2 v2;
	if (a->tex)
		gs_effect_set_texture(a->p[AP_text_tex], a->tex);
	vec2_set(&v2, W, H);
	gs_effect_set_vec2(a->p[AP_quad_size], &v2);
	{
		struct vec3 v3;
		vec3_set(&v3, cx, cy, S);
		gs_effect_set_vec3(a->p[AP_char_box], &v3);
	}
	set_v4(a->p[AP_pose], wiggle, sq_x, sq_y, a->t);
	set_v4(a->p[AP_eyes], blink, a->look_x, a->look_y, a->mood > 1.0f ? 1.0f : a->mood);
	set_v4(a->p[AP_bubble], bx, by, a->bw, a->bh);
	vec2_set(&v2, apex_x, apex_y);
	gs_effect_set_vec2(a->p[AP_apex], &v2);
	float pad = floorf(14.0f * ui);
	vec2_set(&v2, bx + pad - WF_TEXT_PAD, by + floorf(11.0f * ui) - WF_TEXT_PAD);
	gs_effect_set_vec2(a->p[AP_text_pos], &v2);
	vec2_set(&v2, (float)(a->tw ? a->tw : 1), (float)(a->th ? a->th : 1));
	gs_effect_set_vec2(a->p[AP_text_size], &v2);
	gs_effect_set_float(a->p[AP_balpha], alpha);
	gs_effect_set_int(a->p[AP_theme], a->theme);
	gs_effect_set_float(a->p[AP_ui], ui);

	while (gs_effect_loop(a->effect, "Draw"))
		gs_draw_sprite(NULL, 0, a->width, a->height);

	gs_blend_state_pop();
	gs_enable_framebuffer_srgb(prev_srgb);
}

static uint32_t as_width(void *data)
{
	return ((struct assist *)data)->width;
}

static uint32_t as_height(void *data)
{
	return ((struct assist *)data)->height;
}

struct obs_source_info win_assistant_source_info = {
	.id = "win_paperclip_assistant",
	.type = OBS_SOURCE_TYPE_INPUT,
	.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_CUSTOM_DRAW,
	.get_name = as_get_name,
	.create = as_create,
	.destroy = as_destroy,
	.update = as_update,
	.get_defaults = as_defaults,
	.get_properties = as_properties,
	.video_tick = as_tick,
	.video_render = as_render,
	.get_width = as_width,
	.get_height = as_height,
	.icon_type = OBS_ICON_TYPE_CUSTOM,
};
