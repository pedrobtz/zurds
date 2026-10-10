/* Declarations shared by the R-facing translation units. Internal symbols
 * carry the zrd_ / ZRD_ prefix (design section 3). */
#ifndef ZRD_H
#define ZRD_H

#define R_NO_REMAP
#include <R.h>
#include <Rinternals.h>

SEXP zrd_build_info(void);

#endif /* ZRD_H */
