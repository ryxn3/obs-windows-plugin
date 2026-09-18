/*
 * Headless test of the plugin's non-UI logic against real libobs:
 * add-filter, copy-style, framed-source enumeration, HTTPS fetch, preset
 * download error handling and update-check parsing.
 */
#include <winsock2.h>
#include <objbase.h>
#include <ws2tcpip.h>
#include <obs.h>
#include <util/base.h>
#include <util/platform.h>
#include <stdio.h>
#include <string.h>
#include "../../src/wf-actions.h"
#include "../../src/wf-prefs.h"
#include "../../src/wf-http.h"
#include "../../src/wf-notify.h"
#include "../../src/wf-twitch.h"
#include "../../src/wf-emotes.h"
#include "../../src/win-frame-filter.h"
#include "../../src/win-frame-styles.h"

static obs_module_t *g_mod;
obs_module_t *obs_current_module(void)
{
	return g_mod;
}

static int g_fail = 0;
#define CHECK(cond, what)                                                            \
	do {                                                                         \
		if (cond)                                                            \
			printf("  ok    %s\n", what);                                \
		else {                                                               \
			printf("  FAIL  %s\n", what);                                \
			g_fail++;                                                    \
		}                                                                    \
	} while (0)

static void log_handler(int lvl, const char *msg, va_list args, void *p)
{
	(void)p;
	if (lvl > LOG_WARNING)
		return;
	vprintf(msg, args);
	printf("\n");
}

static int g_seen;
static char g_style_seen[64];
static void count_cb(obs_source_t *owner, obs_source_t *flt, void *p)
{
	(void)p;
	g_seen++;
	if (!strcmp(obs_source_get_name(owner), "camB")) {
		obs_data_t *s = obs_source_get_settings(flt);
		strncpy(g_style_seen, obs_data_get_string(s, S_STYLE), sizeof(g_style_seen) - 1);
		obs_data_release(s);
	}
}
static void set_style_cb(obs_source_t *owner, obs_source_t *flt, void *p)
{
	(void)p;
	if (!strcmp(obs_source_get_name(owner), "camA")) {
		obs_data_t *s = obs_source_get_settings(flt);
		obs_data_set_string(s, S_STYLE, "win98");
		obs_data_set_double(s, S_FRAME_THICKNESS, 7.0);
		obs_source_update(flt, s);
		obs_data_release(s);
	}
}

/* raw HTTP client for the localhost notification server */
static int http_raw(int port, const char *req, char *out, size_t n)
{
	SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	struct sockaddr_in a;
	memset(&a, 0, sizeof(a));
	a.sin_family = AF_INET;
	a.sin_port = htons((u_short)port);
	inet_pton(AF_INET, "127.0.0.1", &a.sin_addr);
	if (connect(s, (struct sockaddr *)&a, sizeof(a)) != 0) {
		closesocket(s);
		return -1;
	}
	send(s, req, (int)strlen(req), 0);
	size_t got = 0;
	int r;
	while (got < n - 1 && (r = recv(s, out + got, (int)(n - 1 - got), 0)) > 0)
		got += (size_t)r;
	out[got] = 0;
	closesocket(s);
	return (int)got;
}

int main(int argc, char **argv)
{
	setvbuf(stdout, NULL, _IONBF, 0);
	if (argc < 3) {
		printf("usage: logic-test plugin.dll plugin-data-dir\n");
		return 1;
	}
	base_set_log_handler(log_handler, NULL);
	os_mkdirs("E:/obs windows plugin/build/rt-config2");
	obs_startup("en-US", "E:/obs windows plugin/build/rt-config2", NULL);
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
	ovi.gpu_conversion = true;
	ovi.scale_type = OBS_SCALE_BICUBIC;
	if (obs_reset_video(&ovi) != OBS_VIDEO_SUCCESS)
		return 3;

	obs_module_t *m1 = NULL, *m2 = NULL;
	if (obs_open_module(&m2, "C:/Program Files/obs-studio/obs-plugins/64bit/image-source.dll",
			    "C:/Program Files/obs-studio/data/obs-plugins/image-source") == MODULE_SUCCESS)
		obs_init_module(m2);
	if (obs_open_module(&m1, argv[1], argv[2]) == MODULE_SUCCESS)
		obs_init_module(m1);
	else
		return 4;
	g_mod = m1;

	obs_data_t *is = obs_data_create();
	obs_data_set_string(is, "file", "E:/obs windows plugin/build/test-cam.png");
	obs_source_t *a = obs_source_create("image_source", "camA", is, NULL);
	obs_source_t *b = obs_source_create("image_source", "camB", is, NULL);
	obs_data_release(is);

	char msg[300];
	printf("add / enumerate\n");
	CHECK(wf_add_filter_to_source(a, msg, sizeof(msg)), "adds a frame to camA");
	CHECK(!wf_add_filter_to_source(a, msg, sizeof(msg)), "refuses a second frame on camA");
	CHECK(strstr(msg, "already") != NULL, "explains why");
	g_seen = 0;
	wf_enum_framed(count_cb, NULL);
	CHECK(g_seen == 1, "enumeration sees exactly one framed source");

	printf("copy style\n");
	wf_enum_framed(set_style_cb, NULL);
	CHECK(wf_copy_style("camA", "camB", msg, sizeof(msg)), "copies A -> B (B had no frame)");
	g_seen = 0;
	g_style_seen[0] = 0;
	wf_enum_framed(count_cb, NULL);
	CHECK(g_seen == 2, "B now has a frame");
	CHECK(strcmp(g_style_seen, "win98") == 0, "B's frame carries A's style (win98)");
	CHECK(!wf_copy_style("camA", "camA", msg, sizeof(msg)), "refuses copying onto itself");
	CHECK(!wf_copy_style("camA", "nope", msg, sizeof(msg)), "refuses a missing destination");

	printf("https + downloads\n");
	char *body = NULL;
	size_t len = 0;
	char err[200] = {0};
	CHECK(!wf_http_get("http://example.com/", 1000, &body, &len, err, sizeof(err)), "rejects plain http");
	CHECK(wf_http_get("https://api.github.com/repos/obsproject/obs-studio", 1024 * 1024, &body, &len, err,
			  sizeof(err)) && body && len > 100,
	      "fetches JSON over HTTPS");
	if (!body)
		printf("        (%s)\n", err);
	else
		bfree(body);
	CHECK(!wf_http_get("https://api.github.com/repos/obsproject/obs-studio", 200, &body, &len, err, sizeof(err)),
	      "enforces the size limit");
	CHECK(!wf_http_get("https://api.github.com/this/does/not/exist/xyz", 1000, &body, &len, err, sizeof(err)),
	      "reports HTTP 404");
	printf("        (%s)\n", err);

	int r = wf_download_presets("https://api.github.com/repos/obsproject/obs-studio", msg, sizeof(msg));
	CHECK(r < 0, "a JSON file that is not a preset imports nothing");
	printf("        (%s)\n", msg);
	r = wf_download_presets("http://insecure.example/x.json", msg, sizeof(msg));
	CHECK(r < 0, "download refuses http://");
	r = wf_download_presets("", msg, sizeof(msg));
	CHECK(r < 0, "download refuses an empty URL");

	printf("alt+tab transition\n");
	{
		obs_data_t *ts = obs_data_create();
		obs_data_set_int(ts, "at_duration_ms", 2500);
		obs_data_set_int(ts, "at_version", 4);
		obs_source_t *tr = obs_source_create_private("win_alttab_transition", "at", ts);
		obs_data_release(ts);
		CHECK(tr != NULL, "the transition can be created");
		if (tr) {
			CHECK(obs_transition_fixed(tr), "its duration is locked to the plugin time setting");
			obs_properties_t *tp = obs_source_properties(tr);
			const char *tk[] = {"at_version", "at_duration_ms", "at_hold", "at_count", "at_scale", "at_dark", "at_accent", "at_titles"};
			int miss = 0;
			for (size_t i = 0; i < sizeof(tk) / sizeof(tk[0]); i++)
				if (!obs_properties_get(tp, tk[i]))
					miss++;
			CHECK(miss == 0, "version, time, hold, count, size and colour controls exist");
			obs_properties_destroy(tp);
			obs_source_release(tr);
		}
		CHECK(obs_source_get_display_name("win_alttab_transition") != NULL, "it has a display name for the transitions list");
	}

	printf("new sources (toast / paperclip / update screen)\n");
	{
		const char *ids[] = {"win_toast_source", "win_paperclip_assistant", "win_update_screen"};
		const char *keys[] = {"t_style", "a_theme", "u_version"};
		for (int i = 0; i < 3; i++) {
			obs_source_t *src = obs_source_create_private(ids[i], "s", NULL);
			CHECK(src != NULL, "the source can be created");
			if (src) {
				obs_properties_t *tp = obs_source_properties(src);
				CHECK(obs_properties_get(tp, keys[i]) != NULL, "its main control exists");
				CHECK(obs_source_get_width(src) >= 320, "it has a canvas size");
				obs_properties_destroy(tp);
				obs_source_release(src);
			}
		}
	}

	printf("notification queue, parser and file watcher\n");
	{
		struct wf_msg m;
		CHECK(wf_msg_parse_line("{\"type\":\"sub\",\"title\":\"T\",\"text\":\"Body\"}", &m) && !strcmp(m.type, "sub") &&
			      !strcmp(m.title, "T") && !strcmp(m.text, "Body"),
		      "parses JSON");
		CHECK(wf_msg_parse_line("follow|Hi|Someone followed", &m) && !strcmp(m.type, "follow") &&
			      !strcmp(m.text, "Someone followed"),
		      "parses type|title|text");
		CHECK(wf_msg_parse_line("Just text", &m) && !strcmp(m.text, "Just text"), "parses plain text");
		CHECK(!wf_msg_parse_line("   ", &m), "ignores blank lines");
		struct wf_msg a = {"info", "one", "1"}, b = {"info", "two", "2"};
		wf_notify_push(WF_TARGET_TOAST, &a);
		wf_notify_push(WF_TARGET_TOAST, &b);
		CHECK(!wf_notify_pop(WF_TARGET_ASSISTANT, &m), "queues are separate per target");
		CHECK(wf_notify_pop(WF_TARGET_TOAST, &m) && !strcmp(m.title, "one"), "FIFO order (first)");
		CHECK(wf_notify_pop(WF_TARGET_TOAST, &m) && !strcmp(m.title, "two"), "FIFO order (second)");
		CHECK(!wf_notify_pop(WF_TARGET_TOAST, &m), "empty after draining");
		for (int i = 0; i < 100; i++)
			wf_notify_push(WF_TARGET_TOAST, &a);
		int cnt = 0;
		while (wf_notify_pop(WF_TARGET_TOAST, &m))
			cnt++;
		CHECK(cnt == 64, "queue holds at most 64 messages");

		os_mkdirs("E:/obs windows plugin/build/rt-config");
		const char *path = "E:/obs windows plugin/build/rt-config/tail_test.txt";
		FILE *f = fopen(path, "wb");
		fputs("old line that must be ignored\n", f);
		fclose(f);
		struct wf_filetail ft;
		memset(&ft, 0, sizeof(ft));
		wf_filetail_set(&ft, path);
		wf_filetail_poll(&ft, WF_TARGET_TOAST);
		CHECK(!wf_notify_pop(WF_TARGET_TOAST, &m), "existing file content is ignored");
		f = fopen(path, "ab");
		fputs("chat|Chat|hello there\nhalf a li", f);
		fclose(f);
		wf_filetail_poll(&ft, WF_TARGET_TOAST);
		CHECK(wf_notify_pop(WF_TARGET_TOAST, &m) && !strcmp(m.text, "hello there"), "new line becomes a message");
		CHECK(!wf_notify_pop(WF_TARGET_TOAST, &m), "an unfinished line waits");
		f = fopen(path, "ab");
		fputs("ne\n", f);
		fclose(f);
		wf_filetail_poll(&ft, WF_TARGET_TOAST);
		CHECK(wf_notify_pop(WF_TARGET_TOAST, &m) && !strcmp(m.text, "half a line"), "the finished line arrives");
	}

	printf("localhost notification server\n");
	{
		WSADATA wsa;
		WSAStartup(MAKEWORD(2, 2), &wsa);
		struct wf_prefs *pr = wf_prefs_get();
		pr->http_enabled = false;
		wf_notify_http_apply_prefs();
		CHECK(!wf_notify_http_running(), "off by default: no server");
		pr->http_enabled = true;
		pr->http_port = 17999;
		strcpy(pr->http_token, "testtoken123");
		wf_notify_http_acquire();
		os_sleep_ms(400);
		CHECK(wf_notify_http_running(), "starts when enabled and a source exists");
		char resp[2048];
		struct wf_msg m;
		http_raw(17999, "GET /toast?title=A&text=B HTTP/1.1\r\nHost: x\r\n\r\n", resp, sizeof(resp));
		CHECK(strstr(resp, " 401 ") != NULL, "no token -> 401");
		http_raw(17999, "GET /toast?title=A&text=B&token=wrong HTTP/1.1\r\nHost: x\r\n\r\n", resp, sizeof(resp));
		CHECK(strstr(resp, " 401 ") != NULL, "wrong token -> 401");
		CHECK(!wf_notify_pop(WF_TARGET_TOAST, &m), "nothing queued by rejected requests");
		http_raw(17999,
			 "GET /toast?title=Hello&text=World+wide&type=follow&token=testtoken123 HTTP/1.1\r\nHost: x\r\n\r\n",
			 resp, sizeof(resp));
		CHECK(strstr(resp, " 200 ") != NULL, "right token in the query -> 200");
		CHECK(wf_notify_pop(WF_TARGET_TOAST, &m) && !strcmp(m.title, "Hello") && !strcmp(m.text, "World wide") &&
			      !strcmp(m.type, "follow"),
		      "GET fields reach the toast queue");
		const char *body = "{\"title\":\"J\",\"text\":\"from json\"}";
		char req[512];
		snprintf(req, sizeof(req),
			 "POST /assistant HTTP/1.1\r\nHost: x\r\nX-WF-Token: testtoken123\r\nContent-Length: %d\r\n\r\n%s",
			 (int)strlen(body), body);
		http_raw(17999, req, resp, sizeof(resp));
		CHECK(strstr(resp, " 200 ") != NULL, "POST with the token header -> 200");
		CHECK(wf_notify_pop(WF_TARGET_ASSISTANT, &m) && !strcmp(m.text, "from json"),
		      "JSON body reaches the assistant queue");
		http_raw(17999, "GET /nothing?token=testtoken123 HTTP/1.1\r\nHost: x\r\n\r\n", resp, sizeof(resp));
		CHECK(strstr(resp, " 404 ") != NULL, "unknown path -> 404");
		wf_notify_http_release();
		os_sleep_ms(300);
		CHECK(!wf_notify_http_running(), "stops when the last source is gone");
		CHECK(http_raw(17999, "GET / HTTP/1.1\r\n\r\n", resp, sizeof(resp)) < 0, "port is closed afterwards");
		pr->http_enabled = false;
		WSACleanup();
	}

	printf("twitch chat parser\n");
	{
		struct wf_msg m;
		int all = WF_TW_CHAT | WF_TW_SUBS | WF_TW_BITS | WF_TW_RAIDS;
		const char *chat = "@badges=;display-name=Alex;bits=0 :alex!alex@alex.tmi.twitch.tv PRIVMSG #chan :hello world";
		CHECK(wf_twitch_parse_line(chat, all, &m) && !strcmp(m.type, "chat") && !strcmp(m.title, "Alex") &&
			      !strcmp(m.text, "hello world"),
		      "chat message");
		CHECK(!wf_twitch_parse_line(chat, WF_TW_SUBS, &m), "chat is skipped when switched off");
		const char *bits = "@bits=100;display-name=Sam :sam!sam@sam.tmi.twitch.tv PRIVMSG #chan :Cheer100 nice";
		CHECK(wf_twitch_parse_line(bits, all, &m) && !strcmp(m.type, "donation") && strstr(m.title, "100 bits"), "cheer");
		const char *sub = "@msg-id=resub;display-name=Riley;system-msg=Riley\\ssubscribed\\sfor\\s3\\smonths! :tmi.twitch.tv USERNOTICE #chan :love it";
		CHECK(wf_twitch_parse_line(sub, all, &m) && !strcmp(m.type, "sub") &&
			      strstr(m.text, "Riley subscribed for 3 months!") && strstr(m.text, "love it"),
		      "resub with unescaped system message");
		const char *raid = "@msg-id=raid;display-name=Pat;msg-param-viewerCount=42 :tmi.twitch.tv USERNOTICE #chan";
		CHECK(wf_twitch_parse_line(raid, all, &m) && strstr(m.text, "42"), "raid");
		CHECK(!wf_twitch_parse_line("PING :tmi.twitch.tv", all, &m), "ignores PING");
		CHECK(!wf_twitch_parse_line(":tmi.twitch.tv 001 justinfan1 :Welcome", all, &m), "ignores server lines");
	}

	printf("emotes\n");
	{
		struct wf_msg m;
		int all = WF_TW_CHAT | WF_TW_EMOTES | WF_TW_7TV | WF_TW_BTTV;
		const char *nat = "@display-name=Alex;emotes=25:0-4,9-13 :a!a@a.tmi.twitch.tv PRIVMSG #c :Kappa hi Kappa";
		CHECK(wf_twitch_parse_line(nat, all, &m), "chat with Twitch emotes parses");
		CHECK(m.nemote == 2 && !strcmp(m.emote[0], "T:25") && !strcmp(m.emote[1], "T:25"), "both Kappa emotes found by position");
		CHECK((unsigned char)m.text[0] == 0xEE && (unsigned char)m.text[2] == 0x80 && strstr(m.text, " hi ") != NULL &&
			      (unsigned char)m.text[7] == 0xEE && (unsigned char)m.text[9] == 0x81,
		      "emote words become placeholder characters");
		CHECK(wf_twitch_parse_line(nat, WF_TW_CHAT, &m) && m.nemote == 0 && !strcmp(m.text, "Kappa hi Kappa"), "emotes stay as text when switched off");
		const char *act = "@display-name=Alex;emotes=25:8-12 :a!a@a.tmi.twitch.tv PRIVMSG #c :\x01" "ACTION Kappa\x01";
		CHECK(wf_twitch_parse_line(act, all, &m) && m.nemote == 1, "positions still line up after /me");
		const char *utf = "@display-name=Alex;emotes=25:3-7 :a!a@a.tmi.twitch.tv PRIVMSG #c :\xC3\xA9\xC3\xA9 Kappa";
		CHECK(wf_twitch_parse_line(utf, all, &m) && m.nemote == 1, "positions count characters, not bytes");

		static const unsigned char png[] = {137, 80, 78, 71, 13, 10, 26, 10, 0, 0, 0, 13, 73, 72, 68, 82, 0, 0, 0, 1, 0, 0, 0, 1, 8, 6, 0, 0, 0, 31, 21, 196, 137, 0, 0, 0, 13, 73, 68, 65, 84, 120, 156, 99, 248, 207, 192, 208, 0, 0, 4, 129, 1, 128, 44, 85, 206, 176, 0, 0, 0, 0, 73, 69, 78, 68, 174, 66, 96, 130};
		CoInitializeEx(NULL, COINIT_MULTITHREADED);
		uint32_t w = 0, h = 0;
		uint8_t *px = wf_emote_decode(png, sizeof(png), &w, &h);
		CHECK(px && w == 1 && h == 1, "decodes a PNG");
		CHECK(px && px[0] == 255 && px[1] == 0 && px[2] == 0 && px[3] >= 127 && px[3] <= 129, "straight (not premultiplied) RGBA");
		free(px);
		CHECK(wf_emote_decode((const uint8_t *)"not an image", 12, &w, &h) == NULL, "garbage is rejected");
	}

	printf("update check\n");
	strcpy(wf_prefs_get()->update_url, "");
	CHECK(wf_check_update(msg, sizeof(msg), err, sizeof(err)) < 0, "unconfigured URL is reported");
	strcpy(wf_prefs_get()->update_url, "https://api.github.com/repos/obsproject/obs-studio");
	CHECK(wf_check_update(msg, sizeof(msg), err, sizeof(err)) < 0, "JSON without a version is rejected");
	printf("        (%s)\n", msg);

	printf("\n%s (%d failure%s)\n", g_fail ? "FAILED" : "ALL PASSED", g_fail, g_fail == 1 ? "" : "s");
	return g_fail ? 1 : 0;
}
