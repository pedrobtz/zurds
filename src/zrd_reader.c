/* See zrd_reader.h. No R headers: this file is part of the standalone
 * build. */
#include <string.h>

#include "zrd_reader.h"

void zrd_reader_init_mem(zrd_reader *r, const uint8_t *data, size_t len)
{
    memset(r, 0, sizeof *r);
    r->data = data;
    r->len = len;
    r->ended = 1;
}

void zrd_reader_init_fill(zrd_reader *r, zrd_fill_fn fill, void *ctx,
                          uint8_t *block)
{
    memset(r, 0, sizeof *r);
    r->fill = fill;
    r->ctx = ctx;
    r->block = block;
    r->data = block;
}

/* Moves the unread tail to the front of the block and fills the rest.
 * Returns 0 once the input has ended and nothing more arrived. */
static int refill(zrd_reader *r)
{
    if (r->ended || r->fill == NULL) return 0;
    size_t tail = r->len - r->pos;
    if (tail && r->pos) memmove(r->block, r->block + r->pos, tail);
    r->consumed += r->pos;
    r->pos = 0;
    r->len = tail;
    size_t got = r->fill(r->ctx, r->block + r->len, ZRD_BLOCK_SIZE - r->len);
    if (got == 0) r->ended = 1;
    r->len += got;
    return got > 0;
}

size_t zrd_reader_read(zrd_reader *r, void *dst, size_t n)
{
    size_t done = 0;
    uint8_t *out = dst;
    while (done < n) {
        if (r->pos == r->len && !refill(r)) break;
        size_t take = r->len - r->pos;
        if (take > n - done) take = n - done;
        if (out) memcpy(out + done, r->data + r->pos, take);
        r->pos += take;
        done += take;
    }
    return done;
}

size_t zrd_reader_peek(zrd_reader *r, void *dst, size_t n)
{
    while (r->len - r->pos < n && refill(r)) {}
    size_t have = r->len - r->pos;
    if (have > n) have = n;
    memcpy(dst, r->data + r->pos, have);
    return have;
}

uint64_t zrd_reader_offset(const zrd_reader *r)
{
    return r->consumed + r->pos;
}
