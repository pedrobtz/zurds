/* See zrd_walk.h. No R headers: this file is part of the standalone build.
 *
 * The walk keeps one frame per open item. The loop looks at the top frame:
 * if the item has another child to read, it begins that child (pushing a
 * frame); otherwise it ends the item (popping it). A pairlist's next cell
 * reuses its frame, as ReadItem_Iterative() reuses its loop, so a long
 * pairlist does not deepen the stack. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <zufast/bits.h>
#include <zufast/number.h>

#include "zrd_walk.h"

/* What a frame reads next. */
typedef enum {
    PH_DONE = 0,
    PH_ELEMS,      /* the elements of a STRSXP, VECSXP or EXPRSXP */
    PH_ATTR,       /* the attribute pairlist */
    PH_TAG,        /* a cell's tag */
    PH_CAR,        /* a cell's value */
    PH_CDR,        /* a cell's rest: the next cell or the end */
    PH_PRINTNAME,  /* a symbol's CHARSXP */
    PH_SEQ,        /* the fixed children in `plan` */
    PH_BC_BODY,    /* a BCODESXP item: its ReadBC1 body */
    PH_BC_CODE,    /* ReadBC1: the code vector */
    PH_BC_NCONSTS, /* ReadBC1: the number of constants */
    PH_BC_CONSTS,  /* ReadBCConsts: the constants */
    PH_BCL_ATTR,   /* ReadBCLang: attributes, */
    PH_BCL_TAG,    /* tag, */
    PH_BCL_CAR,    /* car */
    PH_BCL_CDR     /* and cdr */
} zrd_phase;

/* What a frame is: an item, with flags and events, or one of bytecode's
 * structures, which have neither (serialize.c, ReadBC1, ReadBCLang). */
typedef enum { FK_ITEM = 0, FK_BC1, FK_BCLANG } zrd_frame_kind;

struct zrd_frame {
    int type;
    int flags;
    zrd_phase phase;
    int64_t length;
    int64_t next;          /* the next element, or the current cell index */
    uint64_t offset;
    zrd_role role;
    int64_t index;
    int has_name;          /* a cell whose tag is a symbol, or a symbol */
    size_t name_off, name_len;
    zrd_frame_kind kind;
    zrd_role plan[3];      /* PH_SEQ: fixed children, in order */
    int nplan, iplan;
    int tail_attr;         /* PH_SEQ: then attributes, if flagged */
    int64_t reps;          /* BCODESXP: ReadBC's reps vector length */
    int bcl_attr;          /* FK_BCLANG: an ATTRLANGSXP or ATTRLISTSXP */
    int ref_symbol;        /* a REFSXP to a symbol, named name_off/len */
    int32_t ints[ZRD_INT_CAP]; /* a short INTSXP or LGLSXP's values */
    int nints;
    double reals[ZRD_INT_CAP]; /* a short REALSXP's values */
    int nreals;
    const char *lead;      /* a continued pairlist: "@body" after a
                              closure's cell, "@code" after a promise's */
};

#define TICK_EVERY 65536
#define SKIP_CHUNK ((size_t) 1 << 20)

/* ---- failure ------------------------------------------------------- */

static zrd_status fail(zrd_walk *w, zrd_status st, const char *detail)
{
    w->status = st;
    w->err_offset = st == ZRD_ERR_TRUNCATED
        ? zrd_reader_offset(w->in) - w->base : w->item_offset;
    snprintf(w->err_detail, sizeof w->err_detail, "%s", detail);
    return st;
}

const char *zrd_status_name(zrd_status st)
{
    switch (st) {
    case ZRD_OK: return "ZRD_OK";
    case ZRD_STOPPED: return "ZRD_STOPPED";
    case ZRD_ERR_TRUNCATED: return "ZRD_ERR_TRUNCATED";
    case ZRD_ERR_FORMAT: return "ZRD_ERR_FORMAT";
    case ZRD_ERR_UNSUPPORTED: return "ZRD_ERR_UNSUPPORTED";
    case ZRD_ERR_PARSE: return "ZRD_ERR_PARSE";
    case ZRD_ERR_BAD_REF: return "ZRD_ERR_BAD_REF";
    case ZRD_ERR_DEPTH_LIMIT: return "ZRD_ERR_DEPTH_LIMIT";
    case ZRD_ERR_ITEM_LIMIT: return "ZRD_ERR_ITEM_LIMIT";
    case ZRD_ERR_SIZE_LIMIT: return "ZRD_ERR_SIZE_LIMIT";
    case ZRD_ERR_NOMEM: return "ZRD_ERR_NOMEM";
    }
    return "ZRD_ERR_UNKNOWN";
}

/* ---- tokens -------------------------------------------------------- */
/* One reader per format; Stage 2 walks XDR only. */

static uint64_t here(const zrd_walk *w)
{
    return zrd_reader_offset(w->in) - w->base;
}

/* ---- the ASCII format ----------------------------------------------- */

static int is_space(int c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\v' || c == '\f' || c == '\r';
}

/* serialize.c, InWord: skips white space, then reads one word into `buf`
 * (0-terminated), and consumes the white-space character that ends it, as
 * R does. A word as long as the buffer is an error, as in R. */
static zrd_status in_word(zrd_walk *w, char *buf, size_t cap, size_t *len)
{
    uint8_t c;
    do {
        if (zrd_reader_read(w->in, &c, 1) != 1)
            return fail(w, ZRD_ERR_TRUNCATED, "the stream ends before a word");
    } while (is_space(c));
    size_t n = 0;
    for (;;) {
        if (n + 1 >= cap) return fail(w, ZRD_ERR_PARSE, "a word is too long");
        buf[n++] = (char) c;
        if (zrd_reader_read(w->in, &c, 1) != 1 || is_space(c)) break;
    }
    buf[n] = '\0';
    *len = n;
    return ZRD_OK;
}

/* ---- tokens -------------------------------------------------------- */

static int32_t from_u32(uint32_t u)
{
    return u <= INT32_MAX ? (int32_t) u : -(int32_t) (~u) - 1;
}

/* serialize.c, InInteger: four big-endian bytes (XDR), four bytes in the
 * writer's order (binary), or a decimal word or "NA" (ASCII). */
static zrd_status in_int(zrd_walk *w, int32_t *out)
{
    if (w->header.format == ZRD_FORMAT_ASCII) {
        char word[128];
        size_t n;
        zrd_status st = in_word(w, word, sizeof word, &n);
        if (st) return st;
        if (strcmp(word, "NA") == 0) {
            *out = INT32_MIN;
            return ZRD_OK;
        }
        zuf_result r = zuf_parse_i32(word, word + n, out);
        if (r.status != ZUF_OK || r.ptr != word + n)
            return fail(w, ZRD_ERR_PARSE, "an integer is not a number");
        return ZRD_OK;
    }
    uint8_t b[4];
    if (zrd_reader_read(w->in, b, 4) != 4)
        return fail(w, ZRD_ERR_TRUNCATED, "the stream ends inside an integer");
    uint32_t u = w->header.format == ZRD_FORMAT_BINARY && !w->big_endian
        ? zuf_load_le32(b) : zuf_load_be32(b);
    *out = from_u32(u);
    return ZRD_OK;
}

/* serialize.c, InReal: eight big-endian bytes (XDR), eight bytes in the
 * writer's order (binary), or a word: "NA", "NaN", "Inf", "-Inf", or a
 * decimal or (ascii = NA) hexadecimal number. Only the walk's short double
 * capture reads doubles; everything else skips them. */
static zrd_status in_real(zrd_walk *w, double *out)
{
    if (w->header.format == ZRD_FORMAT_ASCII) {
        char word[128];
        size_t n;
        zrd_status st = in_word(w, word, sizeof word, &n);
        if (st) return st;
        if (strcmp(word, "NA") == 0 || strcmp(word, "NaN") == 0) {
            *out = NAN; /* NA and NaN alike here */
            return ZRD_OK;
        }
        if (strcmp(word, "Inf") == 0) { *out = HUGE_VAL; return ZRD_OK; }
        if (strcmp(word, "-Inf") == 0) { *out = -HUGE_VAL; return ZRD_OK; }
        char *end;
        *out = strtod(word, &end);
        if (end != word + n) return fail(w, ZRD_ERR_PARSE, "a double is not a number");
        return ZRD_OK;
    }
    uint8_t b[8];
    if (zrd_reader_read(w->in, b, 8) != 8)
        return fail(w, ZRD_ERR_TRUNCATED, "the stream ends inside a double");
    uint64_t u = w->header.format == ZRD_FORMAT_BINARY && !w->big_endian
        ? zuf_load_le64(b) : zuf_load_be64(b);
    memcpy(out, &u, sizeof *out);
    return ZRD_OK;
}

/* Skips `n` bytes, checking for interrupts between chunks. */
static zrd_status skip(zrd_walk *w, uint64_t n, const char *what)
{
    while (n > 0) {
        size_t take = n > SKIP_CHUNK ? SKIP_CHUNK : (size_t) n;
        if (zrd_reader_read(w->in, NULL, take) != take) {
            char msg[96];
            snprintf(msg, sizeof msg, "the stream ends inside %s", what);
            return fail(w, ZRD_ERR_TRUNCATED, msg);
        }
        n -= take;
        if (n > 0 && w->tick) w->tick(w->tick_ctx);
    }
    return ZRD_OK;
}

/* The bytes an XDR or binary vector element of `type` takes: serialize.c,
 * InIntegerVec, InRealVec, InComplexVec, and the RAWSXP case of
 * ReadItem_Recursive. In ASCII each is one word (two for complex). */
static uint64_t elt_size(int type)
{
    switch (type) {
    case ZRD_LGLSXP: case ZRD_INTSXP: return 4;
    case ZRD_REALSXP: return 8;
    case ZRD_CPLXSXP: return 16;
    case ZRD_RAWSXP: return 1;
    default: return 0;
    }
}

/* The least number of bytes `len` elements of `type` can take in this
 * format: what max_size is checked against. An ASCII word takes at least
 * a character and a separator. */
static double min_vector_bytes(const zrd_walk *w, int type, int64_t len)
{
    if (w->header.format == ZRD_FORMAT_ASCII)
        return 2.0 * (double) len * (type == ZRD_CPLXSXP ? 2 : 1);
    return (double) len * (double) elt_size(type);
}

/* Skips the payload of an atomic vector. */
static zrd_status skip_vector(zrd_walk *w, int type, int64_t len)
{
    if (w->header.format != ZRD_FORMAT_ASCII)
        return skip(w, (uint64_t) len * elt_size(type), "a vector");
    int64_t words = len * (type == ZRD_CPLXSXP ? 2 : 1);
    char word[128];
    size_t n;
    for (int64_t i = 0; i < words; i++) {
        zrd_status st = in_word(w, word, sizeof word, &n);
        if (st) return st;
        if (w->tick && i % TICK_EVERY == TICK_EVERY - 1) w->tick(w->tick_ctx);
    }
    return ZRD_OK;
}

static int host_big_endian(void)
{
    const uint16_t one = 1;
    uint8_t b;
    memcpy(&b, &one, 1);
    return b == 0;
}

static int hex_digit(int c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* serialize.c, InReal for the ASCII format, keeping NA apart from NaN. */
static zrd_status in_real_exact(zrd_walk *w, double *out)
{
    if (w->header.format != ZRD_FORMAT_ASCII) return in_real(w, out);
    char word[128];
    size_t n;
    zrd_status st = in_word(w, word, sizeof word, &n);
    if (st) return st;
    if (strcmp(word, "NA") == 0) {
        uint64_t na = ZRD_NA_REAL_BITS;
        memcpy(out, &na, sizeof *out);
        return ZRD_OK;
    }
    if (strcmp(word, "NaN") == 0) { *out = NAN; return ZRD_OK; }
    if (strcmp(word, "Inf") == 0) { *out = HUGE_VAL; return ZRD_OK; }
    if (strcmp(word, "-Inf") == 0) { *out = -HUGE_VAL; return ZRD_OK; }
    char *end;
    *out = strtod(word, &end);
    if (end != word + n) return fail(w, ZRD_ERR_PARSE, "a double is not a number");
    return ZRD_OK;
}

/* Decodes the payload of an atomic vector into `dst` (see zrd_payload_fn):
 * serialize.c, InIntegerVec, InRealVec, InComplexVec and the RAWSXP case. */
static zrd_status read_vector(zrd_walk *w, int type, int64_t len, void *dst)
{
    zrd_status st;
    if (w->header.format == ZRD_FORMAT_ASCII) {
        int32_t *ints = dst;
        double *dbls = dst;
        uint8_t *raw = dst;
        int64_t n = type == ZRD_CPLXSXP ? 2 * len : len;
        for (int64_t i = 0; i < n; i++) {
            if (type == ZRD_LGLSXP || type == ZRD_INTSXP) {
                if ((st = in_int(w, &ints[i]))) return st;
            } else if (type == ZRD_RAWSXP) {
                /* "%2x": one or two hexadecimal digits. */
                char word[128];
                size_t wn;
                if ((st = in_word(w, word, sizeof word, &wn))) return st;
                int hi = hex_digit((unsigned char) word[0]);
                int lo = wn > 1 ? hex_digit((unsigned char) word[1]) : 0;
                if (hi < 0 || lo < 0 || wn > 2)
                    return fail(w, ZRD_ERR_PARSE, "a raw byte is not hexadecimal");
                raw[i] = (uint8_t) (wn > 1 ? hi * 16 + lo : hi);
            } else if ((st = in_real_exact(w, &dbls[i]))) {
                return st;
            }
            if (w->tick && i % TICK_EVERY == TICK_EVERY - 1) w->tick(w->tick_ctx);
        }
        return ZRD_OK;
    }
    uint64_t size = elt_size(type);
    uint8_t *p = dst;
    uint64_t total = (uint64_t) len * size, done = 0;
    while (done < total) {
        size_t take = total - done > SKIP_CHUNK ? SKIP_CHUNK : (size_t) (total - done);
        if (zrd_reader_read(w->in, p + done, take) != take)
            return fail(w, ZRD_ERR_TRUNCATED, "the stream ends inside a vector");
        done += take;
        if (done < total && w->tick) w->tick(w->tick_ctx);
    }
    /* XDR is big-endian; binary is in the writer's order. */
    int stream_big = w->header.format == ZRD_FORMAT_XDR || w->big_endian;
    if (type == ZRD_RAWSXP || stream_big == host_big_endian()) return ZRD_OK;
    /* The stream's order is not the host's: reverse each element's bytes
     * (a complex number is two doubles). */
    size_t width = size == 4 ? 4 : 8;
    for (uint64_t i = 0; i < total; i += width) {
        for (size_t a = 0, b = width - 1; a < b; a++, b--) {
            uint8_t t = p[i + a];
            p[i + a] = p[i + b];
            p[i + b] = t;
        }
    }
    return ZRD_OK;
}

/* Puts a decoded byte of the current string. */
static void put_char(zrd_walk *w, char *dst, size_t dst_len, size_t i, char c)
{
    if (dst && i < dst_len) dst[i] = c;
    if (w->string_cap && i < w->string_cap) w->string_buf[i] = c;
}

/* serialize.c, InString: `len` bytes (XDR, binary), or `len` characters
 * after white space with C escapes decoded (ASCII). The first `dst_len`
 * go to `dst` when given, and the first string_cap to the capture buffer
 * when strings are captured. */
static zrd_status in_string(zrd_walk *w, int64_t len, char *dst, size_t dst_len)
{
    w->string_len = 0;
    w->string_complete = 1;
    if (w->string_cap) {
        w->string_len = (size_t) len < w->string_cap ? (size_t) len : w->string_cap;
        w->string_complete = (size_t) len <= w->string_cap;
    }
    if (w->header.format != ZRD_FORMAT_ASCII) {
        /* Read what is kept, through a small buffer; skip the rest. */
        uint64_t keep = dst ? dst_len : 0, i = 0;
        if (w->string_len > keep) keep = w->string_len;
        if (keep > (uint64_t) len) keep = (uint64_t) len;
        uint8_t tmp[4096];
        while (i < keep) {
            size_t take = keep - i < sizeof tmp ? (size_t) (keep - i) : sizeof tmp;
            if (zrd_reader_read(w->in, tmp, take) != take)
                return fail(w, ZRD_ERR_TRUNCATED, "the stream ends inside a string");
            for (size_t j = 0; j < take; j++) put_char(w, dst, dst_len, (size_t) i + j, (char) tmp[j]);
            i += take;
        }
        return skip(w, (uint64_t) len - keep, "a string");
    }
    if (len == 0) return ZRD_OK;
    uint8_t c;
    do {
        if (zrd_reader_read(w->in, &c, 1) != 1)
            return fail(w, ZRD_ERR_TRUNCATED, "the stream ends inside a string");
    } while (is_space(c));
    for (int64_t i = 0; i < len; i++) {
        if (i > 0 && zrd_reader_read(w->in, &c, 1) != 1)
            return fail(w, ZRD_ERR_TRUNCATED, "the stream ends inside a string");
        if (c != '\\') {
            put_char(w, dst, dst_len, (size_t) i, (char) c);
            continue;
        }
        if (zrd_reader_read(w->in, &c, 1) != 1)
            return fail(w, ZRD_ERR_TRUNCATED, "the stream ends inside a string");
        char out;
        switch (c) {
        case 'n': out = '\n'; break;
        case 't': out = '\t'; break;
        case 'v': out = '\v'; break;
        case 'b': out = '\b'; break;
        case 'r': out = '\r'; break;
        case 'f': out = '\f'; break;
        case 'a': out = '\a'; break;
        case '0': case '1': case '2': case '3':
        case '4': case '5': case '6': case '7': {
            int d = c - '0', j = 1;
            uint8_t next;
            while (j < 3 && zrd_reader_peek(w->in, &next, 1) == 1 &&
                   next >= '0' && next < '8') {
                zrd_reader_read(w->in, &next, 1);
                d = d * 8 + (next - '0');
                j++;
            }
            out = (char) d;
            break;
        }
        default: out = (char) c; /* \\, \?, \', \" and anything else */
        }
        put_char(w, dst, dst_len, (size_t) i, out);
    }
    return ZRD_OK;
}

/* Checks that `need` more bytes fit under max_size. Called before a
 * length from the stream is trusted (design section 11). */
static zrd_status check_size(zrd_walk *w, double need)
{
    if ((double) here(w) + need > w->limits.max_size)
        return fail(w, ZRD_ERR_SIZE_LIMIT,
                    "the item claims more bytes than max_size allows");
    return ZRD_OK;
}

/* serialize.c, ReadLENGTH. */
static zrd_status in_length(zrd_walk *w, int64_t *out)
{
    int32_t len;
    zrd_status st = in_int(w, &len);
    if (st) return st;
    if (len < -1) return fail(w, ZRD_ERR_PARSE, "negative serialized length for vector");
    if (len == -1) {
        int32_t hi, lo;
        if ((st = in_int(w, &hi)) || (st = in_int(w, &lo))) return st;
        uint32_t uhi = (uint32_t) hi, ulo = (uint32_t) lo;
        if (uhi > 65536)
            return fail(w, ZRD_ERR_PARSE, "invalid upper part of serialized vector length");
        *out = (int64_t) (((uint64_t) uhi << 32) + ulo);
    } else {
        *out = len;
    }
    return ZRD_OK;
}

/* ---- the reference table and symbol names --------------------------- */

static zrd_status arena_append(zrd_walk *w, size_t n, size_t *off)
{
    if (w->arena_cap - w->arena_len < n) {
        size_t cap = w->arena_cap ? w->arena_cap : 256;
        while (cap - w->arena_len < n) {
            if (cap > SIZE_MAX / 2) return fail(w, ZRD_ERR_NOMEM, "out of memory for symbol names");
            cap *= 2;
        }
        char *p = realloc(w->arena, cap);
        if (p == NULL) return fail(w, ZRD_ERR_NOMEM, "out of memory for symbol names");
        w->arena = p;
        w->arena_cap = cap;
    }
    *off = w->arena_len;
    w->arena_len += n;
    return ZRD_OK;
}

/* serialize.c, AddReadRef. The table's growth is bounded by max_items:
 * every entry is an item. */
static zrd_status add_ref(zrd_walk *w, int type, size_t name_off, size_t name_len)
{
    if (w->nrefs == w->refs_cap) {
        size_t cap = w->refs_cap ? 2 * w->refs_cap : 64;
        zrd_ref *p = realloc(w->refs, cap * sizeof *p);
        if (p == NULL) return fail(w, ZRD_ERR_NOMEM, "out of memory for the reference table");
        w->refs = p;
        w->refs_cap = cap;
    }
    w->refs[w->nrefs].type = type;
    w->refs[w->nrefs].name_off = name_off;
    w->refs[w->nrefs].name_len = name_len;
    w->nrefs++;
    return ZRD_OK;
}

/* ---- events -------------------------------------------------------- */

static zrd_status emit_at(zrd_walk *w, zrd_event_kind kind, const zrd_frame *f,
                          int depth, const zrd_frame *parent,
                          int64_t ref, int ref_type, uint64_t end_offset)
{
    if (w->on_event == NULL) return ZRD_OK;
    zrd_event ev;
    memset(&ev, 0, sizeof ev);
    ev.kind = kind;
    ev.type = f->type;
    ev.flags = f->flags;
    ev.length = f->length;
    ev.offset = kind == ZRD_EVENT_BEGIN ? f->offset : end_offset;
    ev.depth = depth;
    ev.role = f->role;
    ev.index = f->index;
    ev.ref = ref;
    ev.ref_type = ref_type;
    if (kind == ZRD_EVENT_END && f->nints > 0) {
        ev.ints = f->ints;
        ev.nints = f->nints;
    }
    if (kind == ZRD_EVENT_END && f->nreals > 0) {
        ev.reals = f->reals;
        ev.nreals = f->nreals;
    }
    if (kind == ZRD_EVENT_END && f->type == ZRD_REFSXP && f->ref_symbol) {
        ev.data = w->arena + f->name_off;
        ev.data_len = f->name_len;
        ev.data_complete = 1;
    }
    if (kind == ZRD_EVENT_END && f->type == ZRD_CHARSXP && w->string_cap) {
        ev.data = w->string_buf;
        ev.data_len = f->length > 0 ? w->string_len : 0;
        ev.data_complete = f->length > 0 ? w->string_complete : 1;
    }
    if (f->role == ZRD_ROLE_CAR && parent && parent->has_name) {
        ev.name = w->arena + parent->name_off;
        ev.name_len = parent->name_len;
    }
    if (w->on_event(w->event_ctx, &ev)) {
        w->status = ZRD_STOPPED;
        return ZRD_STOPPED;
    }
    return ZRD_OK;
}

static zrd_status emit(zrd_walk *w, zrd_event_kind kind, const zrd_frame *f,
                       int depth, const zrd_frame *parent,
                       int64_t ref, int ref_type)
{
    return emit_at(w, kind, f, depth, parent, ref, ref_type, here(w));
}

/* ---- items --------------------------------------------------------- */

static zrd_phase after_attr(const zrd_frame *f)
{
    if (ZRD_IS_PAIRLIST(f->type))
        return ZRD_HAS_TAG(f->flags) ? PH_TAG : PH_CAR;
    return PH_DONE;
}

static zrd_phase attr_or_done(const zrd_frame *f)
{
    return ZRD_HAS_ATTR(f->flags) ? PH_ATTR : PH_DONE;
}

/* Sets up PH_SEQ over up to three fixed children. */
static void plan(zrd_frame *f, int tail_attr, int n, zrd_role a, zrd_role b, zrd_role c)
{
    f->plan[0] = a;
    f->plan[1] = b;
    f->plan[2] = c;
    f->nplan = n;
    f->iplan = 0;
    f->tail_attr = tail_attr;
    f->phase = PH_SEQ;
}

static zrd_phase first_cell_phase(const zrd_frame *f)
{
    if (ZRD_HAS_ATTR(f->flags)) return PH_ATTR;
    return after_attr(f);
}

/* Counts an item against max_items, and ticks. */
static zrd_status count_item(zrd_walk *w)
{
    w->items += 1;
    if (w->items > w->limits.max_items)
        return fail(w, ZRD_ERR_ITEM_LIMIT, "the stream has more items than max_items allows");
    if (w->tick && (uint64_t) w->items % TICK_EVERY == 0) w->tick(w->tick_ctx);
    return ZRD_OK;
}

/* Begins the item whose flags were just read at `offset`, as child
 * `role`/`index` of the top frame (or as the root). Pushes its frame, reads
 * what precedes its children, and emits BEGIN. serialize.c,
 * ReadItem_Recursive. */
static zrd_status begin(zrd_walk *w, int32_t flags, uint64_t offset,
                        zrd_role role, int64_t index)
{
    zrd_status st;
    w->item_offset = offset;
    if ((st = count_item(w))) return st;
    if (w->depth > w->limits.max_depth)
        return fail(w, ZRD_ERR_DEPTH_LIMIT, "the items nest deeper than max_depth allows");
    if ((double) offset > w->limits.max_size)
        return fail(w, ZRD_ERR_SIZE_LIMIT, "the stream is longer than max_size allows");

    zrd_frame *parent = w->depth ? &w->stack[w->depth - 1] : NULL;
    zrd_frame *f = &w->stack[w->depth++];
    memset(f, 0, sizeof *f);
    f->type = ZRD_TYPE(flags);
    f->flags = flags;
    f->offset = offset;
    f->role = role;
    f->index = index;
    f->length = -1;
    f->phase = PH_DONE;

    int64_t ref = 0;
    int ref_type = 0;

    switch (f->type) {
    /* Values with no payload. */
    case ZRD_NILVALUE_SXP: case ZRD_EMPTYENV_SXP: case ZRD_BASEENV_SXP:
    case ZRD_GLOBALENV_SXP: case ZRD_UNBOUNDVALUE_SXP:
    case ZRD_MISSINGARG_SXP: case ZRD_BASENAMESPACE_SXP:
        break;

    /* serialize.c, InRefIndex and GetReadRef. */
    case ZRD_REFSXP: {
        int32_t i = (int32_t) ((uint32_t) flags >> 8);
        if (i == 0 && (st = in_int(w, &i))) return st;
        if (i < 1 || (size_t) i > w->nrefs)
            return fail(w, ZRD_ERR_BAD_REF, "reference index out of range");
        ref = i;
        ref_type = w->refs[i - 1].type;
        if (ref_type == ZRD_SYMSXP) {
            f->ref_symbol = 1;
            f->name_off = w->refs[i - 1].name_off;
            f->name_len = w->refs[i - 1].name_len;
        }
        /* A tag given by reference names its cell like a symbol does. */
        if (role == ZRD_ROLE_TAG && ref_type == ZRD_SYMSXP && parent) {
            parent->has_name = 1;
            parent->name_off = w->refs[i - 1].name_off;
            parent->name_len = w->refs[i - 1].name_len;
        }
        break;
    }

    case ZRD_SYMSXP:
        f->phase = PH_PRINTNAME;
        break;

    /* serialize.c, the CHARSXP case and ReadChar. A symbol's name is kept,
     * for paths and for the tags of later cells; other strings are
     * skipped. */
    case ZRD_CHARSXP: {
        int32_t len;
        if ((st = in_int(w, &len))) return st;
        if (len < -1) return fail(w, ZRD_ERR_PARSE, "invalid length");
        f->length = len;
        if (len > 0 && (st = check_size(w, (double) len))) return st;
        if ((st = emit(w, ZRD_EVENT_BEGIN, f, w->depth - 1, parent, 0, 0))) return st;
        if (len > 0 && parent && parent->type == ZRD_SYMSXP) {
            size_t off;
            if ((st = arena_append(w, (size_t) len, &off))) return st;
            if ((st = in_string(w, len, w->arena + off, (size_t) len))) return st;
            parent->has_name = 1;
            parent->name_off = off;
            parent->name_len = (size_t) len;
        } else {
            char *dst = NULL;
            if (w->payload && len > 0) {
                zrd_event pev;
                memset(&pev, 0, sizeof pev);
                pev.kind = ZRD_EVENT_BEGIN;
                pev.type = f->type;
                pev.flags = f->flags;
                pev.length = len;
                pev.offset = f->offset;
                pev.depth = w->depth - 1;
                pev.role = f->role;
                pev.index = f->index;
                dst = w->payload(w->payload_ctx, &pev);
            }
            if ((st = in_string(w, len > 0 ? len : 0, dst, dst ? (size_t) len : 0)))
                return st;
        }
        f->phase = ZRD_HAS_ATTR(flags) ? PH_ATTR : PH_DONE;
        return ZRD_OK;
    }

    case ZRD_LGLSXP: case ZRD_INTSXP: case ZRD_REALSXP:
    case ZRD_CPLXSXP: case ZRD_RAWSXP: {
        int64_t len;
        if ((st = in_length(w, &len))) return st;
        f->length = len;
        if ((st = check_size(w, min_vector_bytes(w, f->type, len)))) return st;
        if ((st = emit(w, ZRD_EVENT_BEGIN, f, w->depth - 1, parent, 0, 0))) return st;
        void *dst = NULL;
        if (w->payload) {
            zrd_event pev;
            memset(&pev, 0, sizeof pev);
            pev.kind = ZRD_EVENT_BEGIN;
            pev.type = f->type;
            pev.flags = f->flags;
            pev.length = len;
            pev.offset = f->offset;
            pev.depth = w->depth - 1;
            pev.role = f->role;
            pev.index = f->index;
            dst = w->payload(w->payload_ctx, &pev);
        }
        if (dst) {
            if ((st = read_vector(w, f->type, len, dst))) return st;
        } else if (w->capture_ints && (f->type == ZRD_INTSXP || f->type == ZRD_LGLSXP) &&
            len <= ZRD_INT_CAP) {
            for (int64_t i = 0; i < len; i++)
                if ((st = in_int(w, &f->ints[i]))) return st;
            f->nints = (int) len;
        } else if (w->capture_ints && f->type == ZRD_REALSXP && len <= ZRD_INT_CAP) {
            for (int64_t i = 0; i < len; i++)
                if ((st = in_real(w, &f->reals[i]))) return st;
            f->nreals = (int) len;
        } else if ((st = skip_vector(w, f->type, len))) {
            return st;
        }
        f->phase = ZRD_HAS_ATTR(flags) ? PH_ATTR : PH_DONE;
        return ZRD_OK;
    }

    case ZRD_STRSXP: case ZRD_VECSXP: case ZRD_EXPRSXP: {
        int64_t len;
        if ((st = in_length(w, &len))) return st;
        f->length = len;
        /* Each element is an item of at least one four-byte integer. */
        if ((double) len > w->limits.max_items - w->items)
            return fail(w, ZRD_ERR_ITEM_LIMIT, "the vector claims more items than max_items allows");
        if ((st = check_size(w, 4.0 * (double) len))) return st;
        f->phase = len > 0 ? PH_ELEMS : (ZRD_HAS_ATTR(flags) ? PH_ATTR : PH_DONE);
        break;
    }

    /* serialize.c, ReadItem_Iterative: attributes, tag, car, then the
     * cdr as the next item. A closure's tag is its environment, its car
     * its formals and its cdr its body; a promise's are its environment,
     * value and code. */
    case ZRD_LISTSXP: case ZRD_LANGSXP: case ZRD_CLOSXP: case ZRD_PROMSXP:
    case ZRD_DOTSXP:
        f->phase = first_cell_phase(f);
        break;

    /* serialize.c, the ENVSXP case: registered before its contents, so a
     * cycle refers back to it; the attributes are always written. */
    case ZRD_ENVSXP: {
        int32_t locked;
        if ((st = in_int(w, &locked))) return st;
        if ((st = add_ref(w, ZRD_ENVSXP, 0, 0))) return st;
        plan(f, 0, 3, ZRD_ROLE_ENCLOS, ZRD_ROLE_FRAME, ZRD_ROLE_HASHTAB);
        f->tail_attr = 2; /* always */
        break;
    }

    /* serialize.c, InStringVec: a zero, a length, then that many items;
     * registered once read. */
    case ZRD_NAMESPACESXP: case ZRD_PACKAGESXP: case ZRD_PERSISTSXP: {
        int32_t zero, len;
        if ((st = in_int(w, &zero))) return st;
        if (zero != 0)
            return fail(w, ZRD_ERR_PARSE, "names in persistent strings are not supported");
        if ((st = in_int(w, &len))) return st;
        if (len < 0) return fail(w, ZRD_ERR_PARSE, "negative string vector length");
        if ((double) len > w->limits.max_items - w->items)
            return fail(w, ZRD_ERR_ITEM_LIMIT, "the item claims more items than max_items allows");
        f->length = len;
        f->phase = len > 0 ? PH_ELEMS : PH_DONE;
        break;
    }

    /* serialize.c, the EXTPTRSXP and WEAKREFSXP cases: registered first. */
    case ZRD_EXTPTRSXP:
        if ((st = add_ref(w, ZRD_EXTPTRSXP, 0, 0))) return st;
        plan(f, 1, 2, ZRD_ROLE_PROT, ZRD_ROLE_EXTTAG, ZRD_ROLE_ROOT);
        break;
    case ZRD_WEAKREFSXP:
        if ((st = add_ref(w, ZRD_WEAKREFSXP, 0, 0))) return st;
        f->phase = attr_or_done(f);
        break;

    /* serialize.c, the SPECIALSXP and BUILTINSXP cases: a length and the
     * primitive's name. */
    case ZRD_SPECIALSXP: case ZRD_BUILTINSXP: {
        int32_t len;
        if ((st = in_int(w, &len))) return st;
        if (len < 0) return fail(w, ZRD_ERR_PARSE, "invalid length");
        if ((st = check_size(w, (double) len))) return st;
        f->length = len;
        if ((st = emit(w, ZRD_EVENT_BEGIN, f, w->depth - 1, parent, 0, 0))) return st;
        if ((st = in_string(w, len, NULL, 0))) return st;
        f->phase = attr_or_done(f);
        return ZRD_OK;
    }

    /* serialize.c, the OBJSXP case: nothing but attributes. */
    case ZRD_OBJSXP:
        f->phase = attr_or_done(f);
        break;

    /* serialize.c, the ALTREP_SXP case: class information, state and
     * attributes, each an item; no attributes after them. */
    case ZRD_ALTREP_SXP:
        plan(f, 0, 3, ZRD_ROLE_ALTREP_INFO, ZRD_ROLE_ALTREP_STATE, ZRD_ROLE_ALTREP_ATTR);
        break;

    /* serialize.c, ReadBC: the length of the reps vector, then ReadBC1. */
    case ZRD_BCODESXP: {
        int32_t reps;
        if ((st = in_int(w, &reps))) return st;
        if (reps < 0) return fail(w, ZRD_ERR_PARSE, "negative bytecode reps length");
        if ((double) reps > w->limits.max_items - w->items)
            return fail(w, ZRD_ERR_ITEM_LIMIT, "the bytecode claims more items than max_items allows");
        f->reps = reps;
        f->phase = PH_BC_BODY;
        break;
    }

    case ZRD_CLASSREFSXP:
        return fail(w, ZRD_ERR_PARSE, "class references cannot be read");
    case ZRD_GENERICREFSXP:
        return fail(w, ZRD_ERR_PARSE, "generic function references cannot be read");

    default: {
        char msg[96];
        snprintf(msg, sizeof msg, "unknown item type %d", f->type);
        return fail(w, ZRD_ERR_PARSE, msg);
    }
    }
    return emit(w, ZRD_EVENT_BEGIN, f, w->depth - 1, parent, ref, ref_type);
}

/* Reads the flags of the next item and begins it. */
static zrd_status begin_next(zrd_walk *w, zrd_role role, int64_t index)
{
    uint64_t offset = here(w);
    int32_t flags;
    zrd_status st = in_int(w, &flags);
    if (st) return st;
    return begin(w, flags, offset, role, index);
}

/* Ends the top item at `end_offset`: emits END and pops it. A symbol
 * enters the reference table here, after its name, as in
 * ReadItem_Recursive. */
static zrd_status end(zrd_walk *w, uint64_t end_offset)
{
    zrd_frame *f = &w->stack[w->depth - 1];
    zrd_frame *parent = w->depth > 1 ? &w->stack[w->depth - 2] : NULL;
    zrd_status st;
    if (f->kind != FK_ITEM) {
        w->depth--;
        return ZRD_OK;
    }
    if (f->type == ZRD_NAMESPACESXP || f->type == ZRD_PACKAGESXP ||
        f->type == ZRD_PERSISTSXP) {
        if ((st = add_ref(w, f->type, 0, 0))) return st;
    }
    if (f->type == ZRD_SYMSXP) {
        if ((st = add_ref(w, ZRD_SYMSXP, f->name_off, f->has_name ? f->name_len : 0)))
            return st;
        if (f->role == ZRD_ROLE_TAG && parent) {
            parent->has_name = 1;
            parent->name_off = f->name_off;
            parent->name_len = f->has_name ? f->name_len : 0;
        }
    }
    st = emit_at(w, ZRD_EVENT_END, f, w->depth - 1, parent, 0, 0, end_offset);
    w->depth--;
    return st;
}

/* ---- bytecode ------------------------------------------------------ */

/* Pushes one of bytecode's structures, which has no flags and emits no
 * events, but counts against max_items and max_depth like an item. */
static zrd_status push_pseudo(zrd_walk *w, zrd_frame_kind kind, zrd_role role,
                              int64_t index, zrd_frame **out)
{
    zrd_status st;
    w->item_offset = here(w);
    if ((st = count_item(w))) return st;
    if (w->depth > w->limits.max_depth)
        return fail(w, ZRD_ERR_DEPTH_LIMIT, "the items nest deeper than max_depth allows");
    zrd_frame *f = &w->stack[w->depth++];
    memset(f, 0, sizeof *f);
    f->kind = kind;
    f->type = -1;
    f->role = role;
    f->index = index;
    f->offset = w->item_offset;
    f->length = -1;
    *out = f;
    return ZRD_OK;
}

/* The length of the reps vector of the innermost ReadBC. */
static int64_t bc_reps(const zrd_walk *w)
{
    for (int i = w->depth - 1; i >= 0; i--)
        if (w->stack[i].kind == FK_ITEM && w->stack[i].type == ZRD_BCODESXP)
            return w->stack[i].reps;
    return 0;
}

static int is_bc_lang(int32_t type)
{
    return type == ZRD_BCREPDEF || type == ZRD_BCREPREF || type == ZRD_LANGSXP ||
           type == ZRD_LISTSXP || type == ZRD_ATTRLANGSXP || type == ZRD_ATTRLISTSXP;
}

/* serialize.c, ReadBCLang, for a `type` just read. A language cell gets a
 * frame; `reuse`, when given, is the cell whose cdr this is, and its frame
 * is reused so a long call does not deepen the stack. */
static zrd_status bc_lang(zrd_walk *w, int32_t type, zrd_role role, int64_t index,
                          zrd_frame *reuse)
{
    zrd_status st;
    int64_t reps = bc_reps(w);
    if (type == ZRD_BCREPREF) {
        int32_t i;
        if ((st = in_int(w, &i))) return st;
        if (i < 0 || i >= reps)
            return fail(w, ZRD_ERR_BAD_REF, "bytecode reference out of range");
        if (reuse) reuse->phase = PH_DONE;
        return ZRD_OK;
    }
    if (type == ZRD_BCREPDEF) {
        int32_t pos;
        if ((st = in_int(w, &pos)) || (st = in_int(w, &type))) return st;
        if (pos < 0 || pos >= reps)
            return fail(w, ZRD_ERR_BAD_REF, "bytecode definition out of range");
    }
    switch (type) {
    case ZRD_LANGSXP: case ZRD_LISTSXP: case ZRD_ATTRLANGSXP: case ZRD_ATTRLISTSXP: {
        zrd_frame *f = reuse;
        if (f) {
            w->item_offset = here(w);
            if ((st = count_item(w))) return st;
        } else if ((st = push_pseudo(w, FK_BCLANG, role, index, &f))) {
            return st;
        }
        f->bcl_attr = type == ZRD_ATTRLANGSXP || type == ZRD_ATTRLISTSXP;
        f->phase = f->bcl_attr ? PH_BCL_ATTR : PH_BCL_TAG;
        return ZRD_OK;
    }
    case ZRD_BCREPDEF: case ZRD_BCREPREF:
        return fail(w, ZRD_ERR_PARSE, "malformed bytecode language object");
    default:
        /* A marker, then an ordinary item. */
        if (reuse) reuse->phase = PH_DONE;
        return begin_next(w, role, index);
    }
}

/* Advances the top frame by one child, or ends it. */
static zrd_status step(zrd_walk *w)
{
    zrd_frame *f = &w->stack[w->depth - 1];
    zrd_status st;
    switch (f->phase) {
    case PH_ELEMS: {
        int64_t i = f->next++;
        if (f->next >= f->length) {
            int strings = f->type == ZRD_NAMESPACESXP || f->type == ZRD_PACKAGESXP ||
                          f->type == ZRD_PERSISTSXP;
            f->phase = strings ? PH_DONE : attr_or_done(f);
        }
        return begin_next(w, ZRD_ROLE_ELEMENT, i);
    }
    case PH_ATTR:
        f->phase = after_attr(f);
        return begin_next(w, ZRD_ROLE_ATTR, 0);
    case PH_TAG:
        f->phase = PH_CAR;
        return begin_next(w, ZRD_ROLE_TAG, f->next);
    case PH_CAR:
        f->phase = PH_CDR;
        return begin_next(w, ZRD_ROLE_CAR, f->next);
    case PH_PRINTNAME:
        f->phase = PH_DONE;
        return begin_next(w, ZRD_ROLE_PRINTNAME, 0);
    case PH_CDR: {
        uint64_t offset = here(w);
        int32_t flags;
        if ((st = in_int(w, &flags))) return st;
        if (ZRD_IS_PAIRLIST(ZRD_TYPE(flags))) {
            /* The next cell: end this one and reuse the frame. */
            int64_t cell = f->next + 1;
            const char *lead = f->type == ZRD_CLOSXP ? "@body"
                             : f->type == ZRD_PROMSXP ? "@code" : f->lead;
            if ((st = end(w, offset))) return st;
            if ((st = begin(w, flags, offset, ZRD_ROLE_CELL, cell))) return st;
            w->stack[w->depth - 1].next = cell;
            w->stack[w->depth - 1].lead = lead;
            return ZRD_OK;
        }
        f->phase = PH_DONE;
        return begin(w, flags, offset, ZRD_ROLE_CDR, f->next);
    }
    case PH_SEQ:
        if (f->iplan < f->nplan) {
            zrd_role role = f->plan[f->iplan++];
            return begin_next(w, role, 0);
        }
        f->phase = f->tail_attr == 2 ? PH_ATTR : f->tail_attr == 1 ? attr_or_done(f) : PH_DONE;
        return ZRD_OK;

    /* serialize.c, ReadBC and ReadBC1. */
    case PH_BC_BODY: {
        zrd_frame *bc;
        f->phase = attr_or_done(f);
        if ((st = push_pseudo(w, FK_BC1, ZRD_ROLE_ROOT, 0, &bc))) return st;
        bc->phase = PH_BC_CODE;
        return ZRD_OK;
    }
    case PH_BC_CODE:
        f->phase = PH_BC_NCONSTS;
        return begin_next(w, ZRD_ROLE_BC_CODE, 0);
    case PH_BC_NCONSTS: {
        int32_t n;
        if ((st = in_int(w, &n))) return st;
        if (n < 0) return fail(w, ZRD_ERR_PARSE, "negative bytecode constant count");
        if ((double) n > w->limits.max_items - w->items)
            return fail(w, ZRD_ERR_ITEM_LIMIT, "the bytecode claims more items than max_items allows");
        f->length = n;
        f->next = 0;
        f->phase = n > 0 ? PH_BC_CONSTS : PH_DONE;
        return ZRD_OK;
    }
    /* serialize.c, ReadBCConsts. */
    case PH_BC_CONSTS: {
        int64_t i = f->next++;
        int32_t type;
        if (f->next >= f->length) f->phase = PH_DONE;
        if ((st = in_int(w, &type))) return st;
        if (type == ZRD_BCODESXP) {
            zrd_frame *bc;
            if ((st = push_pseudo(w, FK_BC1, ZRD_ROLE_BC_CONST, i, &bc))) return st;
            bc->phase = PH_BC_CODE;
            return ZRD_OK;
        }
        if (is_bc_lang(type)) return bc_lang(w, type, ZRD_ROLE_BC_CONST, i, NULL);
        return begin_next(w, ZRD_ROLE_BC_CONST, i);
    }
    /* serialize.c, ReadBCLang. */
    case PH_BCL_ATTR:
        f->phase = PH_BCL_TAG;
        return begin_next(w, ZRD_ROLE_BC_LANG, 0);
    case PH_BCL_TAG:
        f->phase = PH_BCL_CAR;
        return begin_next(w, ZRD_ROLE_BC_LANG, 0);
    case PH_BCL_CAR: {
        int32_t type;
        f->phase = PH_BCL_CDR;
        if ((st = in_int(w, &type))) return st;
        return bc_lang(w, type, ZRD_ROLE_BC_LANG, 0, NULL);
    }
    case PH_BCL_CDR: {
        int32_t type;
        if ((st = in_int(w, &type))) return st;
        return bc_lang(w, type, ZRD_ROLE_BC_LANG, 0, f);
    }

    case PH_DONE:
        return end(w, here(w));
    }
    return fail(w, ZRD_ERR_PARSE, "internal error: bad walk phase");
}

/* ---- the public entry points --------------------------------------- */

zrd_status zrd_walk_init(zrd_walk *w, zrd_reader *in, const zrd_limits *lim,
                         zrd_event_fn on_event, void *event_ctx,
                         zrd_tick_fn tick, void *tick_ctx)
{
    memset(w, 0, sizeof *w);
    w->in = in;
    w->limits = *lim;
    w->on_event = on_event;
    w->event_ctx = event_ctx;
    w->tick = tick;
    w->tick_ctx = tick_ctx;
    w->base = zrd_reader_offset(in);
    if (lim->max_depth < 1) return fail(w, ZRD_ERR_PARSE, "max_depth must be positive");
    /* The root plus max_depth levels below it. */
    w->stack = calloc((size_t) lim->max_depth + 1, sizeof *w->stack);
    if (w->stack == NULL) return fail(w, ZRD_ERR_NOMEM, "out of memory for the walk stack");
    return ZRD_OK;
}

zrd_status zrd_walk_capture_strings(zrd_walk *w, size_t cap)
{
    if (cap == 0) return ZRD_OK;
    w->string_buf = malloc(cap);
    if (w->string_buf == NULL) return fail(w, ZRD_ERR_NOMEM, "out of memory for strings");
    w->string_cap = cap;
    return ZRD_OK;
}

void zrd_walk_free(zrd_walk *w)
{
    free(w->string_buf);
    w->string_buf = NULL;
    w->string_cap = 0;
    free(w->stack);
    free(w->refs);
    free(w->arena);
    w->stack = NULL;
    w->refs = NULL;
    w->arena = NULL;
}

/* serialize.c, InFormat and the version switch of R_Unserialize. The
 * binary format is in the writer's byte order, which the stream does not
 * record: the version, 2 or 3, tells it. */
zrd_status zrd_walk_header(zrd_walk *w)
{
    uint8_t fmt[2];
    if (zrd_reader_read(w->in, fmt, 2) != 2)
        return fail(w, ZRD_ERR_TRUNCATED, "the stream ends inside its header");
    if (fmt[1] != '\n' || (fmt[0] != 'X' && fmt[0] != 'B' && fmt[0] != 'A'))
        return fail(w, ZRD_ERR_FORMAT, "unknown stream format");
    w->header.format = fmt[0];
    if (fmt[0] == ZRD_FORMAT_BINARY) {
        uint8_t b[4];
        if (zrd_reader_peek(w->in, b, 4) != 4)
            return fail(w, ZRD_ERR_TRUNCATED, "the stream ends inside its header");
        uint32_t le = zuf_load_le32(b), be = zuf_load_be32(b);
        if (le == 2 || le == 3) w->big_endian = 0;
        else if (be == 2 || be == 3) w->big_endian = 1;
        else return fail(w, ZRD_ERR_FORMAT, "unsupported serialization version");
    }
    int32_t v, wv, mv;
    zrd_status st;
    if ((st = in_int(w, &v)) || (st = in_int(w, &wv)) || (st = in_int(w, &mv)))
        return st;
    w->header.version = v;
    w->header.writer_version = wv;
    w->header.min_reader_version = mv;
    if (v == 3) {
        int32_t n;
        if ((st = in_int(w, &n))) return st;
        if (n < 0 || n >= (int32_t) sizeof w->header.native_encoding)
            return fail(w, ZRD_ERR_FORMAT, "invalid length of encoding name");
        size_t cap = w->string_cap;
        w->string_cap = 0;
        st = in_string(w, n, w->header.native_encoding, (size_t) n);
        w->string_cap = cap;
        if (st) return st;
        w->header.native_encoding[n] = '\0';
    } else if (v != 2) {
        char msg[64];
        snprintf(msg, sizeof msg, "unsupported serialization version %d", (int) v);
        return fail(w, ZRD_ERR_FORMAT, msg);
    }
    return ZRD_OK;
}

zrd_status zrd_walk_item(zrd_walk *w)
{
    zrd_status st = begin_next(w, ZRD_ROLE_ROOT, 0);
    while (st == ZRD_OK && w->depth > 0) st = step(w);
    /* ASCII leaves the newline after a final string, which R's reader
     * leaves too; take it, so the walk ends where the stream does. */
    if (st == ZRD_OK && w->header.format == ZRD_FORMAT_ASCII) {
        uint8_t c;
        while (zrd_reader_peek(w->in, &c, 1) == 1 && is_space(c))
            zrd_reader_read(w->in, &c, 1);
    }
    if (st == ZRD_OK) w->status = ZRD_OK;
    return st;
}

/* ---- paths --------------------------------------------------------- */

static void append(char *buf, size_t cap, size_t *len, const char *s, size_t n)
{
    if (*len >= cap) return;
    size_t room = cap - 1 - *len;
    if (n > room) n = room;
    memcpy(buf + *len, s, n);
    *len += n;
    buf[*len] = '\0';
}

void zrd_walk_path(const zrd_walk *w, char *buf, size_t cap)
{
    size_t len = 0;
    if (cap == 0) return;
    buf[0] = '\0';
    if (w->depth > 0 && w->stack[0].lead)
        append(buf, cap, &len, w->stack[0].lead, strlen(w->stack[0].lead));
    for (int i = 1; i < w->depth; i++) {
        const zrd_frame *f = &w->stack[i], *p = &w->stack[i - 1];
        char seg[48];
        const char *fixed = NULL;
        switch (f->role) {
        case ZRD_ROLE_ELEMENT:
            snprintf(seg, sizeof seg, "[[%lld]]", (long long) f->index + 1);
            append(buf, cap, &len, seg, strlen(seg));
            break;
        case ZRD_ROLE_CAR:
            if (p->type == ZRD_CLOSXP) fixed = "@formals";
            else if (p->type == ZRD_PROMSXP) fixed = "@value";
            else if (p->has_name) {
                append(buf, cap, &len, "$", 1);
                append(buf, cap, &len, w->arena + p->name_off, p->name_len);
            } else {
                snprintf(seg, sizeof seg, "[[%lld]]", (long long) f->index + 1);
                append(buf, cap, &len, seg, strlen(seg));
            }
            break;
        case ZRD_ROLE_TAG:
            fixed = p->type == ZRD_CLOSXP || p->type == ZRD_PROMSXP ? "@env" : "@tag";
            break;
        case ZRD_ROLE_CDR:
            fixed = p->type == ZRD_CLOSXP ? "@body" : p->type == ZRD_PROMSXP ? "@code" : "@cdr";
            break;
        case ZRD_ROLE_BC_CONST:
            snprintf(seg, sizeof seg, "@consts[[%lld]]", (long long) f->index + 1);
            append(buf, cap, &len, seg, strlen(seg));
            break;
        case ZRD_ROLE_ATTR: case ZRD_ROLE_ALTREP_ATTR: fixed = "@attr"; break;
        case ZRD_ROLE_PRINTNAME: fixed = "@name"; break;
        case ZRD_ROLE_ENCLOS: fixed = "@enclos"; break;
        case ZRD_ROLE_FRAME: fixed = "@frame"; break;
        case ZRD_ROLE_HASHTAB: fixed = "@hashtab"; break;
        case ZRD_ROLE_PROT: fixed = "@prot"; break;
        case ZRD_ROLE_EXTTAG: fixed = "@tag"; break;
        case ZRD_ROLE_ALTREP_INFO: fixed = "@info"; break;
        case ZRD_ROLE_ALTREP_STATE: fixed = "@state"; break;
        case ZRD_ROLE_BC_CODE: fixed = "@code"; break;
        case ZRD_ROLE_BC_LANG: fixed = "@lang"; break;
        default: break;
        }
        if (fixed) append(buf, cap, &len, fixed, strlen(fixed));
        if (f->lead) append(buf, cap, &len, f->lead, strlen(f->lead));
    }
}
