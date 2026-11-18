#ifndef HTTP_H
#define HTTP_H
#include <stddef.h>
#include <stdint.h>

/* Receive an HTTP/1.0 response body into caller-owned memory.
 * Returns 1 for HTTP 200, 0 on error. Does not support TLS or chunked bodies.
 * out_length is the number of body bytes, excluding the final NUL.
 */
int http_get_body(uint32_t server_ip, uint16_t port, const char *host,
                  const char *path, char *out, size_t capacity,
                  size_t *out_length);
/* Legacy terminal-printing HTTP command. */
int http_get(uint32_t server_ip, uint16_t port, const char *host, const char *path);
/* Incremental, single-flight HTTP/1.0 transport for the GUI.
 * poll: 0=in progress, 1=HTTP 200 complete, -1=failed.
 * The caller owns the output buffer until completion or cancellation.
 */
int http_async_start(const char *hostname, uint16_t port, const char *path,
                     char *out, size_t capacity);
int http_async_poll(void);
void http_async_cancel(void);
size_t http_async_length(void);
const char *http_async_error(void);
/* Status and redirect target remain valid until the next request starts. */
int http_async_status(void);
const char *http_async_location(void);
#endif
