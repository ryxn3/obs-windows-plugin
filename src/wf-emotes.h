#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Emote images for chat toasts: Twitch's own emotes, 7TV and BetterTTV.
 * An emote is identified by a short tag: "T:<id>" (Twitch), "7:<id>" (7TV),
 * "B:<id>" (BetterTTV). Everything is downloaded on a background thread and
 * cached in memory; nothing here blocks the video thread. */

/* Learns the emote lists for a Twitch channel (by its numeric room id) plus
 * the global 7TV / BetterTTV lists. flags: WF_TW_7TV / WF_TW_BTTV bits (see
 * wf-twitch.h). Safe to call repeatedly with the same values. */
void wf_emotes_set_channel(const char *room_id, int flags);

/* Looks a chat word up in the 7TV / BetterTTV lists. */
bool wf_emote_find_name(const char *word, char *tag, size_t tag_sz);

/* 1 = ready (w, h, rgba valid - straight alpha RGBA, stays valid until exit),
 * 0 = still downloading (the download is started), -1 = failed. */
int wf_emote_get(const char *tag, uint32_t *w, uint32_t *h, const uint8_t **rgba);

/* Increases whenever any download finishes. */
unsigned wf_emotes_generation(void);

void wf_emotes_shutdown(void);

/* Decodes PNG/GIF/WebP bytes into straight-alpha RGBA (free with free()). */
uint8_t *wf_emote_decode(const uint8_t *data, size_t len, uint32_t *w, uint32_t *h);

#ifdef __cplusplus
}
#endif
