/* The scanner (design sections 5 and 8): reports, from the walk's events,
 * every item that can run code, reach outside the file, or needs a package
 * to load. R-free, like the walk; zrd_scan_r.c and zrd_toc.c feed it. */
#ifndef ZRD_SCAN_H
#define ZRD_SCAN_H

#include <stddef.h>
#include <stdint.h>

#include "zrd_walk.h"

/* Finding kinds, in the order R/scan.R names them. */
typedef enum {
    ZRD_FIND_CLOSURE = 0, ZRD_FIND_PROMISE, ZRD_FIND_ENVIRONMENT,
    ZRD_FIND_BYTECODE, ZRD_FIND_EXTERNAL_POINTER, ZRD_FIND_WEAK_REFERENCE,
    ZRD_FIND_NAMESPACE, ZRD_FIND_PACKAGE_ENVIRONMENT, ZRD_FIND_ALTREP,
    ZRD_FIND_S4, ZRD_FIND_PERSISTENT_REFERENCE
} zrd_finding_kind;

typedef enum {
    ZRD_SEV_EXECUTES = 0, ZRD_SEV_REACHES_OUT, ZRD_SEV_NEEDS_PACKAGE
} zrd_severity;

typedef struct {
    zrd_finding_kind kind;
    zrd_severity severity;
    uint64_t offset;
    size_t path_off, path_len;
    /* Up to two strings: a namespace's name; an ALTREP class and its
     * package; an S4 class and its package. */
    int64_t detail_off[2];
    size_t detail_len[2];
    int deleted;          /* a base ALTREP class: not a finding */
} zrd_finding;

typedef struct zrd_scan {
    const zrd_walk *w;
    zrd_finding *f;
    size_t n, cap;
    char *text;
    size_t text_len, text_cap;
    size_t risky;         /* findings that execute or reach out */
    int nomem;
    /* By depth, max_depth + 2 entries each. */
    int ndepth;
    int *type;
    int64_t *finding;     /* the finding an item at this depth opened */
    int64_t *info_owner;  /* inside an ALTREP item's class info: its finding */
    unsigned char *in_attr;   /* a cell of an attribute pairlist */
    unsigned char *attr_name; /* a value in an attribute pairlist: which */
} zrd_scan;

/* Sets up `s` for a walk of at most `max_depth`; returns non-zero out of
 * memory. Free with zrd_scan_free(), on success or failure. */
int zrd_scan_init(zrd_scan *s, const zrd_walk *w, int max_depth);
void zrd_scan_free(zrd_scan *s);
/* Feeds one event; returns non-zero out of memory. */
int zrd_scan_event(zrd_scan *s, const zrd_event *ev);

#endif /* ZRD_SCAN_H */
