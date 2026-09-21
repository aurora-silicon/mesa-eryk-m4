/* SPDX-License-Identifier: MIT
 * Offline M4 (AGX3b) text-section disassembly using the built Mesa decoder.
 * Link with libagx3b_disasm.a. Supply a section file, start and end offsets.
 * Unknown encodings remain explicit; section padding is not presumed code.
 */
#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "asahi/isa/disasm.h"

int main(int argc, char **argv)
{
    if (argc != 4) return 2;
    char *end;
    errno = 0;
    unsigned long start = strtoul(argv[2], &end, 0);
    if (errno || *end) return 2;
    unsigned long limit = strtoul(argv[3], &end, 0);
    if (errno || *end || limit <= start || limit > UINT_MAX) return 2;
    FILE *file = fopen(argv[1], "rb");
    if (!file) return 2;
    if (fseek(file, 0, SEEK_END)) return 2;
    long length = ftell(file);
    if (length < 0 || limit > (unsigned long)length) return 2;
    rewind(file);
    uint8_t *bytes = malloc(limit);
    if (!bytes || fread(bytes, 1, limit, file) != limit) return 2;
    fclose(file);
    bool errors = false;
    for (unsigned long pos = start; pos < limit;) {
        /* The decoder may inspect a full word: align and zero-pad input. */
        BITSET_WORD words[8] = {0};
        size_t available = limit - pos;
        memcpy(words, bytes + pos, available < sizeof(words) ? available : sizeof(words));
        signed ret = agx3b_disassemble_instr(words, stdout, pos, available, true);
        errors |= ret <= 0;
        unsigned n = ret < 0 ? -ret : ret;
        if (!n || n > available) { errors = true; break; }
        pos += n;
    }
    free(bytes);
    return errors ? 1 : 0;
}
