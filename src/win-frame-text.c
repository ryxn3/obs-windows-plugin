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

/* ---- word-wrapped block ------------------------------------------------- */

static int measure_w(HDC dc, const wchar_t *s, int len)
{
	SIZE sz = {0, 0};
	if (len <= 0)
		return 0;
	GetTextExtentPoint32W(dc, s, len, &sz);
	return sz.cx;
}

/* greedy word wrap; lines[i] = {start, len}; len < 0 marks "needs ellipsis" */
static int wrap_lines(HDC dc, const wchar_t *s, int max_w, int (*lines)[2], int max_lines)
{
	int n = 0, len = (int)wcslen(s), i = 0;
	while (i < len && n < max_lines) {
		while (i < len && s[i] == L' ')
			i++;
		int start = i, last_break = -1, j = i;
		while (j < len) {
			if (s[j] == L'\n')
				break;
			if (measure_w(dc, s + start, j - start + 1) > max_w && j > start)
				break;
			if (s[j] == L' ')
				last_break = j;
			j++;
		}
		int end = j;
		if (j < len && s[j] != L'\n' && last_break > start)
			end = last_break;
		int trimmed = end;
		while (trimmed > start && s[trimmed - 1] == L' ')
			trimmed--;
		lines[n][0] = start;
		lines[n][1] = trimmed - start;
		n++;
		i = end;
		if (i < len && s[i] == L'\n')
			i++;
	}
	if (i < len && n > 0)
		lines[n - 1][1] = -lines[n - 1][1] - 1;
	return n;
}

gs_texture_t *win_frame_render_text_wrapped(const char *header, int header_indent, const char *title,
					     const char *body, const char *font_name, int font_px, int max_w,
					     int max_body_lines, uint32_t header_abgr, uint32_t title_abgr,
					     uint32_t body_abgr, int reveal_chars, uint32_t *out_w, uint32_t *out_h)
{
	if (out_w)
		*out_w = 0;
	if (out_h)
		*out_h = 0;
	if (font_px < 6 || max_w < 20)
		return NULL;
	if (max_body_lines < 1)
		max_body_lines = 1;
	if (max_body_lines > 12)
		max_body_lines = 12;

	wchar_t wfont[64], whead[160] = {0}, wtitle[200] = {0}, wbody[800] = {0};
	if (!font_name || !*font_name || !MultiByteToWideChar(CP_UTF8, 0, font_name, -1, wfont, 64))
		wcscpy_s(wfont, 64, L"Segoe UI");
	if (header && *header)
		MultiByteToWideChar(CP_UTF8, 0, header, -1, whead, 160);
	if (title && *title)
		MultiByteToWideChar(CP_UTF8, 0, title, -1, wtitle, 200);
	if (body && *body)
		MultiByteToWideChar(CP_UTF8, 0, body, -1, wbody, 800);

	HDC dc = CreateCompatibleDC(NULL);
	if (!dc)
		return NULL;
	HFONT f_body = CreateFontW(-font_px, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_TT_PRECIS,
				    CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_DONTCARE, wfont);
	HFONT f_title = CreateFontW(-font_px, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_TT_PRECIS,
				     CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_DONTCARE, wfont);
	int head_px = font_px > 10 ? font_px - 1 : font_px;
	HFONT f_head = CreateFontW(-head_px, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_TT_PRECIS,
				    CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_DONTCARE, wfont);

	TEXTMETRICW tm;
	HFONT old = (HFONT)SelectObject(dc, f_body);
	GetTextMetricsW(dc, &tm);
	const int lh_body = tm.tmHeight;
	SelectObject(dc, f_title);
	GetTextMetricsW(dc, &tm);
	const int lh_title = tm.tmHeight;
	SelectObject(dc, f_head);
	GetTextMetricsW(dc, &tm);
	const int lh_head = tm.tmHeight;

	int head_w = 0, title_w = 0;
	if (whead[0]) {
		SelectObject(dc, f_head);
		while (wcslen(whead) > 4 && header_indent + measure_w(dc, whead, (int)wcslen(whead)) > max_w) {
			size_t l = wcslen(whead);
			whead[l - 4] = 0;
			wcscat_s(whead, 160, L"...");
		}
		head_w = header_indent + measure_w(dc, whead, (int)wcslen(whead));
	}
	if (wtitle[0]) {
		SelectObject(dc, f_title);
		while (wcslen(wtitle) > 4 && measure_w(dc, wtitle, (int)wcslen(wtitle)) > max_w) {
			size_t l = wcslen(wtitle);
			wtitle[l - 4] = 0;
			wcscat_s(wtitle, 200, L"...");
		}
		title_w = measure_w(dc, wtitle, (int)wcslen(wtitle));
	}

	int lines[12][2];
	int nl = 0, body_w = 0;
	if (wbody[0]) {
		SelectObject(dc, f_body);
		nl = wrap_lines(dc, wbody, max_w, lines, max_body_lines);
		for (int i = 0; i < nl; i++) {
			int len = lines[i][1] < 0 ? -lines[i][1] - 1 : lines[i][1];
			int w = measure_w(dc, wbody + lines[i][0], len);
			if (lines[i][1] < 0)
				w += measure_w(dc, L"...", 3);
			if (w > body_w)
				body_w = w;
		}
	}

	const int gap = 3;
	int content_w = head_w;
	if (title_w > content_w)
		content_w = title_w;
	if (body_w > content_w)
		content_w = body_w;
	if (content_w < 4)
		content_w = 4;
	if (content_w > max_w)
		content_w = max_w;
	int y_head = 0, y_title = 0, y_body = 0, y = 0;
	if (whead[0]) {
		y_head = y;
		y += lh_head + gap;
	}
	if (wtitle[0]) {
		y_title = y;
		y += lh_title + gap;
	}
	y_body = y;
	y += nl * lh_body;
	const int content_h = y > 0 ? y : lh_body;

	const int pad = WF_TEXT_PAD;
	const int w = content_w + pad * 2 + 1;
	const int h = content_h + pad * 2;

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

		if (whead[0]) {
			SelectObject(dc, f_head);
			TextOutW(dc, pad + header_indent, pad + y_head, whead, (int)wcslen(whead));
		}
		if (wtitle[0]) {
			SelectObject(dc, f_title);
			TextOutW(dc, pad, pad + y_title, wtitle, (int)wcslen(wtitle));
		}
		if (nl > 0) {
			SelectObject(dc, f_body);
			int left = reveal_chars < 0 ? (1 << 30) : reveal_chars;
			for (int i = 0; i < nl && left > 0; i++) {
				bool cut = lines[i][1] < 0;
				int len = cut ? -lines[i][1] - 1 : lines[i][1];
				int draw = len < left ? len : left;
				wchar_t buf[200];
				if (draw > 190)
					draw = 190;
				memcpy(buf, wbody + lines[i][0], (size_t)draw * sizeof(wchar_t));
				buf[draw] = 0;
				if (cut && draw == len)
					wcscat_s(buf, 200, L"...");
				TextOutW(dc, pad, pad + y_body + i * lh_body, buf, (int)wcslen(buf));
				left -= len + 1;
			}
		}
		GdiFlush();

		uint8_t *px = (uint8_t *)malloc((size_t)w * h * 4);
		const uint8_t *src = (const uint8_t *)bits;
		if (px) {
			const int y_head_end = pad + (whead[0] ? lh_head + gap : 0);
			const int y_title_end = y_head_end + (wtitle[0] ? lh_title + gap : 0);
			for (int yy = 0; yy < h; yy++) {
				uint32_t c = body_abgr;
				if (whead[0] && yy < y_head_end)
					c = header_abgr;
				else if (wtitle[0] && yy < y_title_end)
					c = title_abgr;
				uint8_t cr = (uint8_t)(c & 0xFF), cg = (uint8_t)((c >> 8) & 0xFF);
				uint8_t cb = (uint8_t)((c >> 16) & 0xFF), ca = (uint8_t)((c >> 24) & 0xFF);
				for (int xx = 0; xx < w; xx++) {
					int i = yy * w + xx;
					px[i * 4 + 0] = cr;
					px[i * 4 + 1] = cg;
					px[i * 4 + 2] = cb;
					px[i * 4 + 3] = (uint8_t)((src[i * 4 + 1] * ca) / 255);
				}
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
	SelectObject(dc, old);
	DeleteObject(f_body);
	DeleteObject(f_title);
	DeleteObject(f_head);
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


gs_texture_t *win_frame_render_text_wrapped(const char *header, int header_indent, const char *title,
					     const char *body, const char *font_name, int font_px, int max_w,
					     int max_body_lines, uint32_t header_abgr, uint32_t title_abgr,
					     uint32_t body_abgr, int reveal_chars, uint32_t *out_w, uint32_t *out_h)
{
	(void)header; (void)header_indent; (void)title; (void)body; (void)font_name; (void)font_px; (void)max_w;
	(void)max_body_lines; (void)header_abgr; (void)title_abgr; (void)body_abgr; (void)reveal_chars;
	if (out_w) *out_w = 0;
	if (out_h) *out_h = 0;
	return NULL;
}

#endif
