/* zurds_info()'s entry point, and the proof that zufast links (roadmap
 * Stage 0): a big-endian round trip through its loads and stores, the
 * functions the XDR walk reads every integer with, so a broken LinkingTo
 * fails here rather than in the first stage that needs it. */
#include <string.h>

#include <zufast.h>

#include "zrd.h"

static int zufast_links(void)
{
    static const uint8_t word[4] = {0xde, 0xad, 0xbe, 0xef};
    uint8_t back[4];
    uint32_t v = zuf_load_be32(word);
    zuf_store_be32(back, v);
    return v == 0xdeadbeefu && memcmp(back, word, 4) == 0;
}

SEXP zrd_build_info(void)
{
    const char *names[] = {"zufast_version", "zufast_ok", ""};
    SEXP out = PROTECT(Rf_mkNamed(VECSXP, names));
    SET_VECTOR_ELT(out, 0, Rf_mkString(ZUFAST_VERSION));
    SET_VECTOR_ELT(out, 1, Rf_ScalarLogical(zufast_links()));
    UNPROTECT(1);
    return out;
}
