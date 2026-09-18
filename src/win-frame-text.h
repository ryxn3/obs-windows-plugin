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

/* Renders up to 8 lines of text into ONE texture (an "atlas"): line k occupies
 * rows [k*line_h, (k+1)*line_h). out_widths[k] receives the visible pixel width
 * of line k (excluding the WF_TEXT_PAD margins). Enters the graphics context. */
gs_texture_t *win_frame_render_text_atlas(const char *const *lines, int count, const char *font_name,
					   int font_size_px, bool bold, uint32_t color_abgr, uint32_t *out_w,
					   uint32_t *out_line_h, float *out_widths);

/* Word-wrapped text block: optional small header line, optional bold title,
 * and a wrapped body of at most max_body_lines lines (extra text is cut with
 * an ellipsis). The texture size is computed from the FULL text, so revealing
 * it progressively (reveal_chars = number of body characters drawn, -1 = all)
 * never changes the size or the line breaks - ideal for a typewriter effect.
 * header_indent leaves room for an icon before the header text.
 * Enters the graphics context. Returns NULL on failure. */
gs_texture_t *win_frame_render_text_wrapped(const char *header, int header_indent, const char *title,
					     const char *body, const char *font_name, int font_px, int max_w,
					     int max_body_lines, uint32_t header_abgr, uint32_t title_abgr,
					     uint32_t body_abgr, int reveal_chars, uint32_t *out_w, uint32_t *out_h);

/* Same as above, but the body may contain U+E000 + k characters that stand for
 * images[k] (straight-alpha RGBA, or rgba == NULL while still loading, which
 * reserves a square slot). Images are scaled to about 1.55x the font height. */
struct wf_inline_img {
	uint32_t w, h;
	const uint8_t *rgba;
};
gs_texture_t *win_frame_render_text_wrapped_img(const char *header, int header_indent, const char *title,
						 const char *body, const char *font_name, int font_px, int max_w,
						 int max_body_lines, uint32_t header_abgr, uint32_t title_abgr,
						 uint32_t body_abgr, int reveal_chars, const struct wf_inline_img *images,
						 int nimages, uint32_t *out_w, uint32_t *out_h);

#ifdef __cplusplus
}
#endif
