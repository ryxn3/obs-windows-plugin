#include <string.h>
#include <stdio.h>
#include <obs-module.h>
#include "wf-http.h"

#ifndef WF_PLUGIN_VERSION
#define WF_PLUGIN_VERSION "unknown"
#endif

static void set_err(char *err, size_t n, const char *msg)
{
	if (err && n) {
		strncpy(err, msg, n - 1);
		err[n - 1] = 0;
	}
}

#if defined(_WIN32)

#include <windows.h>
#include <winhttp.h>

bool wf_http_get(const char *url, size_t max_bytes, char **out, size_t *out_len, char *err, size_t err_sz)
{
	*out = NULL;
	if (out_len)
		*out_len = 0;
	if (!url || strncmp(url, "https://", 8) != 0) {
		set_err(err, err_sz, "Only https:// URLs are allowed.");
		return false;
	}

	wchar_t wurl[2048];
	if (!MultiByteToWideChar(CP_UTF8, 0, url, -1, wurl, 2048)) {
		set_err(err, err_sz, "URL is too long or invalid.");
		return false;
	}

	wchar_t host[256] = {0}, path[1800] = {0};
	URL_COMPONENTS uc;
	memset(&uc, 0, sizeof(uc));
	uc.dwStructSize = sizeof(uc);
	uc.lpszHostName = host;
	uc.dwHostNameLength = 255;
	uc.lpszUrlPath = path;
	uc.dwUrlPathLength = 1799;
	uc.dwExtraInfoLength = (DWORD)-1;
	if (!WinHttpCrackUrl(wurl, 0, 0, &uc)) {
		set_err(err, err_sz, "Could not parse the URL.");
		return false;
	}
	/* WinHttpCrackUrl splits the query string off; re-append it. */
	if (uc.lpszExtraInfo && uc.dwExtraInfoLength) {
		size_t pl = wcslen(path);
		if (pl + uc.dwExtraInfoLength < 1799)
			wcsncat_s(path, 1800, uc.lpszExtraInfo, uc.dwExtraInfoLength);
	}

	bool ok = false;
	HINTERNET ses = NULL, con = NULL, req = NULL;
	char *buf = NULL;
	size_t len = 0, cap = 0;

	wchar_t ua[128];
	wchar_t wver[64];
	MultiByteToWideChar(CP_UTF8, 0, WF_PLUGIN_VERSION, -1, wver, 64);
	swprintf_s(ua, 128, L"win-frame-filter/%ls", wver);

	ses = WinHttpOpen(ua, WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
	if (!ses) {
		set_err(err, err_sz, "Could not start an HTTP session.");
		goto done;
	}
	WinHttpSetTimeouts(ses, 8000, 8000, 10000, 15000);
	con = WinHttpConnect(ses, host, uc.nPort, 0);
	if (!con) {
		set_err(err, err_sz, "Could not connect to the server.");
		goto done;
	}
	req = WinHttpOpenRequest(con, L"GET", path, NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
				 WINHTTP_FLAG_SECURE);
	if (!req || !WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
	    !WinHttpReceiveResponse(req, NULL)) {
		set_err(err, err_sz, "The request failed (offline, blocked, or the server did not answer).");
		goto done;
	}

	DWORD status = 0, ssz = sizeof(status);
	WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
			    &status, &ssz, WINHTTP_NO_HEADER_INDEX);
	if (status < 200 || status >= 300) {
		char m[64];
		snprintf(m, sizeof(m), "The server answered HTTP %lu.", (unsigned long)status);
		set_err(err, err_sz, m);
		goto done;
	}

	for (;;) {
		DWORD avail = 0;
		if (!WinHttpQueryDataAvailable(req, &avail))
			break;
		if (avail == 0)
			break;
		if (len + avail > max_bytes) {
			set_err(err, err_sz, "The response is larger than the allowed size.");
			goto done;
		}
		if (len + avail + 1 > cap) {
			cap = (len + avail + 1) * 2;
			if (cap > max_bytes + 1)
				cap = max_bytes + 1;
			buf = brealloc(buf, cap);
		}
		DWORD got = 0;
		if (!WinHttpReadData(req, buf + len, avail, &got) || got == 0)
			break;
		len += got;
	}
	if (!buf) {
		buf = bzalloc(1);
	}
	buf[len] = 0;
	*out = buf;
	buf = NULL;
	if (out_len)
		*out_len = len;
	ok = true;

done:
	if (buf)
		bfree(buf);
	if (req)
		WinHttpCloseHandle(req);
	if (con)
		WinHttpCloseHandle(con);
	if (ses)
		WinHttpCloseHandle(ses);
	return ok;
}

#else

bool wf_http_get(const char *url, size_t max_bytes, char **out, size_t *out_len, char *err, size_t err_sz)
{
	(void)url;
	(void)max_bytes;
	*out = NULL;
	if (out_len)
		*out_len = 0;
	set_err(err, err_sz, "Downloads are only implemented on Windows.");
	return false;
}

#endif
