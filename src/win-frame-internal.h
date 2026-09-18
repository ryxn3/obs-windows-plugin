#pragma once

/* Internal (not installed) struct shared between win-frame-filter.c and
 * win-frame-render.c. */

#include <obs-module.h>
#include <graphics/graphics.h>
#include <graphics/image-file.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Every uniform in win_frame.effect (except "image" and "ViewProj", which
 * obs_source_process_filter_end binds itself). Looked up once at effect load. */
#define WF_EPARAMS(X) \
	X(canvas_size) \
	X(window_rect) \
	X(corner_radii) \
	X(frame_thickness) \
	X(frame_style) \
	X(frame_color) \
	X(body_rect) \
	X(cam_rect) \
	X(cam_crop) \
	X(cam_zoom) \
	X(cam_pos) \
	X(cam_bevel) \
	X(mask_type) \
	X(mask_feather) \
	X(mask_radius) \
	X(use_custom_mask) \
	X(mask_tex) \
	X(bg_color_top) \
	X(bg_color_bottom) \
	X(bg_opacity) \
	X(use_custom_bg) \
	X(bg_tex) \
	X(border_enabled) \
	X(border_thickness) \
	X(border_opacity) \
	X(border_color) \
	X(border_double) \
	X(border_inner_color) \
	X(border_dashed) \
	X(titlebar_enabled) \
	X(titlebar_rect) \
	X(titlebar_style) \
	X(titlebar_color_a) \
	X(titlebar_color_b) \
	X(titlebar_gradient) \
	X(titlebar_grad_horiz) \
	X(glass_reflection) \
	X(title_text) \
	X(title_text_rect) \
	X(title_text_size) \
	X(title_shadow) \
	X(icon_tex) \
	X(icon_rect) \
	X(icon_mode) \
	X(icon_custom) \
	X(statusbar_enabled) \
	X(statusbar_rect) \
	X(statusbar_color) \
	X(status_text) \
	X(status_text_rect) \
	X(status_text_size) \
	X(resizegrip_enabled) \
	X(resizegrip_rect) \
	X(btn_style) \
	X(btn_count) \
	X(btn_rect_0) \
	X(btn_color_0) \
	X(btn_type_0) \
	X(btn_rect_1) \
	X(btn_color_1) \
	X(btn_type_1) \
	X(btn_rect_2) \
	X(btn_color_2) \
	X(btn_type_2) \
	X(btn_symbol_color) \
	X(btn_symbol_close_color) \
	X(btn_glyph) \
	X(btn_lw) \
	X(shadow_enabled) \
	X(shadow_color) \
	X(shadow_opacity) \
	X(shadow_blur) \
	X(shadow_spread) \
	X(shadow_offset) \
	X(glow_enabled) \
	X(glow_color) \
	X(glow_opacity) \
	X(glow_radius) \
	X(glow_intensity) \
	X(anim_alpha) \
	X(anim_scale) \
	X(anim_scale_origin) \
	X(btn_hover) \
	X(snap_enabled) \
	X(snap_rect) \
	X(snap_accent) \
	X(tiles_enabled) \
	X(tile_color) \
	X(cam_pixelate) \
	X(cam_scanlines) \
	X(cam_crt)

enum {
#define X(n) EP_##n,
	WF_EPARAMS(X)
#undef X
	EP_COUNT
};

struct win_frame_eparams {
	gs_eparam_t *p[EP_COUNT];
};

struct win_frame_filter {
	obs_source_t *source;
	gs_effect_t *effect;
	struct win_frame_eparams ep;
	obs_data_t *cached_settings; /* addref'd; render/tick never re-fetch settings */

	/* computed layout, canvas pixels (rebuilt only in win_frame_layout_update) */
	uint32_t canvas_w, canvas_h;
	float ui;                    /* effective UI scale */
	float window_rect[4];
	float body_rect[4];
	float corner_radii[4];
	float cam_rect[4];
	float titlebar_rect[4];
	float title_text_rect[4];
	float icon_rect[4];
	float statusbar_rect[4];
	float status_text_rect[4];
	float resizegrip_rect[4];
	float btn_rect[3][4];
	int btn_type[3];             /* 0 min 1 max 2 close */
	int btn_count;
	float btn_glyph, btn_lw;
	float logo_rect[4];
	float snap_rect[4];
	int snap_on;

	/* cached text textures (regenerated only when inputs change) */
	gs_texture_t *title_text_tex;
	uint32_t title_text_w, title_text_h;
	char cached_title_text[512];
	char cached_title_font[128];
	int cached_title_size;
	bool cached_title_bold;
	uint32_t cached_title_color;

	gs_texture_t *status_text_tex;
	uint32_t status_text_w, status_text_h;
	char cached_status_text[512];
	int cached_status_size;
	uint32_t cached_status_color;

	/* cached loaded images (reloaded only when path changes) */
	gs_image_file_t icon_img;
	char icon_path[512];
	bool icon_loaded;

	gs_image_file_t mask_img;
	char mask_path[512];
	bool mask_loaded;

	gs_image_file_t bg_img;
	char bg_path[512];
	bool bg_loaded;

	gs_image_file_t logo_img;
	char logo_path[512];
	bool logo_loaded;

	gs_texture_t *dummy_tex;     /* 1x1 transparent, bound to unused texture slots */

	bool anim_active_prev;
	uint64_t anim_start_ns;
	float anim_alpha;
	float anim_scale;

	uint32_t last_base_w, last_base_h;
};

#ifdef __cplusplus
}
#endif
