/* The R side of the walk's input: see zrd_source.c. */
#ifndef ZRD_SOURCE_H
#define ZRD_SOURCE_H

#include <R_ext/Connections.h>

#include "zrd_reader.h"

typedef struct zrd_source {
    zrd_reader reader;
    Rconnection con;   /* NULL for a raw vector */
    uint8_t *block;    /* the reader's block, for a connection */
} zrd_source;

zrd_source *zrd_source_get(SEXP ptr);

#endif /* ZRD_SOURCE_H */
