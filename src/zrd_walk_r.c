/* The R side of the walk: runs it over a source, with the walk state owned
 * by an external pointer so an error or an interrupt frees it (design
 * section 12), and reports what it saw. */
#include <stdlib.h>
#include <string.h>

#include "zrd.h"
#include "zrd_source.h"
#include "zrd_walk.h"

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

/* Statistics gathered from the events. */
typedef struct {
    double begun, ended;
    int max_depth;
    double types[256];   /* items begun, by type */
} walk_stats;

static int count_event(void *ctx, const zrd_event *ev)
{
    walk_stats *st = ctx;
    if (ev->kind == ZRD_EVENT_BEGIN) {
        st->begun += 1;
        st->types[ev->type & 255] += 1;
        if (ev->depth > st->max_depth) st->max_depth = ev->depth;
    } else {
        st->ended += 1;
    }
    return 0;
}

static double limit_elt(SEXP limits, const char *name)
{
    SEXP names = Rf_getAttrib(limits, R_NamesSymbol);
    for (R_xlen_t i = 0; i < XLENGTH(limits); i++)
        if (strcmp(CHAR(STRING_ELT(names, i)), name) == 0)
            return Rf_asReal(VECTOR_ELT(limits, i));
    Rf_error("internal error: no limit %s", name);
    return 0;
}

/* Walks the stream in `src` after skipping `skip` bytes (an .rda magic
 * line), and returns the header, the item and byte counts, and on failure
 * the status, offset, path and detail. */
SEXP zrd_walk_stats(SEXP src, SEXP skip, SEXP limits)
{
    zrd_source *s = zrd_source_get(src);
    size_t nskip = (size_t) Rf_asInteger(skip);
    if (zrd_reader_read(&s->reader, NULL, nskip) != nskip)
        Rf_error("internal error: the input is shorter than its magic line");

    zrd_limits lim;
    lim.max_depth = (int) limit_elt(limits, "max_depth");
    lim.max_items = limit_elt(limits, "max_items");
    lim.max_size = limit_elt(limits, "max_size");

    zrd_walk *w = calloc(1, sizeof *w);
    if (w == NULL) Rf_error("cannot allocate the walk");
    SEXP ptr = PROTECT(R_MakeExternalPtr(w, R_NilValue, src));
    R_RegisterCFinalizerEx(ptr, walk_finalizer, TRUE);

    walk_stats stats;
    memset(&stats, 0, sizeof stats);
    zrd_status st = zrd_walk_init(w, &s->reader, &lim, count_event, &stats, tick, NULL);
    if (st == ZRD_OK) st = zrd_walk_header(w);
    if (st == ZRD_OK) st = zrd_walk_item(w);

    char path[512];
    zrd_walk_path(w, path, sizeof path);

    const char *names[] = {"status", "format", "version", "writer_version",
                           "min_reader_version", "native_encoding", "items",
                           "items_ended", "bytes", "max_depth", "offset",
                           "path", "detail", "types", ""};
    SEXP out = PROTECT(Rf_mkNamed(VECSXP, names));
    char fmt[2] = {(char) w->header.format, '\0'};
    SET_VECTOR_ELT(out, 0, Rf_mkString(zrd_status_name(st)));
    SET_VECTOR_ELT(out, 1, Rf_mkString(fmt));
    SET_VECTOR_ELT(out, 2, Rf_ScalarInteger(w->header.version));
    SET_VECTOR_ELT(out, 3, Rf_ScalarInteger(w->header.writer_version));
    SET_VECTOR_ELT(out, 4, Rf_ScalarInteger(w->header.min_reader_version));
    SET_VECTOR_ELT(out, 5, Rf_mkString(w->header.native_encoding));
    SET_VECTOR_ELT(out, 6, Rf_ScalarReal(stats.begun));
    SET_VECTOR_ELT(out, 7, Rf_ScalarReal(stats.ended));
    SET_VECTOR_ELT(out, 8, Rf_ScalarReal((double) (zrd_reader_offset(&s->reader) - w->base)));
    SET_VECTOR_ELT(out, 9, Rf_ScalarInteger(stats.max_depth));
    SET_VECTOR_ELT(out, 10, Rf_ScalarReal(st == ZRD_OK ? NA_REAL : (double) w->err_offset));
    SET_VECTOR_ELT(out, 11, Rf_mkString(path));
    SET_VECTOR_ELT(out, 12, Rf_mkString(st == ZRD_OK ? "" : w->err_detail));
    SEXP types = Rf_allocVector(REALSXP, 256);
    SET_VECTOR_ELT(out, 13, types);
    memcpy(REAL(types), stats.types, sizeof stats.types);

    zrd_walk_free(w);
    UNPROTECT(2);
    return out;
}
