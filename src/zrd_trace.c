/* Recording the walk's events for R: the table rds_str() and the tests are
 * built from. Only events down to `max_depth` are kept, at most
 * `max_events` of them, and a string only where it can describe the
 * structure: inside attributes (names, class, levels, dimnames) and as a
 * symbol's name. Everything is owned by an external pointer, so an error
 * or an interrupt during the walk frees it (design section 12). */
#include <stdlib.h>
#include <string.h>

#include "zrd.h"
#include "zrd_source.h"
#include "zrd_walk.h"

#define STRING_CAP 256

typedef struct {
    int kind, type, flags, depth, role;
    int64_t index, length;
    uint64_t offset;
    int64_t name_off;   /* -1: none */
    size_t name_len;
    int64_t data_off;   /* -1: none */
    size_t data_len;
    int data_complete;
} zrd_rec;

typedef struct {
    zrd_walk w;
    int max_depth;
    double max_events;
    int truncated;
    zrd_rec *recs;
    size_t n, cap;
    char *text;
    size_t text_len, text_cap;
    unsigned char *in_attr;   /* by depth: inside an attribute pairlist */
    int *types;               /* by depth: the item's type */
} zrd_trace;

static void trace_free(zrd_trace *t)
{
    zrd_walk_free(&t->w);
    free(t->recs);
    free(t->text);
    free(t->in_attr);
    free(t->types);
    free(t);
}

static void trace_finalizer(SEXP ptr)
{
    zrd_trace *t = R_ExternalPtrAddr(ptr);
    if (t) trace_free(t);
    R_ClearExternalPtr(ptr);
}

static void walk_finalizer(SEXP ptr)
{
    zrd_walk *w = R_ExternalPtrAddr(ptr);
    if (w) {
        zrd_walk_free(w);
        free(w);
    }
    R_ClearExternalPtr(ptr);
}

static void tick(void *ctx)
{
    (void) ctx;
    R_CheckUserInterrupt();
}

/* Appends text to the arena; returns its offset, or -1 out of memory. */
static int64_t keep_text(zrd_trace *t, const char *s, size_t n)
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

static int record(void *ctx, const zrd_event *ev)
{
    zrd_trace *t = ctx;
    if (ev->kind == ZRD_EVENT_BEGIN) {
        /* A pairlist's later cells are where its first cell was. */
        if (ev->role != ZRD_ROLE_CELL) {
            int parent_attr = ev->depth > 0 && t->in_attr[ev->depth - 1];
            t->in_attr[ev->depth] = parent_attr || ev->role == ZRD_ROLE_ATTR ||
                                    ev->role == ZRD_ROLE_ALTREP_INFO;
        }
        t->types[ev->depth] = ev->type;
    }
    if (ev->depth > t->max_depth) return 0;
    if ((double) t->n >= t->max_events) {
        t->truncated = 1;
        return 0;
    }
    if (t->n == t->cap) {
        size_t cap = t->cap ? 2 * t->cap : 256;
        zrd_rec *p = realloc(t->recs, cap * sizeof *p);
        if (p == NULL) return 1;
        t->recs = p;
        t->cap = cap;
    }
    zrd_rec *r = &t->recs[t->n++];
    r->kind = ev->kind;
    r->type = ev->type;
    r->flags = ev->flags;
    r->depth = ev->depth;
    r->role = ev->role;
    r->index = ev->index;
    r->length = ev->length;
    r->offset = ev->offset;
    r->name_off = -1;
    r->name_len = 0;
    r->data_off = -1;
    r->data_len = 0;
    r->data_complete = 1;
    if (ev->kind == ZRD_EVENT_BEGIN && ev->name) {
        r->name_off = keep_text(t, ev->name, ev->name_len);
        r->name_len = ev->name_len;
        if (r->name_off < 0) return 1;
    }
    int parent_sym = ev->depth > 0 && t->types[ev->depth - 1] == ZRD_SYMSXP;
    /* A string where it describes the structure, and a referenced
     * symbol's name wherever it is. */
    if (ev->kind == ZRD_EVENT_END && ev->data &&
        ((ev->type == ZRD_CHARSXP && ev->length >= 0 && (t->in_attr[ev->depth] || parent_sym)) ||
         ev->type == ZRD_REFSXP)) {
        r->data_off = keep_text(t, ev->data, ev->data_len);
        r->data_len = ev->data_len;
        r->data_complete = ev->data_complete;
        if (r->data_off < 0) return 1;
    }
    return 0;
}

/* A CHARSXP's encoding from its levels (serialize.c, ReadChar). */
static cetype_t rec_encoding(int flags)
{
    int levs = ZRD_LEVELS(flags);
    if (levs & (1 << 3)) return CE_UTF8;
    if (levs & (1 << 2)) return CE_LATIN1;
    if (levs & (1 << 1)) return CE_BYTES;
    return CE_NATIVE;
}

static SEXP text_or_na(const zrd_trace *t, int64_t off, size_t len, cetype_t enc)
{
    if (off < 0) return NA_STRING;
    /* An empty string may have no arena behind it at all. */
    if (len == 0) return R_BlankString;
    const char *s = t->text + off;
    if (memchr(s, '\0', len)) return NA_STRING;
    return Rf_mkCharLenCE(s, (int) len, enc);
}

/* Walks the stream in `src` after skipping `skip` bytes, recording events
 * down to `max_depth`, at most `max_events`. Returns the walk's result as
 * zrd_walk_stats() does, plus the events as columns. */
SEXP zrd_walk_trace(SEXP src, SEXP skip, SEXP limits, SEXP max_depth, SEXP max_events)
{
    zrd_source *s = zrd_source_get(src);
    size_t nskip = (size_t) Rf_asInteger(skip);
    if (zrd_reader_read(&s->reader, NULL, nskip) != nskip)
        Rf_error("internal error: the input is shorter than its magic line");

    zrd_limits lim;
    lim.max_depth = Rf_asInteger(VECTOR_ELT(limits, 0));
    lim.max_items = Rf_asReal(VECTOR_ELT(limits, 1));
    lim.max_size = Rf_asReal(VECTOR_ELT(limits, 2));

    zrd_trace *t = calloc(1, sizeof *t);
    if (t == NULL) Rf_error("cannot allocate the walk");
    SEXP ptr = PROTECT(R_MakeExternalPtr(t, R_NilValue, src));
    R_RegisterCFinalizerEx(ptr, trace_finalizer, TRUE);
    t->max_depth = Rf_asInteger(max_depth);
    t->max_events = Rf_asReal(max_events);
    t->in_attr = calloc((size_t) lim.max_depth + 2, 1);
    t->types = calloc((size_t) lim.max_depth + 2, sizeof *t->types);
    if (t->in_attr == NULL || t->types == NULL) Rf_error("cannot allocate the walk");

    zrd_status st = zrd_walk_init(&t->w, &s->reader, &lim, record, t, tick, NULL);
    if (st == ZRD_OK) st = zrd_walk_capture_strings(&t->w, STRING_CAP);
    if (st == ZRD_OK) st = zrd_walk_header(&t->w);
    if (st == ZRD_OK) st = zrd_walk_item(&t->w);
    if (st == ZRD_STOPPED) Rf_error("cannot allocate the event record");

    char path[512];
    zrd_walk_path(&t->w, path, sizeof path);

    R_xlen_t n = (R_xlen_t) t->n;
    const char *cols[] = {"kind", "type", "flags", "depth", "role", "index",
                          "length", "offset", "name", "data", "data_complete", ""};
    SEXP ev = PROTECT(Rf_mkNamed(VECSXP, cols));
    for (int j = 0; j < 5; j++) SET_VECTOR_ELT(ev, j, Rf_allocVector(INTSXP, n));
    for (int j = 5; j < 8; j++) SET_VECTOR_ELT(ev, j, Rf_allocVector(REALSXP, n));
    SET_VECTOR_ELT(ev, 8, Rf_allocVector(STRSXP, n));
    SET_VECTOR_ELT(ev, 9, Rf_allocVector(STRSXP, n));
    SET_VECTOR_ELT(ev, 10, Rf_allocVector(LGLSXP, n));
    for (R_xlen_t i = 0; i < n; i++) {
        const zrd_rec *r = &t->recs[i];
        INTEGER(VECTOR_ELT(ev, 0))[i] = r->kind;
        INTEGER(VECTOR_ELT(ev, 1))[i] = r->type;
        INTEGER(VECTOR_ELT(ev, 2))[i] = r->flags;
        INTEGER(VECTOR_ELT(ev, 3))[i] = r->depth;
        INTEGER(VECTOR_ELT(ev, 4))[i] = r->role;
        REAL(VECTOR_ELT(ev, 5))[i] = (double) r->index;
        REAL(VECTOR_ELT(ev, 6))[i] = (double) r->length;
        REAL(VECTOR_ELT(ev, 7))[i] = (double) r->offset;
        SET_STRING_ELT(VECTOR_ELT(ev, 8), i, text_or_na(t, r->name_off, r->name_len, CE_NATIVE));
        SET_STRING_ELT(VECTOR_ELT(ev, 9), i,
                       text_or_na(t, r->data_off, r->data_len, rec_encoding(r->flags)));
        LOGICAL(VECTOR_ELT(ev, 10))[i] = r->data_complete;
    }

    const char *names[] = {"status", "format", "version", "writer_version",
                           "min_reader_version", "native_encoding", "bytes",
                           "offset", "path", "detail", "events", "truncated", ""};
    SEXP out = PROTECT(Rf_mkNamed(VECSXP, names));
    char fmt[2] = {(char) t->w.header.format, '\0'};
    SET_VECTOR_ELT(out, 0, Rf_mkString(zrd_status_name(st)));
    SET_VECTOR_ELT(out, 1, Rf_mkString(fmt));
    SET_VECTOR_ELT(out, 2, Rf_ScalarInteger(t->w.header.version));
    SET_VECTOR_ELT(out, 3, Rf_ScalarInteger(t->w.header.writer_version));
    SET_VECTOR_ELT(out, 4, Rf_ScalarInteger(t->w.header.min_reader_version));
    SET_VECTOR_ELT(out, 5, Rf_mkString(t->w.header.native_encoding));
    SET_VECTOR_ELT(out, 6, Rf_ScalarReal((double) (zrd_reader_offset(&s->reader) - t->w.base)));
    SET_VECTOR_ELT(out, 7, Rf_ScalarReal(st == ZRD_OK ? NA_REAL : (double) t->w.err_offset));
    SET_VECTOR_ELT(out, 8, Rf_mkString(path));
    SET_VECTOR_ELT(out, 9, Rf_mkString(st == ZRD_OK ? "" : t->w.err_detail));
    SET_VECTOR_ELT(out, 10, ev);
    SET_VECTOR_ELT(out, 11, Rf_ScalarLogical(t->truncated));
    UNPROTECT(3);
    return out;
}

/* Reads only the stream header: rds_info(). */
SEXP zrd_walk_head(SEXP src, SEXP skip)
{
    zrd_source *s = zrd_source_get(src);
    size_t nskip = (size_t) Rf_asInteger(skip);
    if (zrd_reader_read(&s->reader, NULL, nskip) != nskip)
        Rf_error("internal error: the input is shorter than its magic line");
    zrd_limits lim = {1, 1, 1e18};
    zrd_walk *w = calloc(1, sizeof *w);
    if (w == NULL) Rf_error("cannot allocate the walk");
    SEXP ptr = PROTECT(R_MakeExternalPtr(w, R_NilValue, src));
    R_RegisterCFinalizerEx(ptr, walk_finalizer, TRUE);
    zrd_status st = zrd_walk_init(w, &s->reader, &lim, NULL, NULL, NULL, NULL);
    if (st == ZRD_OK) st = zrd_walk_header(w);
    const char *names[] = {"status", "format", "version", "writer_version",
                           "min_reader_version", "native_encoding", "offset",
                           "path", "detail", ""};
    SEXP out = PROTECT(Rf_mkNamed(VECSXP, names));
    char fmt[2] = {(char) w->header.format, '\0'};
    SET_VECTOR_ELT(out, 0, Rf_mkString(zrd_status_name(st)));
    SET_VECTOR_ELT(out, 1, Rf_mkString(fmt));
    SET_VECTOR_ELT(out, 2, Rf_ScalarInteger(w->header.version));
    SET_VECTOR_ELT(out, 3, Rf_ScalarInteger(w->header.writer_version));
    SET_VECTOR_ELT(out, 4, Rf_ScalarInteger(w->header.min_reader_version));
    SET_VECTOR_ELT(out, 5, Rf_mkString(w->header.native_encoding));
    SET_VECTOR_ELT(out, 6, Rf_ScalarReal(st == ZRD_OK ? NA_REAL : (double) w->err_offset));
    SET_VECTOR_ELT(out, 7, Rf_mkString(""));
    SET_VECTOR_ELT(out, 8, Rf_mkString(st == ZRD_OK ? "" : w->err_detail));
    UNPROTECT(2);
    return out;
}
