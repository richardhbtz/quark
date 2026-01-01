#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Create a new WebSocket client instance. Returns a positive handle (int) or 0 on failure.
int ws_create();

// Connect the WebSocket handle to a URL with optional headers (array of "Name: value").
// Returns 1 if the connection attempt started successfully (asynchronous), 0 on error.
int ws_connect(int handle, const char* url, const char* const* headers, int header_count);

// Connect without headers.
int ws_connect_simple(int handle, const char* url);

// Returns 1 if the socket is currently open, else 0.
int ws_is_open(int handle);

// Send a UTF-8 text message. Returns 1 on success, 0 on failure.
int ws_send_text(int handle, const char* text);

// Receive the next text message. Blocks up to timeout_ms (<=0 means non-blocking poll).
// Returns a malloc-allocated UTF-8 string; empty string on timeout/none. Free with ws_free().
char* ws_recv(int handle, int timeout_ms);

// Close the socket with code and reason. No-op if already closed.
void ws_close(int handle, int code, const char* reason);

// Destroy and free the client. Implicitly closes if open.
void ws_destroy(int handle);

// Last error string for this handle; pointer valid until next ws_* call on this handle.
const char* ws_last_error(int handle);

// Last close code observed for this handle (e.g., 1000 normal closure). 0 if none.
int ws_last_close_code(int handle);

// Clear the last error for this handle.
void ws_clear_last_error(int handle);

// Free strings returned by ws_recv().
void ws_free(char* s);

// Get current time in milliseconds (monotonic clock for timing purposes).
int time_ms();

#ifdef __cplusplus
}
#endif

