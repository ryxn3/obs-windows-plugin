#pragma once

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Qt dialogs. All must be called on OBS's UI thread. */
void wf_ui_show_about(void);
void wf_ui_show_welcome(void);
void wf_ui_show_presets_dialog(void);
void wf_ui_show_copy_dialog(void);
void wf_ui_show_settings_dialog(void);
void wf_ui_show_download_dialog(void);
void wf_ui_show_toast_guide(void);
void wf_ui_check_updates(void);

/* Small helpers usable from the C code (properties-panel buttons). Each
 * returns false when the user cancels. */
bool wf_ui_get_open_path(const char *title, const char *filter, char *out, size_t out_sz);
bool wf_ui_get_save_path(const char *title, const char *default_name, const char *filter, char *out, size_t out_sz);
bool wf_ui_get_text(const char *title, const char *label, const char *initial, char *out, size_t out_sz);
void wf_ui_message(const char *title, const char *text);

/* Brief status-bar message in OBS's main window. */
void wf_ui_status(const char *text);

#ifdef __cplusplus
}
#endif
