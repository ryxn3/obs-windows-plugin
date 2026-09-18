#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <obs-module.h>
#include <util/platform.h>
#include "wf-twitch.h"
#include "wf-notify.h"
#include "wf-emotes.h"

/* ---------------------------------------------------------------- parser */

/* value of tag `key` in the "@k=v;k=v" block, unescaped */
static bool tag_get(const char *line, const char *key, char *out, size_t n)
{
	out[0] = 0;
	if (line[0] != '@')
		return false;
	const char *end = strchr(line, ' ');
	if (!end)
		return false;
	size_t kl = strlen(key);
	const char *p = line + 1;
	while (p < end) {
		const char *semi = memchr(p, ';', (size_t)(end - p));
		const char *stop = semi ? semi : end;
		if ((size_t)(stop - p) > kl && !strncmp(p, key, kl) && p[kl] == '=') {
			const char *v = p + kl + 1;
			size_t o = 0;
			for (; v < stop && o < n - 1; v++) {
				if (*v == '\\' && v + 1 < stop) {
					v++;
					out[o++] = (*v == 's') ? ' ' : (*v == ':') ? ';' : (*v == 'n') ? ' ' : *v;
				} else {
					out[o++] = *v;
				}
			}
			out[o] = 0;
			return true;
		}
		p = stop + 1;
	}
	return false;
}

static void copy_to(char *dst, size_t n, const char *src)
{
	size_t i = 0;
	for (; src[i] && i < n - 1; i++)
		dst[i] = (src[i] == '\r' || src[i] == '\n') ? ' ' : src[i];
	dst[i] = 0;
}

/* Copies `text` into m->text, replacing emote words by placeholder characters
 * and filling m->emote[]. `emotes_tag` is Twitch's "id:0-4,6-10/id2:.." value
 * (positions count characters of the original message, cp_offset skips a
 * stripped prefix). */
struct nat_emote {
	int start, end;
	char id[20];
};

static void put_placeholder(char *dst, size_t n, size_t *o, int k)
{
	if (*o + 3 < n) {
		dst[(*o)++] = (char)0xEE;
		dst[(*o)++] = (char)0x80;
		dst[(*o)++] = (char)(0x80 + k);
	}
}

static void encode_emotes(const char *text, const char *emotes_tag, int cp_offset, int flags, struct wf_msg *m)
{
	struct nat_emote nat[40];
	int nn = 0;
	if ((flags & WF_TW_EMOTES) && emotes_tag && *emotes_tag) {
		const char *p = emotes_tag;
		while (*p && nn < 40) {
			char id[20];
			int il = 0;
			while (*p && *p != ':' && il < 19)
				id[il++] = *p++;
			id[il] = 0;
			if (*p == ':')
				p++;
			while (*p && *p != '/') {
				int a = 0, b = 0;
				if (sscanf(p, "%d-%d", &a, &b) == 2 && nn < 40) {
					nat[nn].start = a - cp_offset;
					nat[nn].end = b - cp_offset;
					strcpy(nat[nn].id, id);
					nn++;
				}
				while (*p && *p != ',' && *p != '/')
					p++;
				if (*p == ',')
					p++;
			}
			if (*p == '/')
				p++;
		}
	}
	const bool names = (flags & (WF_TW_7TV | WF_TW_BTTV)) != 0;
	char out[sizeof(m->text)];
	size_t o = 0;
	m->nemote = 0;
	int cp = 0;
	const char *c = text;
	while (*c && o < sizeof(out) - 4) {
		if (*c == ' ' || *c == '\r' || *c == '\n') {
			out[o++] = ' ';
			c++;
			cp++;
			continue;
		}
		const char *w = c;
		int wcp = cp;
		while (*c && *c != ' ' && *c != '\r' && *c != '\n') {
			if (((unsigned char)*c & 0xC0) != 0x80)
				cp++;
			c++;
		}
		size_t wl = (size_t)(c - w);
		char tag[40] = "";
		for (int i = 0; i < nn; i++)
			if (nat[i].start == wcp) {
				snprintf(tag, sizeof(tag), "T:%s", nat[i].id);
				break;
			}
		if (!tag[0] && names && wl < 32) {
			char word[32];
			memcpy(word, w, wl);
			word[wl] = 0;
			wf_emote_find_name(word, tag, sizeof(tag));
		}
		if (tag[0] && m->nemote < 12) {
			snprintf(m->emote[m->nemote], sizeof(m->emote[0]), "%s", tag);
			put_placeholder(out, sizeof(out), &o, m->nemote);
			m->nemote++;
		} else {
			for (size_t i = 0; i < wl && o < sizeof(out) - 4; i++)
				out[o++] = w[i];
		}
	}
	out[o] = 0;
	memcpy(m->text, out, o + 1);
}

bool wf_twitch_parse_line(const char *line, int flags, struct wf_msg *out)
{
	memset(out, 0, sizeof(*out));
	const char *cmd = strchr(line, ' ');
	if (!cmd)
		return false;
	/* skip the tag block, then the ":nick!user@host" prefix */
	const char *rest = line;
	if (rest[0] == '@')
		rest = cmd + 1;
	if (rest[0] == ':') {
		const char *sp = strchr(rest, ' ');
		if (!sp)
			return false;
		rest = sp + 1;
	}
	bool privmsg = !strncmp(rest, "PRIVMSG ", 8);
	bool usernotice = !strncmp(rest, "USERNOTICE ", 11);
	if (!privmsg && !usernotice)
		return false;
	const char *msgtext = strstr(rest, " :");
	msgtext = msgtext ? msgtext + 2 : "";

	char name[64], val[64], sys[300];
	if (!tag_get(line, "display-name", name, sizeof(name)) || !name[0]) {
		/* fall back to the login in the prefix */
		const char *c = strchr(line, ':');
		size_t i = 0;
		if (c)
			for (c++; *c && *c != '!' && *c != ' ' && i < sizeof(name) - 1; c++)
				name[i++] = *c;
		name[i] = 0;
	}

	if (privmsg) {
		if (tag_get(line, "bits", val, sizeof(val)) && atoi(val) > 0) {
			if (!(flags & WF_TW_BITS))
				return false;
			strcpy(out->type, "donation");
			snprintf(out->title, sizeof(out->title), "%s cheered %d bits", name, atoi(val));
			{
				char plain[sizeof(out->text)], et[400] = "";
				copy_to(plain, sizeof(plain), msgtext);
				tag_get(line, "emotes", et, sizeof(et));
				encode_emotes(plain, et, 0, flags, out);
			}
			if (!out->text[0])
				strcpy(out->text, "Thank you!");
			return true;
		}
		if (!(flags & WF_TW_CHAT))
			return false;
		int cp_off = 0;
		if (!strncmp(msgtext, "\x01" "ACTION ", 8)) {
			msgtext += 8;
			cp_off = 8;
		}
		strcpy(out->type, "chat");
		copy_to(out->title, sizeof(out->title), name);
		char plain[sizeof(out->text)];
		copy_to(plain, sizeof(plain), msgtext);
		size_t l = strlen(plain);
		if (l && plain[l - 1] == '\x01')
			plain[l - 1] = 0;
		char et[400] = "", room[24] = "";
		tag_get(line, "emotes", et, sizeof(et));
		if (tag_get(line, "room-id", room, sizeof(room)))
			wf_emotes_set_channel(room, flags);
		encode_emotes(plain, et, cp_off, flags, out);
		return out->text[0] != 0;
	}

	if (!tag_get(line, "msg-id", val, sizeof(val)))
		return false;
	tag_get(line, "system-msg", sys, sizeof(sys));
	if (!strcmp(val, "raid")) {
		if (!(flags & WF_TW_RAIDS))
			return false;
		char n[16];
		tag_get(line, "msg-param-viewerCount", n, sizeof(n));
		strcpy(out->type, "info");
		snprintf(out->title, sizeof(out->title), "Incoming raid!");
		snprintf(out->text, sizeof(out->text), "%s is raiding with %s viewers", name, n[0] ? n : "some");
		return true;
	}
	if (!strcmp(val, "sub") || !strcmp(val, "resub") || !strcmp(val, "subgift") || !strcmp(val, "submysterygift") ||
	    !strcmp(val, "anonsubgift") || !strcmp(val, "giftpaidupgrade")) {
		if (!(flags & WF_TW_SUBS))
			return false;
		strcpy(out->type, "sub");
		strcpy(out->title, !strcmp(val, "resub") ? "Resubscribed" : (!strncmp(val, "sub", 3) && strlen(val) == 3) ? "New subscriber" : "Gift subscription");
		copy_to(out->text, sizeof(out->text), sys[0] ? sys : name);
		if (msgtext[0]) {
			size_t l = strlen(out->text);
			snprintf(out->text + l, sizeof(out->text) - l, " - %s", msgtext);
		}
		return true;
	}
	return false;
}

#ifdef _WIN32
#include <windows.h>
#include <winhttp.h>

/* ------------------------------------------------------------ connection */

static CRITICAL_SECTION g_lock;
static INIT_ONCE g_once = INIT_ONCE_STATIC_INIT;
static const void *g_owner;
static char g_channel[64];
static int g_flags;
static volatile int g_status = WF_TW_OFF;
static volatile LONG g_generation; /* bump to make the running thread quit */
static HANDLE g_thread;

static BOOL CALLBACK init_lock(PINIT_ONCE o, PVOID p, PVOID *c)
{
	(void)o;
	(void)p;
	(void)c;
	InitializeCriticalSection(&g_lock);
	return TRUE;
}

static bool ws_send(HINTERNET ws, const char *s)
{
	return WinHttpWebSocketSend(ws, WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE, (PVOID)s, (DWORD)strlen(s)) == 0;
}

static void handle_text(char *buf, HINTERNET ws, int flags, LONG gen)
{
	char *line = buf;
	while (line && *line) {
		char *nl = strpbrk(line, "\r\n");
		if (nl)
			*nl = 0;
		if (!strncmp(line, "PING", 4)) {
			char pong[128];
			snprintf(pong, sizeof(pong), "PONG%s", line + 4);
			ws_send(ws, pong);
		} else if (line[0]) {
			struct wf_msg m;
			if (g_generation == gen && wf_twitch_parse_line(line, flags, &m))
				wf_notify_push(WF_TARGET_TOAST, &m);
		}
		if (!nl)
			break;
		line = nl + 1;
		while (*line == '\r' || *line == '\n')
			line++;
	}
}

/* one connection attempt; returns when it drops or the generation changes */
static void session(LONG gen, const char *channel, int flags)
{
	HINTERNET ses = WinHttpOpen(L"win-frame-filter", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, NULL, NULL, 0);
	if (!ses)
		return;
	HINTERNET con = WinHttpConnect(ses, L"irc-ws.chat.twitch.tv", 443, 0);
	HINTERNET req = con ? WinHttpOpenRequest(con, L"GET", L"/", NULL, NULL, NULL, WINHTTP_FLAG_SECURE) : NULL;
	HINTERNET ws = NULL;
	if (req && WinHttpSetOption(req, WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET, NULL, 0) &&
	    WinHttpSendRequest(req, NULL, 0, NULL, 0, 0, 0) && WinHttpReceiveResponse(req, NULL))
		ws = WinHttpWebSocketCompleteUpgrade(req, 0);
	if (req)
		WinHttpCloseHandle(req);
	if (ws) {
		char nick[32];
		snprintf(nick, sizeof(nick), "NICK justinfan%u", 10000u + (unsigned)(GetTickCount() % 80000u));
		char join[100];
		snprintf(join, sizeof(join), "JOIN #%s", channel);
		ws_send(ws, "CAP REQ :twitch.tv/tags twitch.tv/commands");
		ws_send(ws, "PASS SCHMOOOIIE");
		ws_send(ws, nick);
		ws_send(ws, join);
		g_status = WF_TW_CONNECTED;
		char *buf = malloc(16384);
		char *acc = NULL;
		size_t acc_len = 0;
		while (g_generation == gen) {
			DWORD got = 0;
			WINHTTP_WEB_SOCKET_BUFFER_TYPE type;
			DWORD r = WinHttpWebSocketReceive(ws, buf, 16383, &got, &type);
			if (r != 0 || type == WINHTTP_WEB_SOCKET_CLOSE_BUFFER_TYPE)
				break;
			acc = realloc(acc, acc_len + got + 1);
			memcpy(acc + acc_len, buf, got);
			acc_len += got;
			acc[acc_len] = 0;
			if (type == WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE) {
				handle_text(acc, ws, flags, gen);
				free(acc);
				acc = NULL;
				acc_len = 0;
			}
		}
		free(acc);
		free(buf);
		WinHttpCloseHandle(ws);
	}
	if (con)
		WinHttpCloseHandle(con);
	WinHttpCloseHandle(ses);
}

struct job {
	LONG gen;
	char channel[64];
	int flags;
};

static DWORD WINAPI thread_main(LPVOID p)
{
	struct job *j = p;
	int failures = 0;
	while (g_generation == j->gen) {
		g_status = WF_TW_CONNECTING;
		DWORD t0 = GetTickCount();
		session(j->gen, j->channel, j->flags);
		if (g_generation != j->gen)
			break;
		g_status = WF_TW_ERROR;
		failures = (GetTickCount() - t0 > 15000) ? 0 : failures + 1;
		int wait = failures > 5 ? 30 : 4;
		for (int i = 0; i < wait * 10 && g_generation == j->gen; i++)
			Sleep(100);
	}
	free(j);
	return 0;
}

static void stop_locked(void)
{
	InterlockedIncrement(&g_generation);
	g_thread = NULL; /* the thread frees itself; it notices the new generation */
	g_status = WF_TW_OFF;
}

void wf_twitch_set(const void *owner, const char *channel, int flags)
{
	InitOnceExecuteOnce(&g_once, init_lock, NULL, NULL);
	char ch[64];
	size_t o = 0;
	if (channel) {
		while (*channel == '#' || *channel == ' ')
			channel++;
		for (; *channel && o < sizeof(ch) - 1 && !isspace((unsigned char)*channel); channel++)
			ch[o++] = (char)tolower((unsigned char)*channel);
	}
	ch[o] = 0;
	EnterCriticalSection(&g_lock);
	if (g_thread && g_owner == owner && !strcmp(ch, g_channel) && flags == g_flags) {
		LeaveCriticalSection(&g_lock);
		return;
	}
	if (g_thread)
		stop_locked();
	g_owner = owner;
	strcpy(g_channel, ch);
	g_flags = flags;
	if (ch[0]) {
		struct job *j = calloc(1, sizeof(*j));
		j->gen = g_generation;
		strcpy(j->channel, ch);
		j->flags = flags;
		g_thread = CreateThread(NULL, 0, thread_main, j, 0, NULL);
		if (g_thread)
			CloseHandle(g_thread);
		g_thread = g_thread ? (HANDLE)1 : NULL;
		g_status = WF_TW_CONNECTING;
	}
	LeaveCriticalSection(&g_lock);
}

void wf_twitch_release(const void *owner)
{
	InitOnceExecuteOnce(&g_once, init_lock, NULL, NULL);
	EnterCriticalSection(&g_lock);
	if (g_owner == owner && g_thread) {
		stop_locked();
		g_channel[0] = 0;
	}
	LeaveCriticalSection(&g_lock);
}

int wf_twitch_status(void)
{
	return g_status;
}

#else
void wf_twitch_set(const void *o, const char *c, int f) { (void)o; (void)c; (void)f; }
void wf_twitch_release(const void *o) { (void)o; }
int wf_twitch_status(void) { return WF_TW_OFF; }
#endif
