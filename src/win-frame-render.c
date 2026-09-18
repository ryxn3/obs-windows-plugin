#include <string.h>
#include <math.h>
#include <graphics/vec2.h>
#include <graphics/vec4.h>
#include "win-frame-render.h"
#include "win-frame-internal.h"
#include "win-frame-filter.h"

static struct vec4 abgr_to_vec4(long long packed, float alpha_mult)
{
	uint32_t v = (uint32_t)packed;
	struct vec4 out;
	out.x = (float)(v & 0xFF) / 255.0f;
	out.y = (float)((v >> 8) & 0xFF) / 255.0f;
	out.z = (float)((v >> 16) & 0xFF) / 255.0f;
	out.w = (float)((v >> 24) & 0xFF) / 255.0f * alpha_mult;
	return out;
}

static gs_texture_t *get_dummy(struct win_frame_filter *f)
{
	if (!f->dummy_tex) {
		uint8_t px[4] = {0, 0, 0, 0};
		const uint8_t *data = px;
		f->dummy_tex = gs_texture_create(1, 1, GS_RGBA, 1, &data, 0);
	}
	return f->dummy_tex;
}

static void reload_image_if_needed(gs_image_file_t *img, bool *loaded, char *cached_path, size_t cached_sz,
				    const char *new_path)
{
	if (!new_path)
		new_path = "";
	if (strcmp(cached_path, new_path) == 0)
		return;

	if (*loaded) {
		obs_enter_graphics();
		gs_image_file_free(img);
		obs_leave_graphics();
		*loaded = false;
	}
	strncpy(cached_path, new_path, cached_sz - 1);
	cached_path[cached_sz - 1] = 0;

	if (new_path[0]) {
		gs_image_file_init(img, new_path);
		obs_enter_graphics();
		gs_image_file_init_texture(img);
		obs_leave_graphics();
		*loaded = img->texture != NULL;
	}
}

void win_frame_release_cached_assets(struct win_frame_filter *f)
{
	obs_enter_graphics();
	if (f->title_text_tex) {
		gs_texture_destroy(f->title_text_tex);
		f->title_text_tex = NULL;
	}
	if (f->status_text_tex) {
		gs_texture_destroy(f->status_text_tex);
		f->status_text_tex = NULL;
	}
	if (f->dummy_tex) {
		gs_texture_destroy(f->dummy_tex);
		f->dummy_tex = NULL;
	}
	if (f->icon_loaded)
		gs_image_file_free(&f->icon_img);
	if (f->mask_loaded)
		gs_image_file_free(&f->mask_img);
	if (f->bg_loaded)
		gs_image_file_free(&f->bg_img);
	if (f->logo_loaded)
		gs_image_file_free(&f->logo_img);
	obs_leave_graphics();
}

static void set_rect(float *dst, double x, double y, double w, double h)
{
	dst[0] = (float)x;
	dst[1] = (float)y;
	dst[2] = (float)w;
	dst[3] = (float)h;
}

static void refresh_text(struct win_frame_filter *f, gs_texture_t **tex, uint32_t *tw, uint32_t *th,
			 char *cached_text, size_t cached_sz, const char *text, const char *font, int size, bool bold,
			 uint32_t color, char *cached_font, size_t font_sz, int *cached_size, bool *cached_bold,
			 uint32_t *cached_color)
{
	if (!text)
		text = "";
	if (!font)
		font = "";
	bool dirty = strcmp(cached_text, text) != 0 || (cached_font && strcmp(cached_font, font) != 0) ||
		     *cached_size != size || (cached_bold && *cached_bold != bold) || *cached_color != color;
	if (!dirty)
		return;

	if (*tex) {
		obs_enter_graphics();
		gs_texture_destroy(*tex);
		obs_leave_graphics();
		*tex = NULL;
	}
	*tw = *th = 0;
	*tex = win_frame_render_text_texture(text, font, size, bold, color, tw, th);

	strncpy(cached_text, text, cached_sz - 1);
	cached_text[cached_sz - 1] = 0;
	if (cached_font) {
		strncpy(cached_font, font, font_sz - 1);
		cached_font[font_sz - 1] = 0;
	}
	*cached_size = size;
	if (cached_bold)
		*cached_bold = bold;
	*cached_color = color;
	(void)f;
}

/* ------------------------------------------------------------------------
 * Layout: settings -> canvas-pixel rectangles.  Runs from update() and when
 * the upstream resolution changes -- never per frame.
 * ---------------------------------------------------------------------- */
void win_frame_layout_update(struct win_frame_filter *f, obs_data_t *s, uint32_t base_w, uint32_t base_h)
{
	if (base_w == 0)
		base_w = 320;
	if (base_h == 0)
		base_h = 240;

	double ui = obs_data_get_double(s, S_UI_SCALE);
	if (ui < 0.25)
		ui = 1.0;
	f->ui = (float)ui;

	double scale = obs_data_get_double(s, S_SCALE);
	if (scale <= 0.0)
		scale = 1.0;

	const int btn_style = (int)obs_data_get_int(s, S_BTN_STYLE);
	const bool tb_on = obs_data_get_bool(s, S_TITLEBAR_ENABLED);
	const double ft = floor(obs_data_get_double(s, S_FRAME_THICKNESS) * ui + 0.5);
	const double th = tb_on ? floor(obs_data_get_double(s, S_TITLEBAR_HEIGHT) * ui + 0.5) : 0.0;
	const double sb = obs_data_get_bool(s, S_STATUSBAR_ENABLED) ? floor(22.0 * ui + 0.5) : 0.0;
	const double pad = floor(obs_data_get_double(s, S_PADDING) * ui + 0.5);
	const bool inset = tb_on && obs_data_get_bool(s, S_TITLEBAR_INSET);

	double shadow_margin = 0.0;
	if (obs_data_get_bool(s, S_SHADOW_ENABLED))
		shadow_margin = (obs_data_get_double(s, S_SHADOW_BLUR) + obs_data_get_double(s, S_SHADOW_SPREAD) +
				 fabs(obs_data_get_double(s, S_SHADOW_X)) + fabs(obs_data_get_double(s, S_SHADOW_Y))) * ui;
	double glow_margin = obs_data_get_bool(s, S_GLOW_ENABLED) ? obs_data_get_double(s, S_GLOW_RADIUS) * ui : 0.0;
	double margin = floor((shadow_margin > glow_margin ? shadow_margin : glow_margin) + 4.0);

	const double top_h = tb_on ? (inset ? ft + th : th) : ft;
	const double cam_w = floor(base_w * scale + 0.5);
	const double cam_h = floor(base_h * scale + 0.5);
	const double win_w = cam_w + ft * 2.0 + pad * 2.0;
	const double win_h = top_h + pad + cam_h + pad + ft + sb;

	f->canvas_w = (uint32_t)(win_w + margin * 2.0);
	f->canvas_h = (uint32_t)(win_h + margin * 2.0);

	const double wx = margin, wy = margin;
	set_rect(f->window_rect, wx, wy, win_w, win_h);
	set_rect(f->body_rect, wx + ft, wy + top_h, win_w - ft * 2.0, win_h - top_h - ft);
	set_rect(f->cam_rect, wx + ft + pad, wy + top_h + pad, cam_w, cam_h);

	const double r = obs_data_get_double(s, S_CORNER_RADIUS) * ui;
	const bool top_only = obs_data_get_bool(s, S_CORNER_TOP_ONLY);
	f->corner_radii[0] = f->corner_radii[1] = (float)r;
	f->corner_radii[2] = f->corner_radii[3] = top_only ? 0.0f : (float)r;

	/* ---- title bar ---- */
	const double tbx = inset ? wx + ft : wx;
	const double tby = inset ? wy + ft : wy;
	const double tbw = inset ? win_w - ft * 2.0 : win_w;
	set_rect(f->titlebar_rect, tbx, tby, tbw, th);

	/* ---- window buttons ---- */
	const double bh = floor(obs_data_get_double(s, S_BTN_SIZE) * ui + 0.5);
	const double bw = floor(obs_data_get_double(s, S_BTN_WIDTH) * ui + 0.5);
	const double bclose_w = (btn_style == 2) ? floor(bw * 1.7 + 0.5) : bw;
	const double bspace = floor(obs_data_get_double(s, S_BTN_SPACING) * ui + 0.5);
	const double margin_px = floor(obs_data_get_double(s, S_BTN_MARGIN) * ui + 0.5);
	const bool left_side = (btn_style == 4);
	const double group_gap = (btn_style == 0) ? floor(2.0 * ui + 0.5) : 0.0;
	const double by = (btn_style == 2) ? tby + floor(1.0 * ui) : tby + floor((th - bh) / 2.0);

	const bool en_min = obs_data_get_bool(s, S_BTN_MIN_ENABLED);
	const bool en_max = obs_data_get_bool(s, S_BTN_MAX_ENABLED);
	const bool en_close = obs_data_get_bool(s, S_BTN_CLOSE_ENABLED);

	/* order[] is the placement order: from the left edge for left-side
	 * styles, from the right edge otherwise. */
	int order[3];
	if (left_side) {
		order[0] = 2; order[1] = 0; order[2] = 1;      /* close, minimize, maximize */
	} else if (btn_style == 7) {
		order[0] = 0; order[1] = 1; order[2] = 2;      /* Amiga: depth, zoom, (close) */
	} else {
		order[0] = 2; order[1] = 1; order[2] = 0;      /* close, maximize, minimize */
	}

	int count = 0;
	double cx = left_side ? tbx + margin_px : tbx + tbw - margin_px;
	double snap_bx = 0, snap_bw = 0;
	for (int i = 0; i < 3; i++) {
		const int type = order[i];
		const bool on = tb_on && (type == 0 ? en_min : type == 1 ? en_max : en_close);
		if (!on)
			continue;
		const double w = (type == 2) ? bclose_w : bw;
		if (left_side) {
			set_rect(f->btn_rect[count], cx, by, w, bh);
			cx += w + bspace;
		} else {
			cx -= w;
			set_rect(f->btn_rect[count], cx, by, w, bh);
			cx -= bspace;
			if (i == 0)
				cx -= group_gap;
		}
		f->btn_type[count] = type;
		if (type == 1) {
			snap_bx = f->btn_rect[count][0];
			snap_bw = w;
		}
		count++;
	}
	f->btn_count = count;
	for (int i = count; i < 3; i++)
		f->btn_rect[i][2] = 0;

	double buttons_left = tbx + tbw, buttons_right = tbx;
	for (int i = 0; i < count; i++) {
		if (f->btn_rect[i][0] < buttons_left)
			buttons_left = f->btn_rect[i][0];
		if (f->btn_rect[i][0] + f->btn_rect[i][2] > buttons_right)
			buttons_right = f->btn_rect[i][0] + f->btn_rect[i][2];
	}

	/* glyph half-size / half-stroke per button style (px) */
	static const float glyph_tab[8][2] = {{4.0f, 1.0f}, {5.0f, 1.1f}, {4.5f, 1.0f}, {5.0f, 0.5f},
					      {0.0f, 0.0f}, {4.0f, 0.0f}, {4.5f, 0.55f}, {0.0f, 0.5f}};
	const int gi = (btn_style >= 0 && btn_style < 8) ? btn_style : 3;
	f->btn_glyph = (float)(glyph_tab[gi][0] * ui);
	f->btn_lw = (float)(glyph_tab[gi][1] * ui);
	if (btn_style == 7)
		f->btn_glyph = (float)(bh * 0.30);

	/* ---- Windows 11 Snap Layouts flyout, under the maximize button ---- */
	f->snap_on = 0;
	if (obs_data_get_bool(s, S_SNAP_FLYOUT) && tb_on && snap_bw > 0) {
		const double sw = floor(228.0 * ui), sh = floor(96.0 * ui);
		double sx = floor(snap_bx + snap_bw * 0.5 - sw * 0.5);
		const double maxx = wx + win_w - sw - 6.0 * ui;
		if (sx > maxx)
			sx = maxx;
		set_rect(f->snap_rect, sx, tby + th + floor(4.0 * ui), sw, sh);
		f->snap_on = 1;
	}

	/* ---- icon ---- */
	const char *icon_path = obs_data_get_string(s, S_TITLEBAR_ICON_PATH);
	const bool custom_icon = icon_path && icon_path[0];
	const int icon_mode = (int)obs_data_get_int(s, S_TITLEBAR_ICON_MODE);
	const bool show_icon = tb_on && (custom_icon || icon_mode != 0);
	const double icon = floor(obs_data_get_double(s, S_TITLEBAR_ICON_SIZE) * ui + 0.5);
	const bool flush_icon = icon_mode == 3 || icon_mode == 5 || icon_mode == 6;
	double text_min = tbx + floor(8.0 * ui);
	if (left_side)
		text_min = buttons_right + floor(10.0 * ui);
	if (show_icon) {
		double left_pad = flush_icon ? 1.0 : (icon_mode == 2 ? 12.0 : (inset ? 2.0 : 8.0));
		double ix = tbx + floor(left_pad * ui);
		if (left_side && ix < buttons_right + 8.0 * ui)
			ix = buttons_right + floor(8.0 * ui);
		set_rect(f->icon_rect, ix, tby + floor((th - icon) / 2.0), icon, icon);
		text_min = ix + icon + floor((flush_icon ? 4.0 : (inset ? 3.0 : 6.0)) * ui);
		if (icon_mode == 2)
			text_min = ix + icon + floor(14.0 * ui);
	} else {
		f->icon_rect[2] = 0;
	}
	const double text_left = text_min;

	/* ---- fonts / cached text (only rebuilt when inputs change) ---- */
	const uint32_t title_color = (uint32_t)obs_data_get_int(s, S_TITLE_COLOR);
	const char *title_font = obs_data_get_string(s, S_TITLE_FONT);
	int font_px = (int)floor(obs_data_get_double(s, S_TITLE_FONT_SIZE) * ui + 0.5);
	if (font_px < 6)
		font_px = 6;
	const bool bold = obs_data_get_bool(s, S_TITLE_BOLD);

	char title_buf[512];
	{
		const char *t = obs_data_get_string(s, S_TITLE_TEXT);
		strncpy(title_buf, t ? t : "", sizeof(title_buf) - 1);
		title_buf[sizeof(title_buf) - 1] = 0;
		if (obs_data_get_bool(s, S_TITLE_LOWER))
			for (char *c = title_buf; *c; c++)
				if (*c >= 'A' && *c <= 'Z')
					*c = (char)(*c - 'A' + 'a');
	}

	if (tb_on) {
		refresh_text(f, &f->title_text_tex, &f->title_text_w, &f->title_text_h, f->cached_title_text,
			     sizeof(f->cached_title_text), title_buf, title_font, font_px, bold, title_color,
			     f->cached_title_font, sizeof(f->cached_title_font), &f->cached_title_size,
			     &f->cached_title_bold, &f->cached_title_color);
	}

	if (tb_on && f->title_text_tex && f->title_text_w) {
		const double limit = (left_side || count == 0) ? tbx + tbw - floor(8.0 * ui)
							       : buttons_left - floor(6.0 * ui);
		const double vis_w = (double)f->title_text_w - 2.0 * WF_TEXT_PAD;
		double x = text_left - WF_TEXT_PAD;
		if (obs_data_get_int(s, S_TITLE_ALIGN) == 1) {
			double cxm = tbx + tbw * 0.5 - vis_w * 0.5;
			if (cxm < text_left)
				cxm = text_left;
			if (cxm + vis_w > limit)
				cxm = limit - vis_w;
			if (cxm < text_left)
				cxm = text_left;
			x = floor(cxm) - WF_TEXT_PAD;
		}
		const double y = tby + floor(((double)th - (double)f->title_text_h) / 2.0);
		double clip_w = limit - x;
		if (clip_w > f->title_text_w)
			clip_w = f->title_text_w;
		if (clip_w < 0)
			clip_w = 0;
		set_rect(f->title_text_rect, floor(x), y, clip_w, f->title_text_h);
	} else {
		f->title_text_rect[2] = 0;
	}

	/* ---- status bar ---- */
	if (sb > 0) {
		set_rect(f->statusbar_rect, wx + ft, wy + win_h - ft - sb, win_w - ft * 2.0, sb);
		const bool dark = obs_data_get_bool(s, S_DARK_MODE);
		const uint32_t sc = dark ? 0xFFFFFFFFu : 0xFF000000u;
		int sfont = (int)floor(11.0 * ui + 0.5);
		refresh_text(f, &f->status_text_tex, &f->status_text_w, &f->status_text_h, f->cached_status_text,
			     sizeof(f->cached_status_text), obs_data_get_string(s, S_STATUSBAR_TEXT), "Tahoma", sfont,
			     false, sc, NULL, 0, &f->cached_status_size, NULL, &f->cached_status_color);
		if (f->status_text_tex) {
			const double sx = f->statusbar_rect[0] + floor(6.0 * ui) - WF_TEXT_PAD;
			const double sy = f->statusbar_rect[1] + floor((sb - f->status_text_h) / 2.0);
			set_rect(f->status_text_rect, sx, sy, f->statusbar_rect[2] - 24.0 * ui, f->status_text_h);
		} else {
			f->status_text_rect[2] = 0;
		}
	} else {
		f->statusbar_rect[2] = 0;
		f->status_text_rect[2] = 0;
	}

	if (obs_data_get_bool(s, S_RESIZEGRIP_ENABLED)) {
		const double g = floor(15.0 * ui);
		set_rect(f->resizegrip_rect, wx + win_w - ft - g, wy + win_h - ft - g, g, g);
	} else {
		f->resizegrip_rect[2] = 0;
	}

	const double logo = obs_data_get_double(s, S_LOGO_SIZE);
	set_rect(f->logo_rect, wx + obs_data_get_double(s, S_LOGO_POS_X), wy + obs_data_get_double(s, S_LOGO_POS_Y),
		 logo, logo);

	reload_image_if_needed(&f->icon_img, &f->icon_loaded, f->icon_path, sizeof(f->icon_path), icon_path);
	reload_image_if_needed(&f->mask_img, &f->mask_loaded, f->mask_path, sizeof(f->mask_path),
				obs_data_get_string(s, S_MASK_IMAGE_PATH));
	reload_image_if_needed(&f->bg_img, &f->bg_loaded, f->bg_path, sizeof(f->bg_path),
				obs_data_get_string(s, S_CUSTOM_BG_PATH));
	reload_image_if_needed(&f->logo_img, &f->logo_loaded, f->logo_path, sizeof(f->logo_path),
				obs_data_get_string(s, S_LOGO_PATH));

	f->last_base_w = base_w;
	f->last_base_h = base_h;
}

void win_frame_cache_eparams(struct win_frame_filter *f)
{
	int i = 0;
#define X(n) f->ep.p[i++] = gs_effect_get_param_by_name(f->effect, #n);
	WF_EPARAMS(X)
#undef X
}

#define EP(name) (f->ep.p[EP_##name])

static void set_v4(gs_eparam_t *p, const float *r)
{
	struct vec4 v;
	vec4_set(&v, r[0], r[1], r[2], r[3]);
	gs_effect_set_vec4(p, &v);
}

static void set_v2(gs_eparam_t *p, float x, float y)
{
	struct vec2 v;
	vec2_set(&v, x, y);
	gs_effect_set_vec2(p, &v);
}

static void set_col(gs_eparam_t *p, long long packed, float amul)
{
	struct vec4 c = abgr_to_vec4(packed, amul);
	gs_effect_set_vec4(p, &c);
}

void win_frame_set_effect_params(struct win_frame_filter *f, obs_data_t *s)
{
	const float ui = f->ui > 0.0f ? f->ui : 1.0f;
#define D(k) obs_data_get_double(s, k)
#define B(k) (obs_data_get_bool(s, k) ? 1 : 0)
#define I(k) ((int)obs_data_get_int(s, k))

	set_v2(EP(canvas_size), (float)f->canvas_w, (float)f->canvas_h);
	set_v4(EP(window_rect), f->window_rect);
	set_v4(EP(corner_radii), f->corner_radii);
	gs_effect_set_float(EP(frame_thickness), (float)(D(S_FRAME_THICKNESS) * ui));
	gs_effect_set_int(EP(frame_style), I(S_FRAME_STYLE));
	set_col(EP(frame_color), obs_data_get_int(s, S_FRAME_COLOR), 1.0f);
	set_v4(EP(body_rect), f->body_rect);

	set_v4(EP(cam_rect), f->cam_rect);
	{
		float crop[4] = {(float)D(S_CAM_CROP_L), (float)D(S_CAM_CROP_R), (float)D(S_CAM_CROP_T),
				 (float)D(S_CAM_CROP_B)};
		set_v4(EP(cam_crop), crop);
	}
	gs_effect_set_float(EP(cam_zoom), (float)D(S_CAM_ZOOM));
	set_v2(EP(cam_pos), (float)D(S_CAM_POS_X), (float)D(S_CAM_POS_Y));
	gs_effect_set_int(EP(cam_bevel), B(S_CAM_BEVEL));

	const int mask_type = I(S_MASK_TYPE);
	const bool custom_mask = mask_type == 4 && f->mask_loaded;
	gs_effect_set_int(EP(mask_type), mask_type);
	gs_effect_set_float(EP(mask_feather), (float)(D(S_MASK_FEATHER) * ui));
	gs_effect_set_float(EP(mask_radius), (float)(D(S_MASK_RADIUS) * ui));
	gs_effect_set_int(EP(use_custom_mask), custom_mask ? 1 : 0);
	gs_effect_set_texture(EP(mask_tex), custom_mask ? f->mask_img.texture : get_dummy(f));

	set_col(EP(bg_color_top), obs_data_get_int(s, S_BG_COLOR_TOP), 1.0f);
	set_col(EP(bg_color_bottom), obs_data_get_int(s, S_BG_COLOR_BOTTOM), 1.0f);
	gs_effect_set_float(EP(bg_opacity), (float)D(S_BG_OPACITY));
	gs_effect_set_int(EP(use_custom_bg), f->bg_loaded ? 1 : 0);
	gs_effect_set_texture(EP(bg_tex), f->bg_loaded ? f->bg_img.texture : get_dummy(f));

	gs_effect_set_int(EP(border_enabled), B(S_BORDER_ENABLED));
	gs_effect_set_float(EP(border_thickness), (float)(D(S_BORDER_THICKNESS) * ui));
	gs_effect_set_float(EP(border_opacity), (float)D(S_BORDER_OPACITY));
	set_col(EP(border_color), obs_data_get_int(s, S_BORDER_COLOR), 1.0f);
	gs_effect_set_int(EP(border_double), B(S_BORDER_DOUBLE));
	set_col(EP(border_inner_color), obs_data_get_int(s, S_BORDER_INNER_COLOR), 1.0f);
	gs_effect_set_int(EP(border_dashed), B(S_BORDER_DASHED));

	gs_effect_set_int(EP(titlebar_enabled), B(S_TITLEBAR_ENABLED));
	set_v4(EP(titlebar_rect), f->titlebar_rect);
	gs_effect_set_int(EP(titlebar_style), I(S_TITLEBAR_STYLE));
	set_col(EP(titlebar_color_a), obs_data_get_int(s, S_TITLEBAR_COLOR_A), 1.0f);
	set_col(EP(titlebar_color_b), obs_data_get_int(s, S_TITLEBAR_COLOR_B), 1.0f);
	gs_effect_set_int(EP(titlebar_gradient), B(S_TITLEBAR_GRADIENT));
	gs_effect_set_int(EP(titlebar_grad_horiz), B(S_TITLEBAR_GRAD_HORIZ));
	gs_effect_set_float(EP(glass_reflection), (float)D(S_GLASS_REFLECTION));

	set_v4(EP(title_text_rect), f->title_text_rect);
	set_v2(EP(title_text_size), (float)(f->title_text_w ? f->title_text_w : 1),
	       (float)(f->title_text_h ? f->title_text_h : 1));
	gs_effect_set_texture(EP(title_text), f->title_text_tex ? f->title_text_tex : get_dummy(f));
	gs_effect_set_int(EP(title_shadow), I(S_TITLE_SHADOW));

	set_v4(EP(icon_rect), f->icon_rect);
	gs_effect_set_int(EP(icon_mode), I(S_TITLEBAR_ICON_MODE));
	gs_effect_set_int(EP(icon_custom), f->icon_loaded ? 1 : 0);
	gs_effect_set_texture(EP(icon_tex), f->icon_loaded ? f->icon_img.texture : get_dummy(f));

	gs_effect_set_int(EP(statusbar_enabled), B(S_STATUSBAR_ENABLED));
	set_v4(EP(statusbar_rect), f->statusbar_rect);
	set_col(EP(statusbar_color), obs_data_get_int(s, S_BG_COLOR_BOTTOM), 1.0f);
	set_v4(EP(status_text_rect), f->status_text_rect);
	set_v2(EP(status_text_size), (float)(f->status_text_w ? f->status_text_w : 1),
	       (float)(f->status_text_h ? f->status_text_h : 1));
	gs_effect_set_texture(EP(status_text), f->status_text_tex ? f->status_text_tex : get_dummy(f));

	gs_effect_set_int(EP(resizegrip_enabled), B(S_RESIZEGRIP_ENABLED));
	set_v4(EP(resizegrip_rect), f->resizegrip_rect);

	gs_effect_set_int(EP(btn_style), I(S_BTN_STYLE));
	gs_effect_set_int(EP(btn_count), f->btn_count);
	set_col(EP(btn_symbol_color), obs_data_get_int(s, S_BTN_SYMBOL_COLOR), 1.0f);
	set_col(EP(btn_symbol_close_color), obs_data_get_int(s, S_BTN_SYMBOL_CLOSE_COLOR), 1.0f);
	gs_effect_set_float(EP(btn_glyph), f->btn_glyph);
	gs_effect_set_float(EP(btn_lw), f->btn_lw);
	{
		const long long normal = obs_data_get_int(s, S_BTN_COLOR);
		const long long close = obs_data_get_int(s, S_BTN_CLOSE_COLOR);
		set_v4(EP(btn_rect_0), f->btn_rect[0]);
		set_v4(EP(btn_rect_1), f->btn_rect[1]);
		set_v4(EP(btn_rect_2), f->btn_rect[2]);
		set_col(EP(btn_color_0), f->btn_type[0] == 2 ? close : normal, 1.0f);
		set_col(EP(btn_color_1), f->btn_type[1] == 2 ? close : normal, 1.0f);
		set_col(EP(btn_color_2), f->btn_type[2] == 2 ? close : normal, 1.0f);
		gs_effect_set_int(EP(btn_type_0), f->btn_type[0]);
		gs_effect_set_int(EP(btn_type_1), f->btn_type[1]);
		gs_effect_set_int(EP(btn_type_2), f->btn_type[2]);
	}

	gs_effect_set_int(EP(shadow_enabled), B(S_SHADOW_ENABLED));
	set_col(EP(shadow_color), obs_data_get_int(s, S_SHADOW_COLOR), 1.0f);
	gs_effect_set_float(EP(shadow_opacity), (float)D(S_SHADOW_OPACITY));
	gs_effect_set_float(EP(shadow_blur), (float)(D(S_SHADOW_BLUR) * ui));
	gs_effect_set_float(EP(shadow_spread), (float)(D(S_SHADOW_SPREAD) * ui));
	set_v2(EP(shadow_offset), (float)(D(S_SHADOW_X) * ui), (float)(D(S_SHADOW_Y) * ui));

	gs_effect_set_int(EP(glow_enabled), B(S_GLOW_ENABLED));
	set_col(EP(glow_color), obs_data_get_int(s, S_GLOW_COLOR), 1.0f);
	gs_effect_set_float(EP(glow_opacity), (float)D(S_GLOW_OPACITY));
	gs_effect_set_float(EP(glow_radius), (float)(D(S_GLOW_RADIUS) * ui));
	gs_effect_set_float(EP(glow_intensity), (float)D(S_GLOW_INTENSITY));

	gs_effect_set_int(EP(btn_hover), f->snap_on ? 1 : (I(S_BTN_HOVER) - 1));
	gs_effect_set_int(EP(snap_enabled), f->snap_on);
	set_v4(EP(snap_rect), f->snap_rect);
	set_col(EP(snap_accent), obs_data_get_int(s, S_ACCENT_COLOR), 1.0f);
	gs_effect_set_int(EP(tiles_enabled), (B(S_TILES) && f->statusbar_rect[2] > 0.0f) ? 1 : 0);
	set_col(EP(tile_color), obs_data_get_int(s, S_ACCENT_COLOR), 1.0f);
	gs_effect_set_float(EP(cam_pixelate), (float)(D(S_CAM_PIXELATE) * ui));
	gs_effect_set_float(EP(cam_scanlines), (float)D(S_CAM_SCANLINES));
	gs_effect_set_float(EP(cam_crt), (float)D(S_CAM_CRT));

	gs_effect_set_float(EP(anim_alpha), f->anim_alpha);
	gs_effect_set_float(EP(anim_scale), f->anim_scale);
	set_v2(EP(anim_scale_origin), f->window_rect[0] + f->window_rect[2] * 0.5f,
	       f->window_rect[1] + f->window_rect[3] * 0.5f);

#undef D
#undef B
#undef I
}

void win_frame_draw_logo_overlay(struct win_frame_filter *f, obs_data_t *s)
{
	if (!f->logo_loaded || f->logo_rect[2] <= 0.0f)
		return;
	(void)s;

	gs_effect_t *default_effect = obs_get_base_effect(OBS_EFFECT_DEFAULT);
	gs_eparam_t *image_param = gs_effect_get_param_by_name(default_effect, "image");

	gs_matrix_push();
	gs_matrix_translate3f(f->logo_rect[0], f->logo_rect[1], 0.0f);
	gs_matrix_scale3f(f->logo_rect[2] / (float)f->logo_img.cx, f->logo_rect[3] / (float)f->logo_img.cy, 1.0f);

	gs_effect_set_texture(image_param, f->logo_img.texture);
	while (gs_effect_loop(default_effect, "Draw"))
		gs_draw_sprite(f->logo_img.texture, 0, f->logo_img.cx, f->logo_img.cy);

	gs_matrix_pop();
}
