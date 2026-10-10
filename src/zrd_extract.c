/* Extraction (design sections 2, 4 and 5): finding an item by path, and
 * for rds_extract(use_r = TRUE), making its bytes a stream of their own.
 *
 * The format has no index: an item is found by walking from the start,
 * which also keeps the reference table right. A list's names follow its
 * elements, so each step of a path is a walk that records the children of
 * the item at a known offset: where each element begins, and the names.
 *
 * An item's bytes refer back, by REFSXP, to symbols read before it. For
 * unserialize() to read them alone, each such reference is replaced by the
 * symbol's definition, and every reference inside the item is renumbered
 * to match. A reference to anything else read before the item (an
 * environment, say) cannot be resolved, and the item is refused. */
#include <stdlib.h>
#include <string.h>

#include "zrd.h"
#include "zrd_scan.h"
#include "zrd_source.h"
#include "zrd_walk.h"

static void tick(void *ctx)
{
    (void) ctx;
    R_CheckUserInterrupt();
}

static void read_limits(SEXP limits, zrd_limits *lim)
{
    lim->max_depth = Rf_asInteger(VECTOR_ELT(limits, 0));
    lim->max_items = Rf_asReal(VECTOR_ELT(limits, 1));
    lim->max_size = Rf_asReal(VECTOR_ELT(limits, 2));
}

static void skip_magic(zrd_source *s, SEXP skip)
{
    size_t nskip = (size_t) Rf_asInteger(skip);
    if (zrd_reader_read(&s->reader, NULL, nskip) != nskip)
        Rf_error("internal error: the input is shorter than its magic line");
}

static SEXP walk_result(const zrd_walk *w, const zrd_source *s, zrd_status st,
                        const char **extra_names, int nextra)
{
    const char *base[] = {"status", "format", "version", "bytes", "offset", "path", "detail"};
    int n = 7 + nextra;
    SEXP names = PROTECT(Rf_allocVector(STRSXP, n));
    for (int i = 0; i < 7; i++) SET_STRING_ELT(names, i, Rf_mkChar(base[i]));
    for (int i = 0; i < nextra; i++) SET_STRING_ELT(names, 7 + i, Rf_mkChar(extra_names[i]));
    SEXP out = PROTECT(Rf_allocVector(VECSXP, n));
    Rf_setAttrib(out, R_NamesSymbol, names);
    char path[512];
    zrd_walk_path(w, path, sizeof path);
    char fmt[2] = {(char) w->header.format, '\0'};
    SET_VECTOR_ELT(out, 0, Rf_mkString(zrd_status_name(st)));
    SET_VECTOR_ELT(out, 1, Rf_mkString(fmt));
    SET_VECTOR_ELT(out, 2, Rf_ScalarInteger(w->header.version));
    SET_VECTOR_ELT(out, 3, Rf_ScalarReal((double) (zrd_reader_offset(&s->reader) - w->base)));
    SET_VECTOR_ELT(out, 4, Rf_ScalarReal(st == ZRD_OK ? NA_REAL : (double) w->err_offset));
    SET_VECTOR_ELT(out, 5, Rf_mkString(path));
    SET_VECTOR_ELT(out, 6, Rf_mkString(st == ZRD_OK ? "" : w->err_detail));
    UNPROTECT(2);
    return out;
}

/* ---- the children of an item --------------------------------------- */

typedef struct {
    zrd_walk w;
    double target;          /* the item's offset; -1 for the root */
    int found, depth;
    int type;
    int64_t length;
    double *offsets;        /* element i begins at offsets[i] */
    int64_t noffsets;
    char **names;           /* element names, from the names attribute */
    size_t *name_lens;
    int64_t nnames;
    int in_attr, in_names;
    int nomem;
} zrd_children;

static void children_free(zrd_children *c)
{
    zrd_walk_free(&c->w);
    if (c->names)
        for (int64_t i = 0; i < c->nnames; i++) free(c->names[i]);
    free(c->names);
    free(c->name_lens);
    free(c->offsets);
    free(c);
}

static void children_finalizer(SEXP ptr)
{
    zrd_children *c = R_ExternalPtrAddr(ptr);
    if (c) children_free(c);
    R_ClearExternalPtr(ptr);
}

static int children_event(void *ctx, const zrd_event *ev)
{
    zrd_children *c = ctx;
    if (!c->found) {
        if (ev->kind != ZRD_EVENT_BEGIN) return 0;
        if (c->target < 0 ? ev->depth != 0 : (double) ev->offset != c->target) return 0;
        c->found = 1;
        c->depth = ev->depth;
        c->type = ev->type;
        c->length = ev->length;
        if ((ev->type == ZRD_VECSXP || ev->type == ZRD_EXPRSXP) && ev->length > 0) {
            c->offsets = malloc((size_t) ev->length * sizeof *c->offsets);
            c->names = calloc((size_t) ev->length, sizeof *c->names);
            c->name_lens = calloc((size_t) ev->length, sizeof *c->name_lens);
            if (!c->offsets || !c->names || !c->name_lens) return c->nomem = 1;
        }
        return 0;
    }
    int rel = ev->depth - c->depth;
    if (rel == 0 && ev->kind == ZRD_EVENT_END) return 1; /* done */
    if (rel == 1 && ev->kind == ZRD_EVENT_BEGIN) {
        if (ev->role == ZRD_ROLE_ELEMENT && c->offsets && ev->index < c->length) {
            c->offsets[ev->index] = (double) ev->offset;
            c->noffsets = ev->index + 1;
        }
        if (ev->role != ZRD_ROLE_CELL) c->in_attr = ev->role == ZRD_ROLE_ATTR;
    }
    if (rel == 2 && ev->kind == ZRD_EVENT_BEGIN && ev->role == ZRD_ROLE_CAR)
        c->in_names = c->in_attr && ev->name_len == 5 && memcmp(ev->name, "names", 5) == 0;
    if (rel == 3 && ev->kind == ZRD_EVENT_END && c->in_names && ev->type == ZRD_CHARSXP &&
        ev->role == ZRD_ROLE_ELEMENT && c->names && ev->index < c->length && ev->data &&
        ev->length >= 0) {
        /* The whole name, not the walk's first bytes only. */
        if (!ev->data_complete) return 0;
        char *p = malloc(ev->data_len ? ev->data_len : 1);
        if (p == NULL) return c->nomem = 1;
        memcpy(p, ev->data, ev->data_len);
        c->names[ev->index] = p;
        c->name_lens[ev->index] = ev->data_len;
        if (ev->index + 1 > c->nnames) c->nnames = ev->index + 1;
    }
    return 0;
}

/* The children of the item at `target` (-1: the root): its type, length,
 * where each element begins, and its names (NA where it has none, or a
 * name too long to keep). */
SEXP zrd_children_walk(SEXP src, SEXP skip, SEXP limits, SEXP target)
{
    zrd_source *s = zrd_source_get(src);
    skip_magic(s, skip);
    zrd_limits lim;
    read_limits(limits, &lim);
    zrd_children *c = calloc(1, sizeof *c);
    if (c == NULL) Rf_error("cannot allocate the walk");
    SEXP ptr = PROTECT(R_MakeExternalPtr(c, R_NilValue, src));
    R_RegisterCFinalizerEx(ptr, children_finalizer, TRUE);
    c->target = Rf_asReal(target);
    zrd_status st = zrd_walk_init(&c->w, &s->reader, &lim, children_event, c, tick, NULL);
    if (st == ZRD_OK) st = zrd_walk_capture_strings(&c->w, 65536);
    if (st == ZRD_OK) st = zrd_walk_header(&c->w);
    if (st == ZRD_OK) st = zrd_walk_item(&c->w);
    if (c->nomem) Rf_error("cannot allocate the item's children");
    if (st == ZRD_STOPPED) st = ZRD_OK;

    const char *extra[] = {"found", "type", "length", "offsets", "names"};
    SEXP out = PROTECT(walk_result(&c->w, s, st, extra, 5));
    SET_VECTOR_ELT(out, 7, Rf_ScalarLogical(c->found));
    SET_VECTOR_ELT(out, 8, Rf_ScalarInteger(c->type));
    SET_VECTOR_ELT(out, 9, Rf_ScalarReal((double) c->length));
    SEXP offs = Rf_allocVector(REALSXP, (R_xlen_t) c->noffsets);
    SET_VECTOR_ELT(out, 10, offs);
    if (c->noffsets) memcpy(REAL(offs), c->offsets, (size_t) c->noffsets * sizeof(double));
    SEXP nms = Rf_allocVector(STRSXP, (R_xlen_t) c->noffsets);
    SET_VECTOR_ELT(out, 11, nms);
    for (int64_t i = 0; i < c->noffsets; i++) {
        SEXP v = NA_STRING;
        if (i < c->nnames && c->names[i] && !memchr(c->names[i], '\0', c->name_lens[i]))
            v = Rf_mkCharLenCE(c->names[i], (int) c->name_lens[i], CE_UTF8);
        SET_STRING_ELT(nms, (R_xlen_t) i, v);
    }
    UNPROTECT(2);
    return out;
}

/* ---- a standalone stream for one item ------------------------------ */

typedef struct {
    uint64_t offset, len;   /* bytes of the item replaced */
    size_t rep_off, rep_len;/* by these bytes of `rep` */
} zrd_patch;

typedef struct {
    zrd_walk w;
    zrd_scan scan;
    double target;
    int found, depth;
    uint64_t header_end, item_begin, item_end;
    size_t first_inside;    /* references read before the item */
    size_t seen_refs;       /* the walk's table size last seen */
    int64_t *map;           /* old reference index -> new, 0 unset */
    size_t map_cap;
    int64_t next_new;       /* references in the new stream so far */
    zrd_patch *patches;
    size_t npatches, patches_cap;
    uint8_t *rep;
    size_t rep_len, rep_cap;
    uint64_t ref_begin;     /* where the current REFSXP began */
    int pending_end;        /* the item's cell ended; a next cell may follow */
    int outside;            /* a reference to a non-symbol read before */
    char outside_path[512];
    int nomem;
} zrd_patcher;

static void patcher_free(zrd_patcher *p)
{
    zrd_walk_free(&p->w);
    zrd_scan_free(&p->scan);
    free(p->map);
    free(p->patches);
    free(p->rep);
    free(p);
}

static void patcher_finalizer(SEXP ptr)
{
    zrd_patcher *p = R_ExternalPtrAddr(ptr);
    if (p) patcher_free(p);
    R_ClearExternalPtr(ptr);
}

static int rep_put(zrd_patcher *p, const void *bytes, size_t n)
{
    if (p->rep_cap - p->rep_len < n) {
        size_t cap = p->rep_cap ? p->rep_cap : 256;
        while (cap - p->rep_len < n) cap *= 2;
        uint8_t *q = realloc(p->rep, cap);
        if (q == NULL) return 1;
        p->rep = q;
        p->rep_cap = cap;
    }
    memcpy(p->rep + p->rep_len, bytes, n);
    p->rep_len += n;
    return 0;
}

/* An integer as the stream's format writes it (serialize.c, OutInteger). */
static int put_int(zrd_patcher *p, int32_t v)
{
    if (p->w.header.format == ZRD_FORMAT_ASCII) {
        char buf[24];
        int n = snprintf(buf, sizeof buf, "%d\n", (int) v);
        return rep_put(p, buf, (size_t) n);
    }
    uint32_t u = (uint32_t) v;
    uint8_t b[4];
    int big = p->w.header.format == ZRD_FORMAT_XDR || p->w.big_endian;
    for (int i = 0; i < 4; i++) b[big ? i : 3 - i] = (uint8_t) (u >> (24 - 8 * i));
    return rep_put(p, b, 4);
}

/* A string as the stream's format writes it (serialize.c, OutString). */
static int put_string(zrd_patcher *p, const char *s, size_t n)
{
    if (p->w.header.format != ZRD_FORMAT_ASCII) return rep_put(p, s, n);
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char) s[i];
        char buf[8];
        int k;
        switch (c) {
        case '\n': k = snprintf(buf, sizeof buf, "\\n"); break;
        case '\t': k = snprintf(buf, sizeof buf, "\\t"); break;
        case '\\': k = snprintf(buf, sizeof buf, "\\\\"); break;
        case '"': k = snprintf(buf, sizeof buf, "\\\""); break;
        default:
            if (c <= 32 || c > 126) k = snprintf(buf, sizeof buf, "\\%03o", c);
            else { buf[0] = (char) c; k = 1; }
        }
        if (rep_put(p, buf, (size_t) k)) return 1;
    }
    return rep_put(p, "\n", 1);
}

/* serialize.c, OutRefIndex. */
static int put_ref(zrd_patcher *p, int64_t i)
{
    if (i <= (INT32_MAX >> 8)) return put_int(p, (int32_t) ((i << 8) | 255));
    return put_int(p, 255) || put_int(p, (int32_t) i);
}

static int map_set(zrd_patcher *p, size_t old, int64_t nw)
{
    if (old >= p->map_cap) {
        size_t cap = p->map_cap ? p->map_cap : 64;
        while (cap <= old) cap *= 2;
        int64_t *q = realloc(p->map, cap * sizeof *q);
        if (q == NULL) return 1;
        memset(q + p->map_cap, 0, (cap - p->map_cap) * sizeof *q);
        p->map = q;
        p->map_cap = cap;
    }
    p->map[old] = nw;
    return 0;
}

/* Gives the references the walk added since last seen their new indices,
 * in the order they were made. */
static int track_refs(zrd_patcher *p)
{
    while (p->seen_refs < p->w.nrefs) {
        p->seen_refs++;
        if (map_set(p, p->seen_refs, ++p->next_new)) return 1;
    }
    return 0;
}

static int patch_event(void *ctx, const zrd_event *ev)
{
    zrd_patcher *p = ctx;
    if (!p->found) {
        if (ev->kind != ZRD_EVENT_BEGIN) return 0;
        if (p->target < 0 ? ev->depth != 0 : (double) ev->offset != p->target) return 0;
        p->found = 1;
        p->depth = ev->depth;
        p->item_begin = ev->offset;
        /* References made before the item, except one the item itself
         * made as it began (an environment registers before its body). */
        p->first_inside = p->w.nrefs;
        if (ev->type == ZRD_ENVSXP || ev->type == ZRD_EXTPTRSXP || ev->type == ZRD_WEAKREFSXP)
            p->first_inside--;
        p->seen_refs = p->first_inside;
    }
    zrd_event rel = *ev;
    rel.depth -= p->depth;
    /* A pairlist-family item (a closure, a call) goes on in cells at its
     * own depth: it ends when one ends and no next cell begins. */
    if (p->pending_end) {
        if (ev->kind == ZRD_EVENT_BEGIN && rel.depth == 0 && ev->role == ZRD_ROLE_CELL)
            p->pending_end = 0;
        else
            return 1;
    }
    if (rel.depth == 0 && ev->role != ZRD_ROLE_CELL) rel.role = ZRD_ROLE_ROOT;
    if (zrd_scan_event(&p->scan, &rel)) return p->nomem = 1;

    if (ev->kind == ZRD_EVENT_BEGIN && ev->type == ZRD_REFSXP) p->ref_begin = ev->offset;
    if (ev->kind == ZRD_EVENT_END && ev->type == ZRD_REFSXP) {
        int64_t old = ev->ref, nw;
        if ((size_t) old > p->first_inside) {
            nw = p->map[old];
        } else if (ev->ref_type == ZRD_SYMSXP && (size_t) old < p->map_cap && p->map[old]) {
            nw = p->map[old];
        } else if (ev->ref_type == ZRD_SYMSXP) {
            nw = 0;
        } else {
            p->outside = 1;
            zrd_walk_path(&p->w, p->outside_path, sizeof p->outside_path);
            return 1;
        }
        if (p->npatches == p->patches_cap) {
            size_t cap = p->patches_cap ? 2 * p->patches_cap : 32;
            zrd_patch *q = realloc(p->patches, cap * sizeof *q);
            if (q == NULL) return p->nomem = 1;
            p->patches = q;
            p->patches_cap = cap;
        }
        zrd_patch *pt = &p->patches[p->npatches++];
        pt->offset = p->ref_begin;
        pt->len = ev->offset - p->ref_begin;
        pt->rep_off = p->rep_len;
        if (nw == 0) {
            /* The symbol's definition: SYMSXP, then its name as a CHARSXP
             * (ASCII-marked when it is, else UTF-8). */
            const zrd_ref *r = &p->w.refs[old - 1];
            const char *name = p->w.arena + r->name_off;
            int ascii = 1;
            for (size_t i = 0; i < r->name_len; i++) ascii &= (unsigned char) name[i] < 128;
            if (put_int(p, ZRD_SYMSXP) ||
                put_int(p, ZRD_CHARSXP | ((ascii ? 64 : 8) << 12)) ||
                put_int(p, (int32_t) r->name_len) || put_string(p, name, r->name_len))
                return p->nomem = 1;
            nw = ++p->next_new;
            if (map_set(p, (size_t) old, nw)) return p->nomem = 1;
        } else if (put_ref(p, nw)) {
            return p->nomem = 1;
        }
        pt->rep_len = p->rep_len - pt->rep_off;
    }
    if (track_refs(p)) return p->nomem = 1;
    if (ev->kind == ZRD_EVENT_END && rel.depth == 0) {
        p->item_end = ev->offset;
        if (ZRD_IS_PAIRLIST(ev->type)) p->pending_end = 1;
        else return 1;
    }
    return 0;
}

/* For rds_extract(use_r = TRUE): where the item at `target` lies, the
 * patches that make it standalone, and the scanner's findings for it. */
SEXP zrd_extract_patch(SEXP src, SEXP skip, SEXP limits, SEXP target)
{
    zrd_source *s = zrd_source_get(src);
    skip_magic(s, skip);
    zrd_limits lim;
    read_limits(limits, &lim);
    zrd_patcher *p = calloc(1, sizeof *p);
    if (p == NULL) Rf_error("cannot allocate the walk");
    SEXP ptr = PROTECT(R_MakeExternalPtr(p, R_NilValue, src));
    R_RegisterCFinalizerEx(ptr, patcher_finalizer, TRUE);
    p->target = Rf_asReal(target);
    if (zrd_scan_init(&p->scan, &p->w, lim.max_depth)) Rf_error("cannot allocate the scanner");
    zrd_status st = zrd_walk_init(&p->w, &s->reader, &lim, patch_event, p, tick, NULL);
    if (st == ZRD_OK) st = zrd_walk_capture_strings(&p->w, 4096);
    if (st == ZRD_OK) st = zrd_walk_header(&p->w);
    p->header_end = zrd_reader_offset(&s->reader) - p->w.base;
    if (st == ZRD_OK) st = zrd_walk_item(&p->w);
    if (p->nomem) Rf_error("cannot allocate the extraction");
    if (st == ZRD_STOPPED && !p->outside) st = ZRD_OK;

    const char *extra[] = {"found", "outside", "outside_path", "header_end",
                           "item_begin", "item_end", "patch_offset", "patch_length",
                           "patch_bytes", "findings"};
    SEXP out = PROTECT(walk_result(&p->w, s, st == ZRD_STOPPED ? ZRD_OK : st, extra, 10));
    SET_VECTOR_ELT(out, 7, Rf_ScalarLogical(p->found));
    SET_VECTOR_ELT(out, 8, Rf_ScalarLogical(p->outside));
    SET_VECTOR_ELT(out, 9, Rf_mkString(p->outside_path));
    SET_VECTOR_ELT(out, 10, Rf_ScalarReal((double) p->header_end));
    SET_VECTOR_ELT(out, 11, Rf_ScalarReal((double) p->item_begin));
    SET_VECTOR_ELT(out, 12, Rf_ScalarReal((double) p->item_end));
    R_xlen_t n = (R_xlen_t) p->npatches;
    SEXP po = Rf_allocVector(REALSXP, n), pl = Rf_allocVector(REALSXP, n);
    SET_VECTOR_ELT(out, 13, po);
    SET_VECTOR_ELT(out, 14, pl);
    SEXP pb = Rf_allocVector(VECSXP, n);
    SET_VECTOR_ELT(out, 15, pb);
    for (R_xlen_t i = 0; i < n; i++) {
        REAL(po)[i] = (double) p->patches[i].offset;
        REAL(pl)[i] = (double) p->patches[i].len;
        SEXP r = Rf_allocVector(RAWSXP, (R_xlen_t) p->patches[i].rep_len);
        SET_VECTOR_ELT(pb, i, r);
        if (p->patches[i].rep_len) memcpy(RAW(r), p->rep + p->patches[i].rep_off, p->patches[i].rep_len);
    }
    SET_VECTOR_ELT(out, 16, zrd_scan_findings(&p->scan));
    UNPROTECT(2);
    return out;
}
