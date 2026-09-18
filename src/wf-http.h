#pragma once

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Minimal blocking HTTPS GET (WinHTTP on Windows). Only https:// URLs are
 * accepted. The body is returned NUL-terminated in *out (free with bfree).
 * Fails (returns false, fills err) on non-2xx, timeout, or if the body would
 * exceed max_bytes. Call from a worker thread, not the UI thread. */
bool wf_http_get(const char *url, size_t max_bytes, char **out, size_t *out_len, char *err, size_t err_sz);

#ifdef __cplusplus
}
#endif
