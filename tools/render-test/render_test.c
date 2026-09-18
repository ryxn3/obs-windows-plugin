/*
 * Offscreen render harness: loads the real win-frame-filter module into libobs
 * (D3D11 backend), applies each built-in style to a test image, renders it to
 * a texture and writes a contact-sheet PNG. Lets you inspect the look without
 * opening OBS.
 *
 * usage: render-test <out.png> <plugin.dll> <plugin-data-dir> <test-image.png>
 *                    [zoom] [cols] [key=value ...]
 */
#include <obs.h>
#include <graphics/graphics.h>
#include <graphics/vec4.h>
#include <util/base.h>
#include <util/platform.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../src/win-frame-styles.h"
#include "../../src/win-frame-filter.h"

static void log_handler(int lvl, const char *msg, va_list args, void *p)
{
	(void)p;
	if (lvl > LOG_WARNING)
		return;
	vprintf(msg, args);
	printf("\n");
}

#include "png_writer.h"

static void apply_overrides(obs_data_t *d, int argc, char **argv)
{
	for (int i = 0; i < argc; i++) {
		char *eq = strchr(argv[i], '=');
		if (!eq)
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
		else if ((val[0] >= '0' && val[0] <= '9') || val[0] == '-')
			obs_data_set_int(d, key, atoll(val));
		else
			obs_data_set_string(d, key, val);
	}
}

int main(int argc, char **argv)
{
	if (argc < 5) {
		printf("usage: render-test out.png plugin.dll plugin-data test.png [zoom] [cols] [k=v...]\n");
		return 1;
	}
	const char *out = argv[1], *plugin = argv[2], *pdata = argv[3], *img = argv[4];
	int zoom = argc > 5 ? atoi(argv[5]) : 1;
	int cols = argc > 6 ? atoi(argv[6]) : 4;
	if (zoom < 1)
		zoom = 1;
	if (cols < 1)
		cols = 1;
	const int ov_start = 7;
	const int ov_n = argc > ov_start ? argc - ov_start : 0;

	setvbuf(stdout, NULL, _IONBF, 0);
	base_set_log_handler(log_handler, NULL);
	os_mkdirs("E:/obs windows plugin/build/rt-config");
	if (!obs_startup("en-US", "E:/obs windows plugin/build/rt-config", NULL)) {
		printf("obs_startup failed\n");
		return 2;
	}
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
	ovi.adapter = 0;
	ovi.gpu_conversion = true;
	ovi.scale_type = OBS_SCALE_BICUBIC;
	int rc = obs_reset_video(&ovi);
	if (rc != OBS_VIDEO_SUCCESS) {
		printf("obs_reset_video failed: %d\n", rc);
		return 3;
	}

	obs_module_t *m1 = NULL, *m2 = NULL;
	if (obs_open_module(&m2, "C:/Program Files/obs-studio/obs-plugins/64bit/image-source.dll",
			    "C:/Program Files/obs-studio/data/obs-plugins/image-source") == MODULE_SUCCESS)
		obs_init_module(m2);
	else
		printf("could not load image-source\n");
	if (obs_open_module(&m1, plugin, pdata) == MODULE_SUCCESS)
		obs_init_module(m1);
	else {
		printf("could not load plugin\n");
		return 4;
	}

	obs_data_t *is = obs_data_create();
	obs_data_set_string(is, "file", img);
	obs_source_t *src = obs_source_create_private("image_source", "cam", is);
	obs_data_release(is);
	if (!src) {
		printf("no image source\n");
		return 5;
	}

	const int n = STYLE_COUNT - 1; /* skip "custom" */
	uint32_t cw[64] = {0}, ch[64] = {0};
	uint8_t *imgs[64] = {0};
	uint32_t maxw = 0, maxh = 0;

	const char *only[16] = {0};
	int only_n = 0;
	for (int i = 0; i < ov_n; i++)
		if (!strncmp(argv[ov_start + i], "only=", 5) && only_n < 16)
			only[only_n++] = argv[ov_start + i] + 5;

	for (int i = 0; i < n; i++) {
		if (only_n) {
			bool keep = false;
			for (int k = 0; k < only_n; k++)
				if (!strcmp(only[k], win_frame_style_table[i].id))
					keep = true;
			if (!keep)
				continue;
		}
		obs_data_t *fs = obs_data_create();
		apply_overrides(fs, ov_n, argv + ov_start);
		win_frame_apply_style_defaults(fs, win_frame_style_table[i].id);
		apply_overrides(fs, ov_n, argv + ov_start);
		obs_data_set_string(fs, S_STYLE, win_frame_style_table[i].id);
		obs_source_t *flt = obs_source_create_private("win_frame_filter", win_frame_style_table[i].id, fs);
		obs_data_release(fs);
		if (!flt) {
			printf("filter create failed for %s\n", win_frame_style_table[i].id);
			continue;
		}

		obs_source_filter_add(src, flt);
		obs_data_t *st = obs_source_get_settings(flt);
		obs_source_update(flt, st);
		obs_data_release(st);

		obs_enter_graphics();
		uint32_t W = obs_source_get_width(src), H = obs_source_get_height(src);
		if (!W || !H) {
			obs_leave_graphics();
			printf("%s: zero size\n", win_frame_style_table[i].id);
			obs_source_filter_remove(src, flt);
			obs_source_release(flt);
			continue;
		}
		gs_texrender_t *tr = gs_texrender_create(GS_RGBA, GS_ZS_NONE);
		gs_stagesurf_t *ss = gs_stagesurface_create(W, H, GS_RGBA);
		for (int pass = 0; pass < 2; pass++) {
			gs_texrender_reset(tr);
			if (gs_texrender_begin(tr, W, H)) {
				struct vec4 bg;
				vec4_set(&bg, 0.06f, 0.20f, 0.38f, 1.0f);
				gs_clear(GS_CLEAR_COLOR, &bg, 1.0f, 0);
				gs_ortho(0.0f, (float)W, 0.0f, (float)H, -100.0f, 100.0f);
				gs_blend_state_push();
				gs_enable_blending(true);
				gs_blend_function_separate(GS_BLEND_SRCALPHA, GS_BLEND_INVSRCALPHA, GS_BLEND_ONE,
							   GS_BLEND_INVSRCALPHA);
				obs_source_video_render(src);
				gs_blend_state_pop();
				gs_texrender_end(tr);
			}
		}
		gs_stage_texture(ss, gs_texrender_get_texture(tr));
		uint8_t *data;
		uint32_t linesize;
		if (gs_stagesurface_map(ss, &data, &linesize)) {
			imgs[i] = malloc((size_t)W * H * 4);
			for (uint32_t y = 0; y < H; y++)
				memcpy(imgs[i] + (size_t)y * W * 4, data + (size_t)y * linesize, (size_t)W * 4);
			gs_stagesurface_unmap(ss);
		}
		gs_stagesurface_destroy(ss);
		gs_texrender_destroy(tr);
		obs_leave_graphics();

		cw[i] = W;
		ch[i] = H;
		if (W > maxw)
			maxw = W;
		if (H > maxh)
			maxh = H;
		printf("%-14s canvas %ux%u\n", win_frame_style_table[i].id, W, H);
		obs_source_filter_remove(src, flt);
		obs_source_release(flt);
	}

	int cnt = 0;
	for (int i = 0; i < n; i++)
		if (imgs[i]) {
			imgs[cnt] = imgs[i];
			cw[cnt] = cw[i];
			ch[cnt] = ch[i];
			if (cnt != i)
				imgs[i] = NULL;
			cnt++;
		}
	const int total = cnt;
	int rows = (total + cols - 1) / cols;
	uint32_t AW = (uint32_t)cols * maxw * zoom, AH = (uint32_t)rows * maxh * zoom;
	uint8_t *atlas = calloc((size_t)AW * AH, 4);
	for (int i = 0; i < total; i++) {
		if (!imgs[i])
			continue;
		int cx = i % cols, cy = i / cols;
		for (uint32_t y = 0; y < ch[i] * zoom; y++)
			for (uint32_t x = 0; x < cw[i] * zoom; x++) {
				const uint8_t *s = imgs[i] + ((size_t)(y / zoom) * cw[i] + (x / zoom)) * 4;
				uint8_t *d = atlas + ((size_t)(cy * maxh * zoom + y) * AW + (cx * maxw * zoom + x)) * 4;
				d[0] = s[0];
				d[1] = s[1];
				d[2] = s[2];
				d[3] = 255;
			}
	}
	write_png(out, atlas, AW, AH);
	printf("wrote %s (%ux%u)\n", out, AW, AH);
	fflush(stdout);
	return 0;
}
