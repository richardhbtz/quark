#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Perform HTTP GET request. Returns a heap-allocated UTF-8 string with the body.
// Caller is responsible for freeing with free(). Returns empty string on error.
char* http_get(const char* url);

// Perform HTTP POST request with UTF-8 body and content type (e.g., "application/json").
// Returns a heap-allocated UTF-8 string with the body. Caller frees with free().
char* http_post(const char* url, const char* body, const char* content_type);

// Free memory allocated by http_get() or http_post().
void http_free(char* response);

// Cleanup HTTP subsystem resources. Call at program exit.
void http_cleanup();

// ---------------- Extended / Production-style API ----------------
// Generic request supporting arbitrary method, optional body/content type, custom headers,
// per-call timeout override (seconds; <=0 uses default), and header array.
// headers: array of C strings formatted as "Name: value" (may be NULL if header_count==0)
// header_count: number of elements in headers.
// Returns malloc'd body (empty string on failure). Use http_free().
char* http_request_ex(const char* method,
					  const char* url,
					  const char* body,
					  const char* content_type,
					  const char* const* headers,
					  int header_count,
					  long timeout_seconds);

// Get the HTTP status code (e.g., 200) for the last successful/attempted call in this thread.
// Returns 0 if no request has been made or if request failed before obtaining a status.
int http_last_status();

// Get a descriptive error string for the last request in this thread, or empty string if none.
// Pointer is valid until next HTTP call or http_clear_last_error(). Do not free it.
const char* http_last_error();

// Clear the stored last error for this thread.
void http_clear_last_error();

// Override the global maximum response size (bytes) for subsequent requests.
// By default this is 2MB; you can increase (e.g., to 8 * 1024 * 1024).
void http_set_max_response_size(size_t bytes);

#ifdef __cplusplus
}
#endif
