#include "win-frame-text.h"
#include <obs-module.h>

#if defined(_WIN32)

#include <windows.h>
#include <string.h>
#include <stdlib.h>

gs_texture_t *win_frame_render_text_texture(const char *text, const char *font_name, int font_size_px,
					     bool bold, uint32_t color_abgr, uint32_t *out_w, uint32_t *out_h)
{
	if (out_w)
		*out_w = 0;
	if (out_h)
		*out_h = 0;
	if (!text || !*text || font_size_px < 4)
		return NULL;

	wchar_t wtext[512];
	if (!MultiByteToWideChar(CP_UTF8, 0, text, -1, wtext, 512))
		return NULL;

	wchar_t wfont[64];
	if (!font_name || !*font_name || !MultiByteToWideChar(CP_UTF8, 0, font_name, -1, wfont, 64))
		wcscpy_s(wfont, 64, L"Segoe UI");

	HDC dc = CreateCompatibleDC(NULL);
	if (!dc)
		return NULL;

	HFONT font = CreateFontW(-font_size_px, 0, 0, 0, bold ? FW_BOLD : FW_NORMAL, FALSE, FALSE, FALSE,
				  DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
				  DEFAULT_PITCH | FF_DONTCARE, wfont);
	HFONT old_font = (HFONT)SelectObject(dc, font);

	RECT measure = {0, 0, 0, 0};
	DrawTextW(dc, wtext, -1, &measure, DT_CALCRECT | DT_SINGLELINE | DT_NOPREFIX);
	int tw = measure.right - measure.left;
	int th = measure.bottom - measure.top;

	gs_texture_t *tex = NULL;
	if (tw > 0 && th > 0) {
		const int pad = WF_TEXT_PAD;
		int w = tw + pad * 2 + 1;
		int h = th + pad * 2;

		BITMAPINFO bmi;
		memset(&bmi, 0, sizeof(bmi));
		bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
		bmi.bmiHeader.biWidth = w;
		bmi.bmiHeader.biHeight = -h; /* top-down */
		bmi.bmiHeader.biPlanes = 1;
		bmi.bmiHeader.biBitCount = 32;
		bmi.bmiHeader.biCompression = BI_RGB;

		void *bits = NULL;
		HBITMAP bmp = CreateDIBSection(dc, &bmi, DIB_RGB_COLORS, &bits, NULL, 0);
		if (bmp && bits) {
			HBITMAP old_bmp = (HBITMAP)SelectObject(dc, bmp);
			memset(bits, 0, (size_t)w * h * 4); /* black */
			SetBkMode(dc, TRANSPARENT);
			SetTextColor(dc, RGB(255, 255, 255));
			RECT r = {pad, pad, w, h};
			DrawTextW(dc, wtext, -1, &r, DT_LEFT | DT_TOP | DT_NOPREFIX | DT_SINGLELINE);
			GdiFlush();

			uint8_t cr = (uint8_t)(color_abgr & 0xFF);
			uint8_t cg = (uint8_t)((color_abgr >> 8) & 0xFF);
			uint8_t cb = (uint8_t)((color_abgr >> 16) & 0xFF);
			uint8_t ca = (uint8_t)((color_abgr >> 24) & 0xFF);

			uint8_t *px = (uint8_t *)malloc((size_t)w * h * 4);
			const uint8_t *src = (const uint8_t *)bits;
			if (px) {
				for (int i = 0; i < w * h; i++) {
					/* white text on black: green channel == coverage */
					int cov = src[i * 4 + 1];
					px[i * 4 + 0] = cr;
					px[i * 4 + 1] = cg;
					px[i * 4 + 2] = cb;
					px[i * 4 + 3] = (uint8_t)((cov * ca) / 255);
				}
				obs_enter_graphics();
				const uint8_t *planes[1] = {px};
				tex = gs_texture_create((uint32_t)w, (uint32_t)h, GS_RGBA, 1, planes, 0);
				obs_leave_graphics();
				free(px);
				if (tex) {
					if (out_w)
						*out_w = (uint32_t)w;
					if (out_h)
						*out_h = (uint32_t)h;
				}
			}
			SelectObject(dc, old_bmp);
			DeleteObject(bmp);
		}
	}

	SelectObject(dc, old_font);
	DeleteObject(font);
	DeleteDC(dc);
	return tex;
}


gs_texture_t *win_frame_render_text_atlas(const char *const *lines, int count, const char *font_name,
					   int font_size_px, bool bold, uint32_t color_abgr, uint32_t *out_w,
					   uint32_t *out_line_h, float *out_widths)
{
	if (out_w)
		*out_w = 0;
	if (out_line_h)
		*out_line_h = 0;
	if (count < 1 || count > 8 || font_size_px < 4)
		return NULL;

	wchar_t wfont[64];
	if (!font_name || !*font_name || !MultiByteToWideChar(CP_UTF8, 0, font_name, -1, wfont, 64))
		wcscpy_s(wfont, 64, L"Segoe UI");

	HDC dc = CreateCompatibleDC(NULL);
	if (!dc)
		return NULL;
	HFONT font = CreateFontW(-font_size_px, 0, 0, 0, bold ? FW_BOLD : FW_NORMAL, FALSE, FALSE, FALSE,
				  DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
				  DEFAULT_PITCH | FF_DONTCARE, wfont);
	HFONT old_font = (HFONT)SelectObject(dc, font);

	wchar_t wl[8][256];
	int widths[8] = {0};
	int max_w = 8, max_h = 8;
	for (int i = 0; i < count; i++) {
		const char *t = (lines && lines[i]) ? lines[i] : "";
		wl[i][0] = 0;
		MultiByteToWideChar(CP_UTF8, 0, t, -1, wl[i], 256);
		RECT r = {0, 0, 0, 0};
		DrawTextW(dc, wl[i], -1, &r, DT_CALCRECT | DT_SINGLELINE | DT_NOPREFIX);
		widths[i] = r.right - r.left;
		if (widths[i] > 480)
			widths[i] = 480;
		if (widths[i] > max_w)
			max_w = widths[i];
		if (r.bottom - r.top > max_h)
			max_h = r.bottom - r.top;
	}
	const int pad = WF_TEXT_PAD;
	const int w = max_w + pad * 2 + 1;
	const int lh = max_h + pad * 2;
	const int h = lh * count;

	gs_texture_t *tex = NULL;
	BITMAPINFO bmi;
	memset(&bmi, 0, sizeof(bmi));
	bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	bmi.bmiHeader.biWidth = w;
	bmi.bmiHeader.biHeight = -h;
	bmi.bmiHeader.biPlanes = 1;
	bmi.bmiHeader.biBitCount = 32;
	bmi.bmiHeader.biCompression = BI_RGB;
	void *bits = NULL;
	HBITMAP bmp = CreateDIBSection(dc, &bmi, DIB_RGB_COLORS, &bits, NULL, 0);
	if (bmp && bits) {
		HBITMAP old_bmp = (HBITMAP)SelectObject(dc, bmp);
		memset(bits, 0, (size_t)w * h * 4);
		SetBkMode(dc, TRANSPARENT);
		SetTextColor(dc, RGB(255, 255, 255));
		for (int i = 0; i < count; i++) {
			RECT r = {pad, i * lh + pad, w, (i + 1) * lh};
			DrawTextW(dc, wl[i], -1, &r, DT_LEFT | DT_TOP | DT_NOPREFIX | DT_SINGLELINE);
			if (out_widths)
				out_widths[i] = (float)widths[i];
		}
		GdiFlush();

		uint8_t cr = (uint8_t)(color_abgr & 0xFF), cg = (uint8_t)((color_abgr >> 8) & 0xFF);
		uint8_t cb = (uint8_t)((color_abgr >> 16) & 0xFF), ca = (uint8_t)((color_abgr >> 24) & 0xFF);
		uint8_t *px = (uint8_t *)malloc((size_t)w * h * 4);
		const uint8_t *src = (const uint8_t *)bits;
		if (px) {
			for (int i = 0; i < w * h; i++) {
				px[i * 4 + 0] = cr;
				px[i * 4 + 1] = cg;
				px[i * 4 + 2] = cb;
				px[i * 4 + 3] = (uint8_t)((src[i * 4 + 1] * ca) / 255);
			}
			obs_enter_graphics();
			const uint8_t *planes[1] = {px};
			tex = gs_texture_create((uint32_t)w, (uint32_t)h, GS_RGBA, 1, planes, 0);
			obs_leave_graphics();
			free(px);
			if (tex) {
				if (out_w)
					*out_w = (uint32_t)w;
				if (out_line_h)
					*out_line_h = (uint32_t)lh;
			}
		}
		SelectObject(dc, old_bmp);
		DeleteObject(bmp);
	}
	SelectObject(dc, old_font);
	DeleteObject(font);
	DeleteDC(dc);
	return tex;
}

#else

gs_texture_t *win_frame_render_text_texture(const char *text, const char *font_name, int font_size_px,
					     bool bold, uint32_t color_abgr, uint32_t *out_w, uint32_t *out_h)
{
	(void)text;
	(void)font_name;
	(void)font_size_px;
	(void)bold;
	(void)color_abgr;
	if (out_w)
		*out_w = 0;
	if (out_h)
		*out_h = 0;
	return NULL;
}

gs_texture_t *win_frame_render_text_atlas(const char *const *lines, int count, const char *font_name,
					   int font_size_px, bool bold, uint32_t color_abgr, uint32_t *out_w,
					   uint32_t *out_line_h, float *out_widths)
{
	(void)lines; (void)count; (void)font_name; (void)font_size_px; (void)bold; (void)color_abgr; (void)out_widths;
	if (out_w) *out_w = 0;
	if (out_line_h) *out_line_h = 0;
	return NULL;
}

#endif
