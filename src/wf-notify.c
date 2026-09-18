#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <obs-module.h>
#include <util/platform.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include "wf-notify.h"
#include "wf-prefs.h"

#define QUEUE_CAP 64

/* ------------------------------------------------------------------ queue */

static struct wf_msg g_queue[WF_TARGET_COUNT][QUEUE_CAP];
static int g_head[WF_TARGET_COUNT], g_count[WF_TARGET_COUNT];
static CRITICAL_SECTION g_qlock;
static INIT_ONCE g_qonce = INIT_ONCE_STATIC_INIT;

static BOOL CALLBACK qinit(PINIT_ONCE o, PVOID p, PVOID *c)
{
	(void)o;
	(void)p;
	(void)c;
	InitializeCriticalSection(&g_qlock);
	return TRUE;
}

static void qlock(void)
{
	InitOnceExecuteOnce(&g_qonce, qinit, NULL, NULL);
	EnterCriticalSection(&g_qlock);
}

static void qunlock(void)
{
	LeaveCriticalSection(&g_qlock);
}

static void copy_field(char *dst, size_t n, const char *src)
{
	size_t i = 0;
	if (!src)
		src = "";
	for (; src[i] && i < n - 1; i++)
		dst[i] = (src[i] == '\r' || src[i] == '\t') ? ' ' : src[i];
	dst[i] = 0;
}

void wf_notify_push(int target, const struct wf_msg *m)
{
	if (target < 0 || target >= WF_TARGET_COUNT || !m)
		return;
	qlock();
	if (g_count[target] == QUEUE_CAP) { /* drop the oldest */
		g_head[target] = (g_head[target] + 1) % QUEUE_CAP;
		g_count[target]--;
	}
	int idx = (g_head[target] + g_count[target]) % QUEUE_CAP;
	g_queue[target][idx] = *m;
	g_count[target]++;
	qunlock();
}

bool wf_notify_pop(int target, struct wf_msg *out)
{
	if (target < 0 || target >= WF_TARGET_COUNT)
		return false;
	bool ok = false;
	qlock();
	if (g_count[target] > 0) {
		if (out)
			*out = g_queue[target][g_head[target]];
		g_head[target] = (g_head[target] + 1) % QUEUE_CAP;
		g_count[target]--;
		ok = true;
	}
	qunlock();
	return ok;
}

/* ---------------------------------------------------------------- parsing */

bool wf_msg_parse_line(const char *line, struct wf_msg *m)
{
	memset(m, 0, sizeof(*m));
	if (!line)
		return false;
	while (*line == ' ' || *line == '\t')
		line++;
	if (!*line)
		return false;

	if (*line == '{') {
		obs_data_t *d = obs_data_create_from_json(line);
		if (!d)
			return false;
		copy_field(m->type, sizeof(m->type), obs_data_get_string(d, "type"));
		copy_field(m->title, sizeof(m->title), obs_data_get_string(d, "title"));
		copy_field(m->text, sizeof(m->text), obs_data_get_string(d, "text"));
		if (!m->text[0])
			copy_field(m->text, sizeof(m->text), obs_data_get_string(d, "message"));
		obs_data_release(d);
		return m->title[0] || m->text[0];
	}

	const char *p1 = strchr(line, '|');
	if (!p1) {
		copy_field(m->text, sizeof(m->text), line);
		strcpy(m->type, "info");
		return m->text[0] != 0;
	}
	const char *p2 = strchr(p1 + 1, '|');
	if (!p2) { /* title|text */
		char tmp[96];
		size_t n = (size_t)(p1 - line);
		if (n >= sizeof(tmp))
			n = sizeof(tmp) - 1;
		memcpy(tmp, line, n);
		tmp[n] = 0;
		copy_field(m->title, sizeof(m->title), tmp);
		copy_field(m->text, sizeof(m->text), p1 + 1);
		strcpy(m->type, "info");
	} else { /* type|title|text */
		char tmp[96];
		size_t n = (size_t)(p1 - line);
		if (n >= sizeof(tmp))
			n = sizeof(tmp) - 1;
		memcpy(tmp, line, n);
		tmp[n] = 0;
		copy_field(m->type, sizeof(m->type), tmp);
		n = (size_t)(p2 - (p1 + 1));
		if (n >= sizeof(tmp))
			n = sizeof(tmp) - 1;
		memcpy(tmp, p1 + 1, n);
		tmp[n] = 0;
		copy_field(m->title, sizeof(m->title), tmp);
		copy_field(m->text, sizeof(m->text), p2 + 1);
	}
	/* type words are matched case-insensitively later; normalise here */
	for (char *c = m->type; *c; c++)
		*c = (char)tolower((unsigned char)*c);
	if (!m->type[0])
		strcpy(m->type, "info");
	return m->title[0] || m->text[0];
}

/* -------------------------------------------------------------- file tail */

void wf_filetail_set(struct wf_filetail *t, const char *path)
{
	if (!path)
		path = "";
	if (strcmp(t->path, path) == 0)
		return;
	strncpy(t->path, path, sizeof(t->path) - 1);
	t->path[sizeof(t->path) - 1] = 0;
	t->pos = 0;
	t->primed = false;
}

void wf_filetail_poll(struct wf_filetail *t, int target)
{
	if (!t->path[0])
		return;
	FILE *f = os_fopen(t->path, "rb");
	if (!f)
		return;
	fseek(f, 0, SEEK_END);
	long long size = _ftelli64(f);
	if (!t->primed) { /* ignore what was already in the file */
		t->pos = size;
		t->primed = true;
		fclose(f);
		return;
	}
	if (size < t->pos)
		t->pos = 0; /* file was truncated / rewritten */
	if (size > t->pos) {
		long long want = size - t->pos;
		if (want > 8192)
			want = 8192;
		char *buf = bmalloc((size_t)want + 1);
		_fseeki64(f, t->pos, SEEK_SET);
		size_t got = fread(buf, 1, (size_t)want, f);
		buf[got] = 0;
		/* consume whole lines only */
		char *start = buf;
		char *nl;
		size_t consumed = 0;
		while ((nl = strchr(start, '\n')) != NULL) {
			*nl = 0;
			struct wf_msg m;
			if (wf_msg_parse_line(start, &m))
				wf_notify_push(target, &m);
			consumed += (size_t)(nl - start) + 1;
			start = nl + 1;
		}
		if (consumed == 0 && got >= 8192)
			consumed = got; /* a single huge line: skip it */
		t->pos += (long long)consumed;
		bfree(buf);
	}
	fclose(f);
}

/* ------------------------------------------------------------- web address */

#if defined(_WIN32)

#include <bcrypt.h>

static SOCKET g_listen = INVALID_SOCKET;
static HANDLE g_thread = NULL;
static volatile LONG g_stop = 0;
static int g_refs = 0;
static bool g_wsa = false;

static int hexval(char c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	return -1;
}

static void url_decode(char *s)
{
	char *o = s;
	for (; *s; s++) {
		if (*s == '+') {
			*o++ = ' ';
		} else if (*s == '%' && hexval(s[1]) >= 0 && hexval(s[2]) >= 0) {
			*o++ = (char)(hexval(s[1]) * 16 + hexval(s[2]));
			s += 2;
		} else {
			*o++ = *s;
		}
	}
	*o = 0;
}

/* query value into out (decoded); returns true if present */
static bool query_get(const char *query, const char *key, char *out, size_t n)
{
	size_t kl = strlen(key);
	const char *p = query;
	while (p && *p) {
		if (strncmp(p, key, kl) == 0 && p[kl] == '=') {
			const char *v = p + kl + 1;
			size_t len = strcspn(v, "&");
			if (len >= n)
				len = n - 1;
			memcpy(out, v, len);
			out[len] = 0;
			url_decode(out);
			return true;
		}
		p = strchr(p, '&');
		if (p)
			p++;
	}
	return false;
}

static bool token_equal(const char *a, const char *b)
{
	size_t la = strlen(a), lb = strlen(b);
	unsigned char diff = (unsigned char)(la != lb);
	size_t n = la < lb ? la : lb;
	for (size_t i = 0; i < n; i++)
		diff |= (unsigned char)(a[i] ^ b[i]);
	return diff == 0;
}

static void send_reply(SOCKET c, int code, const char *reason, const char *body)
{
	char buf[256];
	int n = snprintf(buf, sizeof(buf),
			 "HTTP/1.1 %d %s\r\nContent-Type: text/plain\r\nContent-Length: %d\r\nConnection: close\r\n\r\n%s",
			 code, reason, (int)strlen(body), body);
	send(c, buf, n, 0);
}

static void handle_client(SOCKET c)
{
	DWORD tmo = 2000;
	setsockopt(c, SOL_SOCKET, SO_RCVTIMEO, (const char *)&tmo, sizeof(tmo));

	char req[16384];
	int total = 0;
	int header_end = -1, content_len = 0;
	while (total < (int)sizeof(req) - 1) {
		int r = recv(c, req + total, (int)sizeof(req) - 1 - total, 0);
		if (r <= 0)
			break;
		total += r;
		req[total] = 0;
		if (header_end < 0) {
			char *he = strstr(req, "\r\n\r\n");
			if (he) {
				header_end = (int)(he - req) + 4;
				char *cl = strstr(req, "Content-Length:");
				if (!cl)
					cl = strstr(req, "content-length:");
				if (cl)
					content_len = atoi(cl + 15);
			}
		}
		if (header_end >= 0 && total >= header_end + content_len)
			break;
	}
	if (header_end < 0) {
		send_reply(c, 400, "Bad Request", "bad request");
		return;
	}
	if (content_len > 8192) {
		send_reply(c, 413, "Payload Too Large", "too large");
		return;
	}

	char method[8] = {0}, target[1024] = {0};
	if (sscanf(req, "%7s %1023s", method, target) != 2 || (strcmp(method, "GET") && strcmp(method, "POST"))) {
		send_reply(c, 405, "Method Not Allowed", "use GET or POST");
		return;
	}
	char *query = strchr(target, '?');
	if (query)
		*query++ = 0;
	else
		query = target + strlen(target);

	int tgt = -1;
	if (!strcmp(target, "/toast"))
		tgt = WF_TARGET_TOAST;
	else if (!strcmp(target, "/assistant"))
		tgt = WF_TARGET_ASSISTANT;

	/* token: query parameter or X-WF-Token header */
	char supplied[96] = {0};
	if (!query_get(query, "token", supplied, sizeof(supplied))) {
		char *h = strstr(req, "X-WF-Token:");
		if (!h)
			h = strstr(req, "x-wf-token:");
		if (h) {
			h += 11;
			while (*h == ' ')
				h++;
			size_t len = strcspn(h, "\r\n");
			if (len >= sizeof(supplied))
				len = sizeof(supplied) - 1;
			memcpy(supplied, h, len);
			supplied[len] = 0;
		}
	}
	const char *expected = wf_prefs_get()->http_token;
	if (!expected[0] || !token_equal(supplied, expected)) {
		send_reply(c, 401, "Unauthorized", "missing or wrong token");
		return;
	}
	if (tgt < 0) {
		send_reply(c, 404, "Not Found", "use /toast or /assistant");
		return;
	}

	struct wf_msg m;
	memset(&m, 0, sizeof(m));
	bool ok = false;
	if (content_len > 0 && header_end + content_len <= total) {
		char body[8200];
		memcpy(body, req + header_end, (size_t)content_len);
		body[content_len] = 0;
		ok = wf_msg_parse_line(body, &m);
	}
	if (!ok) {
		char v[400];
		if (query_get(query, "type", v, sizeof(v)))
			copy_field(m.type, sizeof(m.type), v);
		if (query_get(query, "title", v, sizeof(v)))
			copy_field(m.title, sizeof(m.title), v);
		if (query_get(query, "text", v, sizeof(v)))
			copy_field(m.text, sizeof(m.text), v);
		for (char *ch = m.type; *ch; ch++)
			*ch = (char)tolower((unsigned char)*ch);
		if (!m.type[0])
			strcpy(m.type, "info");
		ok = m.title[0] || m.text[0];
	}
	if (!ok) {
		send_reply(c, 400, "Bad Request", "send title and/or text");
		return;
	}
	wf_notify_push(tgt, &m);
	send_reply(c, 200, "OK", "ok");
}

static DWORD WINAPI server_thread(LPVOID unused)
{
	(void)unused;
	while (!g_stop) {
		fd_set fds;
		FD_ZERO(&fds);
		FD_SET(g_listen, &fds);
		struct timeval tv = {0, 200000};
		int r = select(0, &fds, NULL, NULL, &tv);
		if (r <= 0)
			continue;
		SOCKET c = accept(g_listen, NULL, NULL);
		if (c == INVALID_SOCKET)
			continue;
		handle_client(c);
		shutdown(c, SD_BOTH);
		closesocket(c);
	}
	return 0;
}

static void server_stop(void)
{
	if (!g_thread)
		return;
	InterlockedExchange(&g_stop, 1);
	WaitForSingleObject(g_thread, 3000);
	CloseHandle(g_thread);
	g_thread = NULL;
	if (g_listen != INVALID_SOCKET) {
		closesocket(g_listen);
		g_listen = INVALID_SOCKET;
	}
	blog(LOG_INFO, "[win-frame-filter] local web address stopped");
}

static void server_start(void)
{
	if (g_thread)
		return;
	struct wf_prefs *p = wf_prefs_get();
	if (!p->http_enabled || !p->http_token[0] || p->http_port < 1024 || p->http_port > 65535)
		return;
	if (!g_wsa) {
		WSADATA wsa;
		if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
			return;
		g_wsa = true;
	}
	g_listen = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (g_listen == INVALID_SOCKET)
		return;
	BOOL excl = TRUE;
	setsockopt(g_listen, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, (const char *)&excl, sizeof(excl));
	struct sockaddr_in a;
	memset(&a, 0, sizeof(a));
	a.sin_family = AF_INET;
	a.sin_port = htons((u_short)p->http_port);
	a.sin_addr.s_addr = htonl(INADDR_LOOPBACK); /* this computer only */
	if (bind(g_listen, (struct sockaddr *)&a, sizeof(a)) != 0 || listen(g_listen, 4) != 0) {
		blog(LOG_WARNING, "[win-frame-filter] could not open 127.0.0.1:%d (port in use?)", p->http_port);
		closesocket(g_listen);
		g_listen = INVALID_SOCKET;
		return;
	}
	InterlockedExchange(&g_stop, 0);
	g_thread = CreateThread(NULL, 0, server_thread, NULL, 0, NULL);
	if (!g_thread) {
		closesocket(g_listen);
		g_listen = INVALID_SOCKET;
		return;
	}
	blog(LOG_INFO, "[win-frame-filter] local web address listening on 127.0.0.1:%d", p->http_port);
}

void wf_notify_http_acquire(void)
{
	if (++g_refs == 1)
		server_start();
}

void wf_notify_http_release(void)
{
	if (g_refs > 0 && --g_refs == 0)
		server_stop();
}

void wf_notify_http_apply_prefs(void)
{
	server_stop();
	if (g_refs > 0)
		server_start();
}

bool wf_notify_http_running(void)
{
	return g_thread != NULL;
}

#else

void wf_notify_http_acquire(void) {}
void wf_notify_http_release(void) {}
void wf_notify_http_apply_prefs(void) {}
bool wf_notify_http_running(void) { return false; }

#endif
