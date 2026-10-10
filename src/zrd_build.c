/* The data-only builder (design sections 5, 8, 9 and 12): builds R objects
 * from the walk's events, and only NULL, logical, integer, double,
 * complex, character, raw and list vectors with attributes of those types.
 * Every other item is refused: recorded with its path, and replaced by
 * NULL. It never calls unserialize(), eval() or anything from the file.
 *
 * Each open item has a slot by depth: its object, its pending attributes
 * and, for an ALTREP item, its state, all in R vectors held by the
 * external pointer that owns the walk, so an error or an interrupt frees
 * everything. Base R's ALTREP classes are materialised from their state,
 * as altclasses.c would compute them. The scanner runs in the same pass,
 * for rds_read_data()'s refusal of unsafe content. */
#include <stdlib.h>
#include <string.h>

#include <R_ext/Riconv.h>
#include <zufast/utf8.h>

#include "zrd.h"
#include "zrd_scan.h"
#include "zrd_source.h"
#include "zrd_walk.h"

typedef enum {
    N_VALUE = 0,  /* an object being built */
    N_SKIP,       /* a refused item: becomes NULL */
    N_IGNORE,     /* not part of any value: a tag, an ALTREP's class info */
    N_ATTRCELL,   /* a cell of an attribute pairlist */
    N_RDACELL,    /* a cell of an .rda's pairlist of objects */
    N_STATECELL,  /* a cell of an ALTREP state that is a pairlist */
    N_ALTREP      /* an ALTREP item: materialised at its end */
} node_kind;

/* Slots of the holder vector. */
enum { H_OBJS = 0, H_ATTRS, H_STATES, H_RDA_VALUES, H_RDA_NAMES, H_RESULT, H_N };

typedef struct {
    zrd_walk w;
    zrd_scan scan;
    SEXP holder;            /* protected by the external pointer */
    int rda;
    int strict;
    int ndepth;
    node_kind *kind;
    int *owner;             /* attribute and state cells: their owner's depth */
    int64_t *name_off;      /* a CAR's tag in `text`, -1 none */
    size_t *name_len;
    int skip_depth;         /* inside a skipped subtree below this depth */
    char *text;             /* names */
    size_t text_len, text_cap;
    char *cbuf;             /* the current CHARSXP's bytes */
    size_t ccap;
    int64_t rda_n;
    /* Refused items: their type and path. */
    int *refused_type;
    int64_t *refused_off;
    size_t *refused_len;
    size_t nrefused, refused_cap;
    /* Strings marked native in a stream whose native encoding is not the
     * session's are translated to UTF-8. */
    void *iconv;
    /* Why the walk was stopped. */
    int stop;               /* 1 invalid UTF-8; 2 out of memory; 3 done */
    /* Extraction (rds_extract()): build only the item that begins at
     * `target` in the stream; -1 builds the root. */
    double target;
    int in_target;
    int base_depth;
    char stop_path[512];
    uint64_t stop_offset;
} zrd_builder;

#define STOP_ENCODING 1
#define STOP_NOMEM 2
#define STOP_DONE 3   /* the target item is built */

static void builder_free(zrd_builder *b)
{
    zrd_walk_free(&b->w);
    zrd_scan_free(&b->scan);
    free(b->kind);
    free(b->owner);
    free(b->name_off);
    free(b->name_len);
    free(b->text);
    free(b->cbuf);
    free(b->refused_type);
    free(b->refused_off);
    free(b->refused_len);
    if (b->iconv) Riconv_close(b->iconv);
    free(b);
}

static void builder_finalizer(SEXP ptr)
{
    zrd_builder *b = R_ExternalPtrAddr(ptr);
    if (b) builder_free(b);
    R_ClearExternalPtr(ptr);
}

static void tick(void *ctx)
{
    (void) ctx;
    R_CheckUserInterrupt();
}

static int64_t keep_text(zrd_builder *b, const char *s, size_t n)
{
    if (b->text_cap - b->text_len < n) {
        size_t cap = b->text_cap ? b->text_cap : 1024;
        while (cap - b->text_len < n) cap *= 2;
        char *p = realloc(b->text, cap);
        if (p == NULL) return -1;
        b->text = p;
        b->text_cap = cap;
    }
    if (n) memcpy(b->text + b->text_len, s, n);
    b->text_len += n;
    return (int64_t) (b->text_len - n);
}

static SEXP slot(const zrd_builder *b, int which, int d)
{
    return VECTOR_ELT(VECTOR_ELT(b->holder, which), d);
}

static void set_slot(zrd_builder *b, int which, int d, SEXP v)
{
    SET_VECTOR_ELT(VECTOR_ELT(b->holder, which), d, v);
}

static int refuse(zrd_builder *b, const zrd_event *ev)
{
    if (b->nrefused == b->refused_cap) {
        size_t cap = b->refused_cap ? 2 * b->refused_cap : 16;
        int *t = realloc(b->refused_type, cap * sizeof *t);
        if (t) b->refused_type = t;
        int64_t *o = realloc(b->refused_off, cap * sizeof *o);
        if (o) b->refused_off = o;
        size_t *l = realloc(b->refused_len, cap * sizeof *l);
        if (l) b->refused_len = l;
        if (!t || !o || !l) return 1;
        b->refused_cap = cap;
    }
    char path[1024];
    zrd_walk_path(&b->w, path, sizeof path);
    int64_t off = keep_text(b, path, strlen(path));
    if (off < 0) return 1;
    b->refused_type[b->nrefused] = ev->type == ZRD_REFSXP ? ev->ref_type : ev->type;
    b->refused_off[b->nrefused] = off;
    b->refused_len[b->nrefused] = strlen(path);
    b->nrefused++;
    return 0;
}

static int is_vector(int type)
{
    return type == ZRD_LGLSXP || type == ZRD_INTSXP || type == ZRD_REALSXP ||
           type == ZRD_CPLXSXP || type == ZRD_RAWSXP || type == ZRD_STRSXP ||
           type == ZRD_VECSXP;
}

#define S4_BIT (1 << 4)

static int on_begin(zrd_builder *b, const zrd_event *ev)
{
    int d = ev->depth;
    if (b->skip_depth >= 0 && d > b->skip_depth) return 0;
    node_kind prev = b->kind[d];
    node_kind pk = d > 0 ? b->kind[d - 1] : N_VALUE;
    b->name_off[d] = -1;

    /* A pairlist's later cells continue its first, for the same owner;
     * those of an ignored or refused pairlist are skipped like it. */
    if (ev->role == ZRD_ROLE_CELL &&
        (prev == N_ATTRCELL || prev == N_RDACELL || prev == N_STATECELL))
        return 0;
    if (ev->role == ZRD_ROLE_CELL && (prev == N_IGNORE || prev == N_SKIP)) {
        b->kind[d] = N_IGNORE;
        b->skip_depth = d;
        return 0;
    }
    b->owner[d] = -1;

    /* Within a cell: its tag and attributes are not values; its cdr ends
     * the pairlist, except an ALTREP state's, which is a dotted pair. */
    if (pk == N_ATTRCELL || pk == N_RDACELL || pk == N_STATECELL) {
        if (ev->role == ZRD_ROLE_TAG || ev->role == ZRD_ROLE_ATTR ||
            (ev->role == ZRD_ROLE_CDR && pk != N_STATECELL)) {
            b->kind[d] = N_IGNORE;
            b->skip_depth = d;
            return 0;
        }
        if (ev->role == ZRD_ROLE_CAR && ev->name) {
            int64_t off = keep_text(b, ev->name, ev->name_len);
            if (off < 0) return b->stop = STOP_NOMEM;
            b->name_off[d] = off;
            b->name_len[d] = ev->name_len;
        }
    }
    if (pk == N_ALTREP && ev->role == ZRD_ROLE_ALTREP_INFO) {
        b->kind[d] = N_IGNORE;
        b->skip_depth = d;
        return 0;
    }
    /* An attribute pairlist, or an .rda's pairlist of objects. */
    if (ev->type == ZRD_LISTSXP) {
        if (d == 0 && b->rda) { b->kind[d] = N_RDACELL; return 0; }
        if (ev->role == ZRD_ROLE_ATTR || ev->role == ZRD_ROLE_ALTREP_ATTR) {
            b->kind[d] = N_ATTRCELL;
            b->owner[d] = d - 1;
            return 0;
        }
        if (pk == N_ALTREP && ev->role == ZRD_ROLE_ALTREP_STATE) {
            b->kind[d] = N_STATECELL;
            b->owner[d] = d - 1;
            return 0;
        }
    }
    if (ev->type == ZRD_NILVALUE_SXP &&
        (ev->role == ZRD_ROLE_ATTR || ev->role == ZRD_ROLE_ALTREP_ATTR)) {
        b->kind[d] = N_IGNORE;
        b->skip_depth = d;
        return 0;
    }
    if (ev->type == ZRD_ALTREP_SXP) {
        b->kind[d] = N_ALTREP;
        set_slot(b, H_STATES, d, R_NilValue);
        set_slot(b, H_ATTRS, d, R_NilValue);
        return 0;
    }

    b->kind[d] = N_VALUE;
    set_slot(b, H_ATTRS, d, R_NilValue);
    if (ev->type == ZRD_NILVALUE_SXP || ev->type == ZRD_CHARSXP) {
        set_slot(b, H_OBJS, d, R_NilValue);
        return 0;
    }
    /* An S4 object built on a vector would lose what makes it S4. */
    if (is_vector(ev->type) && !(ZRD_LEVELS(ev->flags) & S4_BIT)) {
        set_slot(b, H_OBJS, d, Rf_allocVector((SEXPTYPE) ev->type, (R_xlen_t) ev->length));
        return 0;
    }
    /* Everything else is refused (design section 8). */
    if (refuse(b, ev)) return b->stop = STOP_NOMEM;
    b->kind[d] = N_SKIP;
    b->skip_depth = d;
    return 0;
}

static void *on_payload(void *ctx, const zrd_event *ev)
{
    zrd_builder *b = ctx;
    int d = ev->depth;
    if (b->target >= 0) {
        if (!b->in_target) return NULL;
        d -= b->base_depth;
    }
    if (d < 0 || d + 1 >= b->ndepth) return NULL;
    if (b->skip_depth >= 0 && d >= b->skip_depth) return NULL;
    if (b->kind[d] != N_VALUE || ev->length <= 0) return NULL;
    if (ev->type == ZRD_CHARSXP) {
        size_t need = (size_t) ev->length;
        if (need > b->ccap) {
            char *p = realloc(b->cbuf, need);
            if (p == NULL) {
                b->stop = STOP_NOMEM;
                return NULL;
            }
            b->cbuf = p;
            b->ccap = need;
        }
        return b->cbuf;
    }
    SEXP obj = slot(b, H_OBJS, d);
    switch (TYPEOF(obj)) {
    case LGLSXP: return LOGICAL(obj);
    case INTSXP: return INTEGER(obj);
    case REALSXP: return REAL(obj);
    case CPLXSXP: return COMPLEX(obj);
    case RAWSXP: return RAW(obj);
    default: return NULL;
    }
}

/* serialize.c, ReadChar: a string with its declared encoding. */
static SEXP make_char(zrd_builder *b, const zrd_event *ev)
{
    if (ev->length < 0) return NA_STRING;
    size_t n = (size_t) ev->length;
    const char *s = n ? b->cbuf : "";
    int levs = ZRD_LEVELS(ev->flags);
    if (levs & (1 << 3)) {
        if (b->strict && !zuf_utf8_valid(s, n)) {
            b->stop = STOP_ENCODING;
            zrd_walk_path(&b->w, b->stop_path, sizeof b->stop_path);
            b->stop_offset = ev->offset;
            return R_NilValue;
        }
        return Rf_mkCharLenCE(s, (int) n, CE_UTF8);
    }
    if (levs & (1 << 2)) return Rf_mkCharLenCE(s, (int) n, CE_LATIN1);
    if (levs & (1 << 1)) return Rf_mkCharLenCE(s, (int) n, CE_BYTES);
    if ((levs & (1 << 6)) || b->iconv == NULL) return Rf_mkCharLenCE(s, (int) n, CE_NATIVE);
    /* A native string from a session in another encoding: to UTF-8. */
    size_t cap = 4 * n + 4;
    char *out = R_alloc(cap, 1);
    const char *in = s;
    size_t inleft = n, outleft = cap;
    char *o = out;
    Riconv(b->iconv, NULL, NULL, NULL, NULL);
    if (Riconv(b->iconv, &in, &inleft, &o, &outleft) == (size_t) -1) {
        if (b->strict) {
            b->stop = STOP_ENCODING;
            zrd_walk_path(&b->w, b->stop_path, sizeof b->stop_path);
            b->stop_offset = ev->offset;
            return R_NilValue;
        }
        return Rf_mkCharLenCE(s, (int) n, CE_NATIVE);
    }
    return Rf_mkCharLenCE(out, (int) (cap - outleft), CE_UTF8);
}

/* Applies the attributes gathered for depth `d` to `obj`, in stream order. */
static void apply_attrs(zrd_builder *b, int d, SEXP obj)
{
    SEXP rev = slot(b, H_ATTRS, d);
    if (rev == R_NilValue || obj == R_NilValue) return;
    PROTECT(obj);
    PROTECT(rev);
    /* Gathered last first: set them first to last. */
    R_xlen_t n = 0;
    for (SEXP a = rev; a != R_NilValue; a = CDR(a)) n++;
    SEXP *cells = (SEXP *) R_alloc((size_t) n, sizeof *cells);
    R_xlen_t k = n;
    for (SEXP a = rev; a != R_NilValue; a = CDR(a)) cells[--k] = a;
    for (k = 0; k < n; k++) Rf_setAttrib(obj, TAG(cells[k]), CAR(cells[k]));
    UNPROTECT(2);
    set_slot(b, H_ATTRS, d, R_NilValue);
}

/* An ALTREP item of one of base R's classes, as its class would compute
 * it (altclasses.c); NULL with `refused` set for any other class. */
static SEXP materialise(zrd_builder *b, int d, int *refused)
{
    *refused = 1;
    int64_t i = b->scan.finding[d];
    if (i < 0) return R_NilValue;
    const zrd_finding *f = &b->scan.f[i];
    if (f->detail_off[0] < 0 || !f->deleted) return R_NilValue; /* not base */
    const char *cls = b->scan.text + f->detail_off[0];
    size_t len = f->detail_len[0];
    SEXP state = slot(b, H_STATES, d);
#define IS(name) (len == strlen(name) && memcmp(cls, name, len) == 0)
    if ((IS("compact_intseq") || IS("compact_realseq")) &&
        TYPEOF(state) == REALSXP && XLENGTH(state) == 3) {
        double n = REAL(state)[0], n1 = REAL(state)[1], inc = REAL(state)[2];
        if (!(n >= 0 && n <= R_XLEN_T_MAX)) return R_NilValue;
        int ints = IS("compact_intseq");
        SEXP out = PROTECT(Rf_allocVector(ints ? INTSXP : REALSXP, (R_xlen_t) n));
        for (R_xlen_t k = 0; k < (R_xlen_t) n; k++) {
            double v = n1 + (double) k * inc;
            if (ints) INTEGER(out)[k] = (int) v;
            else REAL(out)[k] = v;
        }
        UNPROTECT(1);
        *refused = 0;
        return out;
    }
    if (IS("deferred_string") && (TYPEOF(state) == INTSXP || TYPEOF(state) == REALSXP)) {
        *refused = 0;
        return Rf_coerceVector(state, STRSXP);
    }
    /* A wrapper's state is (vector . metadata): the vector was kept. */
    if (len > 5 && memcmp(cls, "wrap_", 5) == 0 && state != R_NilValue) {
        *refused = 0;
        return state;
    }
#undef IS
    return R_NilValue;
}

static void attach(zrd_builder *b, int d, const zrd_event *ev, SEXP v)
{
    PROTECT(v);
    if (d == 0) {
        SET_VECTOR_ELT(b->holder, H_RESULT, v);
        UNPROTECT(1);
        return;
    }
    node_kind pk = b->kind[d - 1];
    if (pk == N_VALUE && ev->role == ZRD_ROLE_ELEMENT) {
        SEXP parent = slot(b, H_OBJS, d - 1);
        if (TYPEOF(parent) == VECSXP) {
            SET_VECTOR_ELT(parent, (R_xlen_t) ev->index, v);
        } else if (TYPEOF(parent) == STRSXP) {
            if (TYPEOF(v) != CHARSXP) Rf_error("a character vector holds a non-string item");
            SET_STRING_ELT(parent, (R_xlen_t) ev->index, v);
        }
    } else if (pk == N_ATTRCELL && ev->role == ZRD_ROLE_CAR && b->name_off[d] >= 0) {
        int o = b->owner[d - 1];
        SEXP name = PROTECT(Rf_mkCharLenCE(b->text + b->name_off[d], (int) b->name_len[d], CE_NATIVE));
        SEXP cell = PROTECT(Rf_cons(v, slot(b, H_ATTRS, o)));
        SET_TAG(cell, Rf_installTrChar(name));
        set_slot(b, H_ATTRS, o, cell);
        UNPROTECT(2);
    } else if (pk == N_RDACELL && ev->role == ZRD_ROLE_CAR) {
        SEXP vals = VECTOR_ELT(b->holder, H_RDA_VALUES);
        SEXP names = VECTOR_ELT(b->holder, H_RDA_NAMES);
        if (b->rda_n == XLENGTH(vals)) {
            R_xlen_t cap = XLENGTH(vals) ? 2 * XLENGTH(vals) : 8;
            SEXP nv = PROTECT(Rf_allocVector(VECSXP, cap));
            SEXP nn = PROTECT(Rf_allocVector(STRSXP, cap));
            for (R_xlen_t k = 0; k < b->rda_n; k++) {
                SET_VECTOR_ELT(nv, k, VECTOR_ELT(vals, k));
                SET_STRING_ELT(nn, k, STRING_ELT(names, k));
            }
            SET_VECTOR_ELT(b->holder, H_RDA_VALUES, nv);
            SET_VECTOR_ELT(b->holder, H_RDA_NAMES, nn);
            UNPROTECT(2);
            vals = nv;
            names = nn;
        }
        SET_VECTOR_ELT(vals, (R_xlen_t) b->rda_n, v);
        SET_STRING_ELT(names, (R_xlen_t) b->rda_n,
                       b->name_off[d] >= 0
                           ? Rf_mkCharLenCE(b->text + b->name_off[d], (int) b->name_len[d], CE_NATIVE)
                           : R_BlankString);
        b->rda_n++;
    } else if (pk == N_STATECELL && ev->role == ZRD_ROLE_CAR) {
        /* A deferred string's state is (vector . info), and a wrapper's
         * (vector . metadata): keep the vector. */
        set_slot(b, H_STATES, b->owner[d - 1], v);
    } else if (pk == N_ALTREP && ev->role == ZRD_ROLE_ALTREP_STATE) {
        set_slot(b, H_STATES, d - 1, v);
    }
    UNPROTECT(1);
}

static int on_end(zrd_builder *b, const zrd_event *ev)
{
    int d = ev->depth;
    if (b->stop) return 1;
    if (b->skip_depth >= 0) {
        if (d > b->skip_depth) return 0;
        b->skip_depth = -1;
        if (b->kind[d] == N_IGNORE) return 0;
    }
    SEXP v;
    switch (b->kind[d]) {
    case N_ATTRCELL: case N_RDACELL: case N_STATECELL: case N_IGNORE:
        return 0;
    case N_SKIP:
        v = R_NilValue;
        break;
    case N_ALTREP: {
        int refused;
        v = PROTECT(materialise(b, d, &refused));
        if (refused) {
            if (refuse(b, ev)) { UNPROTECT(1); return b->stop = STOP_NOMEM; }
        } else {
            apply_attrs(b, d, v);
        }
        UNPROTECT(1);
        break;
    }
    case N_VALUE:
    default:
        if (ev->type == ZRD_CHARSXP) {
            v = make_char(b, ev);
            if (b->stop) return 1;
        } else {
            v = slot(b, H_OBJS, d);
            apply_attrs(b, d, v);
        }
        break;
    }
    attach(b, d, ev, v);
    set_slot(b, H_OBJS, d, R_NilValue);
    return 0;
}

static int on_event(void *ctx, const zrd_event *ev)
{
    zrd_builder *b = ctx;
    zrd_event rel;
    if (b->target >= 0) {
        /* Before the target, only the walk's own reference table matters;
         * the target is built as if it were the root. */
        if (!b->in_target) {
            if (ev->kind != ZRD_EVENT_BEGIN || (double) ev->offset != b->target) return 0;
            b->in_target = 1;
            b->base_depth = ev->depth;
        }
        rel = *ev;
        rel.depth -= b->base_depth;
        if (rel.depth == 0) rel.role = ZRD_ROLE_ROOT;
        ev = &rel;
    }
    if (zrd_scan_event(&b->scan, ev)) return b->stop = STOP_NOMEM;
    int rc = 0;
    if (ev->depth + 1 < b->ndepth)
        rc = ev->kind == ZRD_EVENT_BEGIN ? on_begin(b, ev) : on_end(b, ev);
    if (rc == 0 && b->target >= 0 && ev->kind == ZRD_EVENT_END && ev->depth == 0)
        return b->stop = STOP_DONE;
    return rc;
}

/* Encoding names compared as R's are written: "UTF-8", "utf8" and
 * "UTF8" alike. */
static int same_encoding(const char *a, const char *b)
{
    for (;;) {
        while (*a && !((*a >= '0' && *a <= '9') || ((*a | 32) >= 'a' && (*a | 32) <= 'z'))) a++;
        while (*b && !((*b >= '0' && *b <= '9') || ((*b | 32) >= 'a' && (*b | 32) <= 'z'))) b++;
        if (!*a || !*b) return !*a && !*b;
        if ((*a | 32) != (*b | 32)) return 0;
        a++;
        b++;
    }
}

/* Builds the stream in `src` (after `skip` bytes). `rda` says the root is
 * an .rda's pairlist of objects, built as a named list. `session_encoding`
 * is this session's native encoding, for translating native strings.
 * Returns the walk's result, the value, the refused items and the
 * scanner's findings. */
SEXP zrd_build_walk(SEXP src, SEXP skip, SEXP limits, SEXP rda, SEXP strict,
                    SEXP session_encoding, SEXP target)
{
    zrd_source *s = zrd_source_get(src);
    size_t nskip = (size_t) Rf_asInteger(skip);
    if (zrd_reader_read(&s->reader, NULL, nskip) != nskip)
        Rf_error("internal error: the input is shorter than its magic line");

    zrd_limits lim;
    lim.max_depth = Rf_asInteger(VECTOR_ELT(limits, 0));
    lim.max_items = Rf_asReal(VECTOR_ELT(limits, 1));
    lim.max_size = Rf_asReal(VECTOR_ELT(limits, 2));

    zrd_builder *b = calloc(1, sizeof *b);
    if (b == NULL) Rf_error("cannot allocate the builder");
    SEXP holder = PROTECT(Rf_allocVector(VECSXP, H_N));
    SEXP ptr = PROTECT(R_MakeExternalPtr(b, R_NilValue, holder));
    R_RegisterCFinalizerEx(ptr, builder_finalizer, TRUE);
    b->holder = holder;
    b->rda = Rf_asLogical(rda) == TRUE;
    b->target = Rf_asReal(target);
    if (b->target >= 0) b->rda = 0;
    b->strict = Rf_asLogical(strict) != FALSE;
    b->skip_depth = -1;
    b->ndepth = lim.max_depth + 2;
    size_t nd = (size_t) b->ndepth;
    b->kind = calloc(nd, sizeof *b->kind);
    b->owner = calloc(nd, sizeof *b->owner);
    b->name_off = calloc(nd, sizeof *b->name_off);
    b->name_len = calloc(nd, sizeof *b->name_len);
    if (!b->kind || !b->owner || !b->name_off || !b->name_len)
        Rf_error("cannot allocate the builder");
    for (int j = H_OBJS; j <= H_STATES; j++)
        SET_VECTOR_ELT(holder, j, Rf_allocVector(VECSXP, b->ndepth));
    SET_VECTOR_ELT(holder, H_RDA_VALUES, Rf_allocVector(VECSXP, 0));
    SET_VECTOR_ELT(holder, H_RDA_NAMES, Rf_allocVector(STRSXP, 0));
    if (zrd_scan_init(&b->scan, &b->w, lim.max_depth)) Rf_error("cannot allocate the builder");

    zrd_status st = zrd_walk_init(&b->w, &s->reader, &lim, on_event, b, tick, NULL);
    if (st == ZRD_OK) st = zrd_walk_capture_strings(&b->w, 4096);
    b->w.payload = on_payload;
    b->w.payload_ctx = b;
    if (st == ZRD_OK) st = zrd_walk_header(&b->w);
    /* Native strings from another encoding (version 3 names it). */
    if (st == ZRD_OK && b->w.header.native_encoding[0]) {
        const char *from = b->w.header.native_encoding;
        const char *here = CHAR(STRING_ELT(session_encoding, 0));
        if (here[0] && !same_encoding(from, here)) {
            void *cd = Riconv_open("UTF-8", from);
            if (cd != (void *) -1) b->iconv = cd;
        }
    }
    if (st == ZRD_OK) st = zrd_walk_item(&b->w);
    if (b->stop == STOP_NOMEM) Rf_error("cannot allocate the builder");
    if (b->stop == STOP_DONE && st == ZRD_STOPPED) st = ZRD_OK;
    if (b->target >= 0 && !b->in_target && st == ZRD_OK) {
        Rf_error("internal error: no item at the extraction offset");
    }

    char path[512];
    zrd_walk_path(&b->w, path, sizeof path);
    SEXP value = VECTOR_ELT(holder, H_RESULT);
    if (b->rda) {
        SEXP vals = VECTOR_ELT(holder, H_RDA_VALUES), nms = VECTOR_ELT(holder, H_RDA_NAMES);
        value = PROTECT(Rf_allocVector(VECSXP, (R_xlen_t) b->rda_n));
        SEXP names = PROTECT(Rf_allocVector(STRSXP, (R_xlen_t) b->rda_n));
        for (R_xlen_t k = 0; k < b->rda_n; k++) {
            SET_VECTOR_ELT(value, k, VECTOR_ELT(vals, k));
            SET_STRING_ELT(names, k, STRING_ELT(nms, k));
        }
        Rf_setAttrib(value, R_NamesSymbol, names);
        UNPROTECT(2);
    }
    PROTECT(value);

    R_xlen_t nr = (R_xlen_t) b->nrefused;
    SEXP ref_type = PROTECT(Rf_allocVector(INTSXP, nr));
    SEXP ref_path = PROTECT(Rf_allocVector(STRSXP, nr));
    for (R_xlen_t k = 0; k < nr; k++) {
        INTEGER(ref_type)[k] = b->refused_type[k];
        SET_STRING_ELT(ref_path, k, b->refused_len[k] == 0 ? R_BlankString
                                    : Rf_mkCharLenCE(b->text + b->refused_off[k],
                                                     (int) b->refused_len[k], CE_NATIVE));
    }

    const char *names[] = {"status", "format", "version", "bytes", "offset", "path",
                           "detail", "value", "refused_type", "refused_path",
                           "findings", "encoding_error", "encoding_path",
                           "encoding_offset", ""};
    SEXP out = PROTECT(Rf_mkNamed(VECSXP, names));
    char fmt[2] = {(char) b->w.header.format, '\0'};
    SET_VECTOR_ELT(out, 0, Rf_mkString(zrd_status_name(st)));
    SET_VECTOR_ELT(out, 1, Rf_mkString(fmt));
    SET_VECTOR_ELT(out, 2, Rf_ScalarInteger(b->w.header.version));
    SET_VECTOR_ELT(out, 3, Rf_ScalarReal((double) (zrd_reader_offset(&s->reader) - b->w.base)));
    SET_VECTOR_ELT(out, 4, Rf_ScalarReal(st == ZRD_OK ? NA_REAL : (double) b->w.err_offset));
    SET_VECTOR_ELT(out, 5, Rf_mkString(path));
    SET_VECTOR_ELT(out, 6, Rf_mkString(st == ZRD_OK ? "" : b->w.err_detail));
    SET_VECTOR_ELT(out, 7, value);
    SET_VECTOR_ELT(out, 8, ref_type);
    SET_VECTOR_ELT(out, 9, ref_path);
    SET_VECTOR_ELT(out, 10, zrd_scan_findings(&b->scan));
    SET_VECTOR_ELT(out, 11, Rf_ScalarLogical(b->stop == STOP_ENCODING));
    SET_VECTOR_ELT(out, 12, Rf_mkString(b->stop_path));
    SET_VECTOR_ELT(out, 13, Rf_ScalarReal((double) b->stop_offset));
    UNPROTECT(6);
    return out;
}
