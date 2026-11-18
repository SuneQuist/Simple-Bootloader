#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../tls/https_preview.h"

static void push(https_preview *p, const char *s)
{
    assert(https_preview_feed(p, (const unsigned char *)s, strlen(s)));
}

int main(void)
{
    https_preview p;
    https_preview_init(&p);
    push(&p, "HTTP/1.1 200 OK\r\nContent-Len");
    assert(!p.header_done);
    push(&p, "gth: 5\r\nServer: Tiny64\r\n\r\nHe");
    assert(p.header_done && p.status == 200 && p.has_length && p.content_length == 5);
    assert(p.body_bytes == 2 && !p.complete);
    push(&p, "llo");
    assert(p.complete && p.body_bytes == 5 && strcmp(p.preview, "Hello") == 0);

    https_preview_init(&p);
    push(&p, "HTTP/1.0 301 Moved Permanently\r\nLocation: https://example.com/a\r\n"
             "Content-Length: 0\r\n\r\n");
    assert(p.complete && p.status == 301 && strcmp(p.location,"https://example.com/a")==0);

    https_preview_init(&p);
    push(&p, "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\nContent-Length: 0\r\n\r\n"
             "5\r\nHello\r\n0\r\n\r\n");
    assert(p.header_done && p.chunked && !p.has_length && p.complete);
    assert(p.body_bytes == 5 && strcmp(p.preview,"Hello") == 0);

    https_preview_init(&p);
    unsigned char decoded[16];
    https_preview_set_body(&p, decoded, sizeof decoded);
    push(&p, "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n");
    push(&p, "5;ext=x\r\nHello\r\n7\r\n Tiny64\r\n0\r\n");
    assert(!p.complete && p.body_used == 12);
    push(&p, "X-Trace: 123\r\n\r\n");
    assert(p.complete && p.body_bytes == 12 && strcmp((char *)decoded,"Hello Tiny64") == 0);

    https_preview_init(&p);
    https_preview_set_body(&p, decoded, 5);
    assert(!https_preview_feed(&p, (const unsigned char *)
        "HTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\nHello",
        strlen("HTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\nHello")));

    https_preview_init(&p);
    assert(!https_preview_feed(&p, (const unsigned char *)
        "HTTP/1.1 200 OK\r\nContent-Encoding: gzip\r\n\r\nX",
        strlen("HTTP/1.1 200 OK\r\nContent-Encoding: gzip\r\n\r\nX")));

    https_preview_init(&p);
    assert(!https_preview_feed(&p, (const unsigned char *)"BAD\r\n\r\n", 7));
    https_preview_init(&p);
    assert(!https_preview_feed(&p, (const unsigned char *)
        "HTTP/1.1 200 OK\r\nContent-Length: 4294967296\r\n\r\n", 48));

    https_preview_init(&p);
    char giant[HTTPS_HEADER_CAP + 10];
    memset(giant, 'X', sizeof giant);
    assert(!https_preview_feed(&p, (const unsigned char *)giant, sizeof giant));
    puts("PASS: HTTPS incremental headers, redirects, chunk decoding, buffer limits and encoding checks");
    return 0;
}
