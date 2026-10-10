/* rds_scan()'s entry point: runs the walk with the scanner (zrd_scan.c)
 * listening, both owned by an external pointer (design section 12). */
#include <stdlib.h>
#include <string.h>

#include "zrd.h"
#include "zrd_scan.h"
#include "zrd_source.h"
#include "zrd_walk.h"

typedef struct {
    zrd_walk w;
    zrd_scan scan;
} zrd_scan_run;

static void run_finalizer(SEXP ptr)
{
    zrd_scan_run *r = R_ExternalPtrAddr(ptr);
    if (r) {
        zrd_walk_free(&r->w);
        zrd_scan_free(&r->scan);
        free(r);
    }
    R_ClearExternalPtr(ptr);
}

static void tick(void *ctx)
{
    (void) ctx;
    R_CheckUserInterrupt();
}

static int on_event(void *ctx, const zrd_event *ev)
{
    return zrd_scan_event(ctx, ev);
}

static SEXP text_elt(const zrd_scan *s, int64_t off, size_t len)
{
    if (off < 0) return NA_STRING;
    const char *p = s->text + off;
    if (memchr(p, '\0', len)) return NA_STRING;
    return Rf_mkCharLenCE(p, (int) len, CE_NATIVE);
}

/* The scanner's findings as columns: kind, severity (as numbers R names),
 * offset, path, and two detail strings. */
SEXP zrd_scan_findings(const zrd_scan *s)
{
    R_xlen_t n = 0;
    for (size_t i = 0; i < s->n; i++) n += !s->f[i].deleted;
    const char *cols[] = {"kind", "severity", "offset", "path", "detail1", "detail2", ""};
    SEXP out = PROTECT(Rf_mkNamed(VECSXP, cols));
    SET_VECTOR_ELT(out, 0, Rf_allocVector(INTSXP, n));
    SET_VECTOR_ELT(out, 1, Rf_allocVector(INTSXP, n));
    SET_VECTOR_ELT(out, 2, Rf_allocVector(REALSXP, n));
    for (int j = 3; j < 6; j++) SET_VECTOR_ELT(out, j, Rf_allocVector(STRSXP, n));
    R_xlen_t k = 0;
    for (size_t i = 0; i < s->n; i++) {
        const zrd_finding *f = &s->f[i];
        if (f->deleted) continue;
        INTEGER(VECTOR_ELT(out, 0))[k] = f->kind;
        INTEGER(VECTOR_ELT(out, 1))[k] = f->severity;
        REAL(VECTOR_ELT(out, 2))[k] = (double) f->offset;
        SET_STRING_ELT(VECTOR_ELT(out, 3), k, text_elt(s, (int64_t) f->path_off, f->path_len));
        SET_STRING_ELT(VECTOR_ELT(out, 4), k, text_elt(s, f->detail_off[0], f->detail_len[0]));
        SET_STRING_ELT(VECTOR_ELT(out, 5), k, text_elt(s, f->detail_off[1], f->detail_len[1]));
        k++;
    }
    UNPROTECT(1);
    return out;
}

SEXP zrd_scan_walk(SEXP src, SEXP skip, SEXP limits)
{
    zrd_source *s = zrd_source_get(src);
    size_t nskip = (size_t) Rf_asInteger(skip);
    if (zrd_reader_read(&s->reader, NULL, nskip) != nskip)
        Rf_error("internal error: the input is shorter than its magic line");

    zrd_limits lim;
    lim.max_depth = Rf_asInteger(VECTOR_ELT(limits, 0));
    lim.max_items = Rf_asReal(VECTOR_ELT(limits, 1));
    lim.max_size = Rf_asReal(VECTOR_ELT(limits, 2));

    zrd_scan_run *r = calloc(1, sizeof *r);
    if (r == NULL) Rf_error("cannot allocate the scanner");
    SEXP ptr = PROTECT(R_MakeExternalPtr(r, R_NilValue, src));
    R_RegisterCFinalizerEx(ptr, run_finalizer, TRUE);
    if (zrd_scan_init(&r->scan, &r->w, lim.max_depth))
        Rf_error("cannot allocate the scanner");

    zrd_status st = zrd_walk_init(&r->w, &s->reader, &lim, on_event, &r->scan, tick, NULL);
    if (st == ZRD_OK) st = zrd_walk_capture_strings(&r->w, 4096);
    if (st == ZRD_OK) st = zrd_walk_header(&r->w);
    if (st == ZRD_OK) st = zrd_walk_item(&r->w);
    if (st == ZRD_STOPPED) Rf_error("cannot allocate the scanner's findings");

    char path[512];
    zrd_walk_path(&r->w, path, sizeof path);
    const char *names[] = {"status", "format", "version", "bytes", "offset",
                           "path", "detail", "findings", ""};
    SEXP out = PROTECT(Rf_mkNamed(VECSXP, names));
    char fmt[2] = {(char) r->w.header.format, '\0'};
    SET_VECTOR_ELT(out, 0, Rf_mkString(zrd_status_name(st)));
    SET_VECTOR_ELT(out, 1, Rf_mkString(fmt));
    SET_VECTOR_ELT(out, 2, Rf_ScalarInteger(r->w.header.version));
    SET_VECTOR_ELT(out, 3, Rf_ScalarReal((double) (zrd_reader_offset(&s->reader) - r->w.base)));
    SET_VECTOR_ELT(out, 4, Rf_ScalarReal(st == ZRD_OK ? NA_REAL : (double) r->w.err_offset));
    SET_VECTOR_ELT(out, 5, Rf_mkString(path));
    SET_VECTOR_ELT(out, 6, Rf_mkString(st == ZRD_OK ? "" : r->w.err_detail));
    SET_VECTOR_ELT(out, 7, zrd_scan_findings(&r->scan));
    UNPROTECT(2);
    return out;
}
