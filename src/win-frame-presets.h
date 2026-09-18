#pragma once

#include <obs-module.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Presets are stored as plain obs_data JSON files under
 * <obs config dir>/plugin_config/win-frame-filter/presets/<name>.json
 * Every key matches win-frame-filter.h's S_* settings keys, so a preset
 * file can be applied directly on top of a filter's settings. */

/* Directory presets live in (created on first use). Caller does not own
 * the returned string (static buffer); copy if needed longer-term. */
const char *win_frame_presets_dir(void);

/* Populates `list` (an obs_properties list property) with every *.json file
 * found in the presets directory, sorted alphabetically. */
void win_frame_presets_populate_list(obs_property_t *list);

/* Saves `settings` (only the visual keys, not internal ones) as a preset
 * file named `name`. Returns false on I/O error. Overwrites silently. */
bool win_frame_presets_save(const char *name, obs_data_t *settings);

/* Loads preset `name` into a new obs_data_t (caller must obs_data_release).
 * Returns NULL if the file is missing or fails to parse -- callers must not
 * crash OBS on a corrupt/missing preset, just ignore it. */
obs_data_t *win_frame_presets_load(const char *name);

bool win_frame_presets_delete(const char *name);
bool win_frame_presets_duplicate(const char *src_name, const char *dst_name);
bool win_frame_presets_rename(const char *src_name, const char *dst_name);

/* Exports a preset to an arbitrary external file path (for sharing). */
bool win_frame_presets_export(const char *name, const char *dest_path);

/* Imports an arbitrary external JSON file as a new preset named `name`.
 * Validates that the file parses as JSON before accepting it. */
bool win_frame_presets_import(const char *src_path, const char *name);

/* Writes every built-in style (win-frame-styles.h table) out as a preset
 * file the first time the plugin runs, so the preset list is never empty. */
void win_frame_presets_seed_builtin(void);

/* Calls cb(name) for every preset file; returns the count. */
size_t win_frame_presets_enum(void (*cb)(const char *name, void *param), void *param);

#ifdef __cplusplus
}
#endif
