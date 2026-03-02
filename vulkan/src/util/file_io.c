/**
 * file_io.c
 *
 * Simple file loading for JSON text and SPIR-V binary blobs.
 */

#include "file_io.h"

#include <stdio.h>
#include <stdlib.h>

char *file_io_read_text(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "[file_io] Cannot open: %s\n", path);
        return NULL;
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (size < 0) {
        fclose(f);
        return NULL;
    }

    char *buf = malloc((size_t)size + 1);
    if (!buf) {
        fclose(f);
        return NULL;
    }

    size_t read = fread(buf, 1, (size_t)size, f);
    fclose(f);

    buf[read] = '\0';
    return buf;
}

uint8_t *file_io_read_binary(const char *path, size_t *outSize)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "[file_io] Cannot open: %s\n", path);
        return NULL;
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (size < 0) {
        fclose(f);
        return NULL;
    }

    uint8_t *buf = malloc((size_t)size);
    if (!buf) {
        fclose(f);
        return NULL;
    }

    size_t read = fread(buf, 1, (size_t)size, f);
    fclose(f);

    if (outSize) *outSize = read;
    return buf;
}
