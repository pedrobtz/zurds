/* The walk (design sections 4 and 6): one iterative pass over R's
 * serialization grammar with an explicit stack, emitting an event as each
 * item begins and ends, and building nothing. It never calls R and never
 * allocates an R object: it compiles with -DZRD_STANDALONE and links no R.
 *
 * Every rule names the function of R's src/main/serialize.c it mirrors
 * (wch/r-source a97c000, 2026-10-08), so a change in R is found by diffing
 * that file (design section 14). */
#ifndef ZRD_WALK_H
#define ZRD_WALK_H

#include <stddef.h>
#include <stdint.h>

#include "zrd_reader.h"

/* SEXPTYPEs, as Rinternals.h numbers them; R's headers are not used here. */
enum {
    ZRD_NILSXP = 0, ZRD_SYMSXP = 1, ZRD_LISTSXP = 2, ZRD_CLOSXP = 3,
    ZRD_ENVSXP = 4, ZRD_PROMSXP = 5, ZRD_LANGSXP = 6, ZRD_SPECIALSXP = 7,
    ZRD_BUILTINSXP = 8, ZRD_CHARSXP = 9, ZRD_LGLSXP = 10, ZRD_INTSXP = 13,
    ZRD_REALSXP = 14, ZRD_CPLXSXP = 15, ZRD_STRSXP = 16, ZRD_DOTSXP = 17,
    ZRD_VECSXP = 19, ZRD_EXPRSXP = 20, ZRD_BCODESXP = 21, ZRD_EXTPTRSXP = 22,
    ZRD_WEAKREFSXP = 23, ZRD_RAWSXP = 24, ZRD_OBJSXP = 25
};

/* The pseudo-types 238-255 (serialize.c, "Administrative SXP values"). */
enum {
    ZRD_REFSXP = 255, ZRD_NILVALUE_SXP = 254, ZRD_GLOBALENV_SXP = 253,
    ZRD_UNBOUNDVALUE_SXP = 252, ZRD_MISSINGARG_SXP = 251,
    ZRD_BASENAMESPACE_SXP = 250, ZRD_NAMESPACESXP = 249,
    ZRD_PACKAGESXP = 248, ZRD_PERSISTSXP = 247, ZRD_CLASSREFSXP = 246,
    ZRD_GENERICREFSXP = 245, ZRD_BCREPDEF = 244, ZRD_BCREPREF = 243,
    ZRD_EMPTYENV_SXP = 242, ZRD_BASEENV_SXP = 241, ZRD_ATTRLANGSXP = 240,
    ZRD_ATTRLISTSXP = 239, ZRD_ALTREP_SXP = 238
};

/* The pairlist family, read cell by cell (serialize.c,
 * ReadItem_Iterative). */
#define ZRD_IS_PAIRLIST(type) ((type) == ZRD_LISTSXP || (type) == ZRD_LANGSXP || \
    (type) == ZRD_CLOSXP || (type) == ZRD_PROMSXP || (type) == ZRD_DOTSXP)

/* Flag packing (serialize.c, PackFlags / UnpackFlags). */
#define ZRD_TYPE(flags)     ((flags) & 255)
#define ZRD_LEVELS(flags)   ((int) ((unsigned) (flags) >> 12))
#define ZRD_IS_OBJECT(flags) (((flags) >> 8) & 1)
#define ZRD_HAS_ATTR(flags) (((flags) >> 9) & 1)
#define ZRD_HAS_TAG(flags)  (((flags) >> 10) & 1)

typedef enum {
    ZRD_OK = 0,
    ZRD_STOPPED,            /* the event callback asked to stop */
    ZRD_ERR_TRUNCATED,      /* the stream ended inside an item */
    ZRD_ERR_FORMAT,         /* bad header: format character, version */
    ZRD_ERR_UNSUPPORTED,    /* valid, but not walked yet */
    ZRD_ERR_PARSE,          /* malformed: bad length, unknown type, ... */
    ZRD_ERR_BAD_REF,        /* REFSXP index outside the table */
    ZRD_ERR_DEPTH_LIMIT,
    ZRD_ERR_ITEM_LIMIT,
    ZRD_ERR_SIZE_LIMIT,
    ZRD_ERR_NOMEM
} zrd_status;

/* The stream formats (serialize.c, InFormat). */
typedef enum { ZRD_FORMAT_XDR = 'X', ZRD_FORMAT_BINARY = 'B',
               ZRD_FORMAT_ASCII = 'A' } zrd_format;

typedef struct zrd_header {
    int format;               /* a zrd_format */
    int version;              /* 2 or 3 */
    int writer_version;       /* packed as R_Version(): 65536 v + 256 p + s */
    int min_reader_version;
    char native_encoding[64]; /* version 3 only; "" otherwise */
} zrd_header;

typedef struct zrd_limits {
    int max_depth;
    double max_items;
    double max_size;          /* decompressed bytes */
} zrd_limits;

/* Where an item sits in its parent. */
typedef enum {
    ZRD_ROLE_ROOT = 0,
    ZRD_ROLE_ELEMENT,     /* element `index` of a STRSXP, VECSXP, EXPRSXP */
    ZRD_ROLE_ATTR,        /* the attribute pairlist */
    ZRD_ROLE_TAG,         /* a pairlist cell's tag */
    ZRD_ROLE_CAR,         /* a pairlist cell's value; `name` is its tag */
    ZRD_ROLE_CDR,         /* the end of a pairlist: not itself a cell */
    ZRD_ROLE_CELL,        /* pairlist cell `index` after the first */
    ZRD_ROLE_PRINTNAME,   /* a symbol's name */
    ZRD_ROLE_ENCLOS,      /* an environment's enclosure, */
    ZRD_ROLE_FRAME,       /* frame, */
    ZRD_ROLE_HASHTAB,     /* and hash table */
    ZRD_ROLE_PROT,        /* an external pointer's protected value */
    ZRD_ROLE_EXTTAG,      /* and tag */
    ZRD_ROLE_ALTREP_INFO, /* an ALTREP item's class information, */
    ZRD_ROLE_ALTREP_STATE,/* state, */
    ZRD_ROLE_ALTREP_ATTR, /* and attributes */
    ZRD_ROLE_BC_CODE,     /* bytecode's code vector */
    ZRD_ROLE_BC_CONST,    /* bytecode constant `index` */
    ZRD_ROLE_BC_LANG      /* an item inside a bytecode language object */
} zrd_role;

typedef enum { ZRD_EVENT_BEGIN = 1, ZRD_EVENT_END = 2 } zrd_event_kind;

/* The longest integer vector whose values an END event carries: enough
 * for dim and compact row names. */
#define ZRD_INT_CAP 8

typedef struct zrd_event {
    zrd_event_kind kind;
    int type;             /* ZRD_TYPE(flags) */
    int flags;
    int64_t length;       /* vectors and CHARSXP; -1 for NA_STRING and others */
    uint64_t offset;      /* BEGIN: of the flags; END: just past the item */
    int depth;            /* 0 for the root */
    zrd_role role;
    int64_t index;        /* ELEMENT and CELL: 0-based; CAR: its cell's */
    const char *name;     /* CAR of a tagged cell: the tag, not 0-terminated */
    size_t name_len;
    int64_t ref;          /* REFSXP: the 1-based index it refers to */
    int ref_type;         /* REFSXP: what that entry is (SYMSXP, ENVSXP, ...) */
    const int32_t *ints;  /* INTSXP and LGLSXP END, when ints are captured */
    int nints;            /* and the vector has at most ZRD_INT_CAP: its
                             values; NA_integer_ as INT32_MIN */
    const double *reals;  /* REALSXP END, likewise: an ALTREP compact */
    int nreals;           /* sequence's state, for its length */
    const char *data;     /* CHARSXP END, when strings are captured: the */
    size_t data_len;      /* string's first bytes, not 0-terminated, and */
    int data_complete;    /* whether that is all of it. REFSXP END to a
                             symbol: the symbol's name, always. */
} zrd_event;

/* Return non-zero to stop the walk with ZRD_STOPPED. */
typedef int (*zrd_event_fn)(void *ctx, const zrd_event *ev);

/* Called after the BEGIN event of an atomic vector or a CHARSXP: returns
 * where to decode its payload, or NULL to skip it. A vector's values go
 * as int32_t (LGLSXP, INTSXP; NA as INT32_MIN), double (REALSXP; R's NA
 * keeps its bit pattern), pairs of doubles (CPLXSXP) or bytes (RAWSXP), in
 * the host's byte order; a CHARSXP's bytes as they are, escapes decoded. */
typedef void *(*zrd_payload_fn)(void *ctx, const zrd_event *ev);

/* R's NA_real_: a NaN with payload 1954 (arithmetic.c). */
#define ZRD_NA_REAL_BITS UINT64_C(0x7FF00000000007A2)
/* Called every 65,536 items and between blocks of a long skip; R's side
 * checks for interrupts here. */
typedef void (*zrd_tick_fn)(void *ctx);

typedef struct zrd_frame zrd_frame;

typedef struct zrd_ref {
    int type;             /* SYMSXP, ENVSXP, ... */
    size_t name_off;      /* SYMSXP: its name in the arena */
    size_t name_len;
} zrd_ref;

typedef struct zrd_walk {
    zrd_reader *in;
    zrd_limits limits;
    zrd_header header;
    zrd_event_fn on_event;
    void *event_ctx;
    zrd_tick_fn tick;
    void *tick_ctx;
    zrd_payload_fn payload;
    void *payload_ctx;

    zrd_frame *stack;     /* max_depth + 1 frames, allocated once */
    int depth;            /* frames in use */

    zrd_ref *refs;        /* the reference table, 1-based in the stream */
    size_t nrefs, refs_cap;
    char *arena;          /* symbol names */
    size_t arena_len, arena_cap;

    int big_endian;       /* the binary format: the writer's byte order */
    int capture_ints;     /* deliver short integer and double vectors' values */

    /* Strings delivered with CHARSXP END events, when string_cap > 0: the
     * first string_cap bytes of each, decoded. */
    size_t string_cap;
    char *string_buf;
    size_t string_len;    /* bytes in string_buf for the current string */
    int string_complete;  /* the whole string fitted */

    double items;         /* items begun */
    uint64_t item_offset; /* where the item being begun starts */
    uint64_t base;        /* reader offset where the stream began */

    /* Set on failure. err_offset is where the item at fault starts, or
     * for a truncated stream where it ends. */
    zrd_status status;
    uint64_t err_offset;
    char err_detail[160];
} zrd_walk;

/* Sets up `w` over `in`. Allocates the stack; returns ZRD_ERR_NOMEM on
 * failure. Free with zrd_walk_free(), on success or failure. */
zrd_status zrd_walk_init(zrd_walk *w, zrd_reader *in, const zrd_limits *lim,
                         zrd_event_fn on_event, void *event_ctx,
                         zrd_tick_fn tick, void *tick_ctx);
void zrd_walk_free(zrd_walk *w);
/* Asks for the first `cap` bytes of every string with its END event. Call
 * after zrd_walk_init(); returns ZRD_ERR_NOMEM on failure. */
zrd_status zrd_walk_capture_strings(zrd_walk *w, size_t cap);

/* Reads the stream header (serialize.c, R_Unserialize up to ReadItem). */
zrd_status zrd_walk_header(zrd_walk *w);
/* Walks one complete item, the stream's root (serialize.c, ReadItem). */
zrd_status zrd_walk_item(zrd_walk *w);

/* The item path at the point of failure, as "[[2]]$a@attr$names", into
 * `buf`; "" at the root. Vector elements are named by position: a list's
 * names follow its elements in the stream. Pairlist values are named by
 * their tags, which precede them. */
void zrd_walk_path(const zrd_walk *w, char *buf, size_t cap);

const char *zrd_status_name(zrd_status st);

#endif /* ZRD_WALK_H */
