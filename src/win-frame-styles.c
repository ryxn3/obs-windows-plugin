#include <string.h>
#include "win-frame-styles.h"
#include "win-frame-filter.h"

/* OBS colour properties are 0xAABBGGRR ints. */
#define PACK(r, g, b, a) ((int)(((uint32_t)(a) << 24) | ((uint32_t)(b) << 16) | ((uint32_t)(g) << 8) | (uint32_t)(r)))
#define HEX(rgb, a) PACK(((rgb) >> 16) & 255, ((rgb) >> 8) & 255, (rgb) & 255, (a))

const struct win_frame_style_entry win_frame_style_table[STYLE_COUNT] = {
	{"classic", "Classic Windows"},
	{"win31", "Windows 3.1"},
	{"winnt4", "Windows NT 4.0"},
	{"win95", "Windows 95"},
	{"win98", "Windows 98"},
	{"win98_space", "Windows 98 Plus! - Space"},
	{"win98_creatures", "Windows 98 Plus! - Dangerous Creatures"},
	{"winme", "Windows ME"},
	{"win2000", "Windows 2000"},
	{"winxp_luna", "Windows XP (Luna Blue)"},
	{"winxp_olive", "Windows XP (Luna Olive Green)"},
	{"winxp_silver", "Windows XP (Luna Silver)"},
	{"winxp_royale", "Windows XP (Royale)"},
	{"winxp_zune", "Windows XP (Zune)"},
	{"winxp_embedded", "Windows XP (Embedded)"},
	{"winxp_classic", "Windows XP (Classic)"},
	{"winvista_aero", "Windows Vista (Aero)"},
	{"winvista_basic", "Windows Vista (Basic)"},
	{"win7_aero", "Windows 7 (Aero)"},
	{"win7_basic", "Windows 7 (Basic)"},
	{"win7_hc", "Windows 7 (High Contrast)"},
	{"win8_metro", "Windows 8 (Metro)"},
	{"win81_metro", "Windows 8.1 (Metro)"},
	{"win8_app", "Windows 8 Metro App (full screen)"},
	{"win10_fluent", "Windows 10"},
	{"win10_accent", "Windows 10 (Accent Title Bar)"},
	{"win10_tablet", "Windows 10 (Tablet Mode)"},
	{"win11_fluent", "Windows 11"},
	{"win11_mica", "Windows 11 (Mica Accent + Snap Layouts)"},
	{"wp7", "Windows Phone 7"},
	{"wp8", "Windows Phone 8 (Tiles)"},
	{"macos", "macOS"},
	{"gnome", "GNOME (Adwaita)"},
	{"kde", "KDE Plasma (Breeze)"},
	{"amiga", "Amiga Workbench"},
	{"atari", "Atari TOS / GEM"},
	{"custom", "Custom Style"},
};

static void num(obs_data_t *s, const char *k, double v) { obs_data_set_double(s, k, v); }
static void flag(obs_data_t *s, const char *k, bool v) { obs_data_set_bool(s, k, v); }
static void ival(obs_data_t *s, const char *k, long long v) { obs_data_set_int(s, k, v); }
static void col(obs_data_t *s, const char *k, int packed) { obs_data_set_int(s, k, packed); }
static void str(obs_data_t *s, const char *k, const char *v) { obs_data_set_string(s, k, v); }

/* User content, not style: only defaulted, never overwritten by a style. */
static void user_defaults(obs_data_t *s)
{
	obs_data_set_default_bool(s, S_ENABLED, true);
	obs_data_set_default_double(s, S_SCALE, 1.0);
	obs_data_set_default_double(s, S_UI_SCALE, 1.0);
	obs_data_set_default_double(s, S_ROTATION, 0.0);
	obs_data_set_default_int(s, S_ALIGN_H, 1);
	obs_data_set_default_int(s, S_ALIGN_V, 1);
	obs_data_set_default_double(s, S_CAM_CROP_L, 0.0);
	obs_data_set_default_double(s, S_CAM_CROP_R, 0.0);
	obs_data_set_default_double(s, S_CAM_CROP_T, 0.0);
	obs_data_set_default_double(s, S_CAM_CROP_B, 0.0);
	obs_data_set_default_double(s, S_CAM_ZOOM, 1.0);
	obs_data_set_default_double(s, S_CAM_POS_X, 0.0);
	obs_data_set_default_double(s, S_CAM_POS_Y, 0.0);
	obs_data_set_default_double(s, S_CAM_PIXELATE, 0.0);
	obs_data_set_default_double(s, S_CAM_SCANLINES, 0.0);
	obs_data_set_default_double(s, S_CAM_CRT, 0.0);
	obs_data_set_default_int(s, S_MASK_TYPE, 0);
	obs_data_set_default_double(s, S_MASK_FEATHER, 0.0);
	obs_data_set_default_double(s, S_MASK_RADIUS, 12.0);
	obs_data_set_default_string(s, S_TITLE_TEXT, "Camera");
	obs_data_set_default_string(s, S_STATUSBAR_TEXT, "Ready");
	obs_data_set_default_bool(s, S_ANIM_ENABLED, false);
	obs_data_set_default_int(s, S_ANIM_TYPE, 1);
	obs_data_set_default_int(s, S_ANIM_DURATION, 250);
	obs_data_set_default_int(s, S_ANIM_DIRECTION, 0);
	obs_data_set_default_double(s, S_LOGO_SIZE, 48.0);
	obs_data_set_default_double(s, S_LOGO_OPACITY, 1.0);
	obs_data_set_default_double(s, S_LOGO_POS_X, 8.0);
	obs_data_set_default_double(s, S_LOGO_POS_Y, 8.0);
	obs_data_set_default_bool(s, S_DARK_MODE, false);
}

/* Neutral flat baseline: every style-owned key gets a value, so switching
 * styles fully replaces the previous look. */
static void baseline(obs_data_t *s)
{
	num(s, S_FRAME_THICKNESS, 1.0);
	num(s, S_TITLEBAR_HEIGHT, 30.0);
	num(s, S_CORNER_RADIUS, 0.0);
	flag(s, S_CORNER_TOP_ONLY, false);
	num(s, S_PADDING, 0.0);
	ival(s, S_FRAME_STYLE, 0);
	col(s, S_FRAME_COLOR, HEX(0xFFFFFF, 255));
	flag(s, S_TITLEBAR_ENABLED, true);
	flag(s, S_TITLEBAR_INSET, false);
	ival(s, S_TITLEBAR_STYLE, 0);
	col(s, S_TITLEBAR_COLOR_A, HEX(0xFFFFFF, 255));
	col(s, S_TITLEBAR_COLOR_B, HEX(0xFFFFFF, 255));
	flag(s, S_TITLEBAR_GRADIENT, false);
	flag(s, S_TITLEBAR_GRAD_HORIZ, false);
	num(s, S_GLASS_REFLECTION, 0.0);
	num(s, S_GLASS_BLUR, 0.0);
	col(s, S_BG_COLOR_TOP, HEX(0xFFFFFF, 255));
	col(s, S_BG_COLOR_BOTTOM, HEX(0xFFFFFF, 255));
	num(s, S_BG_OPACITY, 1.0);
	col(s, S_TITLE_COLOR, HEX(0x000000, 255));
	str(s, S_TITLE_FONT, "Segoe UI");
	num(s, S_TITLE_FONT_SIZE, 12.0);
	flag(s, S_TITLE_BOLD, false);
	flag(s, S_TITLE_LOWER, false);
	ival(s, S_TITLE_SHADOW, 0);
	ival(s, S_TITLE_ALIGN, 0);
	ival(s, S_TITLEBAR_ICON_MODE, 1);
	num(s, S_TITLEBAR_ICON_SIZE, 16.0);
	flag(s, S_BTN_MIN_ENABLED, true);
	flag(s, S_BTN_MAX_ENABLED, true);
	flag(s, S_BTN_CLOSE_ENABLED, true);
	ival(s, S_BTN_STYLE, 3);
	num(s, S_BTN_SIZE, 30.0);
	num(s, S_BTN_WIDTH, 46.0);
	num(s, S_BTN_SPACING, 0.0);
	num(s, S_BTN_MARGIN, 0.0);
	ival(s, S_BTN_HOVER, 0);
	flag(s, S_SNAP_FLYOUT, false);
	col(s, S_BTN_COLOR, HEX(0xFFFFFF, 0));
	col(s, S_BTN_CLOSE_COLOR, HEX(0xFFFFFF, 0));
	col(s, S_BTN_SYMBOL_COLOR, HEX(0x000000, 255));
	col(s, S_BTN_SYMBOL_CLOSE_COLOR, HEX(0x000000, 255));
	flag(s, S_BORDER_ENABLED, true);
	num(s, S_BORDER_THICKNESS, 1.0);
	num(s, S_BORDER_OPACITY, 1.0);
	col(s, S_BORDER_COLOR, HEX(0x808080, 255));
	flag(s, S_BORDER_DOUBLE, false);
	col(s, S_BORDER_INNER_COLOR, HEX(0xFFFFFF, 255));
	flag(s, S_BORDER_DASHED, false);
	flag(s, S_SHADOW_ENABLED, false);
	col(s, S_SHADOW_COLOR, HEX(0x000000, 255));
	num(s, S_SHADOW_OPACITY, 0.35);
	num(s, S_SHADOW_BLUR, 16.0);
	num(s, S_SHADOW_SPREAD, 0.0);
	num(s, S_SHADOW_X, 0.0);
	num(s, S_SHADOW_Y, 6.0);
	flag(s, S_GLOW_ENABLED, false);
	col(s, S_GLOW_COLOR, HEX(0xFFFFFF, 255));
	num(s, S_GLOW_OPACITY, 0.5);
	num(s, S_GLOW_RADIUS, 20.0);
	num(s, S_GLOW_INTENSITY, 1.0);
	flag(s, S_STATUSBAR_ENABLED, false);
	flag(s, S_RESIZEGRIP_ENABLED, false);
	flag(s, S_TILES, false);
	flag(s, S_CAM_BEVEL, false);
	col(s, S_ACCENT_COLOR, HEX(0x0078D7, 255));
}

/* 95 / 98 / ME / 2000 / XP-classic (and NT4, Plus! themes): 3D bevel geometry. */
static void classic_family(obs_data_t *s, int face, int title_a, int title_b, bool gradient)
{
	num(s, S_FRAME_THICKNESS, 4.0);
	num(s, S_TITLEBAR_HEIGHT, 18.0);
	num(s, S_PADDING, 2.0);
	ival(s, S_FRAME_STYLE, 1);
	flag(s, S_TITLEBAR_INSET, true);
	flag(s, S_CAM_BEVEL, true);
	col(s, S_FRAME_COLOR, HEX(face, 255));
	col(s, S_BG_COLOR_TOP, HEX(face, 255));
	col(s, S_BG_COLOR_BOTTOM, HEX(face, 255));
	col(s, S_TITLEBAR_COLOR_A, HEX(title_a, 255));
	col(s, S_TITLEBAR_COLOR_B, HEX(title_b, 255));
	flag(s, S_TITLEBAR_GRADIENT, gradient);
	flag(s, S_TITLEBAR_GRAD_HORIZ, true);
	col(s, S_TITLE_COLOR, HEX(0xFFFFFF, 255));
	str(s, S_TITLE_FONT, "Tahoma");
	num(s, S_TITLE_FONT_SIZE, 11.0);
	flag(s, S_TITLE_BOLD, true);
	ival(s, S_BTN_STYLE, 0);
	num(s, S_BTN_SIZE, 14.0);
	num(s, S_BTN_WIDTH, 16.0);
	num(s, S_BTN_SPACING, 0.0);
	num(s, S_BTN_MARGIN, 2.0);
	col(s, S_BTN_COLOR, HEX(face, 255));
	col(s, S_BTN_CLOSE_COLOR, HEX(face, 255));
	col(s, S_BTN_SYMBOL_COLOR, HEX(0x000000, 255));
	col(s, S_BTN_SYMBOL_CLOSE_COLOR, HEX(0x000000, 255));
	flag(s, S_BORDER_ENABLED, false);
	num(s, S_TITLEBAR_ICON_SIZE, 16.0);
}

/* Luna-shaped themes: title gradient tinted between `light` and `dark`. */
static void luna_family(obs_data_t *s, int frame, int light, int dark, int btn, int title_text, int sym)
{
	num(s, S_FRAME_THICKNESS, 4.0);
	num(s, S_TITLEBAR_HEIGHT, 30.0);
	num(s, S_CORNER_RADIUS, 8.0);
	flag(s, S_CORNER_TOP_ONLY, true);
	col(s, S_FRAME_COLOR, HEX(frame, 255));
	col(s, S_BG_COLOR_TOP, HEX(0xECE9D8, 255));
	col(s, S_BG_COLOR_BOTTOM, HEX(0xECE9D8, 255));
	ival(s, S_TITLEBAR_STYLE, 2);
	col(s, S_TITLEBAR_COLOR_A, HEX(light, 255));
	col(s, S_TITLEBAR_COLOR_B, HEX(dark, 255));
	col(s, S_TITLE_COLOR, HEX(title_text, 255));
	str(s, S_TITLE_FONT, "Trebuchet MS");
	num(s, S_TITLE_FONT_SIZE, 13.0);
	flag(s, S_TITLE_BOLD, true);
	ival(s, S_TITLE_SHADOW, 1);
	ival(s, S_BTN_STYLE, 1);
	num(s, S_BTN_SIZE, 21.0);
	num(s, S_BTN_WIDTH, 21.0);
	num(s, S_BTN_SPACING, 2.0);
	num(s, S_BTN_MARGIN, 5.0);
	col(s, S_BTN_COLOR, HEX(btn, 255));
	col(s, S_BTN_CLOSE_COLOR, HEX(0xD9502E, 255));
	col(s, S_BTN_SYMBOL_COLOR, HEX(sym, 255));
	col(s, S_BTN_SYMBOL_CLOSE_COLOR, HEX(0xFFFFFF, 255));
	col(s, S_BORDER_COLOR, HEX(frame, 255));
	flag(s, S_STATUSBAR_ENABLED, true);
	flag(s, S_RESIZEGRIP_ENABLED, true);
}

/* Vista / 7 basic + aero share layout; `glass` chooses translucent vs opaque. */
static void aero_family(obs_data_t *s, int frame_packed, bool glass, int title_color, int shadow_mode)
{
	num(s, S_FRAME_THICKNESS, 8.0);
	num(s, S_TITLEBAR_HEIGHT, 30.0);
	num(s, S_CORNER_RADIUS, 7.0);
	col(s, S_FRAME_COLOR, frame_packed);
	col(s, S_BG_COLOR_TOP, HEX(0xFFFFFF, 255));
	col(s, S_BG_COLOR_BOTTOM, HEX(0xFFFFFF, 255));
	col(s, S_TITLEBAR_COLOR_A, PACK(255, 255, 255, glass ? 60 : 0));
	col(s, S_TITLEBAR_COLOR_B, PACK(255, 255, 255, 0));
	flag(s, S_TITLEBAR_GRADIENT, true);
	num(s, S_GLASS_REFLECTION, glass ? 0.55 : 0.0);
	col(s, S_TITLE_COLOR, HEX(title_color, 255));
	str(s, S_TITLE_FONT, "Segoe UI");
	num(s, S_TITLE_FONT_SIZE, 12.0);
	ival(s, S_TITLE_SHADOW, shadow_mode);
	ival(s, S_BTN_STYLE, 2);
	num(s, S_BTN_SIZE, 19.0);
	num(s, S_BTN_WIDTH, 28.0);
	num(s, S_BTN_MARGIN, 6.0);
	col(s, S_BTN_COLOR, glass ? PACK(214, 226, 242, 140) : HEX(0x9DB4D4, 255));
	col(s, S_BTN_CLOSE_COLOR, HEX(0xC8483C, 255));
	col(s, S_BTN_SYMBOL_COLOR, HEX(0xFFFFFF, 255));
	col(s, S_BTN_SYMBOL_CLOSE_COLOR, HEX(0xFFFFFF, 255));
	col(s, S_BORDER_COLOR, PACK(0, 0, 0, 190));
	flag(s, S_BORDER_DOUBLE, true);
	col(s, S_BORDER_INNER_COLOR, PACK(255, 255, 255, glass ? 170 : 110));
	flag(s, S_SHADOW_ENABLED, true);
	num(s, S_SHADOW_OPACITY, 0.55);
	num(s, S_SHADOW_BLUR, 22.0);
	num(s, S_SHADOW_Y, 5.0);
}

static void win8_buttons(obs_data_t *s, int sym)
{
	ival(s, S_BTN_STYLE, 3);
	num(s, S_BTN_SIZE, 20.0);
	num(s, S_BTN_WIDTH, 34.0);
	col(s, S_BTN_SYMBOL_COLOR, HEX(sym, 255));
	col(s, S_BTN_SYMBOL_CLOSE_COLOR, HEX(0xFFFFFF, 255));
	col(s, S_BTN_CLOSE_COLOR, HEX(0xC75050, 255));
}

void win_frame_apply_style_defaults(obs_data_t *s, const char *style)
{
	user_defaults(s);
	baseline(s);
	const bool dark = obs_data_get_bool(s, S_DARK_MODE);

	/* ------------------------------------------------------------ 3.1 */
	if (strcmp(style, "win31") == 0) {
		num(s, S_FRAME_THICKNESS, 4.0);
		num(s, S_TITLEBAR_HEIGHT, 19.0);
		num(s, S_PADDING, 1.0);
		flag(s, S_TITLEBAR_INSET, true);
		col(s, S_FRAME_COLOR, HEX(0xC0C0C0, 255));
		col(s, S_BG_COLOR_TOP, HEX(0xFFFFFF, 255));
		col(s, S_BG_COLOR_BOTTOM, HEX(0xFFFFFF, 255));
		col(s, S_TITLEBAR_COLOR_A, HEX(0x000080, 255));
		col(s, S_TITLEBAR_COLOR_B, HEX(0x000080, 255));
		col(s, S_TITLE_COLOR, HEX(0xFFFFFF, 255));
		str(s, S_TITLE_FONT, "Microsoft Sans Serif");
		num(s, S_TITLE_FONT_SIZE, 11.0);
		flag(s, S_TITLE_BOLD, true);
		ival(s, S_TITLE_ALIGN, 1);
		ival(s, S_TITLEBAR_ICON_MODE, 3);
		num(s, S_TITLEBAR_ICON_SIZE, 17.0);
		ival(s, S_BTN_STYLE, 5);
		num(s, S_BTN_SIZE, 17.0);
		num(s, S_BTN_WIDTH, 17.0);
		num(s, S_BTN_MARGIN, 1.0);
		col(s, S_BTN_COLOR, HEX(0xC0C0C0, 255));
		col(s, S_BTN_SYMBOL_COLOR, HEX(0x000000, 255));
		flag(s, S_BTN_CLOSE_ENABLED, false);
		col(s, S_BORDER_COLOR, HEX(0x000000, 255));

	} else if (strcmp(style, "winnt4") == 0 || strcmp(style, "win95") == 0 || strcmp(style, "classic") == 0) {
		classic_family(s, 0xC0C0C0, 0x000080, 0x000080, false);

	} else if (strcmp(style, "win98") == 0) {
		classic_family(s, 0xC0C0C0, 0x000080, 0x1084D0, true);

	} else if (strcmp(style, "win98_space") == 0) {
		classic_family(s, 0x4A4A6E, 0x000030, 0x3030B0, true);
		col(s, S_TITLE_COLOR, HEX(0xE0E0FF, 255));
		col(s, S_BTN_SYMBOL_COLOR, HEX(0xF0F0FF, 255));
		col(s, S_BTN_SYMBOL_CLOSE_COLOR, HEX(0xF0F0FF, 255));

	} else if (strcmp(style, "win98_creatures") == 0) {
		classic_family(s, 0xD8C8A0, 0x5A3408, 0xC89A4A, true);

	} else if (strcmp(style, "winme") == 0 || strcmp(style, "win2000") == 0 ||
		   strcmp(style, "winxp_classic") == 0) {
		classic_family(s, 0xD4D0C8, 0x0A246A, 0xA6CAF0, true);
		if (strcmp(style, "winxp_classic") == 0)
			flag(s, S_STATUSBAR_ENABLED, true);

	/* ------------------------------------------------------- XP / Luna */
	} else if (strcmp(style, "winxp_luna") == 0) {
		luna_family(s, 0x0831D9, 0x3C8CFF, 0x0046D0, 0x2D67E4, 0xFFFFFF, 0xFFFFFF);
		ival(s, S_TITLEBAR_STYLE, 1);
		col(s, S_BORDER_COLOR, HEX(0x0019A5, 255));

	} else if (strcmp(style, "winxp_olive") == 0) {
		luna_family(s, 0x7B9A45, 0xC2D488, 0x6A8934, 0x8CA84E, 0xFFFFFF, 0xFFFFFF);
		col(s, S_BORDER_COLOR, HEX(0x4B6220, 255));

	} else if (strcmp(style, "winxp_silver") == 0) {
		luna_family(s, 0xAEAEC6, 0xF2F2FA, 0xA4A4C0, 0xB4B5D2, 0x202040, 0x3A3A66);
		col(s, S_BORDER_COLOR, HEX(0x707090, 255));
		ival(s, S_TITLE_SHADOW, 0);

	} else if (strcmp(style, "winxp_royale") == 0) {
		luna_family(s, 0x143678, 0x5A86D0, 0x0E2A66, 0x2C5AB0, 0xFFFFFF, 0xFFFFFF);
		col(s, S_BORDER_COLOR, HEX(0x0A1C44, 255));

	} else if (strcmp(style, "winxp_zune") == 0) {
		luna_family(s, 0x262626, 0x525252, 0x141414, 0xE8621A, 0xFFFFFF, 0xFFFFFF);
		col(s, S_BTN_CLOSE_COLOR, HEX(0xE8621A, 255));
		col(s, S_BORDER_COLOR, HEX(0x0C0C0C, 255));

	} else if (strcmp(style, "winxp_embedded") == 0) {
		luna_family(s, 0x34496B, 0x8AA0C4, 0x40587E, 0x5670A0, 0xFFFFFF, 0xFFFFFF);
		col(s, S_BORDER_COLOR, HEX(0x1E2C44, 255));

	/* ----------------------------------------------------- Vista / 7 */
	} else if (strcmp(style, "winvista_aero") == 0) {
		aero_family(s, PACK(70, 92, 122, 235), true, 0xFFFFFF, 1);
	} else if (strcmp(style, "winvista_basic") == 0) {
		aero_family(s, HEX(0x5B7CA6, 255), false, 0xFFFFFF, 1);
	} else if (strcmp(style, "win7_aero") == 0) {
		aero_family(s, PACK(150, 190, 230, 205), true, 0x000000, 2);
	} else if (strcmp(style, "win7_basic") == 0) {
		aero_family(s, HEX(0x7F9DC4, 255), false, 0x000000, 0);

	} else if (strcmp(style, "win7_hc") == 0) {
		num(s, S_FRAME_THICKNESS, 3.0);
		num(s, S_TITLEBAR_HEIGHT, 28.0);
		col(s, S_FRAME_COLOR, HEX(0x000000, 255));
		col(s, S_BG_COLOR_TOP, HEX(0x000000, 255));
		col(s, S_BG_COLOR_BOTTOM, HEX(0x000000, 255));
		col(s, S_TITLEBAR_COLOR_A, HEX(0x000000, 255));
		col(s, S_TITLEBAR_COLOR_B, HEX(0x000000, 255));
		col(s, S_TITLE_COLOR, HEX(0xFFFFFF, 255));
		str(s, S_TITLE_FONT, "Segoe UI");
		num(s, S_TITLE_FONT_SIZE, 13.0);
		flag(s, S_TITLE_BOLD, true);
		ival(s, S_BTN_STYLE, 3);
		num(s, S_BTN_SIZE, 22.0);
		num(s, S_BTN_WIDTH, 30.0);
		num(s, S_BTN_SPACING, 2.0);
		num(s, S_BTN_MARGIN, 3.0);
		col(s, S_BTN_COLOR, HEX(0x000000, 255));
		col(s, S_BTN_SYMBOL_COLOR, HEX(0xFFFFFF, 255));
		col(s, S_BTN_SYMBOL_CLOSE_COLOR, HEX(0xFFFFFF, 255));
		col(s, S_BTN_CLOSE_COLOR, HEX(0x000000, 255));
		col(s, S_BORDER_COLOR, HEX(0xFFFFFF, 255));
		num(s, S_BORDER_THICKNESS, 2.0);

	/* --------------------------------------------------------- 8 / 8.1 */
	} else if (strcmp(style, "win8_metro") == 0) {
		num(s, S_FRAME_THICKNESS, 6.0);
		num(s, S_TITLEBAR_HEIGHT, 31.0);
		col(s, S_FRAME_COLOR, HEX(0x4A8AD4, 255));
		col(s, S_TITLEBAR_COLOR_A, HEX(0x4A8AD4, 255));
		col(s, S_TITLEBAR_COLOR_B, HEX(0x4A8AD4, 255));
		win8_buttons(s, 0x000000);
		col(s, S_BORDER_COLOR, HEX(0x3A78C0, 255));
		flag(s, S_SHADOW_ENABLED, true);
		num(s, S_SHADOW_OPACITY, 0.22);
		num(s, S_SHADOW_BLUR, 10.0);
		num(s, S_SHADOW_Y, 2.0);
		col(s, S_ACCENT_COLOR, HEX(0x4A8AD4, 255));

	} else if (strcmp(style, "win81_metro") == 0) {
		num(s, S_TITLEBAR_HEIGHT, 31.0);
		col(s, S_FRAME_COLOR, HEX(0x1979CA, 255));
		win8_buttons(s, 0x000000);
		col(s, S_BORDER_COLOR, HEX(0x1979CA, 255));
		flag(s, S_SHADOW_ENABLED, true);
		num(s, S_SHADOW_OPACITY, 0.22);
		num(s, S_SHADOW_BLUR, 10.0);
		num(s, S_SHADOW_Y, 2.0);
		col(s, S_ACCENT_COLOR, HEX(0x1979CA, 255));

	} else if (strcmp(style, "win8_app") == 0) {
		num(s, S_FRAME_THICKNESS, 6.0);
		num(s, S_TITLEBAR_HEIGHT, 56.0);
		col(s, S_FRAME_COLOR, HEX(0x4617B4, 255));
		col(s, S_BG_COLOR_TOP, HEX(0xFFFFFF, 255));
		col(s, S_BG_COLOR_BOTTOM, HEX(0xFFFFFF, 255));
		col(s, S_TITLEBAR_COLOR_A, HEX(0xFFFFFF, 255));
		col(s, S_TITLEBAR_COLOR_B, HEX(0xFFFFFF, 255));
		col(s, S_TITLE_COLOR, HEX(0x1A1A1A, 255));
		str(s, S_TITLE_FONT, "Segoe UI Light");
		num(s, S_TITLE_FONT_SIZE, 28.0);
		ival(s, S_TITLEBAR_ICON_MODE, 2);
		num(s, S_TITLEBAR_ICON_SIZE, 36.0);
		flag(s, S_BTN_MIN_ENABLED, false);
		flag(s, S_BTN_MAX_ENABLED, false);
		num(s, S_BTN_SIZE, 30.0);
		num(s, S_BTN_WIDTH, 46.0);
		col(s, S_BTN_SYMBOL_COLOR, HEX(0x1A1A1A, 255));
		col(s, S_BTN_SYMBOL_CLOSE_COLOR, HEX(0x1A1A1A, 255));
		col(s, S_BORDER_COLOR, HEX(0x4617B4, 255));
		col(s, S_ACCENT_COLOR, HEX(0x4617B4, 255));

	/* ------------------------------------------------------- 10 / 11 */
	} else if (strcmp(style, "win10_fluent") == 0 || strcmp(style, "win10_accent") == 0) {
		const bool accent = strcmp(style, "win10_accent") == 0;
		const int bg = dark ? 0x2B2B2B : 0xFFFFFF;
		const int tb = accent ? 0x0078D7 : bg;
		const int fg = (dark || accent) ? 0xFFFFFF : 0x000000;
		num(s, S_TITLEBAR_HEIGHT, 31.0);
		col(s, S_FRAME_COLOR, HEX(accent ? 0x0078D7 : bg, 255));
		col(s, S_BG_COLOR_TOP, HEX(bg, 255));
		col(s, S_BG_COLOR_BOTTOM, HEX(bg, 255));
		col(s, S_TITLEBAR_COLOR_A, HEX(tb, 255));
		col(s, S_TITLEBAR_COLOR_B, HEX(tb, 255));
		col(s, S_TITLE_COLOR, HEX(fg, 255));
		num(s, S_BTN_SIZE, 31.0);
		col(s, S_BTN_SYMBOL_COLOR, HEX(fg, 255));
		col(s, S_BTN_SYMBOL_CLOSE_COLOR, HEX(fg, 255));
		col(s, S_BORDER_COLOR, HEX(accent ? 0x0078D7 : (dark ? 0x4A4A4A : 0x9A9A9A), 255));
		flag(s, S_SHADOW_ENABLED, true);
		num(s, S_SHADOW_OPACITY, 0.30);
		num(s, S_SHADOW_BLUR, 18.0);
		col(s, S_ACCENT_COLOR, HEX(0x0078D7, 255));

	} else if (strcmp(style, "win10_tablet") == 0) {
		const int bg = dark ? 0x1F1F1F : 0xF2F2F2;
		const int fg = dark ? 0xFFFFFF : 0x000000;
		num(s, S_TITLEBAR_HEIGHT, 30.0);
		num(s, S_FRAME_THICKNESS, 0.0);
		col(s, S_FRAME_COLOR, HEX(bg, 255));
		col(s, S_BG_COLOR_TOP, HEX(bg, 255));
		col(s, S_BG_COLOR_BOTTOM, HEX(bg, 255));
		col(s, S_TITLEBAR_COLOR_A, HEX(bg, 255));
		col(s, S_TITLEBAR_COLOR_B, HEX(bg, 255));
		col(s, S_TITLE_COLOR, HEX(fg, 255));
		ival(s, S_TITLE_ALIGN, 1);
		ival(s, S_TITLEBAR_ICON_MODE, 4);
		flag(s, S_BTN_MIN_ENABLED, false);
		flag(s, S_BTN_MAX_ENABLED, false);
		num(s, S_BTN_SIZE, 30.0);
		col(s, S_BTN_SYMBOL_COLOR, HEX(fg, 255));
		col(s, S_BTN_SYMBOL_CLOSE_COLOR, HEX(fg, 255));
		flag(s, S_BORDER_ENABLED, false);

	} else if (strcmp(style, "win11_fluent") == 0 || strcmp(style, "win11_mica") == 0) {
		const bool mica = strcmp(style, "win11_mica") == 0;
		const int bg = dark ? 0x202020 : (mica ? 0xDCE6F5 : 0xF3F3F3);
		const int body = dark ? 0x202020 : 0xF3F3F3;
		const int fg = dark ? 0xFFFFFF : 0x1A1A1A;
		num(s, S_TITLEBAR_HEIGHT, 32.0);
		num(s, S_CORNER_RADIUS, 8.0);
		col(s, S_FRAME_COLOR, HEX(bg, 255));
		col(s, S_BG_COLOR_TOP, HEX(body, 255));
		col(s, S_BG_COLOR_BOTTOM, HEX(body, 255));
		col(s, S_TITLEBAR_COLOR_A, HEX(bg, 255));
		col(s, S_TITLEBAR_COLOR_B, HEX(bg, 255));
		col(s, S_TITLE_COLOR, HEX(fg, 255));
		num(s, S_BTN_SIZE, 32.0);
		col(s, S_BTN_SYMBOL_COLOR, HEX(fg, 255));
		col(s, S_BTN_SYMBOL_CLOSE_COLOR, HEX(fg, 255));
		col(s, S_BORDER_COLOR, mica ? PACK(0, 95, 184, 150)
					    : PACK(dark ? 255 : 0, dark ? 255 : 0, dark ? 255 : 0, dark ? 40 : 60));
		flag(s, S_SHADOW_ENABLED, true);
		num(s, S_SHADOW_OPACITY, 0.38);
		num(s, S_SHADOW_BLUR, 26.0);
		num(s, S_SHADOW_Y, 10.0);
		col(s, S_ACCENT_COLOR, HEX(0x005FB8, 255));
		if (mica)
			flag(s, S_SNAP_FLYOUT, true);

	/* --------------------------------------------------- Windows Phone */
	} else if (strcmp(style, "wp7") == 0 || strcmp(style, "wp8") == 0) {
		const bool wp8 = strcmp(style, "wp8") == 0;
		const int accent = wp8 ? 0x0050EF : 0x1BA1E2;
		num(s, S_FRAME_THICKNESS, 8.0);
		num(s, S_TITLEBAR_HEIGHT, 54.0);
		col(s, S_FRAME_COLOR, HEX(0x000000, 255));
		col(s, S_BG_COLOR_TOP, HEX(0x000000, 255));
		col(s, S_BG_COLOR_BOTTOM, HEX(0x000000, 255));
		col(s, S_TITLEBAR_COLOR_A, HEX(0x000000, 255));
		col(s, S_TITLEBAR_COLOR_B, HEX(0x000000, 255));
		col(s, S_TITLE_COLOR, HEX(0xFFFFFF, 255));
		str(s, S_TITLE_FONT, "Segoe UI Light");
		num(s, S_TITLE_FONT_SIZE, 32.0);
		flag(s, S_TITLE_LOWER, true);
		ival(s, S_TITLEBAR_ICON_MODE, 0);
		flag(s, S_BTN_MIN_ENABLED, false);
		flag(s, S_BTN_MAX_ENABLED, false);
		flag(s, S_BTN_CLOSE_ENABLED, false);
		col(s, S_BORDER_COLOR, HEX(accent, 255));
		num(s, S_BORDER_THICKNESS, 3.0);
		col(s, S_ACCENT_COLOR, HEX(accent, 255));
		if (wp8) {
			flag(s, S_STATUSBAR_ENABLED, true);
			flag(s, S_TILES, true);
			str(s, S_STATUSBAR_TEXT, "");
		}

	/* ------------------------------------------------------ other OSes */
	} else if (strcmp(style, "macos") == 0) {
		num(s, S_TITLEBAR_HEIGHT, 28.0);
		num(s, S_CORNER_RADIUS, 10.0);
		col(s, S_FRAME_COLOR, HEX(0xECECEC, 255));
		col(s, S_BG_COLOR_TOP, HEX(0xECECEC, 255));
		col(s, S_BG_COLOR_BOTTOM, HEX(0xECECEC, 255));
		flag(s, S_TITLEBAR_GRADIENT, true);
		col(s, S_TITLEBAR_COLOR_A, HEX(0xEBEBEB, 255));
		col(s, S_TITLEBAR_COLOR_B, HEX(0xD3D3D3, 255));
		col(s, S_TITLE_COLOR, HEX(0x4D4D4D, 255));
		num(s, S_TITLE_FONT_SIZE, 13.0);
		ival(s, S_TITLE_ALIGN, 1);
		ival(s, S_TITLEBAR_ICON_MODE, 0);
		ival(s, S_BTN_STYLE, 4);
		num(s, S_BTN_SIZE, 12.0);
		num(s, S_BTN_WIDTH, 12.0);
		num(s, S_BTN_SPACING, 8.0);
		num(s, S_BTN_MARGIN, 9.0);
		col(s, S_BORDER_COLOR, PACK(0, 0, 0, 70));
		flag(s, S_SHADOW_ENABLED, true);
		num(s, S_SHADOW_OPACITY, 0.5);
		num(s, S_SHADOW_BLUR, 30.0);
		num(s, S_SHADOW_Y, 12.0);

	} else if (strcmp(style, "gnome") == 0) {
		num(s, S_TITLEBAR_HEIGHT, 46.0);
		num(s, S_CORNER_RADIUS, 12.0);
		flag(s, S_CORNER_TOP_ONLY, false);
		col(s, S_FRAME_COLOR, HEX(0xFAFAFA, 255));
		col(s, S_BG_COLOR_TOP, HEX(0xFAFAFA, 255));
		col(s, S_BG_COLOR_BOTTOM, HEX(0xFAFAFA, 255));
		flag(s, S_TITLEBAR_GRADIENT, true);
		col(s, S_TITLEBAR_COLOR_A, HEX(0xEBEBEB, 255));
		col(s, S_TITLEBAR_COLOR_B, HEX(0xDCDCDC, 255));
		col(s, S_TITLE_COLOR, HEX(0x2E3436, 255));
		num(s, S_TITLE_FONT_SIZE, 14.0);
		flag(s, S_TITLE_BOLD, true);
		ival(s, S_TITLE_ALIGN, 1);
		ival(s, S_TITLEBAR_ICON_MODE, 0);
		ival(s, S_BTN_STYLE, 6);
		num(s, S_BTN_SIZE, 26.0);
		num(s, S_BTN_WIDTH, 26.0);
		num(s, S_BTN_SPACING, 6.0);
		num(s, S_BTN_MARGIN, 10.0);
		col(s, S_BTN_COLOR, HEX(0xDADADA, 255));
		col(s, S_BTN_CLOSE_COLOR, HEX(0xDADADA, 255));
		col(s, S_BTN_SYMBOL_COLOR, HEX(0x2E3436, 255));
		col(s, S_BTN_SYMBOL_CLOSE_COLOR, HEX(0x2E3436, 255));
		col(s, S_BORDER_COLOR, HEX(0xB8B8B8, 255));
		flag(s, S_SHADOW_ENABLED, true);
		num(s, S_SHADOW_OPACITY, 0.45);
		num(s, S_SHADOW_BLUR, 24.0);
		num(s, S_SHADOW_Y, 8.0);

	} else if (strcmp(style, "kde") == 0) {
		num(s, S_TITLEBAR_HEIGHT, 30.0);
		num(s, S_CORNER_RADIUS, 5.0);
		col(s, S_FRAME_COLOR, HEX(0xEFF0F1, 255));
		col(s, S_BG_COLOR_TOP, HEX(0xEFF0F1, 255));
		col(s, S_BG_COLOR_BOTTOM, HEX(0xEFF0F1, 255));
		col(s, S_TITLEBAR_COLOR_A, HEX(0xEFF0F1, 255));
		col(s, S_TITLEBAR_COLOR_B, HEX(0xEFF0F1, 255));
		col(s, S_TITLE_COLOR, HEX(0x31363B, 255));
		ival(s, S_TITLE_ALIGN, 1);
		ival(s, S_TITLEBAR_ICON_MODE, 1);
		num(s, S_BTN_SIZE, 22.0);
		num(s, S_BTN_WIDTH, 22.0);
		num(s, S_BTN_SPACING, 4.0);
		num(s, S_BTN_MARGIN, 6.0);
		col(s, S_BTN_SYMBOL_COLOR, HEX(0x31363B, 255));
		col(s, S_BTN_SYMBOL_CLOSE_COLOR, HEX(0x31363B, 255));
		col(s, S_BORDER_COLOR, HEX(0x3DAEE9, 255));
		flag(s, S_SHADOW_ENABLED, true);
		num(s, S_SHADOW_OPACITY, 0.40);
		num(s, S_SHADOW_BLUR, 20.0);
		col(s, S_ACCENT_COLOR, HEX(0x3DAEE9, 255));

	} else if (strcmp(style, "amiga") == 0) {
		num(s, S_FRAME_THICKNESS, 3.0);
		num(s, S_TITLEBAR_HEIGHT, 20.0);
		flag(s, S_TITLEBAR_INSET, true);
		col(s, S_FRAME_COLOR, HEX(0xFFFFFF, 255));
		col(s, S_BG_COLOR_TOP, HEX(0x0055AA, 255));
		col(s, S_BG_COLOR_BOTTOM, HEX(0x0055AA, 255));
		col(s, S_TITLEBAR_COLOR_A, HEX(0xFFFFFF, 255));
		col(s, S_TITLEBAR_COLOR_B, HEX(0xFFFFFF, 255));
		col(s, S_TITLE_COLOR, HEX(0x000000, 255));
		str(s, S_TITLE_FONT, "Courier New");
		num(s, S_TITLE_FONT_SIZE, 13.0);
		flag(s, S_TITLE_BOLD, true);
		ival(s, S_TITLEBAR_ICON_MODE, 5);
		num(s, S_TITLEBAR_ICON_SIZE, 18.0);
		flag(s, S_BTN_CLOSE_ENABLED, false);
		ival(s, S_BTN_STYLE, 7);
		num(s, S_BTN_SIZE, 18.0);
		num(s, S_BTN_WIDTH, 22.0);
		num(s, S_BTN_MARGIN, 1.0);
		col(s, S_BTN_COLOR, HEX(0xFFFFFF, 255));
		col(s, S_BTN_SYMBOL_COLOR, HEX(0x000000, 255));
		col(s, S_BORDER_COLOR, HEX(0x000000, 255));

	} else if (strcmp(style, "atari") == 0) {
		num(s, S_FRAME_THICKNESS, 2.0);
		num(s, S_TITLEBAR_HEIGHT, 20.0);
		flag(s, S_TITLEBAR_INSET, true);
		col(s, S_FRAME_COLOR, HEX(0xFFFFFF, 255));
		col(s, S_BG_COLOR_TOP, HEX(0xFFFFFF, 255));
		col(s, S_BG_COLOR_BOTTOM, HEX(0xFFFFFF, 255));
		ival(s, S_TITLEBAR_STYLE, 3);
		col(s, S_TITLEBAR_COLOR_A, HEX(0x000000, 255));
		col(s, S_TITLEBAR_COLOR_B, HEX(0xFFFFFF, 255));
		col(s, S_TITLE_COLOR, HEX(0x000000, 255));
		str(s, S_TITLE_FONT, "Consolas");
		num(s, S_TITLE_FONT_SIZE, 13.0);
		flag(s, S_TITLE_BOLD, true);
		ival(s, S_TITLE_ALIGN, 1);
		ival(s, S_TITLEBAR_ICON_MODE, 6);
		num(s, S_TITLEBAR_ICON_SIZE, 18.0);
		flag(s, S_BTN_MIN_ENABLED, false);
		flag(s, S_BTN_CLOSE_ENABLED, false);
		ival(s, S_BTN_STYLE, 7);
		num(s, S_BTN_SIZE, 18.0);
		num(s, S_BTN_WIDTH, 22.0);
		num(s, S_BTN_MARGIN, 1.0);
		col(s, S_BTN_COLOR, HEX(0xFFFFFF, 255));
		col(s, S_BTN_SYMBOL_COLOR, HEX(0x000000, 255));
		col(s, S_BORDER_COLOR, HEX(0x000000, 255));

	} else if (strcmp(style, "custom") == 0) {
		/* neutral flat baseline as a starting point */
	}
}
