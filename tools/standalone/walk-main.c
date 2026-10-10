/* The walk with no R (roadmap Stage 2): reads an uncompressed stream, or an
 * .rda with its magic line, and walks it. With --prefixes it also walks
 * every proper prefix of the input, which must fail cleanly. Built by
 * tools/run-standalone with -DZRD_STANDALONE and the sanitizers.
 *
 * Exit status: 0 walked; 2 a type not walked yet; 1 anything else. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "zrd_reader.h"
#include "zrd_walk.h"

static zrd_status walk(const uint8_t *data, size_t len, double *items, uint64_t *bytes)
{
    zrd_reader r;
    zrd_walk w;
    zrd_limits lim = {512, 1e8, 16.0 * 1024 * 1024 * 1024};
    size_t skip = 0;
    if (len >= 5 && memcmp(data, "RD", 2) == 0 && data[4] == '\n') skip = 5;
    zrd_reader_init_mem(&r, data + (len < skip ? len : skip), len < skip ? 0 : len - skip);
    zrd_status st = zrd_walk_init(&w, &r, &lim, NULL, NULL, NULL, NULL);
    if (st == ZRD_OK) st = zrd_walk_header(&w);
    if (st == ZRD_OK) st = zrd_walk_item(&w);
    if (items) *items = w.items;
    if (bytes) *bytes = zrd_reader_offset(&r) - w.base;
    zrd_walk_free(&w);
    return st;
}

int main(int argc, char **argv)
{
    int prefixes = argc > 2 && strcmp(argv[1], "--prefixes") == 0;
    const char *path = argv[argc - 1];
    if (argc < 2) {
        fprintf(stderr, "usage: walk [--prefixes] FILE\n");
        return 1;
    }
    FILE *f = fopen(path, "rb");
    if (f == NULL) { perror(path); return 1; }
    size_t cap = 1 << 16, len = 0, got;
    uint8_t *data = malloc(cap);
    while (data && (got = fread(data + len, 1, cap - len, f)) > 0) {
        len += got;
        if (len == cap) data = realloc(data, cap *= 2);
    }
    fclose(f);
    if (data == NULL) { fprintf(stderr, "out of memory\n"); return 1; }

    double items;
    uint64_t bytes;
    zrd_status st = walk(data, len, &items, &bytes);
    printf("%s: %s, %.0f items, %llu bytes\n", path, zrd_status_name(st), items,
           (unsigned long long) bytes);
    int rc = st == ZRD_OK ? 0 : st == ZRD_ERR_UNSUPPORTED ? 2 : 1;

    /* Every proper prefix: the walk must stop with an error, never read
     * past the end. A prefix may also end inside an unsupported item. */
    if (prefixes && st == ZRD_OK) {
        for (size_t n = 0; n < len; n++) {
            uint8_t *copy = malloc(n ? n : 1);
            if (copy == NULL) { fprintf(stderr, "out of memory\n"); return 1; }
            memcpy(copy, data, n);
            zrd_status pst = walk(copy, n, NULL, NULL);
            free(copy);
            if (pst == ZRD_OK) {
                printf("%s: prefix of %zu bytes walked as complete\n", path, n);
                rc = 1;
            }
        }
    }
    free(data);
    return rc;
}
