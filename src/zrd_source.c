/* A source of decompressed stream bytes for the walk (design section 4):
 * an R connection, read in ZRD_BLOCK_SIZE blocks with R_ReadConnection(),
 * or a raw vector read in place. R opens the connection with gzfile(),
 * which detects gzip, bzip2, xz and zstd, so zurds reads exactly what the
 * running R can read (D3). Everything is owned by an external pointer, so
 * an error or an interrupt during a read frees it (section 12). */
#include <stdlib.h>
#include <string.h>

#include "zrd.h"
#include "zrd_inflate.h"
#include "zrd_source.h"

#if !defined(R_CONNECTIONS_VERSION) || R_CONNECTIONS_VERSION != 1
#error "zurds needs version 1 of R's connections API"
#endif

static void source_free(zrd_source *s)
{
    if (s == NULL) return;
    free(s->block);
    free(s);
}

static void source_finalizer(SEXP ptr)
{
    source_free(R_ExternalPtrAddr(ptr));
    R_ClearExternalPtr(ptr);
}

static size_t conn_fill(void *ctx, uint8_t *buf, size_t cap)
{
    zrd_source *s = ctx;
    size_t got = R_ReadConnection(s->con, buf, cap);
    /* gzfile() reports a corrupt stream as (size_t) -1 after warning
     * (connections.c, gzfile_read); zrd_source_call() turns the warning
     * into an error first, and this catches it if a handler did not. */
    if (got > cap) Rf_error("the connection reported a read error");
    return got;
}

zrd_source *zrd_source_get(SEXP ptr)
{
    zrd_source *s = TYPEOF(ptr) == EXTPTRSXP ? R_ExternalPtrAddr(ptr) : NULL;
    if (s == NULL) Rf_error("internal error: closed or invalid zurds source");
    return s;
}

/* `x` is a raw vector or an open connection; the pointer keeps it alive. */
SEXP zrd_source_new(SEXP x)
{
    zrd_source *s = calloc(1, sizeof *s);
    if (s == NULL) Rf_error("cannot allocate a zurds source");
    SEXP ptr = PROTECT(R_MakeExternalPtr(s, R_NilValue, x));
    R_RegisterCFinalizerEx(ptr, source_finalizer, TRUE);
    if (TYPEOF(x) == RAWSXP) {
        zrd_reader_init_mem(&s->reader, RAW(x), (size_t) XLENGTH(x));
    } else {
        s->con = R_GetConnection(x);
        s->block = malloc(ZRD_BLOCK_SIZE);
        if (s->block == NULL) Rf_error("cannot allocate a zurds input block");
        zrd_reader_init_fill(&s->reader, conn_fill, s, s->block);
    }
    UNPROTECT(1);
    return ptr;
}

/* The next `n` bytes (at most 64), not consumed: fewer at the end of the
 * input. Used to detect the wrapper. */
SEXP zrd_source_peek(SEXP ptr, SEXP n)
{
    zrd_source *s = zrd_source_get(ptr);
    uint8_t tmp[64];
    int want = Rf_asInteger(n);
    if (want < 0 || want > (int) sizeof tmp) Rf_error("internal error: bad peek size");
    size_t got = zrd_reader_peek(&s->reader, tmp, (size_t) want);
    SEXP out = PROTECT(Rf_allocVector(RAWSXP, (R_xlen_t) got));
    if (got) memcpy(RAW(out), tmp, got);
    UNPROTECT(1);
    return out;
}

/* Reads to the end of the input and returns the stream's total length,
 * checking for interrupts between blocks. */
SEXP zrd_source_drain(SEXP ptr)
{
    zrd_source *s = zrd_source_get(ptr);
    while (zrd_reader_read(&s->reader, NULL, ZRD_BLOCK_SIZE) == ZRD_BLOCK_SIZE)
        R_CheckUserInterrupt();
    return Rf_ScalarReal((double) zrd_reader_offset(&s->reader));
}

/* Skips `skip` bytes, then returns the next `n` bytes of the stream:
 * rds_extract(use_r = TRUE) reads an item's bytes this way. */
SEXP zrd_source_bytes(SEXP ptr, SEXP skip, SEXP n)
{
    zrd_source *s = zrd_source_get(ptr);
    size_t nskip = (size_t) Rf_asInteger(skip);
    double want = Rf_asReal(n);
    if (!(want >= 0) || want > R_XLEN_T_MAX) Rf_error("internal error: bad byte count");
    if (zrd_reader_read(&s->reader, NULL, nskip) != nskip)
        Rf_error("internal error: the input is shorter than its magic line");
    SEXP out = PROTECT(Rf_allocVector(RAWSXP, (R_xlen_t) want));
    size_t got = zrd_reader_read(&s->reader, RAW(out), (size_t) want);
    if (got != (size_t) want) Rf_error("internal error: the input changed while read");
    UNPROTECT(1);
    return out;
}

/* Decompresses a lazy-load entry's zlib stream `in` into the `n` bytes it
 * declares (zrd_inflate.c); a status message instead on failure. */
SEXP zrd_inflate_raw(SEXP in, SEXP n)
{
    double want = Rf_asReal(n);
    if (TYPEOF(in) != RAWSXP || !(want >= 0) || want > R_XLEN_T_MAX)
        Rf_error("internal error: bad inflate arguments");
    SEXP out = PROTECT(Rf_allocVector(RAWSXP, (R_xlen_t) want));
    size_t produced;
    zrd_inflate_status st = zrd_inflate_zlib(RAW(in), (size_t) XLENGTH(in), RAW(out),
                                             (size_t) want, &produced);
    SEXP res = st == ZRD_INFLATE_OK ? out : Rf_mkString(zrd_inflate_message(st));
    UNPROTECT(1);
    return res;
}

/* An xz lazy-load entry's raw LZMA2 data `in`, of `n` bytes uncompressed,
 * in an .xz container for memDecompress() (zrd_inflate.c). */
SEXP zrd_xz_wrap(SEXP in, SEXP n)
{
    double want = Rf_asReal(n);
    if (TYPEOF(in) != RAWSXP || !(want >= 0)) Rf_error("internal error: bad xz arguments");
    size_t len = (size_t) XLENGTH(in);
    SEXP out = PROTECT(Rf_allocVector(RAWSXP, (R_xlen_t) zrd_xz_container_size(len)));
    size_t used = zrd_xz_container(RAW(in), len, (uint64_t) want, RAW(out));
    SEXP res = Rf_lengthgets(out, (R_xlen_t) used);
    UNPROTECT(1);
    return res;
}
