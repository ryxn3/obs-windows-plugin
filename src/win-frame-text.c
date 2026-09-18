#include "win-frame-text.h"
#include <obs-module.h>

#if defined(_WIN32)

#include <windows.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

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

/* Inline images (emotes): the body may contain private-use characters
 * U+E000 + k, each standing for images[k]. They are as wide as the image
 * scaled to g_img_h and are drawn after the text. */
static const struct wf_inline_img *g_img;
static int g_nimg, g_img_h;

static int img_index(wchar_t c)
{
	return (g_nimg > 0 && c >= 0xE000 && c < 0xE000 + g_nimg) ? (int)(c - 0xE000) : -1;
}

static int img_width(int k)
{
	if (!g_img[k].rgba || !g_img[k].w || !g_img[k].h)
		return g_img_h;
	int w = (int)((long long)g_img[k].w * g_img_h / g_img[k].h);
	if (w < 4)
		w = 4;
	if (w > g_img_h * 4)
		w = g_img_h * 4;
	return w;
}

static int measure_w(HDC dc, const wchar_t *s, int len)
{
	SIZE sz = {0, 0};
	if (len <= 0)
		return 0;
	if (g_nimg == 0) {
		GetTextExtentPoint32W(dc, s, len, &sz);
		return sz.cx;
	}
	int total = 0, run = 0;
	for (int i = 0; i <= len; i++) {
		int k = i < len ? img_index(s[i]) : -1;
		if (i == len || k >= 0) {
			if (i > run) {
				GetTextExtentPoint32W(dc, s + run, i - run, &sz);
				total += sz.cx;
			}
			if (k >= 0)
				total += img_width(k);
			run = i + 1;
		}
	}
	return total;
}

struct img_place {
	int k, x, y;
};
static struct img_place g_places[96];
static int g_nplaces;

/* draws one line at (x,y) (top of a lh-high row), recording image positions */
static void draw_line(HDC dc, int x, int y, int lh, int text_h, const wchar_t *s, int len)
{
	int run = 0;
	const int ty = y + (lh - text_h) / 2;
	for (int i = 0; i <= len; i++) {
		int k = i < len ? img_index(s[i]) : -1;
		if (i == len || k >= 0) {
			if (i > run) {
				SIZE sz;
				TextOutW(dc, x, ty, s + run, i - run);
				GetTextExtentPoint32W(dc, s + run, i - run, &sz);
				x += sz.cx;
			}
			if (k >= 0) {
				int w = img_width(k);
				if (g_nplaces < 96) {
					g_places[g_nplaces].k = k;
					g_places[g_nplaces].x = x;
					g_places[g_nplaces].y = y + (lh - g_img_h) / 2;
					g_nplaces++;
				}
				x += w;
			}
			run = i + 1;
		}
	}
}

/* bilinear-scaled "over" blend of a straight-alpha RGBA image into px */
static void blit_img(uint8_t *px, int W, int H, int dx, int dy, int k)
{
	const struct wf_inline_img *im = &g_img[k];
	if (!im->rgba || !im->w || !im->h)
		return;
	int dw = img_width(k), dh = g_img_h;
	for (int y = 0; y < dh; y++) {
		int oy = dy + y;
		if (oy < 0 || oy >= H)
			continue;
		float fy = ((float)y + 0.5f) * (float)im->h / (float)dh - 0.5f;
		int y0 = (int)floorf(fy);
		float ty = fy - (float)y0;
		int y1 = y0 + 1;
		y0 = y0 < 0 ? 0 : (y0 >= (int)im->h ? (int)im->h - 1 : y0);
		y1 = y1 < 0 ? 0 : (y1 >= (int)im->h ? (int)im->h - 1 : y1);
		for (int x = 0; x < dw; x++) {
			int ox = dx + x;
			if (ox < 0 || ox >= W)
				continue;
			float fx = ((float)x + 0.5f) * (float)im->w / (float)dw - 0.5f;
			int x0 = (int)floorf(fx);
			float tx = fx - (float)x0;
			int x1 = x0 + 1;
			x0 = x0 < 0 ? 0 : (x0 >= (int)im->w ? (int)im->w - 1 : x0);
			x1 = x1 < 0 ? 0 : (x1 >= (int)im->w ? (int)im->w - 1 : x1);
			const uint8_t *p00 = im->rgba + ((size_t)y0 * im->w + x0) * 4;
			const uint8_t *p10 = im->rgba + ((size_t)y0 * im->w + x1) * 4;
			const uint8_t *p01 = im->rgba + ((size_t)y1 * im->w + x0) * 4;
			const uint8_t *p11 = im->rgba + ((size_t)y1 * im->w + x1) * 4;
			float c[4];
			/* interpolate premultiplied so transparent edges do not bleed dark */
			float a = 0, r = 0, g = 0, b = 0;
			const uint8_t *pp[4] = {p00, p10, p01, p11};
			float wt[4] = {(1 - tx) * (1 - ty), tx * (1 - ty), (1 - tx) * ty, tx * ty};
			for (int q = 0; q < 4; q++) {
				float pa = pp[q][3] / 255.0f * wt[q];
				a += pa;
				r += pp[q][0] * pa;
				g += pp[q][1] * pa;
				b += pp[q][2] * pa;
			}
			if (a <= 0.001f)
				continue;
			c[0] = r / a;
			c[1] = g / a;
			c[2] = b / a;
			c[3] = a;
			uint8_t *d = px + ((size_t)oy * W + ox) * 4;
			float da = d[3] / 255.0f;
			float oa = c[3] + da * (1.0f - c[3]);
			for (int q = 0; q < 3; q++)
				d[q] = (uint8_t)((c[q] * c[3] + d[q] * da * (1.0f - c[3])) / oa + 0.5f);
			d[3] = (uint8_t)(oa * 255.0f + 0.5f);
		}
	}
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

gs_texture_t *win_frame_render_text_wrapped_img(const char *header, int header_indent, const char *title,
						 const char *body, const char *font_name, int font_px, int max_w,
						 int max_body_lines, uint32_t header_abgr, uint32_t title_abgr,
						 uint32_t body_abgr, int reveal_chars, const struct wf_inline_img *images,
						 int nimages, uint32_t *out_w, uint32_t *out_h)
{
	if (out_w)
		*out_w = 0;
	if (out_h)
		*out_h = 0;
	if (font_px < 6 || max_w < 20)
		return NULL;
	g_img = images;
	g_nimg = images ? nimages : 0;
	g_img_h = (int)(font_px * 1.55f);
	g_nplaces = 0;
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
	const int text_h_body = tm.tmHeight;
	int lh_body = tm.tmHeight;
	if (g_nimg > 0 && g_img_h + 2 > lh_body)
		lh_body = g_img_h + 2;
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
				draw_line(dc, pad, pad + y_body + i * lh_body, lh_body, text_h_body, buf, (int)wcslen(buf));
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
			for (int q = 0; q < g_nplaces; q++)
				blit_img(px, w, h, g_places[q].x, g_places[q].y, g_places[q].k);
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
	g_img = NULL;
	g_nimg = 0;
	return tex;
}

gs_texture_t *win_frame_render_text_wrapped(const char *header, int header_indent, const char *title,
					     const char *body, const char *font_name, int font_px, int max_w,
					     int max_body_lines, uint32_t header_abgr, uint32_t title_abgr,
					     uint32_t body_abgr, int reveal_chars, uint32_t *out_w, uint32_t *out_h)
{
	return win_frame_render_text_wrapped_img(header, header_indent, title, body, font_name, font_px, max_w,
						 max_body_lines, header_abgr, title_abgr, body_abgr, reveal_chars, NULL, 0,
						 out_w, out_h);
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
