/* A bounded zlib decoder (RFC 1950 and RFC 1951) for lazy-load entries
 * (design section 7; roadmap Stage 9). R's memDecompress() grows its buffer
 * without bound on a truncated zlib stream when R is built without
 * libdeflate (R 4.6 and earlier), so a corrupt .rdb entry could exhaust
 * memory. This decoder writes into a buffer of the length the entry
 * declares and stops with an error at the first byte that does not fit, at
 * the end of its input, or at anything RFC 1951 does not allow. It decodes
 * a symbol a bit at a time from canonical code counts, which is slow but
 * small and easy to check; lazy-load entries are small.
 *
 * No R headers: part of the standalone build and the fuzz target. */
#include <string.h>

#include "zrd_inflate.h"

#define MAXBITS 15     /* longest code */
#define MAXLCODES 286  /* literal/length codes */
#define MAXDCODES 30   /* distance codes */
#define FIXLCODES 288  /* literal/length codes in the fixed table */

typedef struct {
    const uint8_t *in;
    size_t inlen, inpos;
    uint32_t bitbuf;
    int bitcnt;
    uint8_t *out;
    size_t outlen, outpos;
    zrd_inflate_status err;
} state;

typedef struct {
    short count[MAXBITS + 1]; /* codes of each length */
    short symbol[FIXLCODES];  /* symbols, ordered by code */
} huffman;

/* Takes `need` bits, least significant first; sets err at the input's end. */
static int bits(state *s, int need)
{
    uint32_t val = s->bitbuf;
    while (s->bitcnt < need) {
        if (s->inpos >= s->inlen) {
            s->err = ZRD_INFLATE_TRUNCATED;
            return 0;
        }
        val |= (uint32_t) s->in[s->inpos++] << s->bitcnt;
        s->bitcnt += 8;
    }
    s->bitbuf = val >> need;
    s->bitcnt -= need;
    return (int) (val & ((1UL << need) - 1));
}

static int put(state *s, uint8_t b)
{
    if (s->outpos >= s->outlen) {
        s->err = ZRD_INFLATE_TOO_LONG;
        return 1;
    }
    s->out[s->outpos++] = b;
    return 0;
}

/* RFC 1951, 3.2.4: a stored block. */
static void stored(state *s)
{
    s->bitbuf = 0;
    s->bitcnt = 0;
    if (s->inlen - s->inpos < 4) {
        s->err = ZRD_INFLATE_TRUNCATED;
        return;
    }
    unsigned len = s->in[s->inpos] | (s->in[s->inpos + 1] << 8);
    unsigned nlen = s->in[s->inpos + 2] | (s->in[s->inpos + 3] << 8);
    s->inpos += 4;
    if (len != (~nlen & 0xffffu)) {
        s->err = ZRD_INFLATE_BAD_DATA;
        return;
    }
    if (s->inlen - s->inpos < len) {
        s->err = ZRD_INFLATE_TRUNCATED;
        return;
    }
    if (s->outlen - s->outpos < len) {
        s->err = ZRD_INFLATE_TOO_LONG;
        return;
    }
    memcpy(s->out + s->outpos, s->in + s->inpos, len);
    s->outpos += len;
    s->inpos += len;
}

/* Builds the canonical code from the code lengths of `n` symbols. Returns
 * non-zero for an over-subscribed set; an incomplete one is allowed, and a
 * code it leaves out is an error when it is read. */
static int construct(huffman *h, const short *length, int n)
{
    short offs[MAXBITS + 1];
    memset(h->count, 0, sizeof h->count);
    for (int sym = 0; sym < n; sym++) h->count[length[sym]]++;
    if (h->count[0] == n) return 0; /* no codes: an error if one is read */
    int left = 1;
    for (int len = 1; len <= MAXBITS; len++) {
        left <<= 1;
        left -= h->count[len];
        if (left < 0) return 1;
    }
    offs[1] = 0;
    for (int len = 1; len < MAXBITS; len++) offs[len + 1] = (short) (offs[len] + h->count[len]);
    for (int sym = 0; sym < n; sym++)
        if (length[sym] != 0) h->symbol[offs[length[sym]]++] = (short) sym;
    return 0;
}

/* Reads one symbol of code `h`, a bit at a time (codes are stored most
 * significant bit first). Returns -1 and sets err on failure. */
static int decode(state *s, const huffman *h)
{
    int code = 0, first = 0, index = 0;
    for (int len = 1; len <= MAXBITS; len++) {
        code |= bits(s, 1);
        if (s->err) return -1;
        int count = h->count[len];
        if (code - count < first) return h->symbol[index + (code - first)];
        index += count;
        first += count;
        first <<= 1;
        code <<= 1;
    }
    s->err = ZRD_INFLATE_BAD_DATA; /* a code no symbol has */
    return -1;
}

/* RFC 1951, 3.2.5: lengths 3 to 258 and distances 1 to 32768. */
static const short lbase[29] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
                                35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
static const short lext[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2,
                               3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
static const short dbase[30] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193,
                                257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145,
                                8193, 12289, 16385, 24577};
static const short dext[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6,
                               7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

/* Decodes a block's symbols with the given codes, up to end-of-block. */
static void codes(state *s, const huffman *lencode, const huffman *distcode)
{
    for (;;) {
        int sym = decode(s, lencode);
        if (sym < 0) return;
        if (sym < 256) {
            if (put(s, (uint8_t) sym)) return;
        } else if (sym == 256) {
            return;
        } else {
            sym -= 257;
            if (sym >= 29) {
                s->err = ZRD_INFLATE_BAD_DATA;
                return;
            }
            int len = lbase[sym] + bits(s, lext[sym]);
            if (s->err) return;
            int dsym = decode(s, distcode);
            if (dsym < 0) return;
            if (dsym >= 30) {
                s->err = ZRD_INFLATE_BAD_DATA;
                return;
            }
            size_t dist = (size_t) dbase[dsym] + (size_t) bits(s, dext[dsym]);
            if (s->err) return;
            if (dist > s->outpos) {
                s->err = ZRD_INFLATE_BAD_DATA; /* before the start */
                return;
            }
            if (s->outlen - s->outpos < (size_t) len) {
                s->err = ZRD_INFLATE_TOO_LONG;
                return;
            }
            for (int i = 0; i < len; i++) {
                s->out[s->outpos] = s->out[s->outpos - dist];
                s->outpos++;
            }
        }
    }
}

/* RFC 1951, 3.2.6: the fixed codes. */
static void fixed(state *s)
{
    static huffman lencode, distcode;
    static int built = 0;
    if (!built) {
        short lengths[FIXLCODES];
        int sym = 0;
        for (; sym < 144; sym++) lengths[sym] = 8;
        for (; sym < 256; sym++) lengths[sym] = 9;
        for (; sym < 280; sym++) lengths[sym] = 7;
        for (; sym < FIXLCODES; sym++) lengths[sym] = 8;
        construct(&lencode, lengths, FIXLCODES);
        for (sym = 0; sym < MAXDCODES; sym++) lengths[sym] = 5;
        construct(&distcode, lengths, MAXDCODES);
        built = 1;
    }
    codes(s, &lencode, &distcode);
}

/* RFC 1951, 3.2.7: codes described in the block. */
static void dynamic(state *s)
{
    static const short order[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
    short lengths[MAXLCODES + MAXDCODES];
    huffman lencode, distcode;
    int nlen = bits(s, 5) + 257;
    int ndist = bits(s, 5) + 1;
    int ncode = bits(s, 4) + 4;
    if (s->err) return;
    if (nlen > MAXLCODES || ndist > MAXDCODES) {
        s->err = ZRD_INFLATE_BAD_DATA;
        return;
    }
    int index;
    for (index = 0; index < ncode; index++) {
        lengths[order[index]] = (short) bits(s, 3);
        if (s->err) return;
    }
    for (; index < 19; index++) lengths[order[index]] = 0;
    if (construct(&lencode, lengths, 19)) {
        s->err = ZRD_INFLATE_BAD_DATA;
        return;
    }
    index = 0;
    while (index < nlen + ndist) {
        int sym = decode(s, &lencode);
        if (sym < 0) return;
        if (sym < 16) {
            lengths[index++] = (short) sym;
            continue;
        }
        short len = 0;
        int rep;
        if (sym == 16) {
            if (index == 0) {
                s->err = ZRD_INFLATE_BAD_DATA; /* nothing to repeat */
                return;
            }
            len = lengths[index - 1];
            rep = 3 + bits(s, 2);
        } else if (sym == 17) {
            rep = 3 + bits(s, 3);
        } else {
            rep = 11 + bits(s, 7);
        }
        if (s->err) return;
        if (index + rep > nlen + ndist) {
            s->err = ZRD_INFLATE_BAD_DATA;
            return;
        }
        while (rep--) lengths[index++] = len;
    }
    if (lengths[256] == 0) {
        s->err = ZRD_INFLATE_BAD_DATA; /* no end-of-block code */
        return;
    }
    if (construct(&lencode, lengths, nlen) || construct(&distcode, lengths + nlen, ndist)) {
        s->err = ZRD_INFLATE_BAD_DATA;
        return;
    }
    codes(s, &lencode, &distcode);
}

static uint32_t adler32(const uint8_t *p, size_t n)
{
    uint32_t a = 1, b = 0;
    while (n > 0) {
        size_t chunk = n < 5552 ? n : 5552; /* no overflow before % */
        n -= chunk;
        while (chunk--) {
            a += *p++;
            b += a;
        }
        a %= 65521u;
        b %= 65521u;
    }
    return (b << 16) | a;
}

zrd_inflate_status zrd_inflate_zlib(const uint8_t *in, size_t inlen,
                                    uint8_t *out, size_t outlen, size_t *produced)
{
    state s;
    memset(&s, 0, sizeof s);
    s.in = in;
    s.inlen = inlen;
    s.out = out;
    s.outlen = outlen;
    *produced = 0;
    /* RFC 1950, 2.2: deflate, a window of at most 32K, no preset
     * dictionary, and a header check. */
    if (inlen < 2) return ZRD_INFLATE_TRUNCATED;
    unsigned cmf = in[0], flg = in[1];
    if ((cmf & 0x0f) != 8 || (cmf >> 4) > 7 || (cmf * 256 + flg) % 31 != 0 || (flg & 0x20))
        return ZRD_INFLATE_BAD_HEADER;
    s.inpos = 2;
    int last;
    do {
        last = bits(&s, 1);
        int type = bits(&s, 2);
        if (s.err) break;
        if (type == 0) stored(&s);
        else if (type == 1) fixed(&s);
        else if (type == 2) dynamic(&s);
        else s.err = ZRD_INFLATE_BAD_DATA;
    } while (!last && !s.err);
    *produced = s.outpos;
    if (s.err) return s.err;
    /* The Adler-32 of the output, big-endian, after the last block. */
    if (s.inlen - s.inpos < 4) return ZRD_INFLATE_TRUNCATED;
    uint32_t want = (uint32_t) in[s.inpos] << 24 | (uint32_t) in[s.inpos + 1] << 16 |
                    (uint32_t) in[s.inpos + 2] << 8 | in[s.inpos + 3];
    if (adler32(out, s.outpos) != want) return ZRD_INFLATE_CHECK;
    if (s.outpos != outlen) return ZRD_INFLATE_SHORT;
    return ZRD_INFLATE_OK;
}

const char *zrd_inflate_message(zrd_inflate_status st)
{
    switch (st) {
    case ZRD_INFLATE_OK: return "ok";
    case ZRD_INFLATE_BAD_HEADER: return "not a zlib stream";
    case ZRD_INFLATE_BAD_DATA: return "invalid compressed data";
    case ZRD_INFLATE_TRUNCATED: return "the compressed data is truncated";
    case ZRD_INFLATE_TOO_LONG: return "it decompresses to more bytes than it declares";
    case ZRD_INFLATE_SHORT: return "it decompresses to fewer bytes than it declares";
    case ZRD_INFLATE_CHECK: return "its Adler-32 check does not match";
    }
    return "unknown error";
}

/* ---- xz entries ------------------------------------------------------ */

static uint32_t crc32_update(uint32_t crc, const uint8_t *p, size_t n)
{
    crc = ~crc;
    while (n--) {
        crc ^= *p++;
        for (int k = 0; k < 8; k++) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1)));
    }
    return ~crc;
}

static size_t put_le32(uint8_t *p, uint32_t v)
{
    for (int i = 0; i < 4; i++) p[i] = (uint8_t) (v >> (8 * i));
    return 4;
}

/* The xz format's variable-length integer. */
static size_t put_vli(uint8_t *p, uint64_t v)
{
    size_t n = 0;
    while (v >= 0x80) {
        p[n++] = (uint8_t) (v | 0x80);
        v >>= 7;
    }
    p[n++] = (uint8_t) v;
    return n;
}

size_t zrd_xz_container_size(size_t rawlen)
{
    /* Stream header 12, block header 12, data padded to 4, index at most
     * 1 + 1 + 9 + 9 + 3 padding + 4 CRC, stream footer 12. */
    return 12 + 12 + ((rawlen + 3) & ~(size_t) 3) + 27 + 12;
}

size_t zrd_xz_container(const uint8_t *raw, size_t rawlen, uint64_t uncompressed,
                        uint8_t *out)
{
    size_t n = 0;
    /* Stream header: magic, flags (no check), CRC32 of the flags. */
    static const uint8_t magic[6] = {0xFD, '7', 'z', 'X', 'Z', 0x00};
    static const uint8_t flags[2] = {0x00, 0x00};
    memcpy(out + n, magic, 6);
    n += 6;
    memcpy(out + n, flags, 2);
    n += 2;
    n += put_le32(out + n, crc32_update(0, flags, 2));
    /* Block header: 12 bytes, one filter, LZMA2 (0x21) with a one-byte
     * property: the dictionary of R's preset 6, 8 MiB (2 << 22 / 2 + 11). */
    uint8_t *bh = out + n;
    uint8_t header[8] = {12 / 4 - 1, 0x00, 0x21, 0x01, 22, 0, 0, 0};
    memcpy(bh, header, 8);
    put_le32(bh + 8, crc32_update(0, bh, 8));
    n += 12;
    /* The compressed data, padded to four bytes. */
    memcpy(out + n, raw, rawlen);
    n += rawlen;
    while (n % 4) out[n++] = 0;
    /* Index: one record of the block's unpadded and uncompressed sizes. */
    uint8_t *ix = out + n;
    size_t m = 0;
    ix[m++] = 0x00;
    m += put_vli(ix + m, 1);
    m += put_vli(ix + m, 12 + (uint64_t) rawlen);
    m += put_vli(ix + m, uncompressed);
    while (m % 4) ix[m++] = 0;
    m += put_le32(ix + m, crc32_update(0, ix, m));
    n += m;
    /* Stream footer: CRC32, backward size (the index's), flags, "YZ". */
    uint8_t footer[6];
    put_le32(footer, (uint32_t) (m / 4 - 1));
    footer[4] = flags[0];
    footer[5] = flags[1];
    n += put_le32(out + n, crc32_update(0, footer, 6));
    memcpy(out + n, footer, 6);
    n += 6;
    out[n++] = 'Y';
    out[n++] = 'Z';
    return n;
}
