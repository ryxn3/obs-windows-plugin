#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Reads a Twitch channel's chat anonymously (no login, no token) and turns
 * events into toasts:  chat messages, subscriptions / gifts, cheers (bits)
 * and raids. Follows are NOT available this way - Twitch only sends them to
 * a logged-in app. */
enum {
	WF_TW_CHAT = 1,
	WF_TW_SUBS = 2,
	WF_TW_BITS = 4,
	WF_TW_RAIDS = 8,
	WF_TW_EMOTES = 16, /* Twitch's own emotes */
	WF_TW_7TV = 32,
	WF_TW_BTTV = 64,
};

enum { WF_TW_OFF = 0, WF_TW_CONNECTING = 1, WF_TW_CONNECTED = 2, WF_TW_ERROR = 3 };

/* Start (or change) the connection. An empty channel stops it. `owner`
 * identifies the caller so that only the owner can stop it again. */
void wf_twitch_set(const void *owner, const char *channel, int flags);
void wf_twitch_release(const void *owner);
int wf_twitch_status(void);

/* Turns one raw IRC line into a message (exposed for tests). Returns true and
 * fills type/title/text when the line is an event allowed by `flags`. */
struct wf_msg;
bool wf_twitch_parse_line(const char *line, int flags, struct wf_msg *out);

#ifdef __cplusplus
}
#endif
