/* A bounded zlib decoder for lazy-load entries: see zrd_inflate.c. */
#ifndef ZRD_INFLATE_H
#define ZRD_INFLATE_H

#include <stddef.h>
#include <stdint.h>

typedef enum {
    ZRD_INFLATE_OK = 0,
    ZRD_INFLATE_BAD_HEADER,   /* not RFC 1950, or a preset dictionary */
    ZRD_INFLATE_BAD_DATA,     /* not valid RFC 1951 */
    ZRD_INFLATE_TRUNCATED,    /* the input ends inside the stream */
    ZRD_INFLATE_TOO_LONG,     /* more output than `outlen` */
    ZRD_INFLATE_SHORT,        /* less output than `outlen` */
    ZRD_INFLATE_CHECK         /* the Adler-32 does not match */
} zrd_inflate_status;

/* Decompresses the zlib stream `in` into exactly `outlen` bytes at `out`;
 * `produced` is how many were written. Anything else is an error. */
zrd_inflate_status zrd_inflate_zlib(const uint8_t *in, size_t inlen,
                                    uint8_t *out, size_t outlen, size_t *produced);
const char *zrd_inflate_message(zrd_inflate_status st);

/* R writes xz lazy-load entries as raw LZMA2 (connections.c, R_compress3:
 * lzma_raw_encoder, preset 6), which no R function decodes alone. This
 * wraps such data in an .xz container (no integrity check) that
 * memDecompress(type = "xz") reads, its index giving the sizes, so a
 * truncated entry fails xz's own checks. `out` must hold
 * zrd_xz_container_size(rawlen) bytes; returns the bytes written. */
size_t zrd_xz_container_size(size_t rawlen);
size_t zrd_xz_container(const uint8_t *raw, size_t rawlen, uint64_t uncompressed,
                        uint8_t *out);

#endif /* ZRD_INFLATE_H */
