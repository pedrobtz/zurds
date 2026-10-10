/* See zrd_scan.h. No R headers: part of the standalone build.
 *
 * A finding opens when its item begins, at the item's path. Its detail
 * arrives later, as strings below it: a namespace's name is its first
 * string; an ALTREP class and its package are the first two symbol names
 * in its class information; an S4 class is the first string of its
 * "class" attribute, and the package that attribute's "package"
 * attribute. */
#include <stdlib.h>
#include <string.h>

#include "zrd_scan.h"

enum { ATTR_NONE = 0, ATTR_CLASS, ATTR_PACKAGE, ATTR_OTHER };

/* The S4 bit in an item's levels (Rinternals.h, S4_OBJECT_MASK). */
#define S4_BIT (1 << 4)

int zrd_scan_init(zrd_scan *s, const zrd_walk *w, int max_depth)
{
    memset(s, 0, sizeof *s);
    s->w = w;
    s->ndepth = max_depth + 2;
    size_t n = (size_t) s->ndepth;
    s->type = calloc(n, sizeof *s->type);
    s->finding = calloc(n, sizeof *s->finding);
    s->info_owner = calloc(n, sizeof *s->info_owner);
    s->in_attr = calloc(n, 1);
    s->attr_name = calloc(n, 1);
    if (!s->type || !s->finding || !s->info_owner || !s->in_attr || !s->attr_name)
        return 1;
    return 0;
}

void zrd_scan_free(zrd_scan *s)
{
    free(s->f);
    free(s->text);
    free(s->type);
    free(s->finding);
    free(s->info_owner);
    free(s->in_attr);
    free(s->attr_name);
    memset(s, 0, sizeof *s);
}

static int64_t keep_text(zrd_scan *s, const char *p, size_t n)
{
    if (s->text_cap - s->text_len < n) {
        size_t cap = s->text_cap ? s->text_cap : 1024;
        while (cap - s->text_len < n) cap *= 2;
        char *q = realloc(s->text, cap);
        if (q == NULL) return -1;
        s->text = q;
        s->text_cap = cap;
    }
    if (n) memcpy(s->text + s->text_len, p, n);
    s->text_len += n;
    return (int64_t) (s->text_len - n);
}

/* What an item that begins is a finding of, or -1. */
static int finding_of(const zrd_event *ev, zrd_severity *sev)
{
    switch (ev->type) {
    case ZRD_CLOSXP: *sev = ZRD_SEV_EXECUTES; return ZRD_FIND_CLOSURE;
    case ZRD_PROMSXP: *sev = ZRD_SEV_EXECUTES; return ZRD_FIND_PROMISE;
    case ZRD_BCODESXP: *sev = ZRD_SEV_EXECUTES; return ZRD_FIND_BYTECODE;
    case ZRD_ENVSXP: *sev = ZRD_SEV_REACHES_OUT; return ZRD_FIND_ENVIRONMENT;
    case ZRD_EXTPTRSXP: *sev = ZRD_SEV_REACHES_OUT; return ZRD_FIND_EXTERNAL_POINTER;
    case ZRD_WEAKREFSXP: *sev = ZRD_SEV_REACHES_OUT; return ZRD_FIND_WEAK_REFERENCE;
    case ZRD_NAMESPACESXP: *sev = ZRD_SEV_REACHES_OUT; return ZRD_FIND_NAMESPACE;
    case ZRD_PACKAGESXP: *sev = ZRD_SEV_REACHES_OUT; return ZRD_FIND_PACKAGE_ENVIRONMENT;
    case ZRD_PERSISTSXP: *sev = ZRD_SEV_REACHES_OUT; return ZRD_FIND_PERSISTENT_REFERENCE;
    case ZRD_ALTREP_SXP: *sev = ZRD_SEV_NEEDS_PACKAGE; return ZRD_FIND_ALTREP;
    case ZRD_OBJSXP: *sev = ZRD_SEV_NEEDS_PACKAGE; return ZRD_FIND_S4;
    /* A reference to something already read is that thing again, at
     * another path: each path is where it would be reached. */
    case ZRD_REFSXP:
        *sev = ZRD_SEV_REACHES_OUT;
        switch (ev->ref_type) {
        case ZRD_ENVSXP: return ZRD_FIND_ENVIRONMENT;
        case ZRD_EXTPTRSXP: return ZRD_FIND_EXTERNAL_POINTER;
        case ZRD_WEAKREFSXP: return ZRD_FIND_WEAK_REFERENCE;
        case ZRD_NAMESPACESXP: return ZRD_FIND_NAMESPACE;
        case ZRD_PACKAGESXP: return ZRD_FIND_PACKAGE_ENVIRONMENT;
        case ZRD_PERSISTSXP: return ZRD_FIND_PERSISTENT_REFERENCE;
        default: return -1;
        }
    default:
        /* An S4 object built on a vector carries the S4 bit. */
        if (ev->type < ZRD_ALTREP_SXP && ev->type != ZRD_CHARSXP &&
            (ZRD_LEVELS(ev->flags) & S4_BIT)) {
            *sev = ZRD_SEV_NEEDS_PACKAGE;
            return ZRD_FIND_S4;
        }
        return -1;
    }
}

static int open_finding(zrd_scan *s, const zrd_event *ev, int kind, zrd_severity sev)
{
    if (s->n == s->cap) {
        size_t cap = s->cap ? 2 * s->cap : 32;
        zrd_finding *p = realloc(s->f, cap * sizeof *p);
        if (p == NULL) return 1;
        s->f = p;
        s->cap = cap;
    }
    char path[1024];
    zrd_walk_path(s->w, path, sizeof path);
    int64_t off = keep_text(s, path, strlen(path));
    if (off < 0) return 1;
    zrd_finding *f = &s->f[s->n];
    memset(f, 0, sizeof *f);
    f->kind = (zrd_finding_kind) kind;
    f->severity = sev;
    f->offset = ev->offset;
    f->path_off = (size_t) off;
    f->path_len = strlen(path);
    f->detail_off[0] = f->detail_off[1] = -1;
    s->finding[ev->depth] = (int64_t) s->n++;
    if (sev != ZRD_SEV_NEEDS_PACKAGE) s->risky++;
    return 0;
}

/* Sets the next free detail of finding `i`. */
static int add_detail(zrd_scan *s, int64_t i, int slot, const char *p, size_t n)
{
    zrd_finding *f = &s->f[i];
    if (slot < 0) slot = f->detail_off[0] < 0 ? 0 : f->detail_off[1] < 0 ? 1 : -1;
    if (slot < 0 || f->detail_off[slot] >= 0) return 0;
    int64_t off = keep_text(s, p, n);
    if (off < 0) return 1;
    f->detail_off[slot] = off;
    f->detail_len[slot] = n;
    return 0;
}

static int is_detail(const zrd_scan *s, int64_t i, const char *want, size_t n)
{
    const zrd_finding *f = &s->f[i];
    return f->detail_off[1] >= 0 && f->detail_len[1] == n &&
           memcmp(s->text + f->detail_off[1], want, n) == 0;
}

int zrd_scan_event(zrd_scan *s, const zrd_event *ev)
{
    int d = ev->depth;
    if (d + 1 >= s->ndepth) return 0;

    if (ev->kind == ZRD_EVENT_BEGIN) {
        s->type[d] = ev->type;
        s->finding[d] = -1;
        /* Attribute pairlists: a pairlist's later cells continue its
         * first; a value in an attribute cell knows its tag. */
        if (ev->role != ZRD_ROLE_CELL)
            s->in_attr[d] = ev->role == ZRD_ROLE_ATTR || ev->role == ZRD_ROLE_ALTREP_ATTR;
        s->attr_name[d] = ATTR_NONE;
        if (ev->role == ZRD_ROLE_CAR && d > 0 && s->in_attr[d - 1]) {
            if (ev->name_len == 5 && memcmp(ev->name, "class", 5) == 0)
                s->attr_name[d] = ATTR_CLASS;
            else if (ev->name_len == 7 && memcmp(ev->name, "package", 7) == 0)
                s->attr_name[d] = ATTR_PACKAGE;
            else
                s->attr_name[d] = ATTR_OTHER;
        }
        if (ev->role == ZRD_ROLE_ALTREP_INFO)
            s->info_owner[d] = d > 0 ? s->finding[d - 1] : -1;
        else if (ev->role != ZRD_ROLE_CELL)
            s->info_owner[d] = d > 0 ? s->info_owner[d - 1] : -1;

        zrd_severity sev;
        int kind = finding_of(ev, &sev);
        if (kind >= 0 && open_finding(s, ev, kind, sev)) return s->nomem = 1;
        return 0;
    }

    /* END: strings that complete a finding's detail. */
    const char *data = ev->data;
    size_t len = ev->data_len;
    int is_name = data && ((ev->type == ZRD_CHARSXP && d > 0 && s->type[d - 1] == ZRD_SYMSXP) ||
                           ev->type == ZRD_REFSXP);
    if (is_name && s->info_owner[d] >= 0) {
        if (add_detail(s, s->info_owner[d], -1, data, len)) return s->nomem = 1;
    }
    if (ev->type == ZRD_CHARSXP && data && ev->length >= 0 && d > 0) {
        int pt = s->type[d - 1];
        int64_t owner = s->finding[d - 1];
        if ((pt == ZRD_NAMESPACESXP || pt == ZRD_PACKAGESXP || pt == ZRD_PERSISTSXP) &&
            owner >= 0 && ev->index == 0) {
            if (add_detail(s, owner, 0, data, len)) return s->nomem = 1;
        }
        /* An S4 object's class at depth d - 3, its package at d - 5. */
        if (d >= 3 && s->attr_name[d - 1] == ATTR_CLASS && ev->index == 0) {
            int64_t i = s->finding[d - 3];
            if (i >= 0 && s->f[i].kind == ZRD_FIND_S4 && add_detail(s, i, 0, data, len))
                return s->nomem = 1;
        }
        if (d >= 5 && s->attr_name[d - 1] == ATTR_PACKAGE && s->attr_name[d - 3] == ATTR_CLASS) {
            int64_t i = s->finding[d - 5];
            if (i >= 0 && s->f[i].kind == ZRD_FIND_S4 && add_detail(s, i, 1, data, len))
                return s->nomem = 1;
        }
    }
    /* An ALTREP class of base R's own is data, not a finding: it needs no
     * package, and the data builder materialises it. */
    if (ev->type == ZRD_ALTREP_SXP && s->finding[d] >= 0 &&
        is_detail(s, s->finding[d], "base", 4))
        s->f[s->finding[d]].deleted = 1;
    return 0;
}
