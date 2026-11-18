#ifndef TINY64_HTTPS_PREVIEW_H
#define TINY64_HTTPS_PREVIEW_H
#include <stddef.h>
#include <stdint.h>

/* Incremental HTTP/1.x response decoder used by the verified TLS client.
 * Header storage is bounded. The browser receives only decoded entity bytes,
 * never HTTP headers or chunk framing. No compressed content is supported. */
#define HTTPS_HEADER_CAP 4096u
#define HTTPS_PREVIEW_CAP 240u
#define HTTPS_BODY_LIMIT (1024u * 1024u)

typedef struct {
    char headers[HTTPS_HEADER_CAP];
    size_t headers_used;
    char status_line[96];
    char location[192];
    char preview[HTTPS_PREVIEW_CAP + 1];
    size_t preview_used;
    unsigned char *body;
    size_t body_cap, body_used;
    uint32_t body_bytes;
    uint32_t content_length;
    unsigned status, header_done, has_length, chunked, complete, content_encoded;
    /* Chunk decoder: 0=size, 1=size CR, 2=data, 3=data CR,
     * 4=data LF, 5=trailer, 6=trailer LF, 7=extension. */
    unsigned chunk_state, chunk_digits, trailer_len;
    uint32_t chunk_size, chunk_remaining;
} https_preview;

void https_preview_init(https_preview *p);
/* Optional caller-owned body buffer. Reserve one byte for terminating NUL. */
void https_preview_set_body(https_preview *p, unsigned char *out, size_t capacity);
/* Returns 1 on success, 0 on malformed, encoded, or oversized responses. */
int https_preview_feed(https_preview *p, const unsigned char *data, size_t len);
/* Call only after an authenticated TLS close_notify. */
int https_preview_close(https_preview *p);
#endif
