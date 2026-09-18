#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Plugin-wide preferences, stored as JSON in the module config directory
 * (%APPDATA%\obs-studio\plugin_config\win-frame-filter\settings.json). */
struct wf_prefs {
	char default_style[64]; /* style id used for newly added filters        */
	char update_url[512];   /* URL of a small JSON file: {"version","url"}   */
	char download_url[512]; /* default URL in the "Download presets" dialog  */
	bool welcome_shown;     /* first-run dialog already displayed            */
};

void wf_prefs_load(void);
void wf_prefs_save(void);
struct wf_prefs *wf_prefs_get(void); /* never NULL; loads on first use */

#ifdef __cplusplus
}
#endif
