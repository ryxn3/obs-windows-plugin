#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* Registers the OBS-frontend hooks that add the "Camera Frame" menu-bar
 * button once OBS's main window exists. Safe to call when no frontend is
 * present (the harness / headless use): it then does nothing. */
void wf_ui_init(void);
void wf_ui_shutdown(void);

#ifdef __cplusplus
}
#endif
