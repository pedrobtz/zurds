/* Declarations shared by the R-facing translation units. Internal symbols
 * carry the zrd_ / ZRD_ prefix (design section 3). */
#ifndef ZRD_H
#define ZRD_H

#define R_NO_REMAP
#include <R.h>
#include <Rinternals.h>

SEXP zrd_build_info(SEXP probe);
SEXP zrd_source_new(SEXP x);
SEXP zrd_source_peek(SEXP ptr, SEXP n);
SEXP zrd_source_drain(SEXP ptr);
SEXP zrd_walk_stats(SEXP src, SEXP skip, SEXP limits);
SEXP zrd_walk_trace(SEXP src, SEXP skip, SEXP limits, SEXP max_depth, SEXP max_events);
SEXP zrd_walk_head(SEXP src, SEXP skip);
SEXP zrd_toc_walk(SEXP src, SEXP skip, SEXP limits);
SEXP zrd_scan_walk(SEXP src, SEXP skip, SEXP limits);

#endif /* ZRD_H */
