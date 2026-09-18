/*
 * Headless test of the plugin's non-UI logic against real libobs:
 * add-filter, copy-style, framed-source enumeration, HTTPS fetch, preset
 * download error handling and update-check parsing.
 */
#include <obs.h>
#include <util/base.h>
#include <util/platform.h>
#include <stdio.h>
#include <string.h>
#include "../../src/wf-actions.h"
#include "../../src/wf-prefs.h"
#include "../../src/wf-http.h"
#include "../../src/win-frame-filter.h"
#include "../../src/win-frame-styles.h"

static obs_module_t *g_mod;
obs_module_t *obs_current_module(void)
{
	return g_mod;
}

static int g_fail = 0;
#define CHECK(cond, what)                                                            \
	do {                                                                         \
		if (cond)                                                            \
			printf("  ok    %s\n", what);                                \
		else {                                                               \
			printf("  FAIL  %s\n", what);                                \
			g_fail++;                                                    \
		}                                                                    \
	} while (0)

static void log_handler(int lvl, const char *msg, va_list args, void *p)
{
	(void)p;
	if (lvl > LOG_WARNING)
		return;
	vprintf(msg, args);
	printf("\n");
}

static int g_seen;
static char g_style_seen[64];
static void count_cb(obs_source_t *owner, obs_source_t *flt, void *p)
{
	(void)p;
	g_seen++;
	if (!strcmp(obs_source_get_name(owner), "camB")) {
		obs_data_t *s = obs_source_get_settings(flt);
		strncpy(g_style_seen, obs_data_get_string(s, S_STYLE), sizeof(g_style_seen) - 1);
		obs_data_release(s);
	}
}
static void set_style_cb(obs_source_t *owner, obs_source_t *flt, void *p)
{
	(void)p;
	if (!strcmp(obs_source_get_name(owner), "camA")) {
		obs_data_t *s = obs_source_get_settings(flt);
		obs_data_set_string(s, S_STYLE, "win98");
		obs_data_set_double(s, S_FRAME_THICKNESS, 7.0);
		obs_source_update(flt, s);
		obs_data_release(s);
	}
}

int main(int argc, char **argv)
{
	setvbuf(stdout, NULL, _IONBF, 0);
	if (argc < 3) {
		printf("usage: logic-test plugin.dll plugin-data-dir\n");
		return 1;
	}
	base_set_log_handler(log_handler, NULL);
	os_mkdirs("E:/obs windows plugin/build/rt-config2");
	obs_startup("en-US", "E:/obs windows plugin/build/rt-config2", NULL);
	obs_add_data_path("C:/Program Files/obs-studio/data/libobs/");
	struct obs_video_info ovi = {0};
	ovi.graphics_module = "libobs-d3d11";
	ovi.fps_num = 30;
	ovi.fps_den = 1;
	ovi.base_width = ovi.output_width = 1280;
	ovi.base_height = ovi.output_height = 720;
	ovi.output_format = VIDEO_FORMAT_NV12;
	ovi.colorspace = VIDEO_CS_709;
	ovi.range = VIDEO_RANGE_PARTIAL;
	ovi.gpu_conversion = true;
	ovi.scale_type = OBS_SCALE_BICUBIC;
	if (obs_reset_video(&ovi) != OBS_VIDEO_SUCCESS)
		return 3;

	obs_module_t *m1 = NULL, *m2 = NULL;
	if (obs_open_module(&m2, "C:/Program Files/obs-studio/obs-plugins/64bit/image-source.dll",
			    "C:/Program Files/obs-studio/data/obs-plugins/image-source") == MODULE_SUCCESS)
		obs_init_module(m2);
	if (obs_open_module(&m1, argv[1], argv[2]) == MODULE_SUCCESS)
		obs_init_module(m1);
	else
		return 4;
	g_mod = m1;

	obs_data_t *is = obs_data_create();
	obs_data_set_string(is, "file", "E:/obs windows plugin/build/test-cam.png");
	obs_source_t *a = obs_source_create("image_source", "camA", is, NULL);
	obs_source_t *b = obs_source_create("image_source", "camB", is, NULL);
	obs_data_release(is);

	char msg[300];
	printf("add / enumerate\n");
	CHECK(wf_add_filter_to_source(a, msg, sizeof(msg)), "adds a frame to camA");
	CHECK(!wf_add_filter_to_source(a, msg, sizeof(msg)), "refuses a second frame on camA");
	CHECK(strstr(msg, "already") != NULL, "explains why");
	g_seen = 0;
	wf_enum_framed(count_cb, NULL);
	CHECK(g_seen == 1, "enumeration sees exactly one framed source");

	printf("copy style\n");
	wf_enum_framed(set_style_cb, NULL);
	CHECK(wf_copy_style("camA", "camB", msg, sizeof(msg)), "copies A -> B (B had no frame)");
	g_seen = 0;
	g_style_seen[0] = 0;
	wf_enum_framed(count_cb, NULL);
	CHECK(g_seen == 2, "B now has a frame");
	CHECK(strcmp(g_style_seen, "win98") == 0, "B's frame carries A's style (win98)");
	CHECK(!wf_copy_style("camA", "camA", msg, sizeof(msg)), "refuses copying onto itself");
	CHECK(!wf_copy_style("camA", "nope", msg, sizeof(msg)), "refuses a missing destination");

	printf("https + downloads\n");
	char *body = NULL;
	size_t len = 0;
	char err[200] = {0};
	CHECK(!wf_http_get("http://example.com/", 1000, &body, &len, err, sizeof(err)), "rejects plain http");
	CHECK(wf_http_get("https://api.github.com/repos/obsproject/obs-studio", 1024 * 1024, &body, &len, err,
			  sizeof(err)) && body && len > 100,
	      "fetches JSON over HTTPS");
	if (!body)
		printf("        (%s)\n", err);
	else
		bfree(body);
	CHECK(!wf_http_get("https://api.github.com/repos/obsproject/obs-studio", 200, &body, &len, err, sizeof(err)),
	      "enforces the size limit");
	CHECK(!wf_http_get("https://api.github.com/this/does/not/exist/xyz", 1000, &body, &len, err, sizeof(err)),
	      "reports HTTP 404");
	printf("        (%s)\n", err);

	int r = wf_download_presets("https://api.github.com/repos/obsproject/obs-studio", msg, sizeof(msg));
	CHECK(r < 0, "a JSON file that is not a preset imports nothing");
	printf("        (%s)\n", msg);
	r = wf_download_presets("http://insecure.example/x.json", msg, sizeof(msg));
	CHECK(r < 0, "download refuses http://");
	r = wf_download_presets("", msg, sizeof(msg));
	CHECK(r < 0, "download refuses an empty URL");

	printf("update check\n");
	strcpy(wf_prefs_get()->update_url, "");
	CHECK(wf_check_update(msg, sizeof(msg), err, sizeof(err)) < 0, "unconfigured URL is reported");
	strcpy(wf_prefs_get()->update_url, "https://api.github.com/repos/obsproject/obs-studio");
	CHECK(wf_check_update(msg, sizeof(msg), err, sizeof(err)) < 0, "JSON without a version is rejected");
	printf("        (%s)\n", msg);

	printf("\n%s (%d failure%s)\n", g_fail ? "FAILED" : "ALL PASSED", g_fail, g_fail == 1 ? "" : "s");
	return g_fail ? 1 : 0;
}
