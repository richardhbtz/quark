#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Reads an entire UTF-8 text file into a heap-allocated buffer.
// Returns a malloc()'d, null-terminated string (possibly empty).
// On failure the returned string is empty; call io_last_error() for details.
char* io_read_file(const char* path);

// Writes the provided UTF-8 text to a file, replacing existing contents.
// Returns 1 on success, 0 on failure.
int io_write_file(const char* path, const char* data);

// Appends the provided UTF-8 text to a file, creating the file if needed.
// Returns 1 on success, 0 on failure.
int io_append_file(const char* path, const char* data);

// Reads a single line from stdin (without the trailing newline).
// Returns a malloc()'d string (possibly empty). On EOF/failure, returns empty string
// and records an error retrievable via io_last_error().
char* io_read_stdin_line(void);

// Reads the remainder of stdin until EOF. Returns a malloc()'d string
// (possibly empty). On failure, returns empty string and records an error.
char* io_read_stdin_all(void);

// Writes the provided UTF-8 text to stdout. Returns 1 on success.
int io_stdout_write(const char* data);

// Writes the provided UTF-8 text to stderr. Returns 1 on success.
int io_stderr_write(const char* data);

// Flush helpers. Return 1 on success.
int io_stdout_flush(void);
int io_stderr_flush(void);

// Frees memory returned by io_read_* helpers.
void io_free(void* buffer);

// Retrieves/clears the thread-local error message set by the most recent IO call.
// The returned pointer is valid until the next IO call on the same thread.
const char* io_last_error(void);
void io_clear_last_error(void);

#ifdef __cplusplus
}
#endif
