#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* A message for the toast source or the paperclip assistant.
 * type is a short word that picks the icon: follow, sub, donation, chat, info. */
struct wf_msg {
	char type[16];
	char title[96];
	char text[400];
	/* Chat emotes: text may contain U+E000 + k characters (UTF-8 EE 80 80+k)
	 * standing for emote[k], a tag such as "T:25", "7:<id>" or "B:<id>". */
	int nemote;
	char emote[12][40];
};

enum { WF_TARGET_TOAST = 0, WF_TARGET_ASSISTANT = 1, WF_TARGET_COUNT };

/* Thread-safe FIFO queue per target (64 messages; oldest are dropped). */
void wf_notify_push(int target, const struct wf_msg *m);
bool wf_notify_pop(int target, struct wf_msg *out);

/* Parses one line: JSON {"type","title","text"}, or "type|title|text",
 * "title|text", or just "text". Returns false for an empty line. */
bool wf_msg_parse_line(const char *line, struct wf_msg *m);

/* Watches a text file: every line appended after the watcher is created
 * becomes a message for `target`. Call wf_filetail_poll every few hundred ms. */
struct wf_filetail {
	char path[512];
	long long pos;
	bool primed;
};
void wf_filetail_set(struct wf_filetail *t, const char *path);
void wf_filetail_poll(struct wf_filetail *t, int target);

/* Optional local web address (127.0.0.1 only, secret token required):
 *   POST or GET  http://127.0.0.1:<port>/toast?token=...   JSON body or title=&text=&type=
 *   POST or GET  http://127.0.0.1:<port>/assistant?token=...
 * It runs only while at least one source has called acquire() AND the
 * "enable web address" preference is on. */
void wf_notify_http_acquire(void);
void wf_notify_http_release(void);
void wf_notify_http_apply_prefs(void); /* call after the preferences changed */
bool wf_notify_http_running(void);

#ifdef __cplusplus
}
#endif
