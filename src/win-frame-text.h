#pragma once

#include <graphics/graphics.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WF_TEXT_PAD 2 /* transparent margin (px) so shadow/glow taps aren't clipped */

/* Rasterizes `text` with a Windows GDI font into an RGBA gs_texture
 * (straight alpha). `color_abgr` is OBS's 0xAABBGGRR colour packing.
 * Enters the graphics context itself. Returns NULL on failure. */
gs_texture_t *win_frame_render_text_texture(const char *text, const char *font_name, int font_size_px,
					     bool bold, uint32_t color_abgr, uint32_t *out_w, uint32_t *out_h);

#ifdef __cplusplus
}
#endif
