#include <string.h>
#include <stdio.h>
#include <obs-module.h>
#include <graphics/graphics.h>
#include <util/platform.h>
#include <util/dstr.h>

#include "win-frame-filter.h"
#include "win-frame-internal.h"
#include "win-frame-render.h"
#include "win-frame-styles.h"
#include "win-frame-presets.h"
#include "wf-prefs.h"
#include "ui/dialogs.h"

/* ======================================================================
 * lifecycle
 * ==================================================================== */

static const char *wf_get_name(void *unused)
{
	UNUSED_PARAMETER(unused);
	return obs_module_text("WinFrameFilter.Name");
}

static void wf_update(void *data, obs_data_t *settings);

static void *wf_create(obs_data_t *settings, obs_source_t *source)
{
	struct win_frame_filter *f = bzalloc(sizeof(struct win_frame_filter));
	f->source = source;

	char *effect_path = obs_module_file("effects/win_frame.effect");
	obs_enter_graphics();
	char *err = NULL;
	f->effect = gs_effect_create_from_file(effect_path, &err);
	obs_leave_graphics();
	if (!f->effect) {
		blog(LOG_ERROR, "[win-frame-filter] failed to load effect '%s': %s", effect_path,
		     err ? err : "unknown error");
	} else {
		win_frame_cache_eparams(f);
	}
	bfree(err);
	bfree(effect_path);

	f->anim_alpha = 1.0f;
	f->anim_scale = 1.0f;

	wf_update(f, settings);
	return f;
}

static void wf_destroy(void *data)
{
	struct win_frame_filter *f = data;
	win_frame_release_cached_assets(f);
	if (f->cached_settings)
		obs_data_release(f->cached_settings);
	if (f->effect) {
		obs_enter_graphics();
		gs_effect_destroy(f->effect);
		obs_leave_graphics();
	}
	bfree(f);
}

static void wf_update(void *data, obs_data_t *settings)
{
	struct win_frame_filter *f = data;

	obs_data_addref(settings);
	if (f->cached_settings)
		obs_data_release(f->cached_settings);
	f->cached_settings = settings;

	obs_source_t *target = obs_filter_get_target(f->source);
	uint32_t base_w = target ? obs_source_get_base_width(target) : 0;
	uint32_t base_h = target ? obs_source_get_base_height(target) : 0;
	if (base_w == 0)
		base_w = 640;
	if (base_h == 0)
		base_h = 480;

	win_frame_layout_update(f, settings, base_w, base_h);
}

static void wf_get_defaults(obs_data_t *settings)
{
	/* apply_style_defaults writes real values; libobs needs real *defaults*
	 * here, otherwise they would be applied over the user's saved settings. */
	obs_data_t *tmp = obs_data_create();
	const char *def_style = wf_prefs_get()->default_style;
	win_frame_apply_style_defaults(tmp, def_style);
	for (obs_data_item_t *it = obs_data_first(tmp); it; obs_data_item_next(&it)) {
		const char *name = obs_data_item_get_name(it);
		switch (obs_data_item_gettype(it)) {
		case OBS_DATA_STRING:
			obs_data_set_default_string(settings, name, obs_data_item_get_string(it));
			break;
		case OBS_DATA_NUMBER:
			if (obs_data_item_numtype(it) == OBS_DATA_NUM_INT)
				obs_data_set_default_int(settings, name, obs_data_item_get_int(it));
			else
				obs_data_set_default_double(settings, name, obs_data_item_get_double(it));
			break;
		case OBS_DATA_BOOLEAN:
			obs_data_set_default_bool(settings, name, obs_data_item_get_bool(it));
			break;
		default:
			break;
		}
	}
	obs_data_release(tmp);
	obs_data_set_default_string(settings, S_STYLE, def_style);
}

/* ======================================================================
 * properties (the full customization panel)
 * ==================================================================== */

static bool style_changed_cb(obs_properties_t *props, obs_property_t *p, obs_data_t *settings)
{
	UNUSED_PARAMETER(p);
	UNUSED_PARAMETER(props);
	const char *style = obs_data_get_string(settings, S_STYLE);
	if (strcmp(style, "custom") != 0)
		win_frame_apply_style_defaults(settings, style);
	return true; /* refresh whole panel so new colors are reflected */
}

static bool mask_type_changed_cb(obs_properties_t *props, obs_property_t *p, obs_data_t *settings)
{
	UNUSED_PARAMETER(p);
	int type = (int)obs_data_get_int(settings, S_MASK_TYPE);
	obs_property_t *img = obs_properties_get(props, S_MASK_IMAGE_PATH);
	obs_property_set_visible(img, type == 4);
	return true;
}

static bool preset_save_clicked(obs_properties_t *props, obs_property_t *p, void *data)
{
	UNUSED_PARAMETER(p);
	struct win_frame_filter *f = data;
	obs_data_t *settings = obs_source_get_settings(f->source);
	const char *name = obs_data_get_string(settings, S_PRESET_NAME);
	if (name && *name)
		win_frame_presets_save(name, settings);
	obs_data_release(settings);

	obs_property_t *list = obs_properties_get(props, S_PRESET_LIST);
	if (list)
		win_frame_presets_populate_list(list);
	return true;
}

static bool preset_load_apply(struct win_frame_filter *f, const char *name)
{
	obs_data_t *preset = win_frame_presets_load(name);
	if (!preset)
		return false; /* missing/corrupt preset: silently ignored, never crashes */
	obs_data_t *settings = obs_source_get_settings(f->source);
	obs_data_apply(settings, preset);
	obs_data_release(preset);
	obs_source_update(f->source, settings);
	obs_data_release(settings);
	return true;
}

static bool preset_list_changed_cb(obs_properties_t *props, obs_property_t *p, obs_data_t *settings)
{
	UNUSED_PARAMETER(props);
	UNUSED_PARAMETER(p);
	const char *name = obs_data_get_string(settings, S_PRESET_LIST);
	obs_data_set_string(settings, S_PRESET_NAME, name);
	return true;
}

static bool preset_delete_clicked(obs_properties_t *props, obs_property_t *p, void *data)
{
	UNUSED_PARAMETER(p);
	struct win_frame_filter *f = data;
	obs_data_t *settings = obs_source_get_settings(f->source);
	const char *name = obs_data_get_string(settings, S_PRESET_LIST);
	win_frame_presets_delete(name);
	obs_data_release(settings);

	obs_property_t *list = obs_properties_get(props, S_PRESET_LIST);
	if (list)
		win_frame_presets_populate_list(list);
	return true;
}

static bool preset_duplicate_clicked(obs_properties_t *props, obs_property_t *p, void *data)
{
	UNUSED_PARAMETER(p);
	struct win_frame_filter *f = data;
	obs_data_t *settings = obs_source_get_settings(f->source);
	const char *name = obs_data_get_string(settings, S_PRESET_LIST);
	struct dstr new_name;
	dstr_init_copy(&new_name, name);
	dstr_cat(&new_name, " Copy");
	win_frame_presets_duplicate(name, new_name.array);
	dstr_free(&new_name);
	obs_data_release(settings);

	obs_property_t *list = obs_properties_get(props, S_PRESET_LIST);
	if (list)
		win_frame_presets_populate_list(list);
	return true;
}

static bool preset_reset_clicked(obs_properties_t *props, obs_property_t *p, void *data)
{
	UNUSED_PARAMETER(props);
	UNUSED_PARAMETER(p);
	struct win_frame_filter *f = data;
	obs_data_t *settings = obs_source_get_settings(f->source);
	const char *style = obs_data_get_string(settings, S_STYLE);
	win_frame_apply_style_defaults(settings, style);
	obs_source_update(f->source, settings);
	obs_data_release(settings);
	return true;
}

static bool preset_apply_clicked(obs_properties_t *props, obs_property_t *p, void *data)
{
	UNUSED_PARAMETER(props);
	UNUSED_PARAMETER(p);
	struct win_frame_filter *f = data;
	obs_data_t *settings = obs_source_get_settings(f->source);
	const char *name = obs_data_get_string(settings, S_PRESET_LIST);
	obs_data_release(settings);
	preset_load_apply(f, name);
	return true;
}


static void refresh_preset_list(obs_properties_t *props)
{
	obs_property_t *list = obs_properties_get(props, S_PRESET_LIST);
	if (list)
		win_frame_presets_populate_list(list);
}

static bool preset_import_clicked(obs_properties_t *props, obs_property_t *p, void *data)
{
	UNUSED_PARAMETER(p);
	UNUSED_PARAMETER(data);
	char path[1024];
	if (!wf_ui_get_open_path("Import preset", "Preset (*.json)", path, sizeof(path)))
		return false;
	/* preset name = file name without folder and extension */
	const char *base = path;
	for (const char *c = path; *c; c++)
		if (*c == '/' || *c == '\\')
			base = c + 1;
	char name[256];
	strncpy(name, base, sizeof(name) - 1);
	name[sizeof(name) - 1] = 0;
	char *dot = strrchr(name, '.');
	if (dot)
		*dot = 0;
	if (!name[0])
		strcpy(name, "Imported preset");
	if (!win_frame_presets_import(path, name))
		wf_ui_message("Import preset", "That file is not a valid preset (unreadable or not JSON).");
	refresh_preset_list(props);
	return true;
}

static bool preset_export_clicked(obs_properties_t *props, obs_property_t *p, void *data)
{
	UNUSED_PARAMETER(props);
	UNUSED_PARAMETER(p);
	struct win_frame_filter *f = data;
	obs_data_t *settings = obs_source_get_settings(f->source);
	char name[256];
	strncpy(name, obs_data_get_string(settings, S_PRESET_LIST), sizeof(name) - 1);
	name[sizeof(name) - 1] = 0;
	obs_data_release(settings);
	if (!name[0]) {
		wf_ui_message("Export preset", "Select a preset in the Saved Presets list first.");
		return false;
	}
	char def[300];
	snprintf(def, sizeof(def), "%s.json", name);
	char path[1024];
	if (!wf_ui_get_save_path("Export preset", def, "Preset (*.json)", path, sizeof(path)))
		return false;
	if (!win_frame_presets_export(name, path))
		wf_ui_message("Export preset", "The preset could not be written to that location.");
	return false;
}

static bool preset_rename_clicked(obs_properties_t *props, obs_property_t *p, void *data)
{
	UNUSED_PARAMETER(p);
	struct win_frame_filter *f = data;
	obs_data_t *settings = obs_source_get_settings(f->source);
	char old_name[256];
	strncpy(old_name, obs_data_get_string(settings, S_PRESET_LIST), sizeof(old_name) - 1);
	old_name[sizeof(old_name) - 1] = 0;
	if (!old_name[0]) {
		obs_data_release(settings);
		wf_ui_message("Rename preset", "Select a preset in the Saved Presets list first.");
		return false;
	}
	char new_name[256];
	if (wf_ui_get_text("Rename preset", "New name:", old_name, new_name, sizeof(new_name)) &&
	    strcmp(new_name, old_name) != 0) {
		if (win_frame_presets_rename(old_name, new_name)) {
			obs_data_set_string(settings, S_PRESET_LIST, new_name);
			obs_data_set_string(settings, S_PRESET_NAME, new_name);
		} else {
			wf_ui_message("Rename preset", "The preset could not be renamed.");
		}
	}
	obs_data_release(settings);
	refresh_preset_list(props);
	return true;
}

static bool preset_download_clicked(obs_properties_t *props, obs_property_t *p, void *data)
{
	UNUSED_PARAMETER(props);
	UNUSED_PARAMETER(p);
	UNUSED_PARAMETER(data);
	wf_ui_show_download_dialog();
	return false;
}

static obs_properties_t *wf_get_properties(void *data)
{
	struct win_frame_filter *f = data;
	obs_properties_t *p = obs_properties_create();

	/* -------- GENERAL -------- */
	obs_properties_t *general = obs_properties_create();
	obs_property_t *style = obs_properties_add_list(general, S_STYLE, obs_module_text("Style"), OBS_COMBO_TYPE_LIST,
							 OBS_COMBO_FORMAT_STRING);
	for (int i = 0; i < STYLE_COUNT; i++)
		obs_property_list_add_string(style, win_frame_style_table[i].display, win_frame_style_table[i].id);
	obs_property_set_modified_callback(style, style_changed_cb);

	obs_properties_add_bool(general, S_ENABLED, obs_module_text("FrameEnabled"));
	obs_properties_add_float_slider(general, S_SCALE, obs_module_text("OverallScale"), 0.1, 4.0, 0.01);
	obs_properties_add_float_slider(general, S_UI_SCALE, obs_module_text("UIScale"), 0.5, 4.0, 0.05);
	obs_properties_add_int_slider(general, S_PADDING, obs_module_text("Padding"), 0, 100, 1);
	obs_properties_add_float_slider(general, S_ROTATION, obs_module_text("Rotation"), -180.0, 180.0, 0.5);
	obs_property_t *ah = obs_properties_add_list(general, S_ALIGN_H, obs_module_text("AlignH"), OBS_COMBO_TYPE_LIST,
						      OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(ah, obs_module_text("Left"), 0);
	obs_property_list_add_int(ah, obs_module_text("Center"), 1);
	obs_property_list_add_int(ah, obs_module_text("Right"), 2);
	obs_property_t *av = obs_properties_add_list(general, S_ALIGN_V, obs_module_text("AlignV"), OBS_COMBO_TYPE_LIST,
						      OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(av, obs_module_text("Top"), 0);
	obs_property_list_add_int(av, obs_module_text("Center"), 1);
	obs_property_list_add_int(av, obs_module_text("Bottom"), 2);
	obs_properties_add_group(p, "grp_general", obs_module_text("General"), OBS_GROUP_NORMAL, general);

	/* -------- CAMERA -------- */
	obs_properties_t *camera = obs_properties_create();
	obs_properties_add_float_slider(camera, S_CAM_CROP_L, obs_module_text("CropLeft"), 0.0, 0.49, 0.01);
	obs_properties_add_float_slider(camera, S_CAM_CROP_R, obs_module_text("CropRight"), 0.0, 0.49, 0.01);
	obs_properties_add_float_slider(camera, S_CAM_CROP_T, obs_module_text("CropTop"), 0.0, 0.49, 0.01);
	obs_properties_add_float_slider(camera, S_CAM_CROP_B, obs_module_text("CropBottom"), 0.0, 0.49, 0.01);
	obs_properties_add_float_slider(camera, S_CAM_ZOOM, obs_module_text("Zoom"), 0.2, 4.0, 0.01);
	obs_properties_add_float_slider(camera, S_CAM_POS_X, obs_module_text("PositionX"), -1.0, 1.0, 0.01);
	obs_properties_add_float_slider(camera, S_CAM_POS_Y, obs_module_text("PositionY"), -1.0, 1.0, 0.01);
	obs_properties_add_float_slider(camera, S_CAM_PIXELATE, obs_module_text("Pixelate"), 0.0, 40.0, 1.0);
	obs_properties_add_float_slider(camera, S_CAM_SCANLINES, obs_module_text("Scanlines"), 0.0, 1.0, 0.01);
	obs_properties_add_float_slider(camera, S_CAM_CRT, obs_module_text("CrtEffect"), 0.0, 1.0, 0.01);
	obs_properties_add_group(p, "grp_camera", obs_module_text("Camera"), OBS_GROUP_NORMAL, camera);

	/* -------- FRAME -------- */
	obs_properties_t *frame = obs_properties_create();
	obs_properties_add_float_slider(frame, S_FRAME_THICKNESS, obs_module_text("FrameThickness"), 0.0, 40.0, 0.5);
	obs_properties_add_float_slider(frame, S_CORNER_RADIUS, obs_module_text("CornerRadius"), 0.0, 60.0, 0.5);
	obs_properties_add_bool(frame, S_CORNER_TOP_ONLY, obs_module_text("CornerTopOnly"));
	obs_property_t *fs = obs_properties_add_list(frame, S_FRAME_STYLE, obs_module_text("FrameStyle"),
						      OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(fs, obs_module_text("FrameFlat"), 0);
	obs_property_list_add_int(fs, obs_module_text("FrameBevel"), 1);
	obs_properties_add_color_alpha(frame, S_FRAME_COLOR, obs_module_text("FrameColor"));
	obs_properties_add_bool(frame, S_CAM_BEVEL, obs_module_text("SunkenEdge"));
	obs_properties_add_group(p, "grp_frame", obs_module_text("Frame"), OBS_GROUP_NORMAL, frame);

	/* -------- TITLE BAR -------- */
	obs_properties_t *tb = obs_properties_create();
	obs_properties_add_bool(tb, S_TITLEBAR_ENABLED, obs_module_text("Enabled"));
	obs_properties_add_float_slider(tb, S_TITLEBAR_HEIGHT, obs_module_text("Height"), 12.0, 60.0, 1.0);
	obs_properties_add_text(tb, S_TITLE_TEXT, obs_module_text("TitleText"), OBS_TEXT_DEFAULT);
	obs_properties_add_text(tb, S_TITLE_FONT, obs_module_text("FontName"), OBS_TEXT_DEFAULT);
	obs_properties_add_float_slider(tb, S_TITLE_FONT_SIZE, obs_module_text("FontSize"), 6.0, 40.0, 1.0);
	obs_properties_add_bool(tb, S_TITLE_BOLD, obs_module_text("Bold"));
	obs_property_t *tsh = obs_properties_add_list(tb, S_TITLE_SHADOW, obs_module_text("TextEffect"),
						       OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(tsh, obs_module_text("None"), 0);
	obs_property_list_add_int(tsh, obs_module_text("DropShadow"), 1);
	obs_property_list_add_int(tsh, obs_module_text("GlowText"), 2);
	obs_property_t *tal = obs_properties_add_list(tb, S_TITLE_ALIGN, obs_module_text("TextAlign"),
						       OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(tal, obs_module_text("Left"), 0);
	obs_property_list_add_int(tal, obs_module_text("Center"), 1);
	obs_property_t *tst = obs_properties_add_list(tb, S_TITLEBAR_STYLE, obs_module_text("TitlebarStyle"),
						       OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(tst, obs_module_text("TitlebarPlain"), 0);
	obs_property_list_add_int(tst, obs_module_text("TitlebarLuna"), 1);
	obs_properties_add_bool(tb, S_TITLEBAR_GRAD_HORIZ, obs_module_text("GradientHorizontal"));
	obs_properties_add_bool(tb, S_TITLEBAR_INSET, obs_module_text("InsetInFrame"));
	obs_property_t *imode = obs_properties_add_list(tb, S_TITLEBAR_ICON_MODE, obs_module_text("TitleIcon"),
							 OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(imode, obs_module_text("None"), 0);
	obs_property_list_add_int(imode, obs_module_text("IconCamera"), 1);
	obs_property_list_add_int(imode, obs_module_text("IconBack"), 2);
	obs_property_list_add_int(imode, obs_module_text("IconSysMenu"), 3);
	obs_property_list_add_int(imode, obs_module_text("IconHamburger"), 4);
	obs_property_list_add_int(imode, obs_module_text("IconAmiga"), 5);
	obs_property_list_add_int(imode, obs_module_text("IconGem"), 6);
	obs_properties_add_bool(tb, S_TITLE_LOWER, obs_module_text("Lowercase"));
	obs_properties_add_bool(tb, S_TILES, obs_module_text("StatusTiles"));
	obs_properties_add_color_alpha(tb, S_TITLE_COLOR, obs_module_text("TextColor"));
	obs_properties_add_color_alpha(tb, S_TITLEBAR_COLOR_A, obs_module_text("TitlebarColorTop"));
	obs_properties_add_color_alpha(tb, S_TITLEBAR_COLOR_B, obs_module_text("TitlebarColorBottom"));
	obs_properties_add_bool(tb, S_TITLEBAR_GRADIENT, obs_module_text("UseGradient"));
	obs_properties_add_float_slider(tb, S_GLASS_REFLECTION, obs_module_text("GlassReflection"), 0.0, 1.0, 0.01);
	obs_properties_add_path(tb, S_TITLEBAR_ICON_PATH, obs_module_text("IconImage"), OBS_PATH_FILE,
				 "Images (*.png *.jpg *.jpeg *.bmp)", NULL);
	obs_properties_add_float_slider(tb, S_TITLEBAR_ICON_SIZE, obs_module_text("IconSize"), 8.0, 48.0, 1.0);
	obs_properties_add_bool(tb, S_STATUSBAR_ENABLED, obs_module_text("StatusBarEnabled"));
	obs_properties_add_text(tb, S_STATUSBAR_TEXT, obs_module_text("StatusBarText"), OBS_TEXT_DEFAULT);
	obs_properties_add_bool(tb, S_RESIZEGRIP_ENABLED, obs_module_text("ResizeGripEnabled"));
	obs_properties_add_group(p, "grp_titlebar", obs_module_text("TitleBar"), OBS_GROUP_NORMAL, tb);

	/* -------- WINDOW BUTTONS -------- */
	obs_properties_t *btn = obs_properties_create();
	obs_properties_add_bool(btn, S_BTN_MIN_ENABLED, obs_module_text("MinimizeEnabled"));
	obs_properties_add_bool(btn, S_BTN_MAX_ENABLED, obs_module_text("MaximizeEnabled"));
	obs_properties_add_bool(btn, S_BTN_CLOSE_ENABLED, obs_module_text("CloseEnabled"));
	obs_property_t *bst = obs_properties_add_list(btn, S_BTN_STYLE, obs_module_text("ButtonStyle"),
						       OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(bst, obs_module_text("BtnClassic"), 0);
	obs_property_list_add_int(bst, obs_module_text("BtnLuna"), 1);
	obs_property_list_add_int(bst, obs_module_text("BtnAero"), 2);
	obs_property_list_add_int(bst, obs_module_text("BtnFlat"), 3);
	obs_property_list_add_int(bst, obs_module_text("BtnMac"), 4);
	obs_property_list_add_int(bst, obs_module_text("BtnWin31"), 5);
	obs_property_list_add_int(bst, obs_module_text("BtnGnome"), 6);
	obs_property_list_add_int(bst, obs_module_text("BtnAmiga"), 7);
	obs_properties_add_float_slider(btn, S_BTN_SIZE, obs_module_text("ButtonHeight"), 10.0, 48.0, 1.0);
	obs_properties_add_float_slider(btn, S_BTN_WIDTH, obs_module_text("ButtonWidth"), 10.0, 80.0, 1.0);
	obs_properties_add_float_slider(btn, S_BTN_MARGIN, obs_module_text("ButtonMargin"), 0.0, 24.0, 1.0);
	obs_property_t *hov = obs_properties_add_list(btn, S_BTN_HOVER, obs_module_text("ButtonHover"),
						       OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(hov, obs_module_text("None"), 0);
	obs_property_list_add_int(hov, obs_module_text("Minimize"), 1);
	obs_property_list_add_int(hov, obs_module_text("Maximize"), 2);
	obs_property_list_add_int(hov, obs_module_text("Close"), 3);
	obs_properties_add_bool(btn, S_SNAP_FLYOUT, obs_module_text("SnapFlyout"));
	obs_properties_add_float_slider(btn, S_BTN_SPACING, obs_module_text("ButtonSpacing"), 0.0, 12.0, 1.0);
	obs_properties_add_color_alpha(btn, S_BTN_COLOR, obs_module_text("ButtonColor"));
	obs_properties_add_color_alpha(btn, S_BTN_CLOSE_COLOR, obs_module_text("CloseButtonColor"));
	obs_properties_add_color_alpha(btn, S_BTN_SYMBOL_COLOR, obs_module_text("SymbolColor"));
	obs_properties_add_color_alpha(btn, S_BTN_SYMBOL_CLOSE_COLOR, obs_module_text("CloseSymbolColor"));
	obs_properties_add_group(p, "grp_buttons", obs_module_text("WindowButtons"), OBS_GROUP_NORMAL, btn);

	/* -------- BORDER -------- */
	obs_properties_t *border = obs_properties_create();
	obs_properties_add_bool(border, S_BORDER_ENABLED, obs_module_text("Enabled"));
	obs_properties_add_float_slider(border, S_BORDER_THICKNESS, obs_module_text("Thickness"), 0.0, 12.0, 0.5);
	obs_properties_add_float_slider(border, S_BORDER_OPACITY, obs_module_text("Opacity"), 0.0, 1.0, 0.01);
	obs_properties_add_color_alpha(border, S_BORDER_COLOR, obs_module_text("Color"));
	obs_properties_add_bool(border, S_BORDER_DOUBLE, obs_module_text("DoubleBorder"));
	obs_properties_add_color_alpha(border, S_BORDER_INNER_COLOR, obs_module_text("InnerColor"));
	obs_properties_add_bool(border, S_BORDER_DASHED, obs_module_text("Dashed"));
	obs_properties_add_group(p, "grp_border", obs_module_text("Border"), OBS_GROUP_NORMAL, border);

	/* -------- SHADOW -------- */
	obs_properties_t *shadow = obs_properties_create();
	obs_properties_add_bool(shadow, S_SHADOW_ENABLED, obs_module_text("Enabled"));
	obs_properties_add_color_alpha(shadow, S_SHADOW_COLOR, obs_module_text("Color"));
	obs_properties_add_float_slider(shadow, S_SHADOW_OPACITY, obs_module_text("Opacity"), 0.0, 1.0, 0.01);
	obs_properties_add_float_slider(shadow, S_SHADOW_BLUR, obs_module_text("Blur"), 0.0, 80.0, 1.0);
	obs_properties_add_float_slider(shadow, S_SHADOW_SPREAD, obs_module_text("Spread"), -20.0, 40.0, 1.0);
	obs_properties_add_float_slider(shadow, S_SHADOW_X, obs_module_text("OffsetX"), -60.0, 60.0, 1.0);
	obs_properties_add_float_slider(shadow, S_SHADOW_Y, obs_module_text("OffsetY"), -60.0, 60.0, 1.0);
	obs_properties_add_group(p, "grp_shadow", obs_module_text("Shadow"), OBS_GROUP_NORMAL, shadow);

	/* -------- GLOW -------- */
	obs_properties_t *glow = obs_properties_create();
	obs_properties_add_bool(glow, S_GLOW_ENABLED, obs_module_text("Enabled"));
	obs_properties_add_color_alpha(glow, S_GLOW_COLOR, obs_module_text("Color"));
	obs_properties_add_float_slider(glow, S_GLOW_OPACITY, obs_module_text("Opacity"), 0.0, 1.0, 0.01);
	obs_properties_add_float_slider(glow, S_GLOW_RADIUS, obs_module_text("Radius"), 0.0, 80.0, 1.0);
	obs_properties_add_float_slider(glow, S_GLOW_INTENSITY, obs_module_text("Intensity"), 0.0, 3.0, 0.05);
	obs_properties_add_group(p, "grp_glow", obs_module_text("Glow"), OBS_GROUP_NORMAL, glow);

	/* -------- OVERLAY (logo) -------- */
	obs_properties_t *overlay = obs_properties_create();
	obs_properties_add_path(overlay, S_LOGO_PATH, obs_module_text("LogoImage"), OBS_PATH_FILE,
				 "Images (*.png *.jpg *.jpeg *.bmp)", NULL);
	obs_properties_add_float_slider(overlay, S_LOGO_SIZE, obs_module_text("LogoSize"), 8.0, 256.0, 1.0);
	obs_properties_add_float_slider(overlay, S_LOGO_OPACITY, obs_module_text("Opacity"), 0.0, 1.0, 0.01);
	obs_properties_add_float_slider(overlay, S_LOGO_POS_X, obs_module_text("PositionX"), -50.0, 2000.0, 1.0);
	obs_properties_add_float_slider(overlay, S_LOGO_POS_Y, obs_module_text("PositionY"), -50.0, 2000.0, 1.0);
	obs_properties_add_group(p, "grp_overlay", obs_module_text("Overlay"), OBS_GROUP_NORMAL, overlay);

	/* -------- MASK -------- */
	obs_properties_t *mask = obs_properties_create();
	obs_property_t *mtype = obs_properties_add_list(mask, S_MASK_TYPE, obs_module_text("MaskType"), OBS_COMBO_TYPE_LIST,
							 OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(mtype, obs_module_text("Rectangle"), 0);
	obs_property_list_add_int(mtype, obs_module_text("RoundedRectangle"), 1);
	obs_property_list_add_int(mtype, obs_module_text("Circle"), 2);
	obs_property_list_add_int(mtype, obs_module_text("Ellipse"), 3);
	obs_property_list_add_int(mtype, obs_module_text("CustomImage"), 4);
	obs_property_set_modified_callback(mtype, mask_type_changed_cb);
	obs_properties_add_float_slider(mask, S_MASK_RADIUS, obs_module_text("MaskRadius"), 0.0, 200.0, 1.0);
	obs_properties_add_float_slider(mask, S_MASK_FEATHER, obs_module_text("Feather"), 0.0, 60.0, 1.0);
	obs_properties_add_path(mask, S_MASK_IMAGE_PATH, obs_module_text("MaskImage"), OBS_PATH_FILE,
				 "Images (*.png)", NULL);
	obs_properties_add_group(p, "grp_mask", obs_module_text("Mask"), OBS_GROUP_NORMAL, mask);

	/* -------- BACKGROUND / CUSTOM STYLE -------- */
	obs_properties_t *bg = obs_properties_create();
	obs_properties_add_color_alpha(bg, S_BG_COLOR_TOP, obs_module_text("BgColorTop"));
	obs_properties_add_color_alpha(bg, S_BG_COLOR_BOTTOM, obs_module_text("BgColorBottom"));
	obs_properties_add_float_slider(bg, S_BG_OPACITY, obs_module_text("Opacity"), 0.0, 1.0, 0.01);
	obs_properties_add_path(bg, S_CUSTOM_BG_PATH, obs_module_text("CustomBackgroundImage"), OBS_PATH_FILE,
				 "Images (*.png *.jpg *.jpeg)", NULL);
	obs_properties_add_color_alpha(bg, S_ACCENT_COLOR, obs_module_text("AccentColor"));
	obs_property_t *dm = obs_properties_add_bool(bg, S_DARK_MODE, obs_module_text("DarkMode"));
	obs_property_set_modified_callback(dm, style_changed_cb);
	obs_properties_add_group(p, "grp_bg", obs_module_text("BackgroundCustomStyle"), OBS_GROUP_NORMAL, bg);

	/* -------- ANIMATION -------- */
	obs_properties_t *anim = obs_properties_create();
	obs_properties_add_bool(anim, S_ANIM_ENABLED, obs_module_text("Enabled"));
	obs_property_t *at = obs_properties_add_list(anim, S_ANIM_TYPE, obs_module_text("AnimType"), OBS_COMBO_TYPE_LIST,
						      OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(at, obs_module_text("Fade"), 1);
	obs_property_list_add_int(at, obs_module_text("Slide"), 2);
	obs_property_list_add_int(at, obs_module_text("Scale"), 3);
	obs_properties_add_int_slider(anim, S_ANIM_DURATION, obs_module_text("DurationMs"), 50, 2000, 10);
	obs_properties_add_group(p, "grp_anim", obs_module_text("Animation"), OBS_GROUP_NORMAL, anim);

	/* -------- PRESETS -------- */
	obs_properties_t *presets = obs_properties_create();
	obs_property_t *plist =
		obs_properties_add_list(presets, S_PRESET_LIST, obs_module_text("SavedPresets"), OBS_COMBO_TYPE_LIST,
					 OBS_COMBO_FORMAT_STRING);
	win_frame_presets_populate_list(plist);
	obs_property_set_modified_callback(plist, preset_list_changed_cb);
	obs_properties_add_text(presets, S_PRESET_NAME, obs_module_text("PresetName"), OBS_TEXT_DEFAULT);
	obs_properties_add_button2(presets, "preset_apply", obs_module_text("ApplySelected"), preset_apply_clicked, f);
	obs_properties_add_button2(presets, S_PRESET_SAVE_BTN, obs_module_text("SaveAsNewPreset"), preset_save_clicked, f);
	obs_properties_add_button2(presets, "preset_rename", obs_module_text("RenameSelected"), preset_rename_clicked, f);
	obs_properties_add_button2(presets, S_PRESET_IMPORT_BTN, obs_module_text("ImportPreset"), preset_import_clicked, f);
	obs_properties_add_button2(presets, S_PRESET_EXPORT_BTN, obs_module_text("ExportPreset"), preset_export_clicked, f);
	obs_properties_add_button2(presets, "preset_download", obs_module_text("DownloadPresets"), preset_download_clicked,
				    f);
	obs_properties_add_button2(presets, S_PRESET_DUPLICATE_BTN, obs_module_text("DuplicateSelected"),
				    preset_duplicate_clicked, f);
	obs_properties_add_button2(presets, S_PRESET_DELETE_BTN, obs_module_text("DeleteSelected"), preset_delete_clicked,
				    f);
	obs_properties_add_button2(presets, S_PRESET_RESET_BTN, obs_module_text("ResetToStyleDefaults"),
				    preset_reset_clicked, f);
	obs_properties_add_group(p, "grp_presets", obs_module_text("Presets"), OBS_GROUP_NORMAL, presets);

	return p;
}

/* ======================================================================
 * render
 * ==================================================================== */

static void wf_video_tick(void *data, float seconds)
{
	struct win_frame_filter *f = data;
	UNUSED_PARAMETER(seconds);

	/* Animation state is intentionally simple: alpha/scale ease toward 1
	 * once the source has been active for anim_duration; this keeps the
	 * "window opening" feel without a full timeline/easing system.
	 * Detailed direction-aware slide/scale timelines are left as a
	 * documented extension point (see README "Known simplifications"). */
	if (f->cached_settings) {
		obs_source_t *target = obs_filter_get_target(f->source);
		uint32_t bw = target ? obs_source_get_base_width(target) : 0;
		uint32_t bh = target ? obs_source_get_base_height(target) : 0;
		if (bw && bh && (bw != f->last_base_w || bh != f->last_base_h))
			win_frame_layout_update(f, f->cached_settings, bw, bh);
	}

	bool active = obs_source_showing(f->source);
	if (active && !f->anim_active_prev)
		f->anim_start_ns = os_gettime_ns();
	f->anim_active_prev = active;

	if (!f->cached_settings) {
		f->anim_alpha = 1.0f;
		f->anim_scale = 1.0f;
		return;
	}
	bool anim_on = obs_data_get_bool(f->cached_settings, S_ANIM_ENABLED);
	double duration_ms = obs_data_get_int(f->cached_settings, S_ANIM_DURATION);

	if (!anim_on || duration_ms <= 0) {
		f->anim_alpha = 1.0f;
		f->anim_scale = 1.0f;
		return;
	}

	double elapsed_ms = (double)(os_gettime_ns() - f->anim_start_ns) / 1000000.0;
	float t = (float)(elapsed_ms / duration_ms);
	if (t > 1.0f)
		t = 1.0f;
	if (t < 0.0f)
		t = 0.0f;
	float eased = 1.0f - (1.0f - t) * (1.0f - t); /* ease-out quad */
	f->anim_alpha = eased;
	f->anim_scale = 0.85f + 0.15f * eased;
}

static void wf_video_render(void *data, gs_effect_t *unused_effect)
{
	UNUSED_PARAMETER(unused_effect);
	struct win_frame_filter *f = data;

	bool enabled = f->cached_settings && obs_data_get_bool(f->cached_settings, S_ENABLED);

	if (!enabled || !f->effect) {
		obs_source_skip_video_filter(f->source);
		return;
	}

	/* re-run layout if the upstream source resolution changed (handles
	 * camera reconnects / resolution switches gracefully) */
	obs_source_t *target = obs_filter_get_target(f->source);
	uint32_t base_w = target ? obs_source_get_base_width(target) : 0;
	uint32_t base_h = target ? obs_source_get_base_height(target) : 0;
	if (base_w && base_h && (base_w != f->last_base_w || base_h != f->last_base_h))
		win_frame_layout_update(f, f->cached_settings, base_w, base_h);

	if (f->canvas_w == 0 || f->canvas_h == 0) {
		obs_source_skip_video_filter(f->source);
		return;
	}

	if (!obs_source_process_filter_begin(f->source, GS_RGBA, OBS_ALLOW_DIRECT_RENDERING))
		return;

	win_frame_set_effect_params(f, f->cached_settings);

	obs_source_process_filter_end(f->source, f->effect, f->canvas_w, f->canvas_h);

	win_frame_draw_logo_overlay(f, f->cached_settings);
}

static uint32_t wf_get_width(void *data)
{
	struct win_frame_filter *f = data;
	return f->canvas_w ? f->canvas_w : 1;
}

static uint32_t wf_get_height(void *data)
{
	struct win_frame_filter *f = data;
	return f->canvas_h ? f->canvas_h : 1;
}

struct obs_source_info win_frame_filter_info = {
	.id = "win_frame_filter",
	.type = OBS_SOURCE_TYPE_FILTER,
	.output_flags = OBS_SOURCE_VIDEO,
	.get_name = wf_get_name,
	.create = wf_create,
	.destroy = wf_destroy,
	.update = wf_update,
	.get_defaults = wf_get_defaults,
	.get_properties = wf_get_properties,
	.video_tick = wf_video_tick,
	.video_render = wf_video_render,
	.get_width = wf_get_width,
	.get_height = wf_get_height,
};
