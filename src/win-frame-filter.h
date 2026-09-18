#pragma once

#include <obs-module.h>
#include <graphics/graphics.h>
#include <util/darray.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------------
 * Settings key names. These are also the keys used inside preset JSON
 * files (data/presets/*.json), so keep this list and the presets in sync.
 * ---------------------------------------------------------------------- */

/* General */
#define S_STYLE               "style"                 /* string enum, see win_frame_style.h  */
#define S_ENABLED             "frame_enabled"
#define S_SCALE               "overall_scale"          /* float 0.1 - 4.0 */
#define S_CANVAS_W            "canvas_width"
#define S_CANVAS_H            "canvas_height"
#define S_PADDING             "padding"
#define S_ROTATION            "rotation_deg"
#define S_ALIGN_H             "align_h"                /* 0 left 1 center 2 right */
#define S_ALIGN_V             "align_v"                /* 0 top  1 center 2 bottom */

/* Camera */
#define S_CAM_CROP_L          "cam_crop_left"
#define S_CAM_CROP_R          "cam_crop_right"
#define S_CAM_CROP_T          "cam_crop_top"
#define S_CAM_CROP_B          "cam_crop_bottom"
#define S_CAM_ZOOM            "cam_zoom"
#define S_CAM_POS_X           "cam_pos_x"
#define S_CAM_POS_Y           "cam_pos_y"

/* Frame geometry */
#define S_FRAME_THICKNESS     "frame_thickness"
#define S_CORNER_RADIUS       "corner_radius"

/* Border */
#define S_BORDER_ENABLED      "border_enabled"
#define S_BORDER_THICKNESS    "border_thickness"
#define S_BORDER_COLOR        "border_color"
#define S_BORDER_OPACITY      "border_opacity"
#define S_BORDER_DOUBLE       "border_double"
#define S_BORDER_INNER_COLOR  "border_inner_color"
#define S_BORDER_DASHED       "border_dashed"

/* Window background / glass */
#define S_BG_COLOR_TOP        "bg_color_top"
#define S_BG_COLOR_BOTTOM     "bg_color_bottom"
#define S_BG_OPACITY          "bg_opacity"
#define S_GLASS_BLUR          "glass_blur"
#define S_GLASS_REFLECTION    "glass_reflection"

/* Title bar */
#define S_TITLEBAR_ENABLED    "titlebar_enabled"
#define S_TITLEBAR_HEIGHT     "titlebar_height"
#define S_TITLE_TEXT          "title_text"
#define S_TITLE_FONT          "title_font"
#define S_TITLE_COLOR         "title_color"
#define S_TITLE_ALIGN         "title_align" /* 0 left 1 center */
#define S_TITLEBAR_COLOR_A    "titlebar_color_a"
#define S_TITLEBAR_COLOR_B    "titlebar_color_b"
#define S_TITLEBAR_GRADIENT   "titlebar_gradient"
#define S_TITLEBAR_ICON_PATH  "titlebar_icon_path"
#define S_TITLEBAR_ICON_SIZE  "titlebar_icon_size"
#define S_STATUSBAR_ENABLED   "statusbar_enabled"
#define S_STATUSBAR_TEXT      "statusbar_text"
#define S_RESIZEGRIP_ENABLED  "resizegrip_enabled"

/* Window buttons */
#define S_BTN_MIN_ENABLED     "btn_min_enabled"
#define S_BTN_MAX_ENABLED     "btn_max_enabled"
#define S_BTN_CLOSE_ENABLED   "btn_close_enabled"
#define S_BTN_SIZE            "btn_size"
#define S_BTN_SPACING         "btn_spacing"
#define S_BTN_COLOR           "btn_color"
#define S_BTN_CLOSE_COLOR     "btn_close_color"
#define S_BTN_SYMBOL_COLOR    "btn_symbol_color"

/* Shadow */
#define S_SHADOW_ENABLED      "shadow_enabled"
#define S_SHADOW_COLOR        "shadow_color"
#define S_SHADOW_OPACITY      "shadow_opacity"
#define S_SHADOW_BLUR         "shadow_blur"
#define S_SHADOW_SPREAD       "shadow_spread"
#define S_SHADOW_X            "shadow_x"
#define S_SHADOW_Y            "shadow_y"

/* Glow */
#define S_GLOW_ENABLED        "glow_enabled"
#define S_GLOW_COLOR          "glow_color"
#define S_GLOW_OPACITY        "glow_opacity"
#define S_GLOW_RADIUS         "glow_radius"
#define S_GLOW_INTENSITY      "glow_intensity"

/* Mask */
#define S_MASK_TYPE           "mask_type" /* 0 rect 1 rounded 2 circle 3 ellipse 4 custom png */
#define S_MASK_FEATHER        "mask_feather"
#define S_MASK_IMAGE_PATH     "mask_image_path"

/* Custom images */
#define S_CUSTOM_FRAME_PATH   "custom_frame_path"
#define S_CUSTOM_BG_PATH      "custom_bg_path"
#define S_LOGO_PATH           "logo_path"
#define S_LOGO_SIZE           "logo_size"
#define S_LOGO_OPACITY        "logo_opacity"
#define S_LOGO_POS_X          "logo_pos_x"
#define S_LOGO_POS_Y          "logo_pos_y"

/* Accent (win8/10/11) */
#define S_ACCENT_COLOR        "accent_color"
#define S_DARK_MODE           "dark_mode"

/* Animation */
#define S_ANIM_ENABLED        "anim_enabled"
#define S_ANIM_TYPE           "anim_type" /* 0 none 1 fade 2 slide 3 scale */
#define S_ANIM_DURATION       "anim_duration_ms"
#define S_ANIM_DIRECTION      "anim_direction" /* 0..3 */


/* --- v1.1 additions: authenticity controls --- */
#define S_UI_SCALE              "ui_scale"
#define S_TITLEBAR_INSET        "titlebar_inset"
#define S_TITLEBAR_STYLE        "titlebar_style"        /* 0 flat/gradient, 1 Luna multi-stop */
#define S_TITLEBAR_GRAD_HORIZ   "titlebar_grad_horiz"
#define S_FRAME_STYLE           "frame_style"           /* 0 flat, 1 classic 3D bevel */
#define S_FRAME_COLOR           "frame_color"
#define S_CORNER_TOP_ONLY       "corner_top_only"
#define S_CAM_BEVEL             "cam_bevel"             /* sunken classic client edge */
#define S_TITLE_FONT_SIZE       "title_font_size"
#define S_TITLE_BOLD            "title_bold"
#define S_TITLE_SHADOW          "title_shadow"          /* 0 none, 1 drop shadow, 2 glow */
#define S_TITLEBAR_ICON_BUILTIN "titlebar_icon_builtin"
#define S_BTN_STYLE             "btn_style"             /* 0 classic 1 Luna 2 Aero 3 flat */
#define S_BTN_WIDTH             "btn_width"
#define S_BTN_SYMBOL_CLOSE_COLOR "btn_symbol_close_color"
#define S_TITLEBAR_ICON_MODE    "titlebar_icon_mode"    /* 0 none 1 camera 2 back 3 sysmenu 4 hamburger 5 amiga 6 GEM */
#define S_BTN_MARGIN            "btn_margin"
#define S_BTN_HOVER             "btn_hover"             /* 0 none 1 min 2 max 3 close */
#define S_SNAP_FLYOUT           "snap_flyout"
#define S_TITLE_LOWER           "title_lowercase"
#define S_TILES                 "tiles"
#define S_CAM_PIXELATE          "cam_pixelate"
#define S_CAM_SCANLINES         "cam_scanlines"
#define S_CAM_CRT               "cam_crt"
#define S_MASK_RADIUS           "mask_radius"

/* Presets */
#define S_PRESET_LIST         "preset_list"
#define S_PRESET_NAME         "preset_name"
#define S_PRESET_SAVE_BTN     "preset_save_btn"
#define S_PRESET_DELETE_BTN   "preset_delete_btn"
#define S_PRESET_DUPLICATE_BTN "preset_duplicate_btn"
#define S_PRESET_EXPORT_BTN   "preset_export_btn"
#define S_PRESET_IMPORT_BTN   "preset_import_btn"
#define S_PRESET_RESET_BTN    "preset_reset_btn"

#ifdef __cplusplus
}
#endif
