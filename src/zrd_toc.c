/* rds_toc(): one row per object of an .rda (design section 5), gathered
 * from the walk's events as they pass. An .rda is a pairlist whose cells
 * (depth 0) carry the objects (depth 1) with their names as tags; of each
 * object only its type, length, offsets and three attributes are kept:
 * the first class, dim, and row.names (for a data frame's row count).
 * Memory grows with the number of objects, never with their contents
 * (roadmap Stage 5). Everything is owned by an external pointer. */
#include <stdlib.h>
#include <string.h>

#include "zrd.h"
#include "zrd_scan.h"
#include "zrd_source.h"
#include "zrd_walk.h"

#define DIM_CAP ZRD_INT_CAP

typedef struct {
    size_t name_off, name_len;
    int type, flags;
    int64_t length;
    uint64_t begin, end;
    int64_t class_off;     /* -1: no class */
    size_t class_len;
    int class_flags;
    int ndim;
    int32_t dim[DIM_CAP];
    int64_t nrow;          /* from row.names; -1 unknown */
    int64_t altrep_off;    /* an ALTREP object's class name; -1 none */
    size_t altrep_len;
    double altrep_n;       /* its length, from its state; -1 unknown */
    int64_t cells;         /* a pairlist-family object's cells */
    size_t risky_before;   /* the scanner's risky count when it began */
    int risky;
} zrd_obj;

typedef enum { ATTR_NONE = 0, ATTR_CLASS, ATTR_DIM, ATTR_ROWNAMES, ATTR_OTHER } zrd_attr_kind;

typedef struct {
    zrd_walk w;
    zrd_scan scan;         /* for the risky column */
    zrd_obj *objs;
    size_t n, cap;
    char *text;
    size_t text_len, text_cap;
    int obj_open;          /* an object (depth 1) is being walked */
    int in_attr;           /* depth 2: walking that object's attribute cells */
    int in_info;           /* depth 2: walking an ALTREP object's class info */
    int in_state;          /* depth 2: walking an ALTREP object's state */
    zrd_attr_kind attr;    /* depth 3: the attribute being walked */
    int failed_alloc;
    int root;              /* the stream's root is the one object: a
                              lazy-load entry (rds_lazyload_toc()) */
} zrd_toc;

static void toc_free(zrd_toc *t)
{
    zrd_walk_free(&t->w);
    zrd_scan_free(&t->scan);
    free(t->objs);
    free(t->text);
    free(t);
}

static void toc_finalizer(SEXP ptr)
{
    zrd_toc *t = R_ExternalPtrAddr(ptr);
    if (t) toc_free(t);
    R_ClearExternalPtr(ptr);
}

static void tick(void *ctx)
{
    (void) ctx;
    R_CheckUserInterrupt();
}

static int64_t keep_text(zrd_toc *t, const char *s, size_t n)
{
    if (t->text_cap - t->text_len < n) {
        size_t cap = t->text_cap ? t->text_cap : 1024;
        while (cap - t->text_len < n) cap *= 2;
        char *p = realloc(t->text, cap);
        if (p == NULL) return -1;
        t->text = p;
        t->text_cap = cap;
    }
    if (n) memcpy(t->text + t->text_len, s, n);
    t->text_len += n;
    return (int64_t) (t->text_len - n);
}

static zrd_attr_kind attr_kind(const zrd_event *ev)
{
    if (ev->name == NULL) return ATTR_OTHER;
    if (ev->name_len == 5 && memcmp(ev->name, "class", 5) == 0) return ATTR_CLASS;
    if (ev->name_len == 3 && memcmp(ev->name, "dim", 3) == 0) return ATTR_DIM;
    if (ev->name_len == 9 && memcmp(ev->name, "row.names", 9) == 0) return ATTR_ROWNAMES;
    return ATTR_OTHER;
}

static int on_event(void *ctx, const zrd_event *ev)
{
    zrd_toc *t = ctx;
    size_t risky_now = t->scan.risky;
    if (zrd_scan_event(&t->scan, ev)) { t->failed_alloc = 1; return 1; }
    /* A lone object is seen as an .rda's would be: one level down. */
    zrd_event shifted;
    if (t->root) {
        shifted = *ev;
        shifted.depth += 1;
        if (shifted.depth == 1 && ev->role == ZRD_ROLE_ROOT) shifted.role = ZRD_ROLE_CAR;
        ev = &shifted;
    }
    zrd_obj *o = t->n ? &t->objs[t->n - 1] : NULL;
    /* An object is risky if the scanner found, inside it, anything that
     * runs code or reaches out. */
    if (ev->depth == 1 && ev->kind == ZRD_EVENT_END &&
        (ev->role == ZRD_ROLE_CAR || ev->role == ZRD_ROLE_CELL) && o)
        o->risky = t->scan.risky > o->risky_before;
    if (ev->depth > 4) return 0;

    /* A pairlist-family object's later cells (serialize.c,
     * ReadItem_Iterative): count them, and end the object at the last. */
    if (ev->depth == 1 && ev->role == ZRD_ROLE_CELL && o) {
        if (ev->kind == ZRD_EVENT_BEGIN) {
            o->cells++;
            t->obj_open = 1;
            t->in_attr = t->in_info = t->in_state = 0;
        } else {
            o->end = ev->offset;
            t->obj_open = 0;
        }
        return 0;
    }
    if (ev->depth == 1 && ev->role == ZRD_ROLE_CAR) {
        if (ev->kind == ZRD_EVENT_END) {
            if (o) o->end = ev->offset;
            t->obj_open = 0;
            return 0;
        }
        if (t->n == t->cap) {
            size_t cap = t->cap ? 2 * t->cap : 64;
            zrd_obj *p = realloc(t->objs, cap * sizeof *p);
            if (p == NULL) { t->failed_alloc = 1; return 1; }
            t->objs = p;
            t->cap = cap;
        }
        o = &t->objs[t->n++];
        memset(o, 0, sizeof *o);
        int64_t off = keep_text(t, ev->name ? ev->name : "", ev->name ? ev->name_len : 0);
        if (off < 0) { t->failed_alloc = 1; return 1; }
        o->name_off = (size_t) off;
        o->name_len = ev->name ? ev->name_len : 0;
        /* An object that is a reference is what it refers to: a symbol
         * already seen, or an environment. */
        o->type = ev->type == ZRD_REFSXP ? ev->ref_type : ev->type;
        o->flags = ev->flags;
        o->length = ev->length;
        o->begin = ev->offset;
        o->class_off = -1;
        o->altrep_off = -1;
        o->nrow = -1;
        o->altrep_n = -1;
        o->cells = 1;
        o->risky_before = risky_now;
        t->obj_open = 1;
        t->in_attr = t->in_info = t->in_state = 0;
        return 0;
    }
    if (!t->obj_open || o == NULL) return 0;

    /* The object's attribute cells, or an ALTREP object's class info. */
    if (ev->depth == 2 && ev->kind == ZRD_EVENT_BEGIN) {
        /* A pairlist's later cells continue what its first began. */
        if (ev->role == ZRD_ROLE_CELL) return 0;
        t->in_attr = ev->role == ZRD_ROLE_ATTR || ev->role == ZRD_ROLE_ALTREP_ATTR;
        t->in_info = ev->role == ZRD_ROLE_ALTREP_INFO;
        t->in_state = ev->role == ZRD_ROLE_ALTREP_STATE;
        return 0;
    }
    /* An ALTREP object's length, from its state: a compact sequence's is
     * c(n, start, step); a deferred string's is the pairlist (vector,
     * info); a wrapper's is list(vector, meta) (altclasses.c). */
    if (t->in_state && o->altrep_n < 0) {
        if (ev->depth == 2 && ev->kind == ZRD_EVENT_END && ev->type == ZRD_REALSXP &&
            ev->reals && ev->nreals >= 1)
            o->altrep_n = ev->reals[0];
        else if (ev->depth == 3 && ev->kind == ZRD_EVENT_BEGIN && ev->index == 0 &&
                 (ev->role == ZRD_ROLE_CAR || ev->role == ZRD_ROLE_ELEMENT) &&
                 ev->length >= 0)
            o->altrep_n = (double) ev->length;
        /* The vector behind a deferred string or a wrapper may itself be a
         * compact sequence. */
        else if (ev->depth == 4 && ev->kind == ZRD_EVENT_END && ev->type == ZRD_REALSXP &&
                 ev->role == ZRD_ROLE_ALTREP_STATE && ev->reals && ev->nreals >= 1)
            o->altrep_n = ev->reals[0];
        return 0;
    }
    if (t->in_info && o->altrep_off < 0 && ev->kind == ZRD_EVENT_END && ev->data &&
        (ev->type == ZRD_CHARSXP || ev->type == ZRD_REFSXP)) {
        int64_t off = keep_text(t, ev->data, ev->data_len);
        if (off < 0) { t->failed_alloc = 1; return 1; }
        o->altrep_off = off;
        o->altrep_len = ev->data_len;
        return 0;
    }
    if (!t->in_attr) return 0;

    if (ev->depth == 3 && ev->role == ZRD_ROLE_CAR) {
        if (ev->kind == ZRD_EVENT_BEGIN) {
            t->attr = attr_kind(ev);
            if (t->attr == ATTR_ROWNAMES && ev->type == ZRD_STRSXP) o->nrow = ev->length;
            return 0;
        }
        if (t->attr == ATTR_DIM && ev->type == ZRD_INTSXP && ev->ints) {
            o->ndim = ev->nints;
            memcpy(o->dim, ev->ints, (size_t) ev->nints * sizeof *ev->ints);
        } else if (t->attr == ATTR_ROWNAMES && ev->type == ZRD_INTSXP) {
            /* Compact row names, c(NA, -n) or c(NA, n) (attrib.c), or the
             * row numbers themselves. */
            if (ev->ints && ev->nints == 2 && ev->ints[0] == INT32_MIN)
                o->nrow = ev->ints[1] < 0 ? -(int64_t) ev->ints[1] : ev->ints[1];
            else
                o->nrow = ev->length;
        }
        t->attr = ATTR_NONE;
        return 0;
    }
    /* A dim that is a compact sequence, as array(x, 2:4) leaves it: its
     * state is c(n, start, step). */
    if (ev->depth == 4 && t->attr == ATTR_DIM && ev->kind == ZRD_EVENT_END &&
        ev->type == ZRD_REALSXP && ev->role == ZRD_ROLE_ALTREP_STATE &&
        ev->reals && ev->nreals == 3 && ev->reals[0] >= 0 && ev->reals[0] <= DIM_CAP) {
        o->ndim = (int) ev->reals[0];
        for (int k = 0; k < o->ndim; k++)
            o->dim[k] = (int32_t) (ev->reals[1] + k * ev->reals[2]);
        return 0;
    }
    if (ev->depth == 4 && t->attr == ATTR_CLASS && o->class_off < 0 &&
        ev->kind == ZRD_EVENT_END && ev->type == ZRD_CHARSXP && ev->data) {
        int64_t off = keep_text(t, ev->data, ev->data_len);
        if (off < 0) { t->failed_alloc = 1; return 1; }
        o->class_off = off;
        o->class_len = ev->data_len;
        o->class_flags = ev->flags;
    }
    return 0;
}

static cetype_t char_encoding(int flags)
{
    int levs = ZRD_LEVELS(flags);
    if (levs & (1 << 3)) return CE_UTF8;
    if (levs & (1 << 2)) return CE_LATIN1;
    if (levs & (1 << 1)) return CE_BYTES;
    return CE_NATIVE;
}

static SEXP text_elt(const zrd_toc *t, int64_t off, size_t len, cetype_t enc)
{
    if (off < 0) return NA_STRING;
    /* An empty string may have no arena behind it at all. */
    if (len == 0) return R_BlankString;
    const char *s = t->text + off;
    if (memchr(s, '\0', len)) return NA_STRING;
    return Rf_mkCharLenCE(s, (int) len, enc);
}

/* Walks the .rda stream in `src` after its magic line and returns one
 * column per field, one row per object. */
SEXP zrd_toc_walk(SEXP src, SEXP skip, SEXP limits, SEXP root)
{
    zrd_source *s = zrd_source_get(src);
    size_t nskip = (size_t) Rf_asInteger(skip);
    if (zrd_reader_read(&s->reader, NULL, nskip) != nskip)
        Rf_error("internal error: the input is shorter than its magic line");

    zrd_limits lim;
    lim.max_depth = Rf_asInteger(VECTOR_ELT(limits, 0));
    lim.max_items = Rf_asReal(VECTOR_ELT(limits, 1));
    lim.max_size = Rf_asReal(VECTOR_ELT(limits, 2));

    zrd_toc *t = calloc(1, sizeof *t);
    if (t == NULL) Rf_error("cannot allocate the walk");
    SEXP ptr = PROTECT(R_MakeExternalPtr(t, R_NilValue, src));
    R_RegisterCFinalizerEx(ptr, toc_finalizer, TRUE);
    t->root = Rf_asLogical(root) == TRUE;

    if (zrd_scan_init(&t->scan, &t->w, lim.max_depth)) Rf_error("cannot allocate the scanner");
    zrd_status st = zrd_walk_init(&t->w, &s->reader, &lim, on_event, t, tick, NULL);
    if (st == ZRD_OK) st = zrd_walk_capture_strings(&t->w, 4096);
    t->w.capture_ints = 1;
    if (st == ZRD_OK) st = zrd_walk_header(&t->w);
    if (st == ZRD_OK) st = zrd_walk_item(&t->w);
    if (st == ZRD_STOPPED && t->failed_alloc) Rf_error("cannot allocate the table of contents");

    char path[512];
    zrd_walk_path(&t->w, path, sizeof path);

    R_xlen_t n = (R_xlen_t) t->n;
    const char *cols[] = {"name", "type", "flags", "length", "offset", "end",
                          "class", "dim", "nrow", "altrep", "altrep_length",
                          "cells", "risky", ""};
    SEXP tab = PROTECT(Rf_mkNamed(VECSXP, cols));
    SET_VECTOR_ELT(tab, 0, Rf_allocVector(STRSXP, n));
    SET_VECTOR_ELT(tab, 1, Rf_allocVector(INTSXP, n));
    SET_VECTOR_ELT(tab, 2, Rf_allocVector(INTSXP, n));
    for (int j = 3; j < 6; j++) SET_VECTOR_ELT(tab, j, Rf_allocVector(REALSXP, n));
    SET_VECTOR_ELT(tab, 6, Rf_allocVector(STRSXP, n));
    SET_VECTOR_ELT(tab, 7, Rf_allocVector(VECSXP, n));
    SET_VECTOR_ELT(tab, 8, Rf_allocVector(REALSXP, n));
    SET_VECTOR_ELT(tab, 9, Rf_allocVector(STRSXP, n));
    SET_VECTOR_ELT(tab, 10, Rf_allocVector(REALSXP, n));
    SET_VECTOR_ELT(tab, 11, Rf_allocVector(REALSXP, n));
    SET_VECTOR_ELT(tab, 12, Rf_allocVector(LGLSXP, n));
    for (R_xlen_t i = 0; i < n; i++) {
        const zrd_obj *o = &t->objs[i];
        SET_STRING_ELT(VECTOR_ELT(tab, 0), i,
                       text_elt(t, (int64_t) o->name_off, o->name_len, CE_NATIVE));
        INTEGER(VECTOR_ELT(tab, 1))[i] = o->type;
        INTEGER(VECTOR_ELT(tab, 2))[i] = o->flags;
        REAL(VECTOR_ELT(tab, 3))[i] = (double) o->length;
        REAL(VECTOR_ELT(tab, 4))[i] = (double) o->begin;
        REAL(VECTOR_ELT(tab, 5))[i] = (double) o->end;
        SET_STRING_ELT(VECTOR_ELT(tab, 6), i,
                       text_elt(t, o->class_off, o->class_len, char_encoding(o->class_flags)));
        SEXP dim = Rf_allocVector(INTSXP, o->ndim);
        SET_VECTOR_ELT(VECTOR_ELT(tab, 7), i, dim);
        for (int k = 0; k < o->ndim; k++)
            INTEGER(dim)[k] = o->dim[k] == INT32_MIN ? NA_INTEGER : o->dim[k];
        REAL(VECTOR_ELT(tab, 8))[i] = (double) o->nrow;
        SET_STRING_ELT(VECTOR_ELT(tab, 9), i, text_elt(t, o->altrep_off, o->altrep_len, CE_NATIVE));
        REAL(VECTOR_ELT(tab, 10))[i] = o->altrep_n;
        REAL(VECTOR_ELT(tab, 11))[i] = (double) o->cells;
        LOGICAL(VECTOR_ELT(tab, 12))[i] = o->risky;
    }

    const char *names[] = {"status", "format", "version", "bytes", "offset",
                           "path", "detail", "objects", ""};
    SEXP out = PROTECT(Rf_mkNamed(VECSXP, names));
    char fmt[2] = {(char) t->w.header.format, '\0'};
    SET_VECTOR_ELT(out, 0, Rf_mkString(zrd_status_name(st)));
    SET_VECTOR_ELT(out, 1, Rf_mkString(fmt));
    SET_VECTOR_ELT(out, 2, Rf_ScalarInteger(t->w.header.version));
    SET_VECTOR_ELT(out, 3, Rf_ScalarReal((double) (zrd_reader_offset(&s->reader) - t->w.base)));
    SET_VECTOR_ELT(out, 4, Rf_ScalarReal(st == ZRD_OK ? NA_REAL : (double) t->w.err_offset));
    SET_VECTOR_ELT(out, 5, Rf_mkString(path));
    SET_VECTOR_ELT(out, 6, Rf_mkString(st == ZRD_OK ? "" : t->w.err_detail));
    SET_VECTOR_ELT(out, 7, tab);
    UNPROTECT(3);
    return out;
}
