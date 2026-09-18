#pragma once

#include <obs-module.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Number of entries in win_frame_style_table. "custom" is always the last one. */
#define STYLE_COUNT 37
#define STYLE_CUSTOM (STYLE_COUNT - 1)

struct win_frame_style_entry {
	const char *id;      /* stable string id, used in settings + preset files */
	const char *display; /* name shown in the UI                              */
};

extern const struct win_frame_style_entry win_frame_style_table[STYLE_COUNT];

/* Applies the built-in look for `style_id` onto `settings`. Overwrites every
 * style-owned key so switching styles gives an immediate, complete result. */
void win_frame_apply_style_defaults(obs_data_t *settings, const char *style_id);

#ifdef __cplusplus
}
#endif
