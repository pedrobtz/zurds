/* Runs the fuzz target once on each file named, without libFuzzer: the
 * fuzz canary (tools/run-fuzz-canary) and replaying a finding. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

int main(int argc, char **argv)
{
    for (int i = 1; i < argc; i++) {
        FILE *f = fopen(argv[i], "rb");
        if (f == NULL) { perror(argv[i]); return 2; }
        size_t cap = 1 << 16, len = 0, got;
        uint8_t *buf = malloc(cap);
        while (buf && (got = fread(buf + len, 1, cap - len, f)) > 0) {
            len += got;
            if (len == cap) buf = realloc(buf, cap *= 2);
        }
        fclose(f);
        if (buf == NULL) return 2;
        LLVMFuzzerTestOneInput(buf, len);
        free(buf);
    }
    return 0;
}
