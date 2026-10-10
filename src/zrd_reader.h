/* The input side of the walk, free of R (roadmap rules: the walk compiles
 * with -DZRD_STANDALONE). A reader pulls decompressed bytes from a fill
 * callback into a reused block and hands them out in order. The callback
 * is the only thing that knows where bytes come from: an R connection or a
 * raw vector in src/zrd_source.c, a file or a fuzz input elsewhere. */
#ifndef ZRD_READER_H
#define ZRD_READER_H

#include <stddef.h>
#include <stdint.h>

/* The block size of design section 4. */
#define ZRD_BLOCK_SIZE 65536

/* Fills `buf` with up to `cap` bytes and returns how many; 0 means the
 * input has ended. It may not fail: an R callback raises instead. */
typedef size_t (*zrd_fill_fn)(void *ctx, uint8_t *buf, size_t cap);

typedef struct zrd_reader {
    zrd_fill_fn fill;
    void *ctx;
    /* Either a borrowed contiguous input (fill == NULL), or an owned
     * block refilled through `fill`. */
    const uint8_t *data;
    uint8_t *block;
    size_t len;          /* bytes valid in data */
    size_t pos;          /* next byte in data */
    uint64_t consumed;   /* bytes handed out before data[0] */
    int ended;           /* fill returned 0 */
} zrd_reader;

/* A reader over memory the caller keeps alive. */
void zrd_reader_init_mem(zrd_reader *r, const uint8_t *data, size_t len);
/* A reader over a callback; `block` is ZRD_BLOCK_SIZE bytes owned by the
 * caller, so the caller decides how it is freed on an R error. */
void zrd_reader_init_fill(zrd_reader *r, zrd_fill_fn fill, void *ctx,
                          uint8_t *block);

/* Copies the next `n` bytes into `dst` and returns how many there were:
 * fewer than `n` only at the end of the input. `dst` may be NULL to skip. */
size_t zrd_reader_read(zrd_reader *r, void *dst, size_t n);
/* Copies up to `n` bytes without consuming them; `n` is at most
 * ZRD_BLOCK_SIZE. Returns how many there were. */
size_t zrd_reader_peek(zrd_reader *r, void *dst, size_t n);
/* The offset of the next byte in the decompressed stream. */
uint64_t zrd_reader_offset(const zrd_reader *r);

#endif /* ZRD_READER_H */
