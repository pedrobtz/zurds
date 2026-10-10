/* See zrd_walk.h. No R headers: this file is part of the standalone build.
 *
 * The walk keeps one frame per open item. The loop looks at the top frame:
 * if the item has another child to read, it begins that child (pushing a
 * frame); otherwise it ends the item (popping it). A pairlist's next cell
 * reuses its frame, as ReadItem_Iterative() reuses its loop, so a long
 * pairlist does not deepen the stack. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <zufast/bits.h>

#include "zrd_walk.h"

/* What a frame reads next. */
typedef enum {
    PH_DONE = 0,
    PH_ELEMS,      /* the elements of a STRSXP, VECSXP or EXPRSXP */
    PH_ATTR,       /* the attribute pairlist */
    PH_TAG,        /* a cell's tag */
    PH_CAR,        /* a cell's value */
    PH_CDR,        /* a cell's rest: the next cell or the end */
    PH_PRINTNAME   /* a symbol's CHARSXP */
} zrd_phase;

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

/* serialize.c, InInteger (XDR: R_XDRDecodeInteger). */
static zrd_status in_int(zrd_walk *w, int32_t *out)
{
    uint8_t b[4];
    if (zrd_reader_read(w->in, b, 4) != 4)
        return fail(w, ZRD_ERR_TRUNCATED, "the stream ends inside an integer");
    uint32_t u = zuf_load_be32(b);
    *out = u <= INT32_MAX ? (int32_t) u : -(int32_t) (~u) - 1;
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

/* The bytes an XDR vector of `n` elements of `type` takes, or 0 for a
 * type whose elements are items. serialize.c, InIntegerVec, InRealVec,
 * InComplexVec, and the RAWSXP case of ReadItem_Recursive. */
static uint64_t xdr_elt_size(int type)
{
    switch (type) {
    case ZRD_LGLSXP: case ZRD_INTSXP: return 4;
    case ZRD_REALSXP: return 8;
    case ZRD_CPLXSXP: return 16;
    case ZRD_RAWSXP: return 1;
    default: return 0;
    }
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
    if (f->type == ZRD_LISTSXP)
        return ZRD_HAS_TAG(f->flags) ? PH_TAG : PH_CAR;
    return PH_DONE;
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
            if (zrd_reader_read(w->in, w->arena + off, (size_t) len) != (size_t) len)
                return fail(w, ZRD_ERR_TRUNCATED, "the stream ends inside a string");
            parent->has_name = 1;
            parent->name_off = off;
            parent->name_len = (size_t) len;
        } else if (len > 0 && (st = skip(w, (uint64_t) len, "a string"))) {
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
        uint64_t size = xdr_elt_size(f->type);
        if ((st = check_size(w, (double) len * (double) size))) return st;
        if ((st = emit(w, ZRD_EVENT_BEGIN, f, w->depth - 1, parent, 0, 0))) return st;
        if ((st = skip(w, (uint64_t) len * size, "a vector"))) return st;
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
     * cdr as the next item. */
    case ZRD_LISTSXP:
        f->phase = first_cell_phase(f);
        break;

    /* Walked from Stage 3 on. */
    case ZRD_CLOSXP: case ZRD_ENVSXP: case ZRD_PROMSXP: case ZRD_LANGSXP:
    case ZRD_SPECIALSXP: case ZRD_BUILTINSXP: case ZRD_DOTSXP:
    case ZRD_BCODESXP: case ZRD_EXTPTRSXP: case ZRD_WEAKREFSXP:
    case ZRD_OBJSXP: case ZRD_NAMESPACESXP: case ZRD_PACKAGESXP:
    case ZRD_PERSISTSXP: case ZRD_ALTREP_SXP: {
        char msg[96];
        snprintf(msg, sizeof msg, "item type %d is not walked yet", f->type);
        return fail(w, ZRD_ERR_UNSUPPORTED, msg);
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

/* Advances the top frame by one child, or ends it. */
static zrd_status step(zrd_walk *w)
{
    zrd_frame *f = &w->stack[w->depth - 1];
    zrd_status st;
    switch (f->phase) {
    case PH_ELEMS: {
        int64_t i = f->next++;
        if (f->next >= f->length)
            f->phase = ZRD_HAS_ATTR(f->flags) ? PH_ATTR : PH_DONE;
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
        if (ZRD_TYPE(flags) == ZRD_LISTSXP) {
            /* The next cell: end this one and reuse the frame. */
            int64_t cell = f->next + 1;
            if ((st = end(w, offset))) return st;
            if ((st = begin(w, flags, offset, ZRD_ROLE_CELL, cell))) return st;
            w->stack[w->depth - 1].next = cell;
            return ZRD_OK;
        }
        f->phase = PH_DONE;
        return begin(w, flags, offset, ZRD_ROLE_CDR, f->next);
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

void zrd_walk_free(zrd_walk *w)
{
    free(w->stack);
    free(w->refs);
    free(w->arena);
    w->stack = NULL;
    w->refs = NULL;
    w->arena = NULL;
}

/* serialize.c, InFormat and the version switch of R_Unserialize. */
zrd_status zrd_walk_header(zrd_walk *w)
{
    uint8_t fmt[2];
    if (zrd_reader_read(w->in, fmt, 2) != 2)
        return fail(w, ZRD_ERR_TRUNCATED, "the stream ends inside its header");
    if (fmt[1] != '\n' || (fmt[0] != 'X' && fmt[0] != 'B' && fmt[0] != 'A'))
        return fail(w, ZRD_ERR_FORMAT, "unknown stream format");
    w->header.format = fmt[0];
    if (fmt[0] != ZRD_FORMAT_XDR)
        return fail(w, ZRD_ERR_UNSUPPORTED, "only the XDR format is walked yet");
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
        if (zrd_reader_read(w->in, w->header.native_encoding, (size_t) n) != (size_t) n)
            return fail(w, ZRD_ERR_TRUNCATED, "the stream ends inside its header");
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
    for (int i = 1; i < w->depth; i++) {
        const zrd_frame *f = &w->stack[i], *p = &w->stack[i - 1];
        char seg[48];
        switch (f->role) {
        case ZRD_ROLE_ELEMENT:
            snprintf(seg, sizeof seg, "[[%lld]]", (long long) f->index + 1);
            append(buf, cap, &len, seg, strlen(seg));
            break;
        case ZRD_ROLE_CAR:
            if (p->has_name) {
                append(buf, cap, &len, "$", 1);
                append(buf, cap, &len, w->arena + p->name_off, p->name_len);
            } else {
                snprintf(seg, sizeof seg, "[[%lld]]", (long long) f->index + 1);
                append(buf, cap, &len, seg, strlen(seg));
            }
            break;
        case ZRD_ROLE_ATTR: append(buf, cap, &len, "@attr", 5); break;
        case ZRD_ROLE_TAG: append(buf, cap, &len, "@tag", 4); break;
        case ZRD_ROLE_CDR: append(buf, cap, &len, "@cdr", 4); break;
        case ZRD_ROLE_PRINTNAME: append(buf, cap, &len, "@name", 5); break;
        default: break;
        }
    }
}
