#include <R_ext/Rdynload.h>
#include <R_ext/Visibility.h>

#include "zrd.h"

static const R_CallMethodDef CallEntries[] = {
    {"zrd_build_info", (DL_FUNC) &zrd_build_info, 1},
    {NULL, NULL, 0}
};

/* The one symbol the shared object exports (design section 13). */
void attribute_visible R_init_zurds(DllInfo *dll)
{
    R_registerRoutines(dll, NULL, CallEntries, NULL, NULL);
    R_useDynamicSymbols(dll, FALSE);
    R_forceSymbols(dll, TRUE);
}
