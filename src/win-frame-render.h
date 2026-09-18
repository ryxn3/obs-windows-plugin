#pragma once

#include <obs-module.h>
#include <graphics/graphics.h>
#include "win-frame-text.h"

#ifdef __cplusplus
extern "C" {
#endif

struct win_frame_filter;

/* Looks up and caches every effect uniform's gs_eparam_t* once, right after
 * the effect is loaded (see win-frame-internal.h's win_frame_eparams). */
void win_frame_cache_eparams(struct win_frame_filter *f);

/* Recomputes canvas size + all cached assets (text textures, loaded images)
 * from the current settings. Called from filter_update() only -- NOT every
 * frame -- so per-frame cost stays limited to the shader itself. */
void win_frame_layout_update(struct win_frame_filter *f, obs_data_t *s, uint32_t base_w, uint32_t base_h);

/* Sets every effect uniform (except "image", which obs_source_process_filter_end
 * binds automatically) from the filter's cached layout + settings. */
void win_frame_set_effect_params(struct win_frame_filter *f, obs_data_t *s);

/* Draws the optional decorative logo image on top, after the main effect
 * pass has already been submitted to the current render target. */
void win_frame_draw_logo_overlay(struct win_frame_filter *f, obs_data_t *s);

void win_frame_release_cached_assets(struct win_frame_filter *f);

#ifdef __cplusplus
}
#endif
