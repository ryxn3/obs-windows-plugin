#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <obs-module.h>
#include "wf-emotes.h"
#include "wf-http.h"

#define WF_TW_7TV_BIT 32
#define WF_TW_BTTV_BIT 64

#ifdef _WIN32
#define INITGUID
#define COBJMACROS
#include <windows.h>
#include <shlwapi.h>
#include <wincodec.h>

/* ------------------------------------------------------------ WIC decode */

uint8_t *wf_emote_decode(const uint8_t *data, size_t len, uint32_t *w, uint32_t *h)
{
	*w = *h = 0;
	IWICImagingFactory *fac = NULL;
	IStream *stream = NULL;
	IWICBitmapDecoder *dec = NULL;
	IWICBitmapFrameDecode *frame = NULL;
	IWICFormatConverter *conv = NULL;
	uint8_t *out = NULL;

	if (FAILED(CoCreateInstance(&CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER, &IID_IWICImagingFactory,
				    (void **)&fac)))
		return NULL;
	stream = SHCreateMemStream(data, (UINT)len);
	if (!stream)
		goto done;
	if (FAILED(IWICImagingFactory_CreateDecoderFromStream(fac, stream, NULL, WICDecodeMetadataCacheOnDemand, &dec)))
		goto done;
	if (FAILED(IWICBitmapDecoder_GetFrame(dec, 0, &frame)))
		goto done;
	if (FAILED(IWICImagingFactory_CreateFormatConverter(fac, &conv)))
		goto done;
	if (FAILED(IWICFormatConverter_Initialize(conv, (IWICBitmapSource *)frame, &GUID_WICPixelFormat32bppPBGRA,
						  WICBitmapDitherTypeNone, NULL, 0.0, WICBitmapPaletteTypeCustom)))
		goto done;
	UINT iw = 0, ih = 0;
	IWICBitmapSource_GetSize((IWICBitmapSource *)conv, &iw, &ih);
	if (!iw || !ih || iw > 1024 || ih > 1024)
		goto done;
	out = malloc((size_t)iw * ih * 4);
	if (!out)
		goto done;
	if (FAILED(IWICBitmapSource_CopyPixels((IWICBitmapSource *)conv, NULL, iw * 4, iw * ih * 4, out))) {
		free(out);
		out = NULL;
		goto done;
	}
	/* premultiplied BGRA -> straight RGBA */
	for (size_t i = 0; i < (size_t)iw * ih; i++) {
		uint8_t *p = out + i * 4;
		uint8_t b = p[0], g = p[1], r = p[2], a = p[3];
		if (a > 0 && a < 255) {
			r = (uint8_t)((r * 255 + a / 2) / a);
			g = (uint8_t)((g * 255 + a / 2) / a);
			b = (uint8_t)((b * 255 + a / 2) / a);
		}
		p[0] = r;
		p[1] = g;
		p[2] = b;
	}
	*w = iw;
	*h = ih;
done:
	if (conv)
		IWICFormatConverter_Release(conv);
	if (frame)
		IWICBitmapFrameDecode_Release(frame);
	if (dec)
		IWICBitmapDecoder_Release(dec);
	if (stream)
		IStream_Release(stream);
	IWICImagingFactory_Release(fac);
	return out;
}

/* --------------------------------------------------------------- caches */

#define MAX_IMAGES 600
#define MAX_JOBS 256

struct image {
	char tag[40];
	int state; /* 0 pending, 1 ready, -1 failed */
	uint32_t w, h;
	uint8_t *rgba;
};

struct name_entry {
	char name[32];
	char tag[40];
};

enum { JOB_7TV_CHANNEL, JOB_7TV_GLOBAL, JOB_BTTV_CHANNEL, JOB_BTTV_GLOBAL, JOB_IMAGE };

struct job {
	int type;
	char arg[40];
};

static CRITICAL_SECTION g_lock;
static INIT_ONCE g_once = INIT_ONCE_STATIC_INIT;
static struct image g_img[MAX_IMAGES];
static int g_nimg;
static struct name_entry *g_names;
static size_t g_nnames, g_cap_names;
static struct job g_jobs[MAX_JOBS];
static int g_jhead, g_jcount;
static HANDLE g_thread;
static volatile LONG g_quit;
static volatile LONG g_generation;
static char g_room[24];
static int g_flags;
static bool g_global7, g_globalb;

static BOOL CALLBACK init_lock(PINIT_ONCE o, PVOID p, PVOID *c)
{
	(void)o;
	(void)p;
	(void)c;
	InitializeCriticalSection(&g_lock);
	return TRUE;
}

static void lock(void)
{
	InitOnceExecuteOnce(&g_once, init_lock, NULL, NULL);
	EnterCriticalSection(&g_lock);
}

static void unlock(void)
{
	LeaveCriticalSection(&g_lock);
}

static void push_job_locked(int type, const char *arg)
{
	if (g_jcount >= MAX_JOBS)
		return;
	struct job *j = &g_jobs[(g_jhead + g_jcount) % MAX_JOBS];
	j->type = type;
	snprintf(j->arg, sizeof(j->arg), "%s", arg ? arg : "");
	g_jcount++;
}

static int name_cmp(const void *a, const void *b)
{
	return strcmp(((const struct name_entry *)a)->name, ((const struct name_entry *)b)->name);
}

static void add_name(const char *name, const char *tag)
{
	if (!name || !*name || strlen(name) >= 32)
		return;
	lock();
	if (g_nnames == g_cap_names) {
		g_cap_names = g_cap_names ? g_cap_names * 2 : 1024;
		g_names = realloc(g_names, g_cap_names * sizeof(*g_names));
	}
	if (g_names) {
		snprintf(g_names[g_nnames].name, sizeof(g_names[g_nnames].name), "%s", name);
		snprintf(g_names[g_nnames].tag, sizeof(g_names[g_nnames].tag), "%s", tag);
		g_nnames++;
	}
	unlock();
}

static void sort_names(void)
{
	lock();
	if (g_names && g_nnames > 1)
		qsort(g_names, g_nnames, sizeof(*g_names), name_cmp);
	unlock();
}

/* ------------------------------------------------------------- worker */

static void build_urls(const char *tag, char *u1, char *u2, size_t n)
{
	u1[0] = u2[0] = 0;
	const char *id = tag + 2;
	if (tag[0] == 'T')
		snprintf(u1, n, "https://static-cdn.jtvnw.net/emoticons/v2/%s/static/dark/2.0", id);
	else if (tag[0] == '7') {
		snprintf(u1, n, "https://cdn.7tv.app/emote/%s/2x_static.webp", id);
		snprintf(u2, n, "https://cdn.7tv.app/emote/%s/2x.gif", id);
	} else if (tag[0] == 'B') {
		snprintf(u1, n, "https://cdn.betterttv.net/emote/%s/2x.png", id);
		snprintf(u2, n, "https://cdn.betterttv.net/emote/%s/2x.webp", id);
	}
}

static bool fetch_decode(const char *url, uint8_t **rgba, uint32_t *w, uint32_t *h)
{
	if (!url[0])
		return false;
	char *body = NULL, err[128];
	size_t len = 0;
	if (!wf_http_get(url, 2 * 1024 * 1024, &body, &len, err, sizeof(err)))
		return false;
	*rgba = wf_emote_decode((const uint8_t *)body, len, w, h);
	bfree(body);
	return *rgba != NULL;
}

static void run_image_job(const char *tag)
{
	char u1[160], u2[160];
	build_urls(tag, u1, u2, sizeof(u1));
	uint8_t *rgba = NULL;
	uint32_t w = 0, h = 0;
	bool ok = fetch_decode(u1, &rgba, &w, &h) || fetch_decode(u2, &rgba, &w, &h);
	lock();
	for (int i = 0; i < g_nimg; i++) {
		if (!strcmp(g_img[i].tag, tag)) {
			g_img[i].w = w;
			g_img[i].h = h;
			g_img[i].rgba = rgba;
			g_img[i].state = ok ? 1 : -1;
		}
	}
	unlock();
	InterlockedIncrement(&g_generation);
}

static obs_data_array_t *json_array(obs_data_t *root, const char *key)
{
	return root ? obs_data_get_array(root, key) : NULL;
}

static void read_emotes(obs_data_array_t *arr, const char *name_key, char kind)
{
	if (!arr)
		return;
	size_t n = obs_data_array_count(arr);
	for (size_t i = 0; i < n; i++) {
		obs_data_t *e = obs_data_array_item(arr, i);
		const char *id = obs_data_get_string(e, "id");
		const char *nm = obs_data_get_string(e, name_key);
		if (id && *id && strlen(id) < 36) {
			char tag[40];
			snprintf(tag, sizeof(tag), "%c:%s", kind, id);
			add_name(nm, tag);
		}
		obs_data_release(e);
	}
	obs_data_array_release(arr);
}

static void run_list_job(int type, const char *room)
{
	char url[200], err[128];
	char *body = NULL;
	size_t len = 0;
	if (type == JOB_7TV_CHANNEL)
		snprintf(url, sizeof(url), "https://7tv.io/v3/users/twitch/%s", room);
	else if (type == JOB_7TV_GLOBAL)
		snprintf(url, sizeof(url), "https://7tv.io/v3/emote-sets/global");
	else if (type == JOB_BTTV_CHANNEL)
		snprintf(url, sizeof(url), "https://api.betterttv.net/3/cached/users/twitch/%s", room);
	else
		snprintf(url, sizeof(url), "https://api.betterttv.net/3/cached/emotes/global");
	if (!wf_http_get(url, 12 * 1024 * 1024, &body, &len, err, sizeof(err))) {
		return;
	}

	if (type == JOB_BTTV_GLOBAL) {
		/* a bare JSON array: wrap it so libobs can parse it */
		size_t n = len + 16;
		char *wrapped = bmalloc(n);
		snprintf(wrapped, n, "{\"a\":%s}", body);
		obs_data_t *root = obs_data_create_from_json(wrapped);
		bfree(wrapped);
		read_emotes(json_array(root, "a"), "code", 'B');
		obs_data_release(root);
	} else {
		obs_data_t *root = obs_data_create_from_json(body);
		if (root) {
			if (type == JOB_7TV_CHANNEL) {
				obs_data_t *set = obs_data_get_obj(root, "emote_set");
				if (set) {
					read_emotes(json_array(set, "emotes"), "name", '7');
					obs_data_release(set);
				}
			} else if (type == JOB_7TV_GLOBAL) {
				read_emotes(json_array(root, "emotes"), "name", '7');
			} else {
				read_emotes(json_array(root, "channelEmotes"), "code", 'B');
				read_emotes(json_array(root, "sharedEmotes"), "code", 'B');
			}
			obs_data_release(root);
		}
	}
	bfree(body);
	sort_names();
	InterlockedIncrement(&g_generation);
}

static DWORD WINAPI worker(LPVOID p)
{
	(void)p;
	CoInitializeEx(NULL, COINIT_MULTITHREADED);
	while (!g_quit) {
		struct job j;
		bool have = false;
		lock();
		if (g_jcount > 0) {
			j = g_jobs[g_jhead];
			g_jhead = (g_jhead + 1) % MAX_JOBS;
			g_jcount--;
			have = true;
		}
		unlock();
		if (!have) {
			Sleep(40);
			continue;
		}
		if (j.type == JOB_IMAGE)
			run_image_job(j.arg);
		else
			run_list_job(j.type, j.arg);
	}
	CoUninitialize();
	return 0;
}

static void ensure_worker_locked(void)
{
	if (!g_thread) {
		g_quit = 0;
		g_thread = CreateThread(NULL, 0, worker, NULL, 0, NULL);
	}
}

/* -------------------------------------------------------------- public */

void wf_emotes_set_channel(const char *room_id, int flags)
{
	if (!room_id || !*room_id || strlen(room_id) > 20)
		return;
	for (const char *c = room_id; *c; c++)
		if (*c < '0' || *c > '9')
			return;
	lock();
	bool want7 = (flags & WF_TW_7TV_BIT) != 0, wantb = (flags & WF_TW_BTTV_BIT) != 0;
	bool new_room = strcmp(room_id, g_room) != 0;
	if (new_room) {
		snprintf(g_room, sizeof(g_room), "%s", room_id);
		g_nnames = 0;
		g_global7 = g_globalb = false;
		g_flags = 0;
	}
	if (want7 && !(g_flags & WF_TW_7TV_BIT)) {
		push_job_locked(JOB_7TV_CHANNEL, room_id);
		push_job_locked(JOB_7TV_GLOBAL, "");
		g_global7 = true;
	}
	if (wantb && !(g_flags & WF_TW_BTTV_BIT)) {
		push_job_locked(JOB_BTTV_CHANNEL, room_id);
		push_job_locked(JOB_BTTV_GLOBAL, "");
		g_globalb = true;
	}
	g_flags |= (want7 ? WF_TW_7TV_BIT : 0) | (wantb ? WF_TW_BTTV_BIT : 0);
	if (g_jcount > 0)
		ensure_worker_locked();
	unlock();
}

bool wf_emote_find_name(const char *word, char *tag, size_t tag_sz)
{
	bool found = false;
	if (!word || !*word || strlen(word) >= 32)
		return false;
	lock();
	if (g_names && g_nnames) {
		struct name_entry key;
		snprintf(key.name, sizeof(key.name), "%s", word);
		struct name_entry *hit = bsearch(&key, g_names, g_nnames, sizeof(*g_names), name_cmp);
		if (hit) {
			snprintf(tag, tag_sz, "%s", hit->tag);
			found = true;
		}
	}
	unlock();
	return found;
}

int wf_emote_get(const char *tag, uint32_t *w, uint32_t *h, const uint8_t **rgba)
{
	int state = 0;
	lock();
	struct image *im = NULL;
	for (int i = 0; i < g_nimg; i++)
		if (!strcmp(g_img[i].tag, tag)) {
			im = &g_img[i];
			break;
		}
	if (!im) {
		if (g_nimg >= MAX_IMAGES) {
			unlock();
			return -1;
		}
		im = &g_img[g_nimg++];
		memset(im, 0, sizeof(*im));
		snprintf(im->tag, sizeof(im->tag), "%s", tag);
		push_job_locked(JOB_IMAGE, tag);
		ensure_worker_locked();
	}
	state = im->state;
	if (state == 1) {
		*w = im->w;
		*h = im->h;
		*rgba = im->rgba;
	}
	unlock();
	return state;
}

unsigned wf_emotes_generation(void)
{
	return (unsigned)g_generation;
}

void wf_emotes_shutdown(void)
{
	lock();
	HANDLE t = g_thread;
	g_thread = NULL;
	InterlockedExchange(&g_quit, 1);
	unlock();
	if (t) {
		WaitForSingleObject(t, 20000);
		CloseHandle(t);
	}
}

#else
uint8_t *wf_emote_decode(const uint8_t *d, size_t l, uint32_t *w, uint32_t *h) { (void)d; (void)l; *w = *h = 0; return NULL; }
void wf_emotes_set_channel(const char *r, int f) { (void)r; (void)f; }
bool wf_emote_find_name(const char *w, char *t, size_t n) { (void)w; (void)t; (void)n; return false; }
int wf_emote_get(const char *t, uint32_t *w, uint32_t *h, const uint8_t **p) { (void)t; (void)w; (void)h; (void)p; return -1; }
unsigned wf_emotes_generation(void) { return 0; }
void wf_emotes_shutdown(void) {}
#endif
