/**
 * file_io.h
 *
 * File loading utilities: text files (JSON) and binary files (SPIR-V).
 */

#ifndef FILE_IO_H
#define FILE_IO_H

#include <stddef.h>
#include <stdint.h>

/**
 * Read an entire file as a null-terminated string.
 * Caller must free() the returned buffer.
 * Returns NULL on failure.
 */
char *file_io_read_text(const char *path);

/**
 * Read an entire file as binary data.
 * Caller must free() the returned buffer.
 *
 * @param path     File path
 * @param outSize  Receives the file size in bytes
 * @return Pointer to file data, or NULL on failure.
 */
uint8_t *file_io_read_binary(const char *path, size_t *outSize);

#endif /* FILE_IO_H */
