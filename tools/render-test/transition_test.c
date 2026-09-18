/*
 * Renders the "Windows Alt+Tab" transition at chosen progress values for each
 * Windows version and writes a contact sheet (rows = versions, columns = times).
 *
 * usage: transition-test out.png plugin.dll plugin-data imgA.png imgB.png [zoom_div] [k=v ...]
 *   k=v pairs go to the transition's settings (e.g. at_dark=false at_count=6)
 *   versions=0,3,4  and  times=0.1,0.4,0.9  select which cells to draw
 */
#include <obs.h>
#include <graphics/graphics.h>
#include <graphics/vec4.h>
#include <util/base.h>
#include <util/platform.h>
#include "png_writer.h"

static void log_handler(int lvl, const char *msg, va_list args, void *p)
{
	(void)p;
	if (lvl > LOG_WARNING)
		return;
	vprintf(msg, args);
	printf("\n");
}

static void apply_kv(obs_data_t *d, int argc, char **argv)
{
	for (int i = 0; i < argc; i++) {
		char *eq = strchr(argv[i], '=');
		if (!eq || !strncmp(argv[i], "versions=", 9) || !strncmp(argv[i], "times=", 6))
			continue;
		char key[128];
		size_t kl = (size_t)(eq - argv[i]);
		if (kl >= sizeof(key))
			continue;
		memcpy(key, argv[i], kl);
		key[kl] = 0;
		const char *val = eq + 1;
		if (!strcmp(val, "true"))
			obs_data_set_bool(d, key, true);
		else if (!strcmp(val, "false"))
			obs_data_set_bool(d, key, false);
		else if (strchr(val, '.'))
			obs_data_set_double(d, key, atof(val));
		else
			obs_data_set_int(d, key, atoll(val));
	}
}

static int parse_list(const char *s, float *out, int max)
{
	int n = 0;
	while (*s && n < max) {
		out[n++] = (float)atof(s);
		const char *c = strchr(s, ',');
		if (!c)
			break;
		s = c + 1;
	}
	return n;
}

int main(int argc, char **argv)
{
	setvbuf(stdout, NULL, _IONBF, 0);
	if (argc < 6) {
		printf("usage: transition-test out.png plugin.dll data imgA imgB [zoom_div] [k=v...]\n");
		return 1;
	}
	int div = argc > 6 ? atoi(argv[6]) : 1;
	if (div < 1)
		div = 1;
	int kv0 = 7, kvn = argc > 7 ? argc - 7 : 0;

	float versions[8] = {0, 1, 2, 3, 4}, times[8] = {0.35f, 0.6f, 0.88f};
	int nv = 5, nt = 3;
	for (int i = 0; i < kvn; i++) {
		if (!strncmp(argv[kv0 + i], "versions=", 9))
			nv = parse_list(argv[kv0 + i] + 9, versions, 8);
		if (!strncmp(argv[kv0 + i], "times=", 6))
			nt = parse_list(argv[kv0 + i] + 6, times, 8);
	}

	base_set_log_handler(log_handler, NULL);
	os_mkdirs("E:/obs windows plugin/build/rt-config3");
	obs_startup("en-US", "E:/obs windows plugin/build/rt-config3", NULL);
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
	if (obs_open_module(&m1, argv[2], argv[3]) == MODULE_SUCCESS)
		obs_init_module(m1);
	else
		return 4;

	obs_data_t *sa = obs_data_create();
	obs_data_set_string(sa, "file", argv[4]);
	obs_source_t *a = obs_source_create_private("image_source", "Camera", sa);
	obs_data_release(sa);
	obs_data_t *sb = obs_data_create();
	obs_data_set_string(sb, "file", argv[5]);
	obs_source_t *b = obs_source_create_private("image_source", "Game Capture", sb);
	obs_data_release(sb);

	uint32_t cw = 0, ch = 0;
	uint8_t **cells = calloc((size_t)nv * nt, sizeof(uint8_t *));

	for (int vi = 0; vi < nv; vi++) {
		for (int ti = 0; ti < nt; ti++) {
			obs_data_t *ts = obs_data_create();
			apply_kv(ts, kvn, argv + kv0);
			obs_data_set_int(ts, "at_version", (int)versions[vi]);
			obs_source_t *tr = obs_source_create_private("win_alttab_transition", "tr", ts);
			obs_data_release(ts);
			if (!tr) {
				printf("transition create failed\n");
				return 5;
			}
			obs_transition_set(tr, a);
			obs_transition_start(tr, OBS_TRANSITION_MODE_MANUAL, 1600, b);
			obs_transition_set_manual_time(tr, times[ti]);
			os_sleep_ms(250); /* the manual time is applied on the next video tick */

			obs_enter_graphics();
			uint32_t W = obs_source_get_width(tr), H = obs_source_get_height(tr);
			if (!W || !H) {
				obs_leave_graphics();
				printf("v%d t=%.2f: zero size\n", vi, times[ti]);
				continue;
			}
			gs_texrender_t *trn = gs_texrender_create(GS_RGBA, GS_ZS_NONE);
			gs_stagesurf_t *ss = gs_stagesurface_create(W, H, GS_RGBA);
			for (int pass = 0; pass < 3; pass++) {
				gs_texrender_reset(trn);
				if (gs_texrender_begin(trn, W, H)) {
					struct vec4 bg;
					vec4_zero(&bg);
					gs_clear(GS_CLEAR_COLOR, &bg, 1.0f, 0);
					gs_ortho(0.0f, (float)W, 0.0f, (float)H, -100.0f, 100.0f);
					obs_source_video_render(tr);
					gs_texrender_end(trn);
				}
			}
			gs_stage_texture(ss, gs_texrender_get_texture(trn));
			uint8_t *data;
			uint32_t ls;
			if (gs_stagesurface_map(ss, &data, &ls)) {
				uint8_t *img = malloc((size_t)W * H * 4);
				for (uint32_t y = 0; y < H; y++)
					memcpy(img + (size_t)y * W * 4, data + (size_t)y * ls, (size_t)W * 4);
				cells[vi * nt + ti] = img;
				gs_stagesurface_unmap(ss);
			}
			gs_stagesurface_destroy(ss);
			gs_texrender_destroy(trn);
			obs_leave_graphics();
			cw = W;
			ch = H;
			obs_source_release(tr);
		}
	}

	uint32_t ow = cw / div, oh = ch / div;
	uint32_t AW = ow * nt, AH = oh * nv;
	uint8_t *atlas = calloc((size_t)AW * AH, 4);
	for (int vi = 0; vi < nv; vi++)
		for (int ti = 0; ti < nt; ti++) {
			uint8_t *img = cells[vi * nt + ti];
			if (!img)
				continue;
			for (uint32_t y = 0; y < oh; y++)
				for (uint32_t x = 0; x < ow; x++) {
					const uint8_t *s = img + ((size_t)(y * div) * cw + (x * div)) * 4;
					uint8_t *d = atlas + ((size_t)(vi * oh + y) * AW + (ti * ow + x)) * 4;
					d[0] = s[0];
					d[1] = s[1];
					d[2] = s[2];
					d[3] = 255;
				}
		}
	write_png(argv[1], atlas, AW, AH);
	printf("wrote %s (%ux%u), cell %ux%u\n", argv[1], AW, AH, cw, ch);
	return 0;
}
