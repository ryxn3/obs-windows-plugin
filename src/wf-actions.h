#pragma once

#include <obs-module.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WF_FILTER_ID "win_frame_filter"

/* Applies `style_id` to every Windows Camera Frame filter on the sources of
 * the current scene (including nested groups). Returns how many were changed. */
size_t wf_scene_apply_style(const char *style_id);

/* Calls cb for every (source, filter) pair where filter is a Camera Frame. */
typedef void (*wf_framed_cb)(obs_source_t *owner, obs_source_t *filter, void *param);
size_t wf_enum_framed(wf_framed_cb cb, void *param);

/* Calls cb for every input source that produces video. */
typedef void (*wf_video_source_cb)(obs_source_t *src, void *param);
void wf_enum_video_sources(wf_video_source_cb cb, void *param);

/* Copies all filter settings from the frame on `src_name` to `dst_name`.
 * Adds a frame filter to the destination if it has none. */
bool wf_copy_style(const char *src_name, const char *dst_name, char *msg, size_t msg_sz);

/* Adds a Camera Frame filter (using the preferred default style) to `src`. */
bool wf_add_filter_to_source(obs_source_t *src, char *msg, size_t msg_sz);

/* Adds a frame to every selected item of the current scene. Returns count. */
size_t wf_add_filter_to_selected(char *msg, size_t msg_sz);

/* Downloads community presets from an https:// URL (a single preset JSON, or
 * an index {"presets":[{"name":..,"url":..}]}). Returns presets saved, or -1
 * on error (msg explains). Blocking: run on a worker thread. */
int wf_download_presets(const char *url, char *msg, size_t msg_sz);

/* Checks prefs.update_url. Returns 1 = newer version available, 0 = up to
 * date, -1 = error/not configured. Blocking: run on a worker thread. */
int wf_check_update(char *msg, size_t msg_sz, char *url_out, size_t url_sz);

#ifdef __cplusplus
}
#endif
