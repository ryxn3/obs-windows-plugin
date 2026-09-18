/*
 * OBS Win Frame Filter
 * Adds customizable Windows-era window chrome (95 -> 11) around any video source.
 */

#include <obs-module.h>
#include <util/base.h>
#include "win-frame-presets.h"
#include "ui/menu-button.h"

#ifndef WF_PLUGIN_VERSION
#define WF_PLUGIN_VERSION "unknown"
#endif

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("win-frame-filter", "en-US")

extern struct obs_source_info win_frame_filter_info;

bool obs_module_load(void)
{
	obs_register_source(&win_frame_filter_info);
	win_frame_presets_seed_builtin();
	wf_ui_init();
	blog(LOG_INFO, "[win-frame-filter] plugin loaded (version %s)", WF_PLUGIN_VERSION);
	return true;
}

void obs_module_unload(void)
{
	wf_ui_shutdown();
	blog(LOG_INFO, "[win-frame-filter] plugin unloaded");
}

MODULE_EXPORT const char *obs_module_description(void)
{
	return "Adds customizable Windows-95-through-11 style window frames/chrome around a video source.";
}

MODULE_EXPORT const char *obs_module_name(void)
{
	return "Windows Camera Frame";
}
