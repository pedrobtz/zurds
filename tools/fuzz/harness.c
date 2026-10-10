/* libFuzzer target for the R-free code (roadmap Stage 10; design section
 * 19, criterion 4): the walk, the scanner and the payload decoding the
 * data builder uses, over any bytes, as a stream or an .rda; and the zlib
 * decoder for lazy-load entries over the same bytes. Limits are small so a
 * long claimed vector stops at max_size, not at the allocator.
 *
 * Built by r-actions' fuzz.yml with libFuzzer, and by tools/run-fuzz-canary
 * with tools/fuzz/driver.c. With -DZRD_FUZZ_CANARY it crashes on an input
 * that starts "ZRDCANRY", so the gate can be seen to fail. */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "zrd_inflate.h"
#include "zrd_reader.h"
#include "zrd_scan.h"
#include "zrd_walk.h"

#define PAYLOAD_CAP (1 << 20)

static void *payload(void *ctx, const zrd_event *ev)
{
    /* Decode every vector and string into one scratch buffer, as the
     * builder decodes into R's memory. */
    static uint8_t buf[PAYLOAD_CAP];
    (void) ctx;
    uint64_t size = ev->type == ZRD_CPLXSXP ? 16 : ev->type == ZRD_REALSXP ? 8 :
                    ev->type == ZRD_RAWSXP || ev->type == ZRD_CHARSXP ? 1 : 4;
    if (ev->length < 0 || (uint64_t) ev->length > PAYLOAD_CAP / size) return NULL;
    return buf;
}

static int on_event(void *ctx, const zrd_event *ev)
{
    return zrd_scan_event(ctx, ev);
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
#ifdef ZRD_FUZZ_CANARY
    if (size >= 8 && memcmp(data, "ZRDCANRY", 8) == 0) __builtin_trap();
#endif
    size_t skip = size >= 5 && data[0] == 'R' && data[1] == 'D' && data[4] == '\n' ? 5 : 0;
    zrd_reader r;
    zrd_reader_init_mem(&r, data + skip, size - skip);
    zrd_limits lim = {64, 1e5, 4.0 * 1024 * 1024};
    zrd_walk w;
    zrd_scan scan;
    if (zrd_scan_init(&scan, &w, lim.max_depth) == 0) {
        zrd_status st = zrd_walk_init(&w, &r, &lim, on_event, &scan, NULL, NULL);
        w.capture_ints = 1;
        w.payload = payload;
        if (st == ZRD_OK) st = zrd_walk_capture_strings(&w, 256);
        if (st == ZRD_OK) st = zrd_walk_header(&w);
        if (st == ZRD_OK) st = zrd_walk_item(&w);
        char path[256];
        zrd_walk_path(&w, path, sizeof path);
        zrd_walk_free(&w);
    }
    zrd_scan_free(&scan);
    /* The same bytes as a lazy-load zlib entry: the first two bytes give
     * the length it declares, so short and long claims are both tried. */
    static uint8_t out[1 << 16];
    if (size >= 2) {
        size_t want = ((size_t) data[0] << 8 | data[1]) % sizeof out;
        size_t produced;
        zrd_inflate_zlib(data + 2, size - 2, out, want, &produced);
    }
    return 0;
}
