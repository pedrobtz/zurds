#include <R_ext/Rdynload.h>
#include <R_ext/Visibility.h>

#include "zrd.h"

static const R_CallMethodDef CallEntries[] = {
    {"zrd_build_info",    (DL_FUNC) &zrd_build_info,    1},
    {"zrd_source_new",    (DL_FUNC) &zrd_source_new,    1},
    {"zrd_source_peek",   (DL_FUNC) &zrd_source_peek,   2},
    {"zrd_source_drain",  (DL_FUNC) &zrd_source_drain,  1},
    {"zrd_walk_stats",    (DL_FUNC) &zrd_walk_stats,    3},
    {"zrd_walk_trace",    (DL_FUNC) &zrd_walk_trace,    5},
    {"zrd_walk_head",     (DL_FUNC) &zrd_walk_head,     2},
    {NULL, NULL, 0}
};

/* The one symbol the shared object exports (design section 13). */
void attribute_visible R_init_zurds(DllInfo *dll)
{
    R_registerRoutines(dll, NULL, CallEntries, NULL, NULL);
    R_useDynamicSymbols(dll, FALSE);
    R_forceSymbols(dll, TRUE);
}
