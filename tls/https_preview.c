/* Incremental, bounded HTTP/1.x parser for verified HTTPS application data. */
#include "https_preview.h"

static int lower(int ch) { return ch >= 'A' && ch <= 'Z' ? ch + ('a' - 'A') : ch; }

static int header_is(const char *line, size_t len, const char *name)
{
    size_t i = 0;
    while (name[i]) {
        if (i >= len || lower((unsigned char)line[i]) != name[i]) return 0;
        i++;
    }
    return i < len && line[i] == ':';
}

static int header_has_token(const char *line, size_t len, const char *token)
{
    for (size_t i = 0; i < len; i++) {
        size_t j = 0;
        while (token[j] && i + j < len &&
               lower((unsigned char)line[i + j]) == token[j]) j++;
        if (!token[j]) return 1;
    }
    return 0;
}

static int parse_headers(https_preview *p)
{
    const char *h = p->headers;
    size_t end = p->headers_used;
    size_t first = 0;
    while (first + 1 < end && !(h[first] == '\r' && h[first + 1] == '\n')) first++;
    if (first < 12 || first >= sizeof p->status_line || first + 1 >= end ||
        h[0] != 'H' || h[1] != 'T' || h[2] != 'T' || h[3] != 'P' ||
        h[4] != '/' || h[5] != '1' || h[6] != '.' ||
        (h[7] != '0' && h[7] != '1') ||
        h[8] != ' ' || h[9] < '1' || h[9] > '5' ||
        h[10] < '0' || h[10] > '9' || h[11] < '0' || h[11] > '9') return 0;
    for (size_t i = 0; i < first; i++) p->status_line[i] = h[i];
    p->status_line[first] = 0;
    p->status = (unsigned)(h[9] - '0') * 100u +
                (unsigned)(h[10] - '0') * 10u + (unsigned)(h[11] - '0');

    size_t pos = first + 2;
    while (pos + 1 < end) {
        size_t stop = pos;
        while (stop + 1 < end && !(h[stop] == '\r' && h[stop + 1] == '\n')) stop++;
        if (stop + 1 >= end) return 0;
        if (stop == pos) break;
        size_t len = stop - pos;
        if (header_is(h + pos, len, "content-length")) {
            size_t j = pos + 15;
            while (j < stop && (h[j] == ' ' || h[j] == '\t')) j++;
            if (j == stop) return 0;
            uint32_t value = 0;
            unsigned digits = 0;
            while (j < stop && h[j] >= '0' && h[j] <= '9') {
                unsigned digit = (unsigned)(h[j++] - '0');
                if (value > (UINT32_MAX - digit) / 10u) return 0;
                value = value * 10u + digit;
                digits++;
            }
            while (j < stop && (h[j] == ' ' || h[j] == '\t')) j++;
            if (!digits || j != stop || (p->has_length && value != p->content_length)) return 0;
            p->has_length = 1;
            p->content_length = value;
        } else if (header_is(h + pos, len, "transfer-encoding")) {
            /* Only a single `chunked` transfer coding is implemented. */
            size_t j = pos + 18;
            while (j < stop && (h[j] == ' ' || h[j] == '\t')) j++;
            size_t tail = stop;
            while (tail > j && (h[tail-1] == ' ' || h[tail-1] == '\t')) tail--;
            if (tail - j != 7 || !header_has_token(h + j, tail - j, "chunked")) return 0;
            p->chunked = 1;
        } else if (header_is(h + pos, len, "content-encoding")) {
            size_t j = pos + 17;
            while (j < stop && (h[j] == ' ' || h[j] == '\t')) j++;
            if (stop - j != 8 || !header_has_token(h + j, stop - j, "identity"))
                p->content_encoded = 1;
        } else if (header_is(h + pos, len, "location")) {
            size_t j = pos + 9;
            while (j < stop && (h[j] == ' ' || h[j] == '\t')) j++;
            size_t n = 0;
            while (j < stop && n + 1 < sizeof p->location) p->location[n++] = h[j++];
            if (j != stop) return 0;
            p->location[n] = 0;
        }
        pos = stop + 2;
    }
    if (p->content_encoded) return 0;
    if (p->chunked) p->has_length = 0;
    if (p->body && p->has_length && p->content_length >= p->body_cap) return 0;
    if (p->status == 204 || p->status == 304 || (p->status >= 100 && p->status < 200))
        p->complete = 1;
    else if (p->has_length && p->content_length == 0)
        p->complete = 1;
    return 1;
}

void https_preview_init(https_preview *p)
{
    unsigned char *dst = (unsigned char *)p;
    for (size_t i = 0; i < sizeof *p; i++) dst[i] = 0;
}

void https_preview_set_body(https_preview *p, unsigned char *out, size_t capacity)
{
    p->body = out;
    p->body_cap = capacity;
    p->body_used = 0;
    if (out && capacity) out[0] = 0;
}

static int body_byte(https_preview *p, unsigned char c)
{
    if (p->body_bytes >= HTTPS_BODY_LIMIT) return 0;
    if (p->body) {
        if (p->body_used + 1 >= p->body_cap) return 0;
        p->body[p->body_used++] = c;
        p->body[p->body_used] = 0;
    }
    p->body_bytes++;
    if (p->preview_used < HTTPS_PREVIEW_CAP) {
        p->preview[p->preview_used++] = (c >= 32 && c <= 126) ? (char)c : ' ';
        p->preview[p->preview_used] = 0;
    }
    return 1;
}

static int chunk_byte(https_preview *p, unsigned char c)
{
    switch (p->chunk_state) {
    case 0: /* Hexadecimal chunk size */
        if ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')) {
            unsigned digit = (c <= '9') ? c - '0' : (lower(c) - 'a' + 10);
            if (p->chunk_digits >= 8 || p->chunk_size > (UINT32_MAX - digit) / 16u) return 0;
            p->chunk_size = p->chunk_size * 16u + digit;
            p->chunk_digits++;
        } else if (c == ';' && p->chunk_digits) p->chunk_state = 7;
        else if (c == '\r' && p->chunk_digits) p->chunk_state = 1;
        else return 0;
        return 1;
    case 7: /* Ignore bounded chunk extensions. */
        if (c == '\r') { p->chunk_state = 1; return 1; }
        if (c < 32 || c > 126 || ++p->trailer_len > 256) return 0;
        return 1;
    case 1:
        if (c != '\n') return 0;
        p->chunk_remaining = p->chunk_size;
        p->chunk_state = p->chunk_remaining ? 2 : 5;
        p->trailer_len = 0;
        return 1;
    case 2:
        if (!body_byte(p, c)) return 0;
        if (--p->chunk_remaining == 0) p->chunk_state = 3;
        return 1;
    case 3:
        if (c != '\r') return 0;
        p->chunk_state = 4;
        return 1;
    case 4:
        if (c != '\n') return 0;
        p->chunk_state = 0;
        p->chunk_digits = 0;
        p->chunk_size = 0;
        return 1;
    case 5: /* Trailer line; an empty line terminates the message. */
        if (c == '\r') { p->chunk_state = 6; return 1; }
        if ((c < 32 && c != '\t') || c > 126 || ++p->trailer_len > 1024) return 0;
        return 1;
    case 6:
        if (c != '\n') return 0;
        if (!p->trailer_len) p->complete = 1;
        else { p->trailer_len = 0; p->chunk_state = 5; }
        return 1;
    }
    return 0;
}

int https_preview_feed(https_preview *p, const unsigned char *data, size_t len)
{
    if (!p || (!data && len)) return 0;
    for (size_t i = 0; i < len; i++) {
        unsigned char c = data[i];
        if (!p->header_done) {
            if (p->headers_used + 1 >= sizeof p->headers) return 0;
            p->headers[p->headers_used++] = (char)c;
            size_t n = p->headers_used;
            if (n >= 4 && p->headers[n - 4] == '\r' && p->headers[n - 3] == '\n' &&
                p->headers[n - 2] == '\r' && p->headers[n - 1] == '\n') {
                p->headers[n] = 0;
                if (!parse_headers(p)) return 0;
                p->header_done = 1;
            }
        } else {
            if (p->complete) return 0;
            if (p->chunked) {
                if (!chunk_byte(p, c)) return 0;
            } else {
                if (!body_byte(p, c)) return 0;
                if (p->has_length && p->body_bytes == p->content_length)
                    p->complete = 1;
            }
        }
    }
    return 1;
}

int https_preview_close(https_preview *p)
{
    if (!p || !p->header_done) return 0;
    if (!p->chunked && !p->has_length) p->complete = 1;
    return p->complete;
}
