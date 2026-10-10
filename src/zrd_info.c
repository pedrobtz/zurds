/* zurds_info()'s entry point, and the proof that zufast links (roadmap
 * Stage 0): a big-endian round trip through its loads and stores, the
 * functions the XDR walk reads every integer with, so a broken LinkingTo
 * fails here rather than in the first stage that needs it. The bytes come
 * from R, so the round trip runs on data the compiler cannot fold away. */
#include <string.h>

#include <zufast.h>

#include "zrd.h"

/* Loads each big-endian word of `probe`, stores it back, and checks the
 * bytes survive and the first word reads as 0xdeadbeef. */
static int zufast_links(SEXP probe)
{
    if (TYPEOF(probe) != RAWSXP || XLENGTH(probe) < 4 || XLENGTH(probe) % 4)
        return 0;
    const uint8_t *in = RAW(probe);
    R_xlen_t n = XLENGTH(probe);
    uint8_t back[4];
    for (R_xlen_t i = 0; i + 4 <= n; i += 4) {
        zuf_store_be32(back, zuf_load_be32(in + i));
        if (memcmp(back, in + i, 4) != 0) return 0;
    }
    return zuf_load_be32(in) == 0xdeadbeefu;
}

SEXP zrd_build_info(SEXP probe)
{
    const char *names[] = {"zufast_version", "zufast_ok", ""};
    SEXP out = PROTECT(Rf_mkNamed(VECSXP, names));
    SET_VECTOR_ELT(out, 0, Rf_mkString(ZUFAST_VERSION));
    SET_VECTOR_ELT(out, 1, Rf_ScalarLogical(zufast_links(probe)));
    UNPROTECT(1);
    return out;
}
